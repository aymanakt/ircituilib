/**
 * @file ui_conversation.c
 * @brief The conversation window's model, its geometry, and the painter that reads them.
 *
 * THE MODEL IS ARITHMETIC AND NOTHING ELSE. It runs on the dataset and the
 * widget's width, with no curses call anywhere in it, so it can be computed,
 * dumped and diffed with no terminal -- which is what makes the frozen model dump
 * a reference rather than a picture of one.
 *
 * ONE GEOMETRY, THREE CONSUMERS. Every layout decision is written ONCE, into the
 * row array, and then read by the painter, by the row accessors an application
 * uses, and -- at Gate 4 -- by the hit test. The design document writes the same
 * arithmetic in three places and the three disagree; the correction is not to
 * agree harder but to compute once.
 *
 * THE CLUSTER ALGORITHM IS A FORWARD PASS OVER RUNS, and the difference from the
 * document's incremental fix-up is worth stating: a run is closed by the pass
 * itself, so a single-message run followed by a different sender cannot be left
 * open. The document's version fixed up only the PREVIOUS message at append time,
 * which is correct but has to be read twice to be believed.
 *
 * IT WRITES THROUGH IrcitUiAddUtf8 -- a measurement, not a preference: Gate 0's `wide`
 * case shows a TEXT_VIEW carries a multi-byte glyph ZERO times because the
 * text-view painter charges one column per byte, and a bubble's whole appearance
 * is box glyphs, check marks and emoji.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "ui_priv.h"
/* For IrcitUiAddUtf8. */
#include "ui_engine_priv.h"
#include "generated/ui_decl_access.h"   /* the generated accessors: one per declared member */
#include "ui_palette_priv.h"
#include "ircitui_conversation_state_type_priv.h"
#include "ircituilib/ircitui_conversation.h"

/* The defaults the node's zero sentinels resolve to (C25 refuses 0 as a value, so
 * zero can only mean "not given"). 65 per cent is the design document's own
 * number; the floor of 25 columns is its own too. */
#define CONV_DEFAULT_MAX_PCT 65
#define CONV_DEFAULT_COLLAPSE_LINES 6
#define CONV_MIN_BODY_COLS 25
/* AN ABSOLUTE CEILING, because a percentage alone is not a ceiling. On a 200-column
 * terminal 65 per cent is 130 columns, so no message ever reaches the truncation
 * limit and nothing is ever collapsed -- the design's "truncates with '...' and
 * opens in place" never happens. A chat bubble has a readable maximum width
 * whether or not the terminal is wide, which is why real clients cap it in
 * columns and not only in proportion. */
#define CONV_MAX_BODY_COLS 58
// The avatar: a 5-column, 3-row half-block grid at the gutter's left edge. The
// gutter itself is the node's own key, and its floor is the timestamps' 6 columns.
#define CONV_AVATAR_COLS 5
#define CONV_AVATAR_ROWS 3
#define CONV_AVATAR_MAX_COLS 12

// The banner: one row, a fixed label, and the padding that keeps the label off its corners.
#define CONV_BANNER_PAD 2
#define CONV_BANNER_LABEL_MAX 64

/* --- the model: clusters, reflow, heights, origins ------------------------- */

/** @brief Two messages in one run: same side, same sender, neither a system line
 *         and neither a status indicator -- a status indicator joins no block (R4). */
static bool
sSameRun(const IrcitUiConversationMessage *a_ptr, const IrcitUiConversationMessage *b_ptr)
{
  if (a_ptr->direction == IRCITUI_MSG_SYSTEM || b_ptr->direction == IRCITUI_MSG_SYSTEM) return false;
  if (a_ptr->kind != IRCITUI_MSG_KIND_MESSAGE || b_ptr->kind != IRCITUI_MSG_KIND_MESSAGE) return false;
  if (a_ptr->direction != b_ptr->direction) return false;
  return strcmp(a_ptr->sender_name, b_ptr->sender_name) == 0;
}

/** @brief The body's reflow width for this widget's width: a share, floored. */
static int
sMaxBodyCols(const IrcitUiConversationState *state_ptr, int width)
{
  int pct = state_ptr->bubble_max_pct > 0 ? state_ptr->bubble_max_pct : CONV_DEFAULT_MAX_PCT;
  int max = (width * pct) / 100;

  if (max > CONV_MAX_BODY_COLS) max = CONV_MAX_BODY_COLS;
  return max < CONV_MIN_BODY_COLS ? CONV_MIN_BODY_COLS : max;
}

/** @brief Lines a body of @p cols COLUMNS occupies at @p max columns to a line (R23). */
static int
sBodyLineCount(int cols, int max)
{
  if (cols <= 0) return 1;
  return (cols + max - 1) / max;
}

/* THE COLUMN MEASURE (R23). A body is measured in COLUMNS: a wide glyph owns two
 * of them and a continuation cell is not one of its own, so a byte length is not a
 * width and a byte offset is not a column. wcwidth is the engine's own measure --
 * ui_engine.c charges a TEXT_VIEW the same way -- and a value it refuses (a
 * combining mark, a control) owns no columns. */
static int
sColumns(const char *str_ptr)
{
  mbstate_t st;
  size_t    len, at = 0;
  int       cols = 0;

  if (!str_ptr) return 0;
  memset(&st, 0, sizeof st);
  len = strlen(str_ptr);
  while (at < len) {
    wchar_t wc;
    size_t  n = mbrtowc(&wc, str_ptr + at, len - at, &st);
    int     w;

    if (n == (size_t)-1 || n == (size_t)-2) { /* not a sequence: one byte, one column */
      memset(&st, 0, sizeof st);
      at++;
      cols++;
      continue;
    }
    if (n == 0) break;
    w = wcwidth(wc);
    cols += w > 0 ? w : 0;
    at += n;
  }
  return cols;
}

/** @brief The bytes of the longest prefix of @p str_ptr that fits in @p max_cols
 *         columns WITHOUT SPLITTING A GLYPH: one that does not fit starts the next line (R23). */
static int
sChunkBytes(const char *str_ptr, int max_cols)
{
  mbstate_t st;
  size_t    len, at = 0;
  int       cols = 0;

  if (!str_ptr || max_cols <= 0) return 0;
  memset(&st, 0, sizeof st);
  len = strlen(str_ptr);
  while (at < len) {
    wchar_t wc;
    size_t  n = mbrtowc(&wc, str_ptr + at, len - at, &st);
    int     w;

    if (n == (size_t)-1 || n == (size_t)-2) {
      memset(&st, 0, sizeof st);
      n = 1;
      w = 1;
    }
    else if (n == 0) break;
    else {
      w = wcwidth(wc);
      if (w < 0) w = 0;
    }
    if (cols + w > max_cols) break;
    cols += w;
    at += n;
  }
  return (int)at;
}

// The name tag's extent, from the model so the painter and the router share one
// derivation: it starts in the gutter when there is one, and ends at the box's edge.
static void
sSetTag(IrcitUiConversationRow *row_ptr, const IrcitUiConversationMessage *msg_ptr, int gutter)
{
  char buf[IRCITUI_SENDER_NAME_MAX + 2];
  int  w;

  if (!row_ptr->name_row) {
    row_ptr->name_x = row_ptr->name_w = 0;
    return;
  }
  snprintf(buf, sizeof buf, "@%s", msg_ptr->sender_name);
  w = sColumns(buf);
  row_ptr->name_x = gutter > 0 ? 0 : row_ptr->box_x + 2;
  if (w > row_ptr->box_x + row_ptr->box_w - row_ptr->name_x)
    w = row_ptr->box_x + row_ptr->box_w - row_ptr->name_x;
  row_ptr->name_w = w > 0 ? w : 0;
}

// The avatar's placement: the block's first conversational inbound row gets one,
// and its ink may run one row past the row that owns it (see the header).
static void
sSetAvatar(IrcitUiConversationRow *row_ptr, const IrcitUiConversationMessage *msg_ptr,
           const IrcitUiConversationState *state_ptr, int gutter)
{
  if (gutter <= 0) return;
  if (!row_ptr->name_row) return;
  if (msg_ptr->kind != IRCITUI_MSG_KIND_MESSAGE) return;
  if (msg_ptr->direction != IRCITUI_MSG_INBOUND) return;
  row_ptr->avatar_x     = 0;
  row_ptr->avatar_y     = row_ptr->origin_y + row_ptr->name_row;
  row_ptr->avatar_rows  = CONV_AVATAR_ROWS;
  row_ptr->avatar_pulse = (state_ptr->is_friend_typing &&
                           strcmp(msg_ptr->sender_name, state_ptr->typing_sender_name) == 0) ? 1 : 0;
}

/* THE BANNER'S LABEL, SPELLED ONCE. The model measures it to size the box and the
 * painter draws it into that box, so a second spelling would be a second width and the
 * label would sit off its own corners. The down arrow is escaped here rather than taken
 * from the painter's G_* block because the MODEL needs it first, and the reason the G_*
 * block gives for refusing the ACS door applies to it unchanged: it reads back as its
 * letter. */
static void
sBannerLabel(char *buf_ptr, size_t cap, int unread)
{
  snprintf(buf_ptr, cap, " \xe2\xac\x87 %d New Messages ", unread);
}

/**
 * @brief Recompute every row: clusters, reflow, heights and origins.
 *
 * The whole model in this pass, and the only writer of @c rows. THREE walks now: a
 * run's ceiling and its mark are decided once for the run, then each row's height,
 * then the box and the origins -- because a block's bubbles share one width, and a
 * member's status must not be able to narrow it (R7).
 */
