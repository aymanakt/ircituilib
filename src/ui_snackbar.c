/**
 * @file ui_snackbar.c
 * @brief The transient overlay that rises out of the footer and takes itself away.
 *
 * Copyright (C) 2015-2026  unfacd works
 *
 * This file is part of ircituilib source code.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ---
 *
 * A window, not a node, and not a declaration in any layout: the overlay is
 * programmatic in the way the design document's third section argues for. It has
 * no parent, no children, no place in the box model, and nothing in the tree knows
 * it exists. What it does have is a z-order -- it is stamped into the virtual
 * screen AFTER the frame, so it draws over everything -- and a lifetime.
 *
 * FOUR FACTS ABOUT THE HOST, all measured rather than assumed, and each of them
 * shapes the code below. They are in IRCITUILIB_DESIGN.md's §11.8, measured at that
 * track's gate 0; read them before changing the animation or the paint:
 *
 *   1. mvwin REFUSES any position where the window is not wholly on screen. The
 *      document's design parks the window at row max_y and slides it up; ncurses
 *      6.4 returns ERR for that call and for every row that would overhang the
 *      bottom edge. A box can therefore never be off-screen, and the slide is a
 *      window that GROWS from the bottom edge: h rows at y = LINES - h.
 *
 *   2. A window that moves up or shrinks LEAVES ITS VACATED CELLS ON THE GLASS.
 *      The tree did not move, so the vacated row is in nobody's change set, and
 *      ncurses copies only touched lines into the virtual screen. Measured: the
 *      box's full width, 106 cells, still showing the box after it settled up by
 *      one row. The caller is told when that has happened -- see the return of
 *      IrcitUiSnackbarAdvance -- and answers with the engine's full-repaint lever.
 *
 *   3. A TREE REPAINT PUNCHES THROUGH THE OVERLAY. The root's window covers the
 *      screen, so any frame that repaints the tree rewrites the overlay's cells in
 *      the virtual screen. Measured: 0 cells of the overlay survive a tree repaint
 *      that the overlay did not join. So Render re-stamps EVERY pass while the box
 *      is up, and rewrites its lines while doing it -- an unchanged window's
 *      wnoutrefresh touches nothing and would leave the hole.
 *
 *   4. Thread 0 only, like everything that touches ncurses. There is no crossing
 *      here and none is wanted: the network listener the document's example fires
 *      this from already runs on Thread 0.
 *
 * AND ONE KEY REACHES IT, which is the exception that proves the rule above. The
 * box takes no focus and joins no layout, so there is no routing path that could
 * deliver a keystroke to it; instead it is OFFERED each key before the tree is, by
 * IrcitUiSnackbarKey, called from the top of IrcitUiDispatchKey. It answers for its
 * own action hotkey and for nothing else, so typing still lands in the field
 * underneath -- which the gate-2 capture in IRCITUILIB_DESIGN.md 11.8 records as a
 * property of the overlay rather than an accident.
 */
#include <curses.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ircituilib/config_defs.h"
#include "ui_log.h"
#include "ui_priv.h"
#include "ui_snackbar_priv.h"

/** @brief Where the box is in its journey. Hidden is the only state with nothing
    on the glass. */
typedef enum {
  SNACK_HIDDEN,  /**< Nothing drawn, nothing to draw. */
  SNACK_RISING,  /**< Travelling up, or waiting for the next whole row. */
  SNACK_VISIBLE, /**< Settled, holding until the deadline. */
  SNACK_FALLING  /**< Travelling back down, and about to be hidden. */
} SnackState;

/* One overlay, because there is one screen. The strings are copied in rather than
   borrowed: a notification outlives the buffer its caller built it in, which is
   the opposite of a node's common.text and is why the header says so. */
static struct {
  WINDOW *   win;  /**< NULL until the first frame that has something to draw. */
  SnackState state;
  char       text[PRIV_CONFIG_DEFAULT_SNACKBAR_MSG_SZ];

  int width;       /**< The settled box's width in columns. */
  int x;           /**< The settled box's left column. */
  int screen_rows; /**< The LINES the two above were computed against. */
  int screen_cols; /**< The COLS, for the same reason. */

  /* Travel completed, in thousandths of a row. Integer and not a float: the
     accumulator is the animation, and milli-rows make it exactly assertable. */
  int offset_milli;
  /* The step the LAST PAINT drew, 0 when it drew nothing. The difference between
     this and the current step is what tells the caller a repaint is owed, and it
     is read before the frame, so it must be written by the paint and not by the
     advance. */
  int painted_step;
  /* Content or geometry changed since that paint. */
  int dirty;

  /* The borrowed register run the gradient paints with, or -1, and the box width it
     was borrowed for. The width is part of the state because a ramp is one step per
     column: after a resize the old run is the wrong length and is handed back. */
  int ramp_base;
  int ramp_width;

  /* What the window itself currently is, so a resize is applied once rather than
     every frame. */
  int win_h, win_w, win_y, win_x;

  long long deadline_ms; /**< When a settled box begins to fall. */
  long long last_ms;     /**< The clock the last advance integrated from. */

