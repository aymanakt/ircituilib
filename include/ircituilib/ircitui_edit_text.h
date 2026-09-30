/**
 * @file ircitui_edit_text.h
 * @brief The edit field's public API.
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
 * THE FIELD'S WHOLE PUBLIC SURFACE, AND NOTHING ELSE. These declarations were
 * gathered out of ui.h, which still includes this header so that a consumer
 * reaching for the umbrella goes on compiling unchanged.
 *
 * WHY A HEADER PER WIDGET. An edit field's calls are a seam of their own: they
 * are the only way a host application reaches the state the framework keeps
 * private in widget_state, and what that state IS -- a buffer, a caret, a
 * selection, a prediction -- is a fact about the field rather than about the
 * engine. Reading them beside the engine's layout, dispatch and recycler calls
 * made them look like three more things the tree does to a node. They are not:
 * the engine paints, focuses and routes a field, and a host sets, reads and
 * clears one.
 *
 * WHO CALLS WHAT. A host application uses these and the lifecycle listeners in
 * ircit_ui_interaction_state_type.h, and touches no member of any struct. The
 * engine reaches a field through the internal seam in
 * src/include/ircitui_edit_text_priv.h, which is not installed and never
 * included from outside the library. Nothing in this header exposes a member of
 * IrcitUiTextEditState, and nothing here returns one.
 *
 * IrcitUiNode IS OPAQUE. Every call below takes a handle and gives one back, so
 * a caller needs no declaration of the node's fields to drive a field -- and a
 * call naming a node that is not an edit field, or has no state, is refused
 * rather than undefined. See each call for what its refusal looks like.
 */

#ifndef IRCITUILIB_IRCITUI_EDIT_TEXT_H
#define IRCITUILIB_IRCITUI_EDIT_TEXT_H

#include "ircituilib_defs.h"
#include "ircit_ui_node_type.h"
#include "ircit_ui_dictionary_type.h"
#include "ircit_ui_event_dispatcher_type.h"
#include "ircit_ui_event_type.h"

/**
 * @brief Reads back an edit field's text.
 *
 * @details The field's value is its own buffer and only its buffer. A field whose buffer is
 *          empty reads as @c "" rather than as the placeholder its layout declared: a
 *          placeholder is what the painter falls back to for an empty field, and reporting it
 *          here would hand a caller text the user never typed.
 *
 * @param field_node A node of kind `IRCITUI_EDIT_TEXT`.
 *
 * @return The buffer, borrowed -- it lives until the field is destroyed, and the field goes on
 *         writing into it as the user types. NULL if the node is not an edit field or has no
 *         state. The caller may read it and must not free it or hold it across a dispatch.
 *
 * @see IrcitUiEditTextSetValue
 * @see IrcitUiEditTextClear
 */
PUBLIC_API const char* IrcitUiEditTextGetValue(const IrcitUiNode *field_node);

/**
 * @brief Empties an edit field and parks it at the start.
 *
 * @details The buffer is cleared, the caret goes to 0, the selection collapses to it, and the
 *          prediction is dropped -- the hint AND the suggestion panel, which lists candidates
 *          for the token at the caret and would otherwise stand there listing the old ones.
 *          The field requests a measure pass rather than a repaint, because on a wrap-sized
 *          field the buffer is the content and emptying it changes the box.
 *
 *          The node's declared text is untouched: it is the placeholder the painter shows while
 *          the buffer is empty, and it survives this call.
 *
 * @param field_node A node of kind `IRCITUI_EDIT_TEXT`. Anything else is ignored.
 *
 * @see IrcitUiClipClear
 * @see IrcitUiEditTextGetValue
 */
PUBLIC_API void IrcitUiEditTextClear(IrcitUiNode *field_node);

/* --- the field's own calls, gathered here out of ui.h ----------------------- */

/**
 * @brief Put a value into an edit field's own text buffer.
 *
 * @details This is the setter @ref IrcitUiNodeSetText cannot be for a field, and
 *          the reason is the blind spot documented there: an edit field's display
 *          resolves through an alias the engine points at the field's own buffer,
 *          so the baseline text is shadowed and, worse, a value set that way is
 *          not the string the user goes on to edit.
 *
 *          **The string is copied**, unlike @ref IrcitUiNodeSetText's borrowed
 *          pointer, and that is forced rather than chosen: the user types into
 *          this buffer in place, so a borrowed pointer could not be the target.
 *          @p text_ptr need not outlive the call.
 *
 *          The value is truncated to the field's capacity, and the caret is put at
 *          the end of what was stored — past the end of the string that was there
 *          before, which is not an index into the new one.
 *
 *          This is what a recycler binder fills an editing row with; see
 *          @ref IrcitUiRecyclerBeginEdit.
 *
 * @param node_ptr A node of kind `IRCITUI_EDIT_TEXT`; anything else is refused.
 *
 * @param text_ptr Null-terminated source. NULL is treated as the empty string.
 *
 * @return 1 on success, 0 if the node is not an edit field or has no state.
 */
