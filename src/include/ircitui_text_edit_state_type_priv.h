/**
 * @file ircitui_text_edit_state_type_priv.h
 * @brief ircitui_text_edit_state_type_priv
 *
 * Copyright (C) 2015-2026  unfacd works
 *
 * This file is part of ircituilib source code.
 * Created by ayman on 16/09/2026.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 *  The critical difference is that on_key_down happens before the input buffer changes (giving app user veto power),
 *  whereas on_text_changed happens after the buffer changes (giving user data synchronization power).
 *  By keeping both contracts available, the framework perfectly separates input rules from data tracking:on_key_down gives app
 *  complete veto control over the raw physical keyboard stream before it 
 *  modifies states.on_text_changed provides a clean hook to pass the finalized, updated text buffer to other system components after modifications are complete.
 *
*                   [ Raw Key Ingested from TTY ] (e.g., User typed 'a')
                               │
                               ▼
        ┌─────────────────────────────────────────────┐
        │ 1. EXECUTE INTERRUPT PASS: on_key_down()    │  <-- Fires BEFORE buffer mutations
        └─────────────────────────────────────────────┘
                               │
            Did user return 1 (Veto/Consume event)?
                       ┌───────┴───────┐
             Yes ──────┘               └────── No
              ▼                                 ▼
      ┌───────────────┐               ┌─────────────────────────────────────────────┐
      │  SWALLOWED    │               │ 2. FRAMEWORK INTERNAL MUTATION PASS         │
      │               │               │    (Appends character and shifts array data)│
      │ Abort further │               └─────────────────────────────────────────────┘
      │ processing.   │                                 │
      └───────────────┘                                 ▼
                                      ┌─────────────────────────────────────────────┐
                                      │ 3. NOTIFICATION PASS: on_text_changed()     │  <-- Fires AFTER buffer mutations
                                      └─────────────────────────────────────────────┘
                                                        │
                                                        ▼
                                      [ Invalidate: the frame pass paints, later in
                                        this same loop pass and before the doupdate ]
 */

#ifndef IRCITUILIB_IRCITUI_TEXT_EDIT_STATE_TYPE_PRIV_H
#define IRCITUILIB_IRCITUI_TEXT_EDIT_STATE_TYPE_PRIV_H

#include <stddef.h>
#include "ircituilib/ircit_ui_dictionary_type.h"

#define MAX_TEXT_INPUT_BUFFER 256

typedef struct {
  char text_buffer[MAX_TEXT_INPUT_BUFFER];
  size_t cursor_position;
  int selection_start;
  int selection_end; // The extent, to selection_start's anchor; may be less than it, so the pair is ordered by comparison.

  /* THE PREDICTION, AND WHY IT IS A FIELD RATHER THAN A MODULE REGISTER.

     A hint belongs to the field that shows it, so a field freed while its hint is up takes
     the hint with it. A module-level "the hint being shown" would be a pointer into a buffer
     that a popup closing has already freed -- the hazard the engine clears three registers
     for on every deflate.

     hint_buffer holds the WHOLE candidate and hint_len its byte length, rather than holding
     the visible tail. The tail is `hint_buffer + (cursor_position - hint_token_at)`, derived
     at the point of drawing, because the tail changes as the user types and the candidate
     does not: storing the tail would mean recomputing and rewriting it on every keystroke to
     express a fact the caller can subtract.

     hint_len == 0 is "no hint", and it is the only test: a zero-length hint cannot be a
     distinct state from no hint, because both draw nothing and both accept nothing. */
  char  hint_buffer[MAX_TEXT_INPUT_BUFFER]; ///< The winning candidate, entire; "" when none.
  int   hint_len;                           ///< Its byte length; 0 when there is no hint.
  int   hint_token_at;                      ///< Where in text_buffer the predicted token starts.

  /* BORROWED, NEVER FREED HERE. The dictionary outlives every field attached to it, and
     sFreeWidgetState gives back only what the state owns -- so this pointer is deliberately
     absent from that function. Two fields may share one dictionary; a field that freed it
     would strand the other. See IrcitUiEditTextSetDictionary.

     IT IS INHERITED BY A CLONE, through common.ref, in sAllocWidgetState -- the same route
     the recycler's adapter takes. That is what makes an attachment outlive
     IrcitUiScreenReset, which IrcitUiMainLooper calls before its first frame: the reset
     reinstalls the tree by cloning the model, so an application that attached at wiring time
     would otherwise watch the field predict nothing at all. Attach to the MODEL node for
     that to hold; a node of the live tree is replaced by the next reset. */
  IrcitUiDictionary *dictionary;

  /* Whether this field follows its prediction with a suggestion panel. Off until the
     application asks: a field that grew a popup nobody requested would be a surprise, and the
     inline hint is the feature -- the panel is the list you can walk. */
  int suggest_on;
} IrcitUiTextEditState;

#endif //IRCITUILIB_IRCITUI_TEXT_EDIT_STATE_TYPE_PRIV_H
