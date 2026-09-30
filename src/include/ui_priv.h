/**
 * @file ui_priv.h
 * @brief Module-private declarations shared by ircituilib's translation units:
 *        the node state structs, the key-code names, and the internal limits.
 *
 * Not installed and never included from outside the library.
 */
#ifndef IRCITUI_UI_PRIV_H
#define IRCITUI_UI_PRIV_H
#define _XOPEN_SOURCE 700
#define _XOPEN_SOURCE_EXTENDED 1

#include <curses.h>
#include <ncursesw/ncurses.h>
#include "ircituilib/ui.h"
#include "ircituilib/ircit_ui_interaction_state_type.h"
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <signal.h>
#include <setjmp.h>
#include <time.h>
#include "ircituilib/ircitui_context_type.h"
#include "ircitui_event_dispatcher_type_priv.h"
#include "ircitui_event_type_priv.h"
#include "ircitui_decl_override_type_priv.h"

/* Key codes the engine compares against rather than passing through.
 *
 * ncurses names the function keys and KEY_MOUSE, but the plain ASCII control codes a text
 * editor needs have no portable spelling, so they are named here rather than written as bare
 * numbers at each comparison. The values are the ASCII ones, so what the engine accepts is
 * unchanged. */
#define IRCITUI_KEY_BS      8   /**< Backspace, for terminals that report it as
                                     ASCII rather than KEY_BACKSPACE. */
#define IRCITUI_KEY_CTRL_C  3   /**< Interrupt; the editor copies with it. */
#define IRCITUI_KEY_CTRL_V 22   /**< Literal-next; the editor pastes with it. */
#define IRCITUI_KEY_CTRL_U 21   /**< Kill; the editor empties the field and returns the caret. */
#define IRCITUI_KEY_CTRL_X 24   /**< Cancel; the editor deletes from the caret to the end. */
#define IRCITUI_KEY_ESC    27   /**< Escape; closes the top popup layer. */
#define IRCITUI_KEY_SPACE  32   /**< First code in the printable range. */

/* Shifted navigation, as CODES rather than a modifier flag: this layer's contract is that a
 * keycode arrives and the router matches it, and nothing above carries a modifier bit.
 *
 * The terminal cannot deliver these as ncurses keys. xterm-256color maps kLFT/kRGT to
 * KEY_SLEFT/KEY_SRIGHT but declares no shifted up/down entry (`infocmp` shows none, and no
 * modifier-numbered variant either), so \E[1;2A and \E[1;2B match nothing and arrive as raw
 * bytes. main_looper assembles them into these codes on the way in.
 *
 * Numbered above KEY_MAX (0777): ncurses owns everything at or below it, so a code in that
 * range would be shadowed by whichever key ncurses had already assigned. */
#define IRCITUI_KEY_SHIFT_UP    0x200
#define IRCITUI_KEY_SHIFT_DOWN  0x201
/* Ctrl+Enter, assembled by main_looper. The terminal may send nothing at all: a default xterm
 * puts it on the wire as a bare CR, indistinguishable from Enter, and sends a sequence only
 * under modifyOtherKeys or the kitty/CSI-u protocol. An application that binds this key must
 * also bind something that works without it. */
#define IRCITUI_KEY_CTRL_ENTER  0x202
#define IRCITUI_KEY_DEL   127   /**< Delete; first code past that range. */

/* SS27's double-click window, and the arithmetic that keeps it target-independent.
 *
 * Macros rather than literals because of width, which is the whole reason they exist: a
 * CLOCK_MONOTONIC timestamp in nanoseconds passes 2^31 -- the largest signed 32-bit value --
 * after about two seconds of uptime, so a `long` holding one overflows on a 32-bit target and
 * the subtraction that measures the interval stops meaning anything. The symptom would be a
 * double-click that works on one target and not another, from identical source. INT64_C makes
 * each literal 64-bit wherever it is compiled, and the timestamps compared against it are
 * int64_t for the same reason.
 *
 * The window is a duration, not a deadline, so the clock must be monotonic: see sMonotonicNs. */
#define IRCITUI_NS_PER_SEC         INT64_C(1000000000)
#define IRCITUI_NS_PER_MS          INT64_C(1000000)
#define IRCITUI_DOUBLE_CLICK_MS    INT64_C(300)
#define IRCITUI_DOUBLE_CLICK_NS    (IRCITUI_DOUBLE_CLICK_MS * IRCITUI_NS_PER_MS)