static void
sComputeRows(IrcitUiConversationState *state_ptr, IrcitUiNode *owner_ptr, int width, int content_h)
{
  size_t i;
  int    cum = 0;
  int    avail;
  /* Whether the reader is at the tail, which the banner's watermark below reads and the
   * offset block decides. TRUE until that block says otherwise, and it is the no-owner
   * answer rather than a default: a model with no viewport has no reading position, and
   * it shows the tail, so it IS at the tail. */
  bool   at_tail = true;
  int    cap = state_ptr->bubble_collapse_lines > 0 ? state_ptr->bubble_collapse_lines
                                                    : CONV_DEFAULT_COLLAPSE_LINES;
  // The node's gutter: the columns every inbound box begins after. Zero when the
  // node asked for none, which is the default and leaves the geometry untouched.
  int    gutter = state_ptr->avatar_gutter_cols > 0 ? state_ptr->avatar_gutter_cols : 0;

  if (!state_ptr->rows && state_ptr->cap) {
    state_ptr->rows = calloc(state_ptr->cap, sizeof *state_ptr->rows);
    if (!state_ptr->rows) return;
  }
  if (!state_ptr->rows) {
    state_ptr->rows_valid = true;
    return;
  }

  // Clusters: maximal runs of one sender, closed by the pass.
  i = 0;
  while (i < state_ptr->count) {
    size_t j = i;
    while (j + 1 < state_ptr->count && sSameRun(&state_ptr->msgs[j], &state_ptr->msgs[j + 1])) j++;
    for (size_t k = i; k <= j; k++) {
      if (i == j) state_ptr->msgs[k].cluster_pos = IRCITUI_CLUSTER_LONE;
      else if (k == i) state_ptr->msgs[k].cluster_pos = IRCITUI_CLUSTER_TOP;
      else if (k == j) state_ptr->msgs[k].cluster_pos = IRCITUI_CLUSTER_BOTTOM;
      else state_ptr->msgs[k].cluster_pos = IRCITUI_CLUSTER_MIDDLE;
    }
    i = j + 1;
  }

  // The run first: its ceiling and its box, before any height is known. A block's
  // bubbles share one width, taken from the room the box will have, so nothing overruns.
  for (i = 0; i < state_ptr->count;) {
    size_t j = i;
    int    box_room, max, mark, run_body, bw;
    bool   outbound;

    if (state_ptr->msgs[i].direction == IRCITUI_MSG_SYSTEM) {
      // Cleared here, because this walk is the only one every row passes through.
      memset(&state_ptr->rows[i], 0, sizeof state_ptr->rows[i]);
      state_ptr->rows[i].avatar_x = -1;
      // A notice is centred and unwrapped, so its ceiling is the widget's own:
      // held here so the number the model records for it does not move.
      state_ptr->rows[i].max_width = sMaxBodyCols(state_ptr, width);
      i++;
      continue;
    }
    while (j + 1 < state_ptr->count && sSameRun(&state_ptr->msgs[j], &state_ptr->msgs[j + 1])) j++;

    outbound = state_ptr->msgs[i].direction == IRCITUI_MSG_OUTBOUND;
    // The mark's three columns are the RUN's, not the row's: an expired member must
    // not narrow the box its neighbours share, or their shared border would not line up.
    mark = 0;
    for (size_t k = i; k <= j; k++)
      if (state_ptr->msgs[k].direction == IRCITUI_MSG_OUTBOUND &&
          state_ptr->msgs[k].status != IRCITUI_STATUS_EXPIRED)
        mark = 3;

    box_room = width - (outbound ? 0 : gutter);
    if (box_room < 5) box_room = 5;
    max = sMaxBodyCols(state_ptr, width);
    if (max > box_room - 4 - mark) max = box_room - 4 - mark;
    if (max < 1) max = 1;

    run_body = 0;
    for (size_t k = i; k <= j; k++) {
      IrcitUiConversationRow *row  = &state_ptr->rows[k];
      int                     cols = sColumns(state_ptr->msgs[k].text_content);

      memset(row, 0, sizeof *row);
      row->avatar_x = -1;
      row->max_width = max;
      row->body_cols = cols < max ? cols : max;
      if (row->body_cols > run_body) run_body = row->body_cols;
    }
    // The box, and the proof it fits: run_body <= max <= box_room - 4 - mark, so
    // box_x + box_w is at most width, whatever the widget's width and gutter are.
    bw = run_body + 4 + mark;
    if (bw < 6 && box_room >= 6) bw = 6;
    for (size_t k = i; k <= j; k++) {
      IrcitUiConversationRow *row = &state_ptr->rows[k];
      row->body_cols = run_body;
      row->box_w     = bw;
      row->box_x     = outbound ? width - bw : gutter;
      if (row->box_x < 0) row->box_x = 0;
    }
    i = j + 1;
  }

  // Then each row's own costs, its origin, and the two things that live in the
  // gutter and above the box: the name tag and the avatar.
  for (i = 0; i < state_ptr->count; i++) {
    const IrcitUiConversationMessage *msg = &state_ptr->msgs[i];
    IrcitUiConversationRow           *row = &state_ptr->rows[i];
    int                               cols = sColumns(msg->text_content);
    int                               full, lines;

    row->msg_index = i;
    row->cluster   = msg->cluster_pos;
    row->name_row  = (msg->direction == IRCITUI_MSG_INBOUND
                      && (row->cluster == IRCITUI_CLUSTER_LONE || row->cluster == IRCITUI_CLUSTER_TOP))
                       ? 1 : 0;
    // TWO ROWS FOR A QUOTE: the quoted line, and the divider that closes it (R22).
    row->reply_rows = msg->has_reply_context ? 2 : 0;
    row->badge_row  = msg->reaction_count > 0 ? 1 : 0;
    /* EVERY BUBBLE HAS A TOP RULE (R6, R12). The block's first bubble draws the
     * block's top; every other bubble draws the border it SHARES with the bubble
     * above it -- one row, which is what makes a block read as one shape. The
     * painter tells the two apart by the cluster: LONE/TOP is the block's top,
     * MIDDLE/BOTTOM is a shared border. */
    row->box_top    = 1;
    // The block's LAST bubble has the end rule, and the tail is its bottom-left * corner -- one row, which is why a tail costs no extra row.
    row->box_bottom = (row->cluster == IRCITUI_CLUSTER_LONE || row->cluster == IRCITUI_CLUSTER_BOTTOM) ? 1 : 0;
    // The stamp closes a RUN, not a bubble: joined consecutive messages carry one on their last member -- the block's time is its end's. A lone message is both its run's first and last, so it always carries one.
    row->stamp_row = (state_ptr->is_showing_timestamps && (row->cluster == IRCITUI_CLUSTER_LONE ||
                                                          row->cluster == IRCITUI_CLUSTER_BOTTOM))
                         ? 1
                         : 0;

    if (msg->direction == IRCITUI_MSG_SYSTEM) {
      // A system notice is one centred line; its span is what the painter centres
      // on, so the hit test reads the same pair rather than knowing about centring.
      row->body_cols  = cols;
      row->body_lines = 1;
      row->full_lines = 1;
      row->height     = 1;
      row->box_top = row->box_bottom = 0;
      row->name_row = row->reply_rows = row->badge_row = row->stamp_row = 0;
      /* CENTRED ON WHAT IS DRAWN, brackets and all: the painter's line is
       * "[ <text> ]", so the span the hit test reads and the column the painter
       * starts at are the same pair -- the model's, not a second formula. */
      // Fitted to the room like every other box, so a notice longer than a narrow
      // widget cannot claim cells the widget does not have.
      row->box_w = cols + 4;
      if (row->box_w > width) row->box_w = width < 1 ? 1 : width;
      row->box_x = (width - row->box_w) / 2;
      if (row->box_x < 0) row->box_x = 0;
    }
    else {
      full = sBodyLineCount(cols, row->max_width);
      /* TRUNCATION IS THE FLAG AND NOTHING ELSE, and it is a CONVERSATIONAL rule
       * (R24): a status indicator draws its whole body -- one clipped row for a
       * notice, its full height for a local indicator -- so R24 and R25 leave it
       * alone. The flag itself is the message's own state, set once when it is
       * appended (see Append) or by the application, and flipped by a click. */
      row->truncated  = msg->kind == IRCITUI_MSG_KIND_MESSAGE && msg->is_collapsed && full > cap;
      lines           = row->truncated ? (cap < 1 ? 1 : cap) : full;
      row->full_lines = full;
      row->body_lines = lines;
      /* The bubble's box rules, the name tag, the quote and its divider, the badge
       * row. The height is allocated here rather than discovered by the painter: a
       * height the painter finds late is the defect that put the badges on the
       * border in the first place. */
      row->height = row->name_row + row->reply_rows + lines + row->box_top + row->box_bottom + row->badge_row +
                    row->stamp_row;
    }
    row->origin_y = cum;
    cum += row->height;
    if (msg->direction != IRCITUI_MSG_SYSTEM) {
      sSetTag(row, msg, gutter);
      sSetAvatar(row, msg, state_ptr, gutter);
    }
  }

  /* THE TYPING ROW RESERVES ITS OWN SPACE. It is drawn on the widget's last row,
   * so the messages are fitted into the rows ABOVE it rather than into all of
   * them -- otherwise the indicator is painted over the last bubble's bottom rule,
   * which is what the frame dump showed: "|~Charlie is typing...~~~~~~~~~~~~|"
   * with the rule's glyphs still either side of the text.
   *
   * The design document reserved it the same way, by counting the typing row as
   * one more item than the dataset holds. */
  avail = content_h;

  // The offset counts DOWN from the oldest row, the engine's own convention, so the bar,
  // the wheel and the drag all read it the right way round; this state owns the anchor.
  {
    int offset = cum > avail ? cum - avail : 0;
    if (owner_ptr) {
      int  delta;
      // At the tail the new ceiling is adopted, so appends and reconciles are followed;
      // parked anywhere else the offset is only clamped. A fresh node reads as the tail.
      at_tail = owner_ptr->container.scroll_y >= owner_ptr->container.columns;
      if (owner_ptr->container.scroll_y > offset) owner_ptr->container.scroll_y = offset;
      if (owner_ptr->container.scroll_y < 0) owner_ptr->container.scroll_y = 0;
      owner_ptr->container.columns = offset;
      if (at_tail) owner_ptr->container.scroll_y = offset;
      delta = owner_ptr->container.scroll_y;
      // The avatar rides its own row, overhang and all: one delta, applied to both.
      for (i = 0; i < state_ptr->count; i++) {
        state_ptr->rows[i].origin_y -= delta;
        if (state_ptr->rows[i].avatar_rows > 0) state_ptr->rows[i].avatar_y -= delta;
      }
    }
    else {
      // No owner, so no offset to read: a headless model shows the tail, as it always has.
      for (i = 0; i < state_ptr->count; i++) {
        state_ptr->rows[i].origin_y -= offset;
        if (state_ptr->rows[i].avatar_rows > 0) state_ptr->rows[i].avatar_y -= offset;
      }
    }
  }

  state_ptr->rows_width          = width;
  state_ptr->content_h           = content_h;
  state_ptr->rows_scroll         = owner_ptr ? owner_ptr->container.scroll_y : 0;
  // Every input the rows were computed at, so a config change cannot be missed.
  state_ptr->rows_gutter         = state_ptr->avatar_gutter_cols;
  state_ptr->rows_max_pct        = state_ptr->bubble_max_pct;
  state_ptr->rows_collapse_lines = state_ptr->bubble_collapse_lines;
  state_ptr->rows_timestamps     = state_ptr->is_showing_timestamps;

  // The watermark first: a pass that finds the reader at the tail is caught up by
  // definition, and a pass that does not counts what arrived past the mark.
  if (state_ptr->seen_count > state_ptr->count) state_ptr->seen_count = state_ptr->count;
  if (at_tail) state_ptr->seen_count = state_ptr->count;
  state_ptr->unread_in = state_ptr->unread_out = 0;
  for (i = state_ptr->seen_count; i < state_ptr->count; i++) {
    // OUTBOUND or not: a system notice counts with the inbound, being unseen by the reader.
    if (state_ptr->msgs[i].direction == IRCITUI_MSG_OUTBOUND) state_ptr->unread_out++;
    else state_ptr->unread_in++;
  }

  // The box, measured from the label the painter will draw, so the label cannot sit off
  // its own corners; clipped to the widget, and banner_w == 0 is how "no banner" is said.
  state_ptr->banner_w = 0;
  if (state_ptr->unread_in > 0) {
    char label[CONV_BANNER_LABEL_MAX];
    int  cols;

    sBannerLabel(label, sizeof label, state_ptr->unread_in);
    cols = sColumns(label) + 2 * CONV_BANNER_PAD;
    if (cols > width) cols = width;
    state_ptr->banner_y = content_h > 0 ? content_h - 1 : 0;
    state_ptr->banner_x = (width - cols) / 2;
    state_ptr->banner_w = cols;
  }

  state_ptr->rows_valid          = true;
}

static void
sResolveConfig(IrcitUiConversationState *state_ptr, const IrcitUiNode *node_ptr)
{
  state_ptr->bubble_max_pct        = IrcitUiDeclBubbleMaxPct(node_ptr);
  state_ptr->bubble_collapse_lines = IrcitUiDeclBubbleCollapseLines(node_ptr);
  // The gutter, clamped here as well as in the setter and the validator: a value written into the node directly must not be able to ask for a negative room. A setter's override wins over the declaration, because a setter is a host asking for this node and the declaration is what the layout asked for.
  state_ptr->avatar_gutter_cols = state_ptr->avatar_gutter_override > 0
                                    ? state_ptr->avatar_gutter_override
                                    : IrcitUiDeclBubbleAvatarGutterCols(node_ptr);
  if (state_ptr->avatar_gutter_cols < 0) state_ptr->avatar_gutter_cols = 0;
  if (state_ptr->avatar_gutter_cols > CONV_AVATAR_MAX_COLS) state_ptr->avatar_gutter_cols = CONV_AVATAR_MAX_COLS;
}

/** @brief The node's content box, and the model brought up to date for it. */
/**
 * @brief The container that owns @p node_ptr's model.
 *
 * The public seam takes the CONVERSATION WINDOW -- the node an application
 * resolves with IrcitUiFind. The painter and the hit test take the VIEWPORT,
 * because that is the node the engine dispatches to. Both routes resolve here, so
 * the model has exactly one owner and the two cannot disagree about which it is.
 */
static IrcitUiNode *
sModelOwner(IrcitUiNode *node_ptr)
{
  if (!node_ptr) return NULL;
  if (node_ptr->common.kind == IRCITUI_CONVERSATION_WINDOW) return node_ptr;
  if (node_ptr->common.kind == IRCITUI_MESSAGE_VIEWPORT) return node_ptr->common.parent;
  return NULL;
}

/** @brief The container's viewport child, or NULL before one has been made. */
static IrcitUiNode *
sViewportOf(IrcitUiNode *owner_ptr)
{
  int i;

  if (!owner_ptr || owner_ptr->common.kind != IRCITUI_CONVERSATION_WINDOW) return NULL;
  for (i = 0; i < owner_ptr->container.n_children; i++)
    if (owner_ptr->container.children[i]->common.kind == IRCITUI_MESSAGE_VIEWPORT) {
      return owner_ptr->container.children[i];
    }
  return NULL;
}

