#ifndef FAKE_NCURSES_H
#define FAKE_NCURSES_H
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <wchar.h>
typedef unsigned long mmask_t; typedef unsigned long chtype; typedef unsigned long attr_t; typedef struct { wchar_t ch; } cchar_t;
typedef struct WINDOW WINDOW; struct WINDOW{int dummy;};
typedef struct { int id; unsigned bstate; int x,y,z; } MEVENT;
#define OK 0
#define ERR (-1)
#define TRUE 1
#define FALSE 0
/* The screen handle, opaque: newterm() hands one back and set_term() takes it.
   The fake never looks inside, and nothing here needs it to. */
typedef struct SCREEN SCREEN;
/* Deliberately not NULL. Real curses makes stdscr an object, and the ncurses
   adapter passes it to wnoutrefresh; leaving it NULL would make every flush
   indistinguishable from a call handed a window that does not exist, which is
   the thing fk_null_win_calls exists to detect. The value cannot collide with a
   generated handle -- those start at FK_HANDLE_BASE. */
#define stdscr ((WINDOW*)(uintptr_t)1)
/* The glass as ncurses last painted it, and where the demo harness reads a frame
   back from to assert on what was drawn. A distinct non-NULL sentinel for the
   same reason stdscr is one: the read-back helper checks the window it was handed
   before asking for a cell, and NULL would make every read look like a call
   against a window that does not exist. The value is below FK_HANDLE_BASE, so it
   cannot collide with a generated window handle. */
#define curscr ((WINDOW*)(uintptr_t)2)
#define FK_HANDLE_BASE 0x1000ul
#define KEY_MOUSE 0x100
#define KEY_RESIZE 0x101
/* Shift+Up / Shift+Down. Distinct from the plain arrows above and from KEY_F(n)
   at 0x200+, because main_looper translates these two into the framework's own
   SHIFT_UP/SHIFT_DOWN and must not collide with an unshifted arrow. */
#define KEY_SR 0x102
#define KEY_SF 0x103
#define KEY_BACKSPACE 263
#define KEY_LEFT 260
#define KEY_RIGHT 261
#define KEY_UP 259
#define KEY_DOWN 258
#define KEY_ENTER 343
#define KEY_F(n) (0x200+(n))
#define BUTTON1_PRESSED (1u<<0)
#define BUTTON1_RELEASED (1u<<1)
#define BUTTON1_CLICKED (1u<<2)
#define BUTTON3_PRESSED (1u<<3)
#define BUTTON3_CLICKED (1u<<4)
#define BUTTON4_PRESSED (1u<<5)
#define BUTTON5_PRESSED (1u<<6)
/* Next free low bit. xterm reports a double click for presses that arrive in the
   same read, which is the only way the engine can see one at all. */
#define BUTTON1_DOUBLE_CLICKED (1u<<7)
/* The modifiers live in a HIGH, DISJOINT field, as they do in ncurses. A bstate
   carries a button bit AND its modifiers at once -- that is what a Ctrl-click is
   -- so a modifier sharing the low bits would be indistinguishable from the
   button it arrived with, and ui_engine's (bstate & BUTTON_CTRL) test would fire
   on a plain BUTTON3_PRESSED. */
#define BUTTON_CTRL (1u<<24)
#define BUTTON_SHIFT (1u<<25)
#define ALL_MOUSE_EVENTS 0xffffffffu
#define REPORT_MOUSE_POSITION 0x10000000u
/* THE ATTRIBUTE WORD HAS REAL FIELDS, IN NCURSES' LAYOUT: character low, colour
   in the second byte, attributes above. This replaces a set of values chosen only
   to be distinguishable from one another, and the reason is that the library does
   not merely OR them -- it takes the word apart:
 *
 *   COLOR_PAIR(p) | A_DIM              compose
 *   style & (chtype)~A_CHARTEXT        recover the non-character bits
 *   style & A_ATTRIBUTES & ~A_COLOR    attributes without the pair
 *   PAIR_NUMBER(style)                 the pair without the attributes
 *
 * With attributes at 1/2/4/8 and COLOR_PAIR(x) == x, every one of those masks cut
 * across fields: it would have returned a mixture of a colour index and an
 * attribute, and the caller would have had no way to tell. A fake whose word is
 * laid out wrong makes the library's own masking idioms untestable, which is the
 * one thing this shim exists to avoid. */
