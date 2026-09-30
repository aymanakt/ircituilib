/**
 * @file conversation_stream_protocol.h
 * @brief The conversation stream's wire contract: one JSON object per line, shared by
 *        the simulation server and whatever consumes it.
 *
 * THIS HEADER BELONGS TO NEITHER PROGRAM. It includes nothing from ircituilib, nothing
 * from curses and nothing from pthread, and it defines its functions `static inline`
 * rather than in a translation unit of its own -- so there is no library to link, no
 * link order to get right, and each program compiles its own copy. The absent
 * `target_link_libraries` on the server target is the mechanical form of that claim.
 *
 * ## Two spellings, one field list
 *
 * The wire carries the frozen fixture's key names as primary and the design document's
 * names as aliases beside them: `message_id` and `id`, `sender_name` and `sender`,
 * `cluster_position` and `cluster_mode`. Both are written by the same statement from the
 * same source value, so a spelling cannot be edited without the other moving. The cost is
 * bytes on every frame; the benefit is that a consumer ported from either document
 * decodes the stream without a translation layer.
 *
 * Two things a name is not enough to reconcile:
 *
 * - `timestamp` is the DISPLAY STRING, not the epoch, because the widget prints it
 *   verbatim and never parses it. The epoch travels under `timestamp_epoch`. A number
 *   under `timestamp` is not readable by a string reader at all -- measured in
 *   dev/probes/conversation_streaming/p_stream_schema.c, case `collision`.
 * - `kind` and `interactivity.is_collapsed` are primary-only, because the document's
 *   spelling has no key for either: a collapsed message and a local-side status
 *   indicator are both inexpressible without them (same probe, case `alias`).
 *
 * ## Presence is a frame type, not a field
 *
 * The document's server folds `presence_update` into every frame, and its own client
 * classifies by `is_typing` -- so every message it emits is routed to the presence-off
 * branch and appended to nothing. Here presence is its own frame type and a message
 * frame carries no presence key at all, which is what makes that classifier reach its
 * third branch (same probe, case `presence`).
 *
 * ## Why the frames are text, NUL-free, and one per line
 *
 * A frame has to survive the framework's application-network seam, whose listener takes a
 * `const char *` and NO length (include/ircituilib/ircit_ui_event_dispatcher_type.h:66-69),
 * leaving `strlen()` as the only route to a chunk's length. A wire with a NUL in it would
 * arrive truncated with no way to discover the truth. Every byte of a frame is therefore
 * in [0x20,0x7E], or a continuation byte of a UTF-8 sequence, or the trailing LF -- and
 * the LF is the only delimiter.
 */
#ifndef CONVERSATION_STREAM_PROTOCOL_H
#define CONVERSATION_STREAM_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── The transport's own bounds ─────────────────────────────────────────────── */

#define CONV_STREAM_PORT_BASE 6667
#define CONV_STREAM_PORT_TRIES 10
#define CONV_STREAM_MAX_CLIENTS 16
/* One frame, its newline included. A frame that does not fit is REFUSED rather than
 * truncated -- a truncated frame is one the peer parses as something else.
 *
 * The bound is sized from the WORST case the contract itself permits, because a bound
 * that refuses a legal value is a defect in the server rather than a defence:
 *
 *   6 bytes per byte (the widest escape, \u00XX)
 *   x 2 spellings (the body and the snippet are each written twice)
 *   x (CONV_STREAM_TEXT_MAX + CONV_STREAM_SNIPPET_MAX)
 *   + envelope  ->  14294 bytes, measured
 *
 * So the aliasing's real cost is not "a few bytes per frame": it doubles the dominant
 * term. 16384 holds that with about 2 KB of headroom. Measured in
 * dev/probes/conversation_streaming/p_stream_frames.c, case `bounds`, which fails if
 * this bound and the two below it ever stop agreeing. */
#define CONV_STREAM_LINE_MAX 16384

/* ── The vocabulary ─────────────────────────────────────────────────────────── */

typedef enum {
    CONV_STREAM_FRAME_HELLO = 0,
    CONV_STREAM_FRAME_MESSAGE,
    CONV_STREAM_FRAME_PRESENCE,
    CONV_STREAM_FRAME_MEDIA_PROGRESS,
    /* The fifth frame type, and the only one that travels UPWARD. A client sends a command and the
     * server answers with one of these -- see the control section at the foot of this file. */
    CONV_STREAM_FRAME_CONTROL
} ConvStreamFrameType;

typedef enum {
    CONV_STREAM_DIR_INBOUND = 0,
    CONV_STREAM_DIR_OUTBOUND,
    CONV_STREAM_DIR_SYSTEM
} ConvStreamDirection;

typedef enum {
    CONV_STREAM_STATUS_SENDING = 0,
    CONV_STREAM_STATUS_DELIVERED,
    CONV_STREAM_STATUS_READ,
    CONV_STREAM_STATUS_EXPIRED
} ConvStreamStatus;

typedef enum {
    CONV_STREAM_CLUSTER_LONE = 0,
    CONV_STREAM_CLUSTER_TOP,
    CONV_STREAM_CLUSTER_MIDDLE,
    CONV_STREAM_CLUSTER_BOTTOM
} ConvStreamCluster;

