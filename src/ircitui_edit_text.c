/**
 * @file ircitui_edit_text.c
 * @brief The edit field: its own state, its editor, its painter, and its prediction.
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
 */

#include "ui_priv.h"
#include "ui_engine_priv.h"
#include "ircitui_edit_text_priv.h"
#include "ircitui_text_edit_state_type_priv.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <curses.h>

// The edit field's own state, or NULL when the node is not an input field or was given none.
static IrcitUiTextEditState *
sEditState(IrcitUiNode *node_ptr)
{
  if (!node_ptr || node_ptr->common.kind != IRCITUI_EDIT_TEXT) return NULL;
  return node_ptr->common.widget_state;
}

/* ---- prediction: the token, the query, and the tail that gets drawn -------- */

/* Defined with the panel, below; sEditPredict calls it so that the list follows the hint. */
static void sSuggestRefresh(IrcitUiNode *field);
static int  sSuggestKey(IrcitUiNode *field, int key);
static void sSuggestHide(void);

/* The token the caret sits in: the run of non-space bytes ending at the caret.

   The caret is the token's END rather than its middle, because that is where typing happens.
   A prediction completes the word the user is in the middle of; offering one for a word they
   have already typed past would be offering to rewrite text behind them.

   The backward scan is BYTE-wise, and that is safe rather than sloppy: every byte of a
   multi-byte UTF-8 sequence has its top two bits set (0x80-0xBF) and a space is 0x20, so a
   space can never be a continuation byte and the scan can never stop inside a character. The
   caret is held on a character boundary by every writer in this file, so both ends of the
   token are boundaries. */
static void
sEditToken(const IrcitUiTextEditState *state_ptr, int *at_ptr, int *len_ptr)
{
  size_t have = strlen(state_ptr->text_buffer);
  int    end  = (int)state_ptr->cursor_position;
  int    at;
  if (end > (int)have) end = (int)have;
  at = end;
  while (at > 0 && state_ptr->text_buffer[at - 1] != ' ') at--;
  *at_ptr = at;
  *len_ptr = end - at;
}

/* Re-evaluate a field's prediction against its dictionary.

   Called on every buffer mutation, immediately before the invalidate that mutation already
   raises. There are three such sites -- the fast path, the editor, and the append -- and
   they are exactly the three that change the text; refreshing anywhere else would leave the
   hint a frame behind, which reads as a hint for the word before the one on screen.

   THE CANDIDATE IS VALIDATED HERE RATHER THAN TRUSTED. The interface says a dictionary
   returns the whole candidate and the caller refuses one that does not begin with the typed
   token. That check is what stops a host's dictionary -- which this library does not write
   and cannot inspect -- from putting arbitrary text on the glass, and it is why the seam can
   be public without the painter needing to believe anything. */
static void
sEditPredict(IrcitUiNode *node_ptr)
{
  IrcitUiTextEditState *state_ptr = sEditState(node_ptr);
  char                  prefix[MAX_TEXT_INPUT_BUFFER];
  char                  cand[MAX_TEXT_INPUT_BUFFER];
  size_t                cand_len;
  int                   at = 0, len = 0;

  if (!state_ptr) return;

  // Cleared first and unconditionally: every early return below means "no hint", and a surviving one would outlive the text it was a prediction for.
  state_ptr->hint_buffer[0] = 0;
  state_ptr->hint_len       = 0;
  state_ptr->hint_token_at  = 0;

  if (!state_ptr->dictionary || !state_ptr->dictionary->api.best) return;

  sEditToken(state_ptr, &at, &len);
  if (len <= 0 || len >= (int)sizeof prefix) return;

  memcpy(prefix, state_ptr->text_buffer + at, (size_t)len);
  prefix[len] = 0;

  cand[0] = 0;
  if (!state_ptr->dictionary->api.best(state_ptr->dictionary->instance, prefix, cand, sizeof cand)) return;
  // A dictionary that overran its own bound cannot outlive this line.
  cand[sizeof cand - 1] = 0;

  cand_len = strlen(cand);
  if (cand_len <= (size_t)len) return;                     /* adds nothing: not a completion */
  if (strncasecmp(cand, prefix, (size_t)len) != 0) return; /* does not complete it: refused */

  memcpy(state_ptr->hint_buffer, cand, cand_len + 1);
  state_ptr->hint_len      = (int)cand_len;
  state_ptr->hint_token_at = at;

  sSuggestRefresh(node_ptr);
}

/* Is the caret at the END of the word it sits in, rather than inside one?

   `caret` is the CARET'S OWN OFFSET, not the token's start -- passing the start is the
   mistake this function's first version made, and it made every hint dead: for a token
   beginning at 0 in a non-empty buffer, "the byte at the token's start is not a space" is
   true of the FIRST LETTER, so every live prediction was refused and the field predicted
   nothing at all. The probe caught it in nine cases at once.

   sEditToken defines the token as ending AT THE CARET, so a caret after "ap" in "app" is at
   the end of a token -- just not at the end of the word. The distinction is load-bearing
   twice over: the hint is computed for the whole word at the caret, so with the caret
   mid-word the painter drew the tail from the wrong offset (reading "ap┃plep" on the glass)
   and the accept spliced it at the wrong offset, leaving the old remainder behind.

   A caret immediately before a non-space byte is a caret in the MIDDLE of a word. There is
   nothing sensible to complete from there, so both callers decline rather than guess. */
static int
sEditCaretAtTokenEnd(const IrcitUiTextEditState *state_ptr, size_t caret, size_t have)
{
  return caret >= have || state_ptr->text_buffer[caret] == ' ';
}

/* Is the field's stored prediction still APPLICABLE to the text as it now stands?

   One predicate, because three callers have to agree about it and a disagreement is
   visible: the painter draws the tail, the accept splices it, and the public accessor
   reports the candidate. If the accessor said "apple" while the painter drew nothing, the
   API would be reporting a prediction the field does not have -- which is exactly the
   divergence the demo's two readouts exist to expose, and it would have been a real one.

   Four conditions, and each is a way the stored candidate can go stale:
     - there is one at all;
     - the caret is still in the token it was computed for (its start is the recorded one);
     - the caret is at the end of that WORD, not merely of the token it sits in -- see
       sEditCaretAtTokenEnd, which is where the "applep" defect lived;
     - the typed token is still the candidate's own beginning and something is left to add. */
