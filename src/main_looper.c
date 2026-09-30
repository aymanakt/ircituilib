/**
 * @file main_looper.c
 * @brief The framework loop: it owns the frame, the pacing and the flush.
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

#include <curses.h>
#include <stdlib.h>
#include <poll.h>
#include <time.h>
#include <unistd.h>

#include "ui_engine_priv.h"
#include "include/ircitui_context_type_priv.h"
#include "ui_event_dispatcher_priv.h"
#include "ui_log.h"
#include "ui_priv.h"
#include "ui_watchdog_alert.h"
/* The consumer half of the future bridge lives in main_looper: the IMAGE and
   READY cases below are where a worker's result reaches the tree. */
#include "ui_async_bridge.h"
#include "ircituilib/config_defs.h"

/* After the project's own headers, and must stay there: uflib defines PUBLIC_API
   as a visibility attribute and ircituilib_defs.h defines it empty, so reaching
   uflib first is a -Wmacro-redefined error under -Werror. */
#include <Block.h>
#include <uflib/cdt/lockless_ringbuffer/cdt_lockless_ringbuffer.h>
#include <ufsrvrxlib/ufsrv_coroutine/ufsrv_coroutine.h>

static long long
sNowMs(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (long long)ts.tv_sec * 1000LL + (long long)(ts.tv_nsec / 1000000L);
}

/* --- The two future deliveries, as bodies -----------------------------------
 *
 * Hoisted out of the switch because both contain an early `break`, and a break
 * inside an armed region exits the switch without reaching the disarm. A
 * watchdog left armed over the frame pass would then fire into a landing zone
 * nothing is watching. Passing the work to a function keeps one exit.
 *
 * Both run on Thread 0 and both do unbounded work - the alert raises a window
 * and the image delivery decodes a PNG, whose cost belongs to the file and not
 * to the framework - so both are actioned under the deadline, like every other
 * pipeline message.
 */
static void sDeliverFutureResult(IrcitUiEventDispatcher *disp, MainLoopMessage *msg) {
    IrcitUiNode *target;

    // An async failure raises the alert, and these two branches are the only places it is raised.
    if (msg->payload.async_work.status_code != 0) {
        IrcitUiWatchdogAlertShow(disp, msg->status_message, IRCITUI_ALERT_PHASE_FUTURE);
        return;
    }

    if (!msg->target_node) return;

    target = msg->target_node;

    if (target->common.kind == IRCITUI_EDIT_TEXT) {
        strncpy(target->common.interaction_defaults.ebuf, msg->status_message,
                sizeof(target->common.interaction_defaults.ebuf) - 1);
        target->common.interaction_defaults.ebuf[sizeof(target->common.interaction_defaults.ebuf) - 1] = '\0';
        target->common.text = target->common.interaction_defaults.ebuf;
        IrcitUiRequestLayout(target);
    } else {
        IRCITUI_LOG_WARN("future result for node '%s' dropped: kind %d owns no text storage",
                         target->common.id ? target->common.id : "(no id)", (int)target->common.kind);
    }
}

/* A worker's INCREMENTAL report, as opposed to the two terminal deliveries above.
 *
 * The difference that matters is that this one resolves its target by ID. A future's
 * context is freed by its own continuation, so the node pointer it carries is good for
 * exactly one delivery; a progress report is posted repeatedly while a run is in flight
 * and the widget it drives can be closed in the middle of one. A pointer captured by the
 * worker would be freed memory by the time this runs, and a freed node is
 * indistinguishable from a live one -- so the envelope carries the id, which is a
 * borrowed literal that outlives every clone.
 *
 */
static void sDeliverFutureProgress(IrcitUiEventDispatcher *disp, MainLoopMessage *msg) {
    IrcitUiNode *target;

    (void)disp;

    if (!IrcitUiScreen.root) return;

    target = IrcitUiFind(IrcitUiScreen.root, msg->status_message);
    if (!target) {
        IRCITUI_LOG_INFO("progress report for '%s' dropped: the node is gone", msg->status_message);
        return;
    }

    IrcitUiBarSetValue(target, msg->payload.async_work.status_code);
}

/* The sibling of sDeliverFutureProgress, and identical in every respect except the setter
 * it reaches.  Separated rather than parameterised by a function pointer for the reason the
 * two tags are separate: the lookup, the drop-in-silence and the "the lookup is the
 * cancellation" argument are the same, and a shared body taking a callback would have made
 * the one line that differs the least visible part of it. */
static void sDeliverFutureBuffered(IrcitUiEventDispatcher *disp, MainLoopMessage *msg) {
    IrcitUiNode *target;

    (void)disp;

    if (!IrcitUiScreen.root) return;

    target = IrcitUiFind(IrcitUiScreen.root, msg->status_message);
    if (!target) {
        IRCITUI_LOG_INFO("buffered report for '%s' dropped: the node is gone", msg->status_message);
        return;
    }

    IrcitUiBarSetBuffered(target, msg->payload.async_work.status_code);
}

