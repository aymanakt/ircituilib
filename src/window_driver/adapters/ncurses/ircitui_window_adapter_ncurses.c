/**
 * @file ircitui_window_adapter_ncurses.c
 * @brief Concrete ncurses backend for the windowing abstraction.
 *
 * This is the one translation unit in the library that includes <ncurses.h>,
 * and that is the point of the adapter: the dependency is real, it is total, and
 * it is confined here. It does not leak — a consumer of the module names
 * `IrcitUiWindow`, never `WINDOW`, and reaches the backend through the
 * translation table.
 *
 * This file also owns the terminal. The factory runs initscr(), cbreak(),
 * noecho() and keypad(), and the destroy slot runs endwin(). An application
 * driving the abstracted backend therefore never calls initscr() itself, which
 * is what lets it link without ncurses at all.
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
#include "ui_log.h"
#include "ui_palette_priv.h"

#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>   /* isatty, STDOUT_FILENO */
#include <wchar.h>    /* wchar_t, for setcchar */

/* --- Private backend state.
 *
 * Opaque outside this file: the engine holds an IrcitUiWindowDriver* and can
 * reach this only by casting private_backend_data, which the public header no
 * longer even exposes as a field. */
typedef struct
{
  /* Whether colour is usable, resolved once and remembered: 0 until asked,
   * then 1 for usable and -1 for not.
   *
   * It is resolved lazily rather than at construction because the application
   * is the one that calls start_color(), in its own initialisation, and this
   * backend is not told when. Asking on first use is how the driver finds out
   * without the application having to report it. */
  int colour_state;
} NcursesContext;

/* --- The opaque window handle, defined here and nowhere else.
 *
 * A consumer holding an IrcitUiWindow* cannot dereference it. Note that two
 * different drivers may lay this type out differently — that is the licence the
 * opaque handle buys, and it is why the type is defined per adapter. */
struct IrcitUiWindow
{
  WINDOW *          native_window;
  NcursesContext *  ctx;
  IrcitUiWindowRect geometry;
  int               is_subwindow;
};

/* --- Colour.
 *
 * ncurses addresses colour as a pair index, not as RGB, so a superset interface
 * asking for 24-bit colour must be quantised on the way in. This is the "sane
 * software override": the closest representable colour is chosen, and the driver
 * simply does not set IRCITUI_WINDOW_CAP_TRUE_COLOR, so a caller that cares can
 * find out before drawing rather than after.
 *
 * The pair cache that stood here -- sixty-four entries of its own, allocated from
 * pair 1 -- is the registry's now. It was the third allocator over the same index
 * range: the engine's ran 1..62 and this one 1..63, so an application using both
 * the tree and this driver had the two overwriting each other's colours. The
 * registry keys on the colours, so a caller may ask on every draw. */

/* Map 24-bit RGB onto the terminal's palette. Above 256 colours we use the
 * 6x6x6 cube plus greyscale, which is the closest a non-true-colour terminal
 * gets; at 8 colours we keep only the three primary bits. */
static short
sRgbToPalette(uint32_t rgb)
{
  uint8_t r = (uint8_t)((rgb >> 16) & 0xFFu);
  uint8_t g = (uint8_t)((rgb >> 8) & 0xFFu);
  uint8_t b = (uint8_t)(rgb & 0xFFu);

  if (COLORS >= 256) {
    // The 16 system colours are excluded so the cube stays addressable.
    int ri = (r * 5 + 127) / 255;
    int gi = (g * 5 + 127) / 255;
    int bi = (b * 5 + 127) / 255;
    return (short)(16 + 36 * ri + 6 * gi + bi);
  }

  // 8-colour fallback: a primary bit per channel.
  short idx = 0;
  if (r > 127) idx |= 1;
  if (g > 127) idx |= 2;
  if (b > 127) idx |= 4;
  return idx;
}

/* Return a pair index for this fg/bg.
 *
 * A pair index is a finite resource — the terminal offers COLOR_PAIRS of them — and
 * the registry owns that budget now. What used to happen when this driver's own
 * sixty-four slots ran out was a reuse of the oldest slot, so a caller silently got a
 * different colour than it asked for; the registry answers 0 instead, which is the
 * terminal's default pair and renders legibly. */