  /* The action, when the caller asked for one. TWO fields decide it and they are
     not interchangeable, which is the trap this pair exists to avoid:
       - `label` is what the PAINT reads. It stays set while the box falls, so a
         notification that has been acted on still shows what it was offering as
         it leaves.
       - `action_cb` is what the KEY path reads, and it is cleared the instant the
         action fires. That is what makes the callback run exactly once even if it
         re-enters, and it is why "has an action" is the pointer and not a separate
         flag -- a flag would be a second copy of a fact the pointer already
         carries, and the two could disagree.
     `hotkey` is normalised on the way in, so the three spellings of Enter are one
     value by the time anything compares it. */
  char                          label[PRIV_CONFIG_DEFAULT_SNACKBAR_LABEL_SZ];
  int                           hotkey;
  IrcitUiSnackbarActionCallback action_cb;
  void *                        action_user;
} sSnack = {
  /* ramp_base is -1 when no registers are held, and the initialiser is not decoration:
     zero-initialised, it reads as "I hold the ramp at base 0", which the box then tries to
     use and to release -- a ramp it never borrowed. Same trap as the ramp table's. */
  .ramp_base = -1,
};

static long long
sNowMs(void)
{
  struct timespec ts;

  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (long long)ts.tv_sec * 1000LL + (long long)(ts.tv_nsec / 1000000L);
}

/** @brief Rows the box travels: its own height, plus the clearance it settles on. */
static int
sSpanRows(void)
{
  return PRIV_CONFIG_DEFAULT_SNACKBAR_ROWS + PRIV_CONFIG_DEFAULT_SNACKBAR_MARGIN;
}

/** @brief Whole rows of travel completed. */
static int
sStep(void)
{
  return sSnack.offset_milli / 1000;
}

/** @brief Whether the screen can hold the box at all. */
static int
sFits(void)
{
  return LINES >= sSpanRows() && COLS >= 4;
}

/* The settled box's geometry, measured against the screen as it is NOW.
 *
 * Recomputed on every advance rather than taken once at show time: a notification
 * can outlive a terminal resize, and a box drawn to the width the terminal used to
 * have is a box in the wrong place. Two comparisons in the common case. */
static void
sMeasure(void)
{
  int cols = COLS, w, x;

  if (cols <= 0 || LINES <= 0) return;

  if (cols > PRIV_CONFIG_DEFAULT_SNACKBAR_WIDE_COLS) {
    w = PRIV_CONFIG_DEFAULT_SNACKBAR_MAX_W;
    x = (cols - w) / 2;
  } else {
    w = cols - 2 * PRIV_CONFIG_DEFAULT_SNACKBAR_EDGE_X;
    x = PRIV_CONFIG_DEFAULT_SNACKBAR_EDGE_X;
  }
  /* Too narrow for the margins: keep the box, drop the margins. The alternative is
     refusing to notify on a small terminal, which is the wrong trade for a message
     the application has already decided to show. */
  if (w < PRIV_CONFIG_DEFAULT_SNACKBAR_MIN_COLS) {
    w = cols;
    x = 0;
  }

  if (w != sSnack.width || x != sSnack.x || LINES != sSnack.screen_rows || cols != sSnack.screen_cols) sSnack.dirty = 1;
  sSnack.width       = w;
  sSnack.x           = x;
  sSnack.screen_rows = LINES;
  sSnack.screen_cols = cols;
}

/* What a slot is drawn in, with a fade in flight taking precedence.
 *
 * Two hard-coded colour numbers and a cache around them stood here -- resolved once,
 * because has_colors() needs the terminal up and this is not a static initialiser.
 * Both are gone, and the cache's removal is the part worth noticing: it was the
 * reason a palette change could not reach the box, since nothing could tell it to
 * resolve again. A slot is asked for on every paint instead, so a redefinition
 * arrives with the next repaint and there is no reset hook to forget.
 *
 * The appearance is unchanged at every level, which is what makes this the right
 * first tenant: the slot's 256-colour definition is 255 on 236 -- the two numbers
 * that stood here -- its eight-colour definition is the A_REVERSE this used to fall
 * back to, and its no-colour definition is A_REVERSE too. A slot that moved the
 * widget that motivated it would have been the wrong default. */
static chtype
sStyleOf(IrcitUiPaletteSlot slot)
{
  /* A fade in flight paints this box through its register, and that is the whole reason
     the overlay asks: ncurses emits a register change only for a register a PAIR
     references, so a fade nothing paints through produces no output at all -- measured,
     2 register writes where an interpolation should have produced twenty. A fade is
     therefore invisible until something draws with it.

     It takes precedence over the SLOT as well as over the paint, and the action label
     is included on purpose: a fade means the box is on its way out, and a control that
     stayed at its own bright role colour while its box dissolved would be advertising
     an affordance the user can no longer take -- IrcitUiSnackbarKey is inert from the
     moment the box starts to fall. */
  int handle = IrcitUiPaletteFadeHandle();

  if (handle) {
    chtype fading = IrcitUiPaletteFadeStyle(handle);
    if (fading != A_NORMAL) return fading;
  }

  return IrcitUiPaletteStyle(slot);
}

