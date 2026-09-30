/**
 * @file ircit_ui_dim_type.h
 * @brief One-axis layout request.
 */
#ifndef IRCITUI_IRCIT_UI_DIM_TYPE_H
#define IRCITUI_IRCIT_UI_DIM_TYPE_H

#include "ircituilib/ircit_ui_size_mode_enum.h"

/*!
 * @struct IrcitUiDim
 * @brief One-axis layout request.
 *
 * @c cells and @c percent are alternatives, not two halves of one value: the
 * member that applies is the one @ref mode selects, and the other is unread.
 * They are separate members rather than one overloaded count so that a dim
 * built for one mode cannot be misread as the other's operand.
 */
typedef struct IrcitUiDim {
    IrcitUiSizeMode mode;    ///< EXACT, MATCH, WRAP, or PERCENT.
    int             cells;   ///< Cell count when mode is EXACT.
    int             percent; ///< Share of the parent's offer when mode is
                             ///< PERCENT; 0-100.
} IrcitUiDim;

#endif /* IRCITUI_IRCIT_UI_DIM_TYPE_H */
