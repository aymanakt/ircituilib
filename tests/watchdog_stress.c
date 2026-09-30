/**
 * @file watchdog_stress.c
 * @brief Standalone stress test for the dispatcher's watchdog semantics.
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
 * Standalone on purpose: it is not a CTest target and it is not built by
 * CMake. It links the real archive and drives the real timer, the real signal
 * handler and the real sigjmp_buf through the private headers, because a test
 * that re-implemented the mechanism would prove things about the copy.
 *
 * Build and run:
 *
 * The archive must be built first:
 *
 *   cmake --build build/dev-test -j$(nproc)
 *
 * clang, not cc: this tree needs -fblocks, and cc is gcc here, which rejects it.
 *
 *   clang -D_GNU_SOURCE -Iinclude -Isrc/include -g -std=c17 -fblocks \
 *      -fsanitize=address,undefined tests/watchdog_stress.c \
 *      -o /tmp/watchdog_stress \
 *      build/dev-test/ircituilib.a /usr/local/lib/libufsrvrxlib.a \
 *      /usr/local/lib/libuflib.a -lzlog -lpng -lz -lncursesw -lformw
 *   LD_LIBRARY_PATH=/usr/local/lib /tmp/watchdog_stress
 *
 * Add -Wall -Wextra -Wpedantic -Werror to the clang line to check it strictly.
 * Split the streams to read the verdicts: the handler writes a line to fd 2 on
 * every fire, and case 5 fires a hundred times.
 *
 * Adversarial by intent. Each case tries to make the watchdog do something it
 * must not: fire when it was not armed, fire for a thread that does not own the
 * landing zone, fire after a disarm, or be mistaken for an unwind. A case that
 * passes because the mechanism silently did nothing is a failure, so every case
 * asserts on an observable outcome and not on the absence of a crash.
 *
 * The handler writes a line to fd 2 on every pre-emption, so a passing run
 * still prints one line per fire. That is the handler's own diagnostic and this
 * test does not silence it.
 *
 * NOT covered here, and deliberately: the recovery's unsoundness when the
 * interrupted frame holds a lock (a hang inside calloc strands the glibc arena
 * lock). Reaching that window needs the signal to land between the arena lock's
 * acquire and release - microseconds - so a deterministic case would have to
 * hold the lock open artificially, which would test the harness rather than the
 * watchdog. It is recorded as a PENDING DECISION in
 * src/include/watchdog_context_type_priv.h. Case 4 tests the property that
 * makes it unsound: the frame is abandoned, not unwound.
 */
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <ircituilib/ircit_ui_event_dispatcher_type.h>

#include "ui_event_dispatcher_priv.h"
#include "ircitui_event_dispatcher_type_priv.h"

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

/* Milliseconds since an arbitrary origin, monotonic. */
static long
now_ms(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (long)(ts.tv_sec * 1000L + ts.tv_nsec / 1000000L);
}

static void
arm_for(timer_t timer_id, long ms)
{
  struct itimerspec its = { .it_value = { .tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000L } };

  if (timer_settime(timer_id, 0, &its, NULL) != 0)
    fprintf(stderr, "  arm failed: %s\n", strerror(errno));
}

static void
disarm(timer_t timer_id)
{
  struct itimerspec zero = {0};

  (void)timer_settime(timer_id, 0, &zero, NULL);
}

/* Spin for @p ms without being optimised away, so the signal has a window to
   arrive in and the test cannot be turned into a no-op by the compiler. */
static void
spin_ms(long ms)
{
  long end = now_ms() + ms;

  while (now_ms() < end) {
    /* A syscall-free busy loop that the optimiser may not delete. */
    static volatile unsigned long sink;
    sink++;
  }
}

/* ------------------------------------------------------------------ case 4 */

static volatile int s_inner_completed;

/* Called between the arm and the hang. A jump out of it means it never returns,
   which is the property the recovery is built on and the reason it cannot free
   what it allocated. */
static void
sInnerFrame(void)
{
  spin_ms(2000);
  s_inner_completed = 1;
}

/* ------------------------------------------------------------------ cases */

/* The timer fires, the handler runs, and control lands back at the setjmp -
   on the thread that armed it. */
static void
case_fires_on_the_arming_thread(IrcitUiEventDispatcher *disp)
{
  pthread_t arming_thread = pthread_self();
  pthread_t        landed_on;
  WatchdogContext *wd = &disp->watchdog_context;
  long             started, elapsed;

  printf("case 1: fires, and lands on the thread that armed it\n");

  started = now_ms();
  wd->in_callback = 1;

  if (sigsetjmp(*(sigjmp_buf *)&wd->env, 1) == 0) {
    arm_for(wd->posix_timer_id, 50);
    spin_ms(2000);
    disarm(wd->posix_timer_id);
    wd->in_callback = 0;
    CHECK(0, "the timer never fired: 2s of spin with a 50ms deadline");
    return;
  }

  elapsed   = now_ms() - started;
  landed_on = pthread_self();

  CHECK(elapsed >= 45, "fired early: %ld ms for a 50 ms deadline", elapsed);
  CHECK(elapsed < 1500, "fired very late: %ld ms for a 50 ms deadline", elapsed);
  CHECK(pthread_equal(arming_thread, landed_on), "landed on a thread that did not arm the timer");

  /* The handler does NOT clear the flag; it jumps immediately, and clearing is
     the recovery site's job - every one of the eight wrapped cases in
     main_looper.c does it in its else branch. Asserted so the contract is
     written down somewhere executable: a recovery site that forgets leaves
     in_callback set, and the next armed timer then jumps into a landing zone
     that may no longer be valid. The document's own SS9.6.2 proposal has the
     handler clear it; the tree does not, and this is the tree. */
  CHECK(wd->in_callback != 0, "the handler cleared in_callback - the tree's contract changed, and "
                              "this test now documents something that is no longer true");
  wd->in_callback = 0; /* what the recovery site does */

  printf("  ok: %ld ms, same thread, in_callback left for the recovery site to clear\n", elapsed);
}