typedef enum {
    CONV_STREAM_KIND_MESSAGE = 0,
    CONV_STREAM_KIND_STATUS_INDICATOR
} ConvStreamKind;

/* The widths are the widget's, so a value that fits the wire fits the model. */
#define CONV_STREAM_NAME_MAX 32
#define CONV_STREAM_STAMP_MAX 16
#define CONV_STREAM_TEXT_MAX 1024
#define CONV_STREAM_SNIPPET_MAX 128
#define CONV_STREAM_BADGES_MAX 8
#define CONV_STREAM_EMOJI_MAX 8

typedef struct ConvStreamReaction {
    char emoji[CONV_STREAM_EMOJI_MAX]; ///< A UTF-8 sequence, not a byte.
    int  count;
} ConvStreamReaction;

/** @brief One message as it travels. Both string pointers are borrowed for the call. */
typedef struct ConvStreamMessage {
    uint64_t            message_id;
    char                sender_name[CONV_STREAM_NAME_MAX];
    char                timestamp[CONV_STREAM_STAMP_MAX]; ///< The display string.
    uint64_t            timestamp_epoch;
    const char         *text_ptr;          ///< Borrowed; escaped on the way out.
    ConvStreamDirection direction;
    ConvStreamStatus    status;
    ConvStreamCluster   cluster;
    ConvStreamKind      kind;
    bool                is_collapsed;
    bool                has_reply_context;
    uint64_t            reply_msg_id;
    char                reply_sender[CONV_STREAM_NAME_MAX];
    const char         *reply_snippet_ptr; ///< Borrowed, or NULL.
    ConvStreamReaction  reactions[CONV_STREAM_BADGES_MAX];
    int                 reaction_count;
} ConvStreamMessage;

/* ── The name tables ────────────────────────────────────────────────────────── */

static inline const char *
ConvStreamFrameTypeName(ConvStreamFrameType type)
{
    switch (type) {
        case CONV_STREAM_FRAME_HELLO: return "hello";
        case CONV_STREAM_FRAME_MESSAGE: return "message";
        case CONV_STREAM_FRAME_PRESENCE: return "presence";
        case CONV_STREAM_FRAME_MEDIA_PROGRESS: return "media_progress";
        case CONV_STREAM_FRAME_CONTROL: return "control";
    }
    return "unknown";
}

static inline const char *
ConvStreamDirectionName(ConvStreamDirection dir)
{
    switch (dir) {
        case CONV_STREAM_DIR_INBOUND: return "inbound";
        case CONV_STREAM_DIR_OUTBOUND: return "outbound";
        case CONV_STREAM_DIR_SYSTEM: return "system";
    }
    return "inbound";
}

static inline const char *
ConvStreamStatusName(ConvStreamStatus status)
{
    switch (status) {
        case CONV_STREAM_STATUS_SENDING: return "sending";
        case CONV_STREAM_STATUS_DELIVERED: return "delivered";
        case CONV_STREAM_STATUS_READ: return "read";
        case CONV_STREAM_STATUS_EXPIRED: return "expired";
    }
    return "read";
}

static inline const char *
ConvStreamClusterName(ConvStreamCluster cluster)
{
    switch (cluster) {
        case CONV_STREAM_CLUSTER_LONE: return "lone";
        case CONV_STREAM_CLUSTER_TOP: return "top";
        case CONV_STREAM_CLUSTER_MIDDLE: return "middle";
        case CONV_STREAM_CLUSTER_BOTTOM: return "bottom";
    }
    return "lone";
}

static inline const char *
ConvStreamKindName(ConvStreamKind kind)
{
    return kind == CONV_STREAM_KIND_STATUS_INDICATOR ? "status_indicator" : "message";
}

/* ── The writer's own primitives ────────────────────────────────────────────────
 *
 * Every builder below appends through these and returns -1 the moment one of them
 * refuses, so "the frame did not fit" is one decision taken in one place rather than a
 * bound re-checked at each of thirty call sites.
 */

/*!
 * @brief Escape @p in_ptr into @p out_ptr as JSON string content.
 *
 * @return The bytes written, excluding the terminator, or (size_t)-1 when the buffer is
 *         too small. Control bytes become \u escapes and `"` and `\` are backslashed;
 *         bytes >= 0x80 pass through untouched, which is what keeps a UTF-8 glyph itself
 *         rather than mojibake. A NULL input is the empty string, not an error, because
 *         an optional snippet is absent rather than malformed.
 */
