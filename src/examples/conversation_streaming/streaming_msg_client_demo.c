/**
 * @file streaming_msg_client_demo.c
 * @brief The conversation window driven by a live TCP stream instead of a seed.
 *
 * Copyright (C) 2015-2026  unfacd works
 *
 * This file is part of ircituilib source code.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ---
 *
 * A COPY OF examples/interactive_msg_demo.c, and its own target so the two may
 * diverge: that one is the fixture and typing reference, this one grows the wire.
 * The two hooks, the adapter, the loop and the layout are unchanged -- what is
 * added is a transport, and it arrives through the framework's own network seam.
 *
 * THE POINT OF THIS FILE IS THE TWO HOOKS, wired on one node, each doing the job it
 * exists for:
 *
 *   on_key_down        the INTERCEPT. It runs before the buffer mutates, and a
 *                      non-zero return swallows the key. That is what keeps the
 *                      Enter that sends a message from also inserting a raw '\n'
 *                      into the field it was sent from.
 *   on_text_changed    the SYNC. It runs after a mutation has been accepted, and
 *                      is handed the finished text. That is what lets a payload be
 *                      acted on without polling the buffer.
 *
 * TYPE "test" AND A BUBBLE APPEARS WITHOUT A KEYPRESS -- that is the sync hook, and
 * it is the interesting one, because the hook only fires on the loop's fast lane.
 * An application that drives IrcitUiDispatchKey itself (as
 * examples/conversation/conversation_demo.c does) never reaches stage 3, so this
 * demo runs the framework's own loop and gets it. See the API guide's section 10.3
 * for which route carries which key.
 *
 * IT IS A GENUINE API CONSUMER: it includes ircituilib/ headers and nothing from
 * src/include, so it compiles against the public surface alone. That is why the
 * key arrives through IrcitUiEventGetKey rather than ev->key_code -- IrcitUiEvent
 * is opaque out here, and the accessor is the whole of the contract.
 *
 * THE LAYOUT IS A FILE, NOT A TREE BUILT HERE. IrcitUiDataBind hands over the model
 * generated from layout/ircitui.yaml, and this demo names three ids from it. That
 * is also why the ids below are cmdline and chat_window rather than names invented
 * for the demo: the generator's id index is global, so a second node with an id the
 * layout already uses is a generation-time refusal rather than a silent override.
 *
 * THE HOST OWNS THE DATASET. There is one store here -- s_log -- and the widget is
 * pointed at it with an IrcitUiConversationAdapter rather than handed messages one at
 * a time. A send writes the log and notifies; everything the widget shows is a copy
 * it took at the last reconcile. The binding is made on the MODEL, which is the same
 * rule the two hooks follow and for the same reason: the loop's first screen reset
 * builds a clone, and a clone inherits the binding rather than a node pointer.
 */

#include <locale.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

#include <uflib/mjson/mjson.h>

#include "conversation_stream_protocol.h"

#include "ircituilib/ui.h"
#include "ircituilib/ircitui_conversation.h"
#include "ircituilib/main_looper.h"
#include "ircituilib/window_driver/ircitui_window_registry.h"

/* The three ids this demo knows, all from layout/ircitui.yaml. */
#define CHAT_INPUT_ID  "cmdline"
#define CHAT_WINDOW_ID "chat_window"
#define STATUS_ID      "status"

/* Typing this word raises a bubble on its own, through the sync hook. */
#define TRIGGER_WORD "test"
/* The unread count at which the banner changes colour, and the reason the demo calls
 * IrcitUiConversationWindowUnreadCount rather than keeping a tally: see sApplyBannerStyle. */
#define UNREAD_ALERT_AT 10

/* The host's own identity for each message. The widget copies what it is handed
   and never invents an id, so a host that wants stable ones keeps the counter --
   and it must be STABLE, because the widget keys its reconcile on it and a host
   that renumbers per frame loses the user's collapse toggles on every refresh.

   LOCAL IDS LIVE ABOVE THE WIRE'S. The server owns 1001..1010 for its replay and
   counts upward from 1011, and the widget reconciles ON message_id -- so a local
   message that landed on a wire id would be merged with it and one of the two
   silently lost. A base neither counter reaches in a session makes the two spaces
   disjoint by construction rather than by luck. */
#define CLIENT_LOCAL_ID_BASE 0x100000000ULL

static unsigned long s_msg_seq = 1;
static unsigned long s_sent;

/* --timestamps, and it has to be re-applied rather than set once. See sApplyTimestamps. */
static int s_want_timestamps;

/* --- the host's dataset -----------------------------------------------------
 *
 * THE HOST OWNS THIS. The widget reads it through the two callbacks below, once
 * per reconcile, and copies what it keeps -- so everything here may be overwritten
 * the moment a reconcile returns, which is what a network reader's reused buffer
 * needs.
 *
 * IT GROWS, because a stream has no bound. It was a fixed 64-slot static array
 * while the only sources were a seed and a fixture, which is honest for both -- but
 * at the server's default cadence 64 slots is about 26 seconds of wire, after which
 * every arriving message was dropped. Doubling from a small base keeps the seed and
 * the fixture at one allocation apiece. */
#define APP_LOG_INITIAL_CAP 64

/* How many messages the fixture reader will look for. A bound rather than an
 * allocation: the loop stops at the first index that has no message_id, so this is
 * only here so a malformed file cannot walk forever. */
#define FIXTURE_MESSAGES_MAX 1024

/* The golden input, the same file ircitui_conversation_demo plays. A RELATIVE path,
 * as it is there: both demos are run from the repository root. */
#define DEFAULT_FIXTURE "assets/conversation/conversation_golden_test.json"

/* ONE RECORD PER MESSAGE, AND IT CARRIES EVERY FIELD THE WIDGET DRAWS.
 *
 * This is the layout-primitive boundary written out. Nothing here is domain logic:
 * no network routing, no storage key, no read-receipt list. But a reply header, a
 * badge row and a collapse ceiling are all things the widget measures, so a record
 * that dropped them could not carry the golden fixture -- which is why the fields
 * below are here rather than being trimmed to the three the seed uses. */
typedef struct {
  uint64_t                 id;
  char                     text[512];
  char                     sender[IRCITUI_SENDER_NAME_MAX];
  char                     stamp[IRCITUI_TIMESTAMP_MAX];
  IrcitUiMessageDirection  direction;
  IrcitUiMessageStatus     status;
  bool                     is_status_indicator;
  bool                     is_collapsed;
  bool                     has_reply_context;
  uint64_t                 reply_id;
  char                     reply_sender[IRCITUI_SENDER_NAME_MAX];
  char                     reply_snippet[300];
  IrcitUiMessageReaction   reactions[IRCITUI_REACTION_BADGES_MAX];
  int                      reaction_count;
} AppLogRecord;

static AppLogRecord *s_log;
static size_t        s_log_count;
static size_t        s_log_cap;

/* The fixture's transient presence, if --ingest read one. Applied after the binding,
 * because SetTyping writes a node and the record reading happens before it. */
static char s_typing_name[IRCITUI_SENDER_NAME_MAX];
static bool s_typing_pending;

/* The next free slot, growing the log if it has none. NULL only when an allocation
 * fails, which is the one thing a host cannot paper over -- and the caller must treat
 * that as a dropped message rather than as a silent no-op. */