static int
sEditHintLive(IrcitUiTextEditState *state_ptr)
{
  size_t have, caret;
  int    at = 0, len = 0;

  if (!state_ptr || state_ptr->hint_len <= 0) return 0;

  have  = strlen(state_ptr->text_buffer);
  caret = state_ptr->cursor_position;
  if (caret > have) caret = have;

  sEditToken(state_ptr, &at, &len);
  if (len <= 0 || len >= state_ptr->hint_len) return 0;
  if (at != state_ptr->hint_token_at) return 0;
  if (!sEditCaretAtTokenEnd(state_ptr, caret, have)) return 0;
  if (strncasecmp(state_ptr->hint_buffer, state_ptr->text_buffer + at, (size_t)len) != 0) return 0;
  return 1;
}

/* The tail to draw, or NULL.

   NULL when the prediction is not live (sEditHintLive, which owns every reason it can go
   stale) or when the field is not the focused one: a prediction is about what the user is
   typing, and an unfocused field shows its text and nothing more.

   The tail is derived, not stored: it is `hint_buffer` past the bytes the user has typed,
   and the typed count is the caret's distance from where the token was found to start. */
static const char *
sEditHintTail(IrcitUiNode *node_ptr)
{
  IrcitUiTextEditState *state_ptr = sEditState(node_ptr);
  int                   at = 0, len = 0;

  if (!state_ptr || IrcitUiFocus != node_ptr) return NULL;
  if (!sEditHintLive(state_ptr)) return NULL;

  sEditToken(state_ptr, &at, &len);
  return state_ptr->hint_buffer + len;
}

PUBLIC_API int
IrcitUiEditTextSetValue(IrcitUiNode *node_ptr, const char *text_ptr)
{
  IrcitUiTextEditState *state = sEditState(node_ptr);
  size_t                len;

  if (!state) return 0;
  if (!text_ptr) text_ptr = "";
  len = strlen(text_ptr);
  if (len > MAX_TEXT_INPUT_BUFFER - 1) len = MAX_TEXT_INPUT_BUFFER - 1;
  memcpy(state->text_buffer, text_ptr, len);
  state->text_buffer[len] = '\0';
  state->cursor_position = len;
  state->selection_start = state->selection_end = (int)len;
  sEditPredict(node_ptr);
  IrcitUiInvalidate(node_ptr);
  return 1;
}

PUBLIC_API int
IrcitUiEditTextSetDictionary(IrcitUiNode *node_ptr, IrcitUiDictionary *dictionary_ptr)
{
  if (!node_ptr || node_ptr->common.kind != IRCITUI_EDIT_TEXT) return 0;
  // Lazily give the field its state.
  if (!node_ptr->common.widget_state) IrcitUiEditTextStateAttach(node_ptr);
  if (!node_ptr->common.widget_state) return 0;

  ((IrcitUiTextEditState *)node_ptr->common.widget_state)->dictionary = dictionary_ptr;
  sEditPredict(node_ptr);
  IrcitUiInvalidate(node_ptr);
  return 1;
}

PUBLIC_API int
IrcitUiEditTextShowSuggestions(IrcitUiNode *node_ptr, int on)
{
  IrcitUiTextEditState *state = sEditState(node_ptr);
  if (!state) return 0;
  state->suggest_on = on ? 1 : 0;
  if (!on) sSuggestHide();
  else sSuggestRefresh(node_ptr);
  return 1;
}

PUBLIC_API IrcitUiDictionary *
IrcitUiEditTextGetDictionary(IrcitUiNode *node_ptr)
{
  IrcitUiTextEditState *state = sEditState(node_ptr);
  return state ? state->dictionary : NULL;
}

PUBLIC_API const char *
IrcitUiEditTextHint(IrcitUiNode *node_ptr)
{
  IrcitUiTextEditState *state = sEditState(node_ptr);

  if (!sEditHintLive(state)) return NULL;
  return state->hint_buffer;
}

PUBLIC_API int
IrcitUiEditTextAcceptHint(IrcitUiNode *node_ptr)
{
  IrcitUiTextEditState *state = sEditState(node_ptr);
  const char           *tail_ptr;
  char                  repl[MAX_TEXT_INPUT_BUFFER];
  size_t                tail_len, len, at, put_at, put_len;
  int                   tok_at = 0, tok_len = 0;

    if (!sEditHintLive(state)) return 0;

  sEditToken(state, &tok_at, &tok_len);
  len = strlen(state->text_buffer);
  at  = state->cursor_position;
  if (at > len) at = len;

    put_at  = (size_t)at;
  put_len = 0;
  repl[0] = 0;
  if (state->dictionary->api.replace &&
      state->dictionary->api.replace(state->dictionary->instance, state->hint_buffer, repl, sizeof repl)) {
    put_at  = (size_t)tok_at;
    put_len = (size_t)tok_len;
    tail_ptr = repl;
    tail_len = strlen(repl);
  }
  else {
    tail_ptr = state->hint_buffer + tok_len;
    tail_len = (size_t)(state->hint_len - tok_len);
  }

    if (len - put_len + tail_len >= MAX_TEXT_INPUT_BUFFER) return 0;

  memmove(state->text_buffer + put_at + tail_len, state->text_buffer + put_at + put_len, len - (put_at + put_len) + 1);
  memcpy(state->text_buffer + put_at, tail_ptr, tail_len);
  state->cursor_position = put_at + tail_len;

  state->selection_start = state->selection_end = (int)state->cursor_position;

    sEditPredict(node_ptr);
  IrcitUiInvalidate(node_ptr);
  return 1;
}

/* ---- the suggestion panel --------------------------------------------------- */

/* A list of the candidates, under the field, that the keyboard can walk.
 *
 * IT IS A CONTAINER OF BUTTONS, like the engine's own context menu, and that is the whole of
 * why it inherits the frame profile, the shadow, the palette profiles and the selection
 * highlight without a line of drawing code. What it does NOT copy from the context menu is
 * taking focus: the field has to keep it, or the loop's edit fast path stops firing and the
 * next character goes nowhere. The highlight therefore comes from the panel's own sel0, which
 * sMenuRowIsCursor reads without consulting focus at all.
 *
 * THE ROWS ARE A FIXED POOL. Rebuilding a node per keystroke would put malloc in the typing
 * path; instead the rows are created once per candidate COUNT and their text is repointed
 * afterwards, so a keystroke that does not change how many candidates there are costs no
 * allocation and no layout pass.
 *
 * THE FIELD IS RESOLVED BY ID, never held: a panel that kept a node pointer across a deflate
 * would be holding a freed node, which is the hazard sCtxTarget's clearing exists for. */