PUBLIC_API int
IrcitUiEditTextSetValue(IrcitUiNode *node_ptr, const char *text_ptr);
/**
 * @brief Attach a source of predictions to an edit field.
 *
 * @details A field with a dictionary draws a dimmed completion after the caret as the user
 *          types -- the rest of the best candidate for the token the caret is in -- and a
 *          field with none does not. There is no separate "enable": attaching one is the
 *          switch, and passing NULL detaches.
 *
 *          **The dictionary is BORROWED, and the caller owns it.** Two fields may share one
 *          dictionary, so a field that released it would strand the other; the field never
 *          calls @ref IrcitUiDictionaryInterface::destroy, and freeing a field does not free
 *          what it was attached to. Detach, or destroy the dictionary after every field
 *          using it is gone.
 *
 *          **An attachment made on the MODEL survives @ref IrcitUiScreenReset; one made on a
 *          screen node does not.** A reset reinstalls the screen tree by cloning the model,
 *          and a clone inherits its dictionary from the node it was cloned from -- the same
 *          way a recycler clone inherits its adapter. So attach at wiring time to the node
 *          under @ref IrcitUiModelRoot, which is the field the framework will clone from, and
 *          the attachment outlives every reset. Attaching to a node of the live tree is
 *          attaching to a tree that the next reset replaces.
 *
 *          The prediction is evaluated on the token under the caret -- the run of non-space
 *          bytes ending there, not the whole buffer -- so a field holding a sentence predicts
 *          the word being typed rather than the sentence.
 *
 * @param node_ptr       A node of kind `IRCITUI_EDIT_TEXT`; anything else is refused.
 * @param dictionary_ptr The dictionary to borrow, or NULL to detach.
 *
 * @return 1 on success, 0 if the node is not an edit field or has no state.
 */
PUBLIC_API int
IrcitUiEditTextSetDictionary(IrcitUiNode *node_ptr, IrcitUiDictionary *dictionary_ptr);
/**
 * @brief The dictionary an edit field is borrowing, or NULL.
 *
 * @param node_ptr A node of kind `IRCITUI_EDIT_TEXT`.
 *
 * @return The pointer passed to @ref IrcitUiEditTextSetDictionary, or NULL. Borrowed: it is
 *         the caller's object and this call neither copies nor claims it.
 */
PUBLIC_API IrcitUiDictionary *
IrcitUiEditTextGetDictionary(IrcitUiNode *node_ptr);
/**
 * @brief The word the field is currently predicting, or NULL.
 *
 * @details **The whole candidate, not the dimmed tail on screen.** The tail is the candidate
 *          past what the user has typed, and a caller that wants it can subtract: it is this
 *          string advanced by the length of the token under the caret. A caller that wants to
 *          know what Tab would insert wants this.
 *
 *          NULL when there is no prediction, which includes the cases where a field has no
 *          dictionary, the caret's token has no completion, and the field is not the focused
 *          one.
 *
 * @param node_ptr A node of kind `IRCITUI_EDIT_TEXT`.
 *
 * @return A borrowed pointer into the field's own state, valid until the next keystroke, the
 *         next @ref IrcitUiEditTextSetValue, or the field's destruction.
 */
PUBLIC_API const char *
IrcitUiEditTextHint(IrcitUiNode *node_ptr);
/**
 * @brief Turn the suggestion panel on or off for a field.
 *
 * @details With it on, a field that has a prediction also gets a **list** of the candidates,
 *          anchored under it, which the arrow keys walk. The inline dimmed hint stays: the two
 *          are views of one thing, and moving the selection changes the hint, so Tab accepts
 *          whichever row is highlighted.
 *
 *          **The panel does not take focus** -- that is forced, not chosen. The loop's edit
 *          fast path only fires for a focused `IRCITUI_EDIT_TEXT`, so a panel that took focus
 *          would stop the field accepting characters. The highlighted row therefore comes from
 *          the panel's own cursor, and the field keeps the keyboard.
 *
 *          Three or more candidates are needed before it appears at all: one is what the inline
 *          hint already shows, and a two-row list that says the same thing twice is noise.
 *
 *          Off by default. A field that grew a popup nobody asked for would be a surprise.
 *
 * @param node_ptr A node of kind `IRCITUI_EDIT_TEXT`.
 * @param on       Non-zero to follow the prediction with a list, 0 to stop.
 *
 * @return 1 on success, 0 if the node is not an edit field or has no state.
 */
