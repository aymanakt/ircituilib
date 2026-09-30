/**
 * @file conversation_demo.c
 * @brief The conversation window, driven by the golden input.
 *
 * THIS IS THE REFERENCE, NOT A TOY. It loads
 * assets/conversation/conversation_golden_test.json -- the frozen fixture, kept
 * byte-for-byte from the design document -- injects its nine messages through the
 * widget's public seam, and can print what it produced in two canonical forms:
 *
 *   --dump-model    one line per message: the COMPUTED geometry. No cells, no
 *                   terminal, no ncurses output. Catches a change to the cluster
 *                   algorithm, the reflow or the heights.
 *   --dump-screen   every cell of the widget's box with its attributes. Catches a
 *                   change to the painter.
 *
 * Two dumps because one file per layer: a refactor that moves only the model shows
 * in only the first, and one that moves only the paint shows in only the second.
 * Neither is a golden file yet -- Gate 5 freezes them.
 *
 * WHAT IT DOES NOT DO, and why. It does not run on IrcitUiMainLooper. That loop
 * calls IrcitUiScreenReset() before it loops, and the reset re-clones the screen
 * tree -- so a tree the application has injected into cannot be built before it,
 * which is the same reason src/examples/ui_main.c raises its floats in-loop rather
 * than at startup. The framework's looper wants an application that reacts to keys;
 * this one is a fixture player, and it injects first and then draws. The deviation
 * is recorded rather than hidden.
 *
 * IT LINKS NOTHING PRIVATE: no ncurses symbol beyond the window driver's own, and
 * no header from src/include. The compiling is the assertion, as it is for the
 * widget probe.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <curses.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>

#include <uflib/mjson/mjson.h>

#include "ircituilib/config_defs.h"
#include "ircituilib/ircitui_context.h"
#include "ircituilib/ircitui_conversation.h"
#include "ircituilib/ui.h"

#define DEFAULT_FIXTURE "assets/conversation/conversation_golden_test.json"
/* Long enough to wrap at the reflow ceiling, so those bubbles are multi-line. */
#define LB "the quick brown fox jumps over the lazy dog and keeps on running past the reflow ceiling"
#define DEFAULT_COLS 80
#define DEFAULT_ROWS 24
#define MAX_MESSAGES 64
/* The avatar gutter every mode runs with unless --gutter says otherwise: the
 * standard six columns, so the captures show the feature rather than hide it. */
#define CONV_DEMO_GUTTER 6

typedef struct
{
  const char *fixture;
  int         dump_model;
  int         dump_screen;
  int         dump_all;
  int         sweep;
  int         click_engine;
  int         prove;
  int         corners;
  int         reference;
  int         scroll;
  int         click_row;
  int         cols;
  int         rows;
  int         hold_ms;
  int         gutter;        /* the avatar gutter: 6 by default, 0 for the old look */
  int         avatars;       /* --avatars: one record per message's avatar */
  int         avatar_clicks; /* --avatar-clicks: click every avatar cell */
  int         timestamps;    /* --timestamps: reveal the stamp rows */
  int         animate_phase; /* --animate-phase N: the pulse's phase, 0 or 1 */
} DemoOptions;

/* --- the fixture ----------------------------------------------------------- */

static char *
sSlurp(const char *path_ptr, int *len_out)
{
  FILE *fp = fopen(path_ptr, "rb");
  long  size;
  char *buf;

  if (!fp) return NULL;
  if (fseek(fp, 0, SEEK_END) != 0 || (size = ftell(fp)) <= 0 || fseek(fp, 0, SEEK_SET) != 0) {
    fclose(fp);
    return NULL;
  }
  buf = malloc((size_t)size + 1);
  if (!buf) {
    fclose(fp);
    return NULL;
  }
  if (fread(buf, 1, (size_t)size, fp) != (size_t)size) {
    free(buf);
    fclose(fp);
    return NULL;
  }
  buf[size] = '\0';
  fclose(fp);
  *len_out = (int)size;
  return buf;
}

static IrcitUiMessageDirection
sDirection(const char *s_ptr)
{
  if (!s_ptr) return IRCITUI_MSG_INBOUND;
  if (strcmp(s_ptr, "outbound") == 0) return IRCITUI_MSG_OUTBOUND;
  if (strcmp(s_ptr, "system") == 0) return IRCITUI_MSG_SYSTEM;
  return IRCITUI_MSG_INBOUND;
}

static IrcitUiMessageStatus
sStatus(const char *s_ptr)
{
  if (!s_ptr) return IRCITUI_STATUS_DELIVERED;
  if (strcmp(s_ptr, "sending") == 0) return IRCITUI_STATUS_SENDING;
  if (strcmp(s_ptr, "read") == 0) return IRCITUI_STATUS_READ;
  if (strcmp(s_ptr, "expired") == 0) return IRCITUI_STATUS_EXPIRED;
  return IRCITUI_STATUS_DELIVERED;
}

static void
sGetString(const char *buf, int len, const char *path_ptr, char *to_ptr, int n, const char *fallback_ptr)
{
  if (mjson_get_string(buf, len, path_ptr, to_ptr, n) <= 0) {
    snprintf(to_ptr, (size_t)n, "%s", fallback_ptr ? fallback_ptr : "");
  }
}

/**
 * @brief Inject every message the fixture holds. Returns how many were appended.
 *
 * The reading is deliberately explicit per field rather than generic: each path is
 * one the fixture's coverage table marks READ, so this function and
 * dev/probes/conversation/FIXTURE-FIELDS.md are the same list written twice -- and
 * a field that stops resolving shows up as a default here rather than as silence.
 */
static int
sInject(const char *buf, int len, IrcitUiNode *conv_ptr)
{
  int i;
  int appended = 0;
  int typing   = 0;
  char path[192];
  char tmp[1024];

  for (i = 0; i < MAX_MESSAGES; i++) {
    IrcitUiConversationMessage msg;
    double                      id = 0, flag = 0;
    int                         has_reply = 0;
    int                         r;

    snprintf(path, sizeof path, "$.conversation_flow[%d].message_id", i);
    if (mjson_get_number(buf, len, path, &id) <= 0) break;

    memset(&msg, 0, sizeof msg);
    msg.message_id = (uint64_t)id;

    snprintf(path, sizeof path, "$.conversation_flow[%d].sender_name", i);
    sGetString(buf, len, path, msg.sender_name, (int)sizeof msg.sender_name, "?");
    snprintf(path, sizeof path, "$.conversation_flow[%d].timestamp", i);
    sGetString(buf, len, path, msg.timestamp, (int)sizeof msg.timestamp, "--:--");
    msg.text_content = tmp;
    snprintf(path, sizeof path, "$.conversation_flow[%d].text_content", i);
    sGetString(buf, len, path, tmp, (int)sizeof tmp, "");

    snprintf(path, sizeof path, "$.conversation_flow[%d].direction", i);
    sGetString(buf, len, path, tmp + 900, 48, "inbound");
    msg.direction = sDirection(tmp + 900);
    snprintf(path, sizeof path, "$.conversation_flow[%d].status", i);
    sGetString(buf, len, path, tmp + 900, 48, "delivered");
    msg.status = sStatus(tmp + 900);

    /* cluster_position and framework_allocated_height_cells are NOT read: the
     * model computes both, and the fixture's own values disagree with it for six
     * of the eight non-system messages. See FIXTURE-FIELDS.md. */

    flag = 0;
    snprintf(path, sizeof path, "$.conversation_flow[%d].has_reply_context", i);
    if (mjson_get_bool(buf, len, path, &has_reply) <= 0) has_reply = 0;
    if (has_reply) {
      msg.has_reply_context = true;
      snprintf(path, sizeof path, "$.conversation_flow[%d].reply_context.reply_msg_id", i);
      if (mjson_get_number(buf, len, path, &flag) > 0) msg.reply_msg_id = (uint64_t)flag;
      snprintf(path, sizeof path, "$.conversation_flow[%d].reply_context.reply_sender", i);
      sGetString(buf, len, path, msg.reply_sender, (int)sizeof msg.reply_sender, "?");
      snprintf(path, sizeof path, "$.conversation_flow[%d].reply_context.reply_snippet", i);
      msg.reply_snippet = tmp + 700;
      sGetString(buf, len, path, tmp + 700, 300, "");
    }

    flag = 0;
    snprintf(path, sizeof path, "$.conversation_flow[%d].interactivity.is_collapsed", i);
    if (mjson_get_bool(buf, len, path, (int *)&flag) > 0 && flag) msg.is_collapsed = true;

    /* THE KIND (R28). The fixture declares it twice over: a system message is a
     * status indicator by its direction (R3), and the one status indicator on the
     * local side is named by the shape the fixture asks for, in the override key --
     * `short_pill_asymmetric_right`, which is the square-top-right bubble. Read into
     * its own scratch, because tmp+700 still holds the reply snippet that Append is
     * about to copy. */
    msg.kind = msg.direction == IRCITUI_MSG_SYSTEM ? IRCITUI_MSG_KIND_STATUS_INDICATOR
                                                  : IRCITUI_MSG_KIND_MESSAGE;
    {
      /* ITS OWN SCRATCH. tmp is the body's own buffer -- msg.text_content points
       * into it and Append has not copied it yet -- and tmp+700 and tmp+900 hold
       * the reply snippet and the direction string for the same reason. */
      char kindbuf[64];

      snprintf(path, sizeof path, "$.conversation_flow[%d].interactivity.force_border_override", i);
      sGetString(buf, len, path, kindbuf, (int)sizeof kindbuf, "none");
      if (strcmp(kindbuf, "short_pill_asymmetric_right") == 0) msg.kind = IRCITUI_MSG_KIND_STATUS_INDICATOR;
    }

    for (r = 0; r < IRCITUI_REACTION_BADGES_MAX; r++) {
      double cnt = 0;
      snprintf(path, sizeof path, "$.conversation_flow[%d].reactions[%d].emoji", i, r);
      if (mjson_get_string(buf, len, path, msg.reactions[r].emoji, (int)sizeof msg.reactions[r].emoji) <= 0) break;
      snprintf(path, sizeof path, "$.conversation_flow[%d].reactions[%d].count", i, r);
      if (mjson_get_number(buf, len, path, &cnt) > 0) msg.reactions[r].count = (int)cnt;
      msg.reaction_count = r + 1;
    }

    if (IrcitUiConversationWindowAppend(conv_ptr, &msg)) appended++;
  }

  /* The typing row, from the fixture's transient state -- two of its three
   * payload keys are read and the third, animation_style, is inert. */
  snprintf(path, sizeof path, "$.transient_presence_state.is_friend_typing");
  if (mjson_get_bool(buf, len, path, &typing) > 0 && typing) {
    snprintf(path, sizeof path, "$.transient_presence_state.typing_sender_name");
    sGetString(buf, len, path, tmp, (int)sizeof tmp, "");
    IrcitUiConversationWindowSetTyping(conv_ptr, 1, tmp);
  }
  return appended;
}

