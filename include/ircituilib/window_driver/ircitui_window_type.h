/**
 * @file ircitui_window_type.h
 * @brief The windowing abstraction: opaque handles, value types, and the driver
 *        translation table an application drives the backend through.
 *
 * The abstraction framework is intended ultimately to be a true superset interface, however at this stage we are using the
 * capability matrix of ncurses as our  baseline. This means the abstraction layer must natively support hierarchical windows (newwin),
 * relative coordinate-clipping sub-windows (derwin), comprehensive keyboard harvesting (wgetch), full terminal telemetry resizing (resize_term),
 * and low-level mouse pointer event decoding (getmouse).
 *
 * Because we are decoupling through an opaque pointer architecture, we must map out every data layout structure and vtable function pointer to handle this baseline.
 * If an alternative backend lacks relative cell offset positioning (derwin),
 * our abstractions driver will intercept the call and compute those spatial coordinate transformations using software emulation vectors automatically.
 *
 * **What this header hides.** @ref IrcitUiWindow and @ref IrcitUiWindowDriver
 * are opaque: this header names them and never defines them, so a consumer can
 * hold one, pass it, and reach its members only through the sanctioned
 * accessors below. The driver's private state lives behind
 * @ref IrcitUiWindowDriver, whose definition is driver-private, and a window's
 * contents live behind @ref IrcitUiWindow, whose definition belongs to the
 * driver that allocated it — two drivers may lay the same type out differently.
 * The types that *are* defined here are value types: they are passed by value
 * and their fields are the interface rather than an implementation detail.
 *
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

#ifndef IRCITUILIB_IRCITUI_WINDOW_TYPE_H
#define IRCITUILIB_IRCITUI_WINDOW_TYPE_H

#include <stdint.h>
#include <stddef.h>

/* This is a C library, but the header has to be usable from C++ translation
 * units: the test harness compiles its test sources as C++ (LINKER_LANGUAGE
 * CXX). Without these guards a C++ caller emits mangled names against a library
 * that exports the unmangled ones, and the link fails with an undefined
 * reference. ui.h and ircitui_context.h carry the same guards for the same
 * reason. */