static AppLogRecord *
sLogEnsure(void)
{
  AppLogRecord *grown;
  size_t        cap;

  if (s_log_count < s_log_cap) return &s_log[s_log_count];

  cap   = s_log_cap ? s_log_cap * 2 : APP_LOG_INITIAL_CAP;
  grown = realloc(s_log, cap * sizeof *grown);
  if (!grown) return NULL;
  s_log     = grown;
  s_log_cap = cap;
  return &s_log[s_log_count];
}

/* Add one record, stamping it now. NULL when the log cannot grow. */
static AppLogRecord *
sLogAdd(IrcitUiMessageDirection direction, IrcitUiMessageStatus status, int is_status_indicator,
        const char *sender_ptr, const char *text_ptr)
{
  AppLogRecord *rec;
  time_t        now;
  struct tm     tm_buf;

  if (!text_ptr) return NULL;
  rec = sLogEnsure();
  if (!rec) return NULL;
  memset(rec, 0, sizeof *rec);
  rec->id                  = CLIENT_LOCAL_ID_BASE + s_msg_seq++;
  rec->direction           = direction;
  rec->status              = status;
  rec->is_status_indicator = is_status_indicator;
  snprintf(rec->text, sizeof rec->text, "%s", text_ptr);
  snprintf(rec->sender, sizeof rec->sender, "%s", sender_ptr ? sender_ptr : "");

  now = time(NULL);
  if (localtime_r(&now, &tm_buf)) strftime(rec->stamp, sizeof rec->stamp, "%H:%M", &tm_buf);
  else snprintf(rec->stamp, sizeof rec->stamp, "%s", "--:--");

  s_log_count++;
  return rec;
}

/* Three messages before the loop starts, so the first frame already shows an
 * ingest, and a status indicator so the kind branch has a case. */
static void
sSeedHistory(void)
{
  sLogAdd(IRCITUI_MSG_SYSTEM, IRCITUI_STATUS_DELIVERED, 1, "",
          "Channel ready -- the host dataset is bound.");
  sLogAdd(IRCITUI_MSG_INBOUND, IRCITUI_STATUS_DELIVERED, 0, "Alice",
          "Hello -- has the character-aware measure landed?");
  sLogAdd(IRCITUI_MSG_OUTBOUND, IRCITUI_STATUS_SENDING, 0, "Me",
          "Yes. The paint pass reads geometry; it does not compute it.");
}

/* --- --ingest: the golden input, read into the host's own store -------------- */

/* The fixture's own spellings, as ircitui_conversation_demo maps them. */
static IrcitUiMessageDirection
sDirection(const char *str_ptr)
{
  if (!str_ptr) return IRCITUI_MSG_INBOUND;
  if (strcmp(str_ptr, "outbound") == 0) return IRCITUI_MSG_OUTBOUND;
  if (strcmp(str_ptr, "system") == 0) return IRCITUI_MSG_SYSTEM;
  return IRCITUI_MSG_INBOUND;
}

static IrcitUiMessageStatus
sStatus(const char *str_ptr)
{
  if (!str_ptr) return IRCITUI_STATUS_DELIVERED;
  if (strcmp(str_ptr, "sending") == 0) return IRCITUI_STATUS_SENDING;
  if (strcmp(str_ptr, "read") == 0) return IRCITUI_STATUS_READ;
  if (strcmp(str_ptr, "expired") == 0) return IRCITUI_STATUS_EXPIRED;
  return IRCITUI_STATUS_DELIVERED;
}

static void
sGetString(const char *buf, int len, const char *path_ptr, char *to_ptr, int n, const char *fallback_ptr)
{
  if (mjson_get_string(buf, len, path_ptr, to_ptr, n) <= 0) snprintf(to_ptr, (size_t)n, "%s", fallback_ptr);
}

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

/* Read the fixture into s_log. Returns how many records it added, or -1.
 *
 * THE SAME FIELD LIST AS ircitui_conversation_demo's reader, and that is the claim:
 * the fixture reaches the glass through the ADAPTER here, where that demo pushes it
 * with IrcitUiConversationWindowAppend. Nothing about the reading changes; only the
 * direction of the data does, and that is the whole of the seam. */
static int
sIngestFixture(const char *path_ptr)
{
  int   len = 0;
  char *buf = sSlurp(path_ptr, &len);
  char  path[192];
  char  tmp[64];
  int   i, added = 0;

  if (!buf) return -1;

  for (i = 0; i < FIXTURE_MESSAGES_MAX; i++) {
    AppLogRecord *rec;
    double        id = 0, num = 0;
    int           flag = 0, r;

    snprintf(path, sizeof path, "$.conversation_flow[%d].message_id", i);
    if (mjson_get_number(buf, len, path, &id) <= 0) break;

    rec = sLogEnsure();
    if (!rec) break;
    memset(rec, 0, sizeof *rec);
    rec->id = (uint64_t)id;

    snprintf(path, sizeof path, "$.conversation_flow[%d].sender_name", i);
    sGetString(buf, len, path, rec->sender, (int)sizeof rec->sender, "?");
    snprintf(path, sizeof path, "$.conversation_flow[%d].timestamp", i);
    sGetString(buf, len, path, rec->stamp, (int)sizeof rec->stamp, "--:--");
    snprintf(path, sizeof path, "$.conversation_flow[%d].text_content", i);
    sGetString(buf, len, path, rec->text, (int)sizeof rec->text, "");

    snprintf(path, sizeof path, "$.conversation_flow[%d].direction", i);
    sGetString(buf, len, path, tmp, (int)sizeof tmp, "inbound");
    rec->direction = sDirection(tmp);
    snprintf(path, sizeof path, "$.conversation_flow[%d].status", i);
    sGetString(buf, len, path, tmp, (int)sizeof tmp, "delivered");
    rec->status = sStatus(tmp);

    /* A system message is a status indicator by its direction; the one local-side
     * indicator is named by the bubble shape the fixture asks for (R28/R3). */
    rec->is_status_indicator = rec->direction == IRCITUI_MSG_SYSTEM;
    {
      char kindbuf[64];

      snprintf(path, sizeof path, "$.conversation_flow[%d].interactivity.force_border_override", i);
      sGetString(buf, len, path, kindbuf, (int)sizeof kindbuf, "none");
      if (strcmp(kindbuf, "short_pill_asymmetric_right") == 0) rec->is_status_indicator = true;
    }

    snprintf(path, sizeof path, "$.conversation_flow[%d].has_reply_context", i);
    if (mjson_get_bool(buf, len, path, &flag) > 0 && flag) {
      rec->has_reply_context = true;
      snprintf(path, sizeof path, "$.conversation_flow[%d].reply_context.reply_msg_id", i);
      if (mjson_get_number(buf, len, path, &num) > 0) rec->reply_id = (uint64_t)num;
      snprintf(path, sizeof path, "$.conversation_flow[%d].reply_context.reply_sender", i);
      sGetString(buf, len, path, rec->reply_sender, (int)sizeof rec->reply_sender, "?");
      snprintf(path, sizeof path, "$.conversation_flow[%d].reply_context.reply_snippet", i);
      sGetString(buf, len, path, rec->reply_snippet, (int)sizeof rec->reply_snippet, "");
    }

    snprintf(path, sizeof path, "$.conversation_flow[%d].interactivity.is_collapsed", i);
    if (mjson_get_bool(buf, len, path, &flag) > 0 && flag) rec->is_collapsed = true;

    for (r = 0; r < IRCITUI_REACTION_BADGES_MAX; r++) {
      double cnt = 0;

      snprintf(path, sizeof path, "$.conversation_flow[%d].reactions[%d].emoji", i, r);
      if (mjson_get_string(buf, len, path, rec->reactions[r].emoji, (int)sizeof rec->reactions[r].emoji) <= 0) break;
      snprintf(path, sizeof path, "$.conversation_flow[%d].reactions[%d].count", i, r);
      if (mjson_get_number(buf, len, path, &cnt) > 0) rec->reactions[r].count = (int)cnt;
      rec->reaction_count = r + 1;
    }

    /* The fixture's transient state, read for the same reason that demo reads it. */
    snprintf(path, sizeof path, "$.transient_presence_state.is_friend_typing");
    if (mjson_get_bool(buf, len, path, &flag) > 0 && flag) {
      snprintf(path, sizeof path, "$.transient_presence_state.typing_sender_name");
      sGetString(buf, len, path, tmp, (int)sizeof tmp, "");
      snprintf(s_typing_name, sizeof s_typing_name, "%s", tmp);
      s_typing_pending = true;
    }

    s_log_count++;
    added++;
  }

  free(buf);
  return added;
}

