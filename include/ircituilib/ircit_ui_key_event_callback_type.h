/**
 * @file ircit_ui_key_event_callback_type.h
 * @brief Input hook.
 */
#ifndef IRCITUI_IRCIT_UI_KEY_EVENT_CALLBACK_TYPE_H
#define IRCITUI_IRCIT_UI_KEY_EVENT_CALLBACK_TYPE_H

#include "ircituilib/ircit_ui_node_type.h"

/**
 * @brief Input hook.
 * @param n   widget
 * @param key ncurses key or KEY_MOUSE
 */
typedef void (*IrcitUiKeyEventCallback)(struct IrcitUiNode *n, int key);

#endif /* IRCITUI_IRCIT_UI_KEY_EVENT_CALLBACK_TYPE_H */