/* An idle process must not be jumped. in_callback is the whole gate. */
static void
case_no_fire_when_not_in_callback(IrcitUiEventDispatcher *disp)
{
  WatchdogContext *wd = &disp->watchdog_context;
  int              jumped = 0;

  printf("case 2: does NOT fire when in_callback is clear\n");

  wd->in_callback = 0;
  if (sigsetjmp(*(sigjmp_buf *)&wd->env, 1) == 0) {
    arm_for(wd->posix_timer_id, 50);
    spin_ms(300);
    disarm(wd->posix_timer_id);
  } else {
    jumped = 1;
  }

  CHECK(!jumped, "jumped while in_callback was clear: an idle process would be pre-empted");

  printf("  ok: 300 ms of spin with a 50 ms deadline armed, no jump\n");
}

/* A disarm must actually disarm. */
static void
case_no_fire_after_disarm(IrcitUiEventDispatcher *disp)
{
  WatchdogContext *wd = &disp->watchdog_context;
  int              jumped = 0;

  printf("case 3: does NOT fire after a disarm\n");

  wd->in_callback = 1;
  if (sigsetjmp(*(sigjmp_buf *)&wd->env, 1) == 0) {
    arm_for(wd->posix_timer_id, 50);
    disarm(wd->posix_timer_id);
    spin_ms(300);
  } else {
    jumped = 1;
  }
  wd->in_callback = 0;

  CHECK(!jumped, "fired after being disarmed");

  printf("  ok: armed then disarmed, 300 ms of spin, no jump\n");
}

/* The frame between the arm and the jump is abandoned, not unwound. This is the
   property that makes the recovery unsound with a lock held, so it is asserted
   rather than described. */
static void
case_jump_abandons_the_frame(IrcitUiEventDispatcher *disp)
{
  WatchdogContext *wd = &disp->watchdog_context;

  printf("case 4: the jump abandons the frame rather than unwinding it\n");

  s_inner_completed = 0;
  wd->in_callback   = 1;

  if (sigsetjmp(*(sigjmp_buf *)&wd->env, 1) == 0) {
    arm_for(wd->posix_timer_id, 50);
    sInnerFrame(); /* never returns: jumped out of mid-call */
    disarm(wd->posix_timer_id);
  }
  wd->in_callback = 0;

  CHECK(s_inner_completed == 0,
        "the interrupted frame completed, so the jump unwound it after all - the "
        "recovery would then be sound, and the PENDING DECISION would be wrong");
  /* Mid-frame, so the disarm above was skipped. Leaving the timer armed would
     fire again into a landing zone nothing is watching. */
  disarm(wd->posix_timer_id);

  printf("  ok: the inner frame never completed, and its disarm was skipped - "
         "which is why the recovery must not hold a lock\n");
}

/* Arm and disarm many times. Catches a timer that latches, leaks a pending
   signal into the next cycle, or fires spuriously on reuse. */
static void
case_repeated_cycles(IrcitUiEventDispatcher *disp)
{
  WatchdogContext *wd = &disp->watchdog_context;
  int              i, spurious = 0, fired = 0;

  printf("case 5: 200 arm/disarm cycles, no spurious fire, no lost fire\n");

  for (i = 0; i < 200; i++) {
    wd->in_callback = 1;
    if (sigsetjmp(*(sigjmp_buf *)&wd->env, 1) == 0) {
      if (i % 2 == 0) {
        /* Armed and allowed to fire. */
        arm_for(wd->posix_timer_id, 5);
        spin_ms(120);
        disarm(wd->posix_timer_id);
        /* Reached only when it did not fire, which for an armed timer at 5ms
           against 120ms of spin is a lost fire. */
        wd->in_callback = 0;
        continue;
      }
      /* Armed and disarmed immediately: must not fire. */
      arm_for(wd->posix_timer_id, 5);
      disarm(wd->posix_timer_id);
      spin_ms(40);
    } else {
      if (i % 2 == 0) {
        fired++;
      } else {
        spurious++;
      }
    }
    wd->in_callback = 0;
  }

  CHECK(spurious == 0, "%d spurious fires out of 100 disarmed cycles", spurious);
  CHECK(fired == 100, "%d of 100 armed cycles fired; %d were lost", fired, 100 - fired);

  printf("  ok: 100 fired, 0 spurious\n");
}

int
main(void)
{
  IrcitUiEventDispatcher *disp;

  printf("watchdog stress - the real timer, handler and landing zone\n\n");

  disp = IrcitUiEventDispatcherCreate();
  if (!disp) {
    fprintf(stderr, "IrcitUiEventDispatcherCreate failed; nothing can be tested\n");
    return 2;
  }

  printf("watchdog deadline is %llu ms, timer handle %p\n\n", (unsigned long long)disp->watchdog_timeout_ms,
         (void *)disp->watchdog_context.posix_timer_id);

  case_fires_on_the_arming_thread(disp);
  case_no_fire_when_not_in_callback(disp);
  case_no_fire_after_disarm(disp);
  case_jump_abandons_the_frame(disp);
  case_repeated_cycles(disp);

  IrcitUiEventDispatcherDestroy(disp);

  printf("\n%s (%d failure%s)\n", s_failures ? "FAILED" : "PASSED", s_failures, s_failures == 1 ? "" : "s");
  return s_failures != 0;
}