#define A_CHARTEXT 0x000000ffu /* the character itself */
#define A_COLOR 0x0000ff00u    /* the colour pair */
#define A_NORMAL 0
#define A_REVERSE 0x00010000u
#define A_BOLD 0x00020000u
#define A_DIM 0x00040000u
#define A_UNDERLINE 0x00080000u
/* What tells an ACS_ glyph apart from the letter it looks like. sPaintGlass reads
   it back off the cell it is about to overwrite. */
#define A_ALTCHARSET 0x00100000u
/* Everything but the character, colour included -- ncurses' own definition
   (~A_CHARTEXT), and the reason the library's `& ~A_COLOR` is not redundant. */
#define A_ATTRIBUTES 0xffffff00u
#define ACS_VLINE '|'
#define ACS_HLINE '-'
#define ACS_CKBOARD '#'
#define ACS_DIAMOND '*'
/* The four box corners, drawn by the snackbar's own frame. Distinct from one
   another and from ACS_HLINE/VLINE, because the point of an ACS_ glyph is that
   its A_ALTCHARSET bit tells the terminal to draw a line rather than the letter
   the code point resembles -- and a set of corners that collapsed to one
   character would make that distinction untestable. */
#define ACS_ULCORNER '+'
#define ACS_URCORNER '+'
#define ACS_LLCORNER '+'
#define ACS_LRCORNER '+'
/* The pair is PLACED, not returned bare, so that it lands in A_COLOR and the
   attribute bits stay clear of it. See A_CHARTEXT above for why that separation
   is load-bearing rather than tidy. */
#define COLOR_PAIR(x) ((chtype)(x) << 8)
#define PAIR_NUMBER(a) (((a) & A_COLOR) >> 8)
/* Colour numbers, so anything reading COLOR_WHITE compiles without curses.
   Values match ncurses: the engine stores them in short fg/bg/bdr members and
   sPairOf packs them into an init_pair() argument. */
#define COLOR_BLACK 0
#define COLOR_RED 1
#define COLOR_GREEN 2
#define COLOR_YELLOW 3
#define COLOR_BLUE 4
#define COLOR_MAGENTA 5
#define COLOR_CYAN 6
#define COLOR_WHITE 7
/* Terminal capabilities, read by the ncurses adapter to decide which path to
   take. Macros rather than the variables real ncurses declares: a variable in a
   header shared by every translation unit is one unused copy per unit, which
   the library target's -Wall -Werror turns into a build failure. Nothing here
   takes their address, which is what would need them to be objects. */
#define COLORS 8
#define COLOR_PAIRS 64
#define COLS 80
#define LINES 24

/* --- Instrumentation --------------------------------------------------------
 *
 * The stubs below record what the library asked curses to do, so that a test can
 * assert on the shape of a frame rather than only on its flags: how many nodes
 * were painted, how many were refreshed, in what order, and whether any call was
 * handed a window that does not exist.
 *
 * THE STATE IS `extern`, NOT `static`. A static in a header is one copy per
 * translation unit: the library would increment its own copy and a test would
 * read a different one, so every assertion would pass or fail for a reason that
 * has nothing to do with the code under test. fake_ncurses.c defines the state
 * once and the CMake target links it into everything that consumes this header.
 *
 * Counters are per-frame: call fkReset() between frames. Nothing inside a stub
 * can tell where one frame ends and the next begins.
 *
 * Every window is a distinct non-NULL pointer, so a logged call can be traced
 * back to the node that owns it by comparing against node->common.win. The
 * sequence is never rewound -- not even by fkReset -- because a node keeps the
 * window it was realised with, and reissuing a handle would alias it.
 */
#define FK_OP_LOG_CAP 4096
typedef enum {
  FK_OP_WBKGD,  /* a node began painting */
  FK_OP_WERASE, /* a node cleared its surface */
  FK_OP_REFRESH,/* a window was pushed into the virtual screen */
  FK_OP_TOUCH, /* a window was marked changed, which is what makes a stage copy anything */
  FK_OP_DELWIN,
  FK_OP_NEWWIN,
  FK_OP_DERWIN
} FkOp;
typedef struct { int op; const WINDOW *win; } FkOpEntry;

extern FkOpEntry     fk_op_log[FK_OP_LOG_CAP];
extern int           fk_op_count;   /* recorded entries */
extern int           fk_op_dropped; /* calls past the cap */
extern unsigned long fk_handle_seq;
extern int           fk_null_win_calls; /* calls handed a NULL window */
/* Colour registers, in ncurses' own 0-1000 channel scale. The fake STORES these
   rather than refusing them, and that is a fidelity decision with a consequence:
   IrcitUiPaletteRampBorrow probes a register by writing (1000,0,500), reading it
   back and restoring it, to find out whether the terminal lets the application
   own a ramp. A fake that answered ERR would report every register unwritable and
   send the whole palette down its fallback path, so the borrowing code would
   never run under this shim -- the one thing a fake is for. */
