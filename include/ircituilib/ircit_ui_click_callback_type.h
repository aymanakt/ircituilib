/**
 * @file ircit_ui_click_callback_type.h
 * @brief Application hook for an activation, and its verdict.
 */
#ifndef IRCITUI_IRCIT_UI_CLICK_CALLBACK_TYPE_H
#define IRCITUI_IRCIT_UI_CLICK_CALLBACK_TYPE_H

#include "ircituilib/ircit_ui_click_disposition_enum.h"
#include "ircituilib/ircit_ui_node_type.h"

/**
 * @brief Application policy for an activation.
 *
 * Called when a node the application has registered is activated, before the
 * engine acts on it. The return value decides whether it does.
 *
 * The node passed is the live one that was activated, not the reference node
 * the registration named: when a popup is inflated the node is a fresh clone,
 * so a handler that receives the live pointer can read and paint the node the
 * user actually clicked. Registering by id is what makes that work — every live
 * copy of an id routes to the same registered callback.
 *
 * Runs on the engine's thread, inside dispatch. It may not block, and it may
 * create or destroy nodes: a callback that refuses an action and posts a
 * floating window saying so is the intended use, not an abuse.
 *
 * @param node_ptr  The live node that was activated. Never NULL.
 * @param user_ptr  The pointer registered alongside this callback; NULL when
 *                  the registration named none.
 *
 * @return @ref IRCITUI_CLICK_CARRY to let the engine proceed, or
 *         @ref IRCITUI_CLICK_SINK to stop it.
 */
typedef IrcitUiClickDisposition (*IrcitUiClickCallback)(struct IrcitUiNode *node_ptr, void *user_ptr);

#endif /* IRCITUI_IRCIT_UI_CLICK_CALLBACK_TYPE_H */