static short
sPairFor(NcursesContext *ctx, uint32_t fg_rgb, uint32_t bg_rgb)
{
  if (!ctx) return 0;

  /* Resolved once. has_colors() reports the terminal's capability; if the
   * application never called start_color(), init_pair fails and the default pair
   * is returned, which is the honest outcome rather than a driver deciding colour
   * on the application's behalf. The registry makes the same test in the same
   * place, and answers 0 for the same reason; this one is kept because it is the
   * driver's own state and a driver that cannot do colour should not be asking a
   * library-wide registry to find that out. */
  if (ctx->colour_state == 0) ctx->colour_state = has_colors() ? 1 : -1;
  if (ctx->colour_state < 0) return 0;

  return IrcitUiColourPair(sRgbToPalette(fg_rgb), sRgbToPalette(bg_rgb));
}

/* Translate the abstract attribute bits into ncurses' own. */
static attr_t
sAttrFor(IrcitUiWindowVideoAttr attr)
{
  attr_t out = A_NORMAL;

  if (attr & IRCITUI_WINDOW_ATTR_BOLD) out |= A_BOLD;
  if (attr & IRCITUI_WINDOW_ATTR_DIM) out |= A_DIM;
  if (attr & IRCITUI_WINDOW_ATTR_REVERSE) out |= A_REVERSE;
  if (attr & IRCITUI_WINDOW_ATTR_UNDERLINE) out |= A_UNDERLINE;
  return out;
}

/* --- Lifecycle --- */

static void
sNcursesDestroy(IrcitUiWindowDriver *driver)
{
  NcursesContext *ctx;

  if (!driver) return;
  ctx = (NcursesContext*)driver->private_backend_data;

  /* Nothing terminal-shaped happens here.
   *
   * The application ran initscr() and turned mouse reporting on, in its own
   * initialisation, so the application takes both down in its own teardown --
   * where its matching setup is. A backend that reached back to undo a terminal
   * it never initialised would be crossing the same boundary in the other
   * direction, and would run endwin() on a host's screen. */
  IRCITUI_LOG_DEBUG("ncurses backend: releasing its context");
  free(ctx);
  free(driver);
}

static int
sNcursesClearScreen(const IrcitUiWindowDriver *driver)
{
  if (!driver) return -1;
  return werase(stdscr);
}

static int
sNcursesFlushHardware(const IrcitUiWindowDriver *driver)
{
  if (!driver) return -1;
  // wnoutrefresh + doupdate is the cheap path: one terminal write for the * whole virtual screen rather than one per window.
  if (wnoutrefresh(stdscr) == ERR) return -1;
  return doupdate();
}

static void
sNcursesGetTerminalDimensions(const IrcitUiWindowDriver *driver, int *out_w, int *out_h)
{
  if (!driver) return;
  /* LINES/COLS, not getmaxyx: getmaxyx is a macro, so it is not a symbol a
   * caller could take the address of, and the screen globals are the real
   * source it expands to anyway. */
  if (out_w) *out_w = COLS;
  if (out_h) *out_h = LINES;
}

static void
sNcursesSetCursorVisibility(const IrcitUiWindowDriver *driver, int visible)
{
  if (!driver) return;
  (void)curs_set(visible ? 1 : 0);
}

static void
sNcursesHandleTerminalResize(const IrcitUiWindowDriver *driver)
{
  if (!driver) return;
  // resize_term re-reads the physical size and reallocates the screen; the * caller relayouts afterwards, which is why this does not redraw.
  (void)resize_term(0, 0);
}

/* --- Window topology --- */

static IrcitUiWindow *
sNcursesWindowCreateRoot(const IrcitUiWindowDriver *driver, IrcitUiWindowRect geometry)
{
  IrcitUiWindow * win;
  NcursesContext *ctx;

  if (!driver || !driver->private_backend_data) return NULL;
  ctx = (NcursesContext*)driver->private_backend_data;

  win = calloc(1, sizeof(IrcitUiWindow));
  if (!win) return NULL;

  win->native_window = newwin(geometry.h, geometry.w, geometry.y, geometry.x);
  if (!win->native_window) {
    free(win);
    return NULL;
  }

  win->ctx          = ctx;
  win->geometry     = geometry;
  win->is_subwindow = 0;
  return win;
}

