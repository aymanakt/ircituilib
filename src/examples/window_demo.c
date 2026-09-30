/**
 * @file window_demo.c
 * @brief Drives the windowing abstraction from the application side.
 *
 * This program is what an application looks like against the abstracted
 * windowing layer. It:
 *
 *   1. evaluates a startup choice — @c --window-driver=NAME
 *   2. hands that name to the registry, which resolves and initialises a backend
 *   3. reads the locked driver's capabilities and translation table back
 *   4. paints through the table, with no reference to any backend
 *
 * **What it does not include is the point.** No <ncurses.h>, no ncurses symbol,
 * no backend header. It links ircituilib and nothing else curses-shaped; the
 * ncurses adapter opens and closes the terminal on its behalf. That is the
 * module's claim, and this file is where it is either true or it is not.
 *
 * It is deliberately a *consumer*: it is given no private include directory, so
 * the compiler enforces the boundary this file is asserting. Anything it can
 * reach, an application can reach.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>   /* usleep */

/* The entire API surface this program is allowed to know. */
#include "ircituilib/window_driver/ircitui_window_registry.h"
#include "ircituilib/window_driver/ircitui_window_type.h"
/* For IrcitUiLogInit/Fini. Also public API — the demo is still a pure consumer. */
#include "ircituilib/ui.h"

typedef struct
{
  const char *driver_name; /**< Backend to request; NULL means the default. */
  int         hold_ms;     /**< How long to leave the frame on the glass. */
  const char *log_file;    /**< NULL leaves logging closed. */
} DemoOptions;

enum
{
  OPT_WINDOW_DRIVER = 256,
  OPT_HOLD_MS,
  OPT_LOG_FILE,
  OPT_HELP
};

static const struct option s_long_opts[] = {
  {"window-driver", required_argument, NULL, OPT_WINDOW_DRIVER}, {"hold-ms", required_argument, NULL, OPT_HOLD_MS},
  {"log-file", required_argument, NULL, OPT_LOG_FILE}, {"help", no_argument, NULL, OPT_HELP}, {NULL, 0, NULL, 0}
};

static void
sUsage(FILE *out_ptr, const char *prog_ptr)
{
  fprintf(out_ptr,
          "usage: %s [--window-driver=NAME] [--hold-ms=N]\n" "\n"
          "  --window-driver=NAME  backend to request: ncurses (default) or ansi.\n"
          "                        A backend that cannot start falls back, and the\n"
          "                        capabilities below report what was actually locked.\n"
          "  --hold-ms=N           leave the frame on the glass for N ms (default 900)\n"
          "  --log-file=PATH       write the library's own log there. Without it the\n"
          "                        registry and the backends log to nowhere: they call\n"
          "                        IRCITUI_LOG_*, which is a no-op until IrcitUiLogInit\n"
          "                        has run. This is how you see which backend was locked\n"
          "                        and what it claimed.\n", prog_ptr);
}

static int
sParseArgs(int argc, char **argv_ptr, DemoOptions *opts_ptr)
{
  int opt;

  while ((opt = getopt_long(argc, argv_ptr, "", s_long_opts, NULL)) != -1) {
    switch (opt) {
    case OPT_WINDOW_DRIVER:
      opts_ptr->driver_name = optarg;
      break;
    case OPT_HOLD_MS:
      opts_ptr->hold_ms = atoi(optarg);
      break;
    case OPT_LOG_FILE:
      opts_ptr->log_file = optarg;
      break;
    case OPT_HELP:
      sUsage(stdout, argv_ptr[0]);
      return 1;
    default:
      sUsage(stderr, argv_ptr[0]);
      return -1;
    }
  }
  return 0;
}

/**
 * @brief Report what the locked backend declares it can do.
 *
 * The abstracted answer to "what am I talking to". Note it is asked of the
 * driver, not inferred from the name that was requested — a request that fell
 * back still reports honestly, because the mask comes from the driver that was
 * actually built.
 */
static void
sReportCapabilities(const IrcitUiWindowDriver *driver_ptr)
{
  uint32_t caps = IrcitUiWindowDriverCapabilities(driver_ptr);

  printf("  declared capabilities:\n");
  printf("    true 24-bit colour : %s\n", (caps & IRCITUI_WINDOW_CAP_TRUE_COLOR) ? "yes" : "no (quantised)");
  printf("    hardware dim       : %s\n", (caps & IRCITUI_WINDOW_CAP_HARDWARE_DIM) ? "yes" : "no");
  printf("    native sub-windows : %s\n", (caps & IRCITUI_WINDOW_CAP_SUBWINDOWS) ? "yes" : "no (emulated)");
  printf("    mouse reporting    : %s\n", (caps & IRCITUI_WINDOW_CAP_MOUSE) ? "yes" : "no");
}

