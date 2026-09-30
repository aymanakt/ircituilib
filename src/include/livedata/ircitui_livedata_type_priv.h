/**
 * @file ircitui_livedata_type_priv.h
 * @brief The concrete LiveData container, hidden behind the public opaque handle.
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

#ifndef IRCITUILIB_IRCITUI_LIVEDATA_TYPE_PRIV_H
#define IRCITUILIB_IRCITUI_LIVEDATA_TYPE_PRIV_H

#include <stdatomic.h>
#include "livedata/ircitui_observer_token_type_priv.h"

#if 0


+─────────────────────────────────────────────────────────────────────────────────────────────────────────────────+
│                                              HOST APPLICATION DOMAIN                                            │
│                                                                                                                 │
│   [ WORKER BACKGROUND THREAD(S) Pool / Async I/O ]                 [ MAIN UI THREAD 0 - Event Callbacks ]       │
│                           │                                                           │                         │
│                           │ (Asynchronous Post)                                       │ (Synchronous Write)     │
│                           ▼                                                           ▼                         │
│             IrcitUiLiveDataPostValue()                                     IrcitUiLiveDataSetValue()            │
+───────────────────────────┼───────────────────────────────────────────────────────────┼─────────────────────────+
                            │                                                           │
                            ▼                                                           │
            +────────────────────────────────+                                          │
            │  1. atomic_store(&live_data)   │◄─────────────────────────────────────────┤ (Direct Update)
            +───────────────────────┬────────+                                          │
                                    │                                                   │
                                    ▼                                                   │
            +────────────────────────────────+                                          │
            │  2. atomic_exchange(pending)   │                                          │
            +───────────────────────┬────────+                                          │
                                    │                                                   │
                  [Is Pending == 1?]                                                    │
                 ╱                  ╲                                                   │
           (Yes)╱                    ╲(No)                                              │
               ▼                      ▼                                                 │
        ┌──────────────┐     ┌────────────────────────────────┐                         │
        │ Skip Message │     │ 3. Allocate Clang Block        │                         │
        │ (Coalesced)  │     │    - Capture weak references   │                         │
        └──────────────┘     │    - Capture proposed data     │                         │
                             └────────────────┬───────────────┘                         │
                                              │                                         │
                                              ▼                                         │
                             ┌────────────────────────────────┐                         │
                             │ 4. Pack Envelope Into Message  │                         │
                             │    (MAIN_MSG_COROUTINE_READY)  │                         │
                             └────────────────┬───────────────┘                         │
                                              │                                         │
                                              ▼                                         │
                             ┌────────────────────────────────┐                         │
                             │ 5. Lockless MPSC Ring Buffer   │                         │
                             └────────────────┼───────────────┘                         │
─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ┼ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ┼ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─
│                                             │                                         │                         │
│                                             ▼                                         │
│                            ┌────────────────────────────────┐                         │
│                            │ 6. IrcitUiMainLooper Loop      │                         │
│                            │    (main_looper.c Process Msg) │                         │
│                            └────────────────┼───────────────┘                         │
│                                             │                                         │
│                                             ▼                                         │
│                            ┌────────────────────────────────┐                         │
│                            │ 7. Extract & Run Clang Block   │                         │
│                            │    - atomic_store(pending, 0)  │                         │
│                            │    - atomic_load(&value)       │                         │
│                            └────────────────┼─────────────────────────────────────────┘
│                                             │
│                                             ▼
│                            ┌────────────────────────────────┐
│                            │ 8. Arm POSIX Watchdog Timer    │
│                            │    - sigsetjmp() Marker        │
│                            │    - timer_settime(30ms Limit) │
│                            └────────────────┼────────────────────────────────────────┐
│                                             │                                        │
│                                             ▼                                        │ (If Watchdog Fires)
│                            ┌────────────────────────────────┐                        │   SIGRTMIN Delivered
│                            │ 9. Evaluate Custom Filter      │                        ▼
│                            │    (IrcitUiLiveDataFilter)     │             ┌────────────────────┐
│                            └────────────────┼───────────────┘             │  13. siglongjmp()  │
│                                             │                             │      Triggered     │
│                                   [Filter Returns True?]                  └──────────┬─────────┘
│                                  ╱                  ╲                                │
│                            (Yes)╱                    ╲(No: Drop Update)              ▼
│                                ▼                      ▼                   ┌────────────────────┐
│                  ┌───────────────────────────┐  ┌──────────────────┐      │ 14. EVICT CRASHED  │
│                  │ 10. Execute Observer Block│  │ Disarm Watchdog  │      │     SUBSCRIBER     │
│                  │     (Host UI Changes)     │  │ & Move Next      │      │ (Disconnect Token) │
│                  └──────────────┬────────────┘  └──────────────────┘      └──────────┬─────────┘
│                                 │                                                    │
│                                 ▼                                                    │
│                  ┌───────────────────────────┐                                       │
│                  │ 11. Disarm Watchdog Timer │                                       │
│                  │     (timer_settime -> 0)  │◄──────────────────────────────────────┘
│                  └──────────────┬────────────┘
│                                 │
│                                 ▼
│                  ┌───────────────────────────┐
│                  │ 12. Run UI Canvas Engine  │
│                  │     - IrcitUiNodeSetText  │
│                  │     - RequestLayout tree  │
│                  │     - Redraw via ncurses  │
│                  └───────────────────────────┘
│                                                                                                                 │
│                                         FRAMEWORK RUNTIME DOMAIN                                                │
+─────────────────────────────────────────────────────────────────────────────────────────────────────────────────+

#endif
/**
 * @brief The dispatch policy a container was created with.
 *
 * Private: the caller picks a policy by choosing a constructor, never by passing a value, so
 * nothing outside this module needs to name it. It lives here rather than in its own public
 * header for that reason — the public companion to this field is the pair of factory functions
 * in ircitui_livedata.h.
 *
 * The only reader of `mode` outside the two gate helpers is IrcitUiLiveDataMap, which copies it
 * onto the derived container so a projection conflates exactly when its source does.
 */
typedef enum {
  IRCITUI_LIVEDATA_DISPATCH_ATOMIC = 0, /* One ring message per post. The default. */
  IRCITUI_LIVEDATA_DISPATCH_CONFLATED,  /* At most one undispatched message; newest value wins. */
} IrcitUiLiveDataMode;

/**
 * @brief Internal concrete definition of the core LiveData abstraction.
 * Completely hidden behind the public opaque pointer boundary.
 */
struct IrcitUiLiveData {
  _Atomic(void *) value;               /* Lock-free hardware atomic reference to the current payload */
  _Atomic(int) dispatch_pending;       /* CONFLATED only: 1 while a message is queued undispatched */
  IrcitUiLiveDataMode mode;            /* Dispatch policy; fixed at creation, read by PostValue */
  struct IrcitUiObserverToken *observers; /* Main-thread only singly-linked observer chain */
};

#endif //IRCITUILIB_IRCITUI_LIVEDATA_TYPE_PRIV_H
