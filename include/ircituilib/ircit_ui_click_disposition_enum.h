/**
 * @file ircit_ui_click_disposition_enum.h
 * @brief What the application decided an activation should do.
 */
#ifndef IRCITUI_IRCIT_UI_CLICK_DISPOSITION_ENUM_H
#define IRCITUI_IRCIT_UI_CLICK_DISPOSITION_ENUM_H

/*!
 * @enum IrcitUiClickDisposition
 * @brief The fate an application registers for an activation.
 *
 * Returned by @ref IrcitUiClickCallback and read by the engine before it takes
 * any action of its own. The value answers exactly one question: may the engine
 * go on and do what it would otherwise have done with this activation?
 *
 * The engine's default action is not one step but several — it moves focus,
 * toggles @ref IRCITUI_F_CHECKED on the state widgets, calls the node's own
 * @ref IrcitUiNode::on_input, writes the status echo, and dismisses the menu
 * layer. @ref IRCITUI_CLICK_SINK is consulted before the first of them, so a
 * refusal is total rather than partial: there is no ordering in which a refused
 * activation leaves focus moved and the panel unopened.
 */
typedef enum {
    IRCITUI_CLICK_CARRY = 0, ///< Hand the activation back: the engine proceeds exactly as if no callback had been registered for this node.
    IRCITUI_CLICK_SINK  = 1  ///< Take the activation out of the engine's hands. Focus, the checked toggle, the node's own @ref IrcitUiNode::on_input, the status echo and the menu dismissal all do not happen.
} IrcitUiClickDisposition;

#endif /* IRCITUI_IRCIT_UI_CLICK_DISPOSITION_ENUM_H */