static void sReconcile(IrcitUiConversationState *state_ptr, IrcitUiNode *node_ptr);

/** @brief The model, computed against an explicitly given box. */
static IrcitUiConversationState *
sModelAt(IrcitUiNode *owner_ptr, int w, int h)
{
  IrcitUiConversationState *state_ptr;

  if (!owner_ptr) return NULL;
  state_ptr = owner_ptr->common.widget_state;
  if (!state_ptr) return NULL;

  /* The node's configuration, resolved every time: it is two int copies, and a
   * gate on "have I resolved yet" was a flag that could be wrong in exactly the
   * case that matters -- the first frame after a clone. */
  sResolveConfig(state_ptr, owner_ptr);

  // A clone inherits the adapter but not the dataset, so its first pass reads the
  // host's storage before any geometry is computed from it.
  if (state_ptr->needs_reconcile) sReconcile(state_ptr, owner_ptr);

  if (w < 1) w = 1;
  if (h < 1) h = 1;
  // Every input sComputeRows reads, the configuration included: a value changed
  // under a live widget must recompute rather than be silently ignored.
  if (!state_ptr->rows_valid || state_ptr->rows_width != w || state_ptr->content_h != h ||
      state_ptr->rows_scroll != (owner_ptr->container.scroll_y) ||
      state_ptr->rows_gutter != state_ptr->avatar_gutter_cols ||
      state_ptr->rows_max_pct != state_ptr->bubble_max_pct ||
      state_ptr->rows_collapse_lines != state_ptr->bubble_collapse_lines ||
      state_ptr->rows_timestamps != state_ptr->is_showing_timestamps) {
    sComputeRows(state_ptr, owner_ptr, w, h);
  }
  return state_ptr;
}

static IrcitUiConversationState *
sModel(IrcitUiNode *node_ptr, int *w_out, int *h_out)
{
  IrcitUiNode *owner_ptr = sModelOwner(node_ptr);
  IrcitUiNode *vp_ptr    = sViewportOf(owner_ptr);
  int          w, h;

  if (!owner_ptr) return NULL;

  // The box is the viewport's once there is one: it is what a click lands on and
  // what the bubbles are clipped to, so the geometry is measured against it.
  if (vp_ptr) {
    w = vp_ptr->common.w;
    h = vp_ptr->common.h;
  }
  else {
    w = owner_ptr->common.w - (owner_ptr->common.padding[1] + owner_ptr->common.padding[3]);
    h = owner_ptr->common.h - (owner_ptr->common.padding[0] + owner_ptr->common.padding[2]);
  }
  if (w_out) *w_out = w < 1 ? 1 : w;
  if (h_out) *h_out = h < 1 ? 1 : h;
  return sModelAt(owner_ptr, w, h);
}

IrcitUiConversationState *
IrcitUiConversationStateAlloc(void)
{
  return calloc(1, sizeof(IrcitUiConversationState));
}

/** @brief Give back every message's owned strings. msgs[] and rows[] stay. */
static void
sFreeMsgStrings(IrcitUiConversationState *state_ptr)
{
  size_t i;

  for (i = 0; i < state_ptr->count; i++) {
    free(state_ptr->msgs[i].text_content);
    free(state_ptr->msgs[i].reply_snippet);
  }
}

void
IrcitUiConversationStateRelease(IrcitUiConversationState *state_ptr)
{
  if (!state_ptr) return;
  /* What the state OWNS and not the state: sFreeWidgetState's tail frees the
   * struct, as it does for every kind -- the division the bar's branch makes when
   * it gives back its colour ramp. */
  sFreeMsgStrings(state_ptr);
  free(state_ptr->msgs);
  free(state_ptr->rows);
  state_ptr->msgs  = NULL;
  state_ptr->rows  = NULL;
  state_ptr->count = 0;
  state_ptr->cap   = 0;
}

static char *
sDupOrNull(const char *src_ptr)
{
  return src_ptr ? strdup(src_ptr) : NULL;
}

/* The two owned strings, and the failure path that clears the slot. A shallow copy
 * would leave the widget reading a buffer the caller has already reused -- and the
 * caller is told it may, in ircitui_conversation.h. */
static int
sMoveMsg(IrcitUiConversationMessage *dest_ptr, const IrcitUiConversationMessage *src_ptr)
{
  *dest_ptr = *src_ptr;
  dest_ptr->text_content  = sDupOrNull(src_ptr->text_content);
  dest_ptr->reply_snippet = sDupOrNull(src_ptr->reply_snippet);
  if (src_ptr->text_content && !dest_ptr->text_content) {
    memset(dest_ptr, 0, sizeof *dest_ptr);
    return 0;
  }
  return 1;
}

/* A body too long to read in one go starts collapsed, so the ellipsis and the expand
 * gesture are the default for a long message. The bound is WIDTH-INDEPENDENT -- lines
 * times the narrowest body the model allows -- because a caller may run before there
 * is a width to measure against, and a per-width decision would make the same dataset
 * collapse differently on two terminals. */
static void
sApplyCollapseRule(const IrcitUiConversationState *state_ptr, IrcitUiConversationMessage *msg_ptr)
{
  int cap, n;

  if (msg_ptr->is_collapsed || msg_ptr->kind != IRCITUI_MSG_KIND_MESSAGE) return;
  cap = state_ptr->bubble_collapse_lines > 0 ? state_ptr->bubble_collapse_lines
                                             : CONV_DEFAULT_COLLAPSE_LINES;
  n   = sColumns(msg_ptr->text_content);
  if (n > cap * CONV_MIN_BODY_COLS) msg_ptr->is_collapsed = true;
}

/** @brief The snapshot's message with this id, or NULL.
 *
 *  A linear scan, so a reconcile is O(n) when ids ascend -- which an append-only chat
 *  produces -- and O(n^2) when they do not. */
static const IrcitUiConversationMessage *
sFindMsgById(const IrcitUiConversationState *state_ptr, uint64_t id)
{
  size_t i;

  for (i = 0; i < state_ptr->count; i++)
    if (state_ptr->msgs[i].message_id == id) return &state_ptr->msgs[i];
  return NULL;
}

/* Re-read the host's dataset into the snapshot. KEYED ON message_id so a refresh
 * keeps the user's collapse toggles; a message the widget does not hold is appended
 * under Append's own rule, so both front doors agree on what a long body looks like.
 *
 * The old snapshot survives any failure here: a half-swapped dataset would be worse
 * than a stale one. */
static void
sReconcile(IrcitUiConversationState *state_ptr, IrcitUiNode *node_ptr)
{
  IrcitUiConversationMessage *new_msgs;
  IrcitUiConversationRow     *grown_rows;
  size_t                      n, i;

  if (!state_ptr->adapter.get_item_count || !state_ptr->adapter.get_item_info) return;

  state_ptr->needs_reconcile = false;
  n = state_ptr->adapter.get_item_count(node_ptr, state_ptr->host_dataset_ctx);
  if (n == 0) {
    sFreeMsgStrings(state_ptr);
    free(state_ptr->msgs);
    state_ptr->msgs  = NULL;
    state_ptr->count = 0;
    state_ptr->cap   = 0;
    state_ptr->rows_valid = false;
    return;
  }

  new_msgs = calloc(n, sizeof *new_msgs);
  if (!new_msgs) return;

  for (i = 0; i < n; i++) {
    IrcitUiConversationMessage        msg;
    const IrcitUiConversationMessage *old;

    memset(&msg, 0, sizeof msg);
    if (!state_ptr->adapter.get_item_info(node_ptr, state_ptr->host_dataset_ctx, i, &msg)) continue;
    old = sFindMsgById(state_ptr, msg.message_id);
    if (old) msg.is_collapsed = old->is_collapsed;
    else     sApplyCollapseRule(state_ptr, &msg);
    if (!sMoveMsg(&new_msgs[i], &msg)) continue;
  }

  // rows[] is sized once by sComputeRows and only ever grown by Append, so raising
  // count without this writes past its end -- first hit by an initial history load.
  grown_rows = realloc(state_ptr->rows, n * sizeof *grown_rows);
  if (!grown_rows) {
    for (i = 0; i < n; i++) {
      free(new_msgs[i].text_content);
      free(new_msgs[i].reply_snippet);
    }
    free(new_msgs);
    return;
  }
  state_ptr->rows = grown_rows;

  sFreeMsgStrings(state_ptr);
  free(state_ptr->msgs);
  state_ptr->msgs       = new_msgs;
  state_ptr->count      = n;
  state_ptr->cap        = n;
  state_ptr->rows_valid = false;
}

/* The node's state, made if this node has none.
 *
 * THE MODEL NODE HAS NONE. A generated model is a static initialiser and
 * widget_state is not among the fields it names (generated/ui_generated.c:835), so
 * without this every bind to the model is refused and the documented "bind the
 * model" rule -- ircitui_conversation.h:16-20 -- has no route. Allocating the
 * CORRECT type when it is absent is safe; it is REPLACING one that is not (see
 * IrcitUiConversationStateRelease). Same idiom as the engine's own lazy paths. */
static IrcitUiConversationState *
sStateEnsure(IrcitUiNode *node_ptr)
{
  IrcitUiConversationState *state_ptr = node_ptr->common.widget_state;

  if (state_ptr) return state_ptr;
  state_ptr = IrcitUiConversationStateAlloc();
  if (state_ptr) node_ptr->common.widget_state = state_ptr;
  return state_ptr;
}

int
IrcitUiConversationWindowBindAdapter(IrcitUiNode *node_ptr,
                                     const IrcitUiConversationAdapter *adapter_ptr,
                                     void *host_ctx)
{
  IrcitUiConversationState *state_ptr;

  if (!node_ptr) return 0;
  node_ptr = sModelOwner(node_ptr);
  if (!node_ptr) return 0;
  if (adapter_ptr && (!adapter_ptr->get_item_count || !adapter_ptr->get_item_info)) return 0;
  state_ptr = sStateEnsure(node_ptr);
  if (!state_ptr) return 0;

  if (adapter_ptr) state_ptr->adapter = *adapter_ptr;
  else             memset(&state_ptr->adapter, 0, sizeof state_ptr->adapter);
  state_ptr->host_dataset_ctx = host_ctx;

  if (state_ptr->adapter.get_item_count && state_ptr->adapter.get_item_info) {
    state_ptr->needs_reconcile = true;
    sReconcile(state_ptr, node_ptr);
  } else {
    // Detached: the snapshot came from a host that is no longer bound, so it goes.
    sFreeMsgStrings(state_ptr);
    free(state_ptr->msgs);
    state_ptr->msgs            = NULL;
    state_ptr->count           = 0;
    state_ptr->cap             = 0;
    state_ptr->rows_valid      = false;
    state_ptr->needs_reconcile = false;
  }

  IrcitUiRequestLayout(node_ptr);
  IrcitUiInvalidate(node_ptr);
  return 1;
}

void
IrcitUiConversationWindowNotifyDatasetChanged(IrcitUiNode *node_ptr)
{
  IrcitUiNodePair           pair;
  IrcitUiConversationState *state_ptr;
  IrcitUiNode              *owner_ptr;
  int                       pass;

  if (!node_ptr) return;
  // The host may hold either tree; resolve to both so the notify reaches the screen
  // clone that paints as well as the model a later clone inherits from.
  pair = IrcitUiFindBoth(IrcitUiNodeGetId(node_ptr));

  for (pass = 0; pass < 2; pass++) {
    IrcitUiNode *vp_ptr;

    owner_ptr = sModelOwner(pass ? pair.screen : pair.model);
    if (!owner_ptr || !owner_ptr->common.widget_state) continue;
    state_ptr = owner_ptr->common.widget_state;
    if (!state_ptr->adapter.get_item_count) continue;
    sReconcile(state_ptr, owner_ptr);
    // A dataset change repaints and cannot move the pane: the widget's measured size is configuration, never the content.
    // Until a viewport has a box, the layout is what creates it; after that the repaint is the whole of a dataset change.
    vp_ptr = sViewportOf(owner_ptr);
    if (!vp_ptr || vp_ptr->common.w < 1 || vp_ptr->common.h < 1) IrcitUiRequestLayout(owner_ptr);
    IrcitUiInvalidate(owner_ptr);
  }
}

/* --- the application's seam ------------------------------------------------ */