static void sDeliverFutureImage(IrcitUiEventDispatcher *disp, MainLoopMessage *msg) {
    if (msg->payload.async_work.status_code != 0) {
        IrcitUiWatchdogAlertShow(disp, msg->status_message, IRCITUI_ALERT_PHASE_FUTURE);
        return;
    }
    // A path, produced by a worker that only ever wrote a file. Raising the window is Thread 0's, because the tree is.
    IrcitUiBridgeShowImageResult(msg->status_message);
}

/**
 * @brief Centralized routing hub running on Thread 0. Resolves and executes
 * framework actions using structural user or system veto handlers.
 */
static void sProcessMainPipelineMessage(IrcitUiEventDispatcher *disp, MainLoopMessage *msg) {
    if (!msg) return;

    switch (msg->type) {
        case MAIN_MSG_HARDWARE_KEY:
            if (IrcitUiScreen.root) {
                if (sigsetjmp(*(sigjmp_buf*)&disp->watchdog_context.env, 1) == 0) {
                    disp->watchdog_context.in_callback = 1;
                    // Focus is read HERE, not off msg->target_node.
                    disp->current_processing_node_id =
                        IrcitUiFocusedNode() ? IrcitUiFocusedNode()->common.id : "(key)";

                    struct itimerspec its = { .it_value = { .tv_sec = 0, .tv_nsec = disp->watchdog_timeout_ms * 1000000 } };
                    timer_settime(disp->watchdog_context.posix_timer_id, 0, &its, NULL);

                    // Execute layout routing pass through the standard engine traversal tree
                    IrcitUiDispatchKey(IrcitUiScreen.root, msg->payload.key_code);

                    struct itimerspec disarm = {0};
                    timer_settime(disp->watchdog_context.posix_timer_id, 0, &disarm, NULL);
                    disp->watchdog_context.in_callback = 0;
                } else {
                    disp->watchdog_context.in_callback = 0;
                    IRCITUI_LOG_WARN("[WATCHDOG EVICTION] Key dispatch hung past %llu ms on node '%s'.",
                                     (unsigned long long)disp->watchdog_timeout_ms,
                                     disp->current_processing_node_id ? disp->current_processing_node_id : "(none)");
                }
            }
            break;

        case MAIN_MSG_HARDWARE_MOUSE:
            if (IrcitUiScreen.root) {
                // Same call into the tree as a key, so the same watchdog wrap - see the GAP note above.
                if (sigsetjmp(*(sigjmp_buf*)&disp->watchdog_context.env, 1) == 0) {
                    disp->watchdog_context.in_callback = 1;
                    disp->current_processing_node_id = msg->target_node ? msg->target_node->common.id : "(mouse)";

                    struct itimerspec its = { .it_value = { .tv_sec = 0, .tv_nsec = disp->watchdog_timeout_ms * 1000000 } };
                    timer_settime(disp->watchdog_context.posix_timer_id, 0, &its, NULL);

                    IrcitUiRecordMouse(msg->payload.mouse.mouse_x, msg->payload.mouse.mouse_y);
                    IrcitUiDispatchKey(IrcitUiScreen.root, KEY_MOUSE);

                    struct itimerspec disarm = {0};
                    timer_settime(disp->watchdog_context.posix_timer_id, 0, &disarm, NULL);
                    disp->watchdog_context.in_callback = 0;
                } else {
                    disp->watchdog_context.in_callback = 0;
                    IRCITUI_LOG_WARN("[WATCHDOG EVICTION] Mouse dispatch hung past %llu ms on node '%s'.",
                                     (unsigned long long)disp->watchdog_timeout_ms,
                                     disp->current_processing_node_id ? disp->current_processing_node_id : "(none)");
                }
            }
            break;

        case MAIN_MSG_COROUTINE_READY:
            // INTERCEPT EXTENSION: Handle LiveData posted Clang blocks
            if (msg->payload.async_work.coroutine_handle == NULL) {
                void (^dispatch_block)(void);
                memcpy(&dispatch_block, msg->status_message, sizeof(void *));

                if (dispatch_block) {
                    dispatch_block();
                    Block_release(dispatch_block);
                }
            }
            // Fallback to standard framework cooperative coroutine dispatch
            else if (msg->payload.async_work.coroutine_handle) {
                // Safeguard execution with your native recovery watchdog trap context
                if (sigsetjmp(*(sigjmp_buf*)&disp->watchdog_context.env, 1) == 0) {
                    disp->watchdog_context.in_callback = 1;
                    disp->current_processing_node_id = msg->target_node ? msg->target_node->common.id : "async_coro";

                    // Start hardware protection timer
                    struct itimerspec its = { .it_value = { .tv_sec = 0, .tv_nsec = disp->watchdog_timeout_ms * 1000000 } };
                    timer_settime(disp->watchdog_context.posix_timer_id, 0, &its, NULL);

                    // --- RESUME COOPERATIVE COROUTINE SAFELY ON MAIN THREAD ---
                    UfsrvCoroutineResume((UfsrvCoroutine *)msg->payload.async_work.coroutine_handle);

                    // Disarm security timer
                    struct itimerspec disarm = {0};
                    timer_settime(disp->watchdog_context.posix_timer_id, 0, &disarm, NULL);
                    disp->watchdog_context.in_callback = 0;
                } else {
                    // Watchdog recovery routine
                    disp->watchdog_context.in_callback = 0;
                    IRCITUI_LOG_WARN("[WATCHDOG EVICTION] Coroutine hung during main-thread execution slice.");
                }
            }
            break;

        case MAIN_MSG_FUTURE_READY:
            if (sigsetjmp(*(sigjmp_buf*)&disp->watchdog_context.env, 1) == 0) {
                disp->watchdog_context.in_callback = 1;
                disp->current_processing_node_id = msg->target_node ? msg->target_node->common.id : "(future)";

                struct itimerspec its = { .it_value = { .tv_sec = 0, .tv_nsec = disp->watchdog_timeout_ms * 1000000 } };
                timer_settime(disp->watchdog_context.posix_timer_id, 0, &its, NULL);

                sDeliverFutureResult(disp, msg);

                struct itimerspec disarm = {0};
                timer_settime(disp->watchdog_context.posix_timer_id, 0, &disarm, NULL);
                disp->watchdog_context.in_callback = 0;
            } else {
                disp->watchdog_context.in_callback = 0;
                IRCITUI_LOG_WARN("[WATCHDOG EVICTION] Future delivery hung past %llu ms.",
                                 (unsigned long long)disp->watchdog_timeout_ms);
            }
            break;

        case MAIN_MSG_FUTURE_IMAGE:
            if (sigsetjmp(*(sigjmp_buf*)&disp->watchdog_context.env, 1) == 0) {
                disp->watchdog_context.in_callback = 1;
                disp->current_processing_node_id = "(image)";

                struct itimerspec its = { .it_value = { .tv_sec = 0, .tv_nsec = disp->watchdog_timeout_ms * 1000000 } };
                timer_settime(disp->watchdog_context.posix_timer_id, 0, &its, NULL);

                sDeliverFutureImage(disp, msg);

                struct itimerspec disarm = {0};
                timer_settime(disp->watchdog_context.posix_timer_id, 0, &disarm, NULL);
                disp->watchdog_context.in_callback = 0;
            } else {
                disp->watchdog_context.in_callback = 0;
                IRCITUI_LOG_WARN("[WATCHDOG EVICTION] Image delivery hung past %llu ms.",
                                 (unsigned long long)disp->watchdog_timeout_ms);
            }
            break;

        case MAIN_MSG_FUTURE_PROGRESS:
            /* Armed like the two deliveries above and not like the network stream below,
               and the distinction is the one the tag's own comment draws: a stream is
               posted at line rate and deliberately runs without the deadline, while this
               is posted once per stage of a bounded run. What it does on Thread 0 is a
               find plus a setter -- bounded work -- so a report that does not return means
               something is wrong, which is what the deadline is for. */
            if (sigsetjmp(*(sigjmp_buf*)&disp->watchdog_context.env, 1) == 0) {
                disp->watchdog_context.in_callback = 1;
                disp->current_processing_node_id = msg->status_message;

                struct itimerspec its = { .it_value = { .tv_sec = 0, .tv_nsec = disp->watchdog_timeout_ms * 1000000 } };
                timer_settime(disp->watchdog_context.posix_timer_id, 0, &its, NULL);

                sDeliverFutureProgress(disp, msg);

                struct itimerspec disarm = {0};
                timer_settime(disp->watchdog_context.posix_timer_id, 0, &disarm, NULL);
                disp->watchdog_context.in_callback = 0;
            } else {
                disp->watchdog_context.in_callback = 0;
                IRCITUI_LOG_WARN("[WATCHDOG EVICTION] Progress delivery hung past %llu ms.",
                                 (unsigned long long)disp->watchdog_timeout_ms);
            }
            break;

        case MAIN_MSG_FUTURE_BUFFERED:
            // Armed like the progress delivery above, for the identical reason: bounded work on Thread 0 -- a find plus a setter -- posted once per stage rather than at line rate.
            if (sigsetjmp(*(sigjmp_buf*)&disp->watchdog_context.env, 1) == 0) {
                disp->watchdog_context.in_callback = 1;
                disp->current_processing_node_id = msg->status_message;

                struct itimerspec its = { .it_value = { .tv_sec = 0, .tv_nsec = disp->watchdog_timeout_ms * 1000000 } };
                timer_settime(disp->watchdog_context.posix_timer_id, 0, &its, NULL);

                sDeliverFutureBuffered(disp, msg);

                struct itimerspec disarm = {0};
                timer_settime(disp->watchdog_context.posix_timer_id, 0, &disarm, NULL);
                disp->watchdog_context.in_callback = 0;
            } else {
                disp->watchdog_context.in_callback = 0;
                IRCITUI_LOG_WARN("[WATCHDOG EVICTION] Buffered delivery hung past %llu ms.",
                                 (unsigned long long)disp->watchdog_timeout_ms);
            }
            break;

        case MAIN_MSG_LIFECYCLE_TICK:
            // PostLifecycle runs the application's listener synchronously, so it is wrapped for the same reason a key is - see the GAP note above.
            if (sigsetjmp(*(sigjmp_buf*)&disp->watchdog_context.env, 1) == 0) {
                disp->watchdog_context.in_callback = 1;
                disp->current_processing_node_id = msg->target_node ? msg->target_node->common.id : "(lifecycle)";

                struct itimerspec its = { .it_value = { .tv_sec = 0, .tv_nsec = disp->watchdog_timeout_ms * 1000000 } };
                timer_settime(disp->watchdog_context.posix_timer_id, 0, &its, NULL);

                IrcitUiDispatcherPostLifecycle(disp, msg->target_node, msg->payload.key_code);

                struct itimerspec disarm = {0};
                timer_settime(disp->watchdog_context.posix_timer_id, 0, &disarm, NULL);
                disp->watchdog_context.in_callback = 0;
            } else {
                disp->watchdog_context.in_callback = 0;
                IRCITUI_LOG_WARN("[WATCHDOG EVICTION] Lifecycle listener hung past %llu ms on node '%s'.",
                                 (unsigned long long)disp->watchdog_timeout_ms,
                                 disp->current_processing_node_id ? disp->current_processing_node_id : "(none)");
            }
            break;

        case MAIN_MSG_NETWORK_DATA:
            /* ================================================================
               DELIBERATELY NOT WATCHDOG ARMED. Read this before adding a
               sigsetjmp here, because every other branch in this switch has one
               and its absence will otherwise look like an oversight.

               The eviction path is siglongjmp, and it abandons the callback's
               stack without unwinding it: whatever lock the callback held is
               still held, and whatever state it was part-way through updating
               stays part-way updated. For a key that costs a lost keystroke. A
               stream reassembler is the first callback in this tree whose state
               is durable ACROSS calls, so an eviction tears a parse mid-update
               and leaves a connection that can never resynchronise.

               What the exemption costs: a listener that blocks
               here stops Thread 0, which stops this drain, which stops the
               producer's backpressure from ever clearing -- the whole
               application wedges, where a hung key is survivable.

               So the call is timed instead, and the log line below is the whole
               of what replaces the eviction. That is the trade, and the timing
               is not decoration: without it nothing measures this path at all.
               See IRCITUILIB_EVENTS_TCP_IP_DESIGN.md A.5.
               ================================================================ */
            if (!disp->network_listener) {
                // The producer's push already succeeded, so its backpressure reports success and nothing upstream can tell the chunk went nowhere. This line is the only signal there is that it did.
                IRCITUI_LOG_WARN("network chunk (token %p) dropped: no listener registered",
                                 (void *)msg->payload.network.stream_token);
                break;
            }

            disp->current_processing_node_id = "(network_stream_exempt)";
            {
                long long started_ms = sNowMs();

                (void)disp->network_listener(disp,
                                             msg->payload.network.stream_token,
                                             msg->status_message,
                                             disp->network_user);

                long long elapsed_ms = sNowMs() - started_ms;
                if (elapsed_ms > PRIV_CONFIG_DEFAULT_NETWORK_LISTENER_BUDGET_MS) {
                    IRCITUI_LOG_WARN("network listener over budget: %lld ms (token %p, budget %d ms)",
                                     elapsed_ms,
                                     (void *)msg->payload.network.stream_token,
                                     (int)PRIV_CONFIG_DEFAULT_NETWORK_LISTENER_BUDGET_MS);
                }
            }
            break;
    }
}