/** @brief What the box itself is drawn in. */
static chtype
sStyle(void)
{
  return sStyleOf(IRCITUI_SLOT_OVERLAY_SNACKBAR);
}

/* How many columns the action label takes, 0 when there is no action.
 *
 * Clipped rather than refused: a label wider than the row is the caller's mistake, and
 * a notification whose message is already written should not be dropped over it. The
 * cap leaves the message at least one column, so an action can never crowd the message
 * out entirely -- and the cap is on the DISPLAY width, not the byte count, because the
 * box is clipped by character everywhere else. */
static int
sLabelCols(void)
{
  wchar_t wbuf[PRIV_CONFIG_DEFAULT_SNACKBAR_LABEL_SZ];
  size_t  n, k;
  int     used = 0, cap;

  if (!sSnack.label[0]) return 0;

  cap = sSnack.width - 8; /* borders, the message's floor, and the gap between them */
  if (cap < 1) return 0;

  n = mbstowcs(wbuf, sSnack.label, sizeof(wbuf) / sizeof(wbuf[0]) - 1);
  if (n == (size_t)-1) {
    size_t bytes = strlen(sSnack.label);
    return (int)(bytes < (size_t)cap ? bytes : (size_t)cap);
  }

  for (k = 0; k < n; k++) {
    int ww = wcwidth(wbuf[k]);
    if (ww < 0) ww = 1;
    if (used + ww > cap) break;
    used += ww;
  }
  return used;
}

/* The message, clipped to the cells the box has room for.
 *
 * Cell-aware and not byte-aware, and the tree has paid for the difference once
 * already: a three-byte glyph written with a byte-wise call advances curses three
 * cells, and a byte-wise clip hands the window half a UTF-8 sequence. So this walks
 * CHARACTERS, stops on the one that would not fit, and writes with mvwaddnwstr. A
 * string the locale cannot read at all falls back to the byte-wise path, which is
 * what every other painter in this tree does with text it cannot interpret. */
/* The message, clipped to the cells the box has room for.
 *
 * When a ramp is live the text is written ONE CHARACTER AT A TIME, each carrying the
 * style of the column it lands in. That is not an optimisation lost but a cost accepted:
 * mvwaddnwstr writes a whole run in the window's single attribute, so the text would sit
 * on one flat colour while the band behind it changed. The character loop is the price of
 * the text riding the gradient, and it is bounded by the box's own width. */
static void
sPaintMessage(WINDOW *win_ptr, int row, int ramp, int label_cols)
{
  wchar_t wbuf[PRIV_CONFIG_DEFAULT_SNACKBAR_MSG_SZ];
  /* The two border columns, the padding, and whatever the action label has taken --
     plus one column of gap, so the message and the label can never touch and read as
     one string of text. */
  int     budget = sSnack.width - 4 - (label_cols ? label_cols + 1 : 0);
  size_t  n, k;
  int     used = 0;

  if (budget <= 0) return;

  n = mbstowcs(wbuf, sSnack.text, sizeof(wbuf) / sizeof(wbuf[0]) - 1);
  if (n == (size_t)-1) {
    // A string this locale cannot read. Byte-wise, with the column's style per byte when a ramp is live -- the same fallback every other painter in this tree takes.
    size_t i;
    if (ramp < 0) {
      mvwaddnstr(win_ptr, row, 2, sSnack.text, budget);
      return;
    }
    for (i = 0; i < (size_t)budget && sSnack.text[i]; i++)
      mvwaddch(win_ptr, row, 2 + (int)i, (chtype)(unsigned char)sSnack.text[i] | IrcitUiPaletteRampStyle(ramp, 2 + (int)i, COLOR_WHITE));
    return;
  }

  for (k = 0; k < n; k++) {
    int ww = wcwidth(wbuf[k]);
    if (ww < 0) ww = 1; /* unprintable here, but it still takes the cell it is written to */
    if (used + ww > budget) break;
    used += ww;
  }
  if (k == 0) return;

  if (ramp < 0) {
    mvwaddnwstr(win_ptr, row, 2, wbuf, (int)k);
    return;
  }

  {
    int    col = 2;
    size_t j;
    for (j = 0; j < k; j++) {
      wchar_t one[2] = {wbuf[j], 0};
      cchar_t cc;
      int     ww = wcwidth(wbuf[j]);
      if (ww < 0) ww = 1;
      setcchar(&cc, one, A_NORMAL, (short)PAIR_NUMBER(IrcitUiPaletteRampStyle(ramp, col, COLOR_WHITE)), NULL);
      mvwadd_wch(win_ptr, row, col, &cc);
      col += ww;
    }
  }
}

/* The action label, right-aligned on the message row.
 *
 * Its own slot, and never the ramp: the gradient is the box's SURFACE, and a control
 * that rode it would change colour along its own length and read as decoration rather
 * than as something to press.
 *
 * wattrset and not the wattron/wattroff pair every other painter here uses, and that is
 * deliberate rather than an oversight. The two styles do not nest: overlay_action
 * carries A_REVERSE and A_BOLD and the box's does not, so a wattroff for the label
 * would clear bits the box's own attribute was relying on and the rest of the row would
 * come out plain. Setting the attribute outright and setting it back is the only shape
 * with no shared-bit hazard. */
