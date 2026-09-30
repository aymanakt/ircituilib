//
// Created by ayman on 15/09/2026.
//

/**
 * @file watchdog_context_type_priv.h
 * @brief watchdog_context_type_priv
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

#ifndef IRCITUILIB_WATCHDOG_CONTEXT_TYPE_PRIV_H
#define IRCITUILIB_WATCHDOG_CONTEXT_TYPE_PRIV_H

#include <stdint.h>
#include <signal.h>
#include <setjmp.h>
#include <bits/types/timer_t.h>

/* PENDING DECISION - the recovery mechanism below is unsound.
 *
 * siglongjmp is safe only if the region between arm and disarm holds no lock and
 * allocates nothing, and it does not: a key that opens a popup reaches calloc
 * (IrcitUiClone, sNodeFromDef, IrcitUiCreateFloatingWindow) and ncurses window
 * creation (IrcitUiRealize). A timer firing mid-calloc strands the arena lock on
 * the loop's thread, and the loop resumes only to deadlock on its next
 * allocation - alive, corrupted, and with a stack that explains nothing.
 *
 * Candidates, none chosen:
 *   1. abort() after the handler's write(). Async-signal-safe, and the core dump
 *      beats any stack the handler could print - backtrace() is unsafe here for
 *      the same reason as everything else: it takes the arena lock.
 *   2. Narrow the arm window until it is genuinely lock-free and keep the jump.
 *      The only option that preserves "recover the loop", but it needs a contract
 *      the framework can enforce, not hope for.
 *   3. Set a flag only. Not a recovery: the frozen callback never returns, so the
 *      only thread that could act on the flag is the one that is stuck.
 *
 * Also open: the deadline covers the dispatch only. IrcitUiRenderFrame and
 * doupdate run outside any armed timer. */
typedef struct {
  timer_t posix_timer_id;
  /* The deadline itself lives on the dispatcher, as watchdog_timeout_ms. It was
     carried here as well, written once at creation and never read, so the same
     value had two homes and only one of them was consulted. The dispatcher's is
     the one the loop arms from; this struct holds the state a firing timer
     touches, not the policy that set it. */
  volatile sig_atomic_t in_callback;
  /* The landing zone the watchdog's siglongjmp() returns to. A recovery abandons
     the frozen callback's stack rather than unwinding it: memory it allocated and
     locks it took are still held, so a callback that can be pre-empted must not
     hold either across a call that can outlast the deadline. */
  sigjmp_buf env;
} WatchdogContext;

#endif //IRCITUILIB_WATCHDOG_CONTEXT_TYPE_PRIV_H