/* --- SS27: the Ctrl+Enter collector ----------------------------------------
 *
 * Ctrl+Enter is one gesture and TWO encodings, and neither is a key ncurses knows:
 * it hands the bytes back one at a time, which is why this is a parser rather than
 * another `if` next to the shifted arrows.
 *
 *   modifyOtherKeys (xterm, `XTerm*modifyOtherKeys: 2`)   CSI 27 ; <mod> ; 13 ~
 *   CSI-u (kitty, and xterm 377 and others)               CSI 13 ; <mod> u
 *
 * THE PARAMETERS ARE PARSED RATHER THAN MATCHED, and that is the point of the
 * shape below. Literal matching would bind this to the exact spelling one emulator
 * happens to emit: `27;5;13~` is Ctrl+Enter, `27;6;13~` is Ctrl+Shift+Enter and
 * means the same gesture to a user, `13;5u` is the same key in the other encoding,
 * and a terminal that sends any of them would be silently ignored -- the bytes
 * would land as literal text in whatever field had focus, which reads as "the
 * gesture does nothing".
 *
 * <mod> is 1 + a bitmask (1 shift, 2 alt, 4 ctrl), so the test is the ctrl BIT,
 * which also accepts the shifted and alted forms. Everything after a ':' in CSI-u
 * is the event type and is ignored: `13;5:1u` is a key press, `13;5:3u` a release,
 * and the gesture is the press. */