PUBLIC_API int
IrcitUiEditTextShowSuggestions(IrcitUiNode *node_ptr, int on);
/**
 * @brief How many candidates a field's suggestion panel is currently listing.
 *
 * @details Zero when the panel is off, when it is not up, or when the panel on screen belongs to
 *          a different field -- a count for someone else's list would be worse than no count.
 *
 *          Useful to a host that wants to show its own chrome around the list, and to a test
 *          that needs to know what was offered without reaching into the tree.
 *
 * @param node_ptr A node of kind `IRCITUI_EDIT_TEXT`.
 *
 * @return The number of rows, or 0.
 */
PUBLIC_API int
IrcitUiEditTextSuggestionCount(IrcitUiNode *node_ptr);
/**
 * @brief The candidate in a given row of the field's suggestion panel, or NULL.
 *
 * @details In the order the source enumerated them, which is the order the arrow keys walk and
 *          the order row @ref IrcitUiEditTextSuggestionSelected is highlighted in.
 *
 * @param node_ptr A node of kind `IRCITUI_EDIT_TEXT`.
 * @param ordinal  Which row, counting from 0.
 *
 * @return A borrowed string owned by the panel, valid until the next keystroke. NULL if there
 *         is no such row.
 */
PUBLIC_API const char *
IrcitUiEditTextSuggestion(IrcitUiNode *node_ptr, int ordinal);
/**
 * @brief Which row of the field's suggestion panel is highlighted.
 *
 * @details The one piece of state the panel and the inline hint share: moving this changes the
 *          dimmed tail as well, so the highlighted row and the hint cannot disagree about what
 *          Tab would accept.
 *
 * @param node_ptr A node of kind `IRCITUI_EDIT_TEXT`.
 *
 * @return The row index, or -1 if the panel is not up for this field.
 */
PUBLIC_API int
IrcitUiEditTextSuggestionSelected(IrcitUiNode *node_ptr);
/**
 * @brief Complete the focused field's prediction, writing the rest of the word in.
 *
 * @details What a host binds its accept key to. **The engine already binds one**: Tab on a
 *          focused field with a prediction calls this, from the key dispatch that precedes
 *          the field's own, so an application that binds Tab itself will not see it while a
 *          hint is up and will see it as before when there is none. This is the entry point
 *          for a host that wants a different key as well.
 *
 *          The predicted tail is spliced in **at the caret**, replacing nothing and deleting
 *          nothing: the token the user has typed stays exactly as typed, and the candidate's
 *          remainder is inserted after it. Casing is the user's -- a list in lower case
 *          completes "App" to "Apple", because only the missing tail is written.
 *
 *          The caret moves to the end of what was written and the selection collapses onto
 *          it, as every other writer here does after moving the caret. The prediction is then
 *          re-evaluated, so accepting a word that is itself the prefix of a longer one offers
 *          the longer one next.
 *
 *          **Refused, not truncated, when the field cannot hold the whole tail.** Accepting
 *          what fits would write half a word and leave the user with text they did not ask
 *          for and cannot see was clipped.
 *
 * @param node_ptr A node of kind `IRCITUI_EDIT_TEXT`.
 *
 * @return 1 if a prediction was accepted, 0 if there was none, the caret has left the token
 *         the prediction was made for, or the field has no room for it.
 */
PUBLIC_API int
IrcitUiEditTextAcceptHint(IrcitUiNode *node_ptr);
/**
 * @brief Light the focused caret and reset the blink phase to lit.
 *
 * @code{.c}
 * IrcitUiNode *root;
 *
 * initscr();
 * cbreak();
 * noecho();
 * keypad(stdscr, TRUE);
 *
 * IrcitUiDataBind();
 * IrcitUiScreenReset();
 * root = IrcitUiScreen.root;
 * IrcitUiLayout(root, 0, 0, COLS, LINES);
 * IrcitUiRealize(root);
 * IrcitUiSetFocus(IrcitUiFind(root, "cmdline"));
 * IrcitUiBlinkReset();      // caret lit immediately after focus moves
 * IrcitUiDraw(root, NULL);
 * doupdate();
 * IrcitUiDeflate(root);
 * endwin();
 * @endcode
 */