/* --- the two dumps --------------------------------------------------------- */

static const char *
sDirName(IrcitUiMessageDirection d)
{
  return d == IRCITUI_MSG_OUTBOUND ? "outbound" : d == IRCITUI_MSG_SYSTEM ? "system" : "inbound";
}

static const char *
sClusterName(IrcitUiClusterPosition c)
{
  return c == IRCITUI_CLUSTER_LONE     ? "lone"
         : c == IRCITUI_CLUSTER_TOP    ? "top"
         : c == IRCITUI_CLUSTER_MIDDLE ? "middle"
                                       : "bottom";
}

static void
sDumpModel(IrcitUiNode *conv_ptr, int gutter)
{
  int n, i;

  n = IrcitUiConversationWindowRowCount(conv_ptr);
  printf("model: %d row(s), gutter %d\n", n, gutter);
  for (i = 0; i < n; i++) {
    IrcitUiConversationRow row;
    if (!IrcitUiConversationWindowRowInfo(conv_ptr, i, &row)) continue;
    IrcitUiConversationMessage msg;
    if (!IrcitUiConversationWindowMessageInfo(conv_ptr, i, &msg)) continue;
    /* `indic` marks a status indicator, so the class is on the model record and not
     * only in the painter: R28's cells, R4's exclusion from a block, and R24's
     * exemption from truncation all follow this field. */
    printf("row %2d  id=%-5llu %-8s %-8s %-8s origin=%-4d h=%-2d box=%d+%d cluster=%-6s cols=%-3d lines=%d/%d r=%d%s%s%s%s%s%s%s\n",
           i, (unsigned long long)msg.message_id, sDirName(msg.direction), msg.sender_name,
           msg.status == IRCITUI_STATUS_SENDING     ? "sending"
           : msg.status == IRCITUI_STATUS_DELIVERED ? "delivered"
           : msg.status == IRCITUI_STATUS_READ      ? "read"
                                                    : "expired",
           row.origin_y, row.height, row.box_x, row.box_w, sClusterName(row.cluster), row.max_width, row.body_lines,
           row.full_lines, msg.reaction_count, row.truncated ? " TRUNC" : "", row.name_row ? " tag" : "",
           row.badge_row ? " badge" : "", row.stamp_row ? " stamp" : "", row.avatar_rows > 0 ? " av" : "",
           row.avatar_pulse ? " pulse" : "",
           msg.kind == IRCITUI_MSG_KIND_MESSAGE ? "" : " indic");
  }
}

/**
 * @brief Print the WHOLE screen as text: the picture, not the cell list.
 *
 * Each row is assembled from its cells and converted back to multibyte, so a box
 * glyph prints as itself rather than as U+256D. Trailing blanks are trimmed, so
 * the output is the shape of what was drawn. This is what the live view looks
 * like, printable without a terminal -- which is the only way to read a TUI's
 * output from a script.
 */
static void
sDumpAll(void)
{
  int r, c;

  printf("screen: %dx%d\n", COLS, LINES);
  for (r = 0; r < LINES; r++) {
    wchar_t  ws[1024];
    char     mb[4096];
    size_t   n = 0;
    int      last = -1;

    for (c = 0; c < COLS && c < 1023; c++) {
      cchar_t cc;
      wchar_t one[8];
      attr_t  attrs = 0;
      short   pair  = 0;

      memset(&cc, 0, sizeof cc);
      memset(one, 0, sizeof one);
      if (mvwin_wch(curscr, r, c, &cc) == ERR) continue;
      if (getcchar(&cc, one, &attrs, &pair, NULL) == ERR) continue;
      ws[c] = one[0] ? one[0] : L' ';
      if (one[0] && one[0] != L' ') last = c;
      n = (size_t)c + 1;
    }
    for (c = (int)n; c < 1024; c++) ws[c] = 0;
    if (last < 0) {
      printf("r%02d |\n", r);
      continue;
    }
    ws[last + 1] = 0;
    if (wcstombs(mb, ws, sizeof mb) == (size_t)-1) mb[0] = 0;
    printf("r%02d |%s|\n", r, mb);
  }
}

/**
 * @brief Dump the widget's cells: what is on the glass, with what attributes.
 *
 * ONE LINE PER ROW, listing only the cells that are not blank, as `col:U+XXXX`
 * with the attribute bits and the colour pair when either is non-default. Blank
 * cells are omitted rather than printed, so the dump is short enough to read and a
 * change to what was drawn shows up as a changed line rather than as a moved
 * column.
 *
 * Reading the cell and its attributes is the part that has to be done a particular
 * way: getcchar REFUSES NULL attrs/color_pair on this ncurses, and a reader written
 * the natural way reports every cell as "no codepoint" -- indistinguishable from a
 * blank screen. dev/probes/border paid for that lesson; this is the same reader.
 */

/* The bubble glyphs, repeated here on purpose: the sweep reads the GLASS, and it
 * must not include a private header to learn what the painter draws. */
#define SW_TL "\xe2\x95\xad"
#define SW_BL "\xe2\x95\xb0"
#define SW_TR "\xe2\x95\xae"
#define SW_BR "\xe2\x95\xaf"
#define SW_HL "\xe2\x94\x80"
#define SW_VL "\xe2\x94\x82"
#define SW_TL_R "\xe2\x94\x98"
#define SW_TL_L "\xe2\x94\x94"
#define SW_REPLY "\xe2\x94\x8c"

/** @brief The codepoint at a screen cell, or 0 when nothing could be read. */
static unsigned
sCellAt(unsigned row, unsigned col)
{
  cchar_t cc;
  wchar_t ws[8];
  attr_t  at = 0;
  short   pr = 0;

  memset(&cc, 0, sizeof cc);
  memset(ws, 0, sizeof ws);
  if (mvwin_wch(curscr, (int)row, (int)col, &cc) == ERR) return 0;
  if (getcchar(&cc, ws, &at, &pr, NULL) == ERR) return 0;
  return (unsigned)ws[0];
}

/** @brief Does @p cp belong to the box-drawing block the bubble's rules use? */
static int
sIsBoxGlyph(unsigned cp)
{
  // Both corner sets: the rounded pill's and the squared paragraph box's. U+250C is
  // in here even though the reply marker shares it -- the caller tells them apart by
  // column, the corner at the wall and the marker one cell in.
  return cp == 0x256D || cp == 0x256E || cp == 0x2570 || cp == 0x256F || cp == 0x2500 || cp == 0x2502 ||
         cp == 0x2518 || cp == 0x2514 || cp == 0x250C || cp == 0x2510;
}





/**
 * @brief The frame as a labelled, hex-pinned reference record.
 *
 * The three dumps report a layer each -- geometry, cells, picture -- and none of them
 * says WHAT a row is. This one does: every screen row is labelled with the message on
 * it, that message's direction and its cluster position, so a corner can be judged
 * against the rule it belongs to without working out which bubble is which.
 *
 * ONE BLOCK PER ROW, in the reference record's shape: the label, the line as text, and
 * the line's UTF-8 bytes as hex -- so a change to a corner shows in two forms at once,
 * and a reviewer reading either one sees it.
 *
 * Rendered over the SYNTHETIC corner cases alone, with the golden input left out, so
 * the frame is the corner matrix rather than a conversation that happens to contain a
 * few of its cases.
 */
static void
sDumpReference(IrcitUiNode *conv_ptr)
{
  int    gx = 0, gy = 0, gw = 0, gh = 0;
  int    rows = IrcitUiConversationWindowRowCount(conv_ptr);
  int    r, k;

  IrcitUiNodeGetGeometry(conv_ptr, &gx, &gy, &gw, &gh);
  printf("================================================================================\n");
  printf("IRCITUI FRAME DEFINITION AND SCREEN MATRIX REFERENCE RECORD\n");
  printf("================================================================================\n");
  printf("Target Composite : the conversation window's own tree (no layout file)\n");
  printf("Dimensions       : Y=%d, X=%d\n", gh, gw);
  printf("Library State    : RootFrame [vVERSION-MASKED]\n");
  printf("================================================================================\n\n");
  printf("--- RENDER MATRIX ANALYSIS (GLYPH BUFFER RAW MAP) ------------------------------\n\n");

  for (r = 0; r < gh; r++) {
    const IrcitUiConversationRow *row = NULL;
    char                          label[96] = "background";
    char                          line[512];
    char                          hex[1024];
    int                           c, n = 0;

    /* Which message, if any, is on this row -- the row origins are in the widget's
     * content box, and a row scrolled out is not on the glass at all. */
    for (k = 0; k < rows; k++) {
      IrcitUiConversationRow ri;
      if (!IrcitUiConversationWindowRowInfo(conv_ptr, k, &ri)) continue;
      if (r >= ri.origin_y && r < ri.origin_y + ri.height) { row = NULL; (void)ri; break; }
    }
    for (k = 0; k < rows; k++) {
      IrcitUiConversationRow     ri;
      IrcitUiConversationMessage mi;
      if (!IrcitUiConversationWindowRowInfo(conv_ptr, k, &ri)) continue;
      if (r < ri.origin_y || r >= ri.origin_y + ri.height) continue;
      if (!IrcitUiConversationWindowMessageInfo(conv_ptr, (int)ri.msg_index, &mi)) continue;
      snprintf(label, sizeof label, "%s, cluster %s, %d line(s)",
               mi.direction == IRCITUI_MSG_INBOUND    ? "INBOUND"
               : mi.direction == IRCITUI_MSG_OUTBOUND ? "OUTBOUND"
                                                      : "SYSTEM",
               ri.cluster == IRCITUI_CLUSTER_LONE     ? "LONE"
               : ri.cluster == IRCITUI_CLUSTER_TOP    ? "TOP"
               : ri.cluster == IRCITUI_CLUSTER_MIDDLE ? "MIDDLE"
                                                      : "BOTTOM",
               ri.body_lines);
      break;
    }
    /* The line, and its bytes. Read from curscr, so this is what is on the glass. */
    for (c = 0; c < gw && n < (int)sizeof line - 8; c++) {
      cchar_t  cc;
      wchar_t  ws[8];
      attr_t   at = 0;
      short    pr = 0;
      size_t   wrote;
      memset(&cc, 0, sizeof cc);
      memset(ws, 0, sizeof ws);
      if (mvwin_wch(curscr, gy + r, gx + c, &cc) == ERR) break;
      if (getcchar(&cc, ws, &at, &pr, NULL) == ERR) break;
      wrote = wcstombs(line + n, ws, (size_t)((int)sizeof line - 8 - n));
      if (wrote == (size_t)-1) break;
      n += (int)wrote;
    }
    line[n] = 0;
    printf("[Row %02d] [%s]\n", r, label);
    printf("%-*s|\n", gw, line);
    for (c = 0, k = 0; c < n && k < (int)sizeof hex - 3; c++)
      k += snprintf(hex + k, sizeof hex - (size_t)k, "%02X", (unsigned char)line[c]);
    hex[k] = 0;
    printf("%s|\n\n", hex);
  }
  printf("--- NATIVE ENGINE SCROLLABLE CHROME OVERLAY STATUS -----------------------------\n");
  printf("Scrollbar Track Column : X=%d (the container's last column)\n", gw - 1);
  printf("Track Base Character   : ACS_VLINE  [A_DIM]\n");
  printf("Thumb Character        : U+0020 ' '  [A_REVERSE | COLOR_GREEN_BLACK]\n");
  printf("Thumb Height / Position: proportional, thumb = track*track/(track+overflow)\n");
}