static inline size_t
ConvStreamEscape(const char *in_ptr, char *out_ptr, size_t cap)
{
    const unsigned char *p_ptr = (const unsigned char *)(in_ptr ? in_ptr : "");
    size_t               at = 0;

    if (!out_ptr || cap == 0) return (size_t)-1;
    for (; *p_ptr; p_ptr++) {
        const char *rep_ptr = NULL;
        char        buf[8];

        switch (*p_ptr) {
            case '"': rep_ptr = "\\\""; break;
            case '\\': rep_ptr = "\\\\"; break;
            case '\b': rep_ptr = "\\b"; break;
            case '\f': rep_ptr = "\\f"; break;
            case '\n': rep_ptr = "\\n"; break;
            case '\r': rep_ptr = "\\r"; break;
            case '\t': rep_ptr = "\\t"; break;
            default:
                if (*p_ptr < 0x20) {
                    snprintf(buf, sizeof buf, "\\u%04X", (unsigned)*p_ptr);
                    rep_ptr = buf;
                }
                break;
        }
        if (rep_ptr) {
            size_t n = strlen(rep_ptr);
            if (at + n + 1 > cap) return (size_t)-1;
            memcpy(out_ptr + at, rep_ptr, n);
            at += n;
        }
        else {
            if (at + 2 > cap) return (size_t)-1;
            out_ptr[at++] = (char)*p_ptr;
        }
    }
    out_ptr[at] = '\0';
    return at;
}

static inline int
ConvStreamPut(char *out_ptr, size_t cap, size_t *at_ptr, const char *text_ptr)
{
    size_t n = strlen(text_ptr);

    if (*at_ptr + n + 1 > cap) return -1;
    memcpy(out_ptr + *at_ptr, text_ptr, n);
    *at_ptr += n;
    out_ptr[*at_ptr] = '\0';
    return 0;
}

/** @brief Append a `%llu`, for the ids and the epoch. */
static inline int
ConvStreamPutU64(char *out_ptr, size_t cap, size_t *at_ptr, uint64_t value)
{
    char tmp[24];

    snprintf(tmp, sizeof tmp, "%llu", (unsigned long long)value);
    return ConvStreamPut(out_ptr, cap, at_ptr, tmp);
}

/** @brief Append an `%d`, for the tallies and the percentages. */
static inline int
ConvStreamPutInt(char *out_ptr, size_t cap, size_t *at_ptr, int value)
{
    char tmp[16];

    snprintf(tmp, sizeof tmp, "%d", value);
    return ConvStreamPut(out_ptr, cap, at_ptr, tmp);
}

/** @brief Append a JSON string literal, escaped and quoted. */
static inline int
ConvStreamPutStr(char *out_ptr, size_t cap, size_t *at_ptr, const char *value_ptr)
{
    size_t at = *at_ptr;

    if (at + 2 > cap) return -1;
    out_ptr[at++] = '"';
    out_ptr[at] = '\0';
    {
        size_t wrote = ConvStreamEscape(value_ptr, out_ptr + at, cap - at);
        if (wrote == (size_t)-1) return -1;
        at += wrote;
    }
    if (at + 2 > cap) return -1;
    out_ptr[at++] = '"';
    out_ptr[at] = '\0';
    *at_ptr = at;
    return 0;
}

/** @brief Append a `"key":` pair's key half. */
static inline int
ConvStreamPutKey(char *out_ptr, size_t cap, size_t *at_ptr, const char *key_ptr)
{
    if (ConvStreamPut(out_ptr, cap, at_ptr, "\"") != 0) return -1;
    if (ConvStreamPut(out_ptr, cap, at_ptr, key_ptr) != 0) return -1;
    return ConvStreamPut(out_ptr, cap, at_ptr, "\":");
}

/** @brief `"key":"value"`, the shape most of a frame is. */
static inline int
ConvStreamPutPair(char *out_ptr, size_t cap, size_t *at_ptr, const char *key_ptr, const char *value_ptr)
{
    if (ConvStreamPutKey(out_ptr, cap, at_ptr, key_ptr) != 0) return -1;
    return ConvStreamPutStr(out_ptr, cap, at_ptr, value_ptr);
}

/** @brief `"key":<number>`, for the ids, the tallies and the epoch. */
static inline int
ConvStreamPutU64Pair(char *out_ptr, size_t cap, size_t *at_ptr, const char *key_ptr, uint64_t value)
{
    if (ConvStreamPutKey(out_ptr, cap, at_ptr, key_ptr) != 0) return -1;
    return ConvStreamPutU64(out_ptr, cap, at_ptr, value);
}

/* ── The builders ───────────────────────────────────────────────────────────── */

/*!
 * @brief The frame sent once when a client attaches: the fixture's own envelope.
 *
 * @details A client that can read the frozen fixture can read this, which is the point --
 *          the identity, the composite it addresses and the palette mapping travel in the
 *          same shape and under the same names they have in the file. The palette values
 *          are the fixture's, verbatim, including the numbering the renderer would
 *          disagree with: the server reproduces the reference, it does not correct it.
 */