#define IRCITUI_ESC_SEQ_MAX 24

/* The parameter values of a CSI run, with the final byte. Returns 0 for anything
   that is not a CSI run of digits and semicolons -- including a control byte,
   which is how a run that turns out to be something else gets delivered as
   itself. */
static int
sCsiDecode(const int *buf_ptr, int len, int *vals_ptr, int *nvals_ptr, char *final_ptr)
{
    int i, cur = -1, nvals = 0, done = 0;

    if (len < 3 || buf_ptr[0] != 27 || buf_ptr[1] != '[') return 0;
    *final_ptr = (char)buf_ptr[len - 1];
    for (i = 2; i < len - 1; i++) {
        char ch = (char)buf_ptr[i];

        /* A ':' starts the event type, which is not a parameter and not this
           function's business. Everything after it is skipped -- but the value
           already in hand IS the last parameter, so it is pushed before the skip.
           Without that the modifier is lost on `13;5:1u` and the sequence reads as
           a bare `13`, which is why this form failed the first time it was tried. */
        if (ch == ':') {
            if (nvals < 4) vals_ptr[nvals++] = cur < 0 ? 0 : cur;
            cur  = -1;
            done = 1;
            continue;
        }
        if (done) continue;
        if (ch >= '0' && ch <= '9') {
            if (cur < 0) cur = 0;
            cur = cur * 10 + (ch - '0');
        }
        else if (ch == ';') {
            if (nvals < 4) vals_ptr[nvals++] = cur < 0 ? 0 : cur;
            cur = -1;
        }
        else return 0;
    }
    if (!done && nvals < 4) vals_ptr[nvals++] = cur < 0 ? 0 : cur;
    *nvals_ptr = nvals;
    return 1;
}