PUBLIC_API void
IrcitUiBlinkReset(void);
/**
 * @brief Should @p node_ptr's caret be drawn on this paint?
 *
 * @details The focused field's caret blinks; every other edit field's stays steady, so two fields
 *          may show a caret and the blinking one is the insertion point. The caret's cell is
 *          reserved either way, so the line never jitters.
 *
 * @param node_ptr Node being painted.
 *
 * @return Non-zero to draw the glyph.
 *
 * @code{.c}
 * IrcitUiNode *root;
 *
 * initscr();
 * cbreak();
 * noecho();
 * keypad(stdscr, TRUE);
 *
 * IrcitUiDataBind();
 * IrcitUiScreenReset();
 * root = IrcitUiScreen.root;
 * IrcitUiNode *field = IrcitUiFind(root, "cmdline");
 *
 * if (!field) {
 *   fprintf(stderr, "reference tree has no cmdline node\n");
 *   IrcitUiDeflate(root);
 *   endwin();
 *   return;
 * }
 *
 * IrcitUiLayout(root, 0, 0, COLS, LINES);
 * IrcitUiRealize(root);
 * IrcitUiSetFocus(field);
 * if (!IrcitUiCaretVisible(field))
 *   IrcitUiBlinkReset();   // this paint would show an empty caret cell
 * IrcitUiDraw(root, NULL);
 * doupdate();
 * IrcitUiDeflate(root);
 * endwin();
 * @endcode
 */
PUBLIC_API int
IrcitUiCaretVisible(const IrcitUiNode *node_ptr);
/**
 * @brief Advance the blink phase.
 *
 * @details 0 when no visible focused edit field is on screen, so an idle tick with a button or menu
 *          focused produces no output at all.
 *
 * @return Non-zero if a repaint is needed.
 *
 * @code{.c}
 * IrcitUiNode *root;
 *
 * initscr();
 * cbreak();
 * noecho();
 * keypad(stdscr, TRUE);
 *
 * IrcitUiDataBind();
 * IrcitUiScreenReset();
 * root = IrcitUiScreen.root;
 * int key;
 *
 * IrcitUiLayout(root, 0, 0, COLS, LINES);
 * IrcitUiRealize(root);
 * timeout(PRIV_CONFIG_DEFAULT_CARET_BLINK_MS);
 *
 * key = getch();
 * if (key == ERR) {                 // blink tick, not input
 *   if (IrcitUiBlinkAdvance()) {
 *     IrcitUiDraw(root, NULL);
 *     doupdate();
 *   }
 * } else {
 *   IrcitUiDispatchKey(root, key);
 * }
 * IrcitUiDeflate(root);
 * endwin();
 * @endcode
 */
PUBLIC_API int
IrcitUiBlinkAdvance(void);
/**
 * @brief Default editor: insert and delete into @p node_ptr's edit buffer.
 *
 * @details Bind it to @c on_input with @c IrcitUiAddChild or @c IrcitUiAddButton, or let the
 *          generated tree bind it for you.
 *
 * @param node_ptr The edit_text node.
 *
 * @param key Key to apply; `KEY_MOUSE` and `ERR` are ignored.
 *
 * @code{.c}
 * IrcitUiNode *root;
 *
 * initscr();
 * cbreak();
 * noecho();
 * keypad(stdscr, TRUE);
 *
 * IrcitUiDataBind();
 * IrcitUiScreenReset();
 * root = IrcitUiScreen.root;
 * IrcitUiWidgetDef def = {
 *   .id = "note", .kind = IRCITUI_EDIT_TEXT, .text = "",
 *   .width_mode = IRCITUI_SIZE_MATCH, .height_mode = IRCITUI_SIZE_WRAP,
 *   .flags = IRCITUI_F_VISIBLE | IRCITUI_F_ENABLED,
 *   .on_input = IrcitUiEditInput,
 *   .fg = 7, .bg = 0
 * };
 * IrcitUiNode *field;
 *
 * IrcitUiLayout(root, 0, 0, COLS, LINES);
 * IrcitUiRealize(root);
 * field = IrcitUiAddChild(IrcitUiFind(root, "canvas"), &def);
 * if (!field) {
 *   fputs("canvas is not a container\n", stderr);
 *   IrcitUiDeflate(root);
 *   endwin();
 *   return;
 * }
 *
 * IrcitUiSetFocus(field);
 * IrcitUiEditInput(field, 'h');
 * IrcitUiEditInput(field, KEY_BACKSPACE);
 * IrcitUiDraw(root, NULL);
 * doupdate();
 * IrcitUiDeflate(root);
 * endwin();
 * @endcode
 */