static inline int
ConvStreamBuildHello(char *out_ptr, size_t cap)
{
    size_t at = 0;

    if (ConvStreamPut(out_ptr, cap, &at, "{\"frame_type\":\"hello\",") != 0) return -1;
    if (ConvStreamPutPair(out_ptr, cap, &at, "test_suite", "IrcitUiConversationWindow Reference Matrix") != 0) return -1;
    if (ConvStreamPut(out_ptr, cap, &at, ",") != 0) return -1;
    if (ConvStreamPutPair(out_ptr, cap, &at, "target_composite_id", "active_conversation_panel") != 0) return -1;
    if (ConvStreamPut(out_ptr, cap, &at, ",") != 0) return -1;
    if (ConvStreamPutU64Pair(out_ptr, cap, &at, "generated_timestamp_epoch_ms", 1790000000000ULL) != 0) return -1;
    if (ConvStreamPut(out_ptr, cap, &at, ",\"color_palette_mapping\":{") != 0) return -1;
    if (ConvStreamPutPair(out_ptr, cap, &at, "1", "cyan_bold_outbound") != 0) return -1;
    if (ConvStreamPut(out_ptr, cap, &at, ",") != 0) return -1;
    if (ConvStreamPutPair(out_ptr, cap, &at, "2", "magenta_inbound_alpha") != 0) return -1;
    if (ConvStreamPut(out_ptr, cap, &at, ",") != 0) return -1;
    if (ConvStreamPutPair(out_ptr, cap, &at, "3", "green_inbound_beta") != 0) return -1;
    if (ConvStreamPut(out_ptr, cap, &at, ",") != 0) return -1;
    if (ConvStreamPutPair(out_ptr, cap, &at, "4", "yellow_system_alert") != 0) return -1;
    if (ConvStreamPut(out_ptr, cap, &at, "}}\n") != 0) return -1;
    return (int)at;
}

/**
 * @brief The message fields every message-shaped frame shares.
 *
 * The aliases are written here, adjacently, from one value each -- so `message_id` and
 * `id` cannot describe different messages, and the reply block's `reply_sender` and
 * `sender` cannot name different people. `kind` and `interactivity` appear once because
 * the document's spelling has no name for either.
 */
static inline int
sConvStreamPutMessageFields(char *out_ptr, size_t cap, size_t *at_ptr, const ConvStreamMessage *msg_ptr)
{
    size_t at = *at_ptr;
    int    r;

    /* The primary alias comes first for a reason beyond looks: a consumer with a naive
     * `strstr` for `"sender":` finds the sender's name rather than the reply header's. */
    if (ConvStreamPutU64Pair(out_ptr, cap, &at, "message_id", msg_ptr->message_id) != 0) return -1;
    if (ConvStreamPut(out_ptr, cap, &at, ",") != 0) return -1;
    if (ConvStreamPutU64Pair(out_ptr, cap, &at, "id", msg_ptr->message_id) != 0) return -1;
    if (ConvStreamPut(out_ptr, cap, &at, ",") != 0) return -1;
    if (ConvStreamPutPair(out_ptr, cap, &at, "sender_name", msg_ptr->sender_name) != 0) return -1;
    if (ConvStreamPut(out_ptr, cap, &at, ",") != 0) return -1;
    if (ConvStreamPutPair(out_ptr, cap, &at, "sender", msg_ptr->sender_name) != 0) return -1;
    if (ConvStreamPut(out_ptr, cap, &at, ",") != 0) return -1;
    if (ConvStreamPutPair(out_ptr, cap, &at, "text_content", msg_ptr->text_ptr) != 0) return -1;
    if (ConvStreamPut(out_ptr, cap, &at, ",") != 0) return -1;
    if (ConvStreamPutPair(out_ptr, cap, &at, "text", msg_ptr->text_ptr) != 0) return -1;
    if (ConvStreamPut(out_ptr, cap, &at, ",") != 0) return -1;
    if (ConvStreamPutPair(out_ptr, cap, &at, "timestamp", msg_ptr->timestamp) != 0) return -1;
    if (ConvStreamPut(out_ptr, cap, &at, ",") != 0) return -1;
    if (ConvStreamPutU64Pair(out_ptr, cap, &at, "timestamp_epoch", msg_ptr->timestamp_epoch) != 0) return -1;
    if (ConvStreamPut(out_ptr, cap, &at, ",") != 0) return -1;
    if (ConvStreamPutPair(out_ptr, cap, &at, "direction", ConvStreamDirectionName(msg_ptr->direction)) != 0) return -1;
    if (ConvStreamPut(out_ptr, cap, &at, ",") != 0) return -1;
    if (ConvStreamPutPair(out_ptr, cap, &at, "cluster_position", ConvStreamClusterName(msg_ptr->cluster)) != 0) return -1;
    if (ConvStreamPut(out_ptr, cap, &at, ",") != 0) return -1;
    if (ConvStreamPutPair(out_ptr, cap, &at, "cluster_mode", ConvStreamClusterName(msg_ptr->cluster)) != 0) return -1;
    if (ConvStreamPut(out_ptr, cap, &at, ",") != 0) return -1;
    if (ConvStreamPutPair(out_ptr, cap, &at, "status", ConvStreamStatusName(msg_ptr->status)) != 0) return -1;
    if (ConvStreamPut(out_ptr, cap, &at, ",") != 0) return -1;
    if (ConvStreamPutPair(out_ptr, cap, &at, "kind", ConvStreamKindName(msg_ptr->kind)) != 0) return -1;
    if (ConvStreamPut(out_ptr, cap, &at, ",") != 0) return -1;

    if (ConvStreamPutKey(out_ptr, cap, &at, "has_reply_context") != 0) return -1;
    if (ConvStreamPut(out_ptr, cap, &at, msg_ptr->has_reply_context ? "true," : "false,") != 0) return -1;
    if (ConvStreamPut(out_ptr, cap, &at, "\"reply_context\":{") != 0) return -1;
    if (ConvStreamPutU64Pair(out_ptr, cap, &at, "reply_msg_id",
                             msg_ptr->has_reply_context ? msg_ptr->reply_msg_id : 0) != 0) return -1;
    if (ConvStreamPut(out_ptr, cap, &at, ",") != 0) return -1;
    if (ConvStreamPutU64Pair(out_ptr, cap, &at, "parent_id",
                             msg_ptr->has_reply_context ? msg_ptr->reply_msg_id : 0) != 0) return -1;
    if (ConvStreamPut(out_ptr, cap, &at, ",") != 0) return -1;
    if (ConvStreamPutPair(out_ptr, cap, &at, "reply_sender",
                          msg_ptr->has_reply_context ? msg_ptr->reply_sender : "") != 0) return -1;
    if (ConvStreamPut(out_ptr, cap, &at, ",") != 0) return -1;
    if (ConvStreamPutPair(out_ptr, cap, &at, "sender",
                          msg_ptr->has_reply_context ? msg_ptr->reply_sender : "") != 0) return -1;
    if (ConvStreamPut(out_ptr, cap, &at, ",") != 0) return -1;
    if (ConvStreamPutPair(out_ptr, cap, &at, "reply_snippet",
                          msg_ptr->has_reply_context ? msg_ptr->reply_snippet_ptr : "") != 0) return -1;
    if (ConvStreamPut(out_ptr, cap, &at, ",") != 0) return -1;
    if (ConvStreamPutPair(out_ptr, cap, &at, "snippet",
                          msg_ptr->has_reply_context ? msg_ptr->reply_snippet_ptr : "") != 0) return -1;
    if (ConvStreamPut(out_ptr, cap, &at, "},\"reactions\":[") != 0) return -1;

    for (r = 0; r < msg_ptr->reaction_count && r < CONV_STREAM_BADGES_MAX; r++) {
        if (r > 0 && ConvStreamPut(out_ptr, cap, &at, ",") != 0) return -1;
        if (ConvStreamPut(out_ptr, cap, &at, "{\"emoji\":") != 0) return -1;
        if (ConvStreamPutStr(out_ptr, cap, &at, msg_ptr->reactions[r].emoji) != 0) return -1;
        if (ConvStreamPut(out_ptr, cap, &at, ",\"count\":") != 0) return -1;
        if (ConvStreamPutInt(out_ptr, cap, &at, msg_ptr->reactions[r].count) != 0) return -1;
        if (ConvStreamPut(out_ptr, cap, &at, "}") != 0) return -1;
    }
    if (ConvStreamPut(out_ptr, cap, &at, "],\"interactivity\":{\"is_collapsed\":") != 0) return -1;
    if (ConvStreamPut(out_ptr, cap, &at, msg_ptr->is_collapsed ? "true}" : "false}") != 0) return -1;

    *at_ptr = at;
    return 0;
}