static void
sPaintLabel(WINDOW *win_ptr, int row, int label_cols)
{
  wchar_t wbuf[PRIV_CONFIG_DEFAULT_SNACKBAR_LABEL_SZ];
  chtype  label_style = sStyleOf(IRCITUI_SLOT_OVERLAY_ACTION);
  size_t  n, k;
  int     used = 0;
  int     col  = sSnack.width - 2 - label_cols;

  if (label_cols <= 0 || col < 2) return;

  n = mbstowcs(wbuf, sSnack.label, sizeof(wbuf) / sizeof(wbuf[0]) - 1);
  if (n == (size_t)-1) {
    // The same fallback sPaintMessage takes, and for the same reason: a string this locale cannot read is written byte-wise rather than dropped.
    wattrset(win_ptr, label_style);
    mvwaddnstr(win_ptr, row, col, sSnack.label, label_cols);
    wattrset(win_ptr, sStyle());
    return;
  }

  // Clipped to the same width sLabelCols measured, so the two cannot disagree about where the label ends -- the position above was computed from that width.
  for (k = 0; k < n; k++) {
    int ww = wcwidth(wbuf[k]);
    if (ww < 0) ww = 1;
    if (used + ww > label_cols) break;
    used += ww;
  }
  if (k == 0) return;

  wattrset(win_ptr, label_style);
  mvwaddnwstr(win_ptr, row, col, wbuf, (int)k);
  wattrset(win_ptr, sStyle());
}

/* Borrow the register run the gradient needs, or -1.
 *
 * Sized to the BOX WIDTH and re-borrowed when the width changes, because a gradient is
 * one step per column: a ramp sized for the old width would either stop short of the
 * right edge or run past it. A release before every acquire is what makes a resize safe
 * -- ncurses hands the registers back to whatever they held, and the new run is written
 * fresh.
 *
 * Returns -1 whenever the effect is off, colour is unavailable, or the terminal refuses
 * the borrow. Every caller treats -1 as "draw the flat box", so a terminal that cannot
 * do this gets the snackbar it had before rather than a broken one. */
static int
sRampEnsure(void)
{
  int base, i, w = sSnack.width;

  if (!(IrcitUiPaletteEffects() & IRCITUI_PALETTE_EFFECT_GRADIENT)) {
    /* An effect that has been switched off MUST hand back what it holds, and this is the
       defect that cost two hours: with the ramp still held, a later notification hid,
       released base 46 -- and by then 46 was the CANOPY's ramp, so the box restored
       ncurses' model colours over the backdrop and freed the canopy's table entry out
       from under it. The canopy then painted in the model's defaults and its pulse wrote
       nothing, because a released base makes RampSet a no-op.
    
       The general rule this is an instance of: a resource held across a configuration
       change has to be checked against the configuration before it is used, not just
       when it is taken. */
    if (sSnack.ramp_base >= 0) {
      IrcitUiPaletteRampRelease(sSnack.ramp_base, sSnack.ramp_width, "snackbar");
      sSnack.ramp_base  = -1;
      sSnack.ramp_width = 0;
    }
    return -1;
  }
  if (sSnack.ramp_base >= 0 && sSnack.ramp_width == w) return sSnack.ramp_base;

  if (sSnack.ramp_base >= 0) {
    IrcitUiPaletteRampRelease(sSnack.ramp_base, sSnack.ramp_width, "snackbar");
    sSnack.ramp_base  = -1;
    sSnack.ramp_width = 0;
  }

  base = IrcitUiPaletteRampAcquire(w, "snackbar");
  if (base < 0) return -1;

  /* A diagonal ramp: two RGB endpoints interpolated across the box's columns. Written
     ONCE per acquisition rather than per frame -- the pulse case is the one that
     rewrites registers every frame, and it is a separate effect for that reason. */
  for (i = 0; i < w; i++) {
    int t = w > 1 ? i * 255 / (w - 1) : 0;
    IrcitUiPaletteRampSet(base, i, 0, 40 + t * 3 / 4, 80 + t / 2);
  }

  sSnack.ramp_base  = base;
  sSnack.ramp_width = w;
  return base;
}

/* The cell style for one column: the ramp's step, or the flat slot style.
 *
 * One function rather than a test at each call site, because there are six of them and
 * the box would come out half-gradiented if one were missed. */
static chtype
sCellStyle(int ramp, int col)
{
  return ramp >= 0 ? IrcitUiPaletteRampStyle(ramp, col, COLOR_WHITE) : sStyle();
}

