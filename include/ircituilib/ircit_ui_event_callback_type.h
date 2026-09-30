/**
 * @file ircit_ui_event_callback_type.h
 * @brief Lifecycle hook with no extra payload.
 */
#ifndef IRCITUI_IRCIT_UI_EVENT_CALLBACK_TYPE_H
#define IRCITUI_IRCIT_UI_EVENT_CALLBACK_TYPE_H

#include "ircituilib/ircit_ui_node_type.h"

/**
 * @brief Lifecycle hook with no extra payload.
 * @param n node that reached the lifecycle point
 */
typedef void (*IrcitUiEventCallback)(struct IrcitUiNode *n);

#endif /* IRCITUI_IRCIT_UI_EVENT_CALLBACK_TYPE_H */
