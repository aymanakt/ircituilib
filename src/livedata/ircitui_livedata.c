/**
 * @file ircitui_livedata.c
 * @brief LiveData: allocation, observer registration, and the Thread-0 dispatch loop.
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

#include <stdlib.h>
#include <stdatomic.h>
#include <time.h>
#include <Block.h>
#include "ircituilib/livedata/ircitui_livedata.h"
#include "ui_priv.h"
#include "ui_event_dispatcher_priv.h"
#include "ui_log.h"
#include "livedata/ircitui_observer_token_type_priv.h"
#include "livedata/ircitui_livedata_type_priv.h"

/* Module-private shared extraction logic */
static void IrcitUiInternalDisconnectToken(IrcitUiLiveData *live_data, IrcitUiObserverToken *target);
static int  IrcitUiLiveDataLifecycleHook(IrcitUiEventDispatcher *disp_ptr, IrcitUiEvent *ev_ptr, void *user_ptr);
static void IrcitUiInternalDispatchToObservers(IrcitUiLiveData *live_data, void *target_value);

/* The conflation gate. Both halves live here so the flag is never read outside this pair.
   sClaimPendingPost: true means a message is already queued, so this post must not queue another.
   sConsumePending:   true means this caller is the one that must dispatch; false means a later
                      SetValue already delivered the newest value and this message is a no-op. */
static bool sClaimPendingPost(struct IrcitUiLiveData *live_data);
static bool sConsumePending(struct IrcitUiLiveData *live_data);
static void sReleasePending(struct IrcitUiLiveData *live_data);

static bool
sClaimPendingPost(struct IrcitUiLiveData *live_data)
{
  if (live_data->mode != IRCITUI_LIVEDATA_DISPATCH_CONFLATED) return false;
  return atomic_exchange(&live_data->dispatch_pending, 1) == 1;
}

static bool
sConsumePending(struct IrcitUiLiveData *live_data)
{
  if (live_data->mode != IRCITUI_LIVEDATA_DISPATCH_CONFLATED) return true;
  return atomic_exchange(&live_data->dispatch_pending, 0) == 1;
}

/* Release a claim whose message will never be delivered, so the container keeps notifying
   rather than going permanently silent. Every abandonment path goes through here. */
static void
sReleasePending(struct IrcitUiLiveData *live_data)
{
  if (live_data->mode != IRCITUI_LIVEDATA_DISPATCH_CONFLATED) return;
  atomic_store(&live_data->dispatch_pending, 0);
}

/* The one allocation. Both public constructors and Map come through here, so the policy is
   written in exactly one place and a new policy is a constructor rather than a parameter. */
static struct IrcitUiLiveData *sCreateLiveData(void *initial_value, IrcitUiLiveDataMode mode);

static struct IrcitUiLiveData *
sCreateLiveData(void *initial_value, IrcitUiLiveDataMode mode)
{
  struct IrcitUiLiveData *live_data = calloc(1, sizeof(struct IrcitUiLiveData));
  if (!live_data) return NULL;
  live_data->mode = mode;
  atomic_init(&live_data->value, initial_value);
  atomic_init(&live_data->dispatch_pending, 0);
  return live_data;
}

PUBLIC_API IrcitUiLiveData *
IrcitUiLiveDataCreate(void *initial_value)
{
  return (IrcitUiLiveData*)sCreateLiveData(initial_value, IRCITUI_LIVEDATA_DISPATCH_ATOMIC);
}

PUBLIC_API IrcitUiLiveData *
IrcitUiLiveDataConflatedCreate(void *initial_value)
{
  return (IrcitUiLiveData*)sCreateLiveData(initial_value, IRCITUI_LIVEDATA_DISPATCH_CONFLATED);
}

