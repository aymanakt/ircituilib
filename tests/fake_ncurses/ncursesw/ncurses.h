/* Shim, so the <ncursesw/ncurses.h> spelling reaches the same single definition.
 *
 * This file used to be a byte-for-byte copy of ../ncurses.h under the same
 * include guard. That worked only while the fake directory was the first place
 * <curses.h> resolved; once the real <curses.h> won, both bodies were live at
 * once and every shared declaration conflicted. One definition, three names. */
#include "../ncurses.h"