/* --- the application's half of the adapter contract ------------------------- */

static size_t
sAppGetItemCount(IrcitUiNode *node_ptr, void *host_ctx)
{
  (void)node_ptr;
  (void)host_ctx;
  return s_log_count;
}

/* The framework copies text_content on return; sender_name and timestamp are
 * inline arrays in IrcitUiConversationMessage and so copy by value. */
static int
sAppGetItemInfo(IrcitUiNode *node_ptr, void *host_ctx, size_t index, IrcitUiConversationMessage *out_ptr)
{
  const AppLogRecord *rec;

  (void)node_ptr;
  (void)host_ctx;
  if (index >= s_log_count) return 0;
  rec = &s_log[index];

  out_ptr->message_id = rec->id;
  out_ptr->kind       = rec->is_status_indicator ? IRCITUI_MSG_KIND_STATUS_INDICATOR : IRCITUI_MSG_KIND_MESSAGE;
  out_ptr->direction  = rec->direction;
  out_ptr->status     = rec->status;
  out_ptr->text_content = (char *)rec->text;
  snprintf(out_ptr->sender_name, sizeof out_ptr->sender_name, "%s", rec->sender);
  memcpy(out_ptr->timestamp, rec->stamp, sizeof out_ptr->timestamp);

  /* The guardrail fields. Zeroed, they draw exactly what a text-only host draws --
   * which is the point: a host that has none of this leaves them alone. */
  out_ptr->is_collapsed      = rec->is_collapsed;
  out_ptr->has_reply_context = rec->has_reply_context;
  out_ptr->reply_msg_id      = rec->reply_id;
  if (rec->has_reply_context) {
    out_ptr->reply_snippet = (char *)rec->reply_snippet;
    snprintf(out_ptr->reply_sender, sizeof out_ptr->reply_sender, "%s", rec->reply_sender);
  }
  out_ptr->reaction_count = rec->reaction_count;
  if (rec->reaction_count > 0)
    memcpy(out_ptr->reactions, rec->reactions, sizeof out_ptr->reactions);
  return 1;
}

static const IrcitUiConversationAdapter s_app_adapter = {
  .get_item_count = sAppGetItemCount,
  .get_item_info  = sAppGetItemInfo
};

/* --- --provider: the host's own avatar ink -----------------------------------------
 *
 * THE CONTRACT IS ONE DISPLAY COLUMN, and the last entry below is deliberately TWO:
 * a real emoji, so the fallback is visible on the glass and not only in the probe.
 * The widget re-validates with wcwidth and takes its own resolution on a violation,
 * so a wide glyph here is declined rather than allowed to break the grid. */
static const struct {
  const char *sender;
  const char *glyph;
} kHostInk[] = {
  {"Alice",   "\xe2\x97\x86"},     /* U+25C6 BLACK DIAMOND: one column, used */
  {"Charlie", "\xf0\x9f\x94\xa5"}, /* U+1F525 FIRE: two columns, DECLINED */
};

static int
sHostAvatarGlyph(const IrcitUiConversationMessage *msg_ptr, char *out_ptr, size_t n, void *host_ctx)
{
  size_t i;

  (void)host_ctx;
  for (i = 0; i < sizeof kHostInk / sizeof kHostInk[0]; i++)
    if (strcmp(msg_ptr->sender_name, kHostInk[i].sender) == 0) {
      snprintf(out_ptr, n, "%s", kHostInk[i].glyph);
      return 1;
    }
  return 0; /* decline: the widget resolves it, exactly as if no provider were bound */
}

static const IrcitUiAvatarProvider s_provider = {
  .resolve_avatar_glyph = sHostAvatarGlyph,
  .host_ctx             = NULL
};

/* Report through the layout's own status line.
 *
 * STATIC, because IrcitUiNodeSetText BORROWS: it stores the pointer and the node
 * reads it on every paint, so a buffer on this frame would be a dangling read by
 * the next one. That is the same rule the layout's other captions follow. */
static void
sReport(const char *text_ptr)
{
  static char s_status[160];
  IrcitUiNode *status;

  if (!text_ptr) return;
  /* The live node when there is one: a report made while the loop is running must
     reach the clone on the glass, and the model it was copied from shows nothing.
     The model otherwise, which is all there is before the first frame -- the first
     clone inherits the text from it. */
  status = IrcitUiScreen.root ? IrcitUiFind(IrcitUiScreen.root, STATUS_ID) : NULL;
  if (!status) status = IrcitUiFindModel(STATUS_ID);
  if (!status) return;

  snprintf(s_status, sizeof s_status, "%s", text_ptr);
  IrcitUiNodeSetText(status, s_status);
  IrcitUiInvalidate(status);
}

/* Send one OUTGOING message.
 *
 * THE HOST WRITES ITS OWN STORE AND SAYS SO. It does not hand the widget a struct:
 * the widget's snapshot is refreshed from the callbacks above, so the log is the
 * only place a message is written and the notify is the only thing that tells the
 * widget to look again.
 *
 * @p text_ptr is borrowed only for the duration of the call: sLogAdd copies it, so
 * a stack buffer is safe here and so is a pointer into the field's own buffer --
 * which is what lets the caller clear the field immediately afterwards. */
static int
sSend(const char *text_ptr)
{
  IrcitUiNode *window;

  if (!text_ptr || !text_ptr[0] || !IrcitUiScreen.root) return 0;
  if (!sLogAdd(IRCITUI_MSG_OUTBOUND, IRCITUI_STATUS_SENDING, 0, "Me", text_ptr)) return 0;

  /* Either node will do: the notify resolves the id to both trees, so the live
     clone on the glass and the model a later clone inherits from are both refreshed. */
  window = IrcitUiFind(IrcitUiScreen.root, CHAT_WINDOW_ID);
  if (window) IrcitUiConversationWindowNotifyDatasetChanged(window);

  s_sent++;
  return 1;
}

