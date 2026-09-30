/**
 * @file ircitui_recycler_type_priv.h
 * @brief The recycler's per-node state: its adapter, its row template, and the
 *        pool of row nodes it owns.
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
 * Private to the library: not installed. A recycler node holds one of these
 * behind IrcitUiCommonState::widget_state, the per-kind extension pointer, on
 * the same terms an edit field holds an IrcitUiTextEditState.
 */
#ifndef IRCITUILIB_IRCITUI_RECYCLER_TYPE_PRIV_H
#define IRCITUILIB_IRCITUI_RECYCLER_TYPE_PRIV_H

#include <stddef.h>
#include <stdint.h>

#include "ircituilib/ircit_ui_node_type.h"
#include "ircituilib/ircit_ui_recycler_adapter_type.h"
#include "ircituilib/ircit_ui_selection_mode_enum.h"

/*! "No dataset position." A size_t sentinel rather than a separate flag: the
 *  value it stands in for is a valid dataset position, and 0 is one, so the
 *  absent case needs a value the dataset cannot produce.
 *
 *  Two fields carry it, and they mean the same thing in both: a recycler with no
 *  section above the viewport, and a recycler with no row being edited. */
#define IRCITUI_NO_SECTION ((size_t)-1)

/* The adapter -- what the application hands over and what it is told back -- is
   a PUBLIC type now: include/ircituilib/ircit_ui_recycler_adapter_type.h. It was
   declared here while the only consumer was this tree's own demo, which made the
   widget unbindable from outside the repository. The state below stays private:
   it is the framework's own bookkeeping, and the two callbacks above are the
   whole of what an application needs. */
