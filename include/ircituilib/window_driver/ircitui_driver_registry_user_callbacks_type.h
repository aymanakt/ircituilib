/**
 * @file ircitui_driver_registry_user_callbacks_type.h
 * @brief The application's lifecycle hooks for a window driver.
 *
 * Handed to @ref IrcitUiWindowRegistryCreate, retained by the registry, and
 * copied onto every driver it creates. They are the application's way of
 * learning that a backend came up and that it is going away, without polling
 * the registry and without the backend knowing an application exists.
 *
 * Both are optional and independently so: a caller with nothing to say passes
 * a NULL member, and the registry simply does not call it.
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

#ifndef IRCITUILIB_IRCITUI_DRIVER_REGISTRY_USER_CALLBACKS_TYPE_H
#define IRCITUILIB_IRCITUI_DRIVER_REGISTRY_USER_CALLBACKS_TYPE_H

#include "ircituilib/window_driver/ircitui_window_type.h"

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * @struct IrcitUiDriverRegistryUserCallbacks
 * @brief Two lifecycle notifications, both optional.
 *
 * Passed **by value** to @ref IrcitUiWindowRegistryCreate. By value rather than
 * by pointer because it is two words of function pointer that the registry
 * copies: there is nothing to share, nothing to free, and no lifetime for a
 * caller to get wrong. The registry keeps its own copy, so the struct a caller
 * passes may go out of scope the moment the call returns.
 *
 * The driver handed to either callback is the same opaque handle the registry
 * returns. It carries no user-data pointer, matching @ref IrcitUiEventCallback
 * and @ref IrcitUiKeyEventCallback elsewhere in this library: an application
 * that needs its own state reaches it the way it reaches it from any other
 * callback here.
 *
 * Neither callback may destroy the registry it was called from. Shutdown in
 * particular is invoked while the registry is still tearing its driver down.
 */
typedef struct IrcitUiDriverRegistryUserCallbacks
{
  /**
   * @brief The application's hook for a driver reaching its usable state.
   *
   * What an application does here is its own: configure the backend it has been
   * handed, read the capabilities it claimed, or set up state that outlives the
   * call. The driver passed in is the same opaque handle the registry returns.
   */
  void (*on_driver_initialised)(IrcitUiWindowDriver *driver_ptr);

  /**
   * @brief The application's hook for a driver going away.
   *
   * The counterpart to @ref on_driver_initialised: whatever an application set
   * up there is unwound here.
   */
  void (*on_driver_shutdown)(IrcitUiWindowDriver *driver_ptr);
} IrcitUiDriverRegistryUserCallbacks;

#ifdef __cplusplus
}
#endif

#endif /* IRCITUILIB_IRCITUI_DRIVER_REGISTRY_USER_CALLBACKS_TYPE_H */
