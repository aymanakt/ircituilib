/* The name the library actually asks for.
 *
 * ui_priv.h includes <curses.h> and <ncursesw/ncurses.h>. This directory reaches
 * the compiler through -I, so a missing <curses.h> here did not fail loudly: the
 * real /usr/include/curses.h was picked up instead, and every type and macro it
 * shares with the fake ncursesw header then collided. Supplying the name is the
 * whole job - ncurses.h carries the guard.
 *
 * Quoted rather than angled: this must resolve to the sibling fake header and
 * never to a system one, whatever the include order happens to be. */
#include "ncurses.h"