/* One frame cell of the box: the profile's glyph when there is a profile, the ACS glyph
 * otherwise.
 *
 * The box is not a node and joins no layout, so it is NOT reached by the engine's own
 * frame painter -- it has to ask for the profile itself or a rounded tree would sail a
 * square notification over it. Keeping the two in step is the reason the glyph table
 * lives in the palette module rather than in the engine: the overlay may not reach into
 * the engine, and it does not have to.
 *
 * The profile path goes through setcchar/mvwadd_wch, which is the path sPaintMessage
 * already uses for this box's own non-ASCII glyphs, so a rounded corner is the same kind
 * of cell as a `✔` in the message. The ACS path is untouched because it is the default:
 * with no profile this is one mvwaddch, exactly as before.
 *
 * @param acs    The ACS glyph to draw when there is no profile (or no decodable one).
 * @param runes  The profile's UTF-8 glyph, or NULL for the ACS box.
 * @param ramp   The gradient's base, or -1 for the flat box; the pair is taken from it. */
static void
sPaintFrameRune(WINDOW *win_ptr, int r, int c, chtype acs, const char *runes, int ramp)
{
  wchar_t w[2];
  cchar_t cc;

  /* "No profile" and "a locale that cannot decode the glyph" are the same answer on
     purpose: both fall back to the ACS cell. A frame with a hole in it where the corner
     should be is worse than a square one, and mbstowcs failing is exactly what a
     non-UTF-8 locale does to every glyph in the table. */
  if (!runes || mbstowcs(w, runes, 2) != 1) {
    mvwaddch(win_ptr, r, c, acs | (ramp >= 0 ? sCellStyle(ramp, c) : 0));
    return;
  }

  w[1] = 0;
  setcchar(&cc, w, A_NORMAL, (short)PAIR_NUMBER(sCellStyle(ramp, c)), NULL);
  (void)mvwadd_wch(win_ptr, r, c, &cc);
}

/* The box's rows [0, h), drawn into a window h rows tall.
 *
 * Row 0 is the top border and the FULL box's last row is the bottom border, so a
 * partially emerged box shows a top border with no bottom one. That is what "rising
 * out of the footer" has to look like: the box is being revealed, not resized. */
static void
sPaintBox(WINDOW *win_ptr, int h)
{
  int rows = PRIV_CONFIG_DEFAULT_SNACKBAR_ROWS;
  int w    = sSnack.width;
  int ramp = sRampEnsure();
  int label_cols = sLabelCols();
  int r, x;

  wbkgdset(win_ptr, sStyle() | ' ');
  werase(win_ptr);
  wattron(win_ptr, sStyle());

  for (r = 0; r < h; r++) {
    /* The band first, so every cell of the row carries its column's colour and the
       glyphs written after it land on the right one. Skipped entirely when there is no
       ramp, which is the only difference between a gradient box and the flat one. */
    if (ramp >= 0)
      for (x = 0; x < w; x++) mvwaddch(win_ptr, r, x, (chtype)' ' | sCellStyle(ramp, x));

    if (r == 0 || r == rows - 1) {
      chtype corner_l = r == 0 ? ACS_ULCORNER : ACS_LLCORNER;
      chtype corner_r = r == 0 ? ACS_URCORNER : ACS_LRCORNER;
      const char *const *prof = IrcitUiBorderGlyphs(IrcitUiBorderStyleGet());
      // The profile is indexed top-left, top-right, bottom-left, bottom-right, rule, side, so the top row takes [0] and [1] and the bottom row [2] and [3].
      const char *gl = prof ? (r == 0 ? prof[0] : prof[2]) : NULL;
      const char *gr = prof ? (r == 0 ? prof[1] : prof[3]) : NULL;
      const char *gh = prof ? prof[4] : NULL;

      /* Per column rather than mvwhline, because the horizontal rule has to change
         colour along its length. mvwhline writes one attribute for the whole run -- and
         a profile's rule is one glyph per cell for the same reason. */
      if (ramp >= 0) {
        for (x = 0; x < w; x++)
          sPaintFrameRune(win_ptr, r, x, x == 0 ? corner_l : x == w - 1 ? corner_r : ACS_HLINE,
                          x == 0 ? gl : x == w - 1 ? gr : gh, ramp);
      }
      else {
        sPaintFrameRune(win_ptr, r, 0, corner_l, gl, ramp);
        // The ASCII run stays one call when there is no profile: mvwhline is what drew this row before profiles existed and there is no reason to make it 18.
        if (prof) for (x = 1; x < w - 1; x++) sPaintFrameRune(win_ptr, r, x, ACS_HLINE, gh, ramp);
        else mvwhline(win_ptr, r, 1, ACS_HLINE, w - 2);
        sPaintFrameRune(win_ptr, r, w - 1, corner_r, gr, ramp);
      }
    } else {
      const char *const *prof = IrcitUiBorderGlyphs(IrcitUiBorderStyleGet());
      const char        *gv   = prof ? prof[5] : NULL;

      sPaintFrameRune(win_ptr, r, 0, ACS_VLINE, gv, ramp);
      sPaintMessage(win_ptr, r, ramp, label_cols);
      /* After the message, so the two cannot overwrite each other: the message is
         clipped to stop short of the label's column, and this is what guarantees it
         even if the display width of one of them is not what the byte count suggests. */
      sPaintLabel(win_ptr, r, label_cols);
      sPaintFrameRune(win_ptr, r, w - 1, ACS_VLINE, gv, ramp);
    }
  }

  wattroff(win_ptr, sStyle());

  /* The registers are NOT released here. The box is repainted on every pass while it is
     up, and releasing at the end of a paint would hand the registers back and re-borrow
     them sixty times a second. They are released when the box goes away -- see
     IrcitUiSnackbarRender's HIDDEN branch and IrcitUiSnackbarDestroy. */
}

