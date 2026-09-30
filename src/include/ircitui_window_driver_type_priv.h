/**
 * @file ircitui_window_driver_type_priv.h
 * @brief Module-private definition of the opaque IrcitUiWindowDriver handle.
 *
 * Not installed and never included from outside the library. The public
 * ircituilib/window_driver/ircitui_window_type.h names IrcitUiWindowDriver and
 * deliberately does not define it; this is where the definition lives, and only
 * the two translation units that must touch a driver's members include this:
 *
 *   - src/window_driver/ircitui_window_driver.c, which implements the two
 *     sanctioned accessors the public header declares;
 *   - the driver adapters, which allocate the handle and reach their own state
 *     through it.
 *
 * The registry does NOT need this header: it holds a driver pointer and hands it
 * back, and the one member it must reach — the translation table, in order to
 * call destroy — comes through the accessor like everyone else's.
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

#ifndef IRCITUILIB_IRCITUI_WINDOW_DRIVER_TYPE_PRIV_H
#define IRCITUILIB_IRCITUI_WINDOW_DRIVER_TYPE_PRIV_H

#include <stdint.h>

#include "ircituilib/window_driver/ircitui_window_type.h"
#include "ircituilib/window_driver/ircitui_driver_registry_user_callbacks_type.h"

/**
 * @struct IrcitUiWindowDriver
 * @brief One instantiated backend, opaque to everything outside the driver layer.
 *
 * Four members, and the split between them is the design:
 *
 * @c vtable is the backend's static contract — the same table every instance of
 * that backend shares, which is why it is a pointer rather than an embedded
 * copy. It is const, so nothing downstream can substitute a slot.
 *
 * @c private_backend_data is the backend's own state, untyped here on purpose:
 * this header does not know, and must not know, whether a driver holds a
 * @c WINDOW* , a file descriptor, or nothing at all. Each adapter casts it to
 * its own context struct. That ownership is why the handle is a distinct
 * allocation from the state rather than one block: the state's layout is private
 * to its adapter, the handle's is private to this file.
 *
 * @c capabilities_bitmask is what the backend claims it can do, as
 * @c IRCITUI_WINDOW_CAP_* bits. It is set once by the factory and read through
 * @ref IrcitUiWindowDriverCapabilities; being here rather than in the state
 * keeps the capability question answerable without knowing the backend.
 *
 * @c user_lifecycle_callback is the application's, not the backend's. The
 * registry copies in what was handed to @ref IrcitUiWindowRegistryCreate and
 * fires both hooks around the driver's life. It sits on the handle rather than
 * in the registry so the driver carries its own record: a backend that wants to
 * notify its host about something it handled itself has one place to look, and
 * the registry never has to consult two sources to know what to call.
 */
struct IrcitUiWindowDriver
{
  const IrcitUiWindowTranslationTable *vtable;
  void *                               private_backend_data;
  uint32_t                             capabilities_bitmask;
  IrcitUiDriverRegistryUserCallbacks   user_lifecycle_callback;
};

#endif /* IRCITUILIB_IRCITUI_WINDOW_DRIVER_TYPE_PRIV_H */
