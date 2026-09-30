/**
 * @file ircit_ui_message_direction_enum.h
 * @brief Which side of the conversation a message belongs to.
 */
#ifndef IRCITUI_IRCIT_UI_MESSAGE_DIRECTION_ENUM_H
#define IRCITUI_IRCIT_UI_MESSAGE_DIRECTION_ENUM_H

/*!
 * @enum IrcitUiMessageDirection
 * @brief The message's side, which is the whole of the asymmetry: an outbound
 *        bubble anchors right and an inbound one left, and a system line is
 *        centred in neither.
 *
 * The order is the ABI and is also the order the cluster algorithm walks, so a
 * new direction is appended.
 */
typedef enum {
    IRCITUI_MSG_INBOUND = 0, ///< From someone else: anchored left.
    IRCITUI_MSG_OUTBOUND,    ///< From the user: anchored right.
    IRCITUI_MSG_SYSTEM       ///< Neither: a centred, borderless notice.
} IrcitUiMessageDirection;

#endif /* IRCITUI_IRCIT_UI_MESSAGE_DIRECTION_ENUM_H */