#ifdef __cplusplus
extern "C" {
#endif

/* --- Abstract opaque handles.
 *
 * Named here, defined elsewhere. Nothing in this header reveals a member of any
 * of them, so a consumer cannot depend on a layout it has no business knowing. */
typedef struct IrcitUiWindowDriver           IrcitUiWindowDriver;
typedef struct IrcitUiWindow                 IrcitUiWindow;
typedef struct IrcitUiWindowTranslationTable IrcitUiWindowTranslationTable;

/* --- Value types.
 *
 * Open by design, not by oversight. A rectangle and a mouse packet are passed
 * *by value* across the interface, so their fields are the contract: there is
 * no invariant to protect and nothing to hide. Contrast the handles above,
 * whose members are the driver's business. */
typedef struct
{
  int x;
  int y;
  int w;
  int h;
} IrcitUiWindowRect;

typedef enum
{
  IRCITUI_WINDOW_ATTR_NORMAL    = 0,
  IRCITUI_WINDOW_ATTR_BOLD      = (1 << 0),
  IRCITUI_WINDOW_ATTR_DIM       = (1 << 1),
  IRCITUI_WINDOW_ATTR_REVERSE   = (1 << 2),
  IRCITUI_WINDOW_ATTR_UNDERLINE = (1 << 3)
} IrcitUiWindowVideoAttr;

/* --- Decoupled unified mouse event schema --- */
typedef enum
{
  IRCITUI_WINDOW_MOUSE_PRESS_LEFT   = (1 << 0),
  IRCITUI_WINDOW_MOUSE_RELEASE_LEFT = (1 << 1),
  IRCITUI_WINDOW_MOUSE_PRESS_RIGHT  = (1 << 2),
  IRCITUI_WINDOW_MOUSE_WHEEL_UP     = (1 << 3),
  IRCITUI_WINDOW_MOUSE_WHEEL_DOWN   = (1 << 4),
  IRCITUI_WINDOW_MOUSE_MOVEMENT     = (1 << 5)
} IrcitUiWindowMouseAction;

typedef struct
{
  int      x;       /* Global absolute screen grid column coordinates */
  int      y;       /* Global absolute screen grid row coordinates */
  uint32_t buttons; /* Bitmask composed of IrcitUiWindowMouseAction flags */
} IrcitUiWindowMouseEvent;

/* --- The capability matrix.
 *
 * What a backend can actually do, as a bitmask read through
 * @ref IrcitUiWindowDriverCapabilities. A driver sets the bits it honours; an
 * engine tests them before asking for something a backend may not have, rather
 * than discovering the gap by calling a slot that is not bound. */
#define IRCITUI_WINDOW_CAP_TRUE_COLOR   (1u << 0) /**< Honours 24-bit fg/bg rather than quantising to a palette. */
#define IRCITUI_WINDOW_CAP_HARDWARE_DIM (1u << 1) /**< Dim is a real terminal attribute, not simulated by colour. */
#define IRCITUI_WINDOW_CAP_SUBWINDOWS   (1u << 2) /**< Native sub-windows; a driver without it emulates window_create_sub. */

#define IRCITUI_WINDOW_CAP_MOUSE        (1u << 3) /**< Reports mouse packets through fetch_mouse. */

/* --- The translation table.
 *
 * The backend contract. A consumer never constructs one: it asks the registry
 * for a driver and reads the driver's table through
 * @ref IrcitUiWindowDriverTranslationTable. Every slot that operates on a
 * driver takes it `const` — the driver's mutable state lives behind it, in the
 * backend's own allocation, not in the handle. Only @ref destroy consumes the
 * handle, and it is the one slot that takes it non-const.
 *
 * A slot is NULL when this backend does not provide it. Test before calling:
 * this is a superset contract, and no implementation is obliged to fill it. */
struct IrcitUiWindowTranslationTable
{
  /* Global Core Infrastructure Operations.
   * destroy is the sole non-const slot: it consumes the handle. */
  void (*destroy)(IrcitUiWindowDriver *driver);
  int (* clear_screen)(const IrcitUiWindowDriver *driver);
  int (* flush_hardware)(const IrcitUiWindowDriver *driver);
  void (*get_terminal_dimensions)(const IrcitUiWindowDriver *driver, int *out_w, int *out_h);
  void (*set_cursor_visibility)(const IrcitUiWindowDriver *driver, int visible);
  void (*handle_terminal_resize)(const IrcitUiWindowDriver *driver); /* Maps to resize_term */

  /* Advanced Window Lifecycle Topologies (Matches newwin / derwin semantics) */
  /**
   * @brief Instantiates a root, absolute-positioned screen memory window cell matrix (newwin).
   */
  IrcitUiWindow * (*window_create_root)(const IrcitUiWindowDriver *driver, IrcitUiWindowRect geometry);

  /**
   * @brief Instantiates a local relative sub-window child mapped inside a parent container (derwin).
   *        Altering lines inside a sub-window automatically updates the parent's memory layout cells.
   *
   * Takes no driver: the parent already refers to the one that allocated it, so
   * a sub-window is created through its parent rather than through a second
   * lookup — which is also what lets two drivers be live at once.
   */
  IrcitUiWindow * (*window_create_sub)(IrcitUiWindow *parent, IrcitUiWindowRect relative_geometry);

  void (*window_destroy)(IrcitUiWindow *win);
  void (*window_resize)(IrcitUiWindow *win, IrcitUiWindowRect new_geometry);
  void (*window_move)(IrcitUiWindow *win, IrcitUiWindowRect relative_geometry); /* mvderwin */
  void (*window_clear)(IrcitUiWindow *win);
  void (*window_box)(IrcitUiWindow *win, uint32_t vertical_glyph, uint32_t horizontal_glyph);

  /* Text Block Render Primitives */
  int (*draw_char)(IrcitUiWindow *win, int x, int y, uint32_t codepoint, IrcitUiWindowVideoAttr attr, uint32_t fg_rgb,
                   uint32_t       bg_rgb);
  int (*draw_string)(IrcitUiWindow *win, int x, int y, const char *str, IrcitUiWindowVideoAttr attr, uint32_t fg_rgb,
                     uint32_t       bg_rgb);
  void (*move_window_caret)(IrcitUiWindow *win, int x, int y); /* Synchronises blinking cursor placement */

  /* Comprehensive Input Harvesting Interception Pipeline */
  /**
   * @brief Drains keys from the input stream.
   *        Returns an integer keycode code value, 0 for mouse events, or -1 if empty.
   */
  int (*fetch_key)(const IrcitUiWindowDriver *driver);

  /**
   * @brief Unpacks a low-level hardware mouse tracking data packet.
   *        Only invoked if fetch_key() returns 0.
   */
  int (*fetch_mouse)(const IrcitUiWindowDriver *driver, IrcitUiWindowMouseEvent *out_event);
};

/**
 * @brief The backend contract the driver was built with.
 *
 * The sanctioned way to reach the translation table. A consumer holds an opaque
 * @ref IrcitUiWindowDriver and cannot read its members, so this is the only door
 * to the slots; it is also where a NULL return is meaningful — a driver always
 * has a table, so NULL here means the caller passed NULL.
 *
 * @param driver_ptr The driver, as handed back by the registry.
 * @return The table, or NULL when @p driver_ptr is NULL. The table outlives the
 *         driver only in the sense that it is static in the driver's own
 *         translation unit: do not hold it past @ref IrcitUiWindowRegistryDestroy.
 */
const IrcitUiWindowTranslationTable *IrcitUiWindowDriverTranslationTable(const IrcitUiWindowDriver *driver_ptr);

/**
 * @brief What this backend can do, as @c IRCITUI_WINDOW_CAP_* bits.
 *
 * Read it before asking for a capability the backend may not have. A mask of 0
 * means the driver declares nothing, which is legal: the slots are still
 * callable, and a caller that needs true colour must check rather than assume.
 *
 * @param driver_ptr The driver, as handed back by the registry.
 * @return The capability bitmask, or 0 when @p driver_ptr is NULL.
 */
uint32_t IrcitUiWindowDriverCapabilities(const IrcitUiWindowDriver *driver_ptr);

#ifdef __cplusplus
}
#endif

#endif /* IRCITUILIB_IRCITUI_WINDOW_TYPE_H */
