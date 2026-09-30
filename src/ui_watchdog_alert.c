/**
 * @file ui_watchdog_alert.c
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
 */
#include <stdio.h>
#include <string.h>

#include "ui_watchdog_alert.h"
#include "ui_priv.h"
#include "ui_log.h"

/* One alert at a time, so one node, and the strings it shows. The strings are
   file-scope rather than locals of Show because a node holds its text as a
   BORROWED pointer (see common.text) - a buffer on Show's stack would be a
   dangling pointer the moment Show returned, and the frame that paints the
   alert runs long after that. */
static IrcitUiNode *s_alert_node;
static int          s_alert_dismiss_requested;
static char         s_alert_target[96];
static char         s_alert_phase[96];

/**
 * @brief The alert's key listener: swallows everything, dismisses on ENTER.
 *
 * Returning 1 for every key is the intercept: while the alert is up, nothing
 * underneath it sees a keystroke. The dismissal is deferred - see
 * IrcitUiWatchdogAlertDismiss.
 */
static int
sAlertKey(IrcitUiEventDispatcher *disp_ptr, IrcitUiEvent *ev_ptr, void *user_ptr)
{
  (void)disp_ptr;
  (void)user_ptr;

  if (!ev_ptr) return 1;

  /* ENTER only: 10 and 13 are its two spellings across terminals.
   *
   * ESC is deliberately NOT accepted, though the design that prompted this
   * named it. The loop reads input with timeout(0) (main_looper.c), so ncurses
   * cannot afford the inter-byte wait that tells a lone ESC apart from the
   * start of a sequence - it hands back 27 first. An arrow key is therefore
   * indistinguishable here from a dismissal, and accepting ESC meant any
   * arrow key closed the alert. Measured, not theorised: see the ESC case in
   * the demo notes. A modal that any arrow key dismisses is not a modal. */
  if (ev_ptr->key_code == 10 || ev_ptr->key_code == 13) {
    IrcitUiWatchdogAlertDismiss();
  }

  return 1;
}

int
IrcitUiWatchdogAlertIsActive(void)
{
  return s_alert_node != NULL;
}

void
IrcitUiWatchdogAlertDismiss(void)
{
  /* A mark, not a teardown. This is reached from the alert's own key listener,
     and IrcitUiPopStacked frees the node - freeing the node whose listener is
     on the stack would hand the dispatch a dangling pointer to unwind through. */
  if (s_alert_node) s_alert_dismiss_requested = 1;
}

void
IrcitUiWatchdogAlertReap(void)
{
  IrcitUiNode *node_ptr = s_alert_node;

  if (!node_ptr || !s_alert_dismiss_requested) return;

  s_alert_node             = NULL;
  s_alert_dismiss_requested = 0;

  // Detach, deflate, free. It also clears focus if the alert held it.
  IrcitUiPopStacked(node_ptr);

  // What was underneath has to be repainted: the alert was drawn over it.
  if (IrcitUiScreen.root) IrcitUiRequestLayout(IrcitUiScreen.root);
}