#define FK_COLOR_CAP 256
extern short fk_color_rgb[FK_COLOR_CAP][3];
/* Window geometry, indexed by handle. See fkWinRegister below for why the fake
   has to track this rather than answer a constant. */
#define FK_WIN_CAP 4096
typedef struct { int begy, begx, maxy, maxx, parent; } FkWinGeom;
extern FkWinGeom fk_win_geom[FK_WIN_CAP];

/* The guard is not optional: these are defined in C and called from C++ tests,
   and without it the test's translation unit expects mangled names that the
   object file does not define. The stubs below need no guard -- they are static
   inline and never leave their unit. Real <ncurses.h> carries the same guard for
   the same reason. */
#ifdef __cplusplus
extern "C" {
#endif
void fkReset(void);
void fkLog(int op, const WINDOW *win);
int  fkCount(int op);
/* Position of @p win's most recent @p op in the log, or -1 if it never had one.
   This is how a test asserts on refresh *order*, which is the mechanism ncurses
   uses to decide stacking. */
int  fkIndexOf(const WINDOW *win, int op);

/* Geometry bookkeeping. THE FAKE HAS TO REMEMBER THIS, and the reason is a test
   that failed when it did not.
 *
 * sWindowIsCurrent() decides whether a node's existing window still matches the
 * rectangle it was asked for, by comparing getmaxy/getmaxx against the requested
 * size and getbegy/getbegx against the node's position. A fake that answered a
 * constant made every one of those comparisons fail, so every node took the
 * REBUILD path -- and IrcituiPaint.ASettledTreePaintsNothing, which asserts a
 * clean tree runs no painter at all, then failed because the tree was being
 * rebuilt every frame. A stub that looks conservative can silently delete the
 * behaviour under test.
 *
 * @p w must be a handle from newwin/derwin; anything else is ignored.
 * @param parent -1 for a top-level window, else the parent's handle index. */
int  fkWinIndex(const WINDOW *w);
void fkWinRegister(const WINDOW *w, int begy, int begx, int maxy, int maxx, int parent);
void fkWinMove(const WINDOW *w, int begy, int begx);
void fkWinResize(const WINDOW *w, int maxy, int maxx);
#ifdef __cplusplus
}
#endif

static inline int delwin(WINDOW*w){fkLog(FK_OP_DELWIN,w);return 0;}
static inline WINDOW*fkNewWindow(int op){
  WINDOW *nw=(WINDOW*)(uintptr_t)(++fk_handle_seq);
  fkLog(op,nw);
  return nw;
}
static inline WINDOW*newwin(int h,int w,int y,int x){
  WINDOW*nw=fkNewWindow(FK_OP_NEWWIN); fkWinRegister(nw,y,x,h,w,-1); return nw;}
/* derwin's y/x are RELATIVE to the parent, as ncurses' are, so the absolute
   origin is the parent's plus the offset -- which is what getbegy/getbegx must
   report, and what sWindowIsCurrent subtracts to recover the relative rectangle. */
static inline WINDOW*derwin(WINDOW*p,int h,int w,int y,int x){
  WINDOW*nw=fkNewWindow(FK_OP_DERWIN);
  int pi=fkWinIndex(p);
  int py=pi<0?0:fk_win_geom[pi].begy, px=pi<0?0:fk_win_geom[pi].begx;
  fkWinRegister(nw,py+y,px+x,h,w,pi);
  return nw;}
/* ABSOLUTE, as ncurses' mvwin is and as the engine calls it. */
static inline int mvwin(WINDOW*w,int y,int x){fkWinMove(w,y,x);return 0;}
static inline int keypad(WINDOW*w,int b){(void)w;(void)b;return 0;}
static inline int leaveok(WINDOW*w,int b){(void)w;(void)b;return 0;}
/* wbkgd is the marker for "this node began painting": IrcitUiDraw calls it first
   and unconditionally, once per painted node, so counting it counts paint passes
   without the engine having to say so. */