/**
 * @brief Paint one frame through the translation table.
 *
 * Nothing here names a backend. Every call is a slot on the table the driver
 * handed back, which is how the same code drives ncurses on a tty and raw
 * escape sequences down a pipe.
 */
static int
sPaint(const IrcitUiWindowTranslationTable *vt_ptr, const IrcitUiWindowDriver *driver_ptr)
{
  IrcitUiWindowRect root_rect = {.x = 2, .y = 1, .w = 52, .h = 9};
  IrcitUiWindowRect sub_rect  = {.x = 2, .y = 6, .w = 46, .h = 2};
  IrcitUiWindow *   root;
  IrcitUiWindow *   sub;
  int               cols = 0;
  int               rows = 0;
  int               rc   = 0;

  if (!vt_ptr->get_terminal_dimensions || !vt_ptr->window_create_root) {
    fprintf(stderr, "  backend does not provide the window primitives\n");
    return 1;
  }

  vt_ptr->get_terminal_dimensions(driver_ptr, &cols, &rows);
  printf("  terminal is %dx%d cells\n", cols, rows);

  if (vt_ptr->clear_screen) vt_ptr->clear_screen(driver_ptr);
  if (vt_ptr->set_cursor_visibility) vt_ptr->set_cursor_visibility(driver_ptr, 0);

  root = vt_ptr->window_create_root(driver_ptr, root_rect);
  if (!root) {
    fprintf(stderr, "  window_create_root returned NULL\n");
    return 1;
  }

  if (vt_ptr->window_box) vt_ptr->window_box(root, 0, 0);

  if (vt_ptr->draw_string) {
    vt_ptr->draw_string(root, 2, 1, "ircituilib windowing abstraction", IRCITUI_WINDOW_ATTR_BOLD, 0x00C8FFu, 0x000000u);
    vt_ptr->draw_string(root, 2, 3, "one translation table, several backends", IRCITUI_WINDOW_ATTR_NORMAL, 0x808080u,
                        0x000000u);
    vt_ptr->draw_string(root, 2, 4, "the application names no backend", IRCITUI_WINDOW_ATTR_NORMAL, 0x808080u,
                        0x000000u);
  }
  else {
    /* Not every backend fills every slot: this is a superset contract, and
     * testing before calling is the caller's half of it. */
    printf("  backend has no draw_string; geometry only\n");
  }

  /* The sub-window is where emulation shows: the same relative rectangle
   * resolves to absolute cells either natively (ncurses derwin) or in the
   * driver (the ANSI backend, which has no window memory at all). */
  if (vt_ptr->window_create_sub) {
    sub = vt_ptr->window_create_sub(root, sub_rect);
    if (sub) {
      if (vt_ptr->draw_string)
        vt_ptr->draw_string(sub, 1, 0, "sub-window: native or emulated, same call", IRCITUI_WINDOW_ATTR_DIM, 0x00FF88u,
                            0x000000u);
      if (vt_ptr->window_destroy) vt_ptr->window_destroy(sub);
    }
  }

  if (vt_ptr->draw_string) vt_ptr->draw_string(root, 2, 7, "press a key, or wait", IRCITUI_WINDOW_ATTR_REVERSE,
                                               0xFFFFFFu, 0x000000u);

  if (vt_ptr->flush_hardware) rc = vt_ptr->flush_hardware(driver_ptr);

  if (vt_ptr->window_destroy) vt_ptr->window_destroy(root);
  return rc == 0 ? 0 : 1;
}

/**
 * @brief Read one key, and a mouse packet when the backend reports mouse.
 */
static void
sPollInput(const IrcitUiWindowTranslationTable *vt_ptr, const IrcitUiWindowDriver *driver_ptr)
{
  int key;

  if (!vt_ptr->fetch_key) return;

  key = vt_ptr->fetch_key(driver_ptr);
  if (key == -1) {
    printf("  fetch_key -> -1 (nothing pending)\n");
    return;
  }

  /* The contract reserves keycode 0 for "a mouse packet is waiting"; anything
   * else is the backend's own keycode. This demo does not decode them, only
   * shows that the channel is live and backend-neutral. */
  if (key == 0) {
    IrcitUiWindowMouseEvent ev = {.x = 0, .y = 0, .buttons = 0};
    if (vt_ptr->fetch_mouse && vt_ptr->fetch_mouse(driver_ptr, &ev) == 0) printf(
      "  fetch_mouse -> (%d,%d) buttons=0x%X\n", ev.x, ev.y, ev.buttons);
    else printf("  mouse reported, but this backend could not unpack it\n");
  }
  else {
    printf("  fetch_key -> %d\n", key);
  }
}