int
IrcitUiConversationWindowAppend(IrcitUiNode *node_ptr, const IrcitUiConversationMessage *msg_ptr)
{
  IrcitUiConversationState   *state_ptr;
  IrcitUiConversationMessage *dest_ptr;

  if (!node_ptr || !msg_ptr) return 0;
  node_ptr = sModelOwner(node_ptr);
  if (!node_ptr) return 0;
  state_ptr = node_ptr->common.widget_state;
  if (!state_ptr) return 0;

  if (state_ptr->count == state_ptr->cap) {
    size_t                      new_cap = state_ptr->cap ? state_ptr->cap * 2 : 16;
    IrcitUiConversationMessage *grown   = realloc(state_ptr->msgs, new_cap * sizeof *grown);
    IrcitUiConversationRow     *grown_r;
    if (!grown) return 0; /* CHECKED: the document's version loses the array here. */
    state_ptr->msgs = grown;
    // The rows grow with the data, and neither array holds a pointer into the * other -- a realloc of one cannot invalidate the other.
    grown_r = realloc(state_ptr->rows, new_cap * sizeof *grown_r);
    if (!grown_r) return 0;
    state_ptr->rows = grown_r;
    state_ptr->cap  = new_cap;
  }

  dest_ptr = &state_ptr->msgs[state_ptr->count];
  if (!sMoveMsg(dest_ptr, msg_ptr)) return 0;
  sApplyCollapseRule(state_ptr, dest_ptr);
  state_ptr->count++;

  // No anchor arithmetic here: a message lengthens the model at the BOTTOM, so a parked
  // reader keeps its place and sComputeRows follows one that was at the tail.
  state_ptr->rows_valid = false;

  IrcitUiRequestLayout(node_ptr);
  IrcitUiInvalidate(node_ptr);
  return 1;
}

void
IrcitUiConversationWindowSetTyping(IrcitUiNode *node_ptr, int is_typing, const char *sender_ptr)
{
  IrcitUiConversationState *state_ptr;

  node_ptr = sModelOwner(node_ptr);
  if (!node_ptr) return;
  state_ptr = node_ptr->common.widget_state;
  if (!state_ptr) return;

  if (state_ptr->is_friend_typing == (is_typing ? true : false) && !sender_ptr) return;

  state_ptr->is_friend_typing = is_typing ? true : false;
  if (sender_ptr) {
    strncpy(state_ptr->typing_sender_name, sender_ptr, sizeof state_ptr->typing_sender_name - 1);
    state_ptr->typing_sender_name[sizeof state_ptr->typing_sender_name - 1] = 0;
  }
  // The indicator OWNS a row, so showing or hiding it moves every origin: the * geometry is stale the moment this flag changes and must be recomputed.
  state_ptr->rows_valid = false;
  IrcitUiRequestLayout(node_ptr);
  IrcitUiInvalidate(node_ptr);
}

void
IrcitUiConversationWindowToggleTimestamps(IrcitUiNode *node_ptr, int visible)
{
  IrcitUiConversationState *state_ptr;

  node_ptr = sModelOwner(node_ptr);
  if (!node_ptr) return;
  state_ptr = node_ptr->common.widget_state;
  if (!state_ptr) return;

  if (state_ptr->is_showing_timestamps == (visible ? true : false)) return;
  state_ptr->is_showing_timestamps = visible ? true : false;
  // The stamp is a row inside every bubble, so this moves every origin: a layout
  // change, not the repaint the old margin was.
  state_ptr->rows_valid = false;
  IrcitUiRequestLayout(node_ptr);
  IrcitUiInvalidate(node_ptr);
}

void
IrcitUiConversationWindowSetAvatarGutter(IrcitUiNode *node_ptr, int cols)
{
  IrcitUiConversationState *state_ptr;

  node_ptr = sModelOwner(node_ptr);
  if (!node_ptr) return;
  state_ptr = node_ptr->common.widget_state;
  if (!state_ptr) return;

  if (cols < 0) cols = 0;
  if (cols > CONV_AVATAR_MAX_COLS) cols = CONV_AVATAR_MAX_COLS;
  if (state_ptr->avatar_gutter_override == cols) return;
  state_ptr->avatar_gutter_override = cols;
  state_ptr->rows_valid = false;
  IrcitUiRequestLayout(node_ptr);
  IrcitUiInvalidate(node_ptr);
}

/**
 * @brief Set the unread banner's ink: a palette role for its colours, and an attribute mask
 *        for its emphasis.
 *
 * A ROLE AND A MASK, NOT A COLOUR. The slot is one of the nine the palette resolves per
 * terminal, so the same call draws on a 256-colour terminal, an eight-colour one and one
 * with no colour at all; the mask is the application's own emphasis, and it REPLACES what
 * the role would have carried rather than adding to it, so a banner can be asked for plain.
 * <tt>IrcitUiConversationWindowSetNewMsgBannerStyle(node, IRCITUI_SLOT_TEXT_ERROR, A_BOLD)</tt>
 * is an emphasised alert; a mask of 0 is the role's colours and nothing more, which is what
 * every banner is until this is called.
 *
 * IT TAKES EFFECT ON THE NEXT FRAME, and it needs nothing else to be called: a style moves
 * no cell, so the geometry stands and there is no relayout -- only an invalidate, so the
 * frame that follows redraws the banner and every cell it covers. The framework does not
 * render from inside a setter; a caller that wants the glass repainted before this returns
 * asks for a frame itself with @ref IrcitUiRenderFrame.
 *
 * WHICH NODE. The style is a NODE field, which @c IrcitUiClone copies wholesale, so setting
 * it on the model before the screen tree is built reaches the clone that paints -- and
 * setting it on the node the application already paints (@c IrcitUiFind(IrcitUiScreen.root,
 * ...)) takes effect at once. A call on the model AFTER a clone exists reaches the next
 * clone, not the one on the glass, which is the rule every node field in this widget follows.
 *
 * @param node_ptr The conversation window, or its viewport.
 *
 * @param slot The palette role for the banner's colours.
 *
 * @param attrs Attributes OR'd onto the role's colours: @c A_BOLD, @c A_REVERSE, @c A_DIM.
 *              0 for plain.
 */
void
IrcitUiConversationWindowSetNewMsgBannerStyle(IrcitUiNode *node_ptr, IrcitUiPaletteSlot slot, chtype attrs)
{
  IrcitUiConversationState *state_ptr;

  node_ptr = sModelOwner(node_ptr);
  if (!node_ptr) return;
  // A slot that names no role is refused rather than clamped: there is no nearest one, and
  // silently drawing in another is worse than leaving the ink as it was.
  if (slot < 0 || slot >= IRCITUI_SLOT_COUNT) return;
  /* The ink moved into the state when its member left the node (§7), so a model node
     needs one: the whole point of this call is to be reachable from the clone the model
     produces, and a model without a state would swallow it. The old comment here said the
     opposite -- "the field is the node's, so a model-node call is not swallowed" -- and
     that was the reason it was on the node; the allocation is what replaces it. */
  state_ptr = node_ptr->common.widget_state;
  if (!state_ptr) {
    state_ptr = IrcitUiConversationStateAlloc();
    if (!state_ptr) return;
    node_ptr->common.widget_state = state_ptr;
  }
  if (state_ptr->banner_slot == (int)slot + 1 && state_ptr->banner_attrs == attrs) return;

  state_ptr->banner_slot  = (int)slot + 1;
  state_ptr->banner_attrs = attrs;
  // Paint only, the rule Animate follows: no rows_valid, no relayout.
  IrcitUiInvalidate(node_ptr);
}

int
IrcitUiConversationWindowSetAvatarProvider(IrcitUiNode *node_ptr,
                                           const IrcitUiAvatarProvider *provider_ptr)
{
  IrcitUiConversationState *state_ptr;

  if (!node_ptr) return 0;
  node_ptr = sModelOwner(node_ptr);
  if (!node_ptr) return 0;
  state_ptr = sStateEnsure(node_ptr);
  if (!state_ptr) return 0;

  if (provider_ptr) state_ptr->avatar_provider = *provider_ptr;
  else              memset(&state_ptr->avatar_provider, 0, sizeof state_ptr->avatar_provider);

  // Ink only: the block's size does not depend on the glyph, so no geometry pass.
  IrcitUiInvalidate(node_ptr);
  return 1;
}

int
IrcitUiConversationWindowAnimationDue(const IrcitUiNode *node_ptr)
{
  IrcitUiNode              *owner_ptr = sModelOwner((IrcitUiNode *)node_ptr);
  IrcitUiConversationState *state_ptr;
  int                       w = 0, h = 0;
  size_t                    i;

  if (!owner_ptr) return 0;
  state_ptr = owner_ptr->common.widget_state;
  if (!state_ptr || !state_ptr->is_friend_typing) return 0;

  // The rows are what say whether a drawn avatar belongs to the typing sender.
  sModel(owner_ptr, &w, &h);
  if (!state_ptr->rows) return 0;
  for (i = 0; i < state_ptr->count; i++)
    if (state_ptr->rows[i].avatar_rows > 0 && state_ptr->rows[i].avatar_pulse) return 1;
  return 0;
}

void
IrcitUiConversationWindowAnimate(IrcitUiNode *node_ptr, int phase)
{
  IrcitUiConversationState *state_ptr;

  node_ptr = sModelOwner(node_ptr);
  if (!node_ptr) return;
  state_ptr = node_ptr->common.widget_state;
  if (!state_ptr) return;

  phase = phase ? 1 : 0;
  if (state_ptr->anim_phase == phase) return;
  // Paint only: the phase moves no cell, so the geometry it was computed at stands.
  state_ptr->anim_phase = phase;
  IrcitUiInvalidate(node_ptr);
}

int
IrcitUiConversationWindowMessageInfo(const IrcitUiNode *node_ptr, int index, IrcitUiConversationMessage *out_ptr)
{
  IrcitUiConversationState *state_ptr;

  if (!node_ptr || !out_ptr || index < 0) return 0;
  node_ptr = sModelOwner((IrcitUiNode *)node_ptr);
  if (!node_ptr) return 0;
  state_ptr = node_ptr->common.widget_state;
  if (!state_ptr || (size_t)index >= state_ptr->count) return 0;
  // A SHALLOW COPY, deliberately: the strings stay the widget's, and the caller is * told they are borrowed. Deep-copying here would make every dump allocate.
  *out_ptr = state_ptr->msgs[index];
  return 1;
}

/*
 * THE ROUTER: one resolver, two thin public wrappers, so the row walk and the box
 * test exist once. It classifies from the model and re-derives nothing -- each row
 * already carries its origin, height, box and per-kind row counts, and the painter
 * draws them in a fixed order, which is the order walked here:
 *
 *     name tag -> top rule -> reply header -> body lines -> badges -> stamp -> bottom rule
 *
 * The counts sum to the row's height, so the walk cannot run past its last cell. A
 * cell it does not claim is NONE, never the nearest thing.
 *
 * THE GUTTER'S TWO BANDS COME FIRST, and only when there is a gutter: an avatar is
 * strictly left of every box (box_x is at least the gutter), but its ink may sit on
 * a row that another row's band owns, so the walk below could never attribute it.
 */