PUBLIC_API void
IrcitUiEditInput(IrcitUiNode *node_ptr, int key);
/**
 * @brief Append UTF-8 onto the focused edit_text, or onto `cmdline`.
 *
 * @details Truncated to fit at a character boundary; a control byte is dropped. The target is re-
 *          derived from @ref IrcitUiFocus on every call, so append to a specific field with @ref
 *          IrcitUiClipPaste instead if focus is not where you want the text.
 *
 * @param utf8_ptr NUL-terminated UTF-8; NULL or empty is ignored.
 *
 * @code{.c}
 * IrcitUiNode *root;
 *
 * initscr();
 * cbreak();
 * noecho();
 * keypad(stdscr, TRUE);
 *
 * IrcitUiDataBind();
 * IrcitUiScreenReset();
 * root = IrcitUiScreen.root;
 * IrcitUiLayout(root, 0, 0, COLS, LINES);
 * IrcitUiRealize(root);
 * IrcitUiSetFocus(IrcitUiFind(root, "cmdline"));
 *
 * IrcitUiInputAppend("hello ");
 * IrcitUiInputAppend("\xf0\x9f\x98\x80");   // +1 emoji, 4 bytes
 * IrcitUiDraw(root, NULL);
 * doupdate();
 * IrcitUiDeflate(root);
 * endwin();
 * @endcode
 */
PUBLIC_API void
IrcitUiInputAppend(const char *utf8_ptr);

/* --- the sync hook's shape, and the binder that installs one ---------------- */

/**
 * @brief The post-mutation notification: what a field's text became, and why it changed.
 *
 * @details Handed the finished buffer, so a host can act on an edit without polling for one. The
 *          buffer has already been mutated and accepted by the time this runs, and the return is
 *          @c void because there is nothing left to veto -- a hook that wants that authority wants
 *          @ref IrcitUiKeyDownCallback instead.
 *
 *          **This fires from the loop's fast lane and from nowhere else.** A key that reaches the
 *          buffer through the dispatcher -- ENTER, the arrows, anything above 126 -- mutates
 *          without notifying, and an application driving @ref IrcitUiDispatchKey itself never
 *          reaches it at all. See the API guide's section 10.3 for which route carries which key.
 *
 * @param disp_ptr      The dispatcher, for a hook that needs to post back through it.
 * @param node_ptr      The field whose text changed. Not a copy of the event's target.
 * @param inserted_char The character just inserted; 0 on a path that does not insert.
 * @param text_ptr      The finished buffer. **Borrowed, and live** -- the field goes on writing
 *                      into it, so a hook that stores it stores a pointer that will change. It is
 *                      the field's own storage, which is why a hook that empties the field must
 *                      copy what it needs first.
 * @param user_ptr      The pointer given to @ref IrcitUiBindTextChanged. Handed back unread.
 *
 * @see IrcitUiBindTextChanged
 */
typedef void (*IrcitUiTextChangedCallback)(IrcitUiEventDispatcher *disp_ptr,
                                           IrcitUiNode *node_ptr,
                                           int inserted_char,
                                           const char *text_ptr,
                                           void *user_ptr);
/**
 * @brief Report a field's post-mutation text to @p callback_ptr.
 *
 * @details The counterpart of @ref IrcitUiBindKey: that one is the intercept, this one is the sync.
 *          It routes the callback into the collection the fast lane reads at its notification
 *          stage, so a handler registered here is reached for every mutation that stage sees.
 *
 *          **The id is resolved once, against the reference model**, and the callback is
 *          installed on the model node that resolution found -- so this is callable from
 *          `main()`, once @ref IrcitUiDataBind has built the model, and then never again. Writing
 *          the model rather than the screen is what makes the binding outlive the tree:
 *          @ref IrcitUiClone copies `interactions_user` wholesale, so every clone of the field
 *          inherits the handler at the moment it is made, and the binding survives
 *          @ref IrcitUiScreenReset.
 *
 * @param node_id_ptr  The id of an `IRCITUI_EDIT_TEXT` node in the reference model.
 * @param callback_ptr The handler. NULL is refused rather than treated as a detach.
 * @param user_ptr     Handed back unread on every call. One pointer per node's interaction state,
 *                     so binding two hooks on one node gives them a shared value.
 *
 * @return 1 when the callback was installed, 0 when the id is NULL or empty, the callback is NULL,
 *         the node does not resolve, it is not an edit field, or the model is not built yet.
 *
 * @see IrcitUiTextChangedCallback
 * @see IrcitUiBindKey
 */
PUBLIC_API int
IrcitUiBindTextChanged(const char *node_id_ptr, IrcitUiTextChangedCallback callback_ptr, void *user_ptr);

#endif //IRCITUILIB_IRCITUI_EDIT_TEXT_H
