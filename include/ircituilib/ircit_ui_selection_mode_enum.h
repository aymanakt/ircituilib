/**
 * @file ircit_ui_selection_mode_enum.h
 * @brief How many rows of a recycler may be marked at once.
 */
#ifndef IRCITUI_IRCIT_UI_SELECTION_MODE_ENUM_H
#define IRCITUI_IRCIT_UI_SELECTION_MODE_ENUM_H

/*!
 * @enum IrcitUiSelectionMode
 * @brief How many rows of a recycler may be marked at once.
 *
 * @details The mode governs what the selection *is*, not merely how large it may
 *          grow.
 *
 *          Under @ref IRCITUI_SELECT_SINGLE the selection follows the cursor: the
 *          marked row is wherever the cursor is, so an arrow key moves the
 *          highlight, which is what a list means by "up and down". Marking is not
 *          a toggle in this mode -- a second click on the marked row leaves it
 *          marked, because the cursor is on it and the cursor is the selection.
 *          To end up with nothing marked, clear the selection programmatically.
 *
 *          Under @ref IRCITUI_SELECT_MULTI the two are independent. The cursor
 *          says where you are, the marks say what you chose, and a click or
 *          @c SPACE toggles the row under the cursor so a set can be built. That
 *          independence is why a multi-select needs the cursor drawn distinctly
 *          from the highlight.
 */
typedef enum {
    /*! One row at a time: marking a row clears the rest. **This is the zero
     *  value deliberately.** A state that was never configured must select one
     *  row at a time, and a hand-built one is calloc'd -- so putting NONE at 0
     *  would make every such state silently refuse to mark anything, which is
     *  the kind of default that looks like a broken list rather than a setting.
     *  With SINGLE at 0, a zeroed state and the framework's default agree. */
    IRCITUI_SELECT_SINGLE = 0,
    IRCITUI_SELECT_MULTI,     ///< Any number of rows, which is what SS17 snapshots.
    IRCITUI_SELECT_NONE       ///< Marking is refused; the selection stays empty.
} IrcitUiSelectionMode;

#endif /* IRCITUI_IRCIT_UI_SELECTION_MODE_ENUM_H */