static inline int wbkgd(WINDOW*w,unsigned long b){fkLog(FK_OP_WBKGD,w);(void)b;return 0;}
static inline int werase(WINDOW*w){fkLog(FK_OP_WERASE,w);return 0;}
static inline int wattron(WINDOW*w,int a){(void)w;(void)a;return 0;}
static inline int wattroff(WINDOW*w,int a){(void)w;(void)a;return 0;}
static inline int box(WINDOW*w,int a,int b){(void)w;(void)a;(void)b;return 0;}
static inline int mvwprintw(WINDOW*w,int y,int x,const char*f,...){(void)w;(void)y;(void)x;(void)f;return 0;}
static inline int mvwaddstr(WINDOW*w,int y,int x,const char*s){(void)w;(void)y;(void)x;(void)s;return 0;}
static inline int waddstr(WINDOW*w,const char*s){(void)w;(void)s;return 0;}
static inline int mvwaddch(WINDOW*w,int y,int x,int c){(void)w;(void)y;(void)x;(void)c;return 0;}
static inline int mvwaddnstr(WINDOW*w,int y,int x,const char*s,int n){(void)w;(void)y;(void)x;(void)s;(void)n;return 0;}
static inline int setcchar(cchar_t*c,const wchar_t*w,const int a,short p,const void*o){(void)c;(void)w;(void)a;(void)p;(void)o;return 0;}
static inline int mvwadd_wch(WINDOW*w,int y,int x,const cchar_t*c){(void)w;(void)y;(void)x;(void)c;return 0;}
static inline int mvwaddwstr(WINDOW*w,int y,int x,const wchar_t*s){(void)w;(void)y;(void)x;(void)s;return 0;}
static inline int waddch(WINDOW*w,chtype c){(void)w;(void)c;return 0;}
static inline int mvwvline(WINDOW*w,int y,int x,chtype c,int n){(void)w;(void)y;(void)x;(void)c;(void)n;return 0;}
static inline int mvwhline(WINDOW*w,int y,int x,chtype c,int n){(void)w;(void)y;(void)x;(void)c;(void)n;return 0;}
static inline int mouseinterval(int d){(void)d;return 0;}
static inline int mvwaddnwstr(WINDOW*w,int y,int x,const wchar_t*s,int n){(void)w;(void)y;(void)x;(void)s;(void)n;return 0;}
static inline int waddnwstr(WINDOW*w,const wchar_t*s,int n){(void)w;(void)s;(void)n;return 0;}
static inline int wmove(WINDOW*w,int y,int x){(void)w;(void)y;(void)x;return 0;}
static inline int mvwchgat(WINDOW*w,int y,int x,int n,int a,short p,const void*o){(void)w;(void)y;(void)x;(void)n;(void)a;(void)p;(void)o;return 0;}
/* touchwin says "every line of this window has changed". It is counted separately from
   FK_OP_REFRESH rather than folded into it, because the engine's staging is now a touch
   followed by a stage: one stage per node is no longer the same claim as one touch. */
static inline int touchwin(WINDOW*w){fkLog(FK_OP_TOUCH,w);return 0;}
static inline int wnoutrefresh(WINDOW*w){fkLog(FK_OP_REFRESH,w);return 0;}
static inline int doupdate(void){return 0;}
static inline int has_colors(void){return 0;}
static inline int start_color(void){return 0;}
static inline int init_pair(short p,short f,short b){(void)p;(void)f;(void)b;return 0;}
/* Real storage, not a refusal -- see fk_color_rgb above for what turns on it. */
static inline int init_color(short c,short r,short g,short b){
  if(c<0||c>=FK_COLOR_CAP)return ERR;
  fk_color_rgb[c][0]=r;fk_color_rgb[c][1]=g;fk_color_rgb[c][2]=b;return OK;}
static inline int color_content(short c,short*r,short*g,short*b){
  if(c<0||c>=FK_COLOR_CAP)return ERR;
  *r=fk_color_rgb[c][0];*g=fk_color_rgb[c][1];*b=fk_color_rgb[c][2];return OK;}
static inline int curs_set(int v){(void)v;return 0;}
static inline int initscr(void){return 0;}
static inline int endwin(void){return 0;}
static inline int cbreak(void){return 0;}
static inline int noecho(void){return 0;}
static inline int timeout(int t){(void)t;return 0;}
static inline int getch(void){return ERR;}
/* The three the ncurses adapter reached for once it grew a real backend: a
   screen resize, a window resize, and a blocking read. All no-ops or ERR, which
   is what the stubs above already do for their narrow counterparts. wresize
   answers OK rather than ERR so the adapter's geometry code takes its success
   path instead of bailing. */
static inline int resize_term(int l,int c){(void)l;(void)c;return 0;}
static inline int wresize(WINDOW*w,int h,int wi){fkWinResize(w,h,wi);return 0;}
static inline int wgetch(WINDOW*w){(void)w;return ERR;}
static inline int getmouse(MEVENT*e){(void)e;return ERR;}
/* The synthetic-input path. conversation_demo injects clicks with ungetmouse and
   then lets the engine's own resolution run, so this answers OK rather than ERR:
   a refusal would make the harness print "ungetmouse refused" and skip the click,
   and every assertion downstream of it would pass for having tested nothing. */
