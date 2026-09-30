/**
 * @file ringbuffer_stress.c
 * @brief Standalone stress test for the pipeline ring under overfill.
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
 * What it is for: the pipeline's saturation policy is DROP, NEVER RETRY, and
 * that was decided rather than discovered - the loop posts its own input through
 * the same call and is the ring's only consumer, so a producer that waits for
 * room waits on a pop only it could have performed. This harness holds the ring
 * full and measures what that policy actually does.
 *
 * It uses the real code. The producer is IrcitUiDispatcherPostMessage, reached
 * through a context built by IrcitUiInitialiseFrameworkContext; the consumer
 * side is the same LocklessRingBufferTryPop call the loop makes. The one thing
 * it cannot use is sProcessMainPipelineMessage, which is static and needs a
 * terminal - so it drains rather than dispatching, and everything it measures
 * about the ring is the ring the loop actually runs on.
 *
 * Build and run:
 *
 *   cmake --build build/dev-test -j$(nproc)
 *
 * clang, not cc: this tree needs -fblocks, and cc is gcc here, which rejects it.
 *
 *   clang -D_GNU_SOURCE -Iinclude -Isrc/include -g -std=c17 -fblocks \
 *      -fsanitize=address,undefined tests/ringbuffer_stress.c \
 *      -o /tmp/ringbuffer_stress \
 *      build/dev-test/ircituilib.a /usr/local/lib/libufsrvrxlib.a \
 *      /usr/local/lib/libuflib.a -lzlog -lpng -lz -lncursesw -lformw
 *   LD_LIBRARY_PATH=/usr/local/lib ASAN_OPTIONS=detect_leaks=0 /tmp/ringbuffer_stress
 *
 * detect_leaks=0 because this harness deliberately leaves the framework's own
 * init allocations standing - the scheduler pool, the second ring, the context -
 * and neither the demo nor the library frees those either. Leak checking here
 * would report the framework's shape, not this test's findings.
 *
 * Add -Wall -Wextra -Wpedantic -Werror to the clang line to check it strictly.
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <ircituilib/ircit_ui_log_retention_enum.h>
#include <ircituilib/ircitui_context.h>
#include <ircituilib/ui.h>

#include "ircitui_context_type_priv.h"
#include "ircitui_event_dispatcher_type_priv.h"
#include "ui_event_dispatcher_priv.h"
#include "ui_log.h"

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

static long
now_ms(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (long)(ts.tv_sec * 1000L + ts.tv_nsec / 1000000L);
}

/* Drain the ring and return how many envelopes came out. This is the loop's own
   consumer call, not a stand-in for it. */
static size_t
drain_ring(LocklessRingBuffer *ring_ptr)
{
  MainLoopMessage discard;
  size_t          n = 0;

  while (LocklessRingBufferTryPop(ring_ptr, &discard)) n++;
  return n;
}

static void
post_many(IrcitUiEventDispatcher *disp, size_t count)
{
  MainLoopMessage msg = {0};
  size_t          i;

  msg.type = MAIN_MSG_HARDWARE_KEY;
  for (i = 0; i < count; i++) {
    msg.payload.key_code = (int)i;
    IrcitUiDispatcherPostMessage(disp, &msg);
  }
}

/* How many times the log recorded a drop. */
static size_t
count_log_lines(const char *path_ptr, const char *needle_ptr)
{
  char   line[512];
  size_t hits = 0;
  FILE * fp = fopen(path_ptr, "r");

  if (!fp) return 0;
  while (fgets(line, sizeof line, fp))
    if (strstr(line, needle_ptr)) hits++;
  fclose(fp);
  return hits;
}

