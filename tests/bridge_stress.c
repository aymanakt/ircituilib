/**
 * @file bridge_stress.c
 * @brief Standalone stress test for the future bridge and promise semantics.
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
 * What it is for: ui_async_bridge.c is the one place a worker's result becomes
 * a value Thread 0 can read, and every claim it makes is a claim about a
 * contract with ufsrvrxlib - that the callback BORROWS the result, that a
 * terminal continuation runs exactly ONCE, on the thread that completed the
 * promise, and that the failure path carries an id rather than a node pointer
 * because a result can outlive the widget it was fetched for. This harness
 * holds it to each of them and reads the envelope it actually posted.
 *
 * Its observable output is the ring: completing a future through the bridge
 * leaves exactly one MainLoopMessage, and every assertion below is made against
 * that envelope or against the spy that recorded who ran the continuation. The
 * ring is the loop's own, reached through a context built by
 * IrcitUiInitialiseFrameworkContext, so what is inspected is what the loop
 * would drain.
 *
 * Build and run:
 *
 *   cmake --build build/dev-test -j$(nproc)
 *
 * clang, not cc: this tree needs -fblocks, and cc is gcc here, which rejects it.
 *
 *   clang -D_GNU_SOURCE -Iinclude -Isrc/include -g -std=c17 -fblocks \
 *      -fsanitize=address,undefined tests/bridge_stress.c \
 *      -o /tmp/bridge_stress \
 *      build/dev-test/ircituilib.a /usr/local/lib/libufsrvrxlib.a \
 *      /usr/local/lib/libuflib.a -lzlog -lpng -lz -lncursesw -lformw
 *   LD_LIBRARY_PATH=/usr/local/lib ASAN_OPTIONS=detect_leaks=0 /tmp/bridge_stress
 *
 * Writing it established one thing worth knowing before it establishes anything
 * else: COMPLETING A PROMISE DESTROYS IT. A second UfsrvPromiseSetValue on the
 * same promise is a use-after-free inside ufsrvrxlib - the first completion
 * frees the promise at ufsrv_future.c:273 and the second writes through it at
 * :255. "Exactly once" is not a guard the promise checks; it is the promise
 * ceasing to exist. A caller that completes a promise twice has no error to
 * observe, only a heap corruption, so the rule is: one completion, one promise.
 *
 * detect_leaks=0 because this harness leaves the framework's own init
 * allocations standing, as the demo and the library both do.
 *
 * Add -Wall -Wextra -Wpedantic -Werror to the clang line to check it strictly.
 */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <ircituilib/ircit_ui_log_retention_enum.h>
#include <ircituilib/ircitui_context.h>
#include <ircituilib/ui.h>
#include <ufsrvrxlib/ufsrv_future/ufsrv_future.h>

#include "ircitui_context_type_priv.h"
#include "ircitui_event_dispatcher_type_priv.h"
#include "ui_async_bridge.h"
#include "ui_event_dispatcher_priv.h"
#include "ui_log.h"
#include "ui_priv.h"

#include <uflib/cdt/lockless_ringbuffer/cdt_lockless_ringbuffer.h>

static int s_failures;

#define CHECK(cond, ...)                                                                           \
  do {                                                                                             \
    if (!(cond)) {                                                                                 \
      s_failures++;                                                                                \
      fprintf(stderr, "  FAIL %s:%d: ", __FILE__, __LINE__);                                       \
      fprintf(stderr, __VA_ARGS__);                                                                \
      fputc('\n', stderr);                                                                         \
    }                                                                                              \
  } while (0)

/* ------------------------------------------------------------------ the spy */

static int       s_spy_calls;
static pthread_t s_spy_thread;

/* Registered in place of the bridge so the harness can see how many times the
   continuation ran and on which thread, then delegate to the real one. */
static void
sSpyContinuation(UfsrvFutureResult *result_ptr, void *context_ptr)
{
  s_spy_calls++;
  s_spy_thread = pthread_self();
  IrcitUiPostFutureContinuation(result_ptr, context_ptr);
}

/* ------------------------------------------------------------------ helpers */

static void
reset_spy(void)
{
  s_spy_calls  = 0;
  s_spy_thread = pthread_self();
}

/* Wait for the spy to have run, without assuming the continuation is
   synchronous - it happens to be, and a test that baked that in would stop
   testing the contract and start testing the implementation detail. */
static int
wait_for_spy(int want, long ms)
{
  struct timespec ts = { .tv_sec = 0, .tv_nsec = 1000000 };
  long            end;

  for (end = 0; end < ms && s_spy_calls < want; end++) nanosleep(&ts, NULL);
  return s_spy_calls;
}

