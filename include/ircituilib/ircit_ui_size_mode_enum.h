/**
 * @file ircit_ui_size_mode_enum.h
 * @brief How an @ref IrcitUiDim is interpreted along one axis.
 */
#ifndef IRCITUI_IRCIT_UI_SIZE_MODE_ENUM_H
#define IRCITUI_IRCIT_UI_SIZE_MODE_ENUM_H

/*!
 * @enum IrcitUiSizeMode
 * @brief How @ref IrcitUiDim is interpreted along one axis.
 */
typedef enum {
    IRCITUI_SIZE_EXACT = 0,  ///< Use @ref IrcitUiDim::cells as the size in cells.
    IRCITUI_SIZE_MATCH,      ///< Fill leftover space in the parent.
    IRCITUI_SIZE_WRAP,       ///< Size to content (`wrap_*`).
    IRCITUI_SIZE_PERCENT     ///< Use @ref IrcitUiDim::percent as a share of the
                             ///< space the parent offers along this axis.
} IrcitUiSizeMode;

#endif /* IRCITUI_IRCIT_UI_SIZE_MODE_ENUM_H */
