/**
 * @file ircitui_livedata.h
 * @brief LiveData: the observer container the host application publishes state through.
 *
 * Copyright (C) 2015-2026  unfacd works
 *
 * This file is part of ircituilib source code.
 * Created by ayman on 27/09/2026.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 */

#ifndef IRCITUILIB_IRCITUI_LIVEDATA_H
#define IRCITUILIB_IRCITUI_LIVEDATA_H

#include "ircituilib/ircituilib_defs.h"
#include "ircituilib/ircit_ui_node_type.h"
#include "ircituilib/livedata/ircitui_livedata_type.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
   CONSTRUCTION AND TEARDOWN
   ========================================================================= */

/**
 * @brief Allocates a LiveData container that queues a message for every post.
 * @param initial_value The payload observers receive on their first delivery. May be NULL.
 * @return A new opaque handle, or NULL if the allocation failed.
 *
 * The default policy: one dispatch per post, in order, so the observer's call count follows the
 * post count. @ref IrcitUiLiveDataConflatedCreate is the lossy alternative.
 */
PUBLIC_API IrcitUiLiveData *IrcitUiLiveDataCreate(void *initial_value);

/**
 * @brief Allocates a LiveData container that queues at most one undispatched message, so a
 *        burst of rapid posts collapses and the observer sees only the newest value.
 * @param initial_value The payload observers receive on their first delivery. May be NULL.
 * @return A new opaque handle, or NULL if the allocation failed.
 *
 * A post arriving while a message is already queued overwrites the value slot and returns
 * without queueing and without allocating. Observers therefore run once per burst, not once
 * per post. The policy is fixed for the container's lifetime; a container from
 * @ref IrcitUiLiveDataMap inherits its source's.
 *
 * Overwritten payloads are unreachable to the framework, so confining the loss is the caller's
 * to manage.
 *
 * @warning The payload must stay immutable from publish to consume. Conflation widens that
 *          window from one looper hop to the length of a burst.
 * @warning A producer that allocates per update must free the conflated-away payloads itself,
 *          or publish from an arena or a recycling pool.
 * @warning Not for a payload that denotes an event — a keystroke, a protocol frame, a log line.
 *          Coalescing it away is loss the framework cannot report. Not for an observer that
 *          accumulates, whose call count would then follow looper load rather than input.
 * @note A post the ring refuses is lost rather than retried. The claim and the block are
 *       released so the container keeps notifying, but that update is gone. The guarantee is
 *       "the newest value is delivered at the next successful post", not "every value is
 *       delivered".
 */
PUBLIC_API IrcitUiLiveData *IrcitUiLiveDataConflatedCreate(void *initial_value);

/**
 * @brief Releases the container and every observer still attached to it.
 * @param live_data The handle returned by IrcitUiLiveDataCreate. NULL is a silent no-op.
 * @note Payloads are not released: the host owns what it published.
 */
PUBLIC_API void IrcitUiLiveDataDestroy(IrcitUiLiveData *live_data);

/* =========================================================================
   PUBLICATION: SYNCHRONOUS (Thread 0) AND ASYNCHRONOUS (any thread)
   ========================================================================= */

/**
 * @brief Immediately updates state synchronously.
 * @param live_data The container to publish through. NULL is a silent no-op.
 * @param value The new payload; the container stores the pointer, never a copy.
 * @note Main Thread 0 only, and enforced: AssertIsOnMainThreadUi aborts the process otherwise.
 *       Unlike a post, this path walks the observer chain and invokes every block on the
 *       caller's thread.
 */
PUBLIC_API void IrcitUiLiveDataSetValue(IrcitUiLiveData *live_data, void *value);

/**
 * @brief Queues a state update from any thread, for delivery on Main Thread 0.
 * @param live_data The container to publish through. NULL is a silent no-op.
 * @param value The new payload; the container stores the pointer, never a copy.
 *
 * The value is stored on the calling thread. No observer runs there: the message is queued and
 * the blocks run when the looper drains it.
 */
PUBLIC_API void IrcitUiLiveDataPostValue(IrcitUiLiveData *live_data, void *value);

