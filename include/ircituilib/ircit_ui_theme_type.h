/**
 * @file ircit_ui_theme_type.h
 * @brief The theme handle, opaque to callers.
 */
#ifndef IRCITUI_IRCIT_UI_THEME_TYPE_H
#define IRCITUI_IRCIT_UI_THEME_TYPE_H

/*!
 * @struct IrcitUiTheme
 * @brief Opaque theme object: a base style plus an ordered rule list.
 *
 * Opaque by design, like @ref IrcitUiNode: the rule array and its capacity
 * live in the library's private header, so a caller holds a pointer and never
 * reaches inside. Rules are added through @ref IrcitUiThemeAddRule and
 * resolved through @ref IrcitUiThemeApply.
 */
typedef struct IrcitUiTheme IrcitUiTheme;

#endif /* IRCITUI_IRCIT_UI_THEME_TYPE_H */
