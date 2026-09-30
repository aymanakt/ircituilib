/**
 * @file ircitui_bar_state_type_priv.h
 * @brief A progress bar's per-node state: the value it is showing.
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
 * Private to the library: not installed. A bar node holds one of these behind
 * IrcitUiCommonState::widget_state, the per-kind extension pointer, on the same
 * terms a recycler holds an IrcitUiRecyclerState and an edit field holds an
 * IrcitUiTextEditState.
 *
 * **The value is here rather than on IrcitUiCommonState.** The glyphs and the
 * label mode are configuration -- they come from the template and are borrowed
 * string literals and an enum -- but the value is live data the application
 * writes and a worker thread may report, and putting it on the common struct
 * would add 4 bytes to all 56 nodes of the model to serve two kinds. The split
 * is the one the extension pointer exists for.
 *
 * **Only the setter writes it.** sBarPaint reads it and nothing else does, which
 * is what keeps the drawn bar and any future hit test from disagreeing: both go
 * through sBarSplit() with the same width, and that width comes from sBarTrackW()
 * -- one function, so that an adjacent label's reserved columns cannot be counted
 * by the painter and not by the reader. See C24.
 */
#ifndef IRCITUILIB_IRCITUI_BAR_STATE_TYPE_PRIV_H
#define IRCITUILIB_IRCITUI_BAR_STATE_TYPE_PRIV_H

/* The seek callback's signature, which this struct stores one of. Named rather than left
   to arrive transitively through ui.h: this header is included after it in every current
   caller, so it would compile either way -- and a header that compiles by accident of
   include order is the kind of thing that breaks when somebody reorders one. */
#include "ircituilib/ircit_ui_seek_callback_type.h"

/**
 * @struct IrcitUiBarState
 * @brief What a progress bar is currently showing.
 */
typedef struct
{
  /**
   * @brief The position, as a whole percentage in [0, 100].
   *
   * An integer rather than a float, and that is deliberate. The design document
   * carries `float progress` spanning [0.0f, 1.0f]; a terminal cell is not
   * divisible, so every read of it would be a rounding, and the rounding is the
   * one thing that must not differ between the painter and a hit test. An
   * integer makes the rounding happen exactly once -- in IrcitUiBarSetValue --
   * and be visible there.
   *
   * 0 is a real value, not "unset": a bar at 0% is a bar, and the label modes
   * read it. So there is no sentinel, and a zeroed state is a bar at rest.
   */
  int value;

  /**
   * @brief How far the bar has been BUFFERED, as a whole percentage in [0, 100].
   *
   * The second of the bar's two positions, and the one a streaming source needs:
   * `value` is where playback has reached, this is how far the data behind it has
   * arrived. A stalled stream is one where the two converge; a healthy one is where
   * this runs ahead.
   *
   * **DRAWN FROM `value` TO HERE, and never behind it.** A buffer further back than
   * the play head is the ordinary state of a stalled stream, not a run drawn
   * backwards, so the painter clamps rather than iterating a negative span.
   *
   * **NO SENTINEL, for the reason `value` has none.** A zeroed state is a bar with
   * nothing buffered, which is both the correct reading and what renders identically
   * to a bar that never had a buffer at all -- so a new field cannot change the
   * appearance of any existing layout, and `IrcitUiBarBuffered` answering 0 for an
   * untouched bar is a true answer rather than a missing one. Adding an is-set flag
   * would buy a distinction the widget has no use for and would contradict the rule
   * stated on `value` three fields up.
   */
  int buffered;

  /* --- the gradient, when the application has asked for one.
   *
   * A ramp is a BORROWED RESOURCE -- at most four exist process-wide
   * (PRIV_CONFIG_DEFAULT_PALETTE_RAMPS) and the canopy, the fade and the notification
   * box already compete for them -- so this state owns one only while `gradient` is set,
   * and `ramp_base` is the handle: -1 means "asked for, could not be had".
   *
   * THAT -1 IS NOT AN ERROR AND MUST NOT BE TREATED AS ONE. ui_palette_priv.h says a
   * caller that gets -1 must draw without the ramp, and the fallback is the flat pair
   * rather than a broken gradient. It is kept in the state rather than collapsed into
   * `gradient` precisely so it can be READ BACK: the application asked for a gradient and
   * the framework could not supply one, and a bar that silently draws flat is
   * indistinguishable from a bar whose gradient is subtle. The demo prints this.
   *
   * `ramp_steps` is the resolution the ramp was acquired at -- a FIXED number and not the
   * bar's width, which is the one design correction this track makes to the document's
   * gradient. The document acquires one register per FILLED COLUMN, so the ramp's size
   * tracks the value and every value change is a release and a re-acquire: a malloc, a
   * color_content() per register, an init_color() per register, then the writes. A fixed
   * resolution means the register set is stable for the life of the bar, nothing is
   * re-acquired as the value moves, and RampSet runs only when a step's colour actually
   * changes -- which is when the endpoints are set, and not otherwise. */
  int gradient;    /**< Non-zero while a gradient has been asked for. */
  int ramp_base;   /**< The borrowed register base, or -1 if none could be had. */
  int ramp_steps;  /**< The resolution the ramp was acquired at. */
  int ramp_written;/**< Non-zero once the ramp's steps have been loaded. */
  short from_rgb[3]; /**< The gradient's colour at the bar's start. */
  short to_rgb[3];   /**< The gradient's colour at the bar's filled end. */

  /* --- the seek binding, when the application has asked for one.
   *
   * Kept here rather than in the node's interactions_user block, and the difference is
   * which of the two vocabularies it belongs to. interactions_user holds the framework's
   * eight listeners -- on_click, on_draw and the rest -- and is what the generator emits
   * per node and what the dispatcher consults. A seek is not one of the eight: it is a
   * bar's own gesture, reported to the application as a position, with a phase
   * vocabulary of its own. Adding a ninth listener slot to serve one widget's gesture
   * would put a bar's concern into every node's struct.
   *
   * Both are NULL until IrcitUiBindSeek resolves an id onto them. A bar with no binding
   * still tracks a drag -- the engine takes sDragNode and the release branch clears it --
   * but reports nothing, which is what a decorative progress bar wants. */
  IrcitUiSeekCallback on_seek;
  void *             seek_user;

  /* --- what a setter wrote, and the only copy of it once the layout's own is gone.
   *
   * The three the layout declares -- the two track glyphs and the label mode --
   * are in the node's declaration block, which is const and shared between bars
   * that declared the same thing. A setter therefore cannot write there, and it
   * writes here instead. NULL and -1 mean "no override", and the painter reads
   * the block, then the framework's default.
   *
   * -1 rather than 0 for label_mode because 0 is IRCITUI_BAR_LABEL_OFF, a real
   * mode: a zeroed state would shadow a declared ADJACENT, which is the same
   * trap mark_fg and sticky_header_type document. */
  const char *fill_glyph;   /**< IrcitUiBarSetGlyphs' fill, or NULL. */
  const char *empty_glyph;  /**< IrcitUiBarSetGlyphs' empty, or NULL. */
  int         label_mode;   /**< IrcitUiBarSetLabelMode's mode, or -1. */
} IrcitUiBarState;

#endif /* IRCITUILIB_IRCITUI_BAR_STATE_TYPE_PRIV_H */