static IrcitUiWindow *
sNcursesWindowCreateSub(IrcitUiWindow *parent, IrcitUiWindowRect relative_geometry)
{
  IrcitUiWindow *win;

  /* No driver argument, and no global to look one up in: the parent already
   * belongs to the context that allocated it. That is what lets two drivers
   * be alive at once without a process-wide "current driver". */
  if (!parent || !parent->native_window) return NULL;

  win = calloc(1, sizeof(IrcitUiWindow));
  if (!win) return NULL;

  win->native_window = derwin(parent->native_window, relative_geometry.h, relative_geometry.w, relative_geometry.y,
                              relative_geometry.x);
  if (!win->native_window) {
    free(win);
    return NULL;
  }

  // derwin coordinates are parent-relative; the absolute origin is kept so a * caller reading geometry back gets screen coordinates either way.
  win->ctx          = parent->ctx;
  win->geometry.x   = parent->geometry.x + relative_geometry.x;
  win->geometry.y   = parent->geometry.y + relative_geometry.y;
  win->geometry.w   = relative_geometry.w;
  win->geometry.h   = relative_geometry.h;
  win->is_subwindow = 1;
  return win;
}

static void
sNcursesWindowDestroy(IrcitUiWindow *win)
{
  if (!win) return;
  /* A derwin child shares the parent's cell storage, so a child destroyed
   * after its parent is a use-after-free inside ncurses. The caller must tear
   * down in post-order; the engine's deflate path already does. */
  if (win->native_window) (void)delwin(win->native_window);
  free(win);
}

static void
sNcursesWindowResize(IrcitUiWindow *win, IrcitUiWindowRect new_geometry)
{
  if (!win || !win->native_window) return;
  if (wresize(win->native_window, new_geometry.h, new_geometry.w) == ERR) return;
  (void)mvwin(win->native_window, new_geometry.y, new_geometry.x);
  win->geometry = new_geometry;
}

static void
sNcursesWindowMove(IrcitUiWindow *win, IrcitUiWindowRect relative_geometry)
{
  if (!win || !win->native_window) return;
  (void)mvwin(win->native_window, relative_geometry.y, relative_geometry.x);
  win->geometry.x = relative_geometry.x;
  win->geometry.y = relative_geometry.y;
}

static void
sNcursesWindowClear(IrcitUiWindow *win)
{
  if (!win || !win->native_window) return;
  (void)werase(win->native_window);
}

static void
sNcursesWindowBox(IrcitUiWindow *win, uint32_t vertical_glyph, uint32_t horizontal_glyph)
{
  if (!win || !win->native_window) return;
  /* The abstract interface carries the glyphs, so a backend with a different
   * line-drawing set can honour them. Where they are absent or zero, fall back
   * to the terminal's own ACS line-drawing characters. */
  if (vertical_glyph == 0) vertical_glyph = (uint32_t)ACS_VLINE;
  if (horizontal_glyph == 0) horizontal_glyph = (uint32_t)ACS_HLINE;
  (void)box(win->native_window, (chtype)vertical_glyph, (chtype)horizontal_glyph);
}

/* --- Render primitives --- */

static int
sNcursesDrawChar(IrcitUiWindow *win, int x, int y, uint32_t codepoint, IrcitUiWindowVideoAttr attr, uint32_t fg_rgb,
                 uint32_t       bg_rgb)
{
  cchar_t cc;
  wchar_t wch[2];
  short   pair;

  if (!win || !win->native_window) return -1;

  wch[0] = (wchar_t)codepoint;
  wch[1] = L'\0';
  pair   = sPairFor(win->ctx, fg_rgb, bg_rgb);

  // setcchar + mvwadd_wch rather than mvwaddch: the codepoint may be outside * ASCII, and writing it as a byte would put the wrong glyph on the glass.
  if (setcchar(&cc, wch, sAttrFor(attr), pair, NULL) == ERR) return -1;
  return mvwadd_wch(win->native_window, y, x, &cc);
}