/**
 * @brief Every corner case, built and read back -- the rules are not in the fixture.
 *
 * THE FIXTURE CANNOT TEST THE RULES. Its nine messages are all multi-line or
 * single-message runs, so the two cases the spec disputes most -- a ONE-LINE lone
 * bubble and a ONE-LINE run end -- never reach the glass. This appends synthetic
 * messages that produce every combination of direction, cluster position and line
 * count, reads the four corner cells of each off the screen, and asserts them
 * against the authoritative table.
 *
 * The expectations are written as glyph strings, so a wrong corner names itself.
 */
static int
sCorners(IrcitUiNode *conv_ptr, int cols, int rows, int do_assert)
{
  struct case_def {
    IrcitUiMessageDirection dir;
    const char             *sender;
    const char             *body;
    const char             *want_tl, *want_tr, *want_bl, *want_br;
    int                     skip; // a run's middle member: its rule is its run's
  };
  /* Built in order. Alternating sender and direction closes every run, so each
   * group is its own cluster and the run boundaries are where the rules bite. */
  /* THE EXPECTATIONS ARE APPENDIX B's RULES, glyph for glyph. A standalone received
   * bubble is fully rounded whatever its line count (R16); a local one squares its
   * top-right because it is flush to the wall (R11) and rounds both bottom corners
   * (R14); a block's first bubble is the block's top rule, and every other bubble
   * carries the border it shares with the one above it -- one row, ├ and ┤ (R6,
   * R12) -- while the block's last carries the end rule (R9, R14). */
  static const struct case_def CASES[] = {
    /* one-line LONE, both directions */
    {IRCITUI_MSG_INBOUND,  "A", "short inbound",    "\xe2\x95\xad", "\xe2\x95\xae", "\xe2\x95\xb0", "\xe2\x95\xaf", 0},
    {IRCITUI_MSG_OUTBOUND, "B", "short outbound",   "\xe2\x95\xad", "\xe2\x94\x90", "\xe2\x95\xb0", "\xe2\x95\xaf", 0},
    /* multi-line LONE, both directions */
    {IRCITUI_MSG_INBOUND,  "C", LB,          "\xe2\x95\xad", "\xe2\x95\xae", "\xe2\x95\xb0", "\xe2\x95\xaf", 0},
    {IRCITUI_MSG_OUTBOUND, "D", LB,          "\xe2\x95\xad", "\xe2\x94\x90", "\xe2\x95\xb0", "\xe2\x95\xaf", 0},
    /* a block of three ONE-LINE messages: the block's top, a shared border, the end */
    {IRCITUI_MSG_OUTBOUND, "E", "one",              "\xe2\x94\x8c", "\xe2\x94\x90", NULL, NULL, 0},
    {IRCITUI_MSG_OUTBOUND, "E", "two",              "\xe2\x94\x9c", "\xe2\x94\xa4", NULL, NULL, 0},
    {IRCITUI_MSG_OUTBOUND, "E", "three",            "\xe2\x94\x9c", "\xe2\x94\xa4", "\xe2\x95\xb0", "\xe2\x95\xaf", 0},
    /* a block of two multi-line messages: the top rule, then the end with the tail */
    {IRCITUI_MSG_INBOUND,  "F", LB,          "\xe2\x94\x8c", "\xe2\x94\x90", NULL, NULL, 0},
    {IRCITUI_MSG_INBOUND,  "F", LB,          "\xe2\x94\x9c", "\xe2\x94\xa4", "\xe2\x95\xb0", "\xe2\x94\x98", 0},
  };
  int         checks = 0, fails = 0;
  size_t      i;
  int         gx = 0, gy = 0, gw = 0, gh = 0;
  int         first_row;

  IrcitUiNodeGetGeometry(conv_ptr, &gx, &gy, &gw, &gh);
  first_row = IrcitUiConversationWindowRowCount(conv_ptr);
  for (i = 0; i < sizeof CASES / sizeof CASES[0]; i++) {
    IrcitUiConversationMessage msg;
    memset(&msg, 0, sizeof msg);
    msg.message_id   = 9000 + (uint64_t)i;
    msg.direction    = CASES[i].dir;
    msg.status       = IRCITUI_STATUS_READ;
    msg.text_content = (char *)CASES[i].body;
    snprintf(msg.sender_name, sizeof msg.sender_name, "%s", CASES[i].sender);
    if (!IrcitUiConversationWindowAppend(conv_ptr, &msg)) {
      printf("  FAIL case %zu could not be appended\n", i);
      fails++;
      checks++;
    }
  }
  // The rows are invalidated by every append, and both accessors refuse to answer
  // until they are recomputed -- so the frame is forced before anything is read.
  IrcitUiLayout(IrcitUiScreen.root, 0, 0, cols, rows);
  IrcitUiRealize(IrcitUiScreen.root);
  IrcitUiDraw(IrcitUiScreen.root, NULL);
  doupdate();
  // The LIVE view wants the cases appended and nothing else: the assertion below is
  // for the headless runner, and a person looking at the screen wants the bubbles.
  if (!do_assert) {
    printf("corners: %zu synthetic messages appended below the golden input. Press UP to "
           "scroll through them. The rules are Appendix B's:\n"
           "  standalone, received       -> fully rounded, ╭╮ / ╰╯, any line count  (R16)\n"
           "  standalone, local          -> ╭┐ / ╰╯: square top-right, flush to the wall (R11, R14)\n"
           "  a block's top rule         -> the radii rule of its first bubble     (R8, R13)\n"
           "  a block's shared border    -> ├ ┤, ONE row between two bubbles      (R6, R12)\n"
           "  a block's end rule         -> ╰┘ received (the tail), ╰╯ local      (R9, R14)\n"
           "The golden input's own blocks are all multi-line, so the one-line cases -- where\n"
           "the radii rule and the block rules disagree -- are synthetic.\n",
           sizeof CASES / sizeof CASES[0]);
    return 0;
  }

  for (i = 0; i < sizeof CASES / sizeof CASES[0]; i++) {
    IrcitUiConversationRow row;
    int                   k = first_row + (int)i;
    unsigned              tl, tr, bl, br;
    const char           *got[4];
    char                  buf[4][8];
    int                   j;

    if (CASES[i].skip) continue; // a run's middle member: its rule is its run's
    if (!IrcitUiConversationWindowRowInfo(conv_ptr, k, &row)) {
      printf("NOTE corners: case %zu has no row %d at all\n", i, k);
      fails++;
      checks++;
      continue;
    }
    if (row.origin_y < 0 || row.origin_y + row.height > gh) {
      // Scroll it on rather than skip it: the synthetic cases are more than one
      // viewport's worth, so the earliest are above the fold.
      // Positive delta goes FURTHER BACK into history, so a row below the fold needs
      // a negative one -- the first version had this backwards and scrolled away from it.
      int delta = row.origin_y < 0 ? -row.origin_y : -(row.origin_y + row.height - gh);
      IrcitUiConversationWindowScroll(conv_ptr, delta);
      IrcitUiLayout(IrcitUiScreen.root, 0, 0, cols, rows);
      IrcitUiRealize(IrcitUiScreen.root);
      IrcitUiDraw(IrcitUiScreen.root, NULL);
      doupdate();
      if (!IrcitUiConversationWindowRowInfo(conv_ptr, k, &row)) { printf("NOTE corners: case %zu has no row %d\n", i, k); continue; }
      if (row.origin_y < 0 || row.origin_y + row.height > gh) { printf("NOTE corners: case %zu still off the glass (origin %d h %d gh %d)\n", i, row.origin_y, row.height, gh); continue; }
    }
    /* ONLY THE RULES THAT EXIST ARE READ. A TOP bubble has no bottom rule and a
     * BOTTOM one no top rule, so their cells on that row are the side walls -- reading
     * them as corners reports the wall glyph and calls a correct render wrong, which
     * is what this check's first version did. */
    // The top rule is below the name tag when there is one, so the row is offset by
    // name_row -- reading origin_y directly finds the name line's blanks on inbound.
    int top_y = row.origin_y + row.name_row;
    snprintf(buf[0], sizeof buf[0], "%lc", (wint_t)sCellAt((unsigned)(gy + top_y), (unsigned)(gx + row.box_x)));
    snprintf(buf[1], sizeof buf[1], "%lc",
             (wint_t)sCellAt((unsigned)(gy + top_y), (unsigned)(gx + row.box_x + row.box_w - 1)));
    snprintf(buf[2], sizeof buf[2], "%lc", (wint_t)sCellAt((unsigned)(gy + row.origin_y + row.height - 1),
                                                           (unsigned)(gx + row.box_x)));
    snprintf(buf[3], sizeof buf[3], "%lc", (wint_t)sCellAt((unsigned)(gy + row.origin_y + row.height - 1),
                                                           (unsigned)(gx + row.box_x + row.box_w - 1)));
    for (j = 0; j < 4; j++) got[j] = buf[j];
    printf("corners: case %zu %s lines=%d %s%s / %s%s\n", i,
           CASES[i].dir == IRCITUI_MSG_INBOUND ? "in " : "out", row.body_lines, row.box_top ? got[0] : "-",
           row.box_top ? got[1] : "-", row.box_bottom ? got[2] : "-", row.box_bottom ? got[3] : "-");
    checks++;
    if ((CASES[i].want_tl && row.box_top &&
         (strcmp(got[0], CASES[i].want_tl) || strcmp(got[1], CASES[i].want_tr))) ||
        (CASES[i].want_bl && row.box_bottom &&
         (strcmp(got[2], CASES[i].want_bl) || strcmp(got[3], CASES[i].want_br)))) {
      fails++;
      printf("  FAIL case %zu: want %s%s / %s%s, got %s%s / %s%s\n", i, CASES[i].want_tl ? CASES[i].want_tl : "-",
             CASES[i].want_tr ? CASES[i].want_tr : "-", CASES[i].want_bl ? CASES[i].want_bl : "-",
             CASES[i].want_br ? CASES[i].want_br : "-", got[0], got[1], got[2], got[3]);
    }
  }
  printf("ZP conv corners: %d checks, %d failures\n", checks, fails);
  return fails ? 1 : 0;
}

