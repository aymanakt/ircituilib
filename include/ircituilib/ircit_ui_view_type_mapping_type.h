/**
 * @file ircit_ui_view_type_mapping_type.h
 * @brief One entry of a heterogeneous recycler's view-type registry: the
 *        application's type tag, and the row template that tag means.
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
 * Public, and for the reason the adapter beside it is public: an application
 * outside this tree that cannot name this type cannot build a heterogeneous
 * recycler at all.
 */
#ifndef IRCITUI_IRCIT_UI_VIEW_TYPE_MAPPING_TYPE_H
#define IRCITUI_IRCIT_UI_VIEW_TYPE_MAPPING_TYPE_H

/*!
 * @struct IrcitUiViewTypeMapping
 * @brief "A row of this type is cloned from that template."
 *
 * @details A homogeneous recycler names one template and every row is a clone of
 *          it. A heterogeneous one names a LIST of these, and the application
 *          tags each dataset position with a type through
 *          @ref IrcitUiRecyclerAdapter::get_item_view_type; the framework maps
 *          the tag back to a template here.
 *
 *          The split between the two is deliberate and is the same one the rest
 *          of the widget follows: **the templates are layout and come from the
 *          declaration; the type of each position is data and comes from the
 *          application.** The framework reads neither the dataset nor the layout
 *          file, it only matches one against the other.
 *
 *          Order is not significant -- a lookup is a linear scan of at most a
 *          handful of entries -- but a caller that declares the list should keep
 *          it small, because a row's template is resolved once per visible slot
 *          per layout pass.
 */
typedef struct IrcitUiViewTypeMapping {
  /*! The tag this entry answers to. Opaque to the framework: it is compared
   *  against what @ref IrcitUiRecyclerAdapter::get_item_view_type returns and is
   *  never interpreted, so any value the engine can compare is a legal one and an
   *  application may use an enum of its own.
   *
   *  **NON-NEGATIVE IN A DECLARATION, AND THAT IS THE DECLARATION'S RULE, NOT THE
   *  ENGINE'S.** C21 refuses a negative `type_id` and both the validator and the
   *  generator enforce it, so a `view_types:` block cannot carry one; but the
   *  engine only ever compares, so a registry installed through
   *  @ref IrcitUiRecyclerSetViewTypes may hold whatever the caller likes and it
   *  will work. Nothing in the engine requires the restriction. It is there
   *  because the layout's `sticky_headers` key names a type_id out of the same
   *  vocabulary and spends a negative value on "off" -- see C22 -- so the two
   *  spell their ids the same way rather than one of them having a corner the
   *  other does not. The two are the same vocabulary from the two sides of the
   *  API: `sticky_headers` names a type out of a declaration and
   *  @ref IrcitUiRecyclerSetStickyHeader names one out of C.
   *
   *  Unique within one registry. Two entries sharing a tag is refused at
   *  generation time (C21) rather than resolved by taking the first, because
   *  which of the two won would be an implementation detail a layout depends
   *  on. */
  int type_id;

  /*! The id of the orphan template every row of this type is cloned from, as
   *  the loader resolved it: a file name in the declaration, its stem at
   *  runtime. **Borrowed and never freed** -- the generator points it at a
   *  literal in ui_generated.c and a C-API caller owns the string it passed,
   *  the same rule @ref IrcitUiRecyclerState::template_id follows.
   *
   *  A tag whose template cannot be resolved is not fatal: the slot is left
   *  unpainted and unbound for that pass rather than the row being built from
   *  some other type's template, because a row of the wrong shape is a wrong
   *  picture and a missing row is not. */
  const char *template_id;
} IrcitUiViewTypeMapping;

#endif /* IRCITUI_IRCIT_UI_VIEW_TYPE_MAPPING_TYPE_H */
