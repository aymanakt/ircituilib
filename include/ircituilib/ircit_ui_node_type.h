/**
 * @file ircit_ui_node_type.h
 * @brief The node handle, opaque to callers.
 */
#ifndef IRCITUI_IRCIT_UI_NODE_TYPE_H
#define IRCITUI_IRCIT_UI_NODE_TYPE_H

/*!
 * @struct IrcitUiNode
 * @brief One node in the reference or screen tree.
 *
 * Opaque by design: the members live in the library's private header, so a
 * caller holds a pointer and never reaches inside. The struct is named, rather
 * than left anonymous, so that a callback can take @c struct @c IrcitUiNode *
 * without a second typedef in scope.
 */
typedef struct IrcitUiNode IrcitUiNode;

#endif /* IRCITUI_IRCIT_UI_NODE_TYPE_H */
