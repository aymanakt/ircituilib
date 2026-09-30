/**
 * @file ircit_ui_message_status_enum.h
 * @brief How far an outbound message has got.
 */
#ifndef IRCITUI_IRCIT_UI_MESSAGE_STATUS_ENUM_H
#define IRCITUI_IRCIT_UI_MESSAGE_STATUS_ENUM_H

/*!
 * @enum IrcitUiMessageStatus
 * @brief The delivery mark, and for one value a change of appearance.
 */
typedef enum {
    IRCITUI_STATUS_SENDING = 0, ///< A hollow circle.
    IRCITUI_STATUS_DELIVERED,   ///< One check.
    IRCITUI_STATUS_READ,        ///< Two checks.
    IRCITUI_STATUS_EXPIRED      ///< Drawn dimmed: the message is going or gone.
} IrcitUiMessageStatus;

#endif /* IRCITUI_IRCIT_UI_MESSAGE_STATUS_ENUM_H */