#define SUGGEST_ROWS 6
#define SUGGEST_COLS 96
#define SUGGEST_W   40

typedef struct
{
  char cand[SUGGEST_ROWS][SUGGEST_COLS];
  char field_id[64];
  int  n;      ///< How many rows are currently built.
  int  shown;  ///< How many of them are visible.
} sSuggestState;

static const char sSuggestId[] = "suggest_panel";

static IrcitUiNode *
sSuggestPanel(void)
{
  if (!IrcitUiScreen.root || !IrcitUiScreen.root->common.win) return NULL;
  return IrcitUiFind(IrcitUiScreen.root, sSuggestId);
}

/* Drop the panel. Safe to call when there is none. */
static void
sSuggestHide(void)
{
  IrcitUiNode *p = sSuggestPanel();
  if (!p) return;
  IrcitUiPopStacked(p);
}

/* The candidate the panel's cursor is on, or NULL. */
static const char *
sSuggestSelected(void)
{
  IrcitUiNode *p = sSuggestPanel();
  sSuggestState *st;
  if (!p) return NULL;
  st = p->common.widget_state;
  if (!st || p->container.sel0 < 0 || p->container.sel0 >= st->n) return NULL;
  return st->cand[p->container.sel0];
}

/* Build the panel for @p field with @p n rows, anchored under it.
 *
 * The anchor flips above the field when there is no room below, which is the one piece of
 * geometry the design document's section 11 gets right and the reason it is worth having a
 * popup rather than an inline list at the bottom of a screen. */
static IrcitUiNode *
sSuggestBuild(IrcitUiNode *field, int n)
{
  IrcitUiNode   *panel;
  sSuggestState *st;
  IrcitUiWidgetDef def;
  int            x, y, h, i;

  if (!field || n <= 0) return NULL;
  h = n + 2; /* the frame */

  memset(&def, 0, sizeof def);
  def.id          = sSuggestId;
  def.kind        = IRCITUI_LINEAR;
  def.width_mode  = IRCITUI_SIZE_EXACT;
  def.width       = SUGGEST_W;
  def.height_mode = IRCITUI_SIZE_EXACT;
  def.height      = h;
  def.flags       = IRCITUI_F_VISIBLE | IRCITUI_F_ENABLED | IRCITUI_F_BORDER | IRCITUI_F_STACKED
                    | IRCITUI_F_CHILDREN_SELECTABLE;
  def.fg          = 7;
  def.bg          = 0;
  def.bdr         = 2;

  panel = IrcitUiCreateFloatingWindow(&def);
  if (!panel) return NULL;

  st = calloc(1, sizeof *st);
  if (!st) {
    IrcitUiPopStacked(panel);
    return NULL;
  }
  panel->common.widget_state = st;
  st->n = n;
  st->shown = 0;
  if (field->common.id) {
    strncpy(st->field_id, field->common.id, sizeof st->field_id - 1);
    st->field_id[sizeof st->field_id - 1] = 0;
  }

  for (i = 0; i < n; i++) {
    IrcitUiWidgetDef row;
    memset(&row, 0, sizeof row);
    row.id          = NULL;
    row.kind        = IRCITUI_BUTTON;
    row.text        = st->cand[i]; /* repointed below; a stable address either way */
    row.width_mode  = IRCITUI_SIZE_MATCH;
    row.height_mode = IRCITUI_SIZE_EXACT;
    row.height      = 1;
    row.flags       = IRCITUI_F_VISIBLE | IRCITUI_F_ENABLED;
    row.fg          = 7;
    row.bg          = 0;
    row.bdr         = 2;
    if (!IrcitUiAddChild(panel, &row)) break;
  }

  x = field->common.x + 2;
  y = field->common.y + field->common.h;
  if (y + h > LINES) {
    y = field->common.y - h;
    if (y < 0) y = 0;
  }
  if (x + SUGGEST_W > COLS) x = COLS - SUGGEST_W;
  if (x < 0) x = 0;

  IrcitUiLayout(panel, x, y, SUGGEST_W, h);
  IrcitUiRealize(panel);
  return panel;
}

/* Follow the field's prediction: show, update or drop the panel. */
static void
sSuggestRefresh(IrcitUiNode *field)
{
  IrcitUiTextEditState *fs;
  IrcitUiNode          *panel;
  sSuggestState        *st;
  char                  buf[SUGGEST_COLS];
  int                   i, n = 0;

  if (!field || field->common.kind != IRCITUI_EDIT_TEXT) return;
  fs = sEditState(field);
  if (!fs || !fs->suggest_on || !fs->dictionary || !fs->dictionary->api.next) {
    sSuggestHide();
    return;
  }
  // ONLY THE FOCUSED FIELD. A panel for a field the user is not in would be a popup following them around the screen.
  if (IrcitUiFocus != field) {
    sSuggestHide();
    return;
  }
  // And only while there is a token to enumerate.
  {
    int at = 0, len = 0;
    sEditToken(fs, &at, &len);
    if (len <= 0) {
      sSuggestHide();
      return;
    }
    for (i = 0; i < SUGGEST_ROWS; i++) {
      buf[0] = 0;
      if (!fs->dictionary->api.next(fs->dictionary->instance, fs->text_buffer + at, i, buf, sizeof buf)) break;
      // The same refusals the inline hint applies, applied here so the list and the hint cannot disagree about what is a completion.
      if ((int)strlen(buf) <= len) continue;
      if (strncasecmp(buf, fs->text_buffer + at, (size_t)len) != 0) continue;
      n++;
    }
  }
  if (n <= 1) {
    // One candidate is what the inline hint already shows; a one-row list is noise.
    sSuggestHide();
    return;
  }

  panel = sSuggestPanel();
  st    = panel ? panel->common.widget_state : NULL;
  if (!panel || !st || st->n != n) {
    sSuggestHide();
    panel = sSuggestBuild(field, n);
    if (!panel) return;
    st = panel->common.widget_state;
    if (!st) return;
  }

  // Fill the rows. The same walk again, this time keeping the strings
  {
    int at = 0, len = 0, k = 0;
    sEditToken(fs, &at, &len);
    for (i = 0; i < SUGGEST_ROWS && k < st->n; i++) {
      buf[0] = 0;
      if (!fs->dictionary->api.next(fs->dictionary->instance, fs->text_buffer + at, i, buf, sizeof buf)) break;
      if ((int)strlen(buf) <= len) continue;
      if (strncasecmp(buf, fs->text_buffer + at, (size_t)len) != 0) continue;
      snprintf(st->cand[k], sizeof st->cand[k], "%s", buf);
      if (k < panel->container.n_children) panel->container.children[k]->common.text = st->cand[k];
      k++;
    }
    st->shown = k;
  }
  if (panel->container.sel0 < 0 || panel->container.sel0 >= st->n) panel->container.sel0 = 0;
  IrcitUiInvalidate(panel);
}

