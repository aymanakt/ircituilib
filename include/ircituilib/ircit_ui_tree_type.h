/**
 * @file ircit_ui_tree_type.h
 * @brief Root handle for a node graph.
 */
#ifndef IRCITUI_IRCIT_UI_TREE_TYPE_H
#define IRCITUI_IRCIT_UI_TREE_TYPE_H

#include "ircituilib/ircit_ui_node_type.h"

/*!
 * @struct IrcitUiTree
 * @brief Root handle for a node graph.
 */
typedef struct {
    IrcitUiNode *root;               ///< Top node, or NULL.
} IrcitUiTree;

#endif /* IRCITUI_IRCIT_UI_TREE_TYPE_H */