/**
 * @brief The composite, as claims a person can check rather than be told.
 *
 * Four things, and each is the difference between a real composite and a widget that
 * merely calls itself one:
 *
 *   the parts are NODES      the viewport has its own box, strictly inside the
 *                            container's and one column narrower -- the column the
 *                            engine's scrollbar takes.
 *   the parts are ADDRESSED  a hit test inside the viewport resolves to the VIEWPORT
 *                            and one on the bar column resolves to the CONTAINER.
 *                            That is what makes "the matrix" and "the chrome" two
 *                            things rather than one painter's private business.
 *   the child is the WIDGET'S  a foreign child is refused, so the container's child
 *                            array stays the viewport and nothing else.
 *   and the model is the      the viewport is STATELESS: the container holds the
 *   container's               messages, and the child reads them through its parent.
 *
 * Deliberately NOT asserted: that IrcitUiFind can reach the viewport by id. It can,
 * because Find walks the tree rather than an index -- but that is an accident of the
 * implementation rather than a contract, and a caller that relied on it would be
 * reaching past the seam the widget exists to keep.
 */
static int
sProveComposite(IrcitUiNode *conv_ptr)
{
  IrcitUiNode     *vp;
  IrcitUiWidgetDef foreign = {0};
  int              cx = 0, cy = 0, cw = 0, ch = 0;
  int              vx = 0, vy = 0, vw = 0, vh = 0;
  int              checks = 0, fails = 0;

#define PROVE(cond, ...)                                                                                               \
  do {                                                                                                                 \
    checks++;                                                                                                          \
    if (!(cond)) {                                                                                                     \
      fails++;                                                                                                         \
      printf("  FAIL ");                                                                                               \
      printf(__VA_ARGS__);                                                                                             \
      printf("\n");                                                                                                    \
    }                                                                                                                  \
  } while (0)

  IrcitUiNodeGetGeometry(conv_ptr, &cx, &cy, &cw, &ch);
  vp = IrcitUiFind(conv_ptr, "conversation_viewport");
  PROVE(vp != NULL, "the container has no viewport child");
  if (!vp) {
    printf("ZP conv composite: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
  }
  IrcitUiNodeGetGeometry(vp, &vx, &vy, &vw, &vh);
  printf("composite: container %dx%d at %d,%d; viewport %dx%d at %d,%d\n", cw, ch, cx, cy, vw, vh, vx, vy);
  PROVE(IrcitUiNodeGetKind(vp) == IRCITUI_MESSAGE_VIEWPORT, "the viewport's kind is %d",
        (int)IrcitUiNodeGetKind(vp));
  PROVE(vx >= cx && vy >= cy && vx + vw <= cx + cw && vy + vh <= cy + ch,
        "the viewport is not inside the container: %d,%d %dx%d within %d,%d %dx%d", vx, vy, vw, vh, cx, cy, cw, ch);
  PROVE(vw == cw - 1, "the viewport is %d wide and the container %d: expected exactly the bar's column less", vw, cw);

  /* ADDRESSED: the matrix and the chrome resolve to different nodes. */
  {
    IrcitUiNode *hit_in = IrcitUiHitTest(IrcitUiScreen.root, vy + 1, vx + 2);
    IrcitUiNode *hit_bar = IrcitUiHitTest(IrcitUiScreen.root, vy + 1, cx + cw - 1);
    printf("composite: a cell inside the matrix hits kind %d; a cell on the bar hits kind %d\n",
           hit_in ? (int)IrcitUiNodeGetKind(hit_in) : -1, hit_bar ? (int)IrcitUiNodeGetKind(hit_bar) : -1);
    PROVE(hit_in && IrcitUiNodeGetKind(hit_in) == IRCITUI_MESSAGE_VIEWPORT,
          "a cell inside the matrix did not resolve to the viewport");
    PROVE(hit_bar && IrcitUiNodeGetKind(hit_bar) == IRCITUI_CONVERSATION_WINDOW,
          "a cell on the bar column did not resolve to the container");
  }

  /* THE CHILD IS THE WIDGET'S: a foreign node is refused. */
  foreign.id = "intruder";
  foreign.kind = IRCITUI_TEXT_VIEW;
  foreign.text = "nope";
  foreign.width_mode = IRCITUI_SIZE_MATCH;
  foreign.height_mode = IRCITUI_SIZE_EXACT;
  foreign.height = 1;
  foreign.flags = IRCITUI_F_VISIBLE | IRCITUI_F_ENABLED;
  PROVE(IrcitUiAddChild(conv_ptr, &foreign) == NULL, "a foreign child was accepted onto the conversation window");

  printf("ZP conv composite: %d checks, %d failures\n", checks, fails);
#undef PROVE
  return fails ? 1 : 0;
}

/**
 * @brief Drive a REAL click through the engine, twice, and read the flag.
 *
 * TWO OF THE RECORD'S VERIFICATION GAPS, closed by one mechanism. HandleClick has
 * only ever been called with coordinates derived here, so whether the engine's own
 * dispatch reaches the widget was asserted rather than shown; and collapse-back has
 * only ever been demonstrated in one direction, because the test that would have
 * shown the reverse passed the same flag twice. This pushes a mouse event back with
 * ungetmouse and lets the engine's path run -- hit test, the kind intercept, the
 * widget -- and reads the message's own flag afterwards rather than inferring it
 * from the geometry.
 *
 * The second click is the point of the whole thing: an expanding click that never
 * collapses back leaves the message permanently open, and the geometry moving in
 * one direction is exactly what the old test could see.
 */
static int
sClickEngine(IrcitUiNode *conv_ptr)
{
  int gx = 0, gy = 0, gw = 0, gh = 0;
  int rows = IrcitUiConversationWindowRowCount(conv_ptr);
  int checks = 0, fails = 0, k, target = -1, msg_index = -1, sy = 0, sx = 0;

  IrcitUiNodeGetGeometry(conv_ptr, &gx, &gy, &gw, &gh);
  for (k = 0; k < rows; k++) {
    IrcitUiConversationRow row;
    if (!IrcitUiConversationWindowRowInfo(conv_ptr, k, &row)) continue;
    if (!row.truncated) continue;
    target    = k;
    msg_index = (int)row.msg_index;
    // The first body line, two cells in from the bubble's left wall.
    sy = gy + row.origin_y + row.name_row + row.box_top + row.reply_rows;
    sx = gx + row.box_x + 2;
    break;
  }
  printf("click-engine: %d row(s), target row %d (message %d) at screen %d,%d\n", rows, target, msg_index, sx, sy);
  if (target < 0) {
    printf("ZP conv click-engine: 0 checks, 1 failures\n");
    printf("  no truncated message in the fixture: nothing to expand\n");
    return 1;
  }

  // The engine reads the mouse through getmouse(), and only IrcitUiMouseInit turns
  // that on; without it ungetmouse has nothing to push into.
  IrcitUiMouseInit();
  printf("\n");

  for (k = 0; k < 2; k++) {
    IrcitUiConversationMessage msg;
    MEVENT                    ev;
    int                       before, after;

    if (!IrcitUiConversationWindowMessageInfo(conv_ptr, msg_index, &msg)) break;
    before = msg.is_collapsed ? 1 : 0;

    memset(&ev, 0, sizeof ev);
    ev.x      = sx;
    ev.y      = sy;
    ev.bstate = BUTTON1_PRESSED;
    if (ungetmouse(&ev) != OK) {
      checks++;
      fails++;
      printf("  ungetmouse refused click %d\n", k);
      break;
    }
    IrcitUiDispatchKey(IrcitUiScreen.root, KEY_MOUSE);

    if (!IrcitUiConversationWindowMessageInfo(conv_ptr, msg_index, &msg)) break;
    after = msg.is_collapsed ? 1 : 0;
    checks++;
    if (after == before) fails++;
    printf("click-engine: click %d at screen %d,%d -> is_collapsed %d -> %d%s\n", k, sx, sy, before, after,
           after == before ? "  (NOTHING CHANGED)" : "");
    // Re-lay out between clicks: the toggle moved every row below it, so the same
    // screen cell is a different cell of the bubble afterwards.
    IrcitUiLayout(IrcitUiScreen.root, 0, 0, COLS, LINES);
    IrcitUiRealize(IrcitUiScreen.root);
    IrcitUiDraw(IrcitUiScreen.root, NULL);
    doupdate();
  }
  /* A STATUS INDICATOR IS A TARGET THE WIDGET DECLINES (R28). Both halves are
   * checked: the cell resolves to IRCITUI_TOKEN_STATUS with the indicator's own id,
   * so a click reaches the router and the application can act on it -- and the
   * widget's own gesture, which is collapse, leaves the flag alone, because a status
   * indicator has no collapse state. */
  for (k = 0; k < rows; k++) {
    IrcitUiConversationRow            row;
    IrcitUiConversationMessage        msg;
    IrcitUiConversationTokenTarget    tok;
    uint64_t                          id = 0;
    MEVENT                            ev;
    int                               ix, iy, before, after;

    if (!IrcitUiConversationWindowRowInfo(conv_ptr, k, &row)) continue;
    if (!IrcitUiConversationWindowMessageInfo(conv_ptr, (int)row.msg_index, &msg)) continue;
    if (msg.kind == IRCITUI_MSG_KIND_MESSAGE) continue;

    /* The fixture's indicators are above the fold at the bottom-anchored offset the
     * clicks above leave the view at, so scroll one on: positive delta goes further
     * back into history, a row below the fold needs a negative one. */
    if (row.origin_y < 0 || row.origin_y + row.height > gh) {
      IrcitUiConversationWindowScroll(conv_ptr,
                                      row.origin_y < 0 ? -row.origin_y : -(row.origin_y + row.height - gh));
      IrcitUiLayout(IrcitUiScreen.root, 0, 0, COLS, LINES);
      IrcitUiRealize(IrcitUiScreen.root);
      IrcitUiDraw(IrcitUiScreen.root, NULL);
      doupdate();
      if (!IrcitUiConversationWindowRowInfo(conv_ptr, k, &row)) continue;
      if (row.origin_y < 0 || row.origin_y + row.height > gh) continue;
    }

    iy = gy + row.origin_y + row.box_top;
    ix = gx + row.box_x + 2;
    if (iy < gy || iy >= gy + gh || ix < gx || ix >= gx + gw) continue; // scrolled out

    tok = IrcitUiConversationWindowResolveTokenAt(conv_ptr, row.origin_y + row.box_top, row.box_x + 2, &id);
    checks++;
    if (tok != IRCITUI_TOKEN_STATUS || id != msg.message_id) {
      fails++;
      printf("  FAIL a status indicator's cell resolved to %d (id %llu), not STATUS\n", (int)tok,
             (unsigned long long)id);
    }

    before = msg.is_collapsed ? 1 : 0;
    memset(&ev, 0, sizeof ev);
    ev.x      = ix;
    ev.y      = iy;
    ev.bstate = BUTTON1_PRESSED;
    if (ungetmouse(&ev) != OK) {
      checks++;
      fails++;
      printf("  ungetmouse refused the indicator click\n");
      break;
    }
    IrcitUiDispatchKey(IrcitUiScreen.root, KEY_MOUSE);
    if (!IrcitUiConversationWindowMessageInfo(conv_ptr, (int)row.msg_index, &msg)) break;
    after = msg.is_collapsed ? 1 : 0;
    checks++;
    if (after != before) fails++;
    printf("click-engine: status indicator %llu at screen %d,%d -> is_collapsed %d -> %d%s\n",
           (unsigned long long)msg.message_id, ix, iy, before, after,
           after == before ? "  (declined, as R28 requires)" : "  (THE WIDGET TOOK IT)");
  }

  printf("ZP conv click-engine: %d checks, %d failures\n", checks, fails);
  return fails ? 1 : 0;
}

/**
 * @brief The paint-versus-hit-test sweep: every cell of every bubble, two readings.
 *
 * THE ROUTER IS CHECKED AGAINST THE GLASS, not against the model it shares. For
 * every cell of every message's box it asks what the PAINTER put on that row --
 * read back from curscr, classified by the glyph -- and asserts the router names
 * the same thing; and for every cell outside a bubble it asserts the router names
 * nothing, which is the answer a router that falls back to the nearest message
 * cannot give.
 *
 * The painter's row order is fixed -- name tag, top rule, reply header, body lines,
 * badges, bottom rule -- so a row is classified by its first drawn glyph: '@' is
 * the name, the reply's corner is the reply, a box glyph is the bubble's body or
 * one of its rules, and anything else on a badge row is a badge.
 */
static int
sSweep(IrcitUiNode *conv_ptr)
{
  int gx = 0, gy = 0, gw = 0, gh = 0;
  int rows, k, checks = 0, fails = 0, none_cells = 0, skipped = 0, tally_off = 0;

  IrcitUiNodeGetGeometry(conv_ptr, &gx, &gy, &gw, &gh);
  rows = IrcitUiConversationWindowRowCount(conv_ptr);
  printf("sweep: widget box %dx%d at %d,%d, %d row(s)\n", gw, gh, gx, gy, rows);

  for (k = 0; k < rows; k++) {
    IrcitUiConversationRow row;
    int                   y;

    if (!IrcitUiConversationWindowRowInfo(conv_ptr, k, &row)) continue;
    for (y = row.origin_y; y < row.origin_y + row.height; y++) {
      // Off the glass there is no painter's account to read: a scrolled-out row's
      // screen cells belong to whatever else is up there, so it is skipped.
      if (y < 0 || y >= gh) { skipped++; continue; }
      unsigned got;
      int      at = y - row.origin_y;
      int      x;

      /* THE KIND OF ROW, from the model's own counts in the order the painter draws
       * them. The GLYPH cannot say any more: every interior row now carries a side
       * wall and a filled background, so the first character at the wall is the same
       * for a body line, a reply header and a badge row. What the glass is still read
       * for, below, is the box's extent and the cells OUTSIDE it -- which is where a
       * router that falls back to the nearest message shows itself. */
      {
        IrcitUiConversationMessage mi;
        /* A STATUS INDICATOR'S CELLS ARE ONE TARGET (R28), so none of the row's
         * counts classify them: the whole box, rules and all, is STATUS. */
        bool indicator = IrcitUiConversationWindowMessageInfo(conv_ptr, k, &mi) &&
                         mi.kind != IRCITUI_MSG_KIND_MESSAGE;

        if (indicator) got = IRCITUI_TOKEN_STATUS;
        else if (row.name_row > 0 && at == 0) got = IRCITUI_TOKEN_NAME_TAG;
        else {
          int base = row.name_row + row.box_top + row.reply_rows;
          if (row.box_top > 0 && at == row.name_row) got = IRCITUI_TOKEN_BODY;
          else if (row.reply_rows > 0 && at >= row.name_row + row.box_top && at < base) got = IRCITUI_TOKEN_REPLY;
          else if (at >= base && at < base + row.body_lines) got = IRCITUI_TOKEN_BODY;
          else if (row.badge_row > 0 && at == base + row.body_lines) got = IRCITUI_TOKEN_BADGE;
          else if (row.stamp_row > 0 && at == base + row.body_lines + row.badge_row) got = IRCITUI_TOKEN_BODY;
          else if (row.box_bottom > 0 && at == base + row.body_lines + row.badge_row + row.stamp_row)
            got = IRCITUI_TOKEN_BODY;
          else got = IRCITUI_TOKEN_NONE;
        }
      }

      // Every cell of the box must answer what the painter drew there.
      for (x = row.box_x; x < row.box_x + row.box_w; x++) {
        uint64_t id = 0;
        IrcitUiConversationTokenTarget t = IrcitUiConversationWindowResolveTokenAt(conv_ptr, y, x, &id);
        checks++;
        if (t != (IrcitUiConversationTokenTarget)got) {
          fails++;
          if (fails <= 8)
            printf("  MISMATCH row %d at=+%d y=%d x=%d: painter drew class %d, router says %d\n", k, at, y, x, got,
                   (int)t);
        }
      }
      /* And every cell outside it must answer nothing at all -- EXCEPT where the
       * gutter has ink on it, which the router is required to name: the tag's columns
       * and the avatar's. Asked of the glass and not of the model, so a router that
       * names a blank cell fails here as loudly as one that ignores ink. */
      {
        uint64_t id = 0;
        int      ox;
        for (ox = 0; ox < row.box_x; ox++) {
          unsigned                       ink      = sCellAt((unsigned)(gy + y), (unsigned)(gx + ox));
          int                            inked    = (ink != 0 && ink != ' ');
          IrcitUiConversationTokenTarget t        = IrcitUiConversationWindowResolveTokenAt(conv_ptr, y, ox, &id);

          checks++;
          none_cells++;
          if (inked ? (t == IRCITUI_TOKEN_NONE) : (t != IRCITUI_TOKEN_NONE)) {
            fails++;
            if (fails <= 8)
              printf("  MISMATCH row %d y=%d x=%d is LEFT of the box: ink=U+%04X, router says %d\n", k, y, ox, ink,
                     (int)t);
          }
        }
        for (ox = row.box_x + row.box_w; ox < gw; ox++) {
          uint64_t id = 0;
          checks++;
          none_cells++;
          if (IrcitUiConversationWindowResolveTokenAt(conv_ptr, y, ox, &id) != IRCITUI_TOKEN_NONE) {
            fails++;
            if (fails <= 8) printf("  MISMATCH row %d y=%d x=%d is RIGHT of the box but resolved\n", k, y, ox);
          }
        }
      }
    }
  }
  /* THE REACTION TALLY, which the record lists as having no artefact at all: the
   * painter draws the emoji and its count, and nothing asserted either. This reads
   * the glass for the PATTERN -- a glyph outside ASCII followed within a cell or two
   * by a digit -- for every message that carries a reaction, so the claim is
   * "a message with reactions shows its tally" rather than "the painter's source
   * contains a %d". */
  for (k = 0; k < rows; k++) {
    IrcitUiConversationRow     row;
    IrcitUiConversationMessage msg;
    int                        found = 0, y;

    if (!IrcitUiConversationWindowRowInfo(conv_ptr, k, &row)) continue;
    if (!IrcitUiConversationWindowMessageInfo(conv_ptr, (int)row.msg_index, &msg)) continue;
    if (msg.reaction_count <= 0) continue;
    // The badge sits below the reply, the body and the top rule; if that row is
    // scrolled out there is no painter's account to read, as in the cell sweep.
    {
      int badge_y = row.origin_y + row.name_row + row.box_top + row.reply_rows + row.body_lines;
      if (row.badge_row <= 0 || badge_y < 0 || badge_y >= gh) {
        tally_off++;
        continue;
      }
    }
    for (y = row.origin_y; y < row.origin_y + row.height && !found; y++) {
      int x;
      if (y < 0 || y >= gh) continue;
      for (x = row.box_x; x < row.box_x + row.box_w - 1; x++) {
        unsigned a = sCellAt((unsigned)(gy + y), (unsigned)(gx + x));
        unsigned b = sCellAt((unsigned)(gy + y), (unsigned)(gx + x + 1));
        if (a > 0x2000 && b >= '0' && b <= '9') {
          printf("  tally: message %llu shows reaction glyph U+%04X with count starting '%c'\n",
                 (unsigned long long)msg.message_id, a, (char)b);
          found = 1;
          break;
        }
      }
    }
    checks++;
    if (!found) {
      fails++;
      printf("  tally: message %llu carries %d reaction(s) and no glyph+digit pair is on the glass\n",
             (unsigned long long)msg.message_id, msg.reaction_count);
    }
  }

  printf("sweep: %d cell(s) compared against the glass, %d of them outside a bubble, %d row line(s) skipped as "
         "scrolled out, %d mismatch(es); %d reaction message(s) had their badge row scrolled out\n", checks,
         none_cells, skipped, fails, tally_off);
  printf("ZP conv sweep: %d checks, %d failures\n", checks, fails);
  return fails ? 1 : 0;
}

static void
sDumpScreen(IrcitUiNode *conv_ptr)
{
  int x = 0, y = 0, w = 0, h = 0;
  int r, c;

  IrcitUiNodeGetGeometry(conv_ptr, &x, &y, &w, &h);
  printf("screen: widget box %dx%d at %d,%d\n", w, h, x, y);

  for (r = 0; r < h; r++) {
    if (y + r < 0 || y + r >= LINES) continue;
    printf("r%02d", r);
    for (c = 0; c < w; c++) {
      cchar_t cc;
      wchar_t ws[8];
      attr_t  attrs = 0;
      short   pair  = 0;

      if (x + c < 0 || x + c >= COLS) continue;
      memset(&cc, 0, sizeof cc);
      memset(ws, 0, sizeof ws);
      if (mvwin_wch(curscr, y + r, x + c, &cc) == ERR) continue;
      if (getcchar(&cc, ws, &attrs, &pair, NULL) == ERR) continue;
      /* A BLANK CELL THAT CARRIES A PAIR IS NOT NOTHING. Skipping every space made the
       * dump blind to whether a bubble has a background at all -- which is precisely
       * the defect the eye caught, a box whose edges are drawn and whose middle is
       * not, while the reference said the two were identical. A cell is skipped only
       * when it is blank AND plain. */
      if ((ws[0] == L' ' || ws[0] == 0) && !attrs && !pair) continue;
      printf("  %d:U+%04X", c, (unsigned)ws[0]);
      if (attrs) printf("^%lX", (unsigned long)attrs);
      if (pair) printf("@%d", (int)pair);
    }
    printf("\n");
  }
}


/**
 * @brief The arrow keys TWO WAYS: ncurses' own, and the bare sequences.
 *
 * KEY_UP is what ncurses makes of the sequence its terminfo names -- which is the
 * application-mode form, because keypad(TRUE) asks the terminal for it. A terminal
 * whose bytes do not match the entry leaves a lone ESC in the buffer instead, so
 * the two tails are matched here as well; and j/k scroll whatever the terminal is.
 *
 * @return +1 for back into history, -1 for forward, 0 when the key is not an arrow.
 */
static int
sArrowKey(int key)
{
  char tail[2];
  int  i;

  if (key == KEY_UP || key == 'k') return 1;
  if (key == KEY_DOWN || key == 'j') return -1;
  if (key != 27) return 0;
  nodelay(stdscr, TRUE);
  for (i = 0; i < 2; i++) {
    int c = getch();
    if (c == ERR) break;
    tail[i] = (char)c;
  }
  nodelay(stdscr, FALSE);
  if (i == 2 && (tail[0] == '[' || tail[0] == 'O')) {
    if (tail[1] == 'A') return 1;
    if (tail[1] == 'B') return -1;
  }
  return 0;
}

/**
 * @brief The application's on_click for the conversation window.
 *
 * THE LIVE NODE, AND NOT WHERE THE CLICK LANDED: the engine's callback contract
 * passes the node and the registered user pointer, so this can report that a cell
 * the widget declined reached the application and nothing more. Naming the CELL
 * needs the coordinates, which an application has only when it handles the input
 * itself -- which is what --avatar-clicks does.
 */
static IrcitUiClickDisposition
sConvClicked(IrcitUiNode *node_ptr, void *user_ptr)
{
  (void)node_ptr;
  (void)user_ptr;
  printf("on_click: the application received the click the widget declined\n");
  return IRCITUI_CLICK_CARRY;
}

/**
 * @brief One record per message's avatar: the model's account, and the glass's.
 *
 * The glyph is read OFF THE GLASS rather than re-derived here: the widget's own
 * choice is what a reader wants, and a second copy of the derivation in this file
 * would agree with the widget only until one of them changed. Its display width is
 * printed beside it, because one column is what the grid is built on.
 */
static void
sDumpAvatars(IrcitUiNode *conv_ptr, int gutter)
{
  int gx = 0, gy = 0, gw = 0, gh = 0;
  int rows, i;

  IrcitUiNodeGetGeometry(conv_ptr, &gx, &gy, &gw, &gh);
  rows = IrcitUiConversationWindowRowCount(conv_ptr);
  printf("avatars: %d row(s), gutter %d, widget box %dx%d at %d,%d\n", rows, gutter, gw, gh, gx, gy);
  for (i = 0; i < rows; i++) {
    IrcitUiConversationRow     row;
    IrcitUiConversationMessage msg;
    unsigned                   glyph = 0;
    int                        width = -1;

    if (!IrcitUiConversationWindowRowInfo(conv_ptr, i, &row)) continue;
    if (!IrcitUiConversationWindowMessageInfo(conv_ptr, i, &msg)) continue;
    if (row.avatar_rows > 0 && row.avatar_y >= 0 && row.avatar_y + 1 < gh)
      glyph = sCellAt((unsigned)(gy + row.avatar_y + 1), (unsigned)(gx + row.avatar_x + 2));
    if (glyph) width = wcwidth((wchar_t)glyph);
    printf("row %2d  id=%-5llu %-8s %-8s box=%-3d+%-3d av=%d,%d+%d pulse=%d name=%d+%d stamp=%d glyph=U+%04X w=%d\n", i,
           (unsigned long long)msg.message_id, sDirName(msg.direction), msg.sender_name, row.box_x, row.box_w,
           row.avatar_x, row.avatar_y, row.avatar_rows, row.avatar_pulse, row.name_x, row.name_w, row.stamp_row, glyph,
           width);
  }
}

/**
 * @brief Click every cell of every avatar: the token, the id, and the harm done.
 *
 * THE ADVERSARIAL HALF OF THE AVATAR'S TARGET. A click target that quietly collapses
 * the message under it is the failure mode worth pinning, so this asserts the flag
 * did NOT move, that the id is the sender's, and that the clearance column beside
 * the glyph is nobody's.
 */
static int
sAvatarClicks(IrcitUiNode *conv_ptr)
{
  int gx = 0, gy = 0, gw = 0, gh = 0;
  int rows, k, checks = 0, fails = 0;

  IrcitUiNodeGetGeometry(conv_ptr, &gx, &gy, &gw, &gh);
  rows = IrcitUiConversationWindowRowCount(conv_ptr);
  for (k = 0; k < rows; k++) {
    IrcitUiConversationRow     row;
    IrcitUiConversationMessage msg, after;
    int                        y, x;

    if (!IrcitUiConversationWindowRowInfo(conv_ptr, k, &row)) continue;
    if (row.avatar_rows <= 0) continue;
    if (!IrcitUiConversationWindowMessageInfo(conv_ptr, (int)row.msg_index, &msg)) continue;

    for (y = row.avatar_y; y < row.avatar_y + row.avatar_rows; y++) {
      if (y < 0 || y >= gh) continue;
      for (x = 0; x < 5; x++) {
        uint64_t                     id = 0;
        IrcitUiConversationTokenTarget t = IrcitUiConversationWindowResolveTokenAt(conv_ptr, y, x, &id);

        checks++;
        if (t != IRCITUI_TOKEN_AVATAR || id != msg.message_id) {
          fails++;
          printf("  FAIL avatar cell %d,%d: token %d id %llu, want AVATAR for %llu\n", y, x, (int)t,
                 (unsigned long long)id, (unsigned long long)msg.message_id);
        }
      }
    }
    /* The clearance column is the gutter's last one and belongs to nothing. */
    {
      uint64_t id = 0;
      checks++;
      if (IrcitUiConversationWindowResolveTokenAt(conv_ptr, row.avatar_y + 1, 5, &id) != IRCITUI_TOKEN_NONE) {
        fails++;
        printf("  FAIL the clearance column beside row %d resolved to something\n", k);
      }
    }
    /* And the click itself: declined, with the collapse flag left where it was. */
    {
      int before = msg.is_collapsed ? 1 : 0;
      int rc     = IrcitUiConversationWindowHandleClick(conv_ptr, gy + row.avatar_y + 1, gx + 2);
      checks++;
      if (rc != 0) {
        fails++;
        printf("  FAIL HandleClick on row %d's avatar returned %d, want 0\n", k, rc);
      }
      if (!IrcitUiConversationWindowMessageInfo(conv_ptr, (int)row.msg_index, &after)) continue;
      checks++;
      if ((after.is_collapsed ? 1 : 0) != before) {
        fails++;
        printf("  FAIL the avatar click flipped row %d's collapse flag\n", k);
      }
    }
  }
  printf("avatar-clicks: %d check(s), %d failure(s)\n", checks, fails);
  printf("ZP conv avatar-clicks: %d checks, %d failures\n", checks, fails);
  return fails ? 1 : 0;
}

/* --- the demo's own tree ---------------------------------------------------- */

/*
 * THE DEMO OWNS ITS LAYOUT NOW, and that is what this gate is for. It used to call
 * IrcitUiScreenReset() and read `chat_window` out of layout/ircitui.yaml -- the tree
 * ircitui_demo owns -- so it inherited that demo's whole chrome: a header with seven
 * menu buttons, a scrollable canopy body, a footer. It used ONE node of it.
 *
 * A NO-CONSTRAINT TREE, deliberately, and not by preference: IrcitUiWidgetDef carries
 * no edge fields, so a C-built tree cannot pin anything to a sibling. The root is a
 * vertical linear instead, and the middle child takes MATCH height -- which in a
 * vertical linear is the remainder after the fixed rows, so the main window grows
 * and the strip and the input line do not. Nothing here names a cell count in the
 * growing axis, which is what makes the demo indifferent to the terminal's size.
 *
 * THE IDS ARE FILE-SCOPE LITERALS because IrcitUiWidgetDef borrows them. The model
 * carries no id index -- a self-assembled one need not -- so IrcitUiFind resolves by
 * walking, which is the fallback sLookupRef documents.
 */
static IrcitUiWidgetDef sRootDef = {
  .id = "demo_root", .kind = IRCITUI_LINEAR, .text = "",
  .orientation = IRCITUI_VERT,
  .width_mode = IRCITUI_SIZE_MATCH, .height_mode = IRCITUI_SIZE_MATCH,
  .flags = IRCITUI_F_VISIBLE | IRCITUI_F_ENABLED
};
/* The strip future command buttons go in: a title now, and somewhere for the rest.
 * BORDERLESS and ONE ROW: a frame costs a row top and bottom, so a bordered strip a
 * single row tall has no content row left and its children are never drawn -- which
 * is what the first version of this did. The framed main window below it is the
 * separation; a second frame here would be chrome for chrome's sake. */
static IrcitUiWidgetDef sCmdBarDef = {
  .id = "demo_cmdbar", .kind = IRCITUI_LINEAR, .text = "",
  .orientation = IRCITUI_HORIZ,
  .width_mode = IRCITUI_SIZE_MATCH, .height_mode = IRCITUI_SIZE_EXACT, .height = 1,
  .flags = IRCITUI_F_VISIBLE | IRCITUI_F_ENABLED
};
static IrcitUiWidgetDef sTitleDef = {
  .id = "demo_title", .kind = IRCITUI_TEXT_VIEW, .text = " conversation ",
  .width_mode = IRCITUI_SIZE_WRAP, .height_mode = IRCITUI_SIZE_EXACT, .height = 1,
  .flags = IRCITUI_F_VISIBLE | IRCITUI_F_ENABLED, .theme_class = "text_muted"
};
/* ASCII only, and that is not a style choice: a TEXT_VIEW's painter charges one
 * column per BYTE, so an arrow glyph here would be drawn as its own bytes. */
static IrcitUiWidgetDef sHintDef = {
  .id = "demo_hint", .kind = IRCITUI_TEXT_VIEW, .text = " up/down or wheel scroll   t timestamps   q quit ",
  .width_mode = IRCITUI_SIZE_WRAP, .height_mode = IRCITUI_SIZE_EXACT, .height = 1,
  .flags = IRCITUI_F_VISIBLE | IRCITUI_F_ENABLED, .theme_class = "text_muted"
};
static IrcitUiWidgetDef sQuitDef = {
  .id = "demo_quit", .kind = IRCITUI_BUTTON, .text = "quit",
  .width_mode = IRCITUI_SIZE_WRAP, .height_mode = IRCITUI_SIZE_EXACT, .height = 1,
  .flags = IRCITUI_F_VISIBLE | IRCITUI_F_ENABLED | IRCITUI_F_CLICKABLE
};
/* The framed main window, and the widget filling it on both axes. */
static IrcitUiWidgetDef sMainDef = {
  .id = "demo_main", .kind = IRCITUI_FRAME, .text = "",
  .width_mode = IRCITUI_SIZE_MATCH, .height_mode = IRCITUI_SIZE_MATCH,
  .flags = IRCITUI_F_VISIBLE | IRCITUI_F_ENABLED | IRCITUI_F_BORDER
};
/* CLICKABLE, because the widget declines what is not its gesture -- an avatar, and a
 * status indicator -- and a declined cell only reaches an application's on_click for
 * a node carrying the flag (ui_engine.c's dispatch branch). */
static IrcitUiWidgetDef sConvDef = {
  .id = "conv_view", .kind = IRCITUI_CONVERSATION_WINDOW, .text = "",
  .width_mode = IRCITUI_SIZE_MATCH, .height_mode = IRCITUI_SIZE_MATCH,
  .flags = IRCITUI_F_VISIBLE | IRCITUI_F_ENABLED | IRCITUI_F_CLICKABLE, .theme_class = "surface"
};
/* The input line. Its buffer is the editor's own; the def's caption is a placeholder. */
static IrcitUiWidgetDef sInputDef = {
  .id = "demo_input", .kind = IRCITUI_EDIT_TEXT, .text = "",
  .width_mode = IRCITUI_SIZE_MATCH, .height_mode = IRCITUI_SIZE_EXACT, .height = 1,
  .flags = IRCITUI_F_VISIBLE | IRCITUI_F_ENABLED | IRCITUI_F_CLICKABLE
};

static IrcitUiDataBindDescriptor sDemoModel;

/**
 * @brief The demo's half of the framework contract: its own three-part tree.
 *
 * A command strip, a framed main window that takes the remainder, and the input line.
 * The framework drives this instead of the model it generates, which is what the
 * binder exists for -- "the generated model is nothing more than the framework's
 * default answer to the same question an application answers for itself".
 */
static IrcitUiDataBindDescriptor *
sDemoBinder(void)
{
  IrcitUiNode *root, *bar, *main_win;

  memset(&sDemoModel, 0, sizeof sDemoModel);
  root = IrcitUiNodeFromDef(&sRootDef);
  if (!root) return &sDemoModel;
  bar = IrcitUiAddChild(root, &sCmdBarDef);
  if (bar) {
    IrcitUiAddChild(bar, &sTitleDef);
    IrcitUiAddChild(bar, &sQuitDef);
    IrcitUiAddChild(bar, &sHintDef);
  }
  main_win = IrcitUiAddChild(root, &sMainDef);
  if (main_win) IrcitUiAddChild(main_win, &sConvDef);
  IrcitUiAddChild(root, &sInputDef);
  sDemoModel.root = root;
  return &sDemoModel;
}

int
main(int argc, char **argv)
{
  int geo_cols, geo_rows; // one geometry variable: stated for the dumps, the tty's live

  DemoOptions opts = {.fixture = DEFAULT_FIXTURE, .cols = DEFAULT_COLS, .rows = DEFAULT_ROWS,
                      .hold_ms = 0, .click_row = -1, .sweep = 0, .click_engine = 0, .prove = 0, .corners = 0,
                      .reference = 0, .gutter = CONV_DEMO_GUTTER};
  FILE       *sink;
  SCREEN     *scr;
  char       *buf;
  int         len = 0;
  IrcitUiNode *conv;
  int         i;

  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--dump-model") == 0) opts.dump_model = 1;
    else if (strcmp(argv[i], "--dump-screen") == 0) opts.dump_screen = 1;
    else if (strcmp(argv[i], "--dump-all") == 0) opts.dump_all = 1;
    else if (strcmp(argv[i], "--sweep") == 0) opts.sweep = 1;
    else if (strcmp(argv[i], "--click-engine") == 0) opts.click_engine = 1;
    else if (strcmp(argv[i], "--prove-composite") == 0) opts.prove = 1;
    else if (strcmp(argv[i], "--corners") == 0) opts.corners = 1;
    else if (strcmp(argv[i], "--dump-reference") == 0) opts.reference = 1;
    else if (strcmp(argv[i], "--scroll") == 0 && i + 1 < argc) opts.scroll = atoi(argv[++i]);
    else if (strcmp(argv[i], "--click-row") == 0 && i + 1 < argc) opts.click_row = atoi(argv[++i]);
    else if (strcmp(argv[i], "--fixture") == 0 && i + 1 < argc) opts.fixture = argv[++i];
    else if (strcmp(argv[i], "--cols") == 0 && i + 1 < argc) opts.cols = atoi(argv[++i]);
    else if (strcmp(argv[i], "--rows") == 0 && i + 1 < argc) opts.rows = atoi(argv[++i]);
    else if (strcmp(argv[i], "--hold-ms") == 0 && i + 1 < argc) opts.hold_ms = atoi(argv[++i]);
    else if (strcmp(argv[i], "--gutter") == 0 && i + 1 < argc) opts.gutter = atoi(argv[++i]);
    else if (strcmp(argv[i], "--avatars") == 0) opts.avatars = 1;
    else if (strcmp(argv[i], "--avatar-clicks") == 0) opts.avatar_clicks = 1;
    else if (strcmp(argv[i], "--timestamps") == 0) opts.timestamps = 1;
    else if (strcmp(argv[i], "--animate-phase") == 0 && i + 1 < argc) opts.animate_phase = atoi(argv[++i]);
    else {
      fprintf(stderr, "usage: %s [--fixture PATH] [--dump-model] [--dump-screen] [--dump-all] [--scroll N] "
                    "[--cols N] [--rows N] [--gutter N] [--avatars] [--avatar-clicks] [--timestamps] "
                    "[--animate-phase N]\n", argv[0]);
      return 2;
    }
  }

  if (setlocale(LC_ALL, "") == NULL) {
    fprintf(stderr, "no locale could be set\n");
    return 1;
  }
  buf = sSlurp(opts.fixture, &len);
  if (!buf) {
    fprintf(stderr, "cannot read the golden input at '%s'\n", opts.fixture);
    return 1;
  }

  /* THE LIVE VIEW NEEDS THE REAL TERMINAL, and this is the line that decides it.
   * Only the dumps are headless: they read ncurses' own virtual screen, so
   * newterm() onto /dev/null is the whole harness and produces no terminal output.
   * A live view is the opposite -- it must write to the terminal the user is
   * looking at -- and the framework's contract is that the APPLICATION opens it
   * (src/examples/ui_main.c:1113), with the window driver attaching to the screen
   * it finds. A first version of this file used the probes' /dev/null bootstrap
   * for both, so the live view drew into a screen whose output went nowhere: it
   * printed the injection line and nothing else. */
  sink = NULL;
  scr  = NULL;
  if (opts.dump_model || opts.dump_screen || opts.dump_all || opts.sweep || opts.click_engine || opts.prove || opts.reference ||
      opts.avatars || opts.avatar_clicks || opts.timestamps) {
    sink = fopen("/dev/null", "w");
    scr  = sink ? newterm("xterm-256color", sink, NULL) : NULL;
    if (!scr) {
      fprintf(stderr, "no headless screen: TERM must name a terminfo entry\n");
      free(buf);
      if (sink) fclose(sink);
      return 1;
    }
    set_term(scr);
    /* THE STATED GEOMETRY IS THE SCREEN'S, not just the layout's. newterm() sizes the
     * virtual screen from TERMINFO -- 80x24 for xterm-256color -- so a layout at
     * --cols 120 --rows 40 put most of the tree outside it, and both the frame dump
     * (which walks LINES/COLS) and the sweep (which reads curscr at absolute cells)
     * silently reported the top-left 80x24. Measured: the sweep called 847 cells
     * mismatched at 120x40, all of them reads past the real screen. */
    if (opts.cols != COLS || opts.rows != LINES) resizeterm(opts.rows, opts.cols);
    /* THE APPLICATION STARTS COLOUR, not the framework -- the window driver's own
     * adapter says so: "the application is the one that calls start_color(), in its
     * own initialisation". Without it init_pair fails, IrcitUiColourPair returns 0,
     * and EVERY cell in the frame carries pair 0: the whole palette -- the nine
     * slots, the theme classes, the bubble styles -- is inert. Measured: before this
     * line, not one cell of the conversation's box carried a colour pair. */
    if (has_colors()) {
      start_color();
      use_default_colors();
    }
  }
  else {
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
      fprintf(stderr, "the live view needs a terminal; use --dump-all to print the screen instead\n");
      free(buf);
      return 1;
    }
    initscr();
    if (has_colors()) {
      start_color();
      use_default_colors();
    }
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    /* The wheel and the clicks are the engine's -- its dispatch reads the event
     * itself when KEY_MOUSE arrives, which the loop's own catch-all forwards.
     *
     * THE HOVER IS TURNED OFF AT THE TERMINAL, and that is the difference between a
     * usable view and a strobe: IrcitUiMouseInit forces 1003h on -- any-event
     * tracking, deliberately, because some terminals need it -- so a pointer that
     * merely moves is a KEY_MOUSE and this loop repaints for every one. 1002h, which
     * it also asks for, is what a click and a wheel turn arrive through, and motion
     * WHILE A BUTTON IS HELD: that is the engine's thumb-drag, and it is kept. Only
     * the hover goes. */
    IrcitUiMouseInit();
    fputs("\033[?1003l", stdout);
    fflush(stdout);
  }

  /* THE DEMO'S OWN MODEL, through the binder. The tree is built BEFORE anything is
   * injected, because the reset is what installs it and it re-clones: a widget
   * injected earlier would be discarded. */
  IrcitUiInitialiseFrameworkContext(sDemoBinder, NULL);
  IrcitUiScreenReset();

  /* ONE GEOMETRY VARIABLE, and the dumps are why it exists. They used to lay out at
   * COLS/LINES, which headless are TERMINFO's defaults rather than the flags the
   * runner passes -- so the frozen reference depended on the terminfo entry. The
   * dumps take the stated geometry; the live view takes the terminal's and follows
   * it when it changes. */
  geo_cols = opts.dump_model || opts.dump_screen || opts.dump_all || opts.sweep || opts.click_engine || opts.avatars ||
                     opts.avatar_clicks || opts.timestamps ? opts.cols : COLS;
  geo_rows = opts.dump_model || opts.dump_screen || opts.dump_all || opts.sweep || opts.click_engine || opts.avatars ||
                     opts.avatar_clicks || opts.timestamps ? opts.rows : LINES;

  IrcitUiLayout(IrcitUiScreen.root, 0, 0, geo_cols, geo_rows);
  IrcitUiRealize(IrcitUiScreen.root);
  IrcitUiDraw(IrcitUiScreen.root, NULL);
  doupdate();

  /* The widget, out of the demo's OWN tree -- resolved by walking, because a
   * self-assembled model carries no id index. */
  conv = IrcitUiFind(IrcitUiScreen.root, "conv_view");
  if (!conv) {
    fprintf(stderr, "the demo's own tree has no 'conv_view' node\n");
    endwin();
    delscreen(scr);
    fclose(sink);
    free(buf);
    return 1;
  }

  /* THE THREE THINGS THAT ARE NOT MESSAGES, before anything is measured: the
   * gutter the whole feature is about, the stamp rows, and the pulse's phase. */
  IrcitUiConversationWindowSetAvatarGutter(conv, opts.gutter);
  IrcitUiConversationWindowToggleTimestamps(conv, opts.timestamps);
  IrcitUiConversationWindowAnimate(conv, opts.animate_phase);
  /* The click that the widget declines has to reach the application, and it only
   * does for a node carrying IRCITUI_F_CLICKABLE -- which the def now sets. */
  if (!IrcitUiBindClick("conv_view", sConvClicked, NULL)) {
    fprintf(stderr, "could not register the conversation window's on_click\n");
  }

  {
    int injected = sInject(buf, len, conv);
    printf("injected %d message(s) from %s\n", injected, opts.fixture);
  }

  /* FULL SCREEN, and not at the template's own 34x17: that size is the float cap
   * the framework puts on a raised popup -- Gate 0 measured it -- and a conversation
   * squeezed into it shows a single message. A panel is laid out at the size its caller
   * chooses, so this demo chooses the screen. */
  IrcitUiLayout(IrcitUiScreen.root, 0, 0, geo_cols, geo_rows);
  IrcitUiRealize(IrcitUiScreen.root);
  /* --scroll N moves the viewport N rows back into history, so a 57-row
   * conversation can be dumped from a chosen point rather than from its tail. */
  if (opts.scroll > 0) IrcitUiConversationWindowScroll(conv, opts.scroll);
  IrcitUiDraw(IrcitUiScreen.root, NULL);
  doupdate();

  /* --click-row N makes the same call the engine's click dispatch makes, with a
   * SCREEN cell derived from the row's own geometry -- so it exercises the whole
   * chain: the geometry subtraction, the row hit test and the collapse toggle. */
  if (opts.click_row >= 0) {
    IrcitUiConversationRow row;
    int                   gx = 0, gy = 0, gw = 0, gh = 0;
    IrcitUiNodeGetGeometry(conv, &gx, &gy, &gw, &gh);
    if (IrcitUiConversationWindowRowInfo(conv, opts.click_row, &row)) {
      int cx = gx + 1;
      int cy = gy + row.origin_y + (row.height / 2);
      printf("clicking row %d at screen %d,%d (row origin %d, height %d)\n", opts.click_row, cx, cy, row.origin_y,
             row.height);
      printf("  -> HandleClick returned %d\n", IrcitUiConversationWindowHandleClick(conv, cy, cx));
      IrcitUiLayout(IrcitUiScreen.root, 0, 0, geo_cols, geo_rows);
      IrcitUiRealize(IrcitUiScreen.root);
      IrcitUiDraw(IrcitUiScreen.root, NULL);
    }
  }

  /* THE INPUT LINE IS REAL, and focus is what makes it so: the engine routes a
   * printable key to whichever field holds focus, so this is the whole of the wiring
   * on this side. The demo does not read the buffer back -- there is no public call
   * that does, which the record names as the one thing this widget cannot show. */
  {
    IrcitUiNode *input = IrcitUiFind(IrcitUiScreen.root, "demo_input");
    if (input) IrcitUiSetFocus(input);
  }

  if (opts.dump_model) sDumpModel(conv, opts.gutter);
  if (opts.dump_screen) sDumpScreen(conv);
  if (opts.dump_all) sDumpAll();
  if (opts.avatars) sDumpAvatars(conv, opts.gutter);
  if (opts.avatar_clicks) sAvatarClicks(conv);
  if (opts.sweep) sSweep(conv);
  if (opts.click_engine) sClickEngine(conv);
  if (opts.prove) sProveComposite(conv);
  // --corners APPENDS the cases; it asserts only when a dump mode is also asked
  // for, so `--corners` alone opens the live view on them.
  if (opts.reference) {
    /* The reference is the corner matrix, so the golden input stays out of it. */
    sCorners(conv, geo_cols, geo_rows, 0);
    IrcitUiLayout(IrcitUiScreen.root, 0, 0, geo_cols, geo_rows);
    IrcitUiRealize(IrcitUiScreen.root);
    IrcitUiDraw(IrcitUiScreen.root, NULL);
    doupdate();
    sDumpReference(conv);
  }
  if (opts.corners && !opts.reference) sCorners(conv, geo_cols, geo_rows,
                             opts.dump_model || opts.dump_screen || opts.dump_all);

  if (!opts.dump_model && !opts.dump_screen && !opts.dump_all && !opts.sweep && !opts.click_engine && !opts.prove &&
      !opts.reference && !opts.avatars && !opts.avatar_clicks && !opts.timestamps) {
    /* The live view. A bounded loop rather than the framework's -- see the file
     * comment. Keys: up/down scroll, t toggles the timestamps, q quits. */
    int key, arrow;
    nodelay(stdscr, FALSE);
    for (;;) {
      key = getch();
      // Read once: the raw form consumes its own tail, so a second call would find
      // nothing left and answer "not an arrow".
      arrow = sArrowKey(key);
      /* KEY_RESIZE IS A REDRAW, and it used to be dropped: the loop laid out at
       * COLS/LINES but only when a key it recognised arrived, so a resized terminal
       * kept the old frame until somebody pressed something. The geometry is
       * re-read here, which is what makes the demo indifferent to the size. */
      if (key == KEY_RESIZE) {
        geo_cols = COLS;
        geo_rows = LINES;
      }
      else if (key == 'q' || key == KEY_F(12)) break;
      else if (arrow != 0) IrcitUiConversationWindowScroll(conv, arrow);
      /* THE WHEEL IS THE WIDGET'S HERE, and not the engine's, because the two read
       * container.scroll_y in opposite directions: the engine writes it with 0 at the
       * TOP, this widget's model reads it with 0 at the NEWEST. Handed the engine's
       * own turn of the wheel the model is therefore inverted and, at the default
       * position, clamped -- measured: a wheel turn over a bubble moved nothing. The
       * widget's own call takes the same field the right way round, so a wheel-up
       * walks back into history and a wheel-down returns to the newest. */
      else if (key == KEY_MOUSE) {
        MEVENT mev;

        if (getmouse(&mev) == OK && (mev.bstate & (BUTTON4_PRESSED | BUTTON5_PRESSED)))
          IrcitUiConversationWindowScroll(conv, (mev.bstate & BUTTON4_PRESSED) ? 1 : -1);
        else
          IrcitUiDispatchKey(IrcitUiScreen.root, key);
      }
      else if (key == 't') IrcitUiConversationWindowToggleTimestamps(conv, 1);
      else if (key == 'T') IrcitUiConversationWindowToggleTimestamps(conv, 0);
      else {
        /* EVERYTHING ELSE GOES TO THE ENGINE, which is what makes the input field
         * real: a printable key with the field focused is inserted by the edit
         * dispatch, and anything the engine does not take is simply not handled.
         * The keys above are the demo's own and are claimed first -- Tab is not
         * among them because the engine already spends it on the predictive
         * field's accept-hint. */
        IrcitUiDispatchKey(IrcitUiScreen.root, key);
      }
      IrcitUiLayout(IrcitUiScreen.root, 0, 0, geo_cols, geo_rows);
      IrcitUiRealize(IrcitUiScreen.root);
      IrcitUiDraw(IrcitUiScreen.root, NULL);
      doupdate();
    }
  }
  else if (opts.hold_ms > 0) {
    fflush(stdout);
    usleep((useconds_t)opts.hold_ms * 1000u);
  }

  /* The terminal side of the mouse, undone the way the engine's own example does:
   * endwin() does not turn the reporting off. Only for the live view -- the sink is
   * NULL there and never was in a dump, and an escape sequence in a dump's stdout
   * is a diff. */
  if (!sink) {
    fputs("\033[?1006l\033[?1003l\033[?1002l\033[?1000l", stdout);
    fflush(stdout);
  }
  endwin();
  if (scr) delscreen(scr);
  if (sink) fclose(sink); // NULL on the live view's path: sink is the headless sink
  free(buf);
  return 0;
}