/* Put --timestamps on the SCREEN node, which is the only node it can be put on.
 *
 * THE CLONE DOES NOT CARRY THE FLAG, and this is the workaround for it. The flag lives
 * in the widget's state and has no node field, and the clone IrcitUiMainLooper builds
 * inherits only the adapter, the dataset context and the avatar provider
 * (src/ui_engine.c:sAllocWidgetState). The gutter needs none of this because it is
 * backed by node->common.bubble_avatar_gutter_cols, a node field the clone does carry
 * and sResolveConfig re-reads every pass.
 *
 * So a single call before the loop cannot work: the loop's screen reset is what builds
 * the node that draws. This re-applies on the clone instead, which is exactly what
 * examples/conversation/conversation_demo.c does -- it sets the flag on a node taken
 * from IrcitUiScreen.root, in a demo that never runs the loop and so never re-clones.
 *
 * Calling it every pass is deliberate and costs a tree walk and a comparison: the
 * setter returns immediately when the value already matches (ui_conversation.c:842). */
static void
sApplyTimestamps(void)
{
  IrcitUiNode *chat;

  if (!s_want_timestamps || !IrcitUiScreen.root) return;
  chat = IrcitUiFind(IrcitUiScreen.root, CHAT_WINDOW_ID);
  if (chat) IrcitUiConversationWindowToggleTimestamps(chat, 1);
}

/**
 * @brief The banner turns red at the tenth unread message, and back below it.
 *
 * THE COUNT IS THE WIDGET'S, asked for through IrcitUiConversationWindowUnreadCount rather
 * than counted here: a host that kept its own tally would be a second derivation of a number
 * the banner is already displaying, and the two would be free to disagree the first time
 * either changed its rule. It is the INBOUND count the banner shows, so the demo's own
 * messages never turn it red.
 *
 * IDEMPOTENT AND SO CALLED ON EVERY CHUNK: the setter returns immediately when the style
 * already matches, so nothing is remembered here and there is no threshold edge to miss --
 * which is what makes it safe on the path a streamed frame arrives on.
 */
static void
sApplyBannerStyle(void)
{
  IrcitUiNode *chat;
  int          unread;

  if (!IrcitUiScreen.root) return;
  chat = IrcitUiFind(IrcitUiScreen.root, CHAT_WINDOW_ID);
  if (!chat) return;

  unread = IrcitUiConversationWindowUnreadCount(chat);
  /* Colour only: the same banner, in the palette's warning role instead of its accent. */
  if (unread >= UNREAD_ALERT_AT) IrcitUiConversationWindowSetNewMsgBannerStyle(chat, IRCITUI_SLOT_TEXT_ERROR, 0);
  else IrcitUiConversationWindowSetNewMsgBannerStyle(chat, IRCITUI_SLOT_ACCENT, 0);
}

/* --- the stream: one reader thread, and the framework's own network seam ------ *
 * THE SHAPE IS THE FRAMEWORK'S, NOT OURS. A worker may not touch a node -- "Thread 0
 * owns the tree. A worker posts; it never touches a node"
 * (IRCITUILIB_FRAMEWORK_API_GUIDE.md:639-643) -- so the reader's whole job is recv()
 * and IrcitUiDispatcherPostNetworkChunk(). Everything that reads a message and writes
 * the log happens on Thread 0, inside IrcitUiMainLooper's envelope drain, in the
 * listener below. There is no mutex here and none is needed: the reader owns nothing
 * the loop reads.
 *
 * TWO SIZES, AND BOTH ARE DERIVED.
 *   The read is 511 bytes because the seam refuses more: the envelope's inline buffer
 *   is 512 and an oversized chunk is REFUSED rather than truncated
 *   (ui_event_dispatcher.c:212). A hard bound, not a preference.
 *   The accumulator is the wire's line cap plus one chunk, because a legal line may
 *   arrive split and the residue has to survive the chunk that finishes it. Sized to
 *   the line alone it would refuse exactly the boundary case the framing exists for.
 *
 * THE LISTENER IS HANDED NO LENGTH. IrcitUiNetworkListener takes a const char * and
 * the seam NUL-terminates what it copied, so strlen() is the only route to a chunk's
 * end -- which is why this wire is newline-delimited JSON and NUL-free by
 * construction (conversation_stream_protocol.h:39-46).
 *
 * IT RUNS WITHOUT THE WATCHDOG (main_looper.c:392-414), so it must not block. It does
 * no allocation, no syscall and no wait: parse one line, write one record, return. */

#define CLIENT_READ_MAX 511
#define CLIENT_RX_CAP   (CONV_STREAM_LINE_MAX + 512)

typedef struct {
  int          fd;
  pthread_t    thread;
  atomic_int   stop;
  char         rx[CLIENT_RX_CAP];
  size_t       rx_len;
  size_t       frames, messages, presence, control, unknown, dropped;
} StreamState;

static StreamState s_stream = { .fd = -1 };

/** @brief The socket's read side. Reads, hands the bytes over, touches nothing else. */
static void *
sStreamReader(void *arg_ptr)
{
  StreamState *st = arg_ptr;
  char         buf[CLIENT_READ_MAX];

  while (!atomic_load(&st->stop)) {
    ssize_t n = recv(st->fd, buf, sizeof buf, 0);

    if (n > 0) {
      /* A ZERO FROM THE POST ENDS THE STREAM RATHER THAN CONTINUING IT. A lost chunk
         desynchronises the line reassembler permanently, so dropping one is worse
         than stopping: the seam waits out its deadline before giving up, and a zero
         here is that answer. The same rule the MSP harness's reader applies. */
      if (!IrcitUiDispatcherPostNetworkChunk(IrcitUiDispatcherInstance(), st, buf, (size_t)n, 200)) {
        fprintf(stderr, "streaming_msg_client_demo: pipeline full for the deadline; ending the stream\n");
        break;
      }
      continue;
    }
    if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
    break; /* EOF, or a real error: either way the stream is over */
  }
  return NULL;
}

/* One wire message, read with the wire's PRIMARY spellings.
 *
 * cluster_position is deliberately NOT read: the widget computes clustering from the
 * run it can see and overwrites whatever a host supplies, so decoding it would put a
 * second authority for it in the tree (ircitui_conversation_message_type.h). And the
 * stamp is the WIRE's, not the local clock's -- this server runs a simulated one, and
 * stamping arrival time would throw away the timeline it is sending. */