/* =========================================================================
   FILTER-SUPPORTING OBSERVATION EXTENSION (Main Thread 0 Only)
   ========================================================================= */

/**
 * @brief Registers an observer backed by a custom filtering block.
 * @param live_data The target data container stream.
 * @param lifecycle_owner The widget context tied to this subscription's lifetime.
 * @param filter The evaluation predicate block. If NULL, all updates pass through.
 * @param observer The layout update block executed when the filter matches. Must not be NULL.
 * @return An opaque tracking token for lifecycle management, or NULL if an argument is NULL or
 *         the allocation failed.
 *
 * The predicate is called as `filter(last_admitted_value, proposed_value)` before each update,
 * and a false return suppresses that update for this observer alone. It is not consulted for
 * the delivery made at registration (see @ref IrcitUiLiveDataObserve).
 */
PUBLIC_API IrcitUiObserverToken *IrcitUiLiveDataObserveWithFilter(IrcitUiLiveData *live_data, IrcitUiNode *lifecycle_owner, IrcitUiLiveDataFilter filter, IrcitUiObserverBlock observer);

/**
 * @brief Binds a reactive observer scope directly to a widget lifecycle.
 * @param live_data The target data container stream.
 * @param lifecycle_owner The widget context tied to this subscription's lifetime.
 * @param observer The layout update block executed on every accepted update. Must not be NULL.
 * @return An opaque tracking token for lifecycle management, or NULL if an argument is NULL
 *         or the allocation failed.
 *
 * The block is invoked once here, on the value in the slot, so a caller paints from current
 * state without waiting for a mutation.
 *
 * @section observer_contract The observer contract
 *
 * The block runs inline on Main Thread 0 and is not defended against:
 *
 *   - **Paint and return.** Read the value, set what it maps to (@ref IrcitUiNodeSetText,
 *     @ref IrcitUiRequestLayout, @ref IrcitUiInvalidate) and return. The whole UI waits behind
 *     the block: every other observer and every frame.
 *   - **Do not block.** No sleeps, no blocking reads, no waiting on a worker, no unbounded
 *     loops. Hand further work to the scheduler pool and deliver the result back through the
 *     async bridge.
 *
 * A breach is measured, not pre-empted: the elapsed time is taken and a warning naming the node
 * is logged once it passes `PRIV_CONFIG_DEFAULT_OBSERVER_BUDGET_MS`. The block is not
 * interrupted, not disconnected and not retried (DESIGN §16).
 */
PUBLIC_API IrcitUiObserverToken *IrcitUiLiveDataObserve(IrcitUiLiveData *live_data, IrcitUiNode *lifecycle_owner, IrcitUiObserverBlock observer);

/**
 * @brief Manually detaches a subscriber stream from its LiveData source.
 * @param token The tracking handle returned by an earlier call to IrcitUiLiveDataObserve.
 * @note Main Thread 0 only. A stale or previously unobserved token is safe and results in a
 *       silent no-op.
 */
PUBLIC_API void IrcitUiLiveDataUnobserve(IrcitUiObserverToken *token);

/**
 * @brief Derives a downstream LiveData stream by transforming an upstream dependency.
 * @param source The parent LiveData node supplying raw inputs.
 * @param lifecycle_owner The widget controlling the functional pipeline's lifetime.
 * @param mapper The functional transformation block. Must not be NULL.
 * @return A new opaque LiveData handle, or NULL if an argument is NULL or the allocation failed.
 *
 * The derived container is seeded with `mapper(source's current value)` and inherits the
 * source's dispatch policy. It is kept current by a subscription on the source, so the mapper
 * runs on Main Thread 0 as part of the source's dispatch.
 *
 * @warning The mapper's return value is stored as-is and is never released. It must outlive the
 *          derived container, which does not copy it.
 */
PUBLIC_API IrcitUiLiveData *IrcitUiLiveDataMap(IrcitUiLiveData *source, IrcitUiNode *lifecycle_owner, IrcitUiMapBlock mapper);

#ifdef __cplusplus
}
#endif

#endif //IRCITUILIB_IRCITUI_LIVEDATA_H