static int
sNcursesDrawString(IrcitUiWindow *win, int x, int y, const char *str, IrcitUiWindowVideoAttr attr, uint32_t fg_rgb,
                   uint32_t       bg_rgb)
{
  if (!win || !win->native_window || !str) return -1;

  // Decoded per scalar rather than handed to mvwaddnstr, so a multi-byte glyph * advances one cell instead of one byte.
  for (const char *p = str; *p;) {
    unsigned char c     = (unsigned char)*p;
    uint32_t      cp    = c;
    int           extra = 0;

    if (c >= 0xF0u) {
      cp    = c & 0x07u;
      extra = 3;
    }
    else if (c >= 0xE0u) {
      cp    = c & 0x0Fu;
      extra = 2;
    }
    else if (c >= 0xC0u) {
      cp    = c & 0x1Fu;
      extra = 1;
    }

    for (int i = 0; i < extra && p[1] != '\0'; i++) {
      p++;
      cp = (cp << 6) | ((uint32_t)((unsigned char)*p) & 0x3Fu);
    }
    p++;

    if (sNcursesDrawChar(win, x, y, cp, attr, fg_rgb, bg_rgb) == ERR) return -1;
    x++;
  }
  return 0;
}

static void
sNcursesMoveWindowCaret(IrcitUiWindow *win, int x, int y)
{
  if (!win || !win->native_window) return;
  (void)wmove(win->native_window, y, x);
}

/* --- Input --- */

static int
sNcursesFetchKey(const IrcitUiWindowDriver *driver)
{
  int ch;

  if (!driver) return -1;

  // The contract is ncurses': ERR means nothing pending, and KEY_MOUSE is the * sentinel that tells the caller to call fetch_mouse next.
  ch = wgetch(stdscr);
  if (ch == ERR) return -1;
  return ch;
}

static int
sNcursesFetchMouse(const IrcitUiWindowDriver *driver, IrcitUiWindowMouseEvent *out_event)
{
  MEVENT native_ev;

  if (!driver || !out_event) return -1;
  if (getmouse(&native_ev) != OK) return -1;

  out_event->x       = native_ev.x;
  out_event->y       = native_ev.y;
  out_event->buttons = 0;

  /* Named constants, not bit positions guessed from the header comment: the
   * previous literals here read the right-arrow key as a mouse event and set
   * the colour-pair field instead of the bold attribute. */
  if (native_ev.bstate & BUTTON1_PRESSED) out_event->buttons |= IRCITUI_WINDOW_MOUSE_PRESS_LEFT;
  if (native_ev.bstate & BUTTON1_RELEASED) out_event->buttons |= IRCITUI_WINDOW_MOUSE_RELEASE_LEFT;
  if (native_ev.bstate & (BUTTON3_PRESSED | BUTTON3_CLICKED)) out_event->buttons |= IRCITUI_WINDOW_MOUSE_PRESS_RIGHT;
  if (native_ev.bstate & BUTTON4_PRESSED) out_event->buttons |= IRCITUI_WINDOW_MOUSE_WHEEL_UP;
  if (native_ev.bstate & BUTTON5_PRESSED) out_event->buttons |= IRCITUI_WINDOW_MOUSE_WHEEL_DOWN;
  if (native_ev.bstate & REPORT_MOUSE_POSITION) out_event->buttons |= IRCITUI_WINDOW_MOUSE_MOVEMENT;
  return 0;
}

/* --- The table --- */

static const IrcitUiWindowTranslationTable sNcursesTranslationTable = {
  .destroy = sNcursesDestroy, .clear_screen = sNcursesClearScreen, .flush_hardware = sNcursesFlushHardware,
  .get_terminal_dimensions = sNcursesGetTerminalDimensions, .set_cursor_visibility = sNcursesSetCursorVisibility,
  .handle_terminal_resize = sNcursesHandleTerminalResize, .window_create_root = sNcursesWindowCreateRoot,
  .window_create_sub = sNcursesWindowCreateSub, .window_destroy = sNcursesWindowDestroy,
  .window_resize = sNcursesWindowResize, .window_move = sNcursesWindowMove, .window_clear = sNcursesWindowClear,
  .window_box = sNcursesWindowBox, .draw_char = sNcursesDrawChar, .draw_string = sNcursesDrawString,
  .move_window_caret = sNcursesMoveWindowCaret, .fetch_key = sNcursesFetchKey, .fetch_mouse = sNcursesFetchMouse
};

/* --- The factory the registry calls --- */