static void
sStreamRecord(const char *line_ptr, int len, AppLogRecord *rec_ptr)
{
  char   tmp[64];
  double num = 0;
  int    flag = 0, r;

  memset(rec_ptr, 0, sizeof *rec_ptr);

  if (mjson_get_number(line_ptr, len, "$.message_id", &num) > 0) rec_ptr->id = (uint64_t)num;
  sGetString(line_ptr, len, "$.sender_name", rec_ptr->sender, (int)sizeof rec_ptr->sender, "");
  sGetString(line_ptr, len, "$.timestamp", rec_ptr->stamp, (int)sizeof rec_ptr->stamp, "--:--");
  sGetString(line_ptr, len, "$.text_content", rec_ptr->text, (int)sizeof rec_ptr->text, "");

  sGetString(line_ptr, len, "$.direction", tmp, (int)sizeof tmp, "inbound");
  rec_ptr->direction = sDirection(tmp);
  sGetString(line_ptr, len, "$.status", tmp, (int)sizeof tmp, "delivered");
  rec_ptr->status = sStatus(tmp);

  /* kind is a FIELD on this wire, unlike the fixture's shape-based guess: the server
     says which class a message is, and a status indicator never joins a block. */
  sGetString(line_ptr, len, "$.kind", tmp, (int)sizeof tmp, "message");
  rec_ptr->is_status_indicator = strcmp(tmp, "status_indicator") == 0;

  if (mjson_get_bool(line_ptr, len, "$.has_reply_context", &flag) > 0 && flag) {
    rec_ptr->has_reply_context = true;
    if (mjson_get_number(line_ptr, len, "$.reply_context.reply_msg_id", &num) > 0) rec_ptr->reply_id = (uint64_t)num;
    sGetString(line_ptr, len, "$.reply_context.reply_sender", rec_ptr->reply_sender,
               (int)sizeof rec_ptr->reply_sender, "");
    sGetString(line_ptr, len, "$.reply_context.reply_snippet", rec_ptr->reply_snippet,
               (int)sizeof rec_ptr->reply_snippet, "");
  }
  if (mjson_get_bool(line_ptr, len, "$.interactivity.is_collapsed", &flag) > 0 && flag) rec_ptr->is_collapsed = true;

  for (r = 0; r < IRCITUI_REACTION_BADGES_MAX; r++) {
    double cnt = 0;

    snprintf(tmp, sizeof tmp, "$.reactions[%d].emoji", r);
    if (mjson_get_string(line_ptr, len, tmp, rec_ptr->reactions[r].emoji,
                         (int)sizeof rec_ptr->reactions[r].emoji) <= 0) break;
    snprintf(tmp, sizeof tmp, "$.reactions[%d].count", r);
    if (mjson_get_number(line_ptr, len, tmp, &cnt) > 0) rec_ptr->reactions[r].count = (int)cnt;
    rec_ptr->reaction_count = r + 1;
  }
}

/* The record already held for @p id, or NULL.
 *
 * Scanned from the TAIL, because the only id this wire ever revises is the newest one:
 * a media transfer re-sends one id with a rising percentage, and everything else
 * carries a fresh id. A tail-first scan is therefore the whole of the index this
 * needs, and it stays O(1) in the case that actually happens. */
static AppLogRecord *
sStreamHeld(uint64_t id)
{
  size_t i;

  for (i = s_log_count; i > 0; i--)
    if (s_log[i - 1].id == id) return &s_log[i - 1];
  return NULL;
}

/** @brief Route one de-framed line. Everything here runs on Thread 0. */
static void
sStreamApply(const char *line_ptr)
{
  char type[24];
  int  len = (int)strlen(line_ptr);

  if (mjson_get_string(line_ptr, len, "$.frame_type", type, (int)sizeof type) <= 0) {
    s_stream.unknown++;
    return;
  }
  s_stream.frames++;

  /* hello names a target_composite_id that is neither this demo's chat_window nor the
     other demo's conv_view -- it is a name from the design document, so it is reported
     and not resolved. */
  if (strcmp(type, "hello") == 0) return;

  if (strcmp(type, "presence") == 0) {
    char who[IRCITUI_SENDER_NAME_MAX];
    int  typing = 0;

    s_stream.presence++;
    if (mjson_get_bool(line_ptr, len, "$.is_typing", &typing) > 0 && typing) {
      sGetString(line_ptr, len, "$.typing_sender", who, (int)sizeof who, "");
      IrcitUiConversationWindowSetTyping(IrcitUiFindModel(CHAT_WINDOW_ID), 1, who);
    } else {
      IrcitUiConversationWindowSetTyping(IrcitUiFindModel(CHAT_WINDOW_ID), 0, NULL);
    }
    return;
  }

  /* A control frame is the server's ANSWER to some client's command and arrives here
     UNCAUSED, because the paused state is global and every client is told
     (conversation_stream_server.c:1020). It is neither an error nor a message. */
  if (strcmp(type, "control") == 0) {
    s_stream.control++;
    return;
  }

  if (strcmp(type, "message") != 0 && strcmp(type, "media_progress") != 0) {
    s_stream.unknown++;
    return;
  }

  {
    AppLogRecord  msg;
    AppLogRecord *slot;

    sStreamRecord(line_ptr, len, &msg);
    s_stream.messages++;

    slot = sStreamHeld(msg.id);
    if (slot) {
      *slot = msg; /* the transfer's next slice, onto the row it already has */
    } else {
      slot = sLogEnsure();
      if (!slot) {
        s_stream.dropped++;
        return;
      }
      *slot = msg;
      s_log_count++;
    }

    /* One notify per line. They are cheap to repeat: the notify only SETS a flag, and
       the model pass consumes it (ui_conversation.c:714 -> :485), so every notify in
       one loop pass collapses into a single reconcile. Batching here would be code
       that does nothing. */
    IrcitUiConversationWindowNotifyDatasetChanged(IrcitUiFind(IrcitUiScreen.root, CHAT_WINDOW_ID));
    /* After the notify, and it reads a count that is already current: the accessor brings the
       model pass with it, so the threshold is crossed on the frame the tenth message arrives
       rather than one message later. */
    sApplyBannerStyle();
  }
}

/** @brief The application's entry point for the seam's bytes. Thread 0, synchronous. */
static int
sOnNetworkChunk(IrcitUiEventDispatcher *disp_ptr, void *stream_token, const char *bytes_ptr, void *user_ptr)
{
  StreamState *st = user_ptr;
  size_t       len;

  (void)disp_ptr;
  (void)stream_token;
  if (!st || !bytes_ptr) return 0;

  /* The tree exists by the first drain, so this is where a flag the clone cannot
     inherit gets onto the clone. Before the parse, so the frame that arrives with it
     is already drawn with its stamp row. */
  sApplyTimestamps();

  len = strlen(bytes_ptr);

  /* THE CAP IS THE WIRE'S AND THE BUFFER IS THIS SIDE'S. A line that cannot fit even
     at the wire's own maximum is one this reassembler can never finish, so the buffer
     is dropped whole rather than grown to fit whatever a peer invents -- the
     alternative is memory chosen by whoever is on the socket. */
  if (len >= sizeof st->rx - st->rx_len) {
    s_stream.dropped++;
    st->rx_len = 0;
    return 1;
  }

  memcpy(st->rx + st->rx_len, bytes_ptr, len);
  st->rx_len += len;
  st->rx[st->rx_len] = '\0';

  {
    char  *head = st->rx;
    char  *nl;
    size_t consumed;

    while ((nl = strchr(head, '\n')) != NULL) {
      *nl = '\0';
      if (nl > head) sStreamApply(head);
      head = nl + 1;
    }

    /* THE RESIDUE IS WHAT IS LEFT, NOT WHAT WAS EATEN. `head` has advanced past every
       complete line, so the bytes still to keep are rx_len - (head - rx); shifting
       `head - rx` of them instead would copy the consumed count from the wrong place
       and fill the buffer with whatever followed it. That read is not fatal, which is
       what makes it worth the note: it produced 30 unparseable lines and no crash. */
    consumed = (size_t)(head - st->rx);
    if (consumed < st->rx_len) {
      memmove(st->rx, head, st->rx_len - consumed);
      st->rx_len -= consumed;
    } else {
      st->rx_len = 0;
    }
    st->rx[st->rx_len] = '\0';
  }
  return 1;
}

