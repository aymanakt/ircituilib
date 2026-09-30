/**
 * @file ircitui_window_adapter_ansi.c
 * @brief Concrete pure-ANSI/ECMA-48 backend: a second driver, and the proof that
 *        the abstraction is one.
 *
 * This file includes no ncurses header and links no curses library. It writes
 * escape sequences to a file descriptor and reads keys off another. That it can
 * satisfy the same translation table as the ncurses adapter — and be selected by
 * the same one-word registry request — is the whole claim of the module
 * demonstrated rather than asserted.
 *
 * It is also the registry's fallback: when ncurses cannot be brought up (no
 * tty, library absent), a request for "ncurses" lands here and still returns a
 * working driver.
 *
 * Two things it does *not* do, and declares as such through its capability
 * mask rather than leaving a caller to find out by drawing:
 *
 *   - No mouse. Parsing the terminal's mouse protocol is real work and this
 *     backend does not do it, so it does not set IRCITUI_WINDOW_CAP_MOUSE, and
 *     @ref sAnsiFetchMouse reports "no event" instead of inventing one.
 *   - No hardware dim. DIM is sent as the SGR faint attribute, which many
 *     terminals ignore, so the bit is not claimed either.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>

/* --- Private backend state, opaque outside this file. --- */
typedef struct {
    int fd_out;
    int fd_in;
    int width;
    int height;
} AnsiContext;

/* --- The opaque window handle, defined here and nowhere else.
 *
 * Deliberately a different shape from the ncurses adapter's: this backend keeps
 * no terminal-side window at all, so the handle is a rectangle and an origin,
 * not a wrapper around a native object. Two adapters, two layouts, one opaque
 * type — which is exactly what an opaque handle is for. */
struct IrcitUiWindow {
    AnsiContext     *ctx;
    IrcitUiWindowRect geometry;
    int              is_subwindow;
};

/* Every move is CUP(row;col), one-based. */
static void
sMoveTo(AnsiContext *ctx, int row, int col)
{
    char seq[32];
    int  n = snprintf(seq, sizeof(seq), "\033[%d;%dH", row + 1, col + 1);
    if (n > 0) (void)!write(ctx->fd_out, seq, (size_t)n);
}

/* UTF-8 encode one scalar. The abstract interface carries codepoints, so the
 * encoder belongs here, in the backend, and not in a caller. */
static int
sPutCodepoint(AnsiContext *ctx, uint32_t cp)
{
    unsigned char utf8[4];
    size_t        len = 0;

    if (cp < 0x80u) {
        utf8[len++] = (unsigned char)cp;
    } else if (cp < 0x800u) {
        utf8[len++] = (unsigned char)(0xC0u | (cp >> 6));
        utf8[len++] = (unsigned char)(0x80u | (cp & 0x3Fu));
    } else if (cp < 0x10000u) {
        utf8[len++] = (unsigned char)(0xE0u | (cp >> 12));
        utf8[len++] = (unsigned char)(0x80u | ((cp >> 6) & 0x3Fu));
        utf8[len++] = (unsigned char)(0x80u | (cp & 0x3Fu));
    } else {
        utf8[len++] = (unsigned char)(0xF0u | (cp >> 18));
        utf8[len++] = (unsigned char)(0x80u | ((cp >> 12) & 0x3Fu));
        utf8[len++] = (unsigned char)(0x80u | ((cp >> 6) & 0x3Fu));
        utf8[len++] = (unsigned char)(0x80u | (cp & 0x3Fu));
    }
    return (write(ctx->fd_out, utf8, len) == (ssize_t)len) ? 0 : -1;
}

/* --- Lifecycle --- */

static void
sAnsiDestroy(IrcitUiWindowDriver *driver)
{
    AnsiContext *ctx;

    if (!driver) return;
    ctx = (AnsiContext *)driver->private_backend_data;

    if (ctx) {
        IRCITUI_LOG_DEBUG("ansi backend: restoring the terminal");
        // Leave the terminal as we found it: attributes off, cursor back, * screen cleared so no half-drawn frame survives the exit.
        (void)!write(ctx->fd_out, "\033[0m\033[?25h\033[2J\033[H", 18);
        free(ctx);
    }
    free(driver);
}

static int
sAnsiClearScreen(const IrcitUiWindowDriver *driver)
{
    AnsiContext *ctx;
    if (!driver || !driver->private_backend_data) return -1;
    ctx = (AnsiContext *)driver->private_backend_data;
    // EL(2) erase whole display, then CUP(1;1) home.
    return (write(ctx->fd_out, "\033[2J\033[H", 7) == 7) ? 0 : -1;
}

