/**
 * @file ircit_ui_recycler_adapter_type.h
 * @brief What a recycler asks its application for, and what it tells it back.
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
 * Public: this is the whole of the surface an application needs to put data into
 * a recycler. It was private until the activation callback was added, at which
 * point the type living behind a header that is not installed meant no
 * application outside this tree could bind a list at all.
 *
 * Install it with @ref IrcitUiRecyclerSetAdapter, on the **model** node: a screen
 * tree is a clone of the model, and sAllocWidgetState copies the adapter from the
 * node a clone came from, so configuring the model once is what makes every tree
 * the framework builds from then on able to bind.
 */
#ifndef IRCITUI_IRCIT_UI_RECYCLER_ADAPTER_TYPE_H
#define IRCITUI_IRCIT_UI_RECYCLER_ADAPTER_TYPE_H

#include <stddef.h>

#include "ircituilib/ircit_ui_node_type.h"
#include "ircituilib/ircit_ui_view_type_mapping_type.h"

/*!
 * @struct IrcitUiRecyclerAdapter
 * @brief Where a recycler's rows come from, and what to do with each one.
 *
 * @details The application fills this in. The recycler never reads the dataset
 *          itself: it asks how many rows there are, and hands one row node to
 *          on_bind_view per visible position so the application can fill it.
 *
 *          **Zero-initialise it** — @c "IrcitUiRecyclerAdapter a = {0};" or a
 *          designated initialiser, which zeroes what it does not name. Every
 *          field but @c item_count is optional, and "not set" is spelled NULL,
 *          so a member left indeterminate is not a default: it is a garbage
 *          function pointer the engine may call. That is not a new hazard, but
 *          @ref get_item_view_type is the first field where the engine calls it
 *          without a stronger gate than "not NULL", so an uninitialised struct
 *          now fails by jumping rather than by binding nothing.
 *
 *          The exposure is bounded: the engine only reaches the discriminator
 *          when the node carries a view-type registry, which only a layout
 *          declaring <tt>view_types</tt> produces. A recycler bound the ordinary
 *          way — @c item_template, no registry — never calls it, whatever the
 *          struct holds.
 */
typedef struct IrcitUiRecyclerAdapter {
  size_t item_count;   ///< Rows in the dataset, which the pool never holds all of.

  /*! The view type of the row at @p position: a tag that
   *  @ref IrcitUiViewTypeMapping::type_id on the recycler's registry answers to.
   *
   *  **Optional, and its absence is the homogeneous case.** With it NULL the
   *  recycler behaves exactly as it did before view types existed: every row is
   *  a clone of the one template the node named, and nothing about the pool
   *  changes as the dataset scrolls. With it set, a pool slot whose shape does
   *  not match the type of the position it is about to show is rebuilt from the
   *  matching template -- so a dataset may mix row shapes, at the cost of that
   *  one slot being freed and re-cloned when the shape changes under it.
   *
   *  The same predicate the application would write for itself: the framework
   *  reads the dataset through this and through nothing else, and it is called
   *  once per pooled slot per layout pass, plus once per slot whose template has
   *  to be resolved. It must therefore be cheap, side-effect free, and stable --
   *  a tag that changes for the same position between two calls in one pass
   *  would have the pool rebuilt twice for that slot.
   *
   *  A tag with no entry in the registry is not an error the framework can
   *  recover from on the application's behalf; the slot is left blank for that
   *  pass. Returning a tag outside the registry is how an application says "no
   *  row here" if it wants a hole in the list. */
  int (*get_item_view_type)(size_t position, void *user_dataset);

  /*! Puts the data for @p position into @p item_node. Called on every layout
   *  pass for every visible row, including rows whose position has not changed,
   *  so a binder must be idempotent rather than incremental.
   *
   *  The node's own history is not the binder's problem: the framework clears the
   *  volatile state a previous position left behind before calling this, so a
   *  binder writes what this position needs and nothing more. */
  void (*on_bind_view)(IrcitUiNode *item_node, size_t position, void *user_dataset);

  /*! Reports that the user chose @p position -- by clicking the row, or with
   *  @c Enter on the cursor's row. @p item_node is the row node it is bound to,
   *  or NULL if that row is not currently pooled.
   *
   *  **Do not retain @p item_node.** Rows are pooled and rebound: the node handed
   *  over now will be showing a different dataset position after the next scroll.
   *  Keep @p position, which is the stable thing, and look the value up in
   *  @p user_dataset -- the framework never reads the dataset and cannot hand
   *  back a value it does not own.
   *
   *  Optional. With it NULL, @c Enter is not consumed and bubbles to whatever is
   *  behind the list, so a button behind a recycler still gets its Enter. */
  void (*on_item_clicked)(IrcitUiNode *item_node, size_t position, void *user_dataset);

  /*! Reports that the user finished editing @p position, with @p revised_text as
   *  the new contents. This is the only way an edit reaches the dataset: the
   *  recycler keeps the text in the row's own buffer while the user types and
   *  reads nothing back out of it except to hand it here.
   *
   *  @p revised_text is borrowed and is only valid for the duration of the call.
   *  Copy it if it is to outlive the callback — which it normally is, since the
   *  row it came from is freed by the layout pass that follows.
   *
   *  Called for a **commit** and never for a cancel, so an application that
   *  ignores this callback loses nothing it was not told to lose.
   *
   *  Required for row editing to be possible at all: with it NULL,
   *  @ref IrcitUiRecyclerBeginEdit refuses, because a field whose result nobody
   *  receives is a user typing into a void.
   *
   *  Optional only in the sense that every field here is. A list that is never
   *  edited never calls it. */
  void (*on_item_edited)(size_t position, const char *revised_text, void *user_dataset);

  void *user_dataset;  ///< Passed back to the callbacks untouched.
} IrcitUiRecyclerAdapter;

#endif /* IRCITUI_IRCIT_UI_RECYCLER_ADAPTER_TYPE_H */