/* Up/Down walk the panel; the selected candidate becomes the field's hint, so the inline tail
 * and the highlighted row are two views of one piece of state rather than two that can drift. */
static int
sSuggestKey(IrcitUiNode *field, int key)
{
  IrcitUiNode          *panel = sSuggestPanel();
  IrcitUiTextEditState *fs    = sEditState(field);
  sSuggestState        *st;
  const char           *pick;

  if (!panel || !fs) return 0;
  st = panel->common.widget_state;
  if (!st || st->n <= 0) return 0;
  if (!st->field_id[0] || !field->common.id || strcmp(st->field_id, field->common.id) != 0) return 0;

  if (key == KEY_UP) {
    if (panel->container.sel0 > 0) panel->container.sel0--;
    else return 1;
  }
  else if (key == KEY_DOWN) {
    if (panel->container.sel0 < st->n - 1) panel->container.sel0++;
    else return 1;
  }
  else return 0;

  pick = sSuggestSelected();
  if (pick && pick[0]) {
    snprintf(fs->hint_buffer, sizeof fs->hint_buffer, "%s", pick);
    fs->hint_len = (int)strlen(fs->hint_buffer);
  }
  IrcitUiInvalidate(panel);
  IrcitUiInvalidate(field);
  return 1;
}

PUBLIC_API int
IrcitUiEditTextSuggestionCount(IrcitUiNode *node_ptr)
{
  IrcitUiNode    *panel = sSuggestPanel();
  sSuggestState  *st;
  IrcitUiTextEditState *fs = sEditState(node_ptr);

  if (!panel || !fs || !fs->suggest_on) return 0;
  st = panel->common.widget_state;
  if (!st) return 0;
  // The panel belongs to one field; a count for a different one would be another field's list.
  if (!node_ptr->common.id || strcmp(st->field_id, node_ptr->common.id) != 0) return 0;
  return st->n;
}

PUBLIC_API const char *
IrcitUiEditTextSuggestion(IrcitUiNode *node_ptr, int ordinal)
{
  IrcitUiNode    *panel = sSuggestPanel();
  sSuggestState  *st;
  IrcitUiTextEditState *fs = sEditState(node_ptr);

  if (!panel || !fs || !fs->suggest_on || ordinal < 0) return NULL;
  st = panel->common.widget_state;
  if (!st || ordinal >= st->n) return NULL;
  if (!node_ptr->common.id || strcmp(st->field_id, node_ptr->common.id) != 0) return NULL;
  return st->cand[ordinal];
}

PUBLIC_API int
IrcitUiEditTextSuggestionSelected(IrcitUiNode *node_ptr)
{
  IrcitUiNode *panel = sSuggestPanel();
  IrcitUiTextEditState *fs = sEditState(node_ptr);
  if (!panel || !fs || !fs->suggest_on) return -1;
  if (!node_ptr->common.id) return -1;
  {
    sSuggestState *st = panel->common.widget_state;
    if (!st || strcmp(st->field_id, node_ptr->common.id) != 0) return -1;
  }
  return panel->container.sel0;
}

/* Drop a field's prediction without touching its text.
 *
 * Private, because the only caller is the engine's own Esc arm: a host that wants the hint
 * gone detaches the dictionary or types, and a public "hide this hint" would be a second way
 * to express a state that the next keystroke re-derives anyway. It exists so that Esc means
 * "not that" while the caret stays where the user put it. */
static void
sEditDismissHint(IrcitUiNode *node_ptr)
{
  IrcitUiTextEditState *state = sEditState(node_ptr);
  if (!state) return;
  if (state->hint_len <= 0) return;
  state->hint_buffer[0] = 0;
  state->hint_len       = 0;
  state->hint_token_at  = 0;
  IrcitUiInvalidate(node_ptr);
}

/* One caret blinks: the one in the focused field. Every other edit field still
   shows a caret, steadily - "blink only while focused" leaves two fields able
   to show one at the same time, and the blinking one is the insertion point.
   The phase lives here rather than in the demo so the painter and the tick
   cannot disagree about it; only the cadence belongs to the caller. */
static int sBlinkLit = 1;

PUBLIC_API void
IrcitUiBlinkReset(void)
{
  sBlinkLit = 1;
}

PUBLIC_API int
IrcitUiCaretVisible(const IrcitUiNode *node_ptr)
{
  if (!node_ptr) return 0;
  return node_ptr == IrcitUiFocus ? sBlinkLit : 1;
}

PUBLIC_API int
IrcitUiBlinkAdvance(void)
{
  IrcitUiNode *f = IrcitUiFocus;
  // Return 0 when there is nothing to animate, so an idle tick with a button or a menu focused produces no output at all rather than a repaint.
  if (!f || f->common.kind != IRCITUI_EDIT_TEXT) return 0;
  if (!(f->common.flags & IRCITUI_F_VISIBLE) || !f->common.win) return 0;
  sBlinkLit = !sBlinkLit;
  // The caret is a painted cell like any other, so the field it sits in is what has to be repainted
  IrcitUiInvalidate(f);
  return 1;
}

