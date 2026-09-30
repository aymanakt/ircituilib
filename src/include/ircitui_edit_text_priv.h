/**
 * @file ircitui_edit_text_priv.h
 * @brief The seam between ui_engine.c and ircitui_edit_text.c.
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
 * Not installed, and never included from outside the library. The public surface
 * of an edit field is ircituilib/ircitui_edit_text.h; this header is the
 * framework's own.
 *
 * THE ENGINE DECIDES WHEN, THIS UNIT DECIDES HOW. IrcitUiDraw, IrcitUiDispatchKey
 * and IrcitUiRealize are the tree's -- they walk, hit-test, focus and route, and
 * a field is only one kind among fifteen they must not special-case further than
 * a case label. So the engine asks "what happened to this node" and this unit
 * answers what that means for the buffer.
 *
 * THAT IS WHY NOTHING HERE HANDS OUT IrcitUiTextEditState. The struct is
 * reachable from both units -- ircitui_text_edit_state_type_priv.h is on the
 * private include path -- but a function returning it would put the caret
 * arithmetic back in the engine, which is the coupling this seam exists to
 * remove. Every entry point below takes a node and a fact about the event, and
 * none takes or returns a member of the state.
 *
 * THE TWO ALIAS CALLS ARE NOT ONE CALL. IrcitUiRealize re-points
 * common.interaction_defaults.user at the field's own buffer unconditionally --
 * a resize must not leave the node reading a window that has just been torn down.
 * The other sites bind it only when nothing has, because overwriting an alias a
 * host installed would take the text out of the field the host is writing to.
 * Those two policies were six copies of one expression apart; they are two
 * functions now, which is the only reason the distinction is visible at all.
 */
#ifndef IRCITUILIB_IRCITUI_EDIT_TEXT_PRIV_H
#define IRCITUILIB_IRCITUI_EDIT_TEXT_PRIV_H

#include "ircituilib/ircit_ui_node_type.h"

/* --- state lifecycle ------------------------------------------------------- */

/* Create the field's extension struct, inheriting the dictionary from the ref
   node when there is one. Called from sAllocWidgetState, the single place any
   node's widget_state is created, so that every creation path gives an edit field
   the same state. A calloc failure leaves the field without state rather than
   failing the node. */
void IrcitUiEditTextStateAttach(IrcitUiNode *node_ptr);

/* --- painting -------------------------------------------------------------- */

/* Paint the field's one row: the prompt, the buffer with its caret and selection,
   and the predictive tail. The caller has already established that the node is
   visible and has a window. */
void IrcitUiEditTextPaint(IrcitUiNode *node_ptr);

/* --- the alias ------------------------------------------------------------- */

/* Point common.interaction_defaults.user at the field's own buffer, whether or
   not it already points there. IrcitUiRealize's rule. */
void IrcitUiEditTextAliasBind(IrcitUiNode *node_ptr);

/* Point it there only if nothing else has. Every other binder's rule. */
void IrcitUiEditTextAliasSync(IrcitUiNode *node_ptr);

/* --- mutation ------------------------------------------------------------- */

/* Append UTF-8 to the field's buffer: control bytes dropped, overflow truncated
   on a character boundary. The paste and the emoji picker both land here. */
void IrcitUiEditTextAppend(IrcitUiNode *node_ptr, const char *utf8_ptr);

/* --- keys ----------------------------------------------------------------- */

/* Drop the field's prediction without touching its text. Self-guarding: a node
   that is not an edit field, or has no hint, is left alone. */
void IrcitUiEditTextDismissHint(IrcitUiNode *node_ptr);

/* Walk the suggestion panel. 1 when the key was consumed, which is the engine's
   signal to stop routing it. */
int IrcitUiEditTextSuggestKey(IrcitUiNode *node_ptr, int key);

/* --- pointer -------------------------------------------------------------- */

/* @p col is a column within the node's content box, as the engine's hit-testing
   computes it: this unit is the one that knows the prompt eats the first two. */

/* Put the caret where the click landed and collapse the selection to it. */
void IrcitUiEditTextCaretToCol(IrcitUiNode *node_ptr, int col);

/* Move the selection's extent to where the drag reached, leaving the caret alone
   -- moving it would move the cell the whole line right of it is drawn against. */
void IrcitUiEditTextSelectionExtendToCol(IrcitUiNode *node_ptr, int col);

/* Select the whole buffer. 1 when the node was an edit field, so a caller that
   also serves other kinds knows which branch it took. */
int IrcitUiEditTextSelectAll(IrcitUiNode *node_ptr);

/* The selection as the field holds it. 1 when the node was an edit field. */
int IrcitUiEditTextSelection(IrcitUiNode *node_ptr, int *start_ptr, int *end_ptr);

/* Delete from the caret to the end of the buffer, leaving the caret where it is.

   The caret stays put rather than moving to the new end: it is already there --
   the buffer's end is what the caret would move to. Snap left off a continuation
   byte first, so the cut lands on a character boundary the way an insert does. */
void IrcitUiEditTextDeleteToEnd(IrcitUiNode *node_ptr);

/* --- the clipboard's own clear -------------------------------------------- */

/* Empty the buffer, park the caret at 0 and collapse the selection to it.

   NOT IrcitUiEditTextClear, and the difference is deliberate rather than an
   oversight: the public clear also drops the prediction and asks for a measure
   pass, and the clipboard's clear has never done either. Unifying them is a
   behaviour change and belongs to a gate of its own. */
void IrcitUiEditTextClipClear(IrcitUiNode *node_ptr);

#endif //IRCITUILIB_IRCITUI_EDIT_TEXT_PRIV_H