/* The interaction state's definition is in an installed header,
   ircituilib/ircit_ui_interaction_state_type.h, included above: the router entry points take
   this type by pointer, and generated/ui_generated.c writes both listener blocks as a literal
   initialiser, so anything compiling the generator's output must be able to see the layout. */

/* ufsrvrxlib is not a dependency of this library, so the scheduler is held through a pointer
   and the struct stays incomplete. */
struct UfsrvScheduler;


typedef struct IrcitUiCommonState
{
  const char *id;
  IrcitUiKind kind;
  IrcitUiDim layout_w, layout_h;
  int margin[4], padding[4];
  unsigned flags;
  short fg, bg, bdr;
  IrcitUiGravity gravity, layout_gravity;
  int c_top_top, c_top_bot, c_bot_top, c_bot_bot, c_start_start, c_start_end, c_end_start, c_end_end;

 /* //todo this is highly problematic and multiplexed member. must untangle ASAP.
   Who provides the storage — five different owners

   ┌────────────────────────────────────────┬───────────────────────────────────────────────────────────────────────────────────────────────────┬───────────┐
   │                Provider                │                                               Site                                                │ Lifetime  │
   ├────────────────────────────────────────┼───────────────────────────────────────────────────────────────────────────────────────────────────┼───────────┤
   │ string literals in the generated table │ sApplyDef → ui_engine.c:4509 (def_ptr->text ?: ""), from .text="Help" in generated/ui_generated.c │ static    │
   ├────────────────────────────────────────┼───────────────────────────────────────────────────────────────────────────────────────────────────┼───────────┤
   │ a layout blob (canvas)                 │ ui_engine.c:5529                                                                                  │ static    │
   ├────────────────────────────────────────┼───────────────────────────────────────────────────────────────────────────────────────────────────┼───────────┤
   │ the host's own buffer                  │ IrcitUiNodeSetText :276, IrcitUiNodeSetTextLive :285                                              │ host's    │
   ├────────────────────────────────────────┼───────────────────────────────────────────────────────────────────────────────────────────────────┼───────────┤
   │ the node's own ebuf                    │ main_looper.c:98                                                                                  │ the node  │
   ├────────────────────────────────────────┼───────────────────────────────────────────────────────────────────────────────────────────────────┼───────────┤
   │ the suggestion panel's sSuggestState   │ ircitui_edit_text.c:499                                                                           │ the panel │
   └────────────────────────────────────────┴───────────────────────────────────────────────────────────────────────────────────────────────────┴───────────┘

   Those last two are the interesting ones: they are the only places where the bytes behind a node's common.text belong to something other than a static table or the host — and both overwrite whatever the layout declared, permanently.

   main_looper.c:89-103 is worth reading in full: only IRCITUI_EDIT_TEXT has storage of its own, so a result for any other kind is dropped with a log line naming the gap ("kind %d owns no text storage").

   Writers — six, and readers — 34

   ┌────────────────────┬─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┐
   │      Purpose       │                                                                    Sites                                                                    │
   ├────────────────────┼─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┤
   │ measuring / wrap   │ :822, :830 (caption via sDispCols), :869 (emoji grid column count)                                                                          │
   ├────────────────────┼─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┤
   │ painting           │ :2941/:2974 (the draw prologue's s), :3025 (check/radio captions), :3036 (emoji grid), :5381 region (canvas), :3687/:3854 (drag/click text) │
   ├────────────────────┼─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┤
   │ accessor           │ IrcitUiNodeGetText :3232 — returns it raw                                                                                                   │
   ├────────────────────┼─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┤
   │ hit-test / parsing │ emoji cells :3432/:3453, emoji click :5295/:5303, image path ui_image.c:167                                                                 │
   ├────────────────────┼─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┤
   │ identity           │ :4465-4466 — a node whose text is "close" is treated as the close button, id or no id                                                       │
   ├────────────────────┼─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┤
   │ group label        │ IrcitUiGroupSelectedValue :5579/:5580/:5590                                                                                                 │
   ├────────────────────┼─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┤
   │ lint / verify      │ ui_verify.c:81, :85, :182                                                                                                                   │
   └────────────────────┴─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┘

   The structural fact: there are two string channels, and one shadows the other

   common.text is not the only place a node's string can live — common.interaction_defaults.user is the other, and seven sites resolve user first:

   ┌─────────────────────┬────────────────────────────────────┐
   │        File         │               Lines                │
   ├─────────────────────┼────────────────────────────────────┤
   │ ui_engine.c         │ 2939, 2972, 3685, 3852, 4477, 5381 │
   ├─────────────────────┼────────────────────────────────────┤
   │ ircitui_edit_text.c │ 1156 (sEditVisibleText)            │
   └─────────────────────┴────────────────────────────────────┘

   That shadowing is load-bearing, and it is documented as the whole reason IrcitUiEditTextSetValue exists. Four consequences, each evidenced:

   1. For an edit field common.text is the placeholder, not the value. The field paints state->text_buffer and falls back to common.text only while the buffer is empty (ircitui_edit_text.c:1087). So IrcitUiNodeSetText on a field writes something the user never edits.
   2. const is nominal. ui_engine.c:5425 casts it away and writes through it — ((char*)node_ptr->common.interaction_defaults.user)[0] = 0; — reaching the same object via the other channel. A node's text is only as read-only as the channel you arrived through.
   3. main_looper.c:98 replaces the declared text outright for an edit field, pointing it at ebuf. The edit field therefore has three strings in play: text_buffer (the value), common.text (its own buffer, via the async path), and ebuf.
   4. ircitui_edit_text.c:499 repoints a suggestion row's common.text into the panel's fixed candidate pool — sound because the pool is stable, but it is a node whose text lives in another object's allocation.
  */
  const char *text;




  int is_dirty;
  IrcitUiInteractionState interaction_defaults; /* Framework-supplied callback listeners. */

  /* Per-widget state, allocated by kind in sAllocWidgetState and NULL on the kinds that carry
   * none: conversation state, text-edit state, bar state, recycler state, and the declaration
   * overrides for the kinds that carry mark_fg or orientation. Callers null-check it.
   *
   * Allocated in IrcitUiClone, the only inflate path, and released in sFreeTree -- NOT in
   * IrcitUiDeflate, which strips the window and leaves the node live: state released there
   * would not come back, because Clone does not run again until the tree is rebuilt.
   * IrcitUiPopStacked frees a node directly rather than through sFreeTree, so it releases this
   * itself.
   */
  void *widget_state;

  /* The kind's declarative attributes: the const block the generator emitted for this node, or
   * NULL when the layout declared none.
   *
   * Borrowed and never freed, like theme_class: the generator points it at a static const block
   * in ui_generated.c that outlives every tree built from the model, and IrcitUiClone copies
   * the pointer with the rest of the struct. One block is shared by every node that declared
   * the same thing, which holds because nothing writes through it -- a value a host can
   * retarget is written to the model's widget_state instead, and this is only its baseline.
   */
  const void *declarative_attributes_aux;

  struct IrcitUiNode *      parent;
  WINDOW *                  win;
  int                       x, y, w, h;
  const struct IrcitUiNode *ref;
  const char *              theme_class;
} IrcitUiCommonState;