/**
 * @brief Attach to the server and start reading. 1 when the stream is running.
 *
 * @details THE PORT WALKS, AS THE SERVER'S DOES. The server takes no arguments: it
 *          binds the first free port from its base and prints which, so a client that
 *          assumes the base is wrong the moment 6667 is busy. Trying the same range is
 *          the client half of that agreement -- no argument, and no log to parse.
 *
 *          FAILING TO ATTACH IS NOT FATAL. The server is a separate program that may
 *          be started after this one, and a chat window that refused to open because a
 *          simulator was not running would be a worse instrument than one that says so
 *          on its own status line.
 */
static int
sStreamStart(const char *host_ptr, int base_port)
{
  struct sockaddr_in addr;
  int                fd = -1, port;

  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  if (inet_pton(AF_INET, host_ptr, &addr.sin_addr) != 1) return 0;

  for (port = base_port; port < base_port + CONV_STREAM_PORT_TRIES; port++) {
    addr.sin_port = htons((uint16_t)port);
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return 0;
    if (connect(fd, (struct sockaddr *)&addr, sizeof addr) == 0) break;
    close(fd);
    fd = -1;
  }
  if (fd < 0) return 0;

  s_stream.fd     = fd;
  s_stream.rx_len = 0;
  atomic_store(&s_stream.stop, 0);

  /* The listener before the thread, so no chunk can be drained with nothing to hand it
     to -- the loop warns and drops in that case, and the drop would be this side's. */
  IrcitUiDispatcherRegisterNetworkListener(IrcitUiDispatcherInstance(), sOnNetworkChunk, &s_stream);
  if (pthread_create(&s_stream.thread, NULL, sStreamReader, &s_stream) != 0) {
    IrcitUiDispatcherRegisterNetworkListener(IrcitUiDispatcherInstance(), NULL, NULL);
    close(fd);
    s_stream.fd = -1;
    return 0;
  }
  return 1;
}

/**
 * @brief Stop the reader and close the socket.
 *
 * @details THE ORDER IS THE CONTRACT, not tidiness: a descriptor closed under a thread
 *          blocked in recv() is undefined, and shutting it down first is what reliably
 *          wakes that thread. The listener is cleared LAST, because a callback reached
 *          with a token whose storage is released is a use-after-free the framework has
 *          no way to detect.
 */
static void
sStreamStop(void)
{
  if (s_stream.fd < 0) return;
  shutdown(s_stream.fd, SHUT_RDWR);
  atomic_store(&s_stream.stop, 1);
  pthread_join(s_stream.thread, NULL);
  close(s_stream.fd);
  s_stream.fd = -1;
  IrcitUiDispatcherRegisterNetworkListener(IrcitUiDispatcherInstance(), NULL, NULL);
}

/* --- stage 1: the intercept, before the buffer mutates ---------------------- */

/**
 * @brief The application's key policy for the chat input.
 *
 * @details Runs before the field's editor, and a non-zero return consumes the key. Enter is
 *          claimed here so the message is sent and the newline is *not* inserted: the field is
 *          single-line, so the character would be invisible yet still reach whatever treats the
 *          buffer as a line. F10 asks the loop to stop.
 *
 * @return 1 to swallow the key, 0 to let the framework's own handling continue.
 */
static int
sOnInputKeyDown(IrcitUiEventDispatcher *disp_ptr, IrcitUiEvent *ev_ptr, void *user_ptr)
{
  IrcitUiNode *field;
  const char  *payload;
  int          key;

  (void)user_ptr;
  if (!ev_ptr || !IrcitUiScreen.root) return 0;

  /* The unstreamed path's only chance at it: no chunk will arrive to carry it. */
  sApplyTimestamps();

  /* The accessor, not the member: IrcitUiEvent is opaque to an application. */
  key = IrcitUiEventGetKey(ev_ptr);

  if (key == KEY_F(10)) {
    IrcitUiDispatcherExit(disp_ptr);
    return 1;
  }
  if (key != '\r' && key != '\n' && key != KEY_ENTER) return 0;

  field = IrcitUiFind(IrcitUiScreen.root, CHAT_INPUT_ID);
  if (!field) return 0;

  payload = IrcitUiEditTextGetValue(field);
  if (payload && payload[0]) {
    /* Send first, report second, clear last: the payload is borrowed from the very
       buffer the clear empties, so the order is the correctness here. */
    if (sSend(payload)) sReport("sent -- F10 to quit");
    IrcitUiEditTextClear(field);
  }
  return 1; /* the raw '\n' never reaches the buffer */
}

/* --- stage 3: the sync, after the mutation is accepted --------------------- */

/**
 * @brief The application's post-mutation notification for the chat input.
 *
 * @details Handed the finished text, so a host can act on it without polling. This one watches
 *          for a single word: type it and a message is sent with no keypress at all. It can only
 *          fire on the loop's fast lane, which is why this demo runs IrcitUiMainLooper.
 *
 * @param inserted_char The character just inserted, or 0 on a path that does not insert.
 * @param new_text      The finished buffer. Borrowed, and it lives until the field is destroyed.
 */
static void
sOnInputTextChanged(IrcitUiEventDispatcher *disp_ptr, IrcitUiNode *node_ptr, int inserted_char, const char *new_text, void *user_ptr)
{
  (void)disp_ptr;
  (void)inserted_char;
  (void)user_ptr;

  if (!node_ptr || !new_text) return;
  if (strcmp(new_text, TRIGGER_WORD) != 0) return;

  /* Copied before the clear, for the same reason the Enter path copies: new_text is
     the field's own buffer, and the clear is about to empty it. */
  {
    char built[160];
    snprintf(built, sizeof built, "%s (typed)", new_text);
    if (sSend(built)) {
      sReport("trigger word sent -- F10 to quit");
      IrcitUiEditTextClear(node_ptr);
    }
  }
}

/**
 * @brief Take the terminal, once the registry has locked a backend.
 *
 * @details This is the hook for it rather than main(): it runs from
 *          IrcitUiInitialiseFrameworkContext, after a driver exists and before the framework uses
 *          it, which is the first point at which an application knows which backend it is
 *          initialising. THE APPLICATION OWNS THE TERMINAL, not the framework -- the engine paints
 *          through the driver and never calls initscr itself.
 */
static void
sOnDriverInitialised(IrcitUiWindowDriver *driver_ptr)
{
  (void)driver_ptr;

  /* Before initscr, because ncurses reads it as it initialises: it stops ncurses
     substituting its own line-drawing glyphs for the ACS characters, which would
     displace the box-drawing the engine emits. */
  setenv("NCURSES_NO_UTF8_ACS", "1", 0);

  initscr();
  cbreak();
  noecho();
  keypad(stdscr, TRUE);
  if (has_colors()) start_color();
  /* The engine paints the only caret there is, so the hardware one is turned off
     rather than left sitting in the field as a second, stationary caret. */
  curs_set(0);
  /* A lone ESC is resolved against ESCDELAY, 1000 ms by default -- longer than
     the blink tick, so Esc would be held for up to a whole tick. */
  set_escdelay(25);
  IrcitUiMouseInit();
}