static IrcitUiConversationTokenTarget
sResolveToken(const IrcitUiNode *node_ptr, int y, int x, int *row_out, uint64_t *msg_out)
{
  IrcitUiNode              *owner_ptr = sModelOwner((IrcitUiNode *)node_ptr);
  IrcitUiConversationState *state_ptr;
  size_t                    i;

  if (row_out) *row_out = -1;
  if (!owner_ptr) return IRCITUI_TOKEN_NONE;
  state_ptr = owner_ptr->common.widget_state;
  if (!state_ptr || !state_ptr->rows_valid) return IRCITUI_TOKEN_NONE;

  // With no gutter this resolves nothing and the walk below is exactly the old one.
  if (state_ptr->avatar_gutter_cols > 0) {
    for (i = 0; i < state_ptr->count; i++) {
      const IrcitUiConversationRow *g = &state_ptr->rows[i];

      if (g->avatar_rows > 0 && y >= g->avatar_y && y < g->avatar_y + g->avatar_rows && x >= g->avatar_x &&
          x < g->avatar_x + CONV_AVATAR_COLS) {
        if (row_out) *row_out = (int)i;
        if (msg_out) *msg_out = state_ptr->msgs[i].message_id;
        return IRCITUI_TOKEN_AVATAR;
      }
      if (g->name_row > 0 && y == g->origin_y && x >= g->name_x && x < g->name_x + g->name_w) {
        if (row_out) *row_out = (int)i;
        if (msg_out) *msg_out = state_ptr->msgs[i].message_id;
        return IRCITUI_TOKEN_NAME_TAG;
      }
    }
  }

  for (i = 0; i < state_ptr->count; i++) {
    const IrcitUiConversationRow *row = &state_ptr->rows[i];
    int                           local;
    int                           at;

    if (y < row->origin_y || y >= row->origin_y + row->height) continue;

    // The box first: a cell beside the bubble is nobody's before its band is read.
    if (x < row->box_x || x >= row->box_x + row->box_w) return IRCITUI_TOKEN_NONE;

    if (row_out) *row_out = (int)i;
    if (msg_out) *msg_out = state_ptr->msgs[i].message_id;

    /* A STATUS INDICATOR'S CELLS ARE A TARGET OF THEIR OWN (R28): every cell it
     * draws resolves to STATUS -- the notice's centred span, the local indicator's
     * box -- with the id reported above, so a click reaches the application rather
     * than the widget's collapse gesture, which an indicator does not have. */
    if (state_ptr->msgs[i].kind != IRCITUI_MSG_KIND_MESSAGE) return IRCITUI_TOKEN_STATUS;

    local = y - row->origin_y;
    at    = 0;
    if (row->name_row > 0) {
      // The name tag is a target of its own: the sweep must account for every cell
      // the painter draws, and a cell the router cannot name is one it cannot check.
      if (local == at) return IRCITUI_TOKEN_NAME_TAG;
      at += row->name_row;
    }
    if (row->box_top > 0) {
      if (local == at) return IRCITUI_TOKEN_BODY;
      at += row->box_top;
    }
    if (row->reply_rows > 0) {
      if (local < at + row->reply_rows) return IRCITUI_TOKEN_REPLY;
      at += row->reply_rows;
    }
    if (local < at + row->body_lines) return IRCITUI_TOKEN_BODY;
    at += row->body_lines;
    if (row->badge_row > 0) {
      if (local == at) return IRCITUI_TOKEN_BADGE;
      at += row->badge_row;
    }
    // The stamp is inside the box, so it is the bubble's: R27's rule, as the rules are.
    if (row->stamp_row > 0) {
      if (local == at) return IRCITUI_TOKEN_BODY;
      at += row->stamp_row;
    }
    if (row->box_bottom > 0 && local == at) return IRCITUI_TOKEN_BODY;
    // In the band and in the box, but past every part: a truncated row's slack.
    return IRCITUI_TOKEN_NONE;
  }
  return IRCITUI_TOKEN_NONE;
}

int
IrcitUiConversationWindowRowAt(const IrcitUiNode *node_ptr, int y, int x)
{
  int row = -1;

  sResolveToken(node_ptr, y, x, &row, NULL);
  return row;
}

IrcitUiConversationTokenTarget
IrcitUiConversationWindowResolveTokenAt(const IrcitUiNode *node_ptr, int y, int x, uint64_t *msg_out)
{
  return sResolveToken(node_ptr, y, x, NULL, msg_out);
}

int
IrcitUiConversationWindowToggleCollapse(IrcitUiNode *node_ptr, int index)
{
  IrcitUiConversationState *state_ptr;

  node_ptr = sModelOwner(node_ptr);
  if (!node_ptr) return 0;
  state_ptr = node_ptr->common.widget_state;
  if (!state_ptr || index < 0 || (size_t)index >= state_ptr->count) return 0;
  // A STATUS INDICATOR HAS NO COLLAPSE STATE (R28): the widget has no gesture for * it, and its cells are the application's to act on.
  if (state_ptr->msgs[index].kind != IRCITUI_MSG_KIND_MESSAGE) return 0;

  state_ptr->msgs[index].is_collapsed = state_ptr->msgs[index].is_collapsed ? false : true;
  // Every row below this one moves, so the whole geometry is stale.
  state_ptr->rows_valid = false;
  IrcitUiRequestLayout(node_ptr);
  IrcitUiInvalidate(node_ptr);
  return 1;
}

/**
 * @brief Whether a tap landed in the unread banner's box -- and took it.
 *
 * THE BOX IS CONSUMED BEFORE ANYTHING UNDER IT, and it is the MODEL's rectangle that is
 * tested, the same one the painter drew, so the cell the reader sees is the cell that
 * answers. THE BOX AND NOT THE ROW: the banner is a centred pill on the viewport's last
 * row, and a bubble it sits beside is visible on the rest of that row, so a tap out there
 * is still the bubble's to take.
 *
 * THE JUMP IS THE WIDGET'S OWN MOTION, in the terms the seam already publishes: the ceiling
 * is geometry and only a model pass knows it, so this asks how far back from the newest the
 * reader now is and takes exactly that off -- no second writer of the offset, and no ceiling
 * derived here. The pass that follows retires the banner, a reader at the tail having read
 * what it was counting.
 *
 * @param node_ptr The conversation window, already resolved to its model owner.
 * @param rel_x Viewport-relative column of the tap.
 * @param rel_y Viewport-relative row of the tap.
 *
 * @return 1 when the tap was the banner's, 0 when it was not.
 */
static int
sBannerTap(IrcitUiNode *node_ptr, int rel_x, int rel_y)
{
  IrcitUiConversationState *state_ptr = node_ptr->common.widget_state;
  int                       back;

  if (!state_ptr || state_ptr->banner_w <= 0) return 0;
  if (rel_y != state_ptr->banner_y) return 0;
  if (rel_x < state_ptr->banner_x || rel_x >= state_ptr->banner_x + state_ptr->banner_w) return 0;

  back = IrcitUiConversationWindowScroll(node_ptr, 0);
  if (back > 0) (void)IrcitUiConversationWindowScroll(node_ptr, -back);
  return 1;
}

int
IrcitUiConversationWindowHandleClick(IrcitUiNode *node_ptr, int screen_y, int screen_x)
{
  int x = 0, y = 0, w = 0, h = 0;
  int row;

  node_ptr = sModelOwner(node_ptr);
  if (!node_ptr) return 0;
  // The viewport's box, because the row origins and boxes are in ITS coordinates.
  {
    IrcitUiNode *vp_ptr = sViewportOf(node_ptr);
    IrcitUiNodeGetGeometry(vp_ptr ? vp_ptr : node_ptr, &x, &y, &w, &h);
  }
  if (screen_x < x || screen_x >= x + w || screen_y < y || screen_y >= y + h) return 0;

  // The banner is an overlay: its box is consumed before the gesture below ever sees it.
  if (sBannerTap(node_ptr, screen_x - x, screen_y - y)) return 1;

  // The widget's gesture is collapse and an avatar has no collapse state: a tap on
  // one is the application's, so it is declined here and the click falls through.
  if (IrcitUiConversationWindowResolveTokenAt(node_ptr, screen_y - y, screen_x - x, NULL) == IRCITUI_TOKEN_AVATAR)
    return 0;

  row = IrcitUiConversationWindowRowAt(node_ptr, screen_y - y, screen_x - x);
  if (row < 0) return 0;
  return IrcitUiConversationWindowToggleCollapse(node_ptr, row);
}

int
IrcitUiConversationWindowRowCount(const IrcitUiNode *node_ptr)
{
  IrcitUiConversationState *state_ptr;

  node_ptr = sModelOwner((IrcitUiNode *)node_ptr);
  if (!node_ptr) return 0;
  state_ptr = node_ptr->common.widget_state;
  if (!state_ptr) return 0;
  return (int)state_ptr->count;
}

/**
 * @brief How many unread inbound arrivals the banner is counting, as of now.
 *
 * THE MODEL IS BROUGHT CURRENT FIRST, which is what makes this readable on the frame a
 * host has just notified on: a count is one of the model's outputs, and the dataset a
 * notify is about is not in it until a pass has read it. A reader that read the field
 * directly would answer with the previous frame's number, and a caller that styles the
 * banner from it would be one message behind.
 *
 * @param node_ptr The conversation window, or its viewport.
 *
 * @return The arrivals the banner is counting, being those past the reader's watermark that
 *         are not the host's own. 0 when the reader is at the tail, and 0 for a node that
 *         holds no model.
 */
int
IrcitUiConversationWindowUnreadCount(const IrcitUiNode *node_ptr)
{
  IrcitUiConversationState *state_ptr;
  IrcitUiNode              *owner_ptr;
  int                       w = 0, h = 0;

  owner_ptr = sModelOwner((IrcitUiNode *)node_ptr);
  if (!owner_ptr) return 0;
  state_ptr = sModel(owner_ptr, &w, &h);
  return state_ptr ? state_ptr->unread_in : 0;
}

int
IrcitUiConversationWindowRowInfo(const IrcitUiNode *node_ptr, int index, IrcitUiConversationRow *out_ptr)
{
  IrcitUiConversationState *state_ptr;

  if (!node_ptr || !out_ptr || index < 0) return 0;
  node_ptr = sModelOwner((IrcitUiNode *)node_ptr);
  if (!node_ptr) return 0;
  state_ptr = node_ptr->common.widget_state;
  if (!state_ptr || !state_ptr->rows_valid) return 0;
  if ((size_t)index >= state_ptr->count) return 0;
  *out_ptr = state_ptr->rows[index];
  return 1;
}

int
IrcitUiConversationWindowScroll(IrcitUiNode *node_ptr, int delta_rows)
{
  IrcitUiConversationState *state_ptr;
  int                       w = 0, h = 0;

  node_ptr = sModelOwner(node_ptr);
  if (!node_ptr) return 0;
  state_ptr = node_ptr->common.widget_state;
  if (!state_ptr) return 0;
  sModel(node_ptr, &w, &h);

  // The field counts down from the oldest, so going back into history is a SUBTRACTION.
  node_ptr->container.scroll_y -= delta_rows;
  state_ptr->rows_valid = false;
  // The second pass is the clamp: sComputeRows is the only thing that knows the * ceiling, because the ceiling is the geometry.
  sModel(node_ptr, &w, &h);
  IrcitUiInvalidate(node_ptr);
  // The offset TAKEN in the caller's terms: rows back from the newest, so 0 is the tail.
  return node_ptr->container.columns - node_ptr->container.scroll_y;
}

/* --- the painter: reads the geometry, writes the cells --------------------- */

/**
 * @brief The background a sender's bubbles are drawn on.
 *
 * PER-SENDER COLOUR, WHICH THE PLAN HAD REFUSED as "no slot to land on". That was
 * wrong about the mechanism rather than about the vocabulary: the nine slots are
 * the right way to ask for a ROLE, and a sender's identity is not a role -- but
 * IrcitUiColourPair() allocates a pair on demand (init_pair, capped), so a widget
 * can hold one background per sender without inventing a slot for each.
 *
 * The six backgrounds are the base eight, so they exist on a terminal with no
 * 256-colour support -- which is the objection the slot vocabulary exists to
 * answer, and it is answered by staying inside the base set rather than by
 * refusing the feature. The foreground is black on all six, because a coloured
 * background with a light foreground is unreadable on a light profile.
 *
 * The mapping is a hash of the name and not a table, so a sender keeps its colour
 * for the life of the process without the widget having to remember which names
 * it has seen.
 */
static short
sSenderBg(const char *name_ptr, short fallback)
{
  /* DARK SHADES, NOT SATURATED HUES, and that is the correction this function
   * needed. The first version returned the base eight -- COLOR_BLUE, COLOR_GREEN,
   * COLOR_RED -- as a background with black text on it, and on the glass that is a
   * slab of solid colour per message, which reads as an inverted block rather than
   * as a bubble. A chat tells senders apart with a slightly different neutral, not
   * with a different primary.
   *
   * The shades are from the 256-colour cube's dark end, so they exist wherever
   * there are enough colours to have a dark end at all. WHERE THERE ARE NOT, THE
   * FUNCTION RETURNS THE NODE'S OWN BACKGROUND: it invents nothing, so an
   * eight-colour terminal gets the bubble it always had rather than six primaries.
   * That is the rule -- "if there is a background colour, pick it; otherwise pick
   * up the border colour" -- applied to the case where there is not one. */
  static const short kShades[] = {235, 236, 237, 238, 239, 240};
  unsigned           h = 2166136261u;

  if (COLORS < 256) return fallback;
  if (!name_ptr || !*name_ptr) return kShades[0];
  while (*name_ptr) {
    h ^= (unsigned char)*name_ptr++;
    h *= 16777619u;
  }
  return kShades[h % (sizeof kShades / sizeof kShades[0])];
}

