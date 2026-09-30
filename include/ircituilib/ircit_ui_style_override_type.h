/**
 * @file ircit_ui_style_override_type.h
 * @brief A partial colour assignment: which attributes to set, and to what.
 */
#ifndef IRCITUI_IRCIT_UI_STYLE_OVERRIDE_TYPE_H
#define IRCITUI_IRCIT_UI_STYLE_OVERRIDE_TYPE_H

/*!
 * @brief A partial colour assignment.
 *
 * Each attribute carries its own `set_` flag, so an override can change one
 * colour and leave the other two as the node already had them. That is what
 * lets a rule be written as a delta rather than a full specification, and it
 * is why the `set_` flags must be tested rather than the colour values: 0 is
 * a legal colour (black) and cannot double as "unset".
 */
typedef struct {
    int   set_fg;   ///< Non-zero to assign @ref fg.
    int   set_bg;   ///< Non-zero to assign @ref bg.
    int   set_bdr;  ///< Non-zero to assign @ref bdr.
    short fg;       ///< Foreground colour 0-7, used when @ref set_fg is non-zero.
    short bg;       ///< Background colour 0-7, used when @ref set_bg is non-zero.
    short bdr;      ///< Border colour 0-7, used when @ref set_bdr is non-zero.
} IrcitUiStyleOverride;

#endif /* IRCITUI_IRCIT_UI_STYLE_OVERRIDE_TYPE_H */
