/**
 * @file fake_ncurses.c
 * @brief The storage behind the fake shim's instrumentation counters.
 *
 * ncurses.h declares the counters and the op log; this file defines them, once,
 * for every translation unit that includes the header. They cannot live in the
 * header: a static there is one copy per unit, so the library would increment
 * its own and a test would read a different one -- an assertion that passes or
 * fails for a reason unrelated to the code under test.
 *
 * Only linked when IRCITUI_USE_FAKE_CURSES is on; see the CMakeLists.txt branch
 * that puts this into IRCITUI_CURSES_LIBS.
 */

#include <ncurses.h>

FkOpEntry     fk_op_log[FK_OP_LOG_CAP];
int           fk_op_count;
int           fk_op_dropped;
unsigned long fk_handle_seq = FK_HANDLE_BASE; /* never rewound; see ncurses.h */
int           fk_null_win_calls;
short         fk_color_rgb[FK_COLOR_CAP][3];

/* Clear the frame's record. The handle sequence is deliberately NOT rewound: a
   node keeps the window it was realised with, and restarting the sequence would
   hand a later node a pointer equal to an earlier node's, making attribution by
   pointer meaningless. */
void
fkReset(void)
{
  fk_op_count       = 0;
  fk_op_dropped     = 0;
  fk_null_win_calls = 0;
}

void
fkLog(int op, const WINDOW *win)
{
  if (!win) fk_null_win_calls++;

  if (fk_op_count >= FK_OP_LOG_CAP) {
    /* Counted, not silently lost: a test that overruns the log should be able to
       see that its own instrument is the limit rather than the code under test. */
    fk_op_dropped++;
    return;
  }

  fk_op_log[fk_op_count].op  = op;
  fk_op_log[fk_op_count].win = win;
  fk_op_count++;
}

int
fkCount(int op)
{
  int i, n = 0;
  for (i = 0; i < fk_op_count; i++)
    if (fk_op_log[i].op == op) n++;
  return n;
}

int
fkIndexOf(const WINDOW *win, int op)
{
  int i;
  for (i = 0; i < fk_op_count; i++)
    if (fk_op_log[i].op == op && fk_op_log[i].win == win) return i;
  return -1;
}

FkWinGeom fk_win_geom[FK_WIN_CAP];

/* Handles are handed out as FK_HANDLE_BASE + n, so the slot is the handle minus
   the base. Slot 0 is never issued -- fkNewWindow pre-increments -- and is
   reserved so that a zeroed entry can be told from a real one. */
int
fkWinIndex(const WINDOW *w)
{
  uintptr_t h = (uintptr_t)w;
  uintptr_t i;

  if (!w || h < FK_HANDLE_BASE) return -1;
  i = h - FK_HANDLE_BASE;
  if (i == 0 || i >= FK_WIN_CAP) return -1;
  return (int)i;
}

void
fkWinRegister(const WINDOW *w, int begy, int begx, int maxy, int maxx, int parent)
{
  int i = fkWinIndex(w);
  if (i < 0) return;

  fk_win_geom[i].begy   = begy;
  fk_win_geom[i].begx   = begx;
  fk_win_geom[i].maxy   = maxy;
  fk_win_geom[i].maxx   = maxx;
  fk_win_geom[i].parent = parent;
}

void
fkWinMove(const WINDOW *w, int begy, int begx)
{
  int i = fkWinIndex(w);
  if (i < 0) return;

  fk_win_geom[i].begy = begy;
  fk_win_geom[i].begx = begx;
}

void
fkWinResize(const WINDOW *w, int maxy, int maxx)
{
  int i = fkWinIndex(w);
  if (i < 0) return;

  fk_win_geom[i].maxy = maxy;
  fk_win_geom[i].maxx = maxx;
}