/**
 * @brief The application's lifecycle hooks, handed to the registry.
 *
 * They are how an application learns that a backend came up and that it is
 * going away, without polling and without the backend knowing it exists. Both
 * receive the same opaque handle the registry returns, so neither can reach
 * past the abstraction to whatever backend is behind it.
 */
static void
sOnDriverInitialised(IrcitUiWindowDriver *driver_ptr)
{
  /* Fires once the driver is locked, so the capability mask is final here. */
  printf("  [hook] driver initialised (caps=0x%X)\n", IrcitUiWindowDriverCapabilities(driver_ptr));
}

static void
sOnDriverShutdown(IrcitUiWindowDriver *driver_ptr)
{
  /* Still readable: the hook fires before the backend's own destroy slot, which
     is the whole reason the contract puts it there. */
  printf("  [hook] driver shutting down (caps=0x%X)\n", IrcitUiWindowDriverCapabilities(driver_ptr));
}

int
main(int argc, char **argv_ptr)
{
  DemoOptions                          opts = {.driver_name = "ncurses", .hold_ms = 900};
  IrcitUiWindowRegistry *              registry;
  const IrcitUiWindowDriver *          driver;
  const IrcitUiWindowTranslationTable *vt;
  int                                  rc;
  int                                  painted;

  rc = sParseArgs(argc, argv_ptr, &opts);
  if (rc != 0) return rc < 0 ? 2 : 0;

  /* Open the library's log before anything can want to write to it. Opt-in by
   * design: with no --log-file, every IRCITUI_LOG_* call inside the library is
   * a no-op, which is the documented behaviour rather than a silence this demo
   * introduces. Pass one and the registry's backend choice, the fallback if it
   * fires, and each adapter's capability claim all land in the file. */
  if (opts.log_file && IrcitUiLogInit("debug", opts.log_file, IRCITUI_LOG_OVERWRITE) != 0) {
    fprintf(stderr, "  could not open the log at '%s'\n", opts.log_file);
    return 1;
  }

  printf("== windowing abstraction demo ==\n");
  printf("  requested backend: %s\n", opts.driver_name);
  if (opts.log_file) printf("  logging to: %s\n", opts.log_file);

  /* Passed by value: the registry keeps its own copy and stamps it onto every
     driver it creates, so this struct need not outlive the call. */
  registry = IrcitUiWindowRegistryCreate((IrcitUiDriverRegistryUserCallbacks){
      .on_driver_initialised = sOnDriverInitialised,
      .on_driver_shutdown    = sOnDriverShutdown,
  });
  if (!registry) {
    fprintf(stderr, "IrcitUiWindowRegistryCreate failed\n");
    IrcitUiLogFini();
    return 1;
  }

  if (!IrcitUiWindowRegistryRequestDriver(registry, opts.driver_name)) {
    fprintf(stderr, "  no backend could be locked for '%s'\n", opts.driver_name);
    IrcitUiWindowRegistryDestroy(registry);
    IrcitUiLogFini();
    return 1;
  }

  driver = IrcitUiWindowRegistryGetActiveDriver(registry);
  vt     = IrcitUiWindowDriverTranslationTable(driver);

  /* NULL here means the request above succeeded but handed back nothing, or
   * the accessor was given a NULL driver. Both are refused rather than
   * dereferenced. */
  if (!driver || !vt) {
    fprintf(stderr, "  registry returned no usable driver\n");
    IrcitUiWindowRegistryDestroy(registry);
    IrcitUiLogFini();
    return 1;
  }

  printf("  locked and usable\n");
  sReportCapabilities(driver);

  printf("\n-- paint --\n");
  painted = sPaint(vt, driver);
  sPollInput(vt, driver);

  /* Leave the frame up long enough to be looked at when stdout is a terminal,
   * and not at all when it is a pipe. */
  if (opts.hold_ms > 0) {
    fflush(stdout);
    (void)usleep((useconds_t)opts.hold_ms * 1000u);
  }

  printf("\n-- teardown --\n");
  IrcitUiWindowRegistryDestroy(registry);
  printf("  registry destroyed\n");

  /* After the registry, so the driver's own shutdown line is not the last
   * thing written into a file that has already been closed. */
  IrcitUiLogFini();

  return painted;
}