/* A pointer column over an edit field, as the byte index it means.

   The caret owns a cell of its own, so the line on screen is the text before
   the caret, then that cell, then the rest: everything right of the caret is
   drawn one column further right than its own column. Feeding the raw pointer
   column to IrcitUiIndexForCol would therefore land one character too far right for
   every click past the caret - which is every click after the first one, since
   the first click is what puts the caret in the middle. @p caret is where the
   caret currently is, so the column of the cell it owns is known and the shift
   can be undone. */
static size_t
sEditIndexForCol(const char *str_ptr, int col, int caret)
{
  size_t at        = caret < 0 ? 0 : (size_t)caret;
  int    caret_col = IrcitUiColForIndex(str_ptr, at);
  if (col > caret_col) col--;
  if (col < 0) col = 0;
  return IrcitUiIndexForCol(str_ptr, col);
}

/* Paint one line of an edit field.

   Two things separate this from sPaintSelText, which stays untouched for
   TEXT_VIEW: characters are measured with wcwidth instead of being charged one
   column per byte, and the caret owns a cell of its own rather than replacing
   the character under it.

   The caret cell is reserved whether or not the caret is lit, so blinking and
   focus changes toggle only the glyph and the line never jitters. A character
   is drawn only when all of its cells fit, so a wide glyph is never split at
   the right edge.

   Selection still renders - an edit field can be drag-selected - over the byte
   range [sel0, sel1), tested on each character's lead byte exactly as
   sPaintSelText tests it.

   Fail-safe: the caret is drawn only where `caret == idx` lands on a character
   boundary the walk actually reached. An index inside a character, or one past
   the last character that fits, draws no caret at all - a missing caret beats a
   line drawn at the wrong offset.

   `hint_ptr` is the completion's tail - the part of the candidate the user has
   not typed - and is drawn after the caret's own cell, dimmed. It is the caller
   that decides whether there is one at all, because "is this field focused" is
   the caller's question and not the painter's: this function takes `lit`, which
   for the focused node is the BLINK PHASE and not a focus test
   (IrcitUiCaretVisible), so gating a hint on it would make the hint blink. A
   caller with no hint passes NULL.

   The tail is drawn by the same two rules as the buffer above it - wcwidth for
   the width, and a character only when all of its cells fit - so the two runs
   clip at the right edge identically rather than by two rules that can drift.
   A tail too long for the remaining columns is CLIPPED, not refused: the
   gesture that accepts it completes the word, and a word the user can see the
   front of is a word they asked for. */
static void
sPaintEditLine(WINDOW *win_ptr, int ox, int oy, int maxw, const char *str_ptr, int caret, int lit, int sel0, int sel1,
               const char *hint_ptr)
{
  int col = 0,    idx = 0, placed = 0;
  int a   = sel0, b   = sel1;
  if (!str_ptr) str_ptr = "";
  if (a > b) {
    int t = a;
    a     = b;
    b     = t;
  }
  while (str_ptr[idx]) {
    size_t k;
    int    ww;
    if (caret == idx) {
      if (lit && col < maxw)
        mvwaddnwstr(win_ptr, oy, ox + col, L"┃", 1);
      col++;
      placed = 1;
    }
    IrcitUiUtf8Step(str_ptr, idx, &k, &ww);
    if (ww <= 0) { /* zero-width: charges no cell */
      idx += (int)k;
      continue;
    }
    if (col + ww > maxw) break;
    {
      char tmp[PRIV_CONFIG_DEFAULT_UTF8_MAX_BYTES];
      memcpy(tmp, str_ptr + idx, k);
      tmp[k] = 0;
      if (idx >= a && idx < b)
        wattron(win_ptr, A_REVERSE);
      else
        wattroff(win_ptr, A_REVERSE);
      IrcitUiAddUtf8(win_ptr, oy, ox + col, tmp, 1);
      wattroff(win_ptr, A_REVERSE);
    }
    col += ww;
    idx += (int)k;
  }
  /* The fail-safe caret for a caret at the very end -- the walk above never reaches an index
     past the last character, so this is the only place that draws one there.

     THE CELL IS RESERVED WHETHER OR NOT THE CARET IS LIT, which is the same rule the walk
     above follows and for the same reason: the line must not move when the caret blinks. It
     matters more here than it used to, because the predictive tail is drawn after this cell
     -- the first version of this code incremented the column inside the `lit` test, which
     made the hint slide one column left on every blink-off half-cycle. */
  if (!placed && caret == idx) {
    if (lit && col < maxw) mvwaddnwstr(win_ptr, oy, ox + col, L"┃", 1);
    col++;
  }
  // The predictive tail. Last, so the caret's cell is already behind it, and drawn unfocused-never by the caller rather than blinked by `lit`.
  if (hint_ptr && hint_ptr[0]) {
    int hi = 0;
    wattron(win_ptr, IrcitUiPaletteStyle(IRCITUI_SLOT_TEXT_MUTED) | A_DIM);
    while (hint_ptr[hi]) {
      size_t k;
      int    ww;
      IrcitUiUtf8Step(hint_ptr, hi, &k, &ww);
      if (ww <= 0) {
        hi += (int)k;
        continue;
      }
      if (col + ww > maxw) break;
      {
        char tmp[PRIV_CONFIG_DEFAULT_UTF8_MAX_BYTES];
        memcpy(tmp, hint_ptr + hi, k);
        tmp[k] = 0;
        IrcitUiAddUtf8(win_ptr, oy, ox + col, tmp, 1);
      }
      col += ww;
      hi += (int)k;
    }
    // Off before returning, so the attribute cannot leak into the fail-safe caret above on a later pass or into anything drawn after this call.
    wattroff(win_ptr, IrcitUiPaletteStyle(IRCITUI_SLOT_TEXT_MUTED) | A_DIM);
  }
}

/* Length of the UTF-8 sequence introduced by a lead byte. Anything that is not a valid lead byte - including a stray continuation - counts as 1, so a
   malformed paste advances instead of stalling. */
static size_t
sUtf8Len(unsigned char c)
{
  if ((c & 0x80) == 0) return 1;
  if ((c & 0xE0) == 0xC0) return 2;
  if ((c & 0xF0) == 0xE0) return 3;
  if ((c & 0xF8) == 0xF0) return 4;
  return 1;
}