PUBLIC_API void
IrcitUiLiveDataDestroy(IrcitUiLiveData *opaque_live_data)
{
  AssertIsOnMainThreadUi();

  IrcitUiLiveData *live_data = (IrcitUiLiveData*)opaque_live_data;
  if (!live_data) return;

  IrcitUiObserverToken *current = live_data->observers;
  while (current) {
    IrcitUiObserverToken *next = current->next;
    /* The predicate is heap-copied at registration like the observer, so it is released like one */
    Block_release(current->filter);
    Block_release(current->block);
    free(current);
    current = next;
  }
  free(live_data);
}

PUBLIC_API IrcitUiObserverToken *
IrcitUiLiveDataObserve(IrcitUiLiveData *opaque_live_data, IrcitUiNode *lifecycle_owner, IrcitUiObserverBlock observer)
{
  AssertIsOnMainThreadUi();

  IrcitUiLiveData *live_data = (IrcitUiLiveData*)opaque_live_data;
  if (!live_data || !lifecycle_owner || !observer) return NULL;

  IrcitUiObserverToken *token = calloc(1, sizeof(struct IrcitUiObserverToken));
  if (!token) return NULL;

  token->lifecycle_owner = lifecycle_owner;
  token->block           = Block_copy(observer);
  token->parent          = live_data;
  token->next            = live_data->observers;
  live_data->observers   = token;

  // Register the automated deflation hook to guard against unmanaged view unmappings
  lifecycle_owner->interactions_user.lifecycle_listeners.on_deflate = IrcitUiLiveDataLifecycleHook;
  lifecycle_owner->interactions_user.user                           = live_data;

  // Fire immediate notification using current cache state upon binding (in spite of no outside mutation)
  void *current_val = atomic_load(&live_data->value);
  token->block(current_val);

  return token;
}

PUBLIC_API IrcitUiObserverToken *
IrcitUiLiveDataObserveWithFilter(IrcitUiLiveData *     opaque_live_data, IrcitUiNode *lifecycle_owner,
                                 IrcitUiLiveDataFilter filter, IrcitUiObserverBlock   observer)
{
  AssertIsOnMainThreadUi();

  IrcitUiLiveData *live_data = (IrcitUiLiveData*)opaque_live_data;
  if (!live_data || !lifecycle_owner || !observer) return NULL;

  struct IrcitUiObserverToken *token = calloc(1, sizeof(struct IrcitUiObserverToken));
  if (!token) return NULL;

  token->lifecycle_owner = lifecycle_owner;
  token->block           = Block_copy(observer);
  token->filter          = filter ? Block_copy(filter) : NULL; /* Safe capture initialization */
  token->parent          = live_data;

  token->next          = live_data->observers;
  live_data->observers = token;

  lifecycle_owner->interactions_user.lifecycle_listeners.on_deflate = IrcitUiLiveDataLifecycleHook;
  lifecycle_owner->interactions_user.user                           = live_data;

  // Deliver the initial state to bootstrap the view framework
  void *current_val           = atomic_load(&live_data->value);
  token->last_delivered_value = current_val;
  token->block(current_val);

  return token;
}

/**
 * @brief HOST API FUNCTION: Explicitly breaks an active observation stream.
 * Decoupled and hidden—the host application triggers it using only the opaque handle.
 *
 * @note Should not be called by a background engine routine directly.
 */
PUBLIC_API void
IrcitUiLiveDataUnobserve(IrcitUiObserverToken *opaque_token)
{
  // Guard Boundary: Enforce  thread isolation before altering the graph
  AssertIsOnMainThreadUi();

  IrcitUiObserverToken *target_token = opaque_token;
  if (!target_token || !target_token->parent) return;

  // Enforce unobservation safely inside the Main Thread 0 domain
  IrcitUiLiveData *parent_live_data = target_token->parent;
  IrcitUiInternalDisconnectToken(parent_live_data, target_token);
}

/**
 * @brief Disconnect tokens from the internal subscriber chains.
 * Centralizing this logic ensures thread safety and consistent behavior between manual calls and automated lifecycle hooks.
 */