static int
sAnsiFlushHardware(const IrcitUiWindowDriver *driver)
{
    if (!driver) return -1;
    // Unbuffered writes mean there is nothing of ours to push, but stdout may * still hold a caller's own buffered output that must precede us.
    return (fflush(stdout) == 0) ? 0 : -1;
}

static void
sAnsiGetTerminalDimensions(const IrcitUiWindowDriver *driver, int *out_w, int *out_h)
{
    AnsiContext *ctx;
    struct winsize ws;

    if (!driver || !driver->private_backend_data) return;
    ctx = (AnsiContext *)driver->private_backend_data;

    if (ioctl(ctx->fd_out, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        ctx->width  = ws.ws_col;
        ctx->height = ws.ws_row;
    } else if (ctx->width == 0) {
        /* No tty to ask — a redirected stdout. Report the conventional default
         * rather than zero, so a caller sizing a layout has something to work
         * with instead of a degenerate one-cell screen. */
        ctx->width  = 80;
        ctx->height = 24;
    }

    if (out_w) *out_w = ctx->width;
    if (out_h) *out_h = ctx->height;
}

static void
sAnsiSetCursorVisibility(const IrcitUiWindowDriver *driver, int visible)
{
    AnsiContext *ctx;
    if (!driver || !driver->private_backend_data) return;
    ctx = (AnsiContext *)driver->private_backend_data;
    if (visible) (void)!write(ctx->fd_out, "\033[?25h", 6);
    else         (void)!write(ctx->fd_out, "\033[?25l", 6);
}

static void
sAnsiHandleTerminalResize(const IrcitUiWindowDriver *driver)
{
    // Re-read the size; the caller relayouts. Deliberately does not redraw.
    int w = 0, h = 0;
    sAnsiGetTerminalDimensions(driver, &w, &h);
}

/* --- Window topology --- */

static IrcitUiWindow *
sAnsiWindowCreateRoot(const IrcitUiWindowDriver *driver, IrcitUiWindowRect geometry)
{
    IrcitUiWindow *win;

    if (!driver || !driver->private_backend_data) return NULL;

    win = calloc(1, sizeof(IrcitUiWindow));
    if (!win) return NULL;

    win->ctx          = (AnsiContext *)driver->private_backend_data;
    win->geometry     = geometry;
    win->is_subwindow = 0;
    return win;
}

/*
 * derwin emulated in software — the case the module's own preamble calls out
 * ("If an alternative backend lacks relative cell offset positioning (derwin),
 * our abstractions driver will intercept the call and compute those spatial
 * coordinate transformations using software emulation vectors automatically").
 *
 * This backend holds no cell storage at all: it writes straight to the
 * terminal. So a sub-window is the parent's absolute origin plus the relative
 * rectangle, resolved here instead of by the library.
 */
static IrcitUiWindow *
sAnsiWindowCreateSub(IrcitUiWindow *parent, IrcitUiWindowRect relative_geometry)
{
    IrcitUiWindow *win;

    if (!parent) return NULL;

    win = calloc(1, sizeof(IrcitUiWindow));
    if (!win) return NULL;

    win->ctx          = parent->ctx;
    win->geometry.x   = parent->geometry.x + relative_geometry.x;
    win->geometry.y   = parent->geometry.y + relative_geometry.y;
    win->geometry.w   = relative_geometry.w;
    win->geometry.h   = relative_geometry.h;
    win->is_subwindow = 1;
    return win;
}

static void
sAnsiWindowDestroy(IrcitUiWindow *win)
{
    // No terminal-side resource is held, so this only frees the handle. The * caller must not use the pointer again.
    free(win);
}

static void
sAnsiWindowResize(IrcitUiWindow *win, IrcitUiWindowRect new_geometry)
{
    if (!win) return;
    win->geometry = new_geometry;
}

static void
sAnsiWindowMove(IrcitUiWindow *win, IrcitUiWindowRect relative_geometry)
{
    if (!win) return;
    win->geometry.x = relative_geometry.x;
    win->geometry.y = relative_geometry.y;
}

static void
sAnsiWindowClear(IrcitUiWindow *win)
{
    if (!win || !win->ctx) return;
    // Clear the rectangle the window covers without disturbing the rest.
    for (int row = 0; row < win->geometry.h; row++) {
        sMoveTo(win->ctx, win->geometry.y + row, win->geometry.x);
        for (int col = 0; col < win->geometry.w; col++) (void)!write(win->ctx->fd_out, " ", 1);
    }
}

static void
sAnsiWindowBox(IrcitUiWindow *win, uint32_t vertical_glyph, uint32_t horizontal_glyph)
{
    if (!win || !win->ctx) return;

    // The abstract interface carries the glyphs so a caller can choose them; * UTF-8 box-drawing is this backend's default when it does not.
    if (vertical_glyph == 0)   vertical_glyph   = 0x2502u; /* │ */
    if (horizontal_glyph == 0) horizontal_glyph = 0x2500u; /* ─ */

    for (int col = 0; col < win->geometry.w; col++) {
        sMoveTo(win->ctx, win->geometry.y, win->geometry.x + col);
        (void)sPutCodepoint(win->ctx, horizontal_glyph);
        if (win->geometry.h > 1) {
            sMoveTo(win->ctx, win->geometry.y + win->geometry.h - 1, win->geometry.x + col);
            (void)sPutCodepoint(win->ctx, horizontal_glyph);
        }
    }
    for (int row = 0; row < win->geometry.h; row++) {
        sMoveTo(win->ctx, win->geometry.y + row, win->geometry.x);
        (void)sPutCodepoint(win->ctx, vertical_glyph);
        if (win->geometry.w > 1) {
            sMoveTo(win->ctx, win->geometry.y + row, win->geometry.x + win->geometry.w - 1);
            (void)sPutCodepoint(win->ctx, vertical_glyph);
        }
    }
}

/* --- Render primitives --- */

static int
sAnsiDrawChar(IrcitUiWindow *win, int x, int y, uint32_t codepoint,
              IrcitUiWindowVideoAttr attr, uint32_t fg_rgb, uint32_t bg_rgb)
{
    char seq[64];
    int  n;

    if (!win || !win->ctx) return -1;

    sMoveTo(win->ctx, win->geometry.y + y, win->geometry.x + x);

    if (attr & IRCITUI_WINDOW_ATTR_BOLD)      (void)!write(win->ctx->fd_out, "\033[1m", 4);
    if (attr & IRCITUI_WINDOW_ATTR_DIM)       (void)!write(win->ctx->fd_out, "\033[2m", 4);
    if (attr & IRCITUI_WINDOW_ATTR_REVERSE)   (void)!write(win->ctx->fd_out, "\033[7m", 4);
    if (attr & IRCITUI_WINDOW_ATTR_UNDERLINE) (void)!write(win->ctx->fd_out, "\033[4m", 4);

    /* 24-bit SGR, honoured directly. This is the capability ncurses does not
     * have and must quantise; here the requested colour reaches the terminal
     * unchanged, which is what IRCITUI_WINDOW_CAP_TRUE_COLOR advertises. */
    if (fg_rgb != 0u || bg_rgb != 0u) {
        n = snprintf(seq, sizeof(seq), "\033[38;2;%u;%u;%um\033[48;2;%u;%u;%um",
                     (fg_rgb >> 16) & 0xFFu, (fg_rgb >> 8) & 0xFFu, fg_rgb & 0xFFu,
                     (bg_rgb >> 16) & 0xFFu, (bg_rgb >> 8) & 0xFFu, bg_rgb & 0xFFu);
        if (n > 0) (void)!write(win->ctx->fd_out, seq, (size_t)n);
    }

    if (sPutCodepoint(win->ctx, codepoint) != 0) return -1;

    (void)!write(win->ctx->fd_out, "\033[0m", 4);
    return 0;
}

static int
sAnsiDrawString(IrcitUiWindow *win, int x, int y, const char *str,
                IrcitUiWindowVideoAttr attr, uint32_t fg_rgb, uint32_t bg_rgb)
{
    int col = x;

    if (!win || !win->ctx || !str) return -1;

    // One cell per scalar, not per byte: a multi-byte glyph must advance the * cursor once or the string after it lands in the wrong column.
    for (const char *p = str; *p; ) {
        unsigned char c = (unsigned char)*p;
        uint32_t      cp = c;
        int           extra = 0;

        if (c >= 0xF0u)      { cp = c & 0x07u; extra = 3; }
        else if (c >= 0xE0u) { cp = c & 0x0Fu; extra = 2; }
        else if (c >= 0xC0u) { cp = c & 0x1Fu; extra = 1; }

        for (int i = 0; i < extra && p[1] != '\0'; i++) {
            p++;
            cp = (cp << 6) | ((uint32_t)((unsigned char)*p) & 0x3Fu);
        }
        p++;

        if (sAnsiDrawChar(win, col, y, cp, attr, fg_rgb, bg_rgb) != 0) return -1;
        col++;
    }
    return 0;
}

static void
sAnsiMoveWindowCaret(IrcitUiWindow *win, int x, int y)
{
    if (!win || !win->ctx) return;
    sMoveTo(win->ctx, win->geometry.y + y, win->geometry.x + x);
}

/* --- Input --- */

static int
sAnsiFetchKey(const IrcitUiWindowDriver *driver)
{
    AnsiContext  *ctx;
    unsigned char c;
    ssize_t       n;

    if (!driver || !driver->private_backend_data) return -1;
    ctx = (AnsiContext *)driver->private_backend_data;

    n = read(ctx->fd_in, &c, 1);
    if (n <= 0) return -1;                 /* nothing pending, or the stream ended */

    // An escape sequence is reported as ESC. This backend has no keymap, so it * does not pretend to know what the sequence meant.
    if (c == 0x1Bu) {
        unsigned char tail[8];
        (void)read(ctx->fd_in, tail, sizeof(tail));
        return 0x1B;
    }
    return (int)c;
}

static int
sAnsiFetchMouse(const IrcitUiWindowDriver *driver, IrcitUiWindowMouseEvent *out_event)
{
    (void)driver;
    (void)out_event;

    /* No mouse protocol is parsed here. Reporting "no event" is the honest
     * answer, and the driver does not claim IRCITUI_WINDOW_CAP_MOUSE, so a
     * caller had a way to know before it asked. Fabricating a zeroed event
     * would be worse: it is indistinguishable from a real press at (0,0). */
    return -1;
}

/* --- The table --- */

static const IrcitUiWindowTranslationTable sAnsiTranslationTable = {
    .destroy                 = sAnsiDestroy,
    .clear_screen            = sAnsiClearScreen,
    .flush_hardware          = sAnsiFlushHardware,
    .get_terminal_dimensions = sAnsiGetTerminalDimensions,
    .set_cursor_visibility   = sAnsiSetCursorVisibility,
    .handle_terminal_resize  = sAnsiHandleTerminalResize,

    .window_create_root      = sAnsiWindowCreateRoot,
    .window_create_sub       = sAnsiWindowCreateSub,
    .window_destroy          = sAnsiWindowDestroy,
    .window_resize           = sAnsiWindowResize,
    .window_move             = sAnsiWindowMove,
    .window_clear            = sAnsiWindowClear,
    .window_box              = sAnsiWindowBox,

    .draw_char               = sAnsiDrawChar,
    .draw_string             = sAnsiDrawString,
    .move_window_caret       = sAnsiMoveWindowCaret,

    .fetch_key               = sAnsiFetchKey,
    .fetch_mouse             = sAnsiFetchMouse
};

/* --- The factory the registry calls --- */

IrcitUiWindowDriver *
IrcitUiWindowCreateAnsiFallbackDriver(void)
{
    AnsiContext         *ctx;
    IrcitUiWindowDriver *driver;
    int                  flags;

    ctx = calloc(1, sizeof(AnsiContext));
    if (!ctx) {
        IRCITUI_LOG_ERROR("ansi backend: out of memory allocating its context");
        return NULL;
    }

    ctx->fd_out = STDOUT_FILENO;
    ctx->fd_in  = STDIN_FILENO;

    // Non-blocking input: fetch_key is a poll, and the contract's "-1 means * nothing pending" is only truthful if the read cannot block.
    flags = fcntl(ctx->fd_in, F_GETFL, 0);
    if (flags >= 0) (void)fcntl(ctx->fd_in, F_SETFL, flags | O_NONBLOCK);

    driver = calloc(1, sizeof(IrcitUiWindowDriver));
    if (!driver) {
        IRCITUI_LOG_ERROR("ansi backend: out of memory allocating the driver handle");
        free(ctx);
        return NULL;
    }

    driver->vtable               = &sAnsiTranslationTable;
    driver->private_backend_data = ctx;

    /* True colour, because it writes the requested 24-bit values out verbatim.
     * Sub-windows, because it emulates derwin rather than declaring it absent.
     * Deliberately neither MOUSE nor HARDWARE_DIM — see the file header. */
    driver->capabilities_bitmask = IRCITUI_WINDOW_CAP_TRUE_COLOR |
                                   IRCITUI_WINDOW_CAP_SUBWINDOWS;

    /* The mask is logged with the backend, because it is the only record of
     * what was claimed. The two backends deliberately claim different things,
     * and a log that said merely "ansi backend created" would not show it. */
    IRCITUI_LOG_INFO("ansi backend created: caps=0x%X "
                     "(true colour, emulated sub-windows; no mouse, no hardware dim)",
                     driver->capabilities_bitmask);
    return driver;
}