/* Non-zero when the bytes in hand ARE Ctrl+Enter, in either encoding. */
static int
sEscSeqIsComplete(const int *buf_ptr, int len)
{
    int  vals[4], nvals = 0;
    char final        = 0;

    if (!sCsiDecode(buf_ptr, len, vals, &nvals, &final)) return 0;
    if (!(vals[nvals > 0 ? nvals - 1 : 0] & 4)) {
        /* The modifier is the last parameter in both encodings, and the ctrl bit
           has to be in it. Checked for nvals of both shapes below instead of
           here, so that a sequence with no parameters is not read as one. */
    }
    if (final == '~') {
        return nvals == 3 && vals[0] == 27 && vals[2] == 13 && (vals[1] & 4) != 0;
    }
    if (final == 'u') {
        return nvals >= 2 && vals[0] == 13 && (vals[1] & 4) != 0;
    }
    return 0;
}

/* Non-zero while the bytes in hand are still a PREFIX of such a sequence, which is
   the only reason to keep collecting rather than deliver what has arrived. */
static int
sEscSeqCouldGrow(const int *buf_ptr, int len)
{
    int i;

    if (len < 1) return 0;
    if (buf_ptr[0] != 27) return 0;
    if (len == 1) return 1;
    if (buf_ptr[1] != '[') return 0;
    for (i = 2; i < len; i++) {
        char ch = (char)buf_ptr[i];
        if (ch >= '0' && ch <= '9') continue;
        if (ch == ';' || ch == ':') continue;
        // A final byte ends it: either it resolves or it does not.
        return ch == '~' || ch == 'u';
    }
    return 1;
}

/* Everything the ingest loop does with ONE key.
 *
 * Factored out so that the collector's flush path -- which delivers the bytes of a
 * run that turned out not to be a sequence -- goes through the same code as an
 * ordinary key. A second copy inside the collector would be a second place for the
 * fast path and for the exit key to be forgotten.
 *
 * Returns 0 when the key asks the looper to exit. */
