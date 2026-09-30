//
// Created by ayman on 15/09/2026.
//

/**
 * @file ircitui_event_type_priv.h
 * @brief ircitui_event_type_priv
 *
 * Copyright (C) 2015-2026  unfacd works
 *
 * This file is part of ircituilib source code.
 * Created by ayman on 15/09/2026.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 */

#ifndef IRCITUILIB_IRCITUI_EVENT_TYPE_PRIV_H
#define IRCITUILIB_IRCITUI_EVENT_TYPE_PRIV_H

#include "ircituilib/ircit_ui_node_type.h"
#include "ircituilib/ircit_ui_event_type_enum.h"

/* The event capsule the dispatcher hands to a listener.
 *
 * The body is here rather than left to a forward declaration because the
 * staging ring below holds events by value, and an array of incomplete type
 * cannot be declared - so the layout has to be complete before the dispatcher
 * that queues it. A caller still cannot see it: this header is not installed,
 * and the public typedef in ircit_ui_event_type.h stays a forward declaration.
 *
 * One capsule serves input, layout and lifecycle, so `type` is what decides
 * which of the payload fields below are meaningful. The rest are left at zero,
 * which is what the accessors report for a tag that does not carry them. */
struct IrcitUiEvent
{
  IrcitUiEventType type;
  IrcitUiNode *    target;       /**< Node the event resolved to, or NULL. */
  int              mouse_x;      /**< Click column, in screen cells. */
  int              mouse_y;      /**< Click row, in screen rows. */
  int              key_code;     /**< Key or sequence, as ncurses reports it. */
  int              scroll_delta; /**< Wheel notches; negative is up. */
};

#endif //IRCITUILIB_IRCITUI_EVENT_TYPE_PRIV_H