/** @brief One message, in both spellings, newline included. */
static inline int
ConvStreamBuildMessage(const ConvStreamMessage *msg_ptr, char *out_ptr, size_t cap)
{
    size_t at = 0;

    if (!msg_ptr || !out_ptr || cap == 0) return -1;
    if (ConvStreamPut(out_ptr, cap, &at, "{\"frame_type\":\"message\",") != 0) return -1;
    if (sConvStreamPutMessageFields(out_ptr, cap, &at, msg_ptr) != 0) return -1;
    if (ConvStreamPut(out_ptr, cap, &at, "}\n") != 0) return -1;
    return (int)at;
}

/**
 * @brief The typing row, as its own frame.
 *
 * @param is_typing  Non-zero to raise the row, zero to clear it.
 * @param sender_ptr Who is typing, or NULL when clearing.
 */
static inline int
ConvStreamBuildPresence(int is_typing, const char *sender_ptr, uint64_t epoch, char *out_ptr, size_t cap)
{
    size_t at = 0;

    if (ConvStreamPut(out_ptr, cap, &at, "{\"frame_type\":\"presence\",") != 0) return -1;
    if (ConvStreamPutKey(out_ptr, cap, &at, "is_typing") != 0) return -1;
    if (ConvStreamPut(out_ptr, cap, &at, is_typing ? "true," : "false,") != 0) return -1;
    if (ConvStreamPutPair(out_ptr, cap, &at, "typing_sender", is_typing ? sender_ptr : "") != 0) return -1;
    if (ConvStreamPut(out_ptr, cap, &at, ",") != 0) return -1;
    /* The document's own name for the same value, so a consumer of either spelling
     * raises the row. `presence_update` is carried for the same reason. */
    if (ConvStreamPutPair(out_ptr, cap, &at, "typing_sender_name", is_typing ? sender_ptr : "") != 0) return -1;
    if (ConvStreamPut(out_ptr, cap, &at, ",") != 0) return -1;
    if (ConvStreamPutU64Pair(out_ptr, cap, &at, "timestamp_epoch", epoch) != 0) return -1;
    if (ConvStreamPut(out_ptr, cap, &at, ",\"presence_update\":{\"is_typing\":") != 0) return -1;
    if (ConvStreamPut(out_ptr, cap, &at, is_typing ? "true" : "false") != 0) return -1;
    if (ConvStreamPut(out_ptr, cap, &at, ",\"typing_sender\":") != 0) return -1;
    if (ConvStreamPutStr(out_ptr, cap, &at, is_typing ? sender_ptr : "") != 0) return -1;
    if (ConvStreamPut(out_ptr, cap, &at, "}}\n") != 0) return -1;
    return (int)at;
}