static void
IrcitUiInternalDisconnectToken(struct IrcitUiLiveData *live_data, struct IrcitUiObserverToken *target)
{
  IrcitUiObserverToken *current = live_data->observers;
  IrcitUiObserverToken *prev    = NULL;

  while (current) {
    if (current == target) {
      // Sever the links in our singly-linked pipeline chain
      if (prev) {
        prev->next = current->next;
      }
      else {
        live_data->observers = current->next;
      }

      // Clean up associated lifecycle bindings on the target component node if needed
      if (current->lifecycle_owner && current->lifecycle_owner->interactions_user.user == live_data) {
        current->lifecycle_owner->interactions_user.lifecycle_listeners.on_deflate = NULL;
        current->lifecycle_owner->interactions_user.user                           = NULL;
      }

      IRCITUI_LOG_INFO("LiveData Token [Owner node: '%s'] successfully detached.",
                       (current->lifecycle_owner && current->lifecycle_owner->common.id) ? current->lifecycle_owner->common.id : "(anonymous)");

      Block_release(current->filter);
      Block_release(current->block);
      free(current);
      return;
    }
    prev    = current;
    current = current->next;
  }
}

/**
 * @brief Framework Functional Map Extension
 * Create a downstream LiveData projection driven by transformations running on Thread 0.
 */
PUBLIC_API IrcitUiLiveData *
IrcitUiLiveDataMap(IrcitUiLiveData *source, IrcitUiNode *lifecycle_owner, IrcitUiMapBlock mapper)
{
  AssertIsOnMainThreadUi();

  if (!source || !lifecycle_owner || !mapper) return NULL;

  // Deep copy the transformation block onto the heap to capture variable scopes
  __block IrcitUiMapBlock heap_mapper = Block_copy(mapper);

  // Safely read the initial source state to initialize the map
  void *initial_source_val = atomic_load(&((struct IrcitUiLiveData *)source)->value);
  void *initial_mapped_val = heap_mapper(initial_source_val);

  // Allocate the dependent tracking container, inheriting the source's dispatch policy
  struct IrcitUiLiveData *target = sCreateLiveData(initial_mapped_val, ((struct IrcitUiLiveData*)source)->mode);
  if (!target) {
    Block_release(heap_mapper);
    return NULL;
  }

  // Set up structural bindings on Thread 0. Using explicit __block variables prevents dangling pointer mutations.
  __block struct IrcitUiLiveData *weak_target = target;

  IrcitUiLiveDataObserve(source, lifecycle_owner, ^(const void *new_source_value) {
    // Runs automatically on Thread 0 when the upstream source updates
    void *transformed = heap_mapper(new_source_value);
    IrcitUiLiveDataSetValue((IrcitUiLiveData*)weak_target, transformed);
  });

  return (IrcitUiLiveData*)target;
}

/**
 * @brief FRAMEWORK RUNTIME AUTOMATION: lifecycle unbinding hook: Cleans up subscriptions automatically when components deflate.
 */
static int
IrcitUiLiveDataLifecycleHook(IrcitUiEventDispatcher *disp_ptr, IrcitUiEvent *ev_ptr, void *user_ptr)
{
  (void)disp_ptr;
  if (!ev_ptr || !ev_ptr->target || !user_ptr) return 0;

  IrcitUiNode *           target_node = ev_ptr->target;
  struct IrcitUiLiveData *live_data   = (struct IrcitUiLiveData*)user_ptr;

  struct IrcitUiObserverToken *current = live_data->observers;
  while (current) {
    struct IrcitUiObserverToken *next = current->next;
    if (current->lifecycle_owner == target_node) {
      // Hand over execution to our core extraction block
      IrcitUiInternalDisconnectToken(live_data, current);
    }
    current = next;
  }
  return 0;
}