/* The three spellings of Enter are one key.
 *
 * 10 and 13 are the two a terminal sends for the Return key and ncurses passes both
 * through unchanged; KEY_ENTER is what the numeric keypad's Enter arrives as once
 * keypad() is on. The watchdog alert already treats the first two as one -- "10 and 13
 * are its two spellings across terminals" -- and this adds the third, so a caller who
 * maps Enter gets it wherever the user presses it. */
static int
sNormaliseHotkey(int key)
{
  if (key == 13 || key == KEY_ENTER) return 10;
  return key;
}

/* The one show path: refuse, copy, and start the rise. Returns 1 when the box is up.
 *
 * The action is installed only AFTER the refusals, and that ordering is the whole
 * reason this is not two functions: installing it first would let a refused Show -- a
 * NULL message, a screen that shrank -- strip the label off a box already on the glass
 * without putting anything in its place. */
static int
sShow(const char *text_ptr, unsigned int duration_ms, const char *label_ptr, int hotkey,
      IrcitUiSnackbarActionCallback cb, void *user_ptr)
{
  long long now;
  size_t    i, n;

  if (!text_ptr || !text_ptr[0]) return 0;

  if (!IrcitUiScreen.root) {
    /* Nothing to draw over. Reported rather than queued: an overlay that appears
       when the screen does would arrive attached to a notification nobody is
       waiting for any more. */
    IRCITUI_LOG_WARN("snackbar dropped: no screen tree to draw over");
    return 0;
  }
  if (!sFits()) {
    IRCITUI_LOG_WARN("snackbar dropped: a %dx%d screen cannot hold a %d-row box", COLS, LINES,
                     PRIV_CONFIG_DEFAULT_SNACKBAR_ROWS);
    return 0;
  }

  sSnack.label[0]    = '\0';
  sSnack.hotkey      = 0;
  sSnack.action_cb   = NULL;
  sSnack.action_user = NULL;
  if (cb) {
    for (n = 0; n + 1 < sizeof(sSnack.label) && label_ptr[n]; n++) {
      unsigned char ch = (unsigned char)label_ptr[n];
      sSnack.label[n]  = (ch < 32 || ch == 127) ? ' ' : (char)ch;
    }
    sSnack.label[n]    = '\0';
    sSnack.hotkey      = hotkey;
    sSnack.action_cb   = cb;
    sSnack.action_user = user_ptr;
  }

  /* COPIED, and with the control characters taken out. The copy is what the
     overlay's lifetime requires -- it outlives the caller's buffer. The
     substitution is not decoration: a newline in a one-row box is a cursor move to
     the window, and it would land on the border row. */
  for (i = 0; i + 1 < sizeof(sSnack.text) && text_ptr[i]; i++) {
    unsigned char ch = (unsigned char)text_ptr[i];
    sSnack.text[i]   = (ch < 32 || ch == 127) ? ' ' : (char)ch;
  }
  sSnack.text[i] = '\0';

  now = sNowMs();
  sMeasure();

  if (sSnack.state == SNACK_HIDDEN) {
    sSnack.offset_milli = 0;
    sSnack.state        = SNACK_RISING;
    sSnack.last_ms      = now;
  } else if (sSnack.state == SNACK_FALLING) {
    /* Caught on the way down: driven back up from where it is, rather than restarted
       from the footer, so a second notification replaces the first instead of
       bouncing the box off the bottom edge. */
    sSnack.state = SNACK_RISING;
  }

  sSnack.deadline_ms = now + (duration_ms ? duration_ms : PRIV_CONFIG_DEFAULT_SNACKBAR_DURATION_MS);
  sSnack.dirty       = 1;

  IRCITUI_LOG_INFO("snackbar: '%s' for %u ms%s%s", sSnack.text,
                   duration_ms ? duration_ms : (unsigned)PRIV_CONFIG_DEFAULT_SNACKBAR_DURATION_MS,
                   sSnack.label[0] ? ", action [" : "", sSnack.label[0] ? sSnack.label : "");
  return 1;
}

PUBLIC_API void
IrcitUiSnackbarShow(const char *text_ptr, unsigned int duration_ms)
{
  (void)sShow(text_ptr, duration_ms, NULL, 0, NULL, NULL);
}