typedef struct IrcitUiContainerState
{
  struct IrcitUiNode **children;
  int                  n_children;
  int                  columns;
  int                  scroll_y;
  int                  sel0;
  int                  sel1;
  /* Whether a collapsible container is folded. A persistent property, not transient state: it
   * survives IrcitUiLayout, IrcitUiRealize and IrcitUiDeflate the way scroll_y does, so a node
   * the generator declared folded (layout YAML 'collapsed') arrives at the screen tree already
   * folded. IrcitUiClone copies it with the rest of the struct.
   *
   * Mutable through IrcitUiSetCollapsed. IrcitUiScreenReset rebuilds the screen tree by cloning
   * the reference, so a runtime toggle is not preserved across one.
   *
   * Meaningless unless IRCITUI_F_COLLAPSIBLE is set on the same node. */
  bool                 is_collapsed;
} IrcitUiContainerState;

// The public, opaque interface boundary application code interacts with.
typedef struct IrcitUiNode
{
  IrcitUiCommonState      common;
  IrcitUiContainerState   container;
  /* User-supplied callback listeners; can be defined in yaml, or programmatically. */
  IrcitUiInteractionState interactions_user;
} IrcitUiNode;

/* The colour-pair registry, in its own header. A pair is obtained by including this and calling
   the allocator. */
#include "ui_palette_priv.h"

/* The dispatcher, the last-mouse store and the framework defaults are declared in ui.h, which
   this header includes above. */

#endif