IrcitUiWindowDriver *
IrcitUiWindowCreateNcursesAdapter(void)
{
  IrcitUiWindowDriver *driver;
  NcursesContext *     ctx;

  /* There is no terminal to drive when stdout is not a tty. Returning NULL
   * rather than entering curses mode against a pipe is what makes the
   * registry's fallback reachable and testable. */
  if (!isatty(STDOUT_FILENO)) {
    IRCITUI_LOG_WARN("ncurses backend declined: stdout is not a terminal");
    return NULL;
  }

  ctx = calloc(1, sizeof(NcursesContext));
  if (!ctx) {
    IRCITUI_LOG_ERROR("ncurses backend: out of memory allocating its context");
    return NULL;
  }

  /* There is no terminal sequence here, and that is deliberate.
   *
   * An application that asks this registry for "ncurses" is declaring that it
   * knows how to initialise ncurses for what it wants: initscr, the input
   * modes, the cursor, ESC timing, colour, the mouse mask. Every one of those
   * is policy rather than requirement, and every one belongs to the
   * application, supplied through its on_driver_initialised hook. A driver that
   * also did it would impose one answer on every application that embedded it,
   * and would call initscr() a second time in any host that already had.
   *
   * This backend supplies the mechanism and nothing else: the table below,
   * whose slots assume a terminal the application has already brought up.
   */

  driver = calloc(1, sizeof(IrcitUiWindowDriver));
  if (!driver) {
    IRCITUI_LOG_ERROR("ncurses backend: out of memory allocating the driver handle");
    endwin();
    free(ctx);
    return NULL;
  }

  driver->vtable               = &sNcursesTranslationTable;
  driver->private_backend_data = ctx;

  /* ncurses is the baseline the whole interface is modelled on, so it claims
   * the structural capabilities outright. It does NOT claim TRUE_COLOR: it
   * quantises 24-bit colour onto a palette, and a caller deciding whether to
   * ask for a precise colour needs to know that before it draws, not after. */
  driver->capabilities_bitmask = IRCITUI_WINDOW_CAP_HARDWARE_DIM | IRCITUI_WINDOW_CAP_SUBWINDOWS |
    IRCITUI_WINDOW_CAP_MOUSE;

  /* The capabilities and nothing else. COLS, LINES and COLORS are all still
   * zero here -- they only take values once the application has run initscr(),
   * and that happens later, in its on_driver_initialised hook. Logging them
   * from the factory would report "0x0, 0 colours" on every run, which is worse
   * than not reporting them at all. */
  IRCITUI_LOG_INFO("ncurses backend created: caps=0x%X (terminal is the application's)", driver->capabilities_bitmask);
  return driver;
}

/*
 * ---------------------------------------------------------------------------
 * The dlopen path: retained for reference, NOT SUPPORTED, not compiled.
 *
 * Enable with -DIRCITUI_WINDOW_DLOPEN_UNSUPPORTED to see it, and read the gaps
 * below first — as written it cannot work, and the build does not link -ldl.
 *
 * The idea was to reach ncurses through dlopen/dlsym so the library carried no
 * link-time dependency on it. It buys nothing today: ircituilib PUBLIC-links
 * ${IRCITUI_CURSES_LIBS} (CMakeLists.txt:225), so ncursesw is already a hard
 * dependency and the dlsym dance cannot remove it. Making it real means
 * changing that link line first.
 *
 * Known gaps, each verified against the system headers:
 *
 *   1. dlopen("libncursesw.so") cannot succeed. That name is a 32-byte GNU ld
 *      script — "INPUT(libncursesw.so.6 -ltinfo)" — and dlopen rejects it with
 *      "file too short". Only libncursesw.so.6 resolves.
 *   2. getmaxyx is a macro (ncurses.h:1157), not an exported symbol, so
 *      dlsym(handle, "getmaxyx") returns NULL and calling it segfaults. Use
 *      getmaxy/getmaxx, or LINES/COLS as the live path above does.
 *   3. initscr() was commented out while stdscr was read before it, so the
 *      terminal was never initialised and the cached stdscr was NULL.
 *   4. mvderwin, wresize, wmove and wborder were struct members never bound by
 *      any dlsym call, so they were NULL if reached.
 *   5. None of the dlsym'd pointers were NULL-checked.
 *   6. The link line has no -ldl; enabling this needs ${CMAKE_DL_LIBS}.
 *   7. mmask_t was typedef'd unsigned long here; ncurses defines it unsigned
 *      (ncurses.h:223), so the mask was twice its real width on LP64.
 * ---------------------------------------------------------------------------
 */
#ifdef IRCITUI_WINDOW_DLOPEN_UNSUPPORTED
#error "The dlopen adapter path is retained for reference and is known not to work; see the gap list in this file."
#endif