static inline int ungetmouse(MEVENT*e){(void)e;return OK;}
static inline int nodelay(WINDOW*w,int b){(void)w;(void)b;return OK;}
static inline int use_default_colors(void){return OK;}
static inline int resizeterm(int l,int c){(void)l;(void)c;return OK;}
/* The sink the harness opens: newterm() onto /dev/null, so a run makes no
   terminal output. A sentinel rather than NULL because the demo takes its
   "I have a screen" path on a non-NULL answer, and the null path is the one that
   skips everything the harness exists to do. */
static inline SCREEN*newterm(const char*t,FILE*out,FILE*in){
  (void)t;(void)out;(void)in;return (SCREEN*)(uintptr_t)1;}
static inline SCREEN*set_term(SCREEN*s){(void)s;return (SCREEN*)(uintptr_t)1;}
/* void in ncurses, and matched rather than made to return OK: a caller that
   wrote `if (delscreen(s))` against a returning stub would compile here and not
   against real curses. */
static inline void delscreen(SCREEN*s){(void)s;}
static inline int mousemask(mmask_t a,mmask_t*b){(void)a;(void)b;return 0;}
static inline int set_escdelay(int d){(void)d;return 0;}
static inline int wtimeout(WINDOW*w,int t){(void)w;(void)t;return 0;}
static inline int wattrset(WINDOW*w,int a){(void)w;(void)a;return 0;}
/* THE FAKE TRACKS NO ATTRIBUTE STATE, so this answers A_NORMAL and the
   save/restore pair around it is inert here. That makes one real behaviour
   invisible under this shim: sAddStyledScope saves with getattrs and restores
   with wattrset because wattroff(style) would take off an ENCLOSING scope's dim
   as well as its own. A test for that needs a terminal that keeps attributes --
   build/dev-test, which links real curses. Reporting a plausible-looking value
   instead would be worse than reporting none: the restore would then look
   effective while nothing had been saved. */
static inline int getattrs(const WINDOW*w){(void)w;return A_NORMAL;}
/* NOT LOGGED AS FK_OP_WBKGD, and the distinction is the whole reason this is not
   folded into wbkgd above. wbkgd is the engine's per-node paint marker, and
   counting it counts paint passes. wbkgdset is the snackbar setting a background
   without repainting; logging it under the same op would inflate every frame's
   paint count by however many times the snackbar drew itself. */
static inline int wbkgdset(WINDOW*w,chtype b){(void)w;(void)b;return 0;}

/* --- Geometry, read back from the table ------------------------------------
 *
 * The engine asks these to decide whether a node's window still matches the
 * rectangle it wanted, so they must report what was actually registered rather
 * than a constant. See fkWinRegister in the extern block above for the test that
 * failed when they did not.
 *
 * An unknown handle reports 0 / NULL: it is not a window this shim made, so
 * there is no rectangle to describe. */
static inline int getmaxy(const WINDOW*w){int i=fkWinIndex(w);return i<0?0:fk_win_geom[i].maxy;}
static inline int getmaxx(const WINDOW*w){int i=fkWinIndex(w);return i<0?0:fk_win_geom[i].maxx;}
static inline int getbegy(const WINDOW*w){int i=fkWinIndex(w);return i<0?0:fk_win_geom[i].begy;}
static inline int getbegx(const WINDOW*w){int i=fkWinIndex(w);return i<0?0:fk_win_geom[i].begx;}
static inline WINDOW*wgetparent(const WINDOW*w){
  int i=fkWinIndex(w); if(i<0||fk_win_geom[i].parent<0)return NULL;
  return (WINDOW*)(uintptr_t)(FK_HANDLE_BASE+fk_win_geom[i].parent);}

/* Reading a cell back off the screen. The fake keeps no framebuffer -- its
   windows are handles, not storage -- so both answer ERR, which is ncurses' own
   answer for a cell that holds nothing a caller can use. The engine treats it as
   "nothing to preserve" and falls back to painting the shade outright. */
static inline int getcchar(const cchar_t*c,wchar_t*w,attr_t*a,short*p,void*o){
  (void)c;(void)w;(void)a;(void)p;(void)o;return ERR;}
static inline int mvwin_wch(WINDOW*w,int y,int x,cchar_t*c){
  (void)w;(void)y;(void)x;(void)c;return ERR;}
#endif
