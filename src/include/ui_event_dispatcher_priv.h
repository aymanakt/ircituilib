//
// Created by ayman on 15/09/2026.
//

/**
 * @file ui_event_dispatcher_priv.h
 * @brief ui_event_dispatcher_priv
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

#ifndef IRCITUILIB_UI_EVENT_DISPATCHER_PRIV_H
#define IRCITUILIB_UI_EVENT_DISPATCHER_PRIV_H

#include <stdbool.h>
#include "ircitui_event_dispatcher_type_priv.h"

IrcitUiEventDispatcher *IrcitUiEventDispatcherCreate(void);
void IrcitUiEventDispatcherDestroy(IrcitUiEventDispatcher *disp);
/**
 * @brief The descriptor the loop polls for a worker's doorbell.
 *
 * One eventfd, read and written through the same number: the loop polls it, the
 * producer writes a counter increment to it. -1 before Create and after Destroy.
 *
 * @param disp The dispatcher. Ignored if NULL.
 * @return The descriptor, or -1 when there is none.
 */
int IrcitUiEventDispatcherWakeFd(const IrcitUiEventDispatcher *disp);

/**
 * @brief Producer phase: posting value messages without heap contention.
 *
 * The submission point both the hardware read cycle and the background worker
 * pool push through. The payload is copied by value into a ring slot, so the
 * caller's stack frame may be released the moment this returns, and the write
 * to the self-pipe is what wakes the loop if it is blocked in poll().
 *
 * A full ring DROPS the incoming envelope and logs it. It must not retry: the
 * loop posts its own input through this same call and is the ring's only
 * consumer, so a producer that waits for room is waiting on a pop that only it
 * could have performed. Saturating MPSC without that coupling (a pure worker
 * pool, no self-posting) could retry safely.
 *
 * PENDING DECISION - the drop policy has a recorded alternative:
 *   1. Drop the newest, as built. Never blocks, never loses ordering, and the
 *      event that gets lost is the one that arrived when the loop was already
 *      not draining. Chosen.
 *   2. Drop the OLDEST to make room for the newest. No freshness is lost, and
 *      for input that is arguably the better trade - a stale keystroke is worth
 *      less than a live one. Not taken because popping on the producing side
 *      races the consumer for the same slot, which turns a lock-free ring into
 *      one that needs a claim on the read index to be correct. Revisit only if
 *      input loss is observed in practice.
 *
 * A caller that transferred ownership of something inside the envelope MUST inspect the
 * result and reclaim it on failure. The payload is copied by value, so the envelope itself
 * needs no attention, but a pointer packed into @c status_message is opaque to this layer:
 * when the push is dropped, nothing downstream will ever run to take that pointer, and a
 * caller that does not reclaim it leaks it. LiveData's dispatch blocks are the case in tree.
 *
 * @param disp The dispatcher holding the pipeline. Ignored if NULL.
 * @param msg  The envelope to copy in. Ignored if NULL.
 * @return true when the envelope is in the ring and the loop has been woken; false when it
 *         was dropped, in which case the loop was NOT woken and nothing will be delivered.
 */
bool IrcitUiDispatcherPostMessage(IrcitUiEventDispatcher *disp, const MainLoopMessage *msg);

#endif //IRCITUILIB_UI_EVENT_DISPATCHER_PRIV_H