/* THE TWO SEAMS AS WIRED, and the direction of every arrow is a claim.
 *
 * INPUT -- the host owns the data; the adapter is the READ half.
 *
 *   keystroke ──► cmdline (IRCITUI_EDIT_TEXT: the framework's own field, no adapter)
 *      │
 *      ├─ on_key_down      IrcitUiBindKey         sOnInputKeyDown
 *      └─ on_text_changed  IrcitUiBindTextChanged sOnInputTextChanged
 *      │                            │
 *      └────────────┬───────────────┘
 *                   ▼
 *              sSend
 *                   │
 *                   ├─ sLogAdd ──► s_log  (the HOST's store; only the two writers here touch it)
 *                   │
 *                   └─ NotifyDatasetChanged(window)
 *                              │
 *                              ▼
 *                sReconcile (ui_conversation.c:620)
 *                              │  one read per index, and it COPIES what it keeps
 *                              ▼
 *                sAppGetItemInfo   ← the adapter, and the ONLY way in
 *                              │
 *                              ▼
 *                the widget's snapshot ──► the glass
 *
 * STREAM -- the same adapter, the same store, a SECOND WRITER. The only difference from
 * the arrow above is which thread the write lands on, and the framework frames it: a
 * worker may not touch a node, so the reader hands bytes over and Thread 0 writes.
 *
 *   socket ──► sStreamReader (its OWN thread)
 *                 │  recv() at most 511 bytes, because the seam REFUSES more
 *                 ▼
 *        IrcitUiDispatcherPostNetworkChunk(disp, &s_stream, buf, n, 200)
 *                 │  a zero here ENDS the stream: a lost chunk desynchronises a
 *                 │  reassembler permanently, so stopping beats dropping
 *                 ▼
 *        eventfd doorbell ──► IrcitUiMainLooper's poll() wakes
 *                 │
 *                 ▼
 *   sOnNetworkChunk ── THREAD 0, from the loop's own envelope drain
 *                 │  accumulates to a '\n'; the buffer is the wire's line cap plus
 *                 │  one chunk, because a legal line may arrive split
 *                 ▼
 *   sStreamApply ─┬─ hello / control ─► counted, never messages (control arrives
 *                 │                    UNCAUSED: the paused state is global, so every
 *                 │                    client is told about every client's command)
 *                 ├─ presence ────────► SetTyping(model). NOT a record: it has no id,
 *                 │                     and a phantom row is what the doc's own client
 *                 │                     built by treating it as one
 *                 └─ message / media_progress
 *                          │  mjson, the wire's PRIMARY spellings
 *                          ▼
 *                   sStreamHeld(id) ── hit? ──► that row is UPDATED in place
 *                          │ miss               (a transfer re-sends one id)
 *                          ▼
 *                      sLogEnsure() ──► s_log
 *                          │
 *                          └─ NotifyDatasetChanged(window) ──► the sReconcile above
 *
 * ONE RECONCILE PER LOOP PASS, NOT PER FRAME. The notify only SETS a flag and the model
 * pass consumes it (ui_conversation.c:714 -> :485), so a burst of frames collapses into a
 * single reconcile. Batching here would be code that does nothing.
 *
 * PAINT -- the provider supplies one cell of ink and moves nothing else.
 *
 *   sDrawAvatar (ui_conversation.c:1481) ──► sAvatarGlyph (:1424)
 *                                                 │
 *               provider bound? ──── no ──────────┤
 *                     │ yes                       │
 *                     ▼                           │
 *           sHostAvatarGlyph                      │
 *                     │                           │
 *           one display column? ── no ──► DECLINED┤  a wide glyph cannot go in a
 *                     │ yes                       │  one-column grid, so it is
 *                     ▼                           ▼  refused, not drawn wide
 *           the host's glyph              the built-in: the sender's initial,
 *                     │                   else the message-id-hashed list
 *                     └──────────┬────────────────┘
 *                                ▼
 *             the gutter cell: a 5x3 block whose middle row holds it
 *
 * WIRED AT STARTUP, IN THIS ORDER, all on the MODEL node, in main's body:
 *   1. sSeedHistory or sIngestFixture -- fill s_log
 *   2. IrcitUiConversationWindowBindAdapter(IrcitUiFindModel(CHAT_WINDOW_ID), ...)
 *   3. IrcitUiConversationWindowSetAvatarGutter, then ...SetAvatarProvider
 *   4. sStreamStart -- LAST, and before the loop: the listener runs on Thread 0 from
 *      inside the loop's drain, so installing it once the loop is running is a race
 *      rather than a reconfiguration
 *
 * STEP 3 MUST COME AFTER STEP 2. The model node has no state until the binding gives
 * it some, and SetAvatarGutter returns early on a node without state -- called first
 * it sets nothing and the gutter stays 0 whatever --avatars says.
 *
 * EVERY NAME ABOVE IS A NAME AND NOT A LINE. These were line numbers, and one edit --
 * inserting the stream section -- moved every one of them at once. A diagram whose
 * claims are numbers rots on contact, so the call sites are named and found with grep.
 * The library's numbers (ui_conversation.c) are the exception: that file is not this
 * one's to edit, and those three still resolve.
 *
 * WHY THE INPUT ARROW IS ONE-WAY. Measured: with get_item_count patched to return 0,
 * a typed message does not reach the glass -- the exit line reads "1 sent, host holds
 * 4, chat_window holds 0 row(s)", the host log having grown anyway. There is no
 * second path into the widget, and the framework never writes into host memory; a
 * browser reading this file is the host, and sSend is where it speaks. */

/**
 * @brief Run the interactive chat on the framework's own loop.
 *
 * @details Flags: an optional driver name; --ingest [path] to load the golden
 *          fixture instead of the seed; --avatars to open the gutter and
 *          --provider to supply its ink; --timestamps to show each run's stamp row;
 *          --stream [host[:port]] to attach to the conversation streaming server.
 *
 * @param argc Argument count.
 *
 * @param argv Argument vector, parsed in main's first loop.
 *
 * @return 0 on a normal exit.
 */