PUBLIC_API int
IrcitUiSnackbarShowAction(const char *text_ptr, unsigned int duration_ms, const char *action_label_ptr, int hotkey,
                          IrcitUiSnackbarActionCallback cb, void *user_ptr)
{
  int action_ok = 1;

  /* The two ways an action is refused, both of them things a caller cannot discover
     from the signature. A refused ACTION IS NOT A REFUSED MESSAGE: the notification is
     still raised, and 0 is how the caller learns the affordance is not there -- which
     matters when the data behind it has already been hidden. */
  if (cb) {
    if (!action_label_ptr || !action_label_ptr[0]) {
      IRCITUI_LOG_WARN("snackbar: an action with no label was refused -- a hotkey with nothing drawn to "
                       "advertise it is a trap rather than a reservation");
      action_ok = 0;
    } else if (hotkey < 0 || (hotkey >= 32 && hotkey <= 127)) {
      /* Printable, or not a key at all. Printable is not style: the loop's edit-field
         fast path hands every printable character straight to a focused edit field
         without ever reaching the dispatcher, so such a hotkey would fire only while no
         field held focus -- intermittently, with nothing to explain why. A negative code
         is ERR or a caller's own sentinel and can never arrive. */
      IRCITUI_LOG_WARN("snackbar: hotkey %d cannot be an action key -- it is printable, so it only reaches the "
                       "overlay while no edit field holds focus, or it is not a key at all",
                       hotkey);
      action_ok = 0;
    }
  }
  if (!action_ok) cb = NULL;

  return sShow(text_ptr, duration_ms, cb ? action_label_ptr : NULL, sNormaliseHotkey(hotkey), cb, user_ptr) && action_ok;
}

PUBLIC_API void
IrcitUiSnackbarDismiss(void)
{
  if (sSnack.state == SNACK_HIDDEN) return;
  // A request, not a teardown: the box leaves the way it arrived, and the frame that draws its last row is also the frame that puts back the cells it covered.
  sSnack.state       = SNACK_FALLING;
  sSnack.deadline_ms = 0;
}

PUBLIC_API int
IrcitUiSnackbarActive(void)
{
  return sSnack.state != SNACK_HIDDEN;
}

PUBLIC_API int
IrcitUiSnackbarKey(int key_code)
{
  IrcitUiSnackbarActionCallback cb;
  void *                        user;

  /* The action is available while the box is arriving and while it is settled, and gone
     from the moment it starts to leave: a box on its way out is not an affordance any
     more, and a key that fired one mid-departure would act on something the user can no
     longer see clearly. */
  if (sSnack.state != SNACK_RISING && sSnack.state != SNACK_VISIBLE) return 0;
  if (!sSnack.action_cb || key_code != sSnack.hotkey) return 0;

  /* Taken, then cleared, then dismissed -- and the clearing is HARDNESSING RATHER THAN
     THE MECHANISM. Mutation-tested, and the result is worth recording because the
     obvious reading of this function is wrong: remove the two clears and the callback
     still runs exactly once, because the dismiss below leaves the state FALLING and the
     guard above refuses everything from a box that is leaving. What the clears buy is
     that the property survives a later reordering of the two statements -- call the
     callback first, which is what the design that prompted this did, and the state is
     still VISIBLE with the action still installed while the callback runs, so a callback
     that re-enters this function recurses. Cheap insurance against an edit, not a thing
     the current order needs.
     The LABEL is deliberately not cleared, so the box still shows what it was offering
     while it falls. */
  cb                 = sSnack.action_cb;
  user               = sSnack.action_user;
  sSnack.action_cb   = NULL;
  sSnack.action_user = NULL;

  /* Dismissed FIRST, then the callback. The other order -- call, then dismiss -- is
     what the design that prompted this did, and it discards a notification raised from
     inside the callback, which is the documented way to report that the undo worked.
     Dismissing first also means the callback sees a box already on its way out, so a
     replacement is driven back up from where it is rather than bouncing. */
  IrcitUiSnackbarDismiss();

  cb(user);
  return 1;
}

PUBLIC_API int
IrcitUiSnackbarAdvance(void)
{
  long long now;
  long long dt;
  int       after;
  int       span_milli = sSpanRows() * 1000;

  /* Measured even while hidden: a resize that happens between two notifications
     would otherwise be applied by the next Show, from a geometry that was never on
     screen. It changes nothing on the glass, so nothing is owed. */
  if (sSnack.state == SNACK_HIDDEN) {
    sMeasure();
    return 0;
  }
  if (!IrcitUiScreen.root) return 0;

  now = sNowMs();
  dt  = now - sSnack.last_ms;
  sSnack.last_ms = now;
  if (dt < 0) dt = 0;
  /* A pass can be late for reasons that have nothing to do with the animation -- a
     stop, a debugger, a starved scheduler -- and integrating a two-second gap would
     teleport the box rather than slide it. Anything past the cap is one frame. */
  if (dt > PRIV_CONFIG_DEFAULT_SNACKBAR_TICK_CAP_MS) dt = PRIV_CONFIG_DEFAULT_SNACKBAR_FRAME_MS;

  sMeasure();

  switch (sSnack.state) {
    case SNACK_RISING:
      sSnack.offset_milli += (int)dt * PRIV_CONFIG_DEFAULT_SNACKBAR_ROWS_PER_SEC;
      if (sSnack.offset_milli >= span_milli) {
        sSnack.offset_milli = span_milli;
        sSnack.state        = SNACK_VISIBLE;
        // A duration shorter than the rise means the hold has already elapsed.
        if (now >= sSnack.deadline_ms) sSnack.state = SNACK_FALLING;
      }
      break;

    case SNACK_VISIBLE:
      if (now >= sSnack.deadline_ms) sSnack.state = SNACK_FALLING;
      break;

    case SNACK_FALLING:
      sSnack.offset_milli -= (int)dt * PRIV_CONFIG_DEFAULT_SNACKBAR_ROWS_PER_SEC;
      /* THE FALL ENDS AT THE LAST WHOLE ROW, not at zero. The last fraction of a
         row is travel the box does not have: with less than a whole row left it
         draws nothing, because a box cannot be 0.4 of a row tall. Ending at zero
         instead left the state non-hidden for up to a whole row's worth of passes
         with nothing on the glass -- Active() reporting a notification nobody can
         see, and the loop paying for a full repaint per pass to service it.
         Found by the gate-1 probe disagreeing with the glass, not by reading. */
      if (sSnack.offset_milli < 0 || sStep() <= 0) {
        sSnack.offset_milli = 0;
        sSnack.state        = SNACK_HIDDEN;
      }
      break;

    case SNACK_HIDDEN:
      break;
  }

  after = sStep();

  /* The whole of what the caller needs: has the box moved off cells that the last
     frame painted it on, or is it showing something the last frame did not. Both
     mean the tree has to repaint underneath it -- fact 2 at the top of this file. */
  return (after != sSnack.painted_step || sSnack.dirty) ? 1 : 0;
}

