/**
 * @file ircitui_node_pair_type.h
 * @brief One id's node in both trees.
 */
#ifndef IRCITUILIB_IRCITUI_NODE_PAIR_TYPE_H
#define IRCITUILIB_IRCITUI_NODE_PAIR_TYPE_H

#include "ircituilib/ircit_ui_node_type.h"

/*!
 * @struct IrcitUiNodePair
 * @brief One id's node in both trees.
 *
 * Two trees are live at once and they are not the same tree. The model is the
 * tree the framework was fed; the screen tree is the clone of it currently on
 * the glass, and it is the one that receives input. An id therefore names up to
 * two different nodes, and this carries both so a caller does not have to know
 * which tree it will need before it asks.
 *
 * A member is NULL for a genuine absence, not for an error:
 *
 *   both NULL     no node with that id in either tree
 *   model only    the id is in the model but is not on the glass - a template
 *                 whose popup is closed, or any node before IrcitUiScreenReset
 *   screen only   the node was created at run time and has no model entry:
 *                 ctx_menu, or anything IrcitUiAddChild built
 *
 * The two are not interchangeable. A pointer into the screen tree dies when
 * IrcitUiScreenReset replaces that tree or when its popup is popped; a pointer
 * into the model is stable for the life of the process.
 */
typedef struct IrcitUiNodePair {
    IrcitUiNode *model;   ///< The node in the model the framework is driving, or NULL.
    IrcitUiNode *screen;  ///< The node on the glass, or NULL.
} IrcitUiNodePair;

#endif /* IRCITUILIB_IRCITUI_NODE_PAIR_TYPE_H */