/**
 * @brief
 * Strictly executed on Main Thread 0. This is the exact intersection point
 * where the Watchdog, the Filtering handling logic, and the UI layout
 * triggers are evaluated right before calling the observer.
 */
static void
IrcitUiInternalDispatchToObservers(IrcitUiLiveData *live_data, void *target_value)
{
  IrcitUiObserverToken *obs = live_data->observers;
  while (obs) {
    // Read the link BEFORE the block runs: an observer may unobserve itself, freeing the token held here
    IrcitUiObserverToken *next = obs->next;

    if (obs->lifecycle_owner && (obs->lifecycle_owner->common.flags & IRCITUI_F_VISIBLE) &&
      !(obs->lifecycle_owner->common.flags & IRCITUI_F_CLIPPED)) {
      // 1. FILTER STEP: Evaluates safely on Thread 0
      if (obs->filter) {
        if (!obs->filter(obs->last_delivered_value, target_value)) {
          obs = next;
          continue; // Filter drop: skip downstream execution
        }
      }

      // 2. SUBSCRIPTION EXECUTION STEP
      obs->last_delivered_value = target_value;

      // Timed, never pre-empted: the overrun is logged and Thread 0 waits it out (DESIGN §16)
      struct timespec started = {0};
      (void)clock_gettime(CLOCK_MONOTONIC, &started);

      obs->block(target_value); // Invokes Host App UI Mutation Block

      struct timespec ended = {0};
      if (clock_gettime(CLOCK_MONOTONIC, &ended) == 0) {
        long long elapsed_ms = (long long)(ended.tv_sec - started.tv_sec) * 1000 +
                               (long long)(ended.tv_nsec - started.tv_nsec) / 1000000;
        if (elapsed_ms > PRIV_CONFIG_DEFAULT_OBSERVER_BUDGET_MS) {
          IRCITUI_LOG_WARN("observer over budget: %lld ms (node '%s', budget %d ms) -- an "
                           "observer must paint and return; hand slow work to the scheduler",
                           elapsed_ms,
                           obs->lifecycle_owner->common.id ? obs->lifecycle_owner->common.id
                                                           : "(anonymous)",
                           (int)PRIV_CONFIG_DEFAULT_OBSERVER_BUDGET_MS);
        }
      }
    }
    obs = next;
  }
}

/* Framework Main-Thread Set: Direct synchronous execution and dispatch loop notification */
PUBLIC_API void
IrcitUiLiveDataSetValue(IrcitUiLiveData *opaque_live_data, void *value)
{
  // The synchronous path mutates the graph on the caller's thread, so it owes the same guard its siblings carry
  AssertIsOnMainThreadUi();

  IrcitUiLiveData *live_data = (IrcitUiLiveData*)opaque_live_data;
  if (!live_data) return;

  // Synchronous Thread 0 path: update memory cache and process pipelines immediately
  atomic_store(&live_data->value, value);

  // Conflated mode: this delivery supersedes a queued message, making it a no-op
  sReleasePending(live_data);

  IrcitUiInternalDispatchToObservers(live_data, value);
}

#if 0 //pre filter
void
IrcitUiLiveDataSetValue(IrcitUiLiveData *live_data, void *value)
{
  if (!live_data) return;

  // Enforce state assignment safely onto the lockless reference carrier
  atomic_store(&live_data->value, value);

  // Iterate and evaluate active observers immediately (Thread 0 execution guaranteed)
  IrcitUiObserverToken *obs = live_data->observers;
  while (obs) {
    if (obs->lifecycle_owner && (obs->lifecycle_owner->common.flags & IRCITUI_F_VISIBLE) &&
      !(obs->lifecycle_owner->common.flags & IRCITUI_F_CLIPPED)) {
      obs->block(value);
    }
    obs = obs->next;
  }
}
#endif