int
main(void)
{
  const char *         log_path = "/tmp/ircitui_ring_stress.log";
  IrcitUiContext *     ctx_ptr;
  IrcitUiEventDispatcher *disp;
  LocklessRingBuffer * ring_ptr;
  size_t               capacity;
  MainLoopMessage      msg = {0};
  int                  fd;
  uint64_t             counter = 0;
  long                 started, elapsed;

  printf("ringbuffer stress - the real producer, the loop's own consumer\n\n");

  /* Logging on, because the drop is otherwise invisible: every IRCITUI_LOG_*
     macro is guarded by the category pointer and does nothing while it is NULL.
     A test that never opened the log would measure the ring and miss the one
     thing the policy says the operator is told. */
  if (IrcitUiLogInit("error", log_path, IRCITUI_LOG_OVERWRITE) != 0) {
    fprintf(stderr, "IrcitUiLogInit failed; drops will not be observable in the log\n");
  }

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

  capacity = LocklessRingBufferCapacity(ring_ptr);
  fd       = IrcitUiEventDispatcherWakeFd(disp);
  printf("capacity %zu envelopes of %zu bytes, wake fd %d\n\n", capacity, sizeof(MainLoopMessage), fd);
  CHECK(capacity > 0, "the ring reports no capacity");

  /* ------------------------------------------------------------------ 1 */
  printf("case 1: filling to capacity loses nothing\n");
  (void)drain_ring(ring_ptr);
  post_many(disp, capacity);
  {
    size_t held = drain_ring(ring_ptr);
    CHECK(held == capacity, "posted %zu, drained %zu: %zu lost below capacity", capacity, held,
          capacity - held);
    printf("  ok: %zu posted, %zu drained\n", capacity, held);
  }

  /* ------------------------------------------------------------------ 2 */
  printf("case 2: overfilling drops the excess, and only the excess\n");
  (void)drain_ring(ring_ptr);
  post_many(disp, capacity + 500);
  {
    size_t held     = drain_ring(ring_ptr);
    size_t logged   = count_log_lines(log_path, "pipeline full");
    CHECK(held == capacity, "ring holds %zu, expected %zu", held, capacity);
    CHECK(logged >= 500, "only %zu drops logged for 500 dropped envelopes", logged);
    printf("  ok: %zu kept, %zu dropped, %zu logged\n", held, capacity + 500 - held, logged);
  }

  /* ------------------------------------------------------------------ 3 */
  printf("case 3: a full ring never blocks the producer\n");
  (void)drain_ring(ring_ptr);
  post_many(disp, capacity); /* leave it permanently full */
  started = now_ms();
  post_many(disp, 20000); /* every one of these meets a full ring */
  elapsed = now_ms() - started;
  CHECK(elapsed < 2000, "20000 posts against a full ring took %ld ms: something is waiting", elapsed);
  printf("  ok: 20000 drops against a full ring in %ld ms - the policy is drop, not wait\n", elapsed);

  /* ------------------------------------------------------------------ 4 */
  printf("case 4: a dropped post does not ring the doorbell\n");
  (void)drain_ring(ring_ptr);
  counter = 0;
  (void)read(fd, &counter, sizeof counter); /* clear whatever earlier cases left */
  post_many(disp, capacity);                /* all accepted, each one wakes */
  counter = 0;
  if (read(fd, &counter, sizeof counter) < 0) printf("  (read: %s)\n", strerror(errno));
  CHECK(counter == capacity, "counter is %llu after %zu accepted posts, expected %zu",
        (unsigned long long)counter, capacity, capacity);
  post_many(disp, 100); /* all dropped; none of them should wake */
  counter = 0;
  (void)read(fd, &counter, sizeof counter);
  CHECK(counter == 0, "counter is %llu after 100 dropped posts, expected 0",
        (unsigned long long)counter);
  printf("  ok: %zu accepted posts woke it %zu times; 100 drops woke it 0\n", capacity, capacity);

  /* ------------------------------------------------------------------ 5 */
  printf("case 5: the ring recovers once the loop drains it\n");
  (void)drain_ring(ring_ptr);
  msg.type = MAIN_MSG_FUTURE_READY;
  IrcitUiDispatcherPostMessage(disp, &msg);
  {
    size_t held = drain_ring(ring_ptr);
    CHECK(held == 1, "posted 1 after a drain, drained %zu", held);
    printf("  ok: 1 posted after a drain, %zu drained\n", held);
  }

  IrcitUiEventDispatcherDestroy(disp);
  IrcitUiLogFini();

  printf("\n%s (%d failure%s)\n", s_failures ? "FAILED" : "PASSED", s_failures,
         s_failures == 1 ? "" : "s");
  return s_failures != 0;
}