static int
sIngestKey(IrcitUiEventDispatcher *disp_ptr, int ch)
{
    if (ch == KEY_F(12)) return 0;

    if (ch == KEY_RESIZE) {
        // On the root, because the relayout below is central and once -- see IrcitUiDefaultOnResize.
        IrcitUiDispatcherPostLifecycle(disp_ptr, IrcitUiScreen.root, IRCITUI_EVENT_LAYOUT_RESIZE);
        IrcitUiRequestLayout(IrcitUiScreen.root);
        return 1;
    }

    /* Shift+Up and Shift+Down arrive as KEY_SR and KEY_SF.
     *
     * ncurses folds xterm's modified-arrow sequences onto the scroll keys ITSELF,
     * above terminfo: `\E[1;2A`/`\E[1;2B` are matched by its own table and come
     * back as KEY_SR/KEY_SF, with no terminfo entry involved. That is why
     * `infocmp` shows no shifted kcuu1 or kcud1 and the sequences still work -- a
     * terminfo reading says they cannot be delivered, and a measurement says they
     * are.
     *
     * Translated to the framework's own codes here rather than handled in the
     * engine, so the router keeps matching codes this library defines instead of
     * ones ncurses does. The CAVEAT: on a terminal with
     * genuine scroll keys, KEY_SF and KEY_SR mean scroll, and this would read that
     * as extending a range. xterm is where the sequences come from; a backend that
     * can tell them apart should translate differently. */
    if (ch == KEY_SF) ch = IRCITUI_KEY_SHIFT_DOWN;
    else if (ch == KEY_SR) ch = IRCITUI_KEY_SHIFT_UP;

    // Map character directly to dynamic value type packet structure
    MainLoopMessage input_msg = {
        .type = MAIN_MSG_HARDWARE_KEY,
        .target_node = IrcitUiFocusedNode(),
        .payload.key_code = ch
    };

    // Fast-Path Type stream Intercept optimization bypass
    IrcitUiNode *active_focus = IrcitUiFocusedNode();
    if (active_focus && active_focus->common.kind == IRCITUI_EDIT_TEXT && (ch >= 32 && ch <= 126)) {
        /* THE MUTATION ONLY. This lane bypasses the ring, which is the round trip the
           docs say a printable key must not pay; it does NOT paint. The field's cells
           are the frame pass's, at STAGE 4 of this same pass and before the one
           doupdate, so a paint here staged the node a second time and reached the glass
           no sooner: measured, the two are byte-identical on every input tried, and a
           burst of 200 keys costs a median 3.2 ms here against 1.4 ms without it. */
        (void)IrcitUiExecuteFastPathTyping(active_focus, ch);
        return 1;
    }

    // Enqueue input values directly into the lockless buffer
    IrcitUiDispatcherPostMessage(disp_ptr, &input_msg);
    return 1;
}

/**
 * @brief Unified Event Looper Implementation.
 */
