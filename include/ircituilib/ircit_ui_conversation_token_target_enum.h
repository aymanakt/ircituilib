/**
 * @file ircit_ui_conversation_token_target_enum.h
 * @brief What a cell inside a conversation bubble belongs to.
 */
#ifndef IRCITUI_IRCIT_UI_CONVERSATION_TOKEN_TARGET_ENUM_H
#define IRCITUI_IRCIT_UI_CONVERSATION_TOKEN_TARGET_ENUM_H

/*!
 * @enum IrcitUiConversationTokenTarget
 * @brief What a cell inside a conversation bubble belongs to.
 *
 * THE UNIT OF A HIT TEST, and it exists because a message is not one target. A
 * bubble carries a name tag, an optional reply header, its body, an optional
 * badge row and its rules, all stacked at heights the model computes; a router
 * that answers only "which message" cannot tell a click on a quote from a click
 * on the text, and the two want different things done.
 *
 * EVERY VALUE IS REACHABLE. That is a requirement rather than a hope: an
 * enumerator a router can never return is a dead value that reads as a feature,
 * which is the defect the design document's own click-target enum was refused
 * for. Each of these is returned by @ref IrcitUiConversationWindowResolveTokenAt
 * for a cell the painter actually drew, and the paint-versus-hit-test sweep walks
 * every cell of every bubble in the fixture to say so.
 */
typedef enum {
    IRCITUI_TOKEN_NONE = 0,  ///< No bubble claims this cell: padding, the gutter,
                             ///< the timestamps margin, or past the last message.
                             ///< Clicking here must do NOTHING, which is why it is
                             ///< a value of its own rather than a fallback to the
                             ///< nearest thing.
    IRCITUI_TOKEN_BODY,      ///< The message's own text, or a rule of its box.
                             ///< The bubble's frame belongs to the bubble: a click
                             ///< on its edge is a click on the message.
    IRCITUI_TOKEN_REPLY,     ///< The quoted original inside the bubble, at the
                             ///< top of it. A different gesture from the body's,
                             ///< which is the whole reason it is told apart.
    IRCITUI_TOKEN_BADGE,     ///< The reaction badges, fused to the sender's edge.
                             ///< Its own target because a badge is an action on the
                             ///< message rather than a selection of it.
    IRCITUI_TOKEN_STATUS,    ///< A status indicator: a notice, or a local indicator
                             ///< such as an acknowledgement. Its own target because
                             ///< the widget has no gesture for it -- a status
                             ///< indicator has no collapse state -- so the click is
                             ///< the application's, and the message id comes back
                             ///< with it. Never reported for a conversational
                             ///< message, whose cells are BODY, REPLY, BADGE or
                             ///< NAME_TAG.
    IRCITUI_TOKEN_NAME_TAG,  ///< The sender's name, above a run's first inbound
                             ///< bubble. Reported rather than folded into the body
                             ///< because it is a cell the painter draws and the
                             ///< sweep must therefore account for it -- a target
                             ///< the router cannot name is a cell it cannot be
                             ///< checked against.
    IRCITUI_TOKEN_AVATAR     ///< The sender's avatar, in the gutter left of a block's
                             ///< first inbound bubble. The widget declines it (R28's
                             ///< pattern): its gesture is collapse, and a tap on an
                             ///< avatar is the application's.
} IrcitUiConversationTokenTarget;

#endif /* IRCITUI_IRCIT_UI_CONVERSATION_TOKEN_TARGET_ENUM_H */