/** @brief One UTF-8 run at a cell, in a slot's style. A negative row draws nothing. */
static void
sPut(WINDOW *win_ptr, int y, int x, const char *str_ptr, chtype style, int maxc)
{
  int saved;

  if (y < 0 || x < 0 || !str_ptr || !*str_ptr) return;
  /* THE ATTRIBUTES ARE SAVED AND RESTORED, not turned off. wattroff() takes off the
   * bits named in `style`, and a style that carries A_DIM -- the quote's, the stamp's,
   * the avatar's dimmed phase -- therefore takes off the dim an ENCLOSING scope had
   * set. The symptom the frame dump showed: an expired bubble's stamp row drew its
   * own dim correctly and left the bottom rule beneath it at full strength. */
  saved = getattrs(win_ptr);
  wattron(win_ptr, style);
  IrcitUiAddUtf8(win_ptr, y, x, str_ptr, maxc);
  wattrset(win_ptr, saved);
}

/* The box glyphs, as UTF-8. Spelled here rather than taken from the ACS letters
 * because the ACS door reads back as its letter -- dev/probes/border records that
 * a scan for a frame finds 'l', 'm', 'q' in ordinary text -- and because a bubble
 * is a SHAPE, which the ACS set cannot express. */
#define G_TL "\xe2\x95\xad" /* ╭ */
#define G_TR "\xe2\x95\xae" /* ╮ */
#define G_BL "\xe2\x95\xb0" /* ╰ */
#define G_BR "\xe2\x95\xaf" /* ╯ */
#define G_HL "\xe2\x94\x80" /* ─ */
#define G_VL "\xe2\x94\x82" /* │ */
/* THE SQUARED SET, for a paragraph box and for a wall corner that meets a neighbour.
 *
 * The tail used to have macros of its own -- G_TAIL_R/G_TAIL_L, the bottom sender-side
 * corner squared -- and they were the same two glyphs as these. The corner rules
 * subsumed them: a run's end corner is now whatever the radii rule gives, so the tail
 * is a ╯ at the end of a pill and a ┘ at the end of a box, and there is no separate
 * tail to name.
 *
 * BOTH SETS COME FROM THE BOX-DRAWING BLOCK, which is what the design document draws
 * and not a diagonal: the first version of this painter used U+2571/U+2572, and a
 * diagonal is a glyph many terminals fall back to a filled cell for, which reads as an
 * inverted block rather than as a corner. */
#define G_SQ_TL "\xe2\x94\x8c"  /* ┌ */
#define G_SQ_TR "\xe2\x94\x90"  /* ┐ */
#define G_SQ_BL "\xe2\x94\x94"  /* └ */
#define G_SQ_BR "\xe2\x94\x98"  /* ┘ */
/* THE SHARED BORDER'S OWN GLYPHS, and the quote's divider's: a rule that a wall
 * runs through. ├ and ┤ are the only pair that keeps the wall continuous, which is
 * why a block's interior border is one row and not two abutting cornered rules. */
#define G_TEE_L "\xe2\x94\x9c" /* ├ */
#define G_TEE_R "\xe2\x94\xa4" /* ┤ */
#define G_REPLY "\xe2\x94\x8c"  /* ┌ */
#define G_DOT "\xe2\x97\x8f"    /* ● */
#define G_CHECK "\xe2\x9c\x93"  /* ✓ */
#define G_CIRCLE "\xe2\x97\x8b" /* ○ */
// The avatar's half-blocks: a ceiling, a frame and a floor inside three rows.
#define G_UP "\xe2\x96\x80"     // ▀
#define G_DOWN "\xe2\x96\x84"   // ▄
#define G_FULL "\xe2\x96\x88"   // █

/** @brief A horizontal rule of @p n cells: the glyph, then (n-2) rules, then the glyph. */
static void
sPutRule(WINDOW *win_ptr, int y, int x, int n, const char *left_ptr, const char *right_ptr, chtype style)
{
  char buf[512];
  int  i, k = 0;

  if (n < 2 || y < 0) return;
  k += snprintf(buf + k, sizeof buf - (size_t)k, "%s", left_ptr);
  for (i = 0; i < n - 2 && k < (int)sizeof buf - 4; i++) k += snprintf(buf + k, sizeof buf - (size_t)k, "%s", G_HL);
  snprintf(buf + k, sizeof buf - (size_t)k, "%s", right_ptr);
  sPut(win_ptr, y, x, buf, style, n);
}

/**
 * @brief The bubble's four corners, from its line count and its place in the run.
 *
 * TWO RULES, and the second refines the first rather than replacing it.
 *
 *   the radii   a body of ONE line is a pill -- all four corners rounded; TWO or
 *               more is a paragraph box, all four squared. Line count and not
 *               column count, because a short phrase with a forced break reads as
 *               squashed with rounded corners on every side.
 *
 *   the wall    the side the bubble is anchored to is spliced into its cluster, so a
 *               run reads as one stack against the wall. A wall corner squares WHERE
 *               IT MEETS A NEIGHBOUR: the top one when a bubble sits above (MIDDLE,
 *               BOTTOM) and the bottom one when one sits below (TOP, MIDDLE). At the
 *               run's ENDS the radii rule governs -- so a LONE one-line bubble is a
 *               whole pill and a LONE multi-line one a whole box -- and the "tail" is
 *               simply that end corner, ╯/╰ for a pill and ┘/└ for a box.
 *
 * The NON-WALL side is never touched by the second rule, so a multi-line TOP bubble
 * has a squared top corner because of its line count rather than because of its
 * position. That is the ruling's prose reading -- "the wall-side BOTTOM corner loses
 * its roundness and flattens" -- and it is the reading the frozen references pin.
 */
static void
sResolveCorners(const IrcitUiConversationRow *row_ptr, const IrcitUiConversationMessage *msg_ptr,
                const char **tl_out, const char **tr_out, const char **bl_out, const char **br_out,
                bool *shared_out)
{
  const bool local   = msg_ptr->direction == IRCITUI_MSG_OUTBOUND;
  const bool squared = row_ptr->body_lines > 1;

  // THE TOP RULE IS A BORDER THE BUBBLE SHARES, on every bubble but its block's * first: one row, with the wall running through it (R6, R12).
  *shared_out = row_ptr->cluster == IRCITUI_CLUSTER_MIDDLE || row_ptr->cluster == IRCITUI_CLUSTER_BOTTOM;

  if (row_ptr->cluster == IRCITUI_CLUSTER_LONE) {
    /* A STANDALONE BUBBLE IS NEVER SPLICED. A received one is fully rounded
     * whatever its line count (R16); a local one squares its top-right because it
     * is flush to the wall (R11), and a status indicator is the same (R15). The
     * bottom is rounded at both corners either way (R14, R15, R16). */
    *tl_out = G_TL;
    *tr_out = local ? G_SQ_TR : G_TR;
    *bl_out = G_BL;
    *br_out = G_BR;
    return;
  }
  if (row_ptr->cluster == IRCITUI_CLUSTER_TOP) {
    /* THE BLOCK'S TOP RULE. A local block's first bubble squares its top-left as
     * well (R13) and its top-right always (R11); a received block's takes the radii
     * rule of the first bubble's line count (R8, R5). */
    *tl_out = local ? G_SQ_TL : (squared ? G_SQ_TL : G_TL);
    *tr_out = local ? G_SQ_TR : (squared ? G_SQ_TR : G_TR);
    *bl_out = G_BL;
    *br_out = G_BR;
    return;
  }
  /* MIDDLE and BOTTOM share their top rule, and BOTTOM is the block's end: the
   * received end carries the tail in its bottom-left corner and takes its
   * bottom-right from the radii rule (R9), while a local end is rounded in both
   * (R14). MIDDLE has no end rule at all -- the block continues below it. */
  *tl_out = G_TL;
  *tr_out = G_TR;
  *bl_out = G_BL;
  *br_out = local ? G_BR : (squared ? G_SQ_BR : G_BR);
}


/**
 * @brief Fill a run of cells with the bubble's own background and nothing else.
 *
 * WITHOUT THIS THE BUBBLE IS NOT FILLED. sPut styles only the characters it writes,
 * so a body line shorter than the box leaves the widget's backdrop showing between
 * the last glyph and the right wall -- the box's edges are drawn and its middle is
 * not, which reads as a highlight on the text rather than as a bubble.
 *
 * The cell carries the pair as part of the chtype rather than under a wattron: the
 * same measurement the scrollbar's thumb records, that an attribute set around a
 * blank write does not reliably reach the cells.
 */
static void
sFillRow(WINDOW *win_ptr, int y, int x, int n, chtype style)
{
  int i;

  if (y < 0 || n <= 0) return;
  for (i = 0; i < n; i++) mvwaddch(win_ptr, y, x + i, (chtype)' ' | (style & (chtype)A_COLOR));
}

/**
 * @brief The bubble's two side walls for one row.
 *
 * EVERY interior row draws them, which is what makes the box read as a box. The reply
 * header and the badge row were drawn without them -- their content only -- so the
 * left and right edges broke at exactly those rows and the shape came apart.
 */
static void
sPutWalls(WINDOW *win_ptr, int y, int bx, int bw, chtype style)
{
  if (y < 0 || bw < 2) return;
  sPut(win_ptr, y, bx, G_VL, style, 1);
  sPut(win_ptr, y, bx + bw - 1, G_VL, style, 1);
}

// The sender's colour: the FNV-1a the bubble's shade already uses, over the six base
// primaries, so an avatar and its bubbles agree about which sender they belong to.
static short
sPrimaryFg(const char *name_ptr)
{
  static const short kPrimaries[] = {COLOR_RED, COLOR_GREEN, COLOR_YELLOW, COLOR_BLUE, COLOR_MAGENTA, COLOR_CYAN};
  unsigned           h            = 2166136261u;

  if (!name_ptr || !*name_ptr) return kPrimaries[0];
  while (*name_ptr) {
    h ^= (unsigned char)*name_ptr++;
    h *= 16777619u;
  }
  return kPrimaries[h % (sizeof kPrimaries / sizeof kPrimaries[0])];
}

// A sender's texture: the stipple the grid's ceiling and floor alternate with. One
// column each, and from the same hash the colour comes from but a different DIGIT of
// it, so a colour and a texture do not always travel together.
static const char *
sTexture(const char *name_ptr)
{
  static const char *kTextures[] = {"\xe2\x96\x91", "\xe2\x96\x92", "\xe2\x96\x93",
                                    "\xe2\x96\x9a", "\xe2\x96\x9e", "\xe2\x96\xaa"};
  unsigned           h = 2166136261u;

  if (!name_ptr || !*name_ptr) return kTextures[0];
  while (*name_ptr) {
    h ^= (unsigned char)*name_ptr++;
    h *= 16777619u;
  }
  return kTextures[(h / 6u) % (sizeof kTextures / sizeof kTextures[0])];
}

// The one display column a texture must own, or the medium shade, which always does.
static const char *
sOneColumn(const char *str_ptr)
{
  mbstate_t st;
  wchar_t   wc;

  memset(&st, 0, sizeof st);
  if (mbrtowc(&wc, str_ptr, strlen(str_ptr), &st) == (size_t)-1) return "\xe2\x96\x92";
  return wcwidth(wc) == 1 ? str_ptr : "\xe2\x96\x92";
}