/* Append @p utf8 onto @p n's own text buffer. Control bytes are dropped rather than stored: the field is single-line and
   paints one row, so a retained '\n' would be invisible yet still reach the Enter echo. Overflow truncates to fit - the paste visibly lands - cut back to
   a whole character so the buffer never ends mid-codepoint. */
static void
sEditAppend(IrcitUiNode *node_ptr, const char *utf8_ptr)
{
  const unsigned char * p = (const unsigned char*)utf8_ptr;
  const unsigned char * end;
  size_t                out, have;
  IrcitUiTextEditState *st = sEditState(node_ptr);

  if (!st || !utf8_ptr || !utf8_ptr[0]) return;
  end  = p + strlen(utf8_ptr);
  have = strlen(st->text_buffer);
  if (have >= MAX_TEXT_INPUT_BUFFER - 1) return;
  out = have;
  while (p < end) {
    size_t k;
    if (*p < 0x20 || *p == 0x7f) { /* single-line field: drop it */
      p++;
      continue;
    }
    k = sUtf8Len(*p);
    if (p + k > end) /* source ends mid-codepoint */
      break;
    if (out + k > MAX_TEXT_INPUT_BUFFER - 1) /* no room for this whole char */
      break;
    memcpy(st->text_buffer + out, p, k);
    out += k;
    p   += k;
  }
  st->text_buffer[out] = 0;
  // The one mutating exit, so both append callers -- a paste and the emoji picker's Enter -- are covered here rather than twice over. Text was added, which for a fixed-width field changes nothing about its box.
  sEditPredict(node_ptr);
  IrcitUiInvalidate(node_ptr);
}

/* The editor's body, so that the prediction can be refreshed exactly once after it.

   Split rather than called at each of the body's many returns, for the reason the body's own
   invalidate is raised once at the top: every branch below changes the buffer or the caret,
   and a refresh written at each return is a refresh that will one day be missing from one of
   them. The body is unchanged. */
static void
sEditInputApply(IrcitUiNode *node_ptr, int key)
{
  size_t                len, at;
  IrcitUiTextEditState *state = sEditState(node_ptr);

  if (!state) return;
  if (!node_ptr->common.interaction_defaults.user) node_ptr->common.interaction_defaults.user = state->text_buffer;
  len = strlen(state->text_buffer);

  at  = state->cursor_position;
  if (at > len) at = len;
  // Snap left onto a character boundary
  while (at > 0 && ((unsigned char)state->text_buffer[at] & 0xC0) == 0x80) at--;
  state->cursor_position = at;
  // One invalidate covers every exit below
  IrcitUiInvalidate(node_ptr);
  if (key == KEY_RESIZE || key == ERR || key == KEY_MOUSE) return;
  if (key == KEY_LEFT) {
    state->cursor_position = IrcitUiUtf8Prev(state->text_buffer, at);
    return;
  }
  if (key == KEY_RIGHT) {
    state->cursor_position = IrcitUiUtf8Next(state->text_buffer, at);
    return;
  }
  if (key == KEY_BACKSPACE || key == IRCITUI_KEY_DEL || key == IRCITUI_KEY_BS) {
    // delete the whole character left of the caret, not the last byte
    size_t prev = IrcitUiUtf8Prev(state->text_buffer, at);
    if (prev < at) {
      memmove(state->text_buffer + prev, state->text_buffer + at, len - at + 1);
      state->cursor_position = prev;
    }
    return;
  }
  if (key == '\n' || key == '\r' || key == KEY_ENTER) {
    IrcitUiNode *st = IrcitUiFind(IrcitUiScreen.root, "status");
    if (st) {
      static char echo[PRIV_CONFIG_DEFAULT_EDIT_ECHO_SZ];
      snprintf(echo, sizeof echo, "sent: %.240s", state->text_buffer);
      st->common.interaction_defaults.user = echo;
      // A second node, on the other side of the tree, whose painted bytes just changed.
      IrcitUiInvalidate(st);
    }
    state->text_buffer[0]  = 0;
    state->cursor_position = 0;
    return;
  }
  if (key >= IRCITUI_KEY_SPACE && key < IRCITUI_KEY_DEL && len < MAX_TEXT_INPUT_BUFFER - 1) {
    // open a one-byte hole at the caret and drop the character in
    memmove(state->text_buffer + at + 1, state->text_buffer + at, len - at + 1);
    state->text_buffer[at] = (char)key;
    state->cursor_position = at + 1;
  }
}

PUBLIC_API void
IrcitUiEditInput(IrcitUiNode *node_ptr, int key)
{
  sEditInputApply(node_ptr, key);
  sEditPredict(node_ptr);
}

#if 1
/*
[ User types character 'A' on keyboard ]
               │
               ▼ (Main Looper unblocks via poll)
 ┌─────────────────────────────────────────┐
 │ 1. Fast-Path Verification Match         │ ──► Checks kind == TEXT_EDIT
 └─────────────────────────────────────────┘
               │
               ▼ (Bypasses global dispatcher loop, queues, and mutexes)
 ┌─────────────────────────────────────────┐
 │ 2. IrcitUiExecuteFastPathTyping()      │ ──► Updates text_buffer array in place
 └─────────────────────────────────────────┘
               │
               │  No paint in this lane. The key changed the buffer and invalidated the
               │  node; the field's cells are the frame pass's, at STAGE 4 of this same
               │  pass and before the one doupdate, so a paint here reached the glass no
               │  sooner and staged the node twice.
               ▼ (Atomic kernel update)
 ┌─────────────────────────────────────────┐
 │ 3. doupdate()                           │ ──► Terminal changes updated in under 1ms
 └─────────────────────────────────────────┘
*/
// Both structs are real now: IrcitUiTextEditState in ircitui_text_edit_state_type_priv.h, IrcitUiNode in ui_priv.h.
// A row painter lived here and drew from window column 1 with a bordered box's width, one column left of the field's own.

/**
 * @brief High-speed synchronous text buffer mutation (Fast-Path).
 */
