/**
 * @file ui_async_bridge.h
 * @brief The bridge from a ufsrvrxlib future's resolution onto Thread 0.
 *
 * Copyright (C) 2015-2026  unfacd works
 *
 * This file is part of ircituilib source code.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * A background worker must never touch a node. The tree it would reach is
 * owned by the loop's thread, and a worker that wrote to it would race every
 * paint and every layout pass that the same thread is running. What a worker
 * may do is hand a value over and ring the doorbell, which is what this is:
 * IrcitUiPostFutureContinuation is registered as the terminal continuation of a
 * future, runs on whichever thread completed it, and does nothing but copy the
 * result into a fixed-size envelope and post it. Thread 0 reads that envelope
 * on its next pass and applies it there, where the tree is safe to touch.
 *
 * The result is BORROWED. UfsrvFutureCallback's contract is explicit that the
 * future still owns the value, so nothing here takes ownership of it, frees it,
 * or modifies it - the string is copied out and the original is left alone.
 *
 * Private to the library: the envelope it posts is a private type and the
 * dispatcher it posts through is a private one, so this header is not installed.
 */
#ifndef IRCITUILIB_UI_ASYNC_BRIDGE_H
#define IRCITUILIB_UI_ASYNC_BRIDGE_H

/* The project's own headers first, then the ufsrv stack's. uflib defines
   PUBLIC_API as a visibility attribute and ircituilib_defs.h defines it empty,
   so reaching uflib first is a -Wmacro-redefined error under -Werror. */
#include <ircituilib/ircit_ui_event_dispatcher_type.h>
#include <ufsrvrxlib/ufsrv_future/ufsrv_future.h>

#include "ircitui_event_dispatcher_type_priv.h"

/**
 * @brief What a future's resolution needs in order to land on Thread 0.
 *
 * Created by whoever registers the continuation and released by the
 * continuation itself once the envelope is posted - a terminal future
 * completes exactly once, so the context has exactly one reader and one
 * lifetime.
 *
 * @warning If the future is cancelled or released before it completes, the
 *          continuation never runs and this context is never freed. A caller
 *          that can abandon a future owns the disposal of its context.
 */
typedef struct
{
  IrcitUiEventDispatcher *dispatcher;    ///< The pipeline to post through.
  IrcitUiNode *           target_node;   ///< The node the result belongs to.
  /**
   * @brief The target's id, captured while the node is still certainly alive.
   *
   * On the failure path the result is reported by id rather than by node,
   * because the node may be gone by the time Thread 0 reads the envelope - a
   * worker's result can outlive the widget it was fetched for, and nothing on
   * the receiving side can tell a freed node from a live one. The id string
   * can: nodes borrow it from the generated table (see common.text), so it
   * outlives every clone, which is the same property IrcitUiCloseTopPopup
   * relies on. May be NULL.
   */
  const char *            target_id;
  /**
   * @brief What Thread 0 should do with a successful result.
   *
   * One of the IRCITUI_BRIDGE_* values. The producer does not interpret the
   * payload either way - it copies a string across and names the discriminant -
   * so the choice is the caller's, made here rather than guessed downstream.
   */
  int                     kind;
  void *                  user_context;  ///< The caller's own, never touched here.
} IrcitUiBridgeContext;

/** @brief Deliver a successful result as text into the target node. */
#define IRCITUI_BRIDGE_TEXT  0
/** @brief Deliver a successful result as the path of an image to show. */
#define IRCITUI_BRIDGE_IMAGE 1

/**
 * @brief Terminal future continuation: copies a result into the pipeline.
 *
 * Registered with UfsrvFutureThen. Runs on the thread that completed the
 * future, which is a worker, so it touches nothing but its own stack and the
 * lock-free ring. It does not modify the result, does not take its value, and
 * does not touch the target node.
 *
 * @param result_ptr  Borrowed result. NULL is tolerated and reported as failure.
 * @param context_ptr An IrcitUiBridgeContext, released by this call.
 */
void IrcitUiPostFutureContinuation(UfsrvFutureResult *result_ptr, void *context_ptr);

/**
 * @brief Shows a delivered image result as a floating window. Thread 0 only.
 *
 * The consumer side of an IRCITUI_BRIDGE_IMAGE delivery. It reads the file the
 * worker wrote and raises a window over the layout showing it, and it must be
 * called from the thread that owns the tree - a worker calling this would be
 * reaching the nodes directly, which is the thing the bridge exists to prevent.
 *
 * @param path_ptr Path to a PNG. Ignored if NULL or empty.
 */
void IrcitUiBridgeShowImageResult(const char *path_ptr);

#endif // IRCITUILIB_UI_ASYNC_BRIDGE_H