/**
 * @brief One slice of a transfer, as its own frame.
 *
 * @details Carries the message fields as well as the payload, because a client with no row
 *          for this id yet needs everything to make one. The sequence is frames sharing an
 *          id, which the widget cannot render in place: it is append-only, so a frame is
 *          not an update and this is the payload a later gate's mutation seam consumes.
 */
static inline int
ConvStreamBuildMediaProgress(const ConvStreamMessage *msg_ptr, const char *file_name_ptr, int percent, char *out_ptr,
                             size_t cap)
{
    size_t at = 0;

    if (!msg_ptr || !file_name_ptr || !out_ptr || cap == 0) return -1;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    if (ConvStreamPut(out_ptr, cap, &at, "{\"frame_type\":\"media_progress\",") != 0) return -1;
    if (sConvStreamPutMessageFields(out_ptr, cap, &at, msg_ptr) != 0) return -1;
    if (ConvStreamPut(out_ptr, cap, &at, ",\"media_payload\":{\"is_media\":true,\"file_name\":") != 0) return -1;
    if (ConvStreamPutStr(out_ptr, cap, &at, file_name_ptr) != 0) return -1;
    if (ConvStreamPut(out_ptr, cap, &at, ",\"direction\":") != 0) return -1;
    if (ConvStreamPutStr(out_ptr, cap, &at, ConvStreamDirectionName(msg_ptr->direction)) != 0) return -1;
    if (ConvStreamPut(out_ptr, cap, &at, ",\"progress_percent\":") != 0) return -1;
    if (ConvStreamPutInt(out_ptr, cap, &at, percent) != 0) return -1;
    if (ConvStreamPut(out_ptr, cap, &at, "}}\n") != 0) return -1;
    return (int)at;
}

/* ── The control channel ─────────────────────────────────────────────────────────
 *
 * THE ONLY THING A CLIENT MAY SEND. Everything else on this wire is the server talking; a command
 * frame is the client talking back, and without it a person testing a widget has to wait for the
 * generator's cycle to reach the state they wanted to look at. The sibling MSP harness has had this
 * from its first gate -- pause, speed, error injection -- and its absence here was the largest
 * operational gap this track's own delivery record names.
 *
 * ## Why the parser is written out rather than delegated
 *
 * The server links NO json library -- that absence is the assertion README §3 makes, and mjson would
 * end it. So the command grammar is a strict SUBSET that can be parsed completely and defensibly by
 * hand, and every construct outside it is REFUSED rather than tolerated:
 *
 *   - a flat object only: no nested objects or arrays, because a value must be a string or a number
 *     and nothing else is accepted;
 *   - no escape sequences at all, so a backslash anywhere is a refusal -- which is what keeps a
 *     value from being a smuggled quote, and keeps the reader from needing an unescaper;
 *   - bounded everything: at most CONV_STREAM_CMD_PAIRS_MAX pairs, and a key and a value that must
 *     each fit their field or the whole line is refused rather than truncated;
 *   - the line must be a whole object: `{` first, `}` last, nothing after.
 *
 * Every one of those is attacked by dev/probes/conversation_streaming/p_stream_control.c.
 */

#define CONV_STREAM_CMD_KEY_MAX 16
#define CONV_STREAM_CMD_VALUE_MAX 24
#define CONV_STREAM_CMD_PAIRS_MAX 6
/* A command line is not a frame, and it is bounded by the parser's own maxima rather than picked:
 * the widest legal line is
 *
 *   { + 6 x ("key": "value") + 5 commas + }  ->  265 bytes, measured
 *
 * with a full-length key (15) and value (23) in every one of the six pairs. 512 holds that with
 * room to spare. A cap below it would REFUSE a line this parser calls legal, which is the defect
 * the LINE_MAX note above records for frames -- p_stream_control.c asserts the relation so it
 * cannot drift back. */
#define CONV_STREAM_CMD_LINE_MAX 512

/** @brief What a client may ask for. */
typedef enum {
    CONV_STREAM_CMD_UNKNOWN = 0, ///< Refused, and the refusal says so.
    CONV_STREAM_CMD_PAUSE,       ///< Stop emitting. The replay is unaffected.
    CONV_STREAM_CMD_RESUME,      ///< Start again.
    CONV_STREAM_CMD_STEP,        ///< Emit exactly one frame, whether paused or not.
    CONV_STREAM_CMD_SPEED,       ///< `arg` is a percentage of the configured cadence; 400 is 4x.
    CONV_STREAM_CMD_SCENE,       ///< `arg` is a scene index; it takes effect at the next run boundary.
    CONV_STREAM_CMD_STATUS       ///< Change nothing; report the state.
} ConvStreamCommandKind;