int
main(int argc, char **argv)
{
  IrcitUiWindowRegistry *registry;
  IrcitUiContext        *ctx;
  const char            *driver  = "ncurses";
  const char            *fixture = NULL;
  char                  host_buf[64] = "";
  const char            *stream_host = "127.0.0.1";
  int                    stream_port = CONV_STREAM_PORT_BASE;
  int                    streaming = 0;
  int                    avatars = 0;
  int                    timestamps = 0;
  int                    provider = 0;
  int                    rows, i;
  int                    ingested = 0;

  /* --ingest [path]: load the golden input into the host's store instead of the
     three-message seed. --avatars opens the gutter; --provider also supplies the
     ink. --stream [host[:port]] attaches to the streaming server, which is the point
     of this copy. The driver name is still the first bare argument. */
  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--ingest") == 0) {
      fixture = (i + 1 < argc && argv[i + 1][0] != '-') ? argv[++i] : DEFAULT_FIXTURE;
    } else if (strcmp(argv[i], "--stream") == 0) {
      streaming = 1;
      if (i + 1 < argc && argv[i + 1][0] != '-') {
        char *colon;

        snprintf(host_buf, sizeof host_buf, "%s", argv[++i]);
        colon = strrchr(host_buf, ':');
        if (colon) {
          *colon = '\0';
          stream_port = atoi(colon + 1);
        }
        stream_host = host_buf;
      }
    } else if (strcmp(argv[i], "--avatars") == 0) {
      avatars = 1;
    } else if (strcmp(argv[i], "--timestamps") == 0) {
      /* THE SAME FLAG NAME conversation_demo USES, and deliberately not a key here the
         way it is there: that demo has no text field, so it can spend 't' on the
         toggle. This one's field needs the letter. */
      timestamps = 1;
    } else if (strcmp(argv[i], "--provider") == 0) {
      avatars = 1;
      provider = 1;
    } else {
      driver = argv[i];
    }
  }

  /* Before anything measures a display column: with the C locale active the
     framework would count UTF-8 bytes where a terminal shows cells. */
  setlocale(LC_ALL, "");

  if (IrcitUiLogInit("warn", "streaming_msg_client_demo.log", IRCITUI_LOG_OVERWRITE) != 0)
    fputs("streaming_msg_client_demo: logging is off\n", stderr);

  /* The application takes the terminal in the callback, which is where a backend
     is known to exist; nothing about this demo's own wiring is needed there. */
  registry = IrcitUiWindowRegistryCreate((IrcitUiDriverRegistryUserCallbacks){
    .on_driver_initialised = sOnDriverInitialised,
  });
  if (!registry || !IrcitUiWindowRegistryRequestDriver(registry, driver)) {
    fprintf(stderr, "%s: no window backend could be locked for '%s'\n", argv[0], driver);
    IrcitUiLogFini();
    return 1;
  }

  /* The binder is invoked inside this call, so the reference model is in place
     before the id lookup below -- there is nothing to call afterwards. */
  ctx = IrcitUiInitialiseFrameworkContext(IrcitUiDataBind, registry);

  /* Filled before the binding, so the first frame already shows the whole dataset
     and the initial reconcile has all of it to carry. --ingest replaces the seed
     rather than adding to it: one source, so the exit counts mean one thing. */
  if (fixture) {
    ingested = sIngestFixture(fixture);
    if (ingested < 0) fprintf(stderr, "streaming_msg_client_demo: could not read the fixture at %s\n", fixture);
    else fprintf(stderr, "streaming_msg_client_demo: ingested %d message(s) from %s\n", ingested, fixture);
  } else {
    sSeedHistory();
  }

  /* Bound on the MODEL, like the two hooks below and for the same reason: the
     model exists as soon as the call above returns, and every clone inherits the
     binding as it is made. */
  if (!IrcitUiConversationWindowBindAdapter(IrcitUiFindModel(CHAT_WINDOW_ID), &s_app_adapter, NULL))
    fputs("streaming_msg_client_demo: could not bind the conversation adapter\n", stderr);

  /* After the binding, because the binding is what gives the model node its state. */
  if (s_typing_pending)
    IrcitUiConversationWindowSetTyping(IrcitUiFindModel(CHAT_WINDOW_ID), 1, s_typing_name);

  /* THE ORDER HERE IS NOT COSMETIC. SetAvatarGutter returns early on a node with no
   * state, so it has to come after the binding -- the same blind spot §4.5 records
   * for the other seven setters. Called first, it would set nothing and the gutter
   * would stay 0 whatever the flag said. */
  if (avatars) {
    IrcitUiNode *chat = IrcitUiFindModel(CHAT_WINDOW_ID);

    IrcitUiConversationWindowSetAvatarGutter(chat, 6);
    if (provider) IrcitUiConversationWindowSetAvatarProvider(chat, &s_provider);
  }

  /* AFTER THE BINDING, for the reason the gutter above is: the setter returns early on
   * a node with no state. It is a REQUEST, not the act -- sApplyTimestamps carries it
   * to the clone the loop builds, which is the node that draws. */
  s_want_timestamps = timestamps;

  /* Both bind on the MODEL, which is why they are made here and not from a
     first-pass callback: the model exists as soon as the call above returns, and
     every clone of these two nodes inherits the hooks as it is made -- before the
     loop's first frame and after every screen reset since. Nothing here holds a
     node pointer, which is what keeps the hooks from becoming dangles. */
  if (!IrcitUiBindKey(CHAT_INPUT_ID, sOnInputKeyDown, NULL))
    fputs("streaming_msg_client_demo: could not bind the key interceptor\n", stderr);
  if (!IrcitUiBindTextChanged(CHAT_INPUT_ID, sOnInputTextChanged, NULL))
    fputs("streaming_msg_client_demo: could not bind the text-changed sync\n", stderr);

  /* Reaches the model, because the screen tree is not up yet -- see sReport. */
  sReport("type " TRIGGER_WORD " for a bubble, or ENTER to send -- F10 quits");

  /* Attached LAST, after every model binding and hook above, and before the loop --
     the two things the seam requires. The listener runs on Thread 0 from inside the
     loop's drain, so installing it while the loop runs would be a race rather than a
     reconfiguration (ircit_ui_event_dispatcher_type.h). */
  if (streaming) {
    if (sStreamStart(stream_host, stream_port)) {
      char msg[160];

      snprintf(msg, sizeof msg, "attached to %s:%d -- F10 quits", stream_host, stream_port);
      sReport(msg);
    } else {
      /* Not fatal: the server is a separate program and may be started after this
         one. A chat window that refused to open because a simulator was not running
         would be a worse instrument than one that says so. */
      fprintf(stderr, "streaming_msg_client_demo: no stream at %s:%d, trying to %d; carrying on unstreamed\n",
              stream_host, stream_port, stream_port + CONV_STREAM_PORT_TRIES - 1);
      sReport("no stream -- F10 quits");
    }
  }

  /* From here the framework owns measuring, drawing, the blink tick, the resize
     and the input fast lane. */
  IrcitUiMainLooper(ctx);

  /* Producers quiesced BEFORE the context goes, which is the order the framework's
     own teardown note asks for -- and this is the only party able to do it. */
  sStreamStop();

  /* Read the widget's own count before the tree goes, so the summary is the
     widget's answer rather than this file's counter agreeing with itself -- and
     print the host's count beside it, because the two come from opposite sides of
     the seam and can only agree if the reconcile ran. */
  rows = IrcitUiConversationWindowRowCount(IrcitUiFind(IrcitUiScreen.root, CHAT_WINDOW_ID));

  IrcitUiDestroyFrameworkContext(ctx);
  IrcitUiWindowRegistryDestroy(registry);

  /* The application ran initscr() and turned mouse reporting on, so it undoes
     both: endwin() does not turn the reporting off by itself. */
  fputs("\033[?1006l\033[?1003l\033[?1002l\033[?1000l", stdout);
  fflush(stdout);
  IrcitUiLogFini();
  endwin();

  printf("streaming_msg_client_demo: %lu sent, host holds %zu, %s holds %d row(s)\n", s_sent, s_log_count,
         CHAT_WINDOW_ID, rows);
  if (streaming) {
    printf("streaming_msg_client_demo: %zu frame(s) -- %zu message, %zu presence, %zu control, %zu unknown, %zu dropped\n",
           s_stream.frames, s_stream.messages, s_stream.presence, s_stream.control, s_stream.unknown, s_stream.dropped);
  }
  return 0;
}