int IrcitUiExecuteFastPathTyping(IrcitUiNode *node, int ch)
{
  if (!node || node->common.kind != IRCITUI_EDIT_TEXT || !node->common.widget_state) return 0;

  IrcitUiTextEditState *state         = node->common.widget_state;
  size_t                active_length = strlen(state->text_buffer);
  size_t                at;

  // Enforce rigid buffer limit safety checks to block buffer overflow exploits
  if (active_length >= (MAX_TEXT_INPUT_BUFFER - 1)) return 0;

  // * STAGE 1: THE INTERRUPT / VETO PASS: Give the host application complete veto authority over this fast-path key.
  IrcitUiEvent synthetic_ev = {0};
  synthetic_ev.type = IRCITUI_EVENT_INPUT_KEY;
  synthetic_ev.target = node;
  synthetic_ev.key_code = ch;

  // Check application listeners first
  if (node->interactions_user.lifecycle_listeners.on_key_down) {
    if (node->interactions_user.lifecycle_listeners.on_key_down(IrcitUiDispatcherInstance(), &synthetic_ev, node->interactions_user.user)) {
      // Consumed, and nothing changed: the return says whether the row needs
      // repainting, not whether the key was swallowed -- the loop consumes either way.
      return 0; // Swallowed by Application Veto
    }
  }
  // Check framework default listeners second
  else if (node->common.interaction_defaults.lifecycle_listeners.on_key_down) {
    if (node->common.interaction_defaults.lifecycle_listeners.on_key_down(IrcitUiDispatcherInstance(), &synthetic_ev, node->common.interaction_defaults.user)) {
      return 1; // Swallowed by Framework Default Veto
    }
  }

  // STAGE 2: INTERNAL MUTATION PASS * Safe from vetos. Update character buffers and shift internal tracker pointers.
  // Repair the caret before it is used as an index: past the end it writes over the terminator.
  at = state->cursor_position;
  if (at > active_length) at = active_length;
  // And snap left off a continuation byte, so an insert cannot split a character.
  while (at > 0 && ((unsigned char)state->text_buffer[at] & 0xC0) == 0x80) at--;

  /*
   * THE ARRAY SHIFTING SOLVER
   * If the cursor sits in the middle of the string, shift all characters
   * to the right of the cursor index forward by exactly one byte.
   */
  if (at < active_length) {
    memmove(&state->text_buffer[at + 1], &state->text_buffer[at], active_length - at);
  }

  // Inject the fresh ASCII byte directly into the vacated memory slot
  state->text_buffer[at] = (char)ch;

  // Advance tracking registers step-forward
  state->cursor_position = at + 1;

  // Guarantee absolute null-termination safety
  state->text_buffer[active_length + 1] = '\0';

  /* The text moved, so the prediction is about a prefix that no longer exists. Refreshed
     here, before the invalidate below, so the frame this keystroke asks for draws the hint
     that belongs to the buffer it is about to show. */
  sEditPredict(node);

    //  STAGE 3: NOTIFICATION SYNC PASS * The buffer mutation is finalized. Broadcast changes to listeners immediately.
  if (node->interactions_user.post_interceptor_listeners.on_text_changed) {
    node->interactions_user.post_interceptor_listeners.on_text_changed(IrcitUiDispatcherInstance(), node, ch, state->text_buffer, node->interactions_user.user );
  }
  else if (node->common.interaction_defaults.post_interceptor_listeners.on_text_changed) {
    node->common.interaction_defaults.post_interceptor_listeners.on_text_changed(IrcitUiDispatcherInstance(), node, ch, state->text_buffer, node->common.interaction_defaults.user );
  }

  // Paint-only: the buffer changed, the box did not, so nothing needs measuring.
  IrcitUiInvalidate(node);

  return 1; // Event fully handled via pipeline path
}

#endif

PUBLIC_API void
IrcitUiInputAppend(const char *utf8_ptr)
{
  IrcitUiNode *ed;

  if (!utf8_ptr || !utf8_ptr[0] || !IrcitUiScreen.root) return;
  ed = IrcitUiFocus && IrcitUiFocus->common.kind == IRCITUI_EDIT_TEXT ? IrcitUiFocus : NULL;
  if (!ed) ed = IrcitUiFind(IrcitUiScreen.root, "cmdline");
  if (!ed) return;
  {
    IrcitUiTextEditState *state = sEditState(ed);
    if (state && !ed->common.interaction_defaults.user) ed->common.interaction_defaults.user = state->text_buffer;
  }

  sEditAppend(ed, utf8_ptr);
}

/* ---- the seam the engine calls through ------------------------------------- */

/* Everything below is declared in ircitui_edit_text_priv.h. Each one exists
   because an engine path has to happen to a field without knowing how the field
   is built: the bodies are the engine's own lines, moved here so that the caret
   arithmetic, the alias policy and the prompt's width all live on one side. */

void
IrcitUiEditTextStateAttach(IrcitUiNode *node_ptr)
{
  IrcitUiTextEditState *field;
  if (!node_ptr || node_ptr->common.kind != IRCITUI_EDIT_TEXT) return;

  field                         = calloc(1, sizeof(IrcitUiTextEditState));
  node_ptr->common.widget_state = field;
  if (field && node_ptr->common.ref && node_ptr->common.ref->common.kind == IRCITUI_EDIT_TEXT) {
    const IrcitUiTextEditState *src = node_ptr->common.ref->common.widget_state;
    if (src) field->dictionary = src->dictionary;
  }
}

void
IrcitUiEditTextPaint(IrcitUiNode *node_ptr)
{
  IrcitUiTextEditState *state;
  const char           *text;

  if (!node_ptr || !node_ptr->common.win) return;
  state = sEditState(node_ptr);
  text  = state && state->text_buffer[0] ?
            state->text_buffer :
            (node_ptr->common.text ? node_ptr->common.text : "");

  // The prompt is the field's chrome, and the 2 below is its width: the column a click is measured from is the same number, which is why both live here.
  mvwaddstr(node_ptr->common.win, 0, 0, "> ");
  sPaintEditLine(node_ptr->common.win, 2, 0, node_ptr->common.w - 2, text, state ? (int)state->cursor_position : 0,
                 IrcitUiCaretVisible(node_ptr), state ? state->selection_start : 0,
                 state ? state->selection_end : 0, sEditHintTail(node_ptr));
}

void
IrcitUiEditTextAliasBind(IrcitUiNode *node_ptr)
{
  IrcitUiTextEditState *state = sEditState(node_ptr);
  if (state) node_ptr->common.interaction_defaults.user = state->text_buffer;
}