/** @brief One key/value pair out of a command line. */
typedef struct ConvStreamPair {
    char key[CONV_STREAM_CMD_KEY_MAX];
    char value[CONV_STREAM_CMD_VALUE_MAX];
    bool numeric; ///< A bare number, so a caller can read it with strtol.
} ConvStreamPair;

/**
 * @brief Parse @p line into flat key/value pairs.
 *
 * @return The pair count, or -1 when the line is not a flat object of string and number pairs --
 *         which includes an unterminated object, a nested one, an escape sequence, a repeated key,
 *         an over-long key or value, and anything after the closing brace.
 */
static inline int
ConvStreamParsePairs(const char *line_ptr, ConvStreamPair *out_ptr, int max)
{
    const char *p = line_ptr;
    int         n = 0;

    if (!line_ptr || !out_ptr || max <= 0) return -1;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '{') return -1;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '}') {
        p++;
        while (*p == ' ' || *p == '\t') p++;
        return *p == '\0' ? 0 : -1;
    }

    for (;;) {
        size_t klen = 0, vlen = 0;

        if (n >= max) return -1;
        if (*p != '"') return -1;
        p++;
        while (*p && *p != '"' && *p != '\\') {
            if (*p < 0x20) return -1;
            if (klen + 1 >= CONV_STREAM_CMD_KEY_MAX) return -1;
            out_ptr[n].key[klen++] = *p++;
        }
        if (*p != '"') return -1; /* unterminated, or an escape we refuse */
        out_ptr[n].key[klen] = '\0';
        p++;
        while (*p == ' ' || *p == '\t') p++;
        if (*p != ':') return -1;
        p++;
        while (*p == ' ' || *p == '\t') p++;

        if (*p == '"') {
            out_ptr[n].numeric = false;
            p++;
            while (*p && *p != '"' && *p != '\\') {
                if (*p < 0x20) return -1;
                if (vlen + 1 >= CONV_STREAM_CMD_VALUE_MAX) return -1;
                out_ptr[n].value[vlen++] = *p++;
            }
            if (*p != '"') return -1;
            p++;
        }
        else {
            const char *start = p;

            out_ptr[n].numeric = true;
            if (*p == '-') p++;
            if (*p < '0' || *p > '9') return -1;
            while (*p >= '0' && *p <= '9') p++;
            if ((size_t)(p - start) >= CONV_STREAM_CMD_VALUE_MAX) return -1;
            while (start < p) out_ptr[n].value[vlen++] = *start++;
        }
        out_ptr[n].value[vlen] = '\0';

        {
            int seen = 0, i;

            for (i = 0; i < n; i++) {
                if (strcmp(out_ptr[i].key, out_ptr[n].key) == 0) seen = 1;
            }
            /* A REPEATED KEY IS REFUSED rather than resolved: which one wins is a parser's private
             * choice, and a command whose meaning depends on that is a command nobody can predict. */
            if (seen) return -1;
        }
        n++;

        while (*p == ' ' || *p == '\t') p++;
        if (*p == ',') {
            p++;
            while (*p == ' ' || *p == '\t') p++;
            continue;
        }
        if (*p == '}') {
            p++;
            while (*p == ' ' || *p == '\t') p++;
            return *p == '\0' ? n : -1;
        }
        return -1;
    }
}

/** @brief The pair named @p key_ptr, or NULL. */
static inline const ConvStreamPair *
ConvStreamPairFind(const ConvStreamPair *pairs_ptr, int count, const char *key_ptr)
{
    int i;

    for (i = 0; i < count; i++) {
        if (strcmp(pairs_ptr[i].key, key_ptr) == 0) return &pairs_ptr[i];
    }
    return NULL;
}

/**
 * @brief Read a command line into its kind and argument.
 *
 * @details A line with no `command` key, an unknown verb, a non-numeric `arg`, or any construct the
 *          pair parser refuses all become @ref CONV_STREAM_CMD_UNKNOWN with @p raw holding what was
 *          written, so the refusal can quote it back.
 */
