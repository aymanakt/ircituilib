/**
 * @file ircitui_window_registry.c
 * @brief Startup driver selection and lifecycle: the backend switchboard with
 *        automated dependency fallback.
 *
 * Copyright (C) 2015-2026  unfacd works
 *
 * This file is part of ircituilib source code.
 * Created by ayman on 16/09/2026.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 */

#include "ircituilib/window_driver/ircitui_window_registry.h"

#include "ircitui_window_driver_type_priv.h"
#include "ui_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Forward declarations for available concrete driver factories.
 *
 * Declared here rather than in a shared header because the registry is their
 * only caller: a driver is reached by name, through this table, and never
 * instantiated by an application. Each factory returns a driver whose
 * private_backend_data points at that adapter's own context, or NULL when the
 * backend cannot be brought up on this host. */
extern IrcitUiWindowDriver *IrcitUiWindowCreateNcursesAdapter(void);
extern IrcitUiWindowDriver *IrcitUiWindowCreateAnsiFallbackDriver(void);

/* The registry's own state, private to this translation unit. The public header
 * names IrcitUiWindowRegistry and stops there; this is the only definition.
 *
 * active_driver is held writable on purpose. The getter hands callers a const
 * pointer, so this is the single place a driver can be replaced or freed, and
 * keeping that asymmetry here is what stops a caller destroying a driver out
 * from under the registry that owns it.
 *
 * user_callbacks is kept as well as stamped onto each driver. The copy on the
 * driver is the record; this one is what a driver created later is stamped
 * from, so the registry does not have to read a driver to find the hooks for
 * the next one. */
struct IrcitUiWindowRegistry
{
  IrcitUiWindowDriver *              active_driver;
  IrcitUiDriverRegistryUserCallbacks user_callbacks;
  char                               selected_driver_name[32];
};

IrcitUiWindowRegistry *IrcitUiWindowRegistryCreate(IrcitUiDriverRegistryUserCallbacks user_callbacks)
{
  IrcitUiWindowRegistry *reg = calloc(1, sizeof(IrcitUiWindowRegistry));

  if (!reg) return NULL;

  // calloc zeroes both members, so a caller that supplied one hook gets the * other as NULL and it is simply never invoked -- no sentinel needed.
  reg->user_callbacks = user_callbacks;
  return reg;
}

void IrcitUiWindowRegistryDestroy(IrcitUiWindowRegistry *reg)
{
  if (!reg) return;

  /* The table still comes through the accessor even though this file can now
   * see the struct: that accessor is what keeps the table the one sanctioned
   * way in, and the members read directly here are only the two this file has
   * a reason to write. */
  const IrcitUiWindowTranslationTable *vt = IrcitUiWindowDriverTranslationTable(reg->active_driver);
  if (vt && vt->destroy) {
    IRCITUI_LOG_DEBUG("window driver '%s' shutting down", reg->selected_driver_name);
    vt->destroy(reg->active_driver);
  }
  free(reg);
}

/* Bring a freshly created driver into service: name it, stamp the application's
 * hooks onto it, and announce it.
 *
 * Both success paths go through here rather than repeating the three steps,
 * because a path that stamped nothing would leave the driver's hooks zeroed
 * while the registry still held the real ones -- and every driver created
 * afterwards would inherit the zeros. */
static void
sLockDriver(IrcitUiWindowRegistry *reg, const char *name)
{
  strncpy(reg->selected_driver_name, name, sizeof(reg->selected_driver_name) - 1);
  reg->active_driver->user_lifecycle_callback = reg->user_callbacks;
  IRCITUI_LOG_INFO("window driver locked: '%s'", name);
}

int IrcitUiWindowRegistryRequestDriver(IrcitUiWindowRegistry *reg, const char *driver_name)
{
  if (!reg || !driver_name) return 0;

  IRCITUI_LOG_DEBUG("window driver requested: '%s'", driver_name);

  // --- DISPATCH SWITCHBOARD MATRIX ---
  if (strcmp(driver_name, "ncurses") == 0) {
    // Attempt to spin up the primary high-performance baseline adapter
    reg->active_driver = IrcitUiWindowCreateNcursesAdapter();

    if (reg->active_driver) {
      sLockDriver(reg, "ncurses");
      return 1;
    }

    /* DEPENDENCY FALLBACK SHIELD
     * If the ncurses factory returned NULL (the terminal is not a tty, or the
     * library is absent), do not crash the application. Intercept the error
     * and spin up the ANSI backup driver inline.
     *
     * The caller is told success either way: the request was for a working
     * windowing backend, and it got one. Which backend is answerable through
     * IrcitUiWindowDriverCapabilities, not through the name that was asked for.
     *
     * Logged at WARN rather than INFO because this is the one place the library
     * hands back something other than what it was asked for. That is deliberate
     * and it is safe, but it is not the same event as a plain success, and a log
     * that could not tell the two apart would be worth little. */
    IRCITUI_LOG_WARN("window driver 'ncurses' could not be created; " "falling back to the ANSI backend");
  }

  if (strcmp(driver_name, "ansi") == 0 || reg->active_driver == NULL) {
    reg->active_driver = IrcitUiWindowCreateAnsiFallbackDriver();

    if (reg->active_driver) {
      sLockDriver(reg, "ansi-fallback");
      return 1;
    }
  }

  /* Every route above has returned; reaching here means the request matched
   * nothing usable and no fallback could be built. The caller gets 0, and this
   * is the line that says why. */
  IRCITUI_LOG_ERROR("no window driver could be locked for '%s'", driver_name);
  return 0;
}

const IrcitUiWindowDriver *
IrcitUiWindowRegistryGetActiveDriver(const IrcitUiWindowRegistry *reg)
{
  return reg ? reg->active_driver : NULL;
}

void
IrcitUiWindowRegistryMarshalUserCallbackOnInitialised(const IrcitUiWindowRegistry *reg)
{
  /* Three absences to survive, and every one of them is ordinary rather than a
   * broken caller:
   *
   * reg is NULL for the layout verifier, which initialises the framework with
   * no backend at all. The hook is NULL for a caller that registered none --
   * that is what a zeroed IrcitUiDriverRegistryUserCallbacks means, and it is
   * what src/examples/ui_main.c passes. Testing reg and then calling straight
   * through the hook is a guaranteed SIGSEGV on any such registry, which is
   * exactly how this read before the guard went in.
   *
   * The driver is checked because initialising the framework before a driver
   * has been locked is legal; there is then nothing to be told about, and
   * handing the hook a NULL driver would only move the fault into the
   * application. */
  if (!reg || !reg->active_driver || !reg->user_callbacks.on_driver_initialised) return;

  IRCITUI_LOG_DEBUG("window driver '%s': framework initialised, marshalling the hook", reg->selected_driver_name);
  reg->user_callbacks.on_driver_initialised(reg->active_driver);
}
