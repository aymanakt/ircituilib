/**
 * @file ui_watchdog_alert.h
 * @brief The modal the framework raises when the watchdog evicts a callback.
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
 * Built on the framework's own popup path - IrcitUiCreateFloatingWindow and
 * IrcitUiPopStacked - rather than on a parallel modal engine. A stacked window
 * is already the thing that sits above the layout, takes focus and is torn
 * down, so an alert is a stacked window that happens to say something alarming.
 *
 * Private to the library.
 */
#ifndef IRCITUILIB_UI_WATCHDOG_ALERT_H
#define IRCITUILIB_UI_WATCHDOG_ALERT_H

#include <ircituilib/ircit_ui_event_dispatcher_type.h>

/**
 * @brief Which phase of a dispatch the watchdog interrupted.
 *
 * Named rather than bare, because the string the alert shows is chosen from
 * this and a caller passing 1 or 2 by hand is a caller that has to read the
 * implementation to know which is which.
 */
#define IRCITUI_ALERT_PHASE_INPUT  1 ///< A hardware event handler was evicted.
#define IRCITUI_ALERT_PHASE_FUTURE 2 ///< A future continuation was evicted.

/**
 * @brief Raises the alert, as a stacked modal window.
 *
 * Does nothing if an alert is already up: an eviction during the dismissal of
 * another eviction's alert must not stack a second one, or a loop that keeps
 * tripping the watchdog would grow a tower of them.
 *
 * @param disp_ptr The dispatcher, for the focus and dirty bookkeeping. Ignored if NULL.
 * @param node_id  The node the eviction was attributed to. May be NULL.
 * @param phase    One of the IRCITUI_ALERT_PHASE_* values.
 */
void IrcitUiWatchdogAlertShow(IrcitUiEventDispatcher *disp_ptr, const char *node_id, int phase);

/**
 * @brief Whether an alert is currently on screen.
 *
 * @return Non-zero while one is up.
 */
int IrcitUiWatchdogAlertIsActive(void);

/**
 * @brief Requests the alert's dismissal.
 *
 * Only marks it. The node is freed by IrcitUiWatchdogAlertReap, on the loop's
 * turn, because this is reached from the alert's own key listener and
 * IrcitUiPopStacked frees the node that listener is running against.
 */
void IrcitUiWatchdogAlertDismiss(void);

/**
 * @brief Tears the alert down if a dismissal has been requested.
 *
 * Called by the loop once per pass, outside any dispatch. Safe to call when
 * nothing is up.
 */
void IrcitUiWatchdogAlertReap(void);

#endif // IRCITUILIB_UI_WATCHDOG_ALERT_H