static inline ConvStreamCommandKind
ConvStreamParseCommand(const char *line_ptr, int *arg_out_ptr, char *raw_ptr, size_t raw_cap)
{
    ConvStreamPair        pairs[CONV_STREAM_CMD_PAIRS_MAX];
    const ConvStreamPair *cmd_ptr;
    const ConvStreamPair *arg_ptr;
    int                   count;
    int                   i;

    static const struct {
        const char          *name_ptr;
        ConvStreamCommandKind kind;
    } kVerbs[] = {{"pause", CONV_STREAM_CMD_PAUSE},
                  {"resume", CONV_STREAM_CMD_RESUME},
                  {"step", CONV_STREAM_CMD_STEP},
                  {"speed", CONV_STREAM_CMD_SPEED},
                  {"scene", CONV_STREAM_CMD_SCENE},
                  {"status", CONV_STREAM_CMD_STATUS}};

    if (arg_out_ptr) *arg_out_ptr = 0;
    if (raw_ptr && raw_cap) raw_ptr[0] = '\0';

    count = ConvStreamParsePairs(line_ptr, pairs, CONV_STREAM_CMD_PAIRS_MAX);
    if (count < 0) return CONV_STREAM_CMD_UNKNOWN;
    cmd_ptr = ConvStreamPairFind(pairs, count, "command");
    if (!cmd_ptr || cmd_ptr->numeric) return CONV_STREAM_CMD_UNKNOWN;
    if (raw_ptr && raw_cap) snprintf(raw_ptr, raw_cap, "%s", cmd_ptr->value);

    for (i = 0; i < (int)(sizeof kVerbs / sizeof kVerbs[0]); i++) {
        if (strcmp(cmd_ptr->value, kVerbs[i].name_ptr) != 0) continue;
        if (kVerbs[i].kind == CONV_STREAM_CMD_SPEED || kVerbs[i].kind == CONV_STREAM_CMD_SCENE) {
            arg_ptr = ConvStreamPairFind(pairs, count, "arg");
            if (!arg_ptr || !arg_ptr->numeric) return CONV_STREAM_CMD_UNKNOWN;
            if (arg_out_ptr) *arg_out_ptr = (int)strtol(arg_ptr->value, NULL, 10);
        }
        return kVerbs[i].kind;
    }
    return CONV_STREAM_CMD_UNKNOWN;
}

/** @brief The generator's state, as a client is told it. */
typedef struct ConvStreamControl {
    const char *event_ptr;   ///< The command as written, or "state" when nothing was asked.
    const char *command_ptr; ///< The verb as parsed, or NULL when the line was refused.
    bool        accepted;    ///< Did the server act on it?
    bool        paused;
    int         speed_pct;   ///< 100 is the configured cadence.
    int         cadence_ms;  ///< What that works out to right now.
    int         next_scene;  ///< The scene the next run will start from.
    const char *next_scene_name_ptr;
    uint64_t    frames;      ///< Frames the generator has emitted since it started.
    const char *detail_ptr;  ///< Why a command was refused, or NULL.
} ConvStreamControl;

/** @brief One control frame: the state after a command, or the state on request. */
static inline int
ConvStreamBuildControl(const ConvStreamControl *ctl_ptr, char *out_ptr, size_t cap)
{
    size_t at = 0;

    if (!ctl_ptr || !out_ptr || cap == 0) return -1;
    if (ConvStreamPut(out_ptr, cap, &at, "{\"frame_type\":\"control\",") != 0) return -1;
    if (ConvStreamPutPair(out_ptr, cap, &at, "event", ctl_ptr->event_ptr ? ctl_ptr->event_ptr : "state") != 0) return -1;
    if (ConvStreamPut(out_ptr, cap, &at, ",") != 0) return -1;
    if (ConvStreamPutPair(out_ptr, cap, &at, "command", ctl_ptr->command_ptr ? ctl_ptr->command_ptr : "") != 0) return -1;
    if (ConvStreamPut(out_ptr, cap, &at, ",\"accepted\":") != 0) return -1;
    if (ConvStreamPut(out_ptr, cap, &at, ctl_ptr->accepted ? "true" : "false") != 0) return -1;
    if (ConvStreamPut(out_ptr, cap, &at, ",\"paused\":") != 0) return -1;
    if (ConvStreamPut(out_ptr, cap, &at, ctl_ptr->paused ? "true" : "false") != 0) return -1;
    if (ConvStreamPut(out_ptr, cap, &at, ",") != 0) return -1;
    if (ConvStreamPutKey(out_ptr, cap, &at, "speed_pct") != 0) return -1;
    if (ConvStreamPutInt(out_ptr, cap, &at, ctl_ptr->speed_pct) != 0) return -1;
    if (ConvStreamPut(out_ptr, cap, &at, ",") != 0) return -1;
    if (ConvStreamPutKey(out_ptr, cap, &at, "cadence_ms") != 0) return -1;
    if (ConvStreamPutInt(out_ptr, cap, &at, ctl_ptr->cadence_ms) != 0) return -1;
    if (ConvStreamPut(out_ptr, cap, &at, ",\"next_scene\":") != 0) return -1;
    if (ConvStreamPutInt(out_ptr, cap, &at, ctl_ptr->next_scene) != 0) return -1;
    if (ConvStreamPut(out_ptr, cap, &at, ",") != 0) return -1;
    if (ConvStreamPutPair(out_ptr, cap, &at, "next_scene_name",
                          ctl_ptr->next_scene_name_ptr ? ctl_ptr->next_scene_name_ptr : "") != 0) return -1;
    if (ConvStreamPut(out_ptr, cap, &at, ",") != 0) return -1;
    if (ConvStreamPutU64Pair(out_ptr, cap, &at, "frames", ctl_ptr->frames) != 0) return -1;
    if (ConvStreamPut(out_ptr, cap, &at, ",") != 0) return -1;
    if (ConvStreamPutPair(out_ptr, cap, &at, "detail", ctl_ptr->detail_ptr ? ctl_ptr->detail_ptr : "") != 0) return -1;
    if (ConvStreamPut(out_ptr, cap, &at, "}\n") != 0) return -1;
    return (int)at;
}

#endif /* CONVERSATION_STREAM_PROTOCOL_H */
