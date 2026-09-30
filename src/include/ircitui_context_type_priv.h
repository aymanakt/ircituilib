/**
 * @file ircitui_context_type_priv.h
 * @brief The private definition of IrcitUiContext.
 *
 * The public ircitui_context_type.h forward-declares the struct so that a
 * consumer can hold a pointer to it; the members live here, and only the
 * library's own translation units include this header.
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

#ifndef IRCITUILIB_IRCITUI_CONTEXT_TYPE_PRIV_H
#define IRCITUILIB_IRCITUI_CONTEXT_TYPE_PRIV_H

#include <pthread.h>
#include "ircituilib/ircit_ui_tree_type.h"
#include "ircituilib/ircit_ui_dirty_list_type.h"
#include "ircituilib/ircit_ui_event_dispatcher_type.h"
#include "ircituilib/ircitui_data_bind_descriptor_type.h"
#include <ufsrvrxlib/ufsrv_scheduler/ufsrv_scheduler_type.h>
#include <uflib/cdt/lockless_ringbuffer/cdt_lockless_ringbuffer_type.h>

#include "ircituilib/window_driver/ircitui_window_registry.h"
#include "ircituilib/window_driver/ircitui_window_type.h"

typedef struct IrcitUiContext IrcitUiContext;

typedef LocklessRingBuffer IrcitUiEventQueue;

struct IrcitUiContext
{
  pthread_t                  ircitui_main_thread_id;
  IrcitUiWindowRegistry *    window_registry;
  IrcitUiEventDispatcher *   ircitui_event_dispatcher;
  UfsrvSchedulerPool *       ircitui_scheduler_pool;
  IrcitUiEventQueue *        input_queue; ///< LOSS-PREVENTION FIFO INPUT PIPELINE
  IrcitUiDataBindDescriptor *ircitui_model;
  ///< The display model the binder
       ///< supplied, which is what the framework drives. Set once by
       ///< IrcitUiInitialiseFrameworkContext and reached afterwards through
       ///< IrcitUiModel. NULL before that call, and not defended against: the
       ///< binder is the application's half of the contract.
  IrcitUiTree *              ircitui_screen; ///< Current node hierarchy on-glass

  /* The frame's change set: the nodes whose paint cache was invalidated since
     the last pass, in registration order. It is the one piece of render state
     that outlives a single call, because the invalidation and the draw that
     consumes it happen at different points in the loop -- an event mutates and
     registers, the frame pass reads and clears.

     Reached through IrcitUiFrameDirtyList rather than read directly: the
     invalidators are public and take no context, so the accessor is how they
     reach the single instance. See IrcitUiDirtyList for what the entries mean
     and who owns them. */
  IrcitUiDirtyList frame_dirty_list;

  struct
  {
    int is_dimmed; ///< Global state
    //todo we need a reference to modal node
  } screen_context;
};
#endif //IRCITUILIB_IRCITUI_CONTEXT_TYPE_PRIV_H