// The avatar's one cell of ink: the host's provider when it names one, then the
// name's first display column, then a list glyph indexed by the message id.
static void
sAvatarGlyph(const IrcitUiConversationState *state_ptr, const IrcitUiConversationMessage *msg_ptr,
             char *out_ptr, size_t n)
{
  static const char *kFallback[] = {"\xe2\x9c\xb3", "\xe2\x97\x8d", "\xe2\x9d\x96",
                                    "\xe2\x96\x9a", "\xe2\x97\x88", "\xe2\x9c\xbf"};
  mbstate_t          st;
  wchar_t            wc;
  size_t             len, got;
  uint64_t           id = msg_ptr->message_id;
  unsigned           h  = 2166136261u;

  // The host's answer is re-validated on the same rule the resolution below obeys,
  // so a provider cannot put a two-column glyph in a one-column grid.
  if (state_ptr && state_ptr->avatar_provider.resolve_avatar_glyph) {
    char host_glyph[8] = {0};

    if (state_ptr->avatar_provider.resolve_avatar_glyph(msg_ptr, host_glyph, sizeof host_glyph,
                                                        state_ptr->avatar_provider.host_ctx) &&
        host_glyph[0] != '\0') {
      memset(&st, 0, sizeof st);
      got = mbrtowc(&wc, host_glyph, strlen(host_glyph), &st);
      if (got != (size_t)-1 && got != (size_t)-2 && got > 0 && wcwidth(wc) == 1 && got < n) {
        memcpy(out_ptr, host_glyph, got);
        out_ptr[got] = 0;
        return;
      }
    }
  }

  len = strlen(msg_ptr->sender_name);
  memset(&st, 0, sizeof st);
  got = mbrtowc(&wc, msg_ptr->sender_name, len, &st);
  // One display column, or the grid breaks: anything else falls to the list.
  if (len > 0 && got != (size_t)-1 && got != (size_t)-2 && got > 0 && wcwidth(wc) == 1 && got < n) {
    memcpy(out_ptr, msg_ptr->sender_name, got);
    out_ptr[got] = 0;
    return;
  }
  while (id) {
    h ^= (unsigned)(id & 0xff);
    h *= 16777619u;
    id >>= 8;
  }
  snprintf(out_ptr, n, "%s", kFallback[h % (sizeof kFallback / sizeof kFallback[0])]);
  // The list's glyphs are one column each, but a locale may say otherwise.
  memset(&st, 0, sizeof st);
  got = mbrtowc(&wc, out_ptr, strlen(out_ptr), &st);
  if (got == (size_t)-1 || got == (size_t)-2 || wcwidth(wc) != 1) snprintf(out_ptr, n, "?");
}

/**
 * @brief Draw the avatar: three half-block rows in the gutter, the initial in the middle.
 *
 * Clipped row by row like the body, never skipped whole: the ceiling row can be above
 * the fold while the floor row is on it, which is why the caller's skip allows for it.
 */
static void
sDrawAvatar(const IrcitUiConversationState *state_ptr, const IrcitUiNode *node_ptr, WINDOW *win_ptr,
            const IrcitUiConversationRow *row_ptr, const IrcitUiConversationMessage *msg_ptr, int phase, int h)
{
  char        glyph[8];
  const char *tex;
  char        cells[32];
  chtype      block, ink, pulse = 0;
  int         i, y;

  if (row_ptr->avatar_rows <= 0 || row_ptr->avatar_x < 0) return;
  sAvatarGlyph(state_ptr, msg_ptr, glyph, sizeof glyph);
  tex = sOneColumn(sTexture(msg_ptr->sender_name));
  if (row_ptr->avatar_pulse) pulse = phase ? A_DIM : A_BOLD;

  /* THE BLOCK IN THE SENDER'S PRIMARY, on all three rows, and the ceiling and floor
   * alternate the primary's half-block with this sender's own TEXTURE -- so the grid
   * is the sender's by pattern and by colour both, while the letter in the middle
   * still names them. Where the terminal has too few colours for any of that,
   * nothing is invented and reverse video carries the initial. */
  if (COLORS >= 8) {
    short primary = sPrimaryFg(msg_ptr->sender_name);
    block         = COLOR_PAIR(IrcitUiColourPair(primary, node_ptr->common.bg)) | pulse;
    ink           = COLOR_PAIR(IrcitUiColourPair(COLOR_BLACK, primary)) | pulse;
  }
  else {
    block = pulse;
    ink   = (chtype)(A_REVERSE | pulse);
  }
  for (i = 0; i < row_ptr->avatar_rows && i < CONV_AVATAR_ROWS; i++) {
    y = row_ptr->avatar_y + i;
    if (y < 0 || y >= h) continue;
    if (i == 1) {
      sPut(win_ptr, y, row_ptr->avatar_x, G_FULL G_DOWN, block, 2);
      sPut(win_ptr, y, row_ptr->avatar_x + 2, glyph, ink, 1);
      sPut(win_ptr, y, row_ptr->avatar_x + 3, G_DOWN G_FULL, block, 2);
    }
    else {
      const char *frame_glyph = i == 0 ? G_DOWN : G_UP;

      snprintf(cells, sizeof cells, "%s%s%s%s%s", frame_glyph, tex, frame_glyph, tex, frame_glyph);
      sPut(win_ptr, y, row_ptr->avatar_x, cells, block, CONV_AVATAR_COLS);
    }
  }
}

/**
 * @brief Draw one message as a bubble.
 *
 * The shape comes from the row and nothing is re-derived: the box's width from
 * body_cols, its rows from box_top/box_bottom, which lines carry the body from
 * body_lines, and its corners from the radii and wall rules. That is the reason the
 * model exists -- a painter that computed any of it again would be the third copy
 * of the arithmetic the design document keeps making.
 */
static void
sDrawBubble(const IrcitUiConversationState *state_ptr, const IrcitUiNode *node_ptr, WINDOW *win_ptr,
            const IrcitUiConversationRow *row_ptr,
            const IrcitUiConversationMessage *msg_ptr, int h, int phase)
{
  const bool   outbound = msg_ptr->direction == IRCITUI_MSG_OUTBOUND;
  /* THE BUBBLE'S FILL, AND THE BOX DRAWN WITH IT.
   *
   * A slot's style word is a COMPOSITE -- IrcitUiPaletteStyle returns the slot's
   * attributes OR'd with its pair -- and an attribute on a box-drawing character
   * is wrong: A_REVERSE turns a wall into a slab, A_UNDERLINE draws a line under a
   * line. So the shape takes the slot's COLOURS and never its attributes, which is
   * what masking with A_COLOR does.
   *
   * The three cases, and each is a rule rather than a preference:
   *
   *   outbound      the ACCENT slot's colours. Cyan on black, and the only loud
   *                 thing on the row -- that is what the role is for.
   *   inbound, 256  the node's foreground on a DARK SHADE per sender. A chat tells
   *                 senders apart with a slightly different neutral. The first
   *                 version of this used the base eight as backgrounds with black
   *                 text and it read as an inverted block per message, which is
   *                 what "still inverse" was.
   *   inbound, 8    the PANEL slot's colours: NOTHING IS INVENTED where there are
   *                 not the colours to invent it from. An eight-colour terminal
   *                 gets the bubble it always had, not six primaries. */
  const chtype panel_pair = IrcitUiPaletteStyle(IRCITUI_SLOT_PANEL) & (chtype)A_COLOR;
  const short  shade      = sSenderBg(msg_ptr->sender_name, node_ptr->common.bg);
  const chtype bubble     = outbound
                                ? (IrcitUiPaletteStyle(IRCITUI_SLOT_ACCENT) & (chtype)A_COLOR)
                                : (COLORS >= 256
                                       ? (chtype)COLOR_PAIR(IrcitUiColourPair(
                                             node_ptr->common.fg ? node_ptr->common.fg : COLOR_WHITE, shade))
                                       : panel_pair);
  const chtype meta     = bubble | A_DIM;
  char         line[1024];
  /* THE STATUS MARK'S COLUMNS ARE RESERVED, and this is the fix for a bug the
   * widget probe found by measuring rather than by reading: an outbound bubble
   * whose body fills its width had its mark drawn OVER the last two characters,
   * because the mark sat at `bw - 4` -- one column inside the body's last cell.
   * "second body" read back as "second bo" plus two checks.
   *
   * The bubble widens by the mark's three cells (a gap and two glyphs) instead.
   * The HEIGHT is unchanged, so the model does not move: the mark lives in the
   * bubble's right padding, which is where the design document puts it -- "inline
   * context status indicators" on the last line, not on the wall. */
  const bool   mark_p  = outbound && msg_ptr->status != IRCITUI_STATUS_EXPIRED;
  /* FROM THE MODEL, not recomputed. The row already carries the box -- the hit test
   * reads the same two fields -- and a second copy here is exactly the defect the model
   * exists to prevent. The viewport's WIDTH is no longer a parameter: it is an input
   * the model consumed when it computed this box, so the painter has nothing left to do
   * with it. The timestamps margin still is one -- the box moved aside for it, and the
   * margin's own text is drawn at the wall. */
  const int    bw = row_ptr->box_w;
  const int    bx = row_ptr->box_x;
  // The four corners and whether the top rule is a shared border, once.
  const char  *c_tl, *c_tr, *c_bl, *c_br;
  bool         shared = false;
  int          y, i, seg;

  sResolveCorners(row_ptr, msg_ptr, &c_tl, &c_tr, &c_bl, &c_br, &shared);


  y = row_ptr->origin_y;
  if (msg_ptr->status == IRCITUI_STATUS_EXPIRED) wattron(win_ptr, A_DIM);

  // The avatar goes down before the frame, so where the two could claim one cell
  // the box wins and the router's account of it still matches the ink.
  sDrawAvatar(state_ptr, node_ptr, win_ptr, row_ptr, msg_ptr, phase, h);

  /* The name tag: the block's first received bubble carries it. THE BUBBLE'S TEXT
   * COLOUR ON THE WIDGET'S OWN BACKGROUND. The foreground is the one the sender's
   * bubbles are drawn in, so the tag is visibly the sender's; the background is
   * NOT the sender's shade -- the tag's row has no bubble around it, and a shade
   * behind five cells would paint a band the rest of the row does not have. The
   * widget's own fill pair cannot be used for the text: its foreground equals its
   * background, which is all a fill needs, so a glyph drawn in it disappears. */
  if (row_ptr->name_row > 0 && y >= 0) {
    const chtype tag_style = COLOR_PAIR(
        IrcitUiColourPair(node_ptr->common.fg ? node_ptr->common.fg : COLOR_WHITE, node_ptr->common.bg));
    snprintf(line, sizeof line, "@%s", msg_ptr->sender_name);
    // The model's extent, not a second derivation: it starts in the gutter when
    // there is one, and stops at the box's own edge.
    sPut(win_ptr, y, row_ptr->name_x, line, tag_style, row_ptr->name_w);
  }
  y += row_ptr->name_row;

  /* The block's top rule, or the border this bubble shares with the one above it:
   * one row either way, and the T-junction is what keeps the wall continuous
   * through the shared one (R6, R12). */
  if (row_ptr->box_top > 0) {
    if (shared) sPutRule(win_ptr, y, bx, bw, G_TEE_L, G_TEE_R, bubble);
    else sPutRule(win_ptr, y, bx, bw, c_tl, c_tr, bubble);
    y++;
  }

  // The reply header, drawn INSIDE the bubble: the quoted original dimmed, and the divider that closes it at the bubble's own strength, as the walls are. Only the quoted TEXT carries the dim: the rule under it is part of the surface.
  if (row_ptr->reply_rows > 0) {
    snprintf(line, sizeof line, "%s %s: %s", G_REPLY, msg_ptr->reply_sender,
             msg_ptr->reply_snippet ? msg_ptr->reply_snippet : "");
    sFillRow(win_ptr, y, bx, bw, bubble);
    sPutWalls(win_ptr, y, bx, bw, bubble);
    sPut(win_ptr, y, bx + 1, line, meta, bw - 2);
    y++;
    sPutRule(win_ptr, y, bx, bw, G_TEE_L, G_TEE_R, bubble);
    y++;
  }

  // The body: every line walled, and the status mark right-aligned on the last.
  {
    const char *body = msg_ptr->text_content ? msg_ptr->text_content : "";
    int         written = 0;

    for (i = 0; i < row_ptr->body_lines; i++) {
      /* THE CHUNK IS MEASURED IN COLUMNS (R23): a byte slice at the ceiling can cut
       * a glyph in half, so the prefix is the longest one that fits in the columns
       * -- three fewer of them when the ellipsis will take the rest (R26). */
      int chunk = sChunkBytes(body + written, row_ptr->truncated && i == row_ptr->body_lines - 1
                                               ? row_ptr->max_width - 3
                                               : row_ptr->max_width);
      if (chunk < 0) chunk = 0;
      if (y + i >= 0 && y + i < h) {
        const char *mark = NULL;
        // mark_p is the model's own decision, not a second test of `outbound`.
        if (mark_p && i == row_ptr->body_lines - 1) {
          mark = msg_ptr->status == IRCITUI_STATUS_SENDING     ? G_CIRCLE
                 : msg_ptr->status == IRCITUI_STATUS_DELIVERED ? G_CHECK
                 : msg_ptr->status == IRCITUI_STATUS_READ      ? G_CHECK
                                                              : NULL;
        }
        // The wall, the body, the wall. A truncated body ends in an ellipsis, and * the model is what said so.
        if (row_ptr->truncated && i == row_ptr->body_lines - 1) {
          snprintf(line, sizeof line, "%s %.*s...", G_VL, chunk, body + written);
        }
        else {
          snprintf(line, sizeof line, "%s %.*s", G_VL, chunk, body + written);
        }
        sFillRow(win_ptr, y + i, bx, bw, bubble);
        sPut(win_ptr, y + i, bx, line, bubble, bw - 1);
        sPut(win_ptr, y + i, bx + bw - 1, G_VL, bubble, 1);
        if (mark) {
          int mx = bx + bw - 4;
          sPut(win_ptr, y + i, mx, mark, meta, 2);
          if (msg_ptr->status == IRCITUI_STATUS_READ) sPut(win_ptr, y + i, mx + 1, G_CHECK, meta, 1);
        }
      }
      written += chunk;
    }
    y += row_ptr->body_lines;
  }

  /* The badges, inside the bubble at the sender's edge -- "fused" in the design
   * document's language, and inside rather than overhanging because a badge that
   * straddles the wall is a cell two things claim. */
  if (row_ptr->badge_row > 0) {
    int k = 0;      /* bytes, for the buffer */
    int kcols = 0;  /* columns, for the placement (R23) */
    seg = outbound ? bx + bw - 2 : bx + 2;
    for (i = 0; i < msg_ptr->reaction_count && i < 3; i++) {
      int n = snprintf(line + k, sizeof line - (size_t)k, "%s%d ", msg_ptr->reactions[i].emoji,
                       msg_ptr->reactions[i].count);
      if (n < 0) break;
      k += n;
    }
    kcols = sColumns(line);
    sFillRow(win_ptr, y, bx, bw, bubble);
    sPutWalls(win_ptr, y, bx, bw, bubble);
    if (seg > 0) sPut(win_ptr, y, seg - (outbound ? kcols : 0), line, meta, k + 2);
    y++;
  }

  // The stamp closes the bubble: a row of its own, below the badges, at the column
  // the body's text starts at and in the quote's own dimmed style.
  if (row_ptr->stamp_row > 0) {
    sFillRow(win_ptr, y, bx, bw, bubble);
    sPutWalls(win_ptr, y, bx, bw, bubble);
    sPut(win_ptr, y, bx + 2, msg_ptr->timestamp, meta, bw - 4);
    y++;
  }

  if (row_ptr->box_bottom > 0) {
    sPutRule(win_ptr, y, bx, bw, c_bl, c_br, bubble);
    y++;
  }

  if (msg_ptr->status == IRCITUI_STATUS_EXPIRED) wattroff(win_ptr, A_DIM);
}