typedef struct IrcitUiRecyclerState {
  IrcitUiRecyclerAdapter adapter;   ///< The application's binding, normally set
                                    ///< after construction.

  /*! The row template's id, borrowed and never freed: the generator owns the
   *  literal it points at, and a C-API caller owns the string it passed. A copy
   *  into a fixed buffer was the alternative and was rejected -- it would be a
   *  copy of a string that outlives the node, for no gain.
   *
   *  The HOMOGENEOUS case only. With a view-type registry below, this is NULL
   *  and the registry decides every row's template instead; the two are
   *  mutually exclusive in a declaration (C21). */
  const char *template_id;

  /*! The view-type registry of a heterogeneous list, borrowed from the node
   *  exactly as template_id is. n_view_types of 0 means homogeneous and this
   *  pointer is never read.
   *
   *  It is what makes a row's SHAPE a function of its position: the slot bound to
   *  position P must be a clone of the template the registry maps the
   *  application's type for P to, so scrolling across a type boundary rebuilds
   *  the slots that crossed it rather than rebinding them. That is the whole
   *  cost of heterogeneity, and it is why the pool's size does not change --
   *  only the shape of some of its slots. */
  const IrcitUiViewTypeMapping *view_types;
  int                           n_view_types;

  /*! The type whose rows are section dividers and are also pinned to the top of
   *  the viewport. -1 is off, and negative rather than 0 because 0 is a
   *  legitimate type_id. C22 requires it to name an entry view_types declares. */
  int sticky_type;

  /*! The pinned header: ONE node, a clone of the sticky type's template, held as
   *  the LAST element of container.children.
   *
   *  Last, and that placement is load-bearing twice over. It keeps the rows at
   *  indices 0..n-1, so the binding arithmetic `scroll_offset_y + i` that
   *  sRowBoundIndex, IrcitUiRecyclerRowAt, the scroll clamp and sCursorFollow all
   *  rest on is untouched by this feature; and it makes the header the last thing
   *  IrcitUiDraw recurses into, which is what puts it on top of the row it
   *  covers. The two go together -- a header at index 0 would keep the arithmetic
   *  honest and paint underneath everything.
   *
   *  NULL when the feature is off. It is a child and not a second node store
   *  beside the array, for the reason SS14's pool partitioning was rejected: all
   *  seven tree-wide walks reach a node through container.children and nothing
   *  else, so a node kept anywhere else is not realised, not painted, not
   *  hit-tested and not freed. */
  IrcitUiNode *header_node;

  /*! The section currently shown by header_node: the dataset position of the
   *  last divider at or before scroll_offset_y, or IRCITUI_NO_SECTION when there
   *  is none above the viewport. Held rather than recomputed so the pass can
   *  tell a pin that MOVED from one that did not, which is the same question
   *  prev_offset answers for the rows and is asked for the same reason: a header
   *  whose section has not changed has nothing to rebind. */
  size_t pinned_section;

  /*! The dataset position shown at the top of the viewport. What scrolling
   *  moves; the pool never moves, only its bindings do. */
  size_t scroll_offset_y;

  /*! Rows each pool entry occupies. Floored to 1 at construction, because it is
   *  a divisor in the layout pass and a zero would be a division by zero rather
   *  than a degenerate layout. */
  int item_fixed_height;

  /*! Rows the viewport actually shows, i.e. the content height divided by
   *  item_fixed_height. Written by the layout pass and read by the input route,
   *  because the pool is NOT this number: the pool carries SS13's cache margin
   *  of two rows beyond it. Following a cursor with the pool size would let the
   *  cursor come to rest in that margin, scrolled out of sight.
   *
   *  Zero before the first layout pass, which the input route treats as "no
   *  viewport yet" rather than dividing. */
  size_t visible_rows;

  /*! The scroll offset the previous layout pass ran with.
   *
   *  This is what decides whether a row has been rebound to a different dataset
   *  entry: rows are bound as `scroll_offset_y + slot`, so when the offset moves,
   *  EVERY row in the pool is about to show something else, and when it has not
   *  moved, no row's identity changed at all. One number answers a question that
   *  would otherwise need a per-row record of what each was last bound to --
   *  SS24.1.2's stale-state trap, without SS24.1.3's per-row cache.
   *
   *  Equal to scroll_offset_y initially, so the first pass clears nothing: the
   *  rows it builds are fresh clones with no history to clear. */
  size_t prev_offset;

  /*! The pivot a contiguous range is expanded from, and whether one is set.
   *
   *  A PAIR rather than a sentinel, and that is the opposite of what mark_fg and
   *  sticky_header_type do -- deliberately. Those two needed a value below zero
   *  because their zero is a LEGAL value (black, type 0), so an unset field
   *  sharing it would make the real one unaskable. Here the datum and the flag
   *  are separable: selection_anchor_index is only ever read when
   *  has_selection_anchor is set, so 0 is a perfectly good anchor and there is
   *  nothing for a sentinel to buy. A pair also cannot be got wrong by a caller
   *  that forgets which negative means what.
   *
   *  Both are zero after calloc, which is the correct default: no anchor, and an
   *  index nothing reads. Nothing has to write them at construction. */
  size_t selection_anchor_index;
  int    has_selection_anchor;

  /*! Fork A's active row: the dataset index the keyboard acts on. It is kept
   *  inside the viewport -- see the input route -- while the selection set below
   *  is not, so a marked row may be scrolled well out of sight and still be
   *  marked. SS16 called this last_selected_index. */
  size_t cursor_index;

  /*! The selection set, one bit per DATASET index, little-endian within each
   *  word: bit (index & 31) of word (index >> 5). Sized to the dataset by
   *  growing on demand, and owned by this struct -- sFreeWidgetState frees it.
   *
   *  Keyed by dataset index and not by pool slot, which is the whole point:
   *  SS16 names storing selection on the visual child as the anti-pattern,
   *  because a row is rebound as the list scrolls and the mark would follow the
   *  pool rather than the item. Nothing on a row node records whether it is
   *  selected; the painter asks this array using the index the row is bound to.
   *
   *  This is the one allocation in the widget that does not stay virtual: it is
   *  O(item_count), 1.25 KB for ten thousand rows. Inherited from SS16, and
   *  SS17 snapshots it, so it cannot be avoided by keying on the pool instead.
   *
   *  NULL until the first selection; bit_words is its capacity in words, never
   *  its population -- a set with one bit in it still has item_count/32 words. */
  uint32_t *selected_bits;
  size_t    bit_words;

  /*! How many rows may be marked at once, defaulting to
   *  IRCITUI_SELECT_SINGLE: a list that marks one row at a time is what a user
   *  expects of a list, and multi-select is opted into. Enforced in
   *  IrcitUiRecyclerSelect, which is the single funnel the click and SPACE both
   *  go through, so the mode cannot be honoured on one path and not the other.
   *
   *  The mode constrains the size of the set, not the act: marking is a toggle
   *  in every mode, so under SINGLE a second click on the marked row clears it
   *  rather than being ignored. */
  IrcitUiSelectionMode selection_mode;

  /*! The attribute a selected row is drawn with, an ncurses attribute mask
   *  (A_REVERSE and friends), applied in addition to the row's colour pair.
   *  SS16's highlight_attr_mask. The style-override machinery could not serve
   *  here: IrcitUiStyleOverride carries fg/bg/bdr and no attribute, so it can
   *  recolour a row but cannot reverse it. */
  uint32_t highlight_attr_mask;

  /*! The dataset position whose row is an inline edit field, or
   *  IRCITUI_NO_SECTION when no row is being edited -- SS26.
   *
   *  IT IS WRITTEN EXPLICITLY AT CONSTRUCTION, and that is not tidiness. The
   *  sentinel's whole job is to be a position the dataset cannot produce, and
   *  the test that reads it is `position == editing_index` -- so a field left at
   *  calloc's zero would not mean "nothing is being edited", it would mean
   *  "row 0 is being edited" and would hand row 0 an input field uninvited. This
   *  is the third field in this widget to carry a value whose zero is legal
   *  (mark_fg, sticky_type, and now this), and the only one where the sentinel is
   *  forced rather than chosen: there is no separate flag, because there is
   *  nothing for one to hold that the position does not already say.
   *
   *  A row in this position is a clone of editing_type_id's template instead of
   *  the shape the application's discriminator asks for -- see sSlotTemplate,
   *  which is the whole of the intercept. */
  size_t editing_index;

  /*! The registry type an editing row takes. -1 is off, and negative rather than
   *  0 for the reason everywhere else in this file: 0 is a legitimate type_id.
   *
   *  Not a YAML key. The type names a template the layout already declares with
   *  `view_types`, so a declaration has nothing left to say about it; what it
   *  cannot express is *when* a row is being edited, which is a run-time fact and
   *  arrives through IrcitUiRecyclerBeginEdit. The same split as selection_mode,
   *  and with the same consequence: an application must configure it from C. */
  int editing_type_id;

  /*! The last press this list saw, and where it landed -- SS27's double-click.
   *
   *  A PAIR, like the selection anchor, and for a sharper reason than that one:
   *  a lone timestamp cannot say whether it is a real press or a fresh calloc.
   *  last_press_ns of 0 is a legal monotonic reading on a machine that booted
   *  moments ago, and a first press 200 ms later would then measure a delta inside
   *  the window and open an editor nobody asked for. The index settles it: the
   *  sentinel is above every dataset position, so a fresh state cannot match
   *  itself, and no separate flag is needed.
   *
   *  Held on the STATE rather than in a static because two lists in one
   *  application must not share a gesture -- a click on one is not the second half
   *  of a click on the other.
   *
   *  Nanoseconds from CLOCK_MONOTONIC, and int64_t rather than long: see
   *  IRCITUI_DOUBLE_CLICK_NS for why that is not a detail. */
  int64_t last_press_ns;
  size_t  last_press_index;

  /*! Set by IrcitUiRecyclerBeginEdit and cleared by the layout pass that gives
   *  it what it is waiting for.
   *
   *  The focus cannot be moved by BeginEdit itself. The row at that position is
   *  still the STANDARD shape when BeginEdit runs -- the pass has not rebuilt it
   *  yet -- and the pass frees that node when it does, so focusing it there would
   *  leave IrcitUiFocus on freed memory. So BeginEdit asks for the pass and this
   *  flag is how the request survives to the point where the field exists. */
  int edit_focus_pending;
} IrcitUiRecyclerState;

#endif //IRCITUILIB_IRCITUI_RECYCLER_TYPE_PRIV_H