PUBLIC_API void
IrcitUiLiveDataPostValue(IrcitUiLiveData *opaque_live_data, void *value)
{
  IrcitUiLiveData *live_data = (IrcitUiLiveData*)opaque_live_data;
  if (!live_data) return;

  // 1. Unconditional: a post that loses the gate has still published its value
  atomic_store(&live_data->value, value);

  // 2. Conflated mode only: a queued message will read this value, so queue nothing
  if (sClaimPendingPost(live_data)) return;

  IrcitUiEventDispatcher *disp = IrcitUiDispatcherInstance();
  if (!disp) {
    // Nothing will consume the claim, so it must not be left standing
    sReleasePending(live_data);
    return;
  }

  // 2. Set up the structural messaging envelope destined for the Main Looper
  MainLoopMessage envelope                     = {0};
  envelope.type                                = MAIN_MSG_COROUTINE_READY;
  envelope.target_node                         = NULL;
  envelope.payload.async_work.coroutine_handle = NULL; // <-- The Main Looper Intercept Sentinel

  __block IrcitUiLiveData *weak_live_data = live_data;
  // __block void *captured_value = value;

  // 3. Deep-copy the entry callback, wrapping the execution logic into a block payload
  void (^dispatch_block)(void) = Block_copy(^{
    // Claim BEFORE reading the slot: a worker writing after this finds the gate open and queues
    if (!sConsumePending(weak_live_data)) return; // SetValue already delivered the newest

    // Resolve the latest coalesced state reference payload
    void *latest_coalesced_value = atomic_load(&weak_live_data->value);

    // Direct execution straight into  central filtering/dispatching clearinghouse running on thread 0
    IrcitUiInternalDispatchToObservers(weak_live_data, latest_coalesced_value); });

  // 4. Pack the block reference into the status array allocation slot
  memcpy(envelope.status_message, &dispatch_block, sizeof(void*));

  // 5. Post the message into the Lock-Free MPSC Ring Buffer
  if (!IrcitUiDispatcherPostMessage(disp, &envelope)) {
    // Nothing will run the block, so give back the claim it would have cleared and the block
    sReleasePending(live_data);
    Block_release(dispatch_block);
  }
}

//pre filter
#if 0
/* Host Asynchronous Thread Post: Zero allocation cross-thread pipeline boundary post */
void
IrcitUiLiveDataPostValue(IrcitUiLiveData *live_data, void *value)
{
  if (!live_data) return;

  // Lock-free update of the reference value using hardware atomic primitives
  atomic_store(&live_data->value, value);

  IrcitUiEventDispatcher *disp = IrcitUiDispatcherInstance();
  if (!disp) return;

  // Repurpose standard coroutine pipeline messages for arbitrary lock-free thread updates
  MainLoopMessage envelope                     = {0};
  envelope.type                                = MAIN_MSG_COROUTINE_READY;
  envelope.target_node                         = NULL;
  envelope.payload.async_work.coroutine_handle = NULL; /* Sentinel indicating lambda extraction */

  // Wrap the state evaluation inside a stackless Clang block allocation
  __block IrcitUiLiveData *weak_live_data = live_data;
  __block void *           captured_value = value;

  // Capture the execution context securely using block copy descriptors
  void (^dispatch_block)(void) = Block_copy(
    ^{ IrcitUiObserverToken *obs = weak_live_data->observers; while (obs) {
      if (obs->lifecycle_owner && (obs->lifecycle_owner->common.flags & IRCITUI_F_VISIBLE) &&
        !(obs->lifecycle_owner->common.flags & IRCITUI_F_CLIPPED)) { obs->block(captured_value); }
      obs = obs->next; } });

  // Pack the block address into the status message buffer area safely as a raw pointer
  memcpy(envelope.status_message, &dispatch_block, sizeof(void*));

  // Submit the event envelope into the lockless Multi-Producer Single-Consumer pipeline
  IrcitUiDispatcherPostMessage(disp, &envelope);
}
#endif
