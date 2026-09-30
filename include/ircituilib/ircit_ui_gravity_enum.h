/**
 * @file ircit_ui_gravity_enum.h
 * @brief Alignment of content, or of a child inside leftover space.
 */
#ifndef IRCITUI_IRCIT_UI_GRAVITY_ENUM_H
#define IRCITUI_IRCIT_UI_GRAVITY_ENUM_H

/*!
 * @enum IrcitUiGravity
 * @brief Alignment of content or of a child inside leftover space.
 */
typedef enum {
    IRCITUI_G_START = 0,     ///< Left / top.
    IRCITUI_G_END,           ///< Right.
    IRCITUI_G_TOP,           ///< Top (alias of start on Y).
    IRCITUI_G_BOTTOM,        ///< Bottom.
    IRCITUI_G_CENTER,        ///< Centered.
    IRCITUI_G_FILL           ///< Stretch to the content box.
} IrcitUiGravity;

#endif /* IRCITUI_IRCIT_UI_GRAVITY_ENUM_H */