PUBLIC_API void
IrcitUiSnackbarRender(void)
{
  int rows = PRIV_CONFIG_DEFAULT_SNACKBAR_ROWS;
  int step, h, y;

  if (sSnack.state == SNACK_HIDDEN) {
    /* Nothing was drawn, so nothing is what the last frame painted. Without this
       the step it painted last stays on the record, and the next Advance compares
       a current step of zero against a stale one and asks for a repaint nobody
       needs. */
    sSnack.painted_step = 0;

    /* And the registers go back, because the box is gone. Since a hidden box is
       repainted never, this is reached once per journey rather than once per frame --
       which is why the release is here and not at the end of a paint. */
    if (sSnack.ramp_base >= 0) {
      IrcitUiPaletteRampRelease(sSnack.ramp_base, sSnack.ramp_width, "snackbar");
      sSnack.ramp_base  = -1;
      sSnack.ramp_width = 0;
    }
    return;
  }
  if (!IrcitUiScreen.root) return;

  sMeasure();
  /* A screen that shrank under a box already on it. Nothing can be drawn that would
     be whole, so nothing is drawn; the box is still up, and it comes back when the
     screen does -- unless the shrink also took the rows, in which case Advance will
     have finished it off. */
  if (sSnack.width < 4 || LINES < sSpanRows()) return;

  step = sStep();
  if (step <= 0) return; /* shown, but has not travelled a whole row yet */

  h = step < rows ? step : rows;
  y = LINES - step;

  if (!sSnack.win) {
    sSnack.win = newwin(h, sSnack.width, y, sSnack.x);
    if (!sSnack.win) {
      IRCITUI_LOG_WARN("snackbar: ncurses refused a %dx%d window at (%d,%d)", sSnack.width, h, sSnack.x, y);
      sSnack.state = SNACK_HIDDEN;
      return;
    }
    leaveok(sSnack.win, TRUE); /* the engine draws the only cursor there is */
  } else {
    /* Resize before the move. Either order leaves the window briefly overlapping the
       bottom edge on one of the two journeys, and neither call objects to that --
       mvwin validates the position it is given, not the one it is leaving. */
    if (sSnack.win_h != h || sSnack.win_w != sSnack.width) {
      wresize(sSnack.win, h, sSnack.width);
      sSnack.win_h = h;
      sSnack.win_w = sSnack.width;
    }
    if (sSnack.win_y != y || sSnack.win_x != sSnack.x) {
      mvwin(sSnack.win, y, sSnack.x);
      sSnack.win_y = y;
      sSnack.win_x = sSnack.x;
    }
  }

  sPaintBox(sSnack.win, h);

  /* Stamped every pass, and rewritten first -- facts 3 and 4 at the top of the file:
     the tree's own repaint has just punched through these cells, and ncurses copies
     only touched lines, so an untouched window would leave the hole it was meant to
     fill. */
  wnoutrefresh(sSnack.win);

  sSnack.painted_step = step;
  sSnack.dirty        = 0;
}

void
IrcitUiSnackbarDestroy(void)
{
  /* Hand the borrowed registers back before anything else. A teardown that skipped this
     would leave the terminal holding a gradient nobody owns -- and the OSC 104 ncurses
     emits at exit would not save it, because that resets the palette to the emulator's
     defaults rather than to what was there. */
  if (sSnack.ramp_base >= 0) {
    IrcitUiPaletteRampRelease(sSnack.ramp_base, sSnack.ramp_width, "snackbar");
    sSnack.ramp_base  = -1;
    sSnack.ramp_width = 0;
  }
  if (sSnack.win) {
    delwin(sSnack.win);
    sSnack.win = NULL;
  }
  memset(&sSnack, 0, sizeof sSnack);
  // The memset takes the sentinel with it.
  sSnack.ramp_base = -1;
}
