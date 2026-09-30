/**
 * @file ircitui_window_registry.h
 * @brief Startup driver selection and lifecycle: the application's entry point
 *        into the windowing abstraction.
 *
 * An application evaluates its startup choices — a command-line flag, a config
 * file, a default — and hands the chosen backend's name to
 * @ref IrcitUiWindowRegistryRequestDriver. It then reads the locked driver back
 * with @ref IrcitUiWindowRegistryGetActiveDriver and drives it through
 * @ref IrcitUiWindowDriverTranslationTable. Nowhere in that path does the
 * application name a backend, include its header, or link its library.
 *
 * The registry handle is opaque: this header names @ref IrcitUiWindowRegistry
 * and never defines it, so a caller owns one without being able to depend on how
 * it is laid out. @ref IrcitUiWindowDriver is opaque for the same reason.
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

#ifndef IRCITUILIB_IRCITUI_WINDOW_REGISTRY_H
#define IRCITUILIB_IRCITUI_WINDOW_REGISTRY_H

#include "ircituilib/window_driver/ircitui_window_type.h"
#include "ircituilib/window_driver/ircitui_driver_registry_user_callbacks_type.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Global runtime driver manager handle context token.
 *
 * Opaque. One registry owns at most one active driver for its lifetime; holding
 * the driver itself is not a substitute for holding the registry, because the
 * registry is what tears the driver down.
 */
typedef struct IrcitUiWindowRegistry IrcitUiWindowRegistry;

/**
 * @brief Initializes the driver registry, with the application's lifecycle hooks.
 *
 * Allocates a registry with no driver locked, remembering @p user_callbacks for
 * every driver it will go on to create. It owns nothing until
 * @ref IrcitUiWindowRegistryRequestDriver succeeds, so destroying one that was
 * never asked for a driver is legal and simply frees the handle — and the
 * shutdown hook is not called, because no driver was ever initialised.
 *
 * The argument is taken **by value** and copied. A caller with nothing to be
 * told passes a zeroed struct, and both hooks are then simply never invoked;
 * the two members are independent, so a caller may supply either alone.
 *
 * @param user_callbacks The application's hooks, copied into the registry and
 *        onto each driver. May be zeroed.
 * @return The registry, or NULL when allocation fails.
 */
IrcitUiWindowRegistry *IrcitUiWindowRegistryCreate(IrcitUiDriverRegistryUserCallbacks user_callbacks);

/**
 * @brief Safely shuts down the active driver and frees registry memory spaces.
 *
 * Calls the driver's @c destroy slot, which is where a backend releases its
 * terminal — a driver that ran `initscr` runs `endwin` here. Anything a caller
 * obtained from the driver — the translation table, its windows — is invalid
 * afterwards. Passing NULL is legal and does nothing.
 *
 * @param reg The registry to destroy; may be NULL.
 */
void IrcitUiWindowRegistryDestroy(IrcitUiWindowRegistry *reg);

/**
 * @brief Requests and hooks a driver at application startup based on a text string identifier.
 *        Evaluates features and falls back to a safe backup if dependencies are missing.
 *
 * A request for a backend whose library is absent does not fail outright: the
 * registry substitutes the fallback backend and still returns success. A caller
 * that must know which backend it actually got compares
 * @ref IrcitUiWindowDriverCapabilities, not the name it asked for.
 *
 * @param reg The registry to lock a driver into.
 * @param driver_name "ncurses", "ansi", or "headless"
 * @return 1 on successful initialization, 0 if no valid driver could be locked.
 */
int IrcitUiWindowRegistryRequestDriver(IrcitUiWindowRegistry *reg, const char *driver_name);

/**
 * @brief Fetches the active, locked polymorphic driver instance for runtime engine passes.
 *
 * Returned `const` deliberately: a caller drives the backend through the
 * translation table and reads its capabilities through the accessor, and has no
 * business rewriting the handle. The registry keeps the writable pointer and is
 * the only thing that mutates or frees it.
 *
 * @param reg The registry to read; may be NULL.
 * @return The locked driver, or NULL if none was requested or the request failed.
 */
const IrcitUiWindowDriver *IrcitUiWindowRegistryGetActiveDriver(const IrcitUiWindowRegistry *reg);

/**
 * @brief Re-invoke the registered initialised hook for the locked driver.
 *
 * For a consumer that built the framework after locking the driver and wants
 * the application told at *that* point rather than at lock time. Called by
 * @ref IrcitUiInitialiseFrameworkContext, which is the framework's own moment
 * of readiness.
 *
 * Does nothing, without fault, when any of the three is absent: a NULL
 * registry (the layout verifier initialises the framework with no backend), a
 * registry with no driver locked, or a caller that registered no hook — which
 * is what a zeroed @ref IrcitUiDriverRegistryUserCallbacks means.
 *
 * @param reg The registry to marshal from; may be NULL.
 */
void IrcitUiWindowRegistryMarshalUserCallbackOnInitialised(const IrcitUiWindowRegistry *reg);

#ifdef __cplusplus
}
#endif

#endif /* IRCITUILIB_IRCITUI_WINDOW_REGISTRY_H */