/** @brief The banner's ink: the role the application asked for, its colours, and the
 *         application's own attribute mask.
 *
 * THE ROLE SUPPLIES THE COLOURS AND THE APPLICATION SUPPLIES THE ATTRIBUTES. The slot's
 * own attributes are masked off for the reason sDrawBubble masks them -- a slot's word is
 * a composite, and one of its attributes on a box-drawing cell is a decision the
 * application did not make. IRCITUI_SLOT_ACCENT is the default role, and -1 is "not given"
 * rather than 0 because SURFACE is a slot an application may legitimately ask for. */
static chtype
sBannerStyle(const IrcitUiConversationState *state_ptr)
{
  // The field holds the slot PLUS ONE: zero is what every unseeded node carries, so zero is "not given" and SURFACE stays askable. It lives in the state now that the node's member is gone (§7); a setter writes it there and a clone inherits it.
  const int          asked = state_ptr->banner_slot;
  IrcitUiPaletteSlot slot  = asked > 0 ? (IrcitUiPaletteSlot)(asked - 1) : IRCITUI_SLOT_ACCENT;

  return (IrcitUiPaletteStyle(slot) & (chtype)A_COLOR) | state_ptr->banner_attrs;
}

/**
 * @brief The banner's fill: @p n cells of @p style, written cell by cell.
 *
 * THE ONE PLACE THIS WIDGET'S TWO FILLS DIFFER, and it is deliberate. sFillRow takes the
 * pair and DROPS the attributes, which is right for a bubble: its attributes arrive from an
 * enclosing scope, so a filled cell must not disagree with the text drawn between them.
 * Here the application's mask IS the ink -- a reversed banner is a bar, and a bar whose
 * background did not reverse would be the defect the caller asked to avoid.
 */
static void
sFillBannerRow(WINDOW *win_ptr, int y, int x, int n, chtype style)
{
  int i;

  if (y < 0 || n <= 0) return;
  for (i = 0; i < n; i++) mvwaddch(win_ptr, y, x + i, (chtype)' ' | (style & (chtype)~A_CHARTEXT));
}

/* THE BANNER, and it is chrome about the READER rather than content: it says how much
 * arrived since they last looked, so it is pinned to the viewport's last row. The typing
 * row is placed in the content's coordinates for the opposite reason -- it is something
 * said in the conversation, and it scrolls away with it.
 *
 * The box is the model's, read here and by the hit test, so the rectangle a click tests is
 * the one the glass shows. The ink is the application's, from sBannerStyle, and the default
 * is PLAIN: the widget imposes no emphasis of its own. The corners take the same word as
 * the label, so they carve out of the bar rather than sitting on it in a second style. */
static void
sDrawBanner(const IrcitUiConversationState *state_ptr, WINDOW *win_ptr, int h)
{
  char   label[CONV_BANNER_LABEL_MAX];
  chtype style = sBannerStyle(state_ptr);
  int    x     = state_ptr->banner_x;
  int    y     = state_ptr->banner_y;
  int    w     = state_ptr->banner_w;

  if (w < 2 * CONV_BANNER_PAD || y < 0 || y >= h) return;
  sBannerLabel(label, sizeof label, state_ptr->unread_in);
  // The fill first: the banner sits over a bubble's cells, and a fill that stopped short
  // would leave that bubble's ink between the label and the corners.
  sFillBannerRow(win_ptr, y, x, w, style);
  sPut(win_ptr, y, x + CONV_BANNER_PAD, label, style, w - 2 * CONV_BANNER_PAD);
  sPut(win_ptr, y, x, G_SQ_BL, style, 1);
  sPut(win_ptr, y, x + w - 1, G_SQ_BR, style, 1);
}

void
IrcitUiConversationWindowPaint(IrcitUiNode *node_ptr)
{
  IrcitUiConversationState *state_ptr;
  IrcitUiNode              *owner_node = NULL;
  WINDOW                   *win_ptr;
  int                       w = 0, h = 0;
  size_t                    i;
  // At function scope, not in the loop: the typing row is drawn after the loop * and uses the same buffer.
  char                      line[512];

  if (!node_ptr) return;

  // One painter, two routes: the engine dispatches the viewport kind here and an
  // application may pass the container. Palette identity stays the container's.
  {
    IrcitUiNode *owner_ptr = sModelOwner(node_ptr);
    IrcitUiNode *vp_ptr    = sViewportOf(owner_ptr);

    if (vp_ptr) node_ptr = vp_ptr;
    owner_node = owner_ptr;
  }
  win_ptr = node_ptr->common.win;
  if (!win_ptr) return;
  state_ptr = sModel(node_ptr, &w, &h);
  if (!state_ptr) return;

  if (state_ptr->count == 0) {
    IrcitUiAddUtf8(win_ptr, 0, 0, "(no messages)", w);
    return;
  }

  for (i = 0; i < state_ptr->count; i++) {
    const IrcitUiConversationRow     *row = &state_ptr->rows[i];
    const IrcitUiConversationMessage *msg = &state_ptr->msgs[i];

    /* Clipped rows cost nothing: a bubble wholly above or below the box is not
     * drawn, and one that straddles the edge draws only its visible lines. An
     * avatar's ink may sit one row past the row that owns it, so a row whose box is
     * above the fold can still own a drawn row. */
    if (row->origin_y + row->height <= 0 && (row->avatar_rows <= 0 || row->avatar_y + row->avatar_rows <= 0))
      continue;
    if (row->origin_y >= h) continue;

    if (msg->direction == IRCITUI_MSG_SYSTEM) {
      // The column is the model's: one centring, read here and by the hit test.
      snprintf(line, sizeof line, "[ %s ]", msg->text_content ? msg->text_content : "");
      if (row->origin_y >= 0) {
        sPut(win_ptr, row->origin_y, row->box_x, line, IrcitUiPaletteStyle(IRCITUI_SLOT_TEXT_MUTED) | A_DIM, w);
      }
      continue;
    }
    sDrawBubble(state_ptr, owner_node ? owner_node : node_ptr, win_ptr, row, msg, h, state_ptr->anim_phase);
  }

  if (state_ptr->is_friend_typing && state_ptr->count) {
    /* BELOW THE LAST MESSAGE, in the content's own coordinates -- not pinned to the
     * viewport's last row. Pinned, it was drawn over whatever occupied that row:
     * measured, "|Charlie is typing... the G10 fix path solves it. L |" with a
     * bubble's body under the indicator. As content it sits after the newest
     * message when the view is at the bottom, and scrolls out of sight when the
     * reader is up in history, which is what a chat does. */
    const IrcitUiConversationRow *last = &state_ptr->rows[state_ptr->count - 1];
    int                           ty   = last->origin_y + last->height;

    if (ty >= 0 && ty < h) {
      snprintf(line, sizeof line, "%s is typing...", state_ptr->typing_sender_name);
      sPut(win_ptr, ty, 1, line, IrcitUiPaletteStyle(IRCITUI_SLOT_TEXT_MUTED) | A_DIM, w);
    }
  }

  /* LAST, so it wins the cells it covers rather than being drawn under the next
   * message's bubble: it is an overlay on the viewport, and this is the one place the
   * z-order of the two is decided. */
  sDrawBanner(state_ptr, win_ptr, h);
}

/* --- the composite: the viewport child ------------------------------------- */

IrcitUiNode *
IrcitUiConversationViewportEnsure(IrcitUiNode *owner_ptr)
{
  IrcitUiWidgetDef def = {0};
  IrcitUiNode     *vp_ptr;

  if (!owner_ptr || owner_ptr->common.kind != IRCITUI_CONVERSATION_WINDOW) return NULL;
  vp_ptr = sViewportOf(owner_ptr);
  if (vp_ptr) return vp_ptr;

  // The id is a file-scope literal (borrowed, and deliberately NOT in the generated
  // id index: this node exists only on a screen clone). MATCH on both axes.
  def.id          = "conversation_viewport";
  def.kind        = IRCITUI_MESSAGE_VIEWPORT;
  def.width_mode  = IRCITUI_SIZE_MATCH;
  def.height_mode = IRCITUI_SIZE_MATCH;
  def.flags       = IRCITUI_F_VISIBLE | IRCITUI_F_ENABLED;
  return IrcitUiAddChild(owner_ptr, &def);
}

void
IrcitUiConversationMinSize(const IrcitUiNode *node_ptr, int *w_out, int *h_out)
{
  // The node's own configuration, not the state's: the state's copy is only
  // resolved once the model has run, and a measurement can be asked for first.
  int cap      = node_ptr && IrcitUiDeclBubbleCollapseLines(node_ptr) > 0 ? IrcitUiDeclBubbleCollapseLines(node_ptr)
                                                                       : CONV_DEFAULT_COLLAPSE_LINES;
  int w        = CONV_MIN_BODY_COLS + 4; // the bubble's two walls and its padding
  bool bordered = node_ptr && (node_ptr->common.flags & IRCITUI_F_BORDER);
  // A declared gutter is part of the minimum: the widget asks for the room the
  // avatars need rather than being handed a box too narrow to hold one.
  int gutter   = node_ptr && IrcitUiDeclBubbleAvatarGutterCols(node_ptr) > 0
                   ? IrcitUiDeclBubbleAvatarGutterCols(node_ptr) : 0;

  if (gutter > CONV_AVATAR_MAX_COLS) gutter = CONV_AVATAR_MAX_COLS;
  w += gutter;
  // Borderless, the bar takes a column of the content: sLayoutConversation reserves it.
  if (!bordered) w += 1;
  if (w_out) *w_out = w;
  // One bubble: a top rule, up to the ceiling, its name tag and its badge row. The
  // stamp row is transient, as the typing row is, so the floor stays the steady one.
  if (h_out) *h_out = cap + 2 + 1 + 1;
}

void
IrcitUiConversationViewportPrime(IrcitUiNode *owner_ptr, int vp_w, int vp_h)
{
  if (!owner_ptr || owner_ptr->common.kind != IRCITUI_CONVERSATION_WINDOW) return;
  // One call does the work: sComputeRows publishes scroll_y and columns as it goes.
  (void)sModelAt(owner_ptr, vp_w, vp_h);
}
