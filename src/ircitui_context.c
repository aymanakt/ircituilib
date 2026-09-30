/**
 * @file ircitui_context.c
 * @brief ircitui_context
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


#include "ircitui_context_type_priv.h"
#include "ircituilib/ircitui_context.h"
#include "ircituilib/ui.h"
#include <ufsrvrxlib/ufsrv_scheduler/ufsrv_scheduler.h>
#include <uflib/cdt/lockless_ringbuffer/cdt_lockless_ringbuffer.h>

#include "ircitui_window_driver_type_priv.h"
#include "ui_event_dispatcher_priv.h"
#include "ui_log.h"
#include "ui_snackbar_priv.h"

static IrcitUiContext ircitui_context;

IrcitUiContext *
IrcitUiInitialiseFrameworkContext(IrcitUiNodeTreeBinder node_tree_binder, IrcitUiWindowRegistry *window_registry)
{
  ircitui_context.ircitui_main_thread_id = pthread_self();

  ircitui_context.window_registry = window_registry;
  if (window_registry) {
    IrcitUiWindowRegistryMarshalUserCallbackOnInitialised(window_registry);
  }

  // The binder is where an application hands over the display model it wants driven, carrying the listeners that bind its domain to this framework's data and interaction requirements.
  ircitui_context.ircitui_model = node_tree_binder();

  // The on-glass handle
  ircitui_context.ircitui_screen = &IrcitUiScreen;

  UfsrvSchedulerPool *ircitui_scheduler_ptr = UfsrvSchedulerPoolCreate(2, "ui_scheduler");
  UfsrvSchedulerPoolStart(ircitui_scheduler_ptr);
  ircitui_context.ircitui_scheduler_pool = ircitui_scheduler_ptr;

  ircitui_context.input_queue     = LocklessRingBufferCreate(1024, sizeof(void *), LOCKLESS_RINGBUF_MODE_MPSC);

  ircitui_context.ircitui_event_dispatcher = IrcitUiEventDispatcherCreate();
  if (!ircitui_context.ircitui_event_dispatcher) return NULL;

  // Configure space for 1024 flat contiguous value objects
  ircitui_context.ircitui_event_dispatcher->async_response_queue =
      LocklessRingBufferCreate(1024, sizeof(MainLoopMessage), LOCKLESS_RINGBUF_MODE_MPSC);

  if (!ircitui_context.ircitui_event_dispatcher->async_response_queue) {
    IRCITUI_LOG_ERROR("Failed to allocate high-performance lockless communications pipeline.");
    return NULL;
  }

  return &ircitui_context;
}

void
IrcitUiDestroyFrameworkContext(IrcitUiContext *context_ptr)
{
  if (!context_ptr) return;

  IrcitUiEventDispatcherDestroy(context_ptr->ircitui_event_dispatcher);
  context_ptr->ircitui_event_dispatcher = NULL;

  UfsrvSchedulerPoolDestroy(context_ptr->ircitui_scheduler_pool);
  context_ptr->ircitui_scheduler_pool = NULL;

  LocklessRingBufferDestroy(context_ptr->input_queue);
  context_ptr->input_queue = NULL;

  IrcitUiSnackbarDestroy();

}

UfsrvSchedulerPool *
IrcitUiScheduler(void)
{
  return ircitui_context.ircitui_scheduler_pool;
}

IrcitUiDataBindDescriptor *
IrcitUiModel(void)
{
  return ircitui_context.ircitui_model;
}

IrcitUiNode *
IrcitUiModelRoot(void)
{
  return ircitui_context.ircitui_model->root;
}

IrcitUiTree *
IrcitUiScreenTree(void)
{
  return ircitui_context.ircitui_screen;
}

const IrcitUiWindowDriver *
IrcitUiContextWindowDriver(void)
{
  return IrcitUiWindowRegistryGetActiveDriver(ircitui_context.window_registry);
}

IrcitUiDirtyList *
IrcitUiFrameDirtyList(void)
{
  return &ircitui_context.frame_dirty_list;
}

bool
IsOnMainThreadUi(void)
{
  return pthread_equal(pthread_self(), ircitui_context.ircitui_main_thread_id);
}

/**
 * Certain graph mutations must be performed on the main ui and this guard violently enforces that.
 */
void
AssertIsOnMainThreadUi(void)
{
  if (!IsOnMainThreadUi()) {
    IRCITUI_LOG_ERROR("[FATAL THREAD VIOLATION] Non-UI thread attempted to mutate framework graphs directly. Execution halted."); \
    abort();
  }
}