void
IrcitUiWatchdogAlertShow(IrcitUiEventDispatcher *disp_ptr, const char *node_id, int phase)
{
  IrcitUiWidgetDef win_def, title_def, body_def, target_def, phase_def, hint_def, btn_def;
  IrcitUiNode *    node_ptr;

  if (!disp_ptr) return;
  if (s_alert_node) return;
  if (!IrcitUiScreen.root) return;

  snprintf(s_alert_target, sizeof(s_alert_target), "Node target    : %s", node_id ? node_id : "(unnamed)");
  snprintf(s_alert_phase, sizeof(s_alert_phase), "Thread context : %s",
           phase == IRCITUI_ALERT_PHASE_FUTURE ? "future continuation" : "hardware event handler");

  memset(&win_def, 0, sizeof(win_def));
  win_def.id          = "watchdog_alert";
  win_def.kind        = IRCITUI_LINEAR;
  win_def.text        = "";
  win_def.orientation = IRCITUI_VERT;
  win_def.width_mode  = IRCITUI_SIZE_EXACT;
  win_def.width       = 52;
  win_def.height_mode = IRCITUI_SIZE_EXACT;
  win_def.height      = 9;
  win_def.flags       = IRCITUI_F_VISIBLE | IRCITUI_F_ENABLED | IRCITUI_F_BORDER;
  /* The colours are named rather than numbered.
   *
   * They were `fg = 7, bg = 0, bdr = 2` here and 3 / 6 / 6 / 8 below -- a set of
   * numbers from which nothing said which line was the warning and which was the
   * muted detail. They are palette slots now, so the alert states its intent and the
   * palette decides that on an eight-colour terminal the warning is a bold red
   * rather than a colour that does not exist.
   *
   * The border keeps a colour of its own: the nine slots name a node's surface, a
   * text line, an overlay or a value's leading edge, and a border is none of them. */
  win_def.theme_class = "panel";
  win_def.bdr         = 2;

  node_ptr = IrcitUiCreateFloatingWindow(&win_def);
  if (!node_ptr) {
    IRCITUI_LOG_WARN("watchdog alert could not be raised: no screen root, or out of memory");
    return;
  }

  memset(&title_def, 0, sizeof(title_def));
  title_def.id          = "watchdog_alert_title";
  title_def.kind        = IRCITUI_TEXT_VIEW;
  title_def.text        = "SYSTEM PROTECTION WARNING";
  title_def.width_mode  = IRCITUI_SIZE_MATCH;
  title_def.height_mode = IRCITUI_SIZE_WRAP;
  title_def.flags       = IRCITUI_F_VISIBLE | IRCITUI_F_ENABLED;
  title_def.theme_class = "text_error";

  memset(&body_def, 0, sizeof(body_def));
  body_def.id          = "watchdog_alert_body";
  body_def.kind        = IRCITUI_TEXT_VIEW;
  body_def.text        = "A background operation was terminated.";
  body_def.width_mode  = IRCITUI_SIZE_MATCH;
  body_def.height_mode = IRCITUI_SIZE_WRAP;
  body_def.flags       = IRCITUI_F_VISIBLE | IRCITUI_F_ENABLED;
  body_def.theme_class = "text";

  memset(&target_def, 0, sizeof(target_def));
  target_def.id          = "watchdog_alert_target";
  target_def.kind        = IRCITUI_TEXT_VIEW;
  target_def.text        = s_alert_target;
  target_def.width_mode  = IRCITUI_SIZE_MATCH;
  target_def.height_mode = IRCITUI_SIZE_WRAP;
  target_def.flags       = IRCITUI_F_VISIBLE | IRCITUI_F_ENABLED;
  target_def.theme_class = "text_muted";

  memset(&phase_def, 0, sizeof(phase_def));
  phase_def.id          = "watchdog_alert_phase";
  phase_def.kind        = IRCITUI_TEXT_VIEW;
  phase_def.text        = s_alert_phase;
  phase_def.width_mode  = IRCITUI_SIZE_MATCH;
  phase_def.height_mode = IRCITUI_SIZE_WRAP;
  phase_def.flags       = IRCITUI_F_VISIBLE | IRCITUI_F_ENABLED;
  phase_def.theme_class = "text_muted";

  memset(&hint_def, 0, sizeof(hint_def));
  hint_def.id          = "watchdog_alert_hint";
  hint_def.kind        = IRCITUI_TEXT_VIEW;
  hint_def.text        = "[ Press ENTER to dismiss ]";
  hint_def.width_mode  = IRCITUI_SIZE_MATCH;
  hint_def.height_mode = IRCITUI_SIZE_WRAP;
  hint_def.flags       = IRCITUI_F_VISIBLE | IRCITUI_F_ENABLED;
  hint_def.theme_class = "text_muted";

  memset(&btn_def, 0, sizeof(btn_def));
  btn_def.id          = "watchdog_alert_dismiss";
  btn_def.text        = "Dismiss";
  btn_def.width_mode  = IRCITUI_SIZE_MATCH;
  btn_def.height_mode = IRCITUI_SIZE_WRAP;

  IrcitUiAddChild(node_ptr, &title_def);
  IrcitUiAddChild(node_ptr, &body_def);
  IrcitUiAddChild(node_ptr, &target_def);
  IrcitUiAddChild(node_ptr, &phase_def);
  IrcitUiAddChild(node_ptr, &hint_def);
  IrcitUiAddButton(node_ptr, &btn_def);

  /* Measure the children that were just added.
   *
   * IrcitUiCreateFloatingWindow laid the window out and realised it while it was
   * still empty, and the root's own layout pass will never revisit it: a stacked
   * node is skipped there (sIsStacked), which is what keeps a floating window out
   * of the flow it floats above. So the window's creator is the only thing that
   * can measure its contents, and without this pair the box draws and its text
   * does not. Geometry is re-used rather than recomputed, so the centring and
   * clamping CreateFloatingWindow already did is left alone. */
  IrcitUiLayout(node_ptr, node_ptr->common.x, node_ptr->common.y, node_ptr->common.w, node_ptr->common.h);
  IrcitUiRealize(node_ptr);

  /* The intercept. Installed on the window's own node, because that is the node
     the key walk reaches once it holds focus, and every key that lands here is
     swallowed - the layout underneath is frozen and must stay that way. */
  node_ptr->interactions_user.lifecycle_listeners.on_key_down = sAlertKey;

  // Take focus so the walk arrives. Then raise the layout request that makes the frame pass measure, realise and paint it over what is already there.
  IrcitUiSetFocus(node_ptr);
  IrcitUiRequestLayout(node_ptr);

  s_alert_node              = node_ptr;
  s_alert_dismiss_requested = 0;

  IRCITUI_LOG_WARN("watchdog alert raised for node '%s' (phase %d)", node_id ? node_id : "(unnamed)", phase);
}
