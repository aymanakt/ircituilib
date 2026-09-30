/**
 * @file ircitui_conversation_message_type.h
 * @brief One message, as the application hands it to the widget.
 */
#ifndef IRCITUI_IRCITUI_CONVERSATION_MESSAGE_TYPE_H
#define IRCITUI_IRCITUI_CONVERSATION_MESSAGE_TYPE_H

#include <stdbool.h>
#include <stdint.h>

#include "ircituilib/ircit_ui_cluster_position_enum.h"
#include "ircituilib/ircit_ui_message_direction_enum.h"
#include "ircituilib/ircit_ui_message_status_enum.h"

/*!
 * @enum IrcitUiMessageKind
 * @brief Whether a message is conversational, or a status indicator.
 *
 * THE CLASS, WHICH DIRECTION DOES NOT CARRY. A status indicator is not a
 * conversational message: it never joins a block and never takes a block's width,
 * it is never truncated, its cells resolve to IRCITUI_TOKEN_STATUS so that a click
 * reaches the application, and the widget's own collapse gesture declines it.
 * `SYSTEM` is the bucket that covers status indicators and is generally an incoming
 * direction; a status indicator on the local side is local, and right-anchored.
 */
typedef enum {
    IRCITUI_MSG_KIND_MESSAGE = 0,      ///< A conversational message: received or local.
    IRCITUI_MSG_KIND_STATUS_INDICATOR  ///< A status indicator: not conversational.
} IrcitUiMessageKind;

/** @brief Bytes reserved for a sender name, including the terminator. */
#define IRCITUI_SENDER_NAME_MAX 32
/** @brief Bytes reserved for a timestamp, including the terminator. */
#define IRCITUI_TIMESTAMP_MAX 16
/** @brief Badges one message may carry. */
#define IRCITUI_REACTION_BADGES_MAX 8
/** @brief Bytes reserved for one reaction glyph, including the terminator: a
 *         4-byte UTF-8 sequence and its NUL, which is what an emoji is. */
#define IRCITUI_REACTION_EMOJI_MAX 8

/*!
 * @struct IrcitUiMessageReaction
 * @brief One emoji and how many people used it.
 */
typedef struct IrcitUiMessageReaction {
    char emoji[IRCITUI_REACTION_EMOJI_MAX]; ///< A UTF-8 sequence, not a byte.
    int  count;                             ///< The tally, reported in the click payload.
} IrcitUiMessageReaction;

/*!
 * @struct IrcitUiConversationMessage
 * @brief One message's semantics, as the application declares them.
 *
 * A STRUCT CONTRACT, not a builder: the application fills this in and hands it
 * over once, and the widget reads it. Nothing here is a live reference -- the
 * widget COPIES the two strings and the caller keeps ownership of what it
 * passed, which is the opposite of IrcitUiWidgetDef's borrowed ids and is
 * deliberate: a message's text arrives from a network reader whose buffer is
 * reused, so borrowing it would be a use-after-free waiting for the second
 * message.
 *
 * WHAT IS ADVISORY. @ref cluster_pos is computed by the widget and the value
 * here is overwritten (see ircit_ui_cluster_position_enum.h). @ref
 * sender_color_tag has no palette slot to land on and is not read at all; the
 * name tag renders in the muted role. Both are carried so a caller ported from
 * the design document compiles, and both are documented rather than silently
 * ignored.
 */
typedef struct IrcitUiConversationMessage {
    uint64_t                 message_id;  ///< Identity: the click payload's and the reply's target.
    char                     sender_name[IRCITUI_SENDER_NAME_MAX];
    char                     timestamp[IRCITUI_TIMESTAMP_MAX];
    char                    *text_content; ///< COPIED on append. The caller keeps its own.
    IrcitUiMessageDirection  direction;
    IrcitUiMessageStatus     status;
    IrcitUiClusterPosition   cluster_pos;  ///< ADVISORY: the widget computes this.
    signed char              sender_color_tag; ///< NOT READ: no slot to land on.

    bool                     has_reply_context;
    uint64_t                 reply_msg_id;
    char                    *reply_snippet; ///< COPIED on append, or NULL.
    char                     reply_sender[IRCITUI_SENDER_NAME_MAX];

    IrcitUiMessageReaction   reactions[IRCITUI_REACTION_BADGES_MAX];
    int                      reaction_count;
    bool                     is_collapsed;
    IrcitUiMessageKind       kind;         ///< Conversational, or a status indicator.
} IrcitUiConversationMessage;

#endif /* IRCITUI_IRCITUI_CONVERSATION_MESSAGE_TYPE_H */
