/**
 * @file ircit_ui_dirty_list_type.h
 * @brief One frame's change set: the nodes the next pass must revisit.
 */
#ifndef IRCITUI_IRCIT_UI_DIRTY_LIST_TYPE_H
#define IRCITUI_IRCIT_UI_DIRTY_LIST_TYPE_H

#include <stddef.h>
#include "ircituilib/ircit_ui_node_type.h"

/*!
 * @def IRCITUI_DIRTY_LIST_CAPACITY
 * @brief Slots in one frame's change set.
 *
 * Fixed rather than grown on demand, so that the list is a value a caller can
 * hold on the stack and the engine never allocates on the frame path.
 *
 * It lives in this header rather than in @c config_defs.h because it is part of
 * a public type's layout -- a consumer sees it through @c sizeof -- and that
 * header's @c PRIV_ prefix promises its constants are private implementation
 * limits.
 *
 * A frame that fills every slot sets @ref IrcitUiDirtyList.overflow and degrades
 * to a full repaint, so the cap trades work for correctness: it never drops a
 * node that needed revisiting.
 */
#define IRCITUI_DIRTY_LIST_CAPACITY 64

/*!
 * @struct IrcitUiDirtyList
 * @brief The nodes a single frame must paint or refresh.
 *
 * A node is registered when its @c IRCITUI_F_DRAWN is cleared: by an
 * invalidate, by a realize that rebuilt its window, or by a repaint of an
 * ancestor that erased the cells the two share. The draw pass consults the list
 * to decide whether a node that is otherwise clean must still be refreshed,
 * because ncurses resolves stacking by refresh order -- a repaint below a clean
 * node shows through it unless that node is pushed again afterwards.
 *
 * Entries are **borrowed**. The list never owns a node, and a node freed before
 * the frame that would consume it must be forgotten by whoever frees it.
 *
 * @c count is the number of occupied slots, not the capacity. Clearing the list
 * is setting @c count and @c overflow to zero: there is nothing to release and
 * no reason to touch the entries themselves.
 */
typedef struct IrcitUiDirtyList {
    IrcitUiNode *entries[IRCITUI_DIRTY_LIST_CAPACITY]; ///< Nodes changed this
                                                       ///< frame, in the order
                                                       ///< they were registered.
    size_t       count;    ///< Occupied slots; 0 when the frame is clean.
    int          overflow; ///< Non-zero once a registration had to be dropped,
                           ///< which degrades the frame to a full repaint.
} IrcitUiDirtyList;

#endif /* IRCITUI_IRCIT_UI_DIRTY_LIST_TYPE_H */