static size_t
drain_ring(LocklessRingBuffer *ring_ptr, MainLoopMessage *last_ptr)
{
  MainLoopMessage discard;
  size_t          n = 0;

  while (LocklessRingBufferTryPop(ring_ptr, &discard)) {
    n++;
    if (last_ptr) *last_ptr = discard;
  }
  return n;
}

static int s_freed;

static void
sFreeCounted(void *ptr)
{
  s_freed++;
  free(ptr);
}

static UfsrvPromise *s_worker_promise;

static void *
sCompleteFromWorker(void *arg)
{
  (void)arg;
  UfsrvPromiseSetValue(s_worker_promise, (void *)"from-a-worker", NULL);
  return NULL;
}

/* ------------------------------------------------------------------ cases */

int
main(void)
{
  IrcitUiContext *        ctx_ptr;
  IrcitUiEventDispatcher *disp;
  LocklessRingBuffer *    ring_ptr;
  IrcitUiNode *           target;
  const char *            target_id;
  MainLoopMessage         got;
  size_t                  n;

  printf("bridge stress - the envelope the bridge actually posts\n\n");

  (void)IrcitUiLogInit("error", "/tmp/ircitui_bridge_stress.log", IRCITUI_LOG_OVERWRITE);

  ctx_ptr = IrcitUiInitialiseFrameworkContext(IrcitUiDataBind, NULL);
  if (!ctx_ptr || !ctx_ptr->ircitui_event_dispatcher) {
    fprintf(stderr, "the framework context could not be built; nothing can be tested\n");
    return 2;
  }
  disp     = ctx_ptr->ircitui_event_dispatcher;
  ring_ptr = disp->async_response_queue;
  if (!ring_ptr) {
    fprintf(stderr, "the pipeline ring was not created\n");
    return 2;
  }

  /* A real node from a real clone, so the id is one the framework owns. */
  IrcitUiScreenReset();
  target = IrcitUiScreen.root;
  if (!target) {
    fprintf(stderr, "no screen tree; the target node cases cannot run\n");
    return 2;
  }
  target_id = target->common.id ? target->common.id : "(no id)";
  printf("target node '%s', ring capacity %zu\n\n", target_id,
         LocklessRingBufferCapacity(ring_ptr));

  /* ------------------------------------------------------------------ 1 */
  printf("case 1: a successful result is enveloped with its value\n");
  {
    UfsrvFuture *         future = NULL;
    UfsrvPromise *        promise;
    IrcitUiBridgeContext *bridge;

    bridge = calloc(1, sizeof *bridge);
    bridge->dispatcher  = disp;
    bridge->target_node = target;
    bridge->target_id   = target_id;
    bridge->kind        = IRCITUI_BRIDGE_TEXT;

    (void)drain_ring(ring_ptr, NULL);
    reset_spy();
    promise = UfsrvPromiseCreate(&future);
    UfsrvFutureThen(future, sSpyContinuation, bridge);
    UfsrvPromiseSetValue(promise, (void *)"payload-arrived", NULL);
    UfsrvFutureRelease(future);

    wait_for_spy(1, 500);
    n = drain_ring(ring_ptr, &got);
    CHECK(n == 1, "%zu envelopes posted for one completion", n);
    CHECK(got.type == MAIN_MSG_FUTURE_READY, "type is %d, expected FUTURE_READY", (int)got.type);
    CHECK(got.payload.async_work.status_code == 0, "status_code is %d for a success",
          got.payload.async_work.status_code);
    CHECK(strcmp(got.status_message, "payload-arrived") == 0, "value is '%s'", got.status_message);
    CHECK(got.target_node == target, "the target node was not carried through");
    printf("  ok: type %d, status 0, value '%s', node carried\n", (int)got.type, got.status_message);
  }

  /* ------------------------------------------------------------------ 2 */
  printf("case 2: a failure is reported by id, and drops the node pointer\n");
  {
    UfsrvFuture *         future = NULL;
    UfsrvPromise *        promise;
    IrcitUiBridgeContext *bridge;

    bridge = calloc(1, sizeof *bridge);
    bridge->dispatcher  = disp;
    bridge->target_node = target;
    bridge->target_id   = target_id;
    bridge->kind        = IRCITUI_BRIDGE_TEXT;

    (void)drain_ring(ring_ptr, NULL);
    reset_spy();
    promise = UfsrvPromiseCreate(&future);
    UfsrvFutureThen(future, sSpyContinuation, bridge);
    UfsrvPromiseSetError(promise, -1);
    UfsrvFutureRelease(future);

    wait_for_spy(1, 500);
    n = drain_ring(ring_ptr, &got);
    CHECK(n == 1, "%zu envelopes posted for one failure", n);
    CHECK(got.payload.async_work.status_code != 0, "status_code is 0 for a failure");
    CHECK(got.target_node == NULL,
          "the failure carried a node pointer: a result can outlive the widget, and a freed "
          "pointer is indistinguishable from a live one on the receiving side");
    CHECK(strcmp(got.status_message, target_id) == 0, "failure names '%s', expected '%s'",
          got.status_message, target_id);
    printf("  ok: status %d, node NULL, named by id '%s'\n", got.payload.async_work.status_code,
           got.status_message);
  }

  /* ------------------------------------------------------------------ 3 */
  printf("case 3: an image delivery gets its own discriminant\n");
  {
    UfsrvFuture *         future = NULL;
    UfsrvPromise *        promise;
    IrcitUiBridgeContext *bridge;

    bridge = calloc(1, sizeof *bridge);
    bridge->dispatcher = disp;
    bridge->target_id  = target_id;
    bridge->kind       = IRCITUI_BRIDGE_IMAGE;

    (void)drain_ring(ring_ptr, NULL);
    reset_spy();
    promise = UfsrvPromiseCreate(&future);
    UfsrvFutureThen(future, sSpyContinuation, bridge);
    UfsrvPromiseSetValue(promise, (void *)"/tmp/some.png", NULL);
    UfsrvFutureRelease(future);

    wait_for_spy(1, 500);
    n = drain_ring(ring_ptr, &got);
    CHECK(n == 1, "%zu envelopes posted", n);
    CHECK(got.type == MAIN_MSG_FUTURE_IMAGE, "type is %d, expected FUTURE_IMAGE (%d)", (int)got.type,
          (int)MAIN_MSG_FUTURE_IMAGE);
    printf("  ok: type %d, path '%s'\n", (int)got.type, got.status_message);
  }

  /* ------------------------------------------------------------------ 4 */
  printf("case 4: an overlong value is truncated, not overflowed\n");
  {
    UfsrvFuture *         future = NULL;
    UfsrvPromise *        promise;
    IrcitUiBridgeContext *bridge;
    char *                big;
    size_t                i;

    big = malloc(5000);
    for (i = 0; i < 4999; i++) big[i] = 'x';
    big[4999] = '\0';

    bridge = calloc(1, sizeof *bridge);
    bridge->dispatcher = disp;
    bridge->target_id  = target_id;
    bridge->kind       = IRCITUI_BRIDGE_TEXT;

    (void)drain_ring(ring_ptr, NULL);
    reset_spy();
    s_freed  = 0;
    promise = UfsrvPromiseCreate(&future);
    UfsrvFutureThen(future, sSpyContinuation, bridge);
    UfsrvPromiseSetValue(promise, big, sFreeCounted);
    /* Read here, before the release. The continuation has already run - it runs
       inside the completion - so this is the moment that tests "borrows": a
       release is what frees the value, and checking after it would test the
       release rather than the bridge. */
    CHECK(s_freed == 0, "the continuation freed the value, which it only BORROWS");
    UfsrvFutureRelease(future);

    wait_for_spy(1, 500);
    n = drain_ring(ring_ptr, &got);
    CHECK(n == 1, "%zu envelopes posted", n);
    /* The seam is the envelope's fixed inline buffer. A 4999-byte value must be
       cut to fit it and stay terminated - ASan is the judge of "not overflowed",
       the length and the terminator are the judge of "cut to fit". */
    CHECK(strlen(got.status_message) == sizeof(got.status_message) - 1,
          "a 4999-byte value became %zu bytes, expected %zu", strlen(got.status_message),
          sizeof(got.status_message) - 1);
    CHECK(got.status_message[sizeof(got.status_message) - 1] == '\0',
          "the envelope's last byte is not the terminator");
    printf("  ok: 4999 bytes cut to %zu, terminated, value untouched until the release\n",
           strlen(got.status_message));
  }

  /* ------------------------------------------------------------------ 5 */
  printf("case 5: the continuation runs on the thread that completed the promise\n");
  {
    UfsrvFuture *         future = NULL;
    IrcitUiBridgeContext *bridge;
    pthread_t             worker;
    pthread_t             main_thread = pthread_self();

    bridge = calloc(1, sizeof *bridge);
    bridge->dispatcher = disp;
    bridge->target_id  = target_id;
    bridge->kind       = IRCITUI_BRIDGE_TEXT;

    (void)drain_ring(ring_ptr, NULL);
    reset_spy();
    s_worker_promise = UfsrvPromiseCreate(&future);
    UfsrvFutureThen(future, sSpyContinuation, bridge);

    if (pthread_create(&worker, NULL, sCompleteFromWorker, NULL) != 0) {
      CHECK(0, "the worker thread could not be created");
    } else {
      pthread_join(worker, NULL);
      wait_for_spy(1, 500);
      CHECK(s_spy_calls == 1, "the continuation ran %d times, expected 1", s_spy_calls);
      CHECK(!pthread_equal(s_spy_thread, main_thread),
            "the continuation ran on the MAIN thread, so the thread boundary is not real");
      CHECK(pthread_equal(s_spy_thread, worker), "the continuation ran on neither the worker "
                                                 "nor main - it ran on a third thread");
      printf("  ok: ran once, on the worker that completed it\n");
    }
    UfsrvFutureRelease(future);
    (void)drain_ring(ring_ptr, NULL);
  }

  /* ------------------------------------------------------------------ 6 */
  printf("case 6: two deliveries, two envelopes, no shared state between them\n");
  {
    UfsrvFuture *         future_a = NULL, *future_b = NULL;
    UfsrvPromise *        promise_a, *promise_b;
    IrcitUiBridgeContext *bridge_a, *bridge_b;
    (void)0;
    size_t                seen_a = 0, seen_b = 0, i;

    bridge_a = calloc(1, sizeof *bridge_a);
    bridge_a->dispatcher = disp;
    bridge_a->target_id  = "target_a";
    bridge_a->kind       = IRCITUI_BRIDGE_TEXT;
    bridge_b = calloc(1, sizeof *bridge_b);
    bridge_b->dispatcher = disp;
    bridge_b->target_id  = "target_b";
    bridge_b->kind       = IRCITUI_BRIDGE_IMAGE;

    (void)drain_ring(ring_ptr, NULL);
    reset_spy();
    promise_a = UfsrvPromiseCreate(&future_a);
    promise_b = UfsrvPromiseCreate(&future_b);
    UfsrvFutureThen(future_a, sSpyContinuation, bridge_a);
    UfsrvFutureThen(future_b, sSpyContinuation, bridge_b);

    UfsrvPromiseSetValue(promise_a, (void *)"value_a", NULL);
    UfsrvPromiseSetError(promise_b, -7);
    UfsrvFutureRelease(future_a);
    UfsrvFutureRelease(future_b);

    wait_for_spy(2, 500);
    CHECK(s_spy_calls == 2, "the spy ran %d times for two completions", s_spy_calls);

    /* Order is not promised, so sort the two envelopes by id rather than by
       arrival - asserting arrival order would be asserting an implementation
       detail the ring does not offer. */
    for (i = 0; i < 2; i++) {
      if (!LocklessRingBufferTryPop(ring_ptr, &got)) break;
      if (strcmp(got.status_message, "value_a") == 0) {
        seen_a++;
        CHECK(got.type == MAIN_MSG_FUTURE_READY, "A posted type %d, expected FUTURE_READY",
              (int)got.type);
        CHECK(got.payload.async_work.status_code == 0, "A carries status %d for a success",
              got.payload.async_work.status_code);
      } else if (strcmp(got.status_message, "target_b") == 0) {
        seen_b++;
        CHECK(got.type == MAIN_MSG_FUTURE_IMAGE,
              "B posted type %d, expected FUTURE_IMAGE - the two contexts share state",
              (int)got.type);
        CHECK(got.payload.async_work.status_code == -7, "B carries status %d, expected -7",
              got.payload.async_work.status_code);
      } else {
        CHECK(0, "an envelope carried '%s', which belongs to neither delivery", got.status_message);
      }
    }
    CHECK(seen_a == 1, "%zu envelopes carried A's value", seen_a);
    CHECK(seen_b == 1, "%zu envelopes carried B's failure", seen_b);
    printf("  ok: two deliveries, one TEXT success and one IMAGE failure, each intact\n");
  }

  /* ------------------------------------------------------------------ 7 */
  printf("case 7: a NULL result is a failure, and NULL arguments are survivable\n");
  {
    IrcitUiBridgeContext *bridge;

    bridge = calloc(1, sizeof *bridge);
    bridge->dispatcher = disp;
    bridge->target_id  = target_id;
    bridge->kind       = IRCITUI_BRIDGE_TEXT;

    (void)drain_ring(ring_ptr, NULL);
    IrcitUiPostFutureContinuation(NULL, bridge); /* result NULL: must not crash */
    n = drain_ring(ring_ptr, &got);
    CHECK(n == 1, "%zu envelopes for a NULL result", n);
    CHECK(got.payload.async_work.status_code == 0 && strcmp(got.status_message, target_id) == 0,
          "a NULL result was not treated as a failure");

    (void)drain_ring(ring_ptr, NULL);
    IrcitUiPostFutureContinuation(NULL, NULL); /* both NULL: must not crash */
    n = drain_ring(ring_ptr, NULL);
    CHECK(n == 0, "%zu envelopes posted for an all-NULL call", n);
    printf("  ok: NULL result became a failure; NULL context posted nothing and did not crash\n");
  }

  IrcitUiEventDispatcherDestroy(disp);
  IrcitUiLogFini();

  printf("\n%s (%d failure%s)\n", s_failures ? "FAILED" : "PASSED", s_failures,
         s_failures == 1 ? "" : "s");
  return s_failures != 0;
}
