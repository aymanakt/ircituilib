/**
 * @file ircitui_window_driver.c
 * @brief The sanctioned accessors for the opaque IrcitUiWindowDriver handle.
 *
 * This translation unit exists for one reason: it is the only file outside a
 * driver adapter allowed to include the private definition of
 * IrcitUiWindowDriver, and it spends that privilege on two functions whose whole
 * job is to hand a caller something it may legitimately see.
 *
 * The alternative — letting the public header define the struct — would have
 * made every consumer able to read and write @c private_backend_data and
 * @c capabilities_bitmask directly. The first is the backend's own state behind
 * a @c void* ; the second is the claim the entire superset contract rests on.
 * Neither should be a public field.
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

#include "ircituilib/window_driver/ircitui_window_type.h"

#include "ircitui_window_driver_type_priv.h"

const IrcitUiWindowTranslationTable *
IrcitUiWindowDriverTranslationTable(const IrcitUiWindowDriver *driver_ptr)
{
  /* NULL in, NULL out rather than a dereference: a caller that has not yet
   * been handed a driver reaches here first, and discovering that by crash is
   * a worse contract than discovering it by return value. */
  return driver_ptr ? driver_ptr->vtable : NULL;
}

uint32_t
IrcitUiWindowDriverCapabilities(const IrcitUiWindowDriver *driver_ptr)
{
  /* Zero — "declares nothing" — rather than a dereference. A caller that
   * needs true colour tests the bit and takes the emulated path, which is the
   * correct behaviour against a driver that was never successfully created. */
  return driver_ptr ? driver_ptr->capabilities_bitmask : 0u;
}