void
IrcitUiEditTextAliasSync(IrcitUiNode *node_ptr)
{
  IrcitUiTextEditState *state = sEditState(node_ptr);
  if (state && !node_ptr->common.interaction_defaults.user) node_ptr->common.interaction_defaults.user = state->text_buffer;
}

void
IrcitUiEditTextAppend(IrcitUiNode *node_ptr, const char *utf8_ptr)
{
  sEditAppend(node_ptr, utf8_ptr);
}

void
IrcitUiEditTextDismissHint(IrcitUiNode *node_ptr)
{
  sEditDismissHint(node_ptr);
}

int
IrcitUiEditTextSuggestKey(IrcitUiNode *node_ptr, int key)
{
  return sSuggestKey(node_ptr, key);
}

void
IrcitUiEditTextCaretToCol(IrcitUiNode *node_ptr, int col)
{
  IrcitUiTextEditState *state = sEditState(node_ptr);
  if (!state) return;
  {
    int idx                = (int)sEditIndexForCol(state->text_buffer, col, (int)state->cursor_position);
    state->cursor_position = (size_t)idx;
    state->selection_start = state->selection_end = idx;
  }
}

void
IrcitUiEditTextSelectionExtendToCol(IrcitUiNode *node_ptr, int col)
{
  IrcitUiTextEditState *state = sEditState(node_ptr);
  /* The caret stays where the press put it: moving it would move the cell it
     owns, and the whole line right of the caret is drawn shifted by that cell -
     the column the pointer is measured against would shift mid-drag. */
  if (state) state->selection_end = (int)sEditIndexForCol(state->text_buffer, col, (int)state->cursor_position);
}

/* What is on the glass, resolved the way the painter resolves it: the alias a host
   or IrcitUiRealize bound, else the node's own declared text. Select-all has to
   measure the string it is selecting, and this is that string. */
static const char *
sEditVisibleText(const IrcitUiNode *node_ptr)
{
  if (node_ptr->common.interaction_defaults.user) return (char*)node_ptr->common.interaction_defaults.user;
  return node_ptr->common.text ? node_ptr->common.text : "";
}

int
IrcitUiEditTextSelectAll(IrcitUiNode *node_ptr)
{
  IrcitUiTextEditState *state = sEditState(node_ptr);
  if (!state) return 0;
  state->selection_start = 0;
  state->selection_end   = (int)strlen(sEditVisibleText(node_ptr));
  return 1;
}

int
IrcitUiEditTextSelection(IrcitUiNode *node_ptr, int *start_ptr, int *end_ptr)
{
  IrcitUiTextEditState *state = sEditState(node_ptr);
  if (!state) return 0;
  if (start_ptr) *start_ptr = state->selection_start;
  if (end_ptr) *end_ptr = state->selection_end;
  return 1;
}

void
IrcitUiEditTextClipClear(IrcitUiNode *node_ptr)
{
  IrcitUiTextEditState *state = sEditState(node_ptr);
  if (!state) return;
  state->text_buffer[0]                      = 0;
  state->cursor_position                     = 0; /* an empty buffer has exactly one position */
  state->selection_start                     = 0;
  state->selection_end                       = 0;
  node_ptr->common.interaction_defaults.user = state->text_buffer;
}

/* ---- the public entries this unit adds ------------------------------------- */

/**
 * @brief Retrieves a borrowed pointer to the active input text buffer.
 */
PUBLIC_API const char *
IrcitUiEditTextGetValue(const IrcitUiNode *field_node)
{
  const IrcitUiTextEditState *state;
  if (!field_node || field_node->common.kind != IRCITUI_EDIT_TEXT) return NULL;
  state = field_node->common.widget_state;
  /* The field's value is its buffer, and only its buffer. The painter falls back
     to the node's declared text when there is no state -- that is a placeholder
     for an empty field, and reporting it here would hand a host text the user
     never typed. */
  return state ? state->text_buffer : NULL;
}

/**
 * @brief Resets the text edit field back to an empty initial state.
 */
PUBLIC_API void
IrcitUiEditTextClear(IrcitUiNode *field_node)
{
  IrcitUiTextEditState *state = sEditState(field_node);
  if (!state) return;

  memset(state->text_buffer, 0, sizeof(state->text_buffer));
  state->cursor_position = 0;
  /* An empty buffer has exactly one position, and it is 0 -- the same convention
     IrcitUiClipClear and the Enter path use. -1 is not "no selection" anywhere in
     this field's contract; it would make an empty field report a selection that
     IrcitUiClipCopy then has to reason about. */
  state->selection_start = 0;
  state->selection_end   = 0;

  /* NOT common.text. That member is the node's declared text -- the placeholder
     the painter falls back to while the buffer is empty -- and aliasing it to the
     buffer destroys it for the life of the node. The alias the painter and a host
     write through is interaction_defaults.user. */
  IrcitUiEditTextAliasSync(field_node);

  /* Re-derives the hint from the emptied buffer AND drops the suggestion panel,
     which lists candidates for the token at the caret: clearing the hint fields
     alone would leave the panel standing with the old list. */
  sEditPredict(field_node);

  // A measure pass rather than a repaint: the buffer is the field's content, so on a wrap-sized field the clear changes its box.
  IrcitUiRequestLayout(field_node);
}

void
IrcitUiEditTextDeleteToEnd(IrcitUiNode *node_ptr)
{
  IrcitUiTextEditState *state = sEditState(node_ptr);
  size_t                at, len;

  if (!state) return;
  len = strlen(state->text_buffer);
  at  = state->cursor_position;
  if (at > len) at = len;
  // Snap left, so the cut cannot leave half a character behind.
  while (at > 0 && ((unsigned char)state->text_buffer[at] & 0xC0) == 0x80) at--;
  if (at >= len) return;

  state->text_buffer[at] = 0;
  state->cursor_position = at;
  // The bytes the selection spanned are gone; it collapses to the caret, which is the convention the empty buffer and every other mutation here share.
  state->selection_start = (int)at;
  state->selection_end   = (int)at;

  IrcitUiEditTextAliasSync(node_ptr);
  // The hint describes a token that may just have been cut in half.
  sEditPredict(node_ptr);
  IrcitUiRequestLayout(node_ptr);
}