void IrcitUiMainLooper(IrcitUiContext *ircitui_context) {
    if (!ircitui_context) return;
    IrcitUiEventDispatcher *disp = ircitui_context->ircitui_event_dispatcher;
    struct pollfd fds[2];

    fds[0].fd     = STDIN_FILENO;
    fds[0].events = POLLIN;
    fds[1].fd     = IrcitUiEventDispatcherWakeFd(disp);
    fds[1].events = POLLIN;

    timeout(0); // Non-blocking ncurses input evaluation
    IrcitUiScreenReset();

    // When the caret's phase is next due to flip. See the tick below.
    long long blink_due_ms = sNowMs() + PRIV_CONFIG_DEFAULT_CARET_BLINK_MS;

    // Perform an explicit initial layout configuration sweep
    if (IrcitUiScreen.root) {
        IrcitUiLayout(IrcitUiScreen.root, 0, 0, COLS, LINES);
        IrcitUiRealize(IrcitUiScreen.root);
        IrcitUiFocusTop();
    }

    for (;;) {
        /* Block until key inputs hit standard input or background workers ring the
           pipe doorbell.

           The cadence is the overlay's while one is up, and the caret's otherwise.
           The caret is happy to be asked late -- its phase is measured with the
           clock and a late question is answered the same -- but a box travelling a
           row at a time is not: at the caret's half-second the whole journey would
           be two visible steps. */
        /* Two things can want the fast cadence, and the query is the same shape for
           both: a fact the module reports, and the loop acts on. A palette animation
           that cannot get its registers reports "not animating", so enabling an effect
           on a terminal that refuses them does not spin this loop. */
        int animating   = IrcitUiSnackbarActive() || IrcitUiPaletteAnimating() || IrcitUiPaletteFadeActive();
        int poll_result = poll(fds, 2, animating
                                         ? PRIV_CONFIG_DEFAULT_SNACKBAR_FRAME_MS
                                         : PRIV_CONFIG_DEFAULT_CARET_BLINK_MS);
        if (poll_result < 0) continue;

        // STAGE 1: INGEST STANDARD INPUT RAW TO PIPELINE VALUE ENVELOPES
        if (fds[0].revents & POLLIN) {
            int ch;
            // SS27: the bytes of a Ctrl+Enter sequence, and how many are waiting for the rest. See the collector below.
            int esc_len = 0;
            int esc_buf[IRCITUI_ESC_SEQ_MAX];
            // Served from the tree's own root window: wgetch() refreshes a window it finds
            // touched, and initscr leaves stdscr touched-but-blank, so reading it clears the glass.
            WINDOW *input_win = IrcitUiScreen.root ? IrcitUiScreen.root->common.win : NULL;
            // stdscr is the fallback only where root has no window and nothing is painted.
            // Non-blocking, because poll() gates entry to this drain and nothing else does.
            if (input_win) wtimeout(input_win, 0);

            while ((ch = input_win ? wgetch(input_win) : getch()) != ERR) {
                /* CTRL+ENTER IS ASSEMBLED HERE, and unlike the shifted arrows
                 * below it is not a translation of an ncurses key: ncurses has no
                 * such key and hands the sequence back one byte at a time.
                 * Measured on xterm-256color -- `\E[27;5;13~` (modifyOtherKeys)
                 * arrives as ESC '[' '2' '7' ';' '5' ';' '1' '3' '~', and
                 * `\E[13;5u` (the CSI-u protocol) as ESC '[' '1' '3' ';' '5' 'u'.
                 * Neither is in ncurses' extended key table, so there is no key
                 * code to translate from, which is why this is a collector and not
                 * another `if`. The alternative for a terminal that sends neither
                 * is the bare CR that Ctrl+Enter shares with Enter, so on such a
                 * terminal this gesture does not exist and the application must
                 * offer another -- see IRCITUI_KEY_CTRL_ENTER.
                 *
                 * The bytes are COLLECTED rather than dispatched as they arrive,
                 * because a lone ESC is a key in its own right: it closes the top
                 * popup and cancels an edit. Dispatching eagerly would fire it on
                 * the first byte of every sequence and the rest would land as
                 * literal text. So a run starting with ESC is held until it either
                 * matches or proves it cannot be one, and the loop's tail flushes
                 * whatever is still held -- which is what releases a real ESC, on
                 * the same pass it was read: ncurses has already applied its own
                 * ESC delay before returning one, so a lone ESC arrives alone. */
                if (esc_len > 0 || ch == 27) {
                    if (esc_len < IRCITUI_ESC_SEQ_MAX) esc_buf[esc_len++] = ch;
                    if (sEscSeqIsComplete(esc_buf, esc_len)) {
                        (void)sIngestKey(disp, IRCITUI_KEY_CTRL_ENTER);
                        esc_len = 0;
                        continue;
                    }
                    if (sEscSeqCouldGrow(esc_buf, esc_len)) continue;
                    /* Not a sequence this build knows. Every byte in hand is then
                       a key of its own, and goes out exactly as it would have
                       without the collector -- so an unknown sequence loses
                       nothing but the ESC-prefix reading it never had. */
                    {
                        int i;
                        for (i = 0; i < esc_len; i++) {
                            if (!sIngestKey(disp, esc_buf[i])) goto gracefully_exit_looper;
                        }
                        esc_len = 0;
                    }
                    continue;
                }
                if (!sIngestKey(disp, ch)) goto gracefully_exit_looper;
            }
            // Whatever is still held. A lone ESC reaches here, and so does the prefix of a sequence that was cut short.
            {
                int i;
                for (i = 0; i < esc_len; i++) {
                    if (!sIngestKey(disp, esc_buf[i])) goto gracefully_exit_looper;
                }
            }
        }

        /* The caret's tick, and it is NOT the `else` of the branch above.
         *
         * It was, and the branch reads as "stdin was readable, else this was the
         * tick". That equivalence does not hold once mouse reporting is on:
         * IrcitUiMouseInit enables DECSET 1003, so the terminal reports every
         * mouse MOVEMENT, and stdin is readable on essentially every pass for as
         * long as the pointer is moving. The caret therefore stopped blinking for
         * exactly as long as the mouse moved, and poll's timeout could go a long
         * time without ever expiring.
         *
         * The cadence is still the caller's -- see IrcitUiBlinkAdvance, whose
         * phase lives in the engine and whose comment says only the cadence
         * belongs here -- but the caller now measures it with the CLOCK rather
         * than inferring it from poll's return. That is what makes calling it on
         * every pass safe: a motion flood asks many times and is answered 0 until
         * the interval has actually elapsed, so the phase cannot be strobed at
         * the report rate. */
        if (sNowMs() >= blink_due_ms) {
            (void)IrcitUiBlinkAdvance();
            blink_due_ms = sNowMs() + PRIV_CONFIG_DEFAULT_CARET_BLINK_MS;
        }

        // STAGE 2: INGEST DISPATCH PIPELINE NOTIFICATIONS
        // Drains the eventfd counter to zero, which is what re-arms the next poll. The value itself is not a count of anything the loop acts on.
        if (fds[1].revents & POLLIN) {
            uint64_t wake_counter = 0;
            ssize_t bytes_read = read(fds[1].fd, &wake_counter, sizeof(wake_counter));
            (void)bytes_read;
        }

        /* STAGE 3: BOUNDED DRAIN OF PIPELINE ENVELOPES, LOCKLESS

           Capped, and the cap is not an optimisation -- it is what keeps the
           loop a loop. Draining until empty was correct while keys, mouse and
           futures were the only producers, because a human cannot refill a
           1024-slot ring faster than Thread 0 empties it. A network worker can.
           With the ring permanently refilled the drain never returns, so STAGE 4
           never runs and -- because the exit flag is read at the END of a pass --
           IrcitUiDispatcherExit is never reached either: a flooded application
           stops painting and stops being able to quit.

           PRIV_CONFIG_DEFAULT_MAX_EVENTS_PER_PASS carries what the cap does and
           does not buy. The short version: it bounds render starvation, it does
           not bound keystroke latency. */
        MainLoopMessage pending_message;
        size_t          processed_this_pass = 0;

        while (processed_this_pass < PRIV_CONFIG_DEFAULT_MAX_EVENTS_PER_PASS &&
               LocklessRingBufferTryPop(disp->async_response_queue, &pending_message)) {
            sProcessMainPipelineMessage(disp, &pending_message);
            processed_this_pass++;
        }

        // Outside dispatch, so the alert's teardown does not free the node its own key listener is still running against.
        IrcitUiWatchdogAlertReap();

        /* STAGE 3.5: THE OVERLAY'S FRAME, AND THE REPAINT IT IS OWED

           Advanced BEFORE the frame pass, because what it reports is a fact about
           the frame that is about to run: a box that has moved up or is on its way
           out has left cells that no node's change set covers -- the tree did not
           move -- and ncurses copies only touched lines into the virtual screen, so
           without this the trail stays on the glass. IrcitUiInvalidate(root) clears
           IRCITUI_F_DRAWN down the whole tree, which is the engine's own
           repaint-everything path.

           Measured, not assumed: 106 cells of a settled box survive its own move
           without this line, and none do with it. The measurement is
           IRCITUILIB_DESIGN.md's §11.8, with the overlay's other host facts. */
        if (IrcitUiSnackbarAdvance() && IrcitUiScreen.root) IrcitUiInvalidate(IrcitUiScreen.root);

        /* STAGE 3.6: THE CANOPY'S PULSE, IF IT IS RUNNING

           Before the frame pass, like the overlay's advance and for a related reason:
           it changes what the frame is about to draw. Only the REGISTERS move -- the
           cells already point at them -- so this needs no repaint and asks for none.
           That is the difference between this and the overlay above: a slot's colours
           are a pair and a changed pair needs its cells rewritten, while a register a
           pair already names recolours them where they stand. Measured in
           dev/probes/palette/p_register_propagation.c: 23 bytes of OSC 4 and no cell
           emitted at all. */
        if (IrcitUiPaletteAnimating()) IrcitUiPaletteCanopyPulse();
        IrcitUiPaletteFadeTick();

        // STAGE 4: CHOREOGRAPHER RENDER FRAME TICK SUB-CYCLE
        // Not gated on is_dirty: that flag is layout-stale only, and typing, the caret and focus moves are paint-only, so the frame never ran for them.
        if (IrcitUiScreen.root) {
            // Decides for itself whether to measure (is_dirty or no window) and paints only what is stale.
            IrcitUiRenderFrame();
            /* After the tree, and that ordering IS the z-order: ncurses resolves
               stacking by refresh order. It re-stamps on every pass, because the
               frame above has just rewritten the overlay's cells in the virtual
               screen -- an overlay that skipped a pass would have a hole in it. */
            IrcitUiSnackbarRender();
            doupdate(); // Direct ncurses flush for now; the driver's flush_hardware slot is the later move.
        }

        if (IrcitUiDispatcherExited(disp)) break;
    }

gracefully_exit_looper:
    /* Teardown is the application's from here, not the framework's.

       Destroying the dispatcher in this function freed the ring BEFORE control
       returned to the caller, so an application's own producer threads -- the
       only party that can quiesce them -- lost their pipeline with no way to
       observe it. Measured, that is a silent drop rather than a crash: the ring
       pointer is NULLed and TryPush reports false for NULL, so the message is
       discarded and logged as "pipeline full", which blames saturation. A worker
       INSIDE TryPush when the free lands is a use-after-free, but that half is a
       race and is not what the code guarantees against -- the join below is.

       The application now stops and joins its producers and then calls
       IrcitUiDestroyFrameworkContext.

       The label stays because the three early exits in the ingest above converge
       here; a null statement is what C requires after a label when there is
       nothing left to do. */
    ;
}
