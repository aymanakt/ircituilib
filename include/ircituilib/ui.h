/**
 * @file ui.h
 * @brief Public API for the declarative ncurses UI engine.
 * @author ircit ui-engine
 *
 * Umbrella header. The types live in one header each, so that a consumer which
 * only needs a type can include just that header and not the whole API surface;
 * this header includes them all and declares the functions.
 *
 * Two trees exist at runtime:
 * - @ref IrcitUiModel     the reference model the framework is driving
 * - @ref IrcitUiScreen live clone that receives input and popups
 *
 * The model is reached through @ref IrcitUiModel and @ref IrcitUiFindModel, both
 * declared below; the loose globals that used to expose it are gone.
 */
#ifndef IRCITUI_UI_H
#define IRCITUI_UI_H

#include "ircituilib/config_defs.h"
#include "ircituilib/ircituilib_defs.h"
#include "ircituilib/ircit_ui_kind_enum.h"
#include "ircituilib/ircit_ui_palette_slot_enum.h"
#include "ircituilib/ircit_ui_border_style_enum.h"
#include "ircituilib/ircit_ui_bar_label_mode_enum.h"
#include "ircituilib/ircit_ui_seek_phase_enum.h"
#include "ircituilib/ircit_ui_seek_callback_type.h"
#include "ircituilib/ircit_ui_dictionary_type.h"
#include "ircituilib/ircit_ui_palette_profile_type.h"
#include "ircituilib/ircit_ui_size_mode_enum.h"
#include "ircituilib/ircit_ui_orient_enum.h"
#include "ircituilib/ircit_ui_gravity_enum.h"
#include "ircituilib/ircit_ui_flags_enum.h"
#include "ircituilib/ircit_ui_event_type_enum.h"
#include "ircituilib/ircit_ui_click_disposition_enum.h"
#include "ircituilib/ircit_ui_log_retention_enum.h"
#include "ircituilib/ircit_ui_log_level_enum.h"
#include "ircituilib/ircit_ui_selection_mode_enum.h"
#include "ircituilib/ircit_ui_event_callback_type.h"
#include "ircituilib/ircit_ui_key_event_callback_type.h"
#include "ircituilib/ircit_ui_click_callback_type.h"
#include "ircituilib/ircit_ui_snackbar_action_callback_type.h"
#include "ircituilib/ircit_ui_binding_type.h"
#include "ircituilib/ircit_ui_dim_type.h"
#include "ircituilib/ircit_ui_dirty_list_type.h"
#include "ircituilib/ircit_ui_node_type.h"
#include "ircituilib/ircit_ui_recycler_adapter_type.h"
#include "ircituilib/ircit_ui_event_type.h"
#include "ircituilib/ircit_ui_event_dispatcher_type.h"
#include "ircituilib/ircit_ui_interaction_state_type.h"
#include "ircituilib/ircit_ui_tree_type.h"
#include "ircituilib/ircitui_data_bind_descriptor_type.h"
#include "ircituilib/ircitui_node_pair_type.h"
#include "ircituilib/ircitui_context.h"
#include "ircituilib/ircitui_edit_text.h"
/* The binding surface: the binders, the callback types they take, and the click table's reset and dispatch.  */
#include "ircituilib/ui_bindings.h"
#include "ircituilib/ircit_ui_widget_def_type.h"
#include "ircituilib/ircit_ui_theme_type.h"
#include "ircituilib/ircit_ui_theme_selector_enum.h"
#include "ircituilib/ircit_ui_style_override_type.h"
#include "ircituilib/ircit_ui_theme_rule_type.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Constraint target meaning “the parent content box”. */
#define IRCITUI_PARENT (-2)
/** @brief Constraint edge not set. */
#define IRCITUI_UNSET  (-1)

/**
 * @brief The notification overlay's two conventional hold times, in milliseconds.
 *
 * These are Android's **Snackbar** durations -- `SnackbarManager.SHORT_DURATION_MS`
 * and `LONG_DURATION_MS` -- named so a caller can say "short" or "long" instead of
 * a number. They are convenience values and nothing more: any millisecond count is
 * accepted, and both are passed to @ref IrcitUiSnackbarShowAction as the ordinary
 * `duration_ms`, so nothing here is a type or a mode.
 *
 * They are deliberately **not** 2000 and 3500. Those are `NotificationManagerService`'s
 * `SHORT_DELAY` and `LONG_DELAY` -- **Toast**, not Snackbar -- and a duration that
 * says "Android's snackbar" while carrying the toast's numbers is worse than no
 * constant at all. Worth knowing when reading Android's own API: there,
 * `Snackbar.LENGTH_SHORT` and `LENGTH_LONG` are sentinel ints (`-1` and `0`) rather
 * than milliseconds, so the names mean something different in the two libraries.
 */
#define IRCITUI_SNACKBAR_LENGTH_SHORT 1500
/** @brief See @ref IRCITUI_SNACKBAR_LENGTH_SHORT. */
#define IRCITUI_SNACKBAR_LENGTH_LONG  2750

/** @brief Mutable clone plus stacked popups (MainUITreeScreenModel). */
extern IrcitUiTree IrcitUiScreen;
/** @brief Node that receives keyboard input; may be NULL. */
extern IrcitUiNode *IrcitUiFocus;

/**
 * @brief Set @ref IrcitUiFocus to @p node_ptr, mirroring it in IRCITUI_F_FOCUSED; NULL clears.
 *
 * @details Also resets the caret blink to lit, so focus never lands on a field whose caret is in
 *          the middle of a dark half-cycle.
 *
 * @param node_ptr Node to focus, or NULL to clear focus altogether.
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
 * IrcitUiSetFocus(field);   // NULL here would clear focus instead
 * IrcitUiDraw(root, NULL);
 * doupdate();
 * IrcitUiDeflate(root);
 * endwin();
 * @endcode
 */
/**
 * @brief Set the baseline text of a node.
 *
 * @details Writes the node's own content field. **The string is borrowed**: the
 *          node never copies and never frees it, so it must outlive the node. That
 *          is the rule every text in this tree already follows, and the one an
 *          out-of-tree caller is most likely to break — a stack buffer here is a
 *          node pointing at reclaimed memory, and a default AddressSanitizer run
 *          will not report it.
 *
 *          **One documented blind spot.** A node whose display is *aliased* is not
 *          changed by this call. The painter resolves its string as the node's
 *          interaction alias first and this value second, and the engine points
 *          that alias at live buffers of its own: an edit field's text buffer, and
 *          the status label's echo buffer. Writing the alias here — or clearing it
 *          to force this value through — would sever that link and lose the
 *          field's text on a resize. So this sets the baseline and the alias, where
 *          present, continues to shadow it.
 *
 * @param node_ptr Target node; NULL is ignored.
 *
 * @param text_ptr Borrowed, null-terminated, and it must outlive the node.
 */
PUBLIC_API void
IrcitUiNodeSetText(IrcitUiNode *node_ptr, const char *text_ptr);
/**
 * @brief Set a node's text when the text CANNOT change the node's box.
 *
 * @details The same assignment as @ref IrcitUiNodeSetText with one difference, and the
 *          difference is the whole point: this one marks the node for REPAINT and does not
 *          request a layout pass.
 *
 *          **The caller asserts that this text cannot change the node's box.** A text view
 *          whose width is `match_parent` and whose height is an exact number of rows cannot be
 *          resized by its string, and that is the shape a live readout has. A node whose size
 *          follows its text -- a `wrap_content` label, anything whose height is computed from
 *          the string -- must keep using @ref IrcitUiNodeSetText, because for those the layout
 *          pass is what makes the change visible at all. Passing such a node here leaves a
 *          stale box.
 *
 *          **Why the distinction matters enough to be a second function.** A layout request
 *          re-runs the layout and re-realizes the subtree: windows are torn down and rebuilt,
 *          and a window re-created at screen size carries ncurses' own clear flag, so the next
 *          refresh emits a whole-screen clear. An application writing a readout at frame rate
 *          measured **15.5 whole-screen clears per second** -- a visibly strobing panel -- and
 *          with this function in place the same application measures the idle baseline. That
 *          is a measured pair, not an expectation.
 *
 *          **When to use which:**
 *
 *          | the text... | use |
 *          |---|---|
 *          | can change the box (wrap_content, a label whose height follows its string) | @ref IrcitUiNodeSetText |
 *          | cannot (fixed height, match_parent width) | this |
 *          | is set once, at build or raise time | either -- the cost is paid once |
 *
 *          @ref IrcitUiBarSetValue is the same idea for a number, and has been since it was
 *          written: it marks the node for repaint and does not request a layout.
 *
 * @param node_ptr Target node. Ignored if NULL.
 *
 * @param text_ptr The new text. **Borrowed, not copied** -- it must outlive the node. NULL is
 *                 legal and clears it.
 *
 * @see IrcitUiNodeSetText
 * @see IrcitUiInvalidate
 */
PUBLIC_API void
IrcitUiNodeSetTextLive(IrcitUiNode *node_ptr, const char *text_ptr);

/**
 * @brief Build a dictionary over a fixed list of words.
 *
 * @details The simplest of the shipped sources, and the shape a host's own is modelled on:
 *          the list is searched in order and the first entry that completes the typed token
 *          wins, case-folded over ASCII.
 *
 *          **The word array is BORROWED and must outlive the dictionary** -- it is usually a
 *          file-scope array, and copying it would be a silent allocation per word for no
 *          gain. The array itself is not copied and not modified.
 *
 *          Every entry is expected to be a whole word; an entry equal to what the user has
 *          typed is not a completion and is skipped, so a list may contain a word and its own
 *          prefixes without offering to complete a word to itself.
 *
 * @param words_ptr An array of NUL-terminated words. Not NULL, and not modified.
 * @param count     How many entries it holds. Zero is refused.
 *
 * @return A dictionary to hand to @ref IrcitUiEditTextSetDictionary, or NULL if the arguments
 *         are unusable or nothing could be allocated. Release it with
 *         @ref IrcitUiDictionaryDestroy.
 */
PUBLIC_API IrcitUiDictionary *
IrcitUiDictionaryCreateList(const char *const *words_ptr, size_t count);

/**
 * @brief Build an empty trie dictionary: the general form of a word list.
 *
 * @details A prefix tree over UTF-8 bytes, so a lookup costs the length of what was typed
 *          rather than the size of the vocabulary, and so an emoji shortcode is
 *          indistinguishable from a word to the traversal. Case-folded over ASCII during both
 *          insertion and lookup.
 *
 *          Children are stored as a sorted array of the bytes that actually occur, not as a
 *          256-entry table per node. That is a departure from the design document and it is
 *          arithmetic rather than taste: a 256-pointer table is 2 KB per node on LP64, and the
 *          same document describes ingesting a file of 100,000 to 500,000 words.
 *
 *          Release with @ref IrcitUiDictionaryDestroy, which frees every node.
 *
 * @return An empty dictionary, or NULL if nothing could be allocated.
 */
PUBLIC_API IrcitUiDictionary *
IrcitUiDictionaryCreateTrie(void);

/**
 * @brief Build a dictionary over emoji shortcodes, keyed `:tag:`.
 *
 * @details The candidates it offers are the tags -- `:fir` completes to `:fire:`, so what the
 *          user reads is the shortcode -- and what an accept writes is the **glyph**. That
 *          substitution is expressed through @ref IrcitUiDictionaryInterface::replace rather
 *          than by the widget, which is why the interface has that entry at all.
 *
 *          The table is fixed and small; it is a demonstration of the seam rather than a
 *          general emoji database.
 *
 * @return A dictionary, or NULL if nothing could be allocated.
 */
PUBLIC_API IrcitUiDictionary *
IrcitUiDictionaryCreateEmoji(void);

/**
 * @brief Build a composite that asks one dictionary and then another.
 *
 * @details The first source that answers wins, and a source that answers nothing is not a
 *          veto -- a chain of an emoji table and a word list still completes a word the emoji
 *          table has never heard of. `next` flattens the ordinals across the chain, so a
 *          candidate list walks the first source's matches and then the second's.
 *
 *          **The members are borrowed, and the composite never destroys them**: one source may
 *          be in two chains. Destroy each member after every composite naming it is gone.
 *
 *          More than two can be chained with @ref IrcitUiDictionaryChain.
 *
 * @param first_ptr  The source asked first. May be NULL.
 * @param second_ptr The source asked second. May be NULL.
 *
 * @return A dictionary, or NULL if both arguments are NULL or nothing could be allocated.
 */
PUBLIC_API IrcitUiDictionary *
IrcitUiDictionaryCreateComposite(IrcitUiDictionary *first_ptr, IrcitUiDictionary *second_ptr);

/**
 * @brief Add another source to the end of a composite's chain.
 *
 * @details The composite asks its sources in the order they were chained, so this appends
 *          rather than inserting: a later source is asked only about the prefixes the earlier
 *          ones had nothing to say about.
 *
 * @param composite_ptr A dictionary built by @ref IrcitUiDictionaryCreateComposite.
 * @param source_ptr    The source to append. Borrowed, as the constructor's are.
 *
 * @return 1 on success, 0 if the first argument is not a composite, if it is already full
 *         (four sources) or either argument is NULL.
 */
PUBLIC_API int
IrcitUiDictionaryChain(IrcitUiDictionary *composite_ptr, IrcitUiDictionary *source_ptr);

/**
 * @brief Put a word into a trie dictionary.
 *
 * @details Inserting the same word twice is not an error and not a duplicate: the second call
 *          updates the payload and the static flag, which is what re-loading a word file or
 *          learning a word the file already supplied does.
 *
 * @param dictionary_ptr A dictionary built by @ref IrcitUiDictionaryCreateTrie.
 * @param word_ptr       The whole word, NUL-terminated. Empty is refused. **Copied**, and
 *                       folded for lookup but stored as given -- so a candidate comes back in
 *                       the casing it was inserted with.
 * @param payload_ptr    What an accept writes instead of the word's tail, or NULL for the
 *                       ordinary case. Copied.
 * @param is_static      Non-zero to protect the entry from @ref IrcitUiDictionaryPrune.
 *
 * @return 1 on success, 0 if the dictionary is not a trie or an allocation failed.
 */
PUBLIC_API int
IrcitUiDictionaryInsert(IrcitUiDictionary *dictionary_ptr, const char *word_ptr,
                        const char *payload_ptr, int is_static);

/**
 * @brief Write a trie's LEARNED entries to a file, so they survive the session.
 *
 * @details A profile is the user's own vocabulary, not the dictionary: only entries that were
 *          learned and are not static are written. A word that came from a file or from the
 *          compiled-in list will be there again next time, and writing it down would mean a
 *          profile that could disagree with the source it came from.
 *
 *          The format is the four-byte marker `UIT1` followed by one record per word --
 *          `[4-byte weight][2-byte length][bytes]` -- written byte by byte rather than as a
 *          struct, so the file means the same thing on a machine that disagrees about padding
 *          or byte order.
 *
 *          **Not called by the framework.** Nothing writes a file during teardown; the
 *          application decides when a profile is saved. @p path_ptr is used as given -- `~` is
 *          the shell's to expand, not this call's.
 *
 * @param dictionary_ptr A dictionary built by @ref IrcitUiDictionaryCreateTrie.
 * @param path_ptr       Where to write.
 *
 * @return How many words were written, or 0 if the file could not be opened or the dictionary
 *         is not a trie.
 */
PUBLIC_API size_t
IrcitUiDictionaryExport(IrcitUiDictionary *dictionary_ptr, const char *path_ptr);

/**
 * @brief Read a profile back into a trie, restoring each word's weight.
 *
 * @details The counterpart of @ref IrcitUiDictionaryExport. A file that is not there is **not
 *          an error** -- a profile that does not exist yet is the first run -- and contributes
 *          nothing.
 *
 *          A malformed file is refused rather than trusted. The length field is read from disk
 *          and is therefore not this library's data, so the marker is checked, the length is
 *          bounded, a record whose length disagrees with its own string is treated as the end
 *          of the file, and whatever was read before that point is kept. Truncation at a record
 *          boundary is indistinguishable from a clean end and is treated as one.
 *
 * @param dictionary_ptr A dictionary built by @ref IrcitUiDictionaryCreateTrie.
 * @param path_ptr       The file to read.
 *
 * @return How many words were loaded, or 0 if there is no usable file or the dictionary is not
 *         a trie.
 */
PUBLIC_API size_t
IrcitUiDictionaryImport(IrcitUiDictionary *dictionary_ptr, const char *path_ptr);

/**
 * @brief Turn on approximate matching for a trie, with an edit-distance threshold.
 *
 * @details A SECOND TIER, ASKED ONLY WHEN THE EXACT WALK FINDS NOTHING. That is the whole of
 *          why it is affordable: an exact prefix walk costs the length of what was typed,
 *          while an approximate one costs a walk of the subtree, so the common case -- a word
 *          being spelled correctly -- never pays for the possibility that it is not. Type
 *          `Aple` and, with this on, the field offers `apple`.
 *
 *          The threshold is clamped to 0..3 rather than refused. Above three, an edit distance
 *          stops being a typo tolerance and starts being a different word, and the cost of the
 *          walk grows with it.
 *
 *          **Approximate matches lose to exact ones**, always: the tier is only reached when
 *          the exact walk returned nothing. Ties among approximate matches are broken the way
 *          the exact walk breaks them -- the heavier word wins, and an equal weight falls to
 *          lexicographic order.
 *
 * @param dictionary_ptr A dictionary built by @ref IrcitUiDictionaryCreateTrie.
 * @param threshold      Maximum edit distance to accept; 0 turns approximate matching off.
 *
 * @return 1 on success, 0 if the dictionary is not a trie.
 */
PUBLIC_API int
IrcitUiDictionarySetFuzzy(IrcitUiDictionary *dictionary_ptr, int threshold);

/**
 * @brief How many nodes a trie dictionary holds.
 *
 * @details The trie's own node count, which is what the memory it costs scales with -- and the
 *          number the sparse-children decision was made against. Reported rather than guessed
 *          at, so a caller loading a large file can see what it bought.
 *
 * @param dictionary_ptr A dictionary.
 *
 * @return The node count of a trie, or 0 for any other kind of dictionary.
 */
PUBLIC_API size_t
IrcitUiDictionarySize(IrcitUiDictionary *dictionary_ptr);

/**
 * @brief Record that a word was used, so it wins the next prediction.
 *
 * @details The heaviest completion of a prefix is the one offered, and this is what makes a
 *          word heavy. A word that is not in the trie is **inserted by being learned**, which
 *          is the point of learning from what a user types rather than from a list.
 *
 *          The weight saturates rather than wrapping: a word typed a million times must not
 *          roll over to a low number and lose to one typed twice.
 *
 * @param dictionary_ptr A dictionary built by @ref IrcitUiDictionaryCreateTrie.
 * @param word_ptr       The whole word, NUL-terminated. Empty is refused.
 *
 * @return 1 on success, 0 if the dictionary is not a trie or an allocation failed.
 */
PUBLIC_API int
IrcitUiDictionaryLearn(IrcitUiDictionary *dictionary_ptr, const char *word_ptr);

/**
 * @brief Drop learned words that have fallen below a weight, and free what held them.
 *
 * @details A post-order sweep. An entry is dropped when it is **not static and its weight is
 *          below @p min_weight**, and every node left with nothing under it is freed -- so
 *          pruning one learned word can free its whole path, which is why the sweep is
 *          post-order and why the node count is recounted afterwards rather than decremented.
 *
 *          **Static entries are never dropped.** A word that came from a file or from the
 *          emoji table is not the user's to lose; one that was learned is, once it stops
 *          being used. That flag is the whole of the policy -- there is deliberately no second
 *          way to mean "system entry", because two ways to say one thing are two ways to
 *          disagree.
 *
 * @param dictionary_ptr A dictionary built by @ref IrcitUiDictionaryCreateTrie.
 * @param min_weight     Keep learned entries at or above this weight; 1 keeps everything
 *                       learned at least once.
 *
 * @return How many words were dropped, 0 if the dictionary is not a trie or nothing qualified.
 */
PUBLIC_API size_t
IrcitUiDictionaryPrune(IrcitUiDictionary *dictionary_ptr, unsigned min_weight);

/**
 * @brief Read a word list off disk into a trie, one word per line.
 *
 * @details Every line that survives is inserted **static**, so the sweep never drops what a
 *          file supplied. Blank lines are skipped, a trailing CR or LF is trimmed, and a line
 *          longer than @p max_word_length is refused rather than truncated -- half a word is a
 *          candidate nobody can type.
 *
 *          **A file that cannot be opened is logged and contributes nothing**, not an error:
 *          an optional dictionary file that is missing is a state the field already handles,
 *          and a library that refused to start over one would be a worse neighbour.
 *
 * @param dictionary_ptr  A dictionary built by @ref IrcitUiDictionaryCreateTrie.
 * @param path_ptr        The file to read. Not expanded: `~` is the shell's, not this call's.
 * @param max_word_length Longest line to accept; 0 for no limit.
 * @param refused_ptr     Optional out-parameter, set to how many lines were refused. May be
 *                        NULL, in which case the count is discarded.
 *
 * @return How many words are in the trie as a result, which is fewer than the lines read when
 *         the file names a word twice.
 */
PUBLIC_API size_t
IrcitUiDictionaryIngestFile(IrcitUiDictionary *dictionary_ptr, const char *path_ptr,
                            size_t max_word_length, size_t *refused_ptr);

/**
 * @brief Release a dictionary built by one of this library's factories.
 *
 * @details Calls the interface's own @ref IrcitUiDictionaryInterface::destroy, so a host's
 *          dictionary releases its own instance its own way, then frees the descriptor.
 *          **Every field still attached to it must be detached first**, or have been freed
 *          already: this frees what they are pointing at.
 *
 *          Passing NULL is a no-op.
 *
 * @param dictionary_ptr The dictionary to release.
 */
PUBLIC_API void
IrcitUiDictionaryDestroy(IrcitUiDictionary *dictionary_ptr);
/**
 * @brief Set a progress bar's position, as a whole percentage.
 *
 * @details The setter for a node of kind @ref IRCITUI_PROGRESS_BAR or
 *          @ref IRCITUI_SEEK_BAR: it is the one thing about a bar that is live
 *          data rather than configuration, so it is the one thing that cannot come
 *          from the layout. The glyphs and the label mode are template keys (C24);
 *          the value is the application's.
 *
 *          **Why a percentage and not the float the design document carries.** A
 *          terminal cell is not divisible. Every read of a [0.0f, 1.0f] value would
 *          be a rounding, and the rounding is exactly what must not differ between
 *          the painter that fills the track and any hit test that maps a column
 *          back to a position -- a disagreement of one column is a seek that lands
 *          on the wrong cell. The rounding happens once, here, and is visible.
 *
 *          **Safe to call from a listener, and safe to call repeatedly.** An
 *          unchanged value is a no-op and does not invalidate, so a worker-driven
 *          bar that reports the same position twice costs one comparison rather
 *          than a repaint. Out-of-range values are clamped, not refused, because a
 *          position is a measurement: a caller that computes 103% from a rounding
 *          of its own means "finished", and an error return would leave the bar
 *          showing a stale value while the caller believed it had reported.
 *
 *          **No aliasing blind spot**, unlike @ref IrcitUiNodeSetText. A bar's
 *          value is not a string and has no `interaction_defaults.user` alias to be
 *          shadowed by, so what is written here is what is drawn.
 *
 * @param node_ptr Target node. A node that is not a bar is ignored, as is NULL.
 *
 * @param pct Position; below 0 becomes 0, above 100 becomes 100.
 *
 * @see IrcitUiBarValue
 */
PUBLIC_API void
IrcitUiBarSetValue(IrcitUiNode *node_ptr, int pct);
/**
 * @brief A bar's current position, 0-100.
 *
 * @details What @ref IrcitUiBarSetValue last stored, which is the value about to
 *          be drawn rather than one read back off the glass -- so it is valid
 *          before the node has been realized or painted, and it cannot be a frame
 *          behind a repaint that has not happened yet.
 *
 * @param node_ptr Target node. NULL, or a node that is not a bar, returns 0 --
 *                 which is also a real position, so a caller that needs to tell
 *                 "not a bar" from "at rest" must check the kind with
 *                 @ref IrcitUiNodeGetKind first.
 *
 * @return The stored position in [0, 100].
 */
PUBLIC_API int
IrcitUiBarValue(const IrcitUiNode *node_ptr);
/**
 * @brief Store how far a bar has been buffered, 0-100.
 *
 * @details The bar's SECOND position, and the one a streaming source needs: @ref
 *          IrcitUiBarSetValue is where playback has reached, this is how far the data
 *          behind it has arrived. The track is then painted in three runs -- played,
 *          buffered-but-not-played, and empty -- so a stalled stream is one where the
 *          two converge and a healthy one is where this runs ahead.
 *
 *          **It is drawn from the value to here, never behind it.** A buffer further
 *          back than the play head is the ordinary state of a stalled stream rather
 *          than a run drawn backwards, and the painter clamps rather than iterating a
 *          negative span. Setting this below @ref IrcitUiBarValue is therefore not an
 *          error and not a special case: it draws nothing, which is what "playback is
 *          ahead of the buffer" looks like.
 *
 *          **The run is the fill glyph at @ref IRCITUI_SLOT_ACCENT dimmed**, not the
 *          node's own colour -- so "buffered" reads the same on every bar, as the same
 *          accent held back, rather than as a second copy of whatever palette role the
 *          template gave the node. Same glyph and same hue as the played run, lower
 *          intensity: that is the whole of the distinction, and it is what makes the
 *          band legible as "the same thing, not yet reached".
 *
 * @param node_ptr Target node. A node that is not a bar is ignored, as is NULL.
 *
 * @param pct Buffered position; below 0 becomes 0, above 100 becomes 100.
 *
 * @see IrcitUiBarBuffered
 * @see IrcitUiBarSetValue
 */
PUBLIC_API void
IrcitUiBarSetBuffered(IrcitUiNode *node_ptr, int pct);
/**
 * @brief A bar's buffered position, 0-100.
 *
 * @details What @ref IrcitUiBarSetBuffered last stored, on the same terms as
 *          @ref IrcitUiBarValue: the value about to be drawn, valid before the node is
 *          realized, not read back off the glass.
 *
 *          **0 is a real answer.** A bar nobody has buffered reports 0, and that is
 *          true rather than missing -- the same rule @ref IrcitUiBarValue states, and
 *          the reason neither field carries an is-set flag. A caller that needs to
 *          distinguish "not a bar" from "nothing buffered" must check the kind with
 *          @ref IrcitUiNodeGetKind first, exactly as it must for the value.
 *
 * @param node_ptr Target node. NULL, or a node that is not a bar, returns 0.
 *
 * @return The stored buffered position in [0, 100].
 */
PUBLIC_API int
IrcitUiBarBuffered(const IrcitUiNode *node_ptr);
/**
 * @brief Report a bar's position from a worker thread.
 *
 * @details The other half of @ref IrcitUiBarSetValue, and the reason it exists rather than
 *          being left to callers: **a worker must never touch a node.** The tree is owned
 *          by the loop's thread, and a worker that wrote to it would race every paint and
 *          every layout pass that thread is running. What a worker may do is hand a value
 *          over and ring the doorbell, which is what this does -- it copies the id and the
 *          number into a fixed-size envelope, posts it on the lock-free ring, and returns.
 *          The loop applies it on its next pass, on the thread that owns the tree.
 *
 *          **It takes an id, not a node, and that is a safety property rather than a
 *          convenience.** A report is posted repeatedly while a run is in flight, and the
 *          widget it belongs to can be closed at any point in the middle -- a panel the
 *          user dismissed. A node pointer captured by the worker would then be a pointer
 *          to freed memory, and nothing on the receiving side can tell a freed node from a
 *          live one. The id cannot dangle: node ids are borrowed literals from the
 *          generated model and outlive every clone. A report whose node is gone resolves to
 *          nothing and is dropped, so a run needs no cancellation protocol to be safe.
 *
 *          The value is applied through the same clamp and the same no-op-on-unchanged
 *          check as the direct setter, because it goes through the same setter.
 *
 * @param disp_ptr The dispatcher the loop is draining. NULL is ignored.
 *
 * @param node_id_ptr The target node's id. Borrowed, and it must remain valid until the
 *                    report is consumed -- which for a generated node is the life of the
 *                    process. NULL or empty is ignored.
 *
 * @param pct Position; below 0 becomes 0, above 100 becomes 100.
 *
 * @see IrcitUiBarSetValue
 */
PUBLIC_API void
IrcitUiPostBarProgress(IrcitUiEventDispatcher *disp_ptr, const char *node_id_ptr, int pct);
/**
 * @brief Report a bar's BUFFERED position from a worker thread.
 *
 * @details The sibling of @ref IrcitUiPostBarProgress, on exactly the same terms -- it
 *          takes an id rather than a node, it is clamped before it is posted, and Thread 0
 *          resolves the id and drops the report in silence when the widget is gone -- and
 *          it differs only in the setter it reaches: @ref IrcitUiBarSetBuffered rather
 *          than @ref IrcitUiBarSetValue.
 *
 *          **It is a separate function rather than a flag on the one above**, which is the
 *          same decision the dispatcher's tag makes and for the same reason: the two
 *          reports are acted on differently on Thread 0, and a flag would mean every
 *          producer of either has to know about a field that is none of its business.
 *
 * @param disp_ptr     The dispatcher. Ignored if NULL.
 *
 * @param node_id_ptr  The target bar's id. A report whose id no longer resolves is
 *                     dropped, which is what makes a run safe to outlive its panel.
 *                     Ignored if NULL or empty.
 *
 * @param pct          Buffered position; below 0 becomes 0, above 100 becomes 100.
 *
 * @see IrcitUiBarSetBuffered
 * @see IrcitUiPostBarProgress
 */
PUBLIC_API void
IrcitUiPostBarBuffered(IrcitUiEventDispatcher *disp_ptr, const char *node_id_ptr, int pct);
/**
 * @brief Set a bar's fill and empty glyphs.
 *
 * @details The C-API half of C24's `fill_glyph` / `empty_glyph`, for a bar built in code
 *          rather than declared in a layout. The two surfaces set the same fields, so a
 *          bar does not care which one configured it.
 *
 *          **Borrowed, on the same terms as @ref IrcitUiNodeSetText**, and for the same
 *          reason: a node never owns its text, and these are strings like any other. The
 *          generated model points them at literals, which live as long as the program; a
 *          caller passing a buffer must keep it alive as long as the node.
 *
 *          Either argument may be NULL or empty, which means "no opinion" and restores the
 *          framework's default for that half -- U+2588 for the fill, U+2591 for the empty
 *          lane. The two are independent, so a bar may name one and default the other.
 *
 *          Naming a glyph that is not exactly one code point is refused HERE rather than
 *          at paint time, because the layout path refuses it too (C24, enforced in the
 *          validator and the generator) and a C-built bar should not be the way to smuggle
 *          in a two-codepoint glyph that would be drawn as its first cell and silently
 *          truncate. A refused call changes nothing.
 *
 * @param node_ptr Target node. A node that is not a bar is ignored, as is NULL.
 *
 * @param fill_ptr  Borrowed glyph for the filled run, or NULL for the default.
 *
 * @param empty_ptr Borrowed glyph for the empty lane, or NULL for the default.
 *
 * @return 1 if the glyphs were set, 0 if the node is not a bar or a glyph was malformed.
 */
PUBLIC_API int
IrcitUiBarSetGlyphs(IrcitUiNode *node_ptr, const char *fill_ptr, const char *empty_ptr);
/**
 * @brief Set where a bar draws its percentage.
 *
 * @details The C-API half of C24's `label_mode`. **@ref IRCITUI_BAR_LABEL_ADJACENT makes
 *          the bar narrower** -- the label's columns are reserved beside it -- so this is
 *          a change to the bar's geometry and not only to its paint, and it invalidates
 *          rather than relayouts because the NODE's box is unchanged: what moves is how
 *          much of that box the track may use. That resolution lives in one function the
 *          painter and any hit test both call, which is what keeps a seek from landing on
 *          the wrong column.
 *
 *          An out-of-range mode is ignored, leaving the bar as it was, following
 *          @ref IrcitUiSetBorderStyle's treatment of an unknown style.
 *
 * @param node_ptr Target node. A node that is not a bar is ignored, as is NULL.
 *
 * @param mode One of @ref IRCITUI_BAR_LABEL_OFF, @ref IRCITUI_BAR_LABEL_ADJACENT or
 *             @ref IRCITUI_BAR_LABEL_CENTERED.
 *
 * @see IrcitUiBarLabelMode
 */
PUBLIC_API void
IrcitUiBarSetLabelMode(IrcitUiNode *node_ptr, IrcitUiBarLabelMode mode);
/**
 * @brief Give a bar a gradient across its filled run, between two colours.
 *
 * @details 8-bit RGB at each end, interpolated across the filled portion. The gradient is
 *          drawn from a borrowed colour ramp -- a register the palette takes out of the
 *          terminal's colour space and hands back -- and a terminal that cannot spare one
 *          is not an error: the bar falls back to its flat colour and **that fact is
 *          recorded rather than hidden**. @ref IrcitUiBarGradientLive is how to ask.
 *
 *          **The ramp is a scarce, shared resource.** Four exist process-wide
 *          (`PRIV_CONFIG_DEFAULT_PALETTE_RAMPS`), and the canopy, the pulse, the fade and
 *          the notification box compete for the same four. A bar holds one only while it
 *          has a gradient, and releases it when the gradient is cleared or the node is
 *          freed -- including when a popup panel carrying it is popped. An application
 *          that wants more live gradients than the budget allows should expect the later
 *          ones to draw flat.
 *
 *          The ramp is acquired at a FIXED resolution and held, so moving the value costs
 *          nothing: a bar advanced every tick does not touch the palette at all. That is
 *          deliberate and is not what the design document does, which acquires a register
 *          per filled column and so reloads the palette on every change.
 *
 * @param node_ptr Target node. A node that is not a bar is ignored, as is NULL.
 *
 * @param r1,g1,b1 The colour at the filled run's start, 0-255 each.
 *
 * @param r2,g2,b2 The colour at its end, 0-255 each.
 *
 * @see IrcitUiBarClearGradient
 * @see IrcitUiBarGradientLive
 */
PUBLIC_API void
IrcitUiBarSetGradient(IrcitUiNode *node_ptr, int r1, int g1, int b1, int r2, int g2, int b2);
/**
 * @brief Take a bar's gradient away and give its ramp back.
 *
 * @details The bar returns to its flat colour and the borrowed register is restored to
 *          whatever it held before, which is what frees one of the four slots for another
 *          widget. Idempotent: clearing a bar with no gradient does nothing.
 *
 * @param node_ptr Target node. A node that is not a bar is ignored, as is NULL.
 */
PUBLIC_API void
IrcitUiBarClearGradient(IrcitUiNode *node_ptr);
/**
 * @brief Whether a bar's gradient is actually being drawn.
 *
 * @details The question @ref IrcitUiBarSetGradient cannot answer. A gradient was ASKED for
 *          when the setter returned; whether it is LIVE depends on a register being
 *          available, which depends on what every other gradient user in the process is
 *          holding at that moment -- so it is a fact about the running program and not
 *          about the call.
 *
 *          Returns 0 for a bar with no gradient, for a bar whose ramp could not be
 *          acquired, and for a node that is not a bar. A caller that needs to tell those
 *          apart can compare @ref IrcitUiBarGradientRequested.
 *
 * @param node_ptr Target node.
 *
 * @return Non-zero when the filled run is being drawn from a ramp.
 */
PUBLIC_API int
IrcitUiBarGradientLive(const IrcitUiNode *node_ptr);
/**
 * @brief Whether a bar has been ASKED for a gradient, whether or not it got one.
 *
 * @details The other half of @ref IrcitUiBarGradientLive, and the pair exists because the
 *          difference between them is the one thing an application cannot otherwise see:
 *          a bar drawing flat because nothing asked, and a bar drawing flat because the
 *          process is out of ramps, look identical on the glass. A demo or a diagnostic
 *          that means to show the second has to be able to name it.
 *
 * @param node_ptr Target node.
 *
 * @return Non-zero when a gradient has been requested and not cleared.
 */
PUBLIC_API int
IrcitUiBarGradientRequested(const IrcitUiNode *node_ptr);
/**
 * @brief Read accessors for a node, because @ref IrcitUiNode is opaque.
 *
 * @details A listener receives a node and, without these, can learn nothing about
 *          it: the geometry a callback needs to size itself, the kind it is, the
 *          id it was declared with, the text it shows, and the flags that say
 *          whether it is visible, enabled or focused. DESIGN.md SS3.8 is the
 *          read-access gap and G25 is its register entry.
 */
/** @brief The node's kind. */
PUBLIC_API IrcitUiKind
IrcitUiNodeGetKind(const IrcitUiNode *node_ptr);
/** @brief The node's id, or NULL if it was declared without one. Borrowed. */
PUBLIC_API const char *
IrcitUiNodeGetId(const IrcitUiNode *node_ptr);
/** @brief The node's text, or NULL. Borrowed: the node never frees it. */
PUBLIC_API const char *
IrcitUiNodeGetText(const IrcitUiNode *node_ptr);
/** @brief The node's laid-out box. Any pointer may be NULL if that value is not
 *         wanted. Zeroes before the first layout. */
PUBLIC_API void
IrcitUiNodeGetGeometry(const IrcitUiNode *node_ptr, int *x_ptr, int *y_ptr, int *w_ptr, int *h_ptr);
/** @brief The node's IRCITUI_F_* flag word. */
PUBLIC_API unsigned
IrcitUiNodeGetFlags(const IrcitUiNode *node_ptr);
/**
 * @brief Read accessors for an event, because @ref IrcitUiEvent is opaque.
 *
 * @details Every listener takes an event as its second argument and, without
 *          these, is signature-compliant and functionally blind -- DESIGN.md G2
 *          and G35. The payload a handler needs is the key code, or the
 *          coordinates that were clicked.
 */
/** @brief The event's key code, or 0 if it carries none. */
PUBLIC_API int
IrcitUiEventGetKey(const IrcitUiEvent *ev_ptr);
/** @brief The event's mouse coordinates. Either pointer may be NULL. */
PUBLIC_API void
IrcitUiEventGetMouseCoords(const IrcitUiEvent *ev_ptr, int *x_ptr, int *y_ptr);

PUBLIC_API void
IrcitUiSetFocus(IrcitUiNode *node_ptr);
/**
 * @brief The node that currently holds keyboard focus, or NULL when nothing does.
 *
 * @details Focus is what the engine resolves a key against before it walks the tree: the node
 *          returned here is the one that receives a keystroke first, and the one whose caret
 *          blinks. It changes when a click lands on a focusable node, when keyboard traversal
 *          moves it, and when @ref IrcitUiSetFocus, @ref IrcitUiFocusTop or @ref
 *          IrcitUiFocusInLayer set it directly.
 *
 *          **It is a pointer, not a promise.** Two things the caller should not assume:
 *
 *          - **The node may not be on the screen.** Nothing clears focus when a node stops being
 *            drawn. Folding a collapsible ancestor, or clipping a node out of its parent, leaves
 *            focus on a node that has no window and is skipped by the painter. Keystrokes still
 *            reach it. Test @c IRCITUI_F_VISIBLE, @c IRCITUI_F_CLIPPED and the window before
 *            treating the result as something the user can see.
 *          - **The node may have moved on since you asked.** Focus is not a lock and the tree is
 *            not frozen between calls. Read it, act, and re-read rather than holding it across a
 *            dispatch.
 *
 *          What is maintained is that the pointer names a **live** node or NULL. It is cleared
 *          where a tree is replaced (@ref IrcitUiScreenReset) and where the subtree containing
 *          it is freed (@ref IrcitUiPopStacked, guarded by containment).
 *
 * @return The focused node, or NULL. Never a freed node, but not necessarily a drawn one.
 *
 * @code{.c}
 * IrcitUiNode *field = IrcitUiFocusedNode();
 *
 * if (field && (field->common.flags & IRCITUI_F_VISIBLE) && field->common.win &&
 *     field->common.kind == IRCITUI_EDIT_TEXT) {
 *     printf("typing goes to %s\n", field->common.id ? field->common.id : "(no id)");
 * }
 * @endcode
 */
PUBLIC_API IrcitUiNode *
IrcitUiFocusedNode(void);
/**
 * @brief Is @p node_ptr a node the keyboard may land on?
 *
 * @param node_ptr Node to test; NULL is not focusable.
 *
 * @return Non-zero if @p node_ptr is shown, enabled, unclipped, and takes input.
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
 * IrcitUiNode *candidate = IrcitUiFind(root, "menu_file");
 *
 * if (!candidate) {
 *   fprintf(stderr, "reference tree has no menu_file node\n");
 *   IrcitUiDeflate(root);
 *   endwin();
 *   return;
 * }
 *
 * IrcitUiLayout(root, 0, 0, COLS, LINES);
 * IrcitUiRealize(root);
 * if (IrcitUiIsFocusable(candidate))
 *   IrcitUiSetFocus(candidate);
 * else
 *   fputs("menu_file cannot take focus in this layout\n", stderr);
 * IrcitUiDeflate(root);
 * endwin();
 * @endcode
 */
PUBLIC_API int
IrcitUiIsFocusable(const IrcitUiNode *node_ptr);
/**
 * @brief Topmost stacked popup, the one a key press should reach first.
 *
 * @return Topmost IRCITUI_F_STACKED child of the screen root, or NULL if none is open.
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
 * IrcitUiNode *top;
 *
 * IrcitUiLayout(root, 0, 0, COLS, LINES);
 * IrcitUiRealize(root);
 * IrcitUiInflate(root, "file_menu");
 *
 * top = IrcitUiTopLayer();
 * if (!top) {
 *   fputs("no popup open\n", stderr);
 *   IrcitUiDeflate(root);
 *   endwin();
 *   return;
 * }
 * IrcitUiCloseTopPopup();   // tears down exactly this node
 * IrcitUiDeflate(root);
 * endwin();
 * @endcode
 */
PUBLIC_API IrcitUiNode *
IrcitUiTopLayer(void);
/**
 * @brief Move focus to the first focusable in @p layer_ptr, unless it is already there.
 *
 * @details Retention rule: a focusable @ref IrcitUiFocus already inside @p layer_ptr keeps focus,
 *          so an activation cannot drag it off the node the user chose.
 *
 * @param layer_ptr Subtree to resolve focus within; NULL is ignored.
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
 * IrcitUiNode *popup;
 *
 * IrcitUiLayout(root, 0, 0, COLS, LINES);
 * IrcitUiRealize(root);
 * popup = IrcitUiInflate(root, "file_menu");
 * if (!popup) {
 *   fputs("file_menu template not found\n", stderr);
 *   IrcitUiDeflate(root);
 *   endwin();
 *   return;
 * }
 *
 * IrcitUiFocusInLayer(popup);   // NULL would simply be ignored
 * IrcitUiDraw(root, NULL);
 * doupdate();
 * IrcitUiDeflate(root);
 * endwin();
 * @endcode
 */
PUBLIC_API void
IrcitUiFocusInLayer(IrcitUiNode *layer_ptr);
/**
 * @brief Re-resolve focus in the top popup if one is open, else in the main tree.
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
 * IrcitUiInflate(root, "tools_panel");
 *
 * IrcitUiFocusTop();        // focus follows the popup that just opened
 * IrcitUiDraw(root, NULL);
 * doupdate();
 * IrcitUiDeflate(root);
 * endwin();
 * @endcode
 */
PUBLIC_API void
IrcitUiFocusTop(void);
/**
 * @brief The @c on_inflate listener for a template that declares a focus request.
 *
 * @details Bind this to @c on_inflate in a popup template to have opening it move focus into it; a
 *          template that names nothing leaves focus where it was. It is a listener rather than a
 *          plain call because the template it is named in cannot make one: the node does not exist
 *          until the template has been cloned, so the request has to be recorded in the template
 *          and acted on when the clone is attached.
 *
 * @param disp_ptr The dispatcher the event was delivered through. Unused.
 * @param ev_ptr   The inflate event; its target is the popup that was just attached.
 * @param user_ptr The node's own interactions_user alias. Unused.
 *
 * @return 1, always. Moving focus is the whole of what this listener does, so
 *         there is nothing for the framework default beneath it to add.
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
 * IrcitUiWidgetDef win = {
 *   .id = "help_win", .kind = IRCITUI_FRAME,
 *   .text = "Help",
 *   .width_mode = IRCITUI_SIZE_EXACT, .width = 40,
 *   .height_mode = IRCITUI_SIZE_EXACT, .height = 12,
 *   .flags = IRCITUI_F_VISIBLE | IRCITUI_F_ENABLED | IRCITUI_F_BORDER | IRCITUI_F_STACKED,
 *   .fg = 7, .bg = 0, .bdr = 2
 * };
 * IrcitUiNode *popup;
 *
 * IrcitUiLayout(root, 0, 0, COLS, LINES);
 * IrcitUiRealize(root);
 * popup = IrcitUiCreateFloatingWindow(&win);
 * if (!popup) {
 *   fputs("out of memory building the popup\n", stderr);
 *   IrcitUiDeflate(root);
 *   endwin();
 *   return;
 * }
 *
 * // A popup built from a YAML template names
 * // `on_inflate: IrcitUiPopupFocusOnOpen` and gets this for free. This one
 * // was built in C from a WidgetDef, so the same move is made directly.
 * IrcitUiFocusInLayer(popup);
 * IrcitUiDraw(root, NULL);
 * doupdate();
 * IrcitUiDeflate(root);
 * endwin();
 * @endcode
 */
PUBLIC_API int
IrcitUiPopupFocusOnOpen(IrcitUiEventDispatcher *disp_ptr, IrcitUiEvent *ev_ptr, void *user_ptr);

/**
 * @brief May @p k own children?
 *
 * @param k Kind to test.
 *
 * @return Non-zero if @p k may own children.
 *
 * @code{.c}
 * IrcitUiWidgetDef def = {0};
 * IrcitUiNode     *node;
 *
 * if (!IrcitUiIsContainer(def.kind)) {
 *   fputs("kind cannot take children; IrcitUiAddChild would return NULL\n", stderr);
 *   return;
 * }
 *
 * IrcitUiDataBind();
 * IrcitUiScreenReset();
 * node = IrcitUiAddChild(IrcitUiScreen.root, &def);
 * if (!node)
 *   fputs("child rejected\n", stderr);
 * @endcode
 */
PUBLIC_API int
IrcitUiIsContainer(IrcitUiKind k);
/**
 * @brief Assign @p node_ptr a box and pack its children.
 *
 * @details Re-run on every resize, then @ref IrcitUiRealize, so the windows follow the new extents.
 *
 * @param node_ptr Node to place.
 *
 * @param x Origin column.
 *
 * @param y Origin row.
 *
 * @param w Width in cells; clamped to at least 1.
 *
 * @param h Height in cells; clamped to at least 1.
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
 * root = IrcitUiScreen.root;
 * IrcitUiLayout(root, 0, 0, COLS, LINES);
 * IrcitUiRealize(root);
 * IrcitUiDraw(root, NULL);
 * doupdate();
 *
 * key = getch();
 * if (key == KEY_RESIZE) {
 *   IrcitUiLayout(root, 0, 0, COLS, LINES);   // COLS/LINES are new now
 *   IrcitUiRealize(root);
 *   IrcitUiDraw(root, NULL);
 *   doupdate();
 * }
 * IrcitUiDeflate(root);
 * endwin();
 * @endcode
 */
PUBLIC_API void
IrcitUiLayout(IrcitUiNode *node_ptr, int x, int y, int w, int h);
/**
 * @brief Create or recreate curses windows for @p node_ptr and descendants.
 *
 * @details STACKED nodes get `newwin`; nested nodes get `derwin`. Safe to call again after a
 *          resize: each node's edit buffer survives, because the buffer outlives the window.
 *
 * @param node_ptr Subtree to realise.
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
 * IrcitUiDraw(root, NULL);
 * doupdate();
 *
 * // A resize must re-layout before it re-realises, or the windows are
 * // rebuilt at the old extents.
 * IrcitUiLayout(root, 0, 0, COLS, LINES);
 * IrcitUiRealize(root);
 * IrcitUiDeflate(root);
 * endwin();
 * @endcode
 */
PUBLIC_API void
IrcitUiRealize(IrcitUiNode *node_ptr);
/**
 * @brief Paint @p node_ptr then children. Does not call `doupdate()`.
 *
 * @details The caller owns the flush. Paint the whole tree rather than one field: `IrcitUiDraw`
 *          ends in `wnoutrefresh`, so refreshing a single window would copy its cells over a
 *          stacked popup's rectangle.
 *
 *          `dirty_list_ptr` is the frame's change set, and is **accepted, passed through to
 *          children, and not yet consulted**. When a reader lands it decides whether a node that
 *          is otherwise clean must still be refreshed -- the repair the paragraph above
 *          describes. Pass NULL for "no change set", which is what every caller does today.
 *
 * @param node_ptr Subtree to paint.
 * @param dirty_list_ptr The frame's change set, or NULL for none. See @ref IrcitUiDirtyList.
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
 * IrcitUiDraw(root, NULL);
 * doupdate();               // the flush IrcitUiDraw leaves to the caller
 * IrcitUiDeflate(root);
 * endwin();
 * @endcode
 */
PUBLIC_API void
IrcitUiDraw(IrcitUiNode *node_ptr, const IrcitUiDirtyList *dirty_list_ptr);
/**
 * @brief Mark @p node_ptr's paint stale, without touching its geometry.
 *
 * @details Use for a change that alters what a node looks like but not how much
 *          space it occupies: typed text in a fixed-width field, a checked
 *          toggle, a moved caret, a recoloured border. The node's own box and
 *          its ancestors' are untouched, so nothing is re-measured and nothing
 *          bubbles — the next @ref IrcitUiDraw repaints this node and no other.
 *
 *          Contrast @ref IrcitUiRequestLayout, which is for a change that can
 *          move or resize a box.
 *
 *          The node is also recorded in the frame's change set, so a clean node
 *          that shares cells with it is pushed again afterwards. That repair is
 *          what keeps stacking correct, and it is why a paint must go through
 *          here rather than by clearing the flag directly.
 *
 * @param node_ptr Node whose paint is stale. NULL is ignored.
 *
 * @code{.c}
 * IrcitUiNode *field;
 *
 * IrcitUiDataBind();
 * IrcitUiScreenReset();
 * field = IrcitUiFind(IrcitUiScreen.root, "cmdline");
 *
 * IrcitUiInvalidate(field);      // the text changed, the box did not
 * IrcitUiRenderFrame();
 * @endcode
 */
PUBLIC_API void
IrcitUiInvalidate(IrcitUiNode *node_ptr);
/**
 * @brief Mark @p node_ptr's paint stale, and the tree's geometry as needing a
 *        measure pass.
 *
 * @details Use for a change that can alter a box: mutating text a `wrap_content`
 *          label sizes itself from, folding a collapsible container, scrolling a
 *          viewport. The request is raised to the root as well as set here,
 *          because the sweep that resolves it is top-down and only the root can
 *          start one.
 *
 *          The named node alone has its paint invalidated, not its subtree. A
 *          move or a scroll shifts a subtree as a unit and its children's cells
 *          are still good; whether that survives is @ref IrcitUiRealize's call,
 *          and it clears what it actually rebuilds.
 *
 * @param node_ptr Node whose geometry may have moved. NULL is ignored.
 *
 * @code{.c}
 * IrcitUiNode *panel;
 *
 * IrcitUiDataBind();
 * IrcitUiScreenReset();
 * panel = IrcitUiFind(IrcitUiScreen.root, "side_panel");
 *
 * IrcitUiSetCollapsed(panel, 1);
 * IrcitUiRequestLayout(panel);   // the fold changed the boxes around it
 * IrcitUiRenderFrame();
 * @endcode
 */
PUBLIC_API void
IrcitUiRequestLayout(IrcitUiNode *node_ptr);
/**
 * @brief Run one frame: measure if the geometry moved, paint what is stale.
 *
 * @details The sequence is the one an application performs by hand — layout and
 *          realize when the tree is dirty, then a draw — plus the bookkeeping
 *          that makes it a frame: the change set is cleared when the pass ends,
 *          and the root's dirty flag with it.
 *
 *          **It does not flush.** Staging the virtual screen is this function's
 *          whole job; pushing it to the terminal is device I/O and belongs to
 *          whoever owns the terminal. Call @ref IrcitUiContextWindowDriver's
 *          `flush_hardware` after it, or `doupdate()` where the loop owns the
 *          terminal directly. Keeping the two apart is what lets a frame be
 *          driven with no terminal attached.
 *
 *          Safe to call when no tree exists: it does nothing.
 *
 * @code{.c}
 * IrcitUiDataBind();
 * IrcitUiScreenReset();
 * IrcitUiLayout(IrcitUiScreen.root, 0, 0, COLS, LINES);
 * IrcitUiRealize(IrcitUiScreen.root);
 *
 * for (;;) {
 *     IrcitUiRenderFrame();
 *     doupdate();                // the flush the frame leaves to the caller
 *     if (getch() == KEY_F(10)) break;
 * }
 * @endcode
 */
PUBLIC_API void
IrcitUiRenderFrame(void);
/**
 * @brief Fold or unfold a collapsible container.
 *
 * @details The node must carry `IRCITUI_F_COLLAPSIBLE`, which a layout file sets with `collapsible:
 *          true`. On any other node the call is a no-op, as is a call asking for the state the node
 *          is already in.
 *
 *          A container folds along the cross axis of its `orientation`: a horizontal one to a single
 *          row, a vertical one to a single column. Its children are hidden rather than resized, so
 *          they return at their own sizes when it unfolds. While folded the container draws no
 *          border and no padding - the strip is one cell - and paints its indicator glyph over a
 *          fill.
 *
 *          A layout request is raised and nothing more: the tree is **not** measured and its windows
 *          are **not** rebuilt by the time this returns. The frame that follows does that, once,
 *          for the whole tree -- see @ref IrcitUiRenderFrame. Painting is the caller's as with
 *          every other mutation here, and the `doupdate()` after it.
 *
 *          @ref IrcitUiScreenReset rebuilds the screen tree by cloning the reference, so it restores
 *          the declared initial state and discards a fold made from code. A resize does not, because
 *          it re-lays out the tree that is already there.
 *
 * @param node_ptr Container to fold or unfold. A node that is not collapsible is ignored.
 * @param collapsed Non-zero to fold, zero to unfold.
 *
 * @code{.c}
 * IrcitUiNode *root;
 * IrcitUiNode *panel;
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
 *
 * panel = IrcitUiFind(root, "side_panel");   // declared `collapsible: true`
 * IrcitUiSetCollapsed(panel, 1);             // folds, and re-lays out
 * IrcitUiDraw(root, NULL);
 * doupdate();
 * endwin();
 * @endcode
 */
PUBLIC_API void
IrcitUiSetCollapsed(IrcitUiNode *node_ptr, int collapsed);
/**
 * @brief Whether a collapsible container is folded.
 *
 * @details Answers 0 for a node that is not collapsible, so the two states stay distinguishable: a
 *          container that cannot fold is never reported as folded.
 *
 *          The state is per node, not per id. A clone carries whatever its reference held at the
 *          moment it was made, and the two then move independently.
 *
 * @param node_ptr Node to ask about.
 * @return Non-zero if folded, 0 otherwise.
 *
 * @code{.c}
 * IrcitUiNode *panel = IrcitUiFind(IrcitUiScreen.root, "side_panel");
 *
 * if (IrcitUiIsCollapsed(panel)) {
 *   IrcitUiSetCollapsed(panel, 0);           // unfold
 *   IrcitUiDraw(IrcitUiScreen.root, NULL);
 *   doupdate();
 * }
 * @endcode
 */
PUBLIC_API int
IrcitUiIsCollapsed(const IrcitUiNode *node_ptr);
/**
 * @brief `delwin` @p node_ptr and descendants; does not free the node.
 *
 * @details The nodes themselves survive, and so does the text they hold. To drop a popup and free
 *          it, use @ref IrcitUiPopStacked instead.
 *
 * @param node_ptr Subtree to tear down; must not be NULL.
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
 * IrcitUiDraw(root, NULL);
 * doupdate();
 *
 * IrcitUiDeflate(root);     // windows gone, nodes and text still there
 * endwin();
 * @endcode
 */
PUBLIC_API void
IrcitUiDeflate(IrcitUiNode *node_ptr);
/**
 * @brief Route one key or `KEY_MOUSE` through capture, hit-test, and focus.
 *
 * @details `ERR` is not a key: an idle blink tick hands back `ERR`, and this returns 0 for it
 *          rather than letting a toggle handler fire on it.
 *
 * @param node_ptr Subtree to dispatch into, normally the screen root.
 *
 * @param key Key returned by `getch()`, or `KEY_MOUSE`.
 *
 * @return Non-zero if consumed.
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
 * IrcitUiSetFocus(IrcitUiFind(root, "cmdline"));
 *
 * key = getch();
 * if (key != ERR && !IrcitUiDispatchKey(root, key))
 *   fputs("key not consumed by any node\n", stderr);
 * IrcitUiDraw(root, NULL);
 * doupdate();
 * IrcitUiDeflate(root);
 * endwin();
 * @endcode
 */
PUBLIC_API int
IrcitUiDispatchKey(IrcitUiNode *node_ptr, int key);
/**
 * @brief Depth-first search for an id under @p node_ptr.
 *
 * @param node_ptr Subtree to search; must not be NULL.
 *
 * @param id_ptr Id to match; must not be NULL.
 *
 * @return First node whose id matches, or NULL if there is none.
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
 *   fprintf(stderr, "%s\n", "no node with id cmdline");
 *   IrcitUiDeflate(root);
 *   endwin();
 *   return;
 * }
 *
 * IrcitUiLayout(root, 0, 0, COLS, LINES);
 * IrcitUiRealize(root);
 * IrcitUiSetFocus(field);
 * IrcitUiDeflate(root);
 * endwin();
 * @endcode
 */
PUBLIC_API IrcitUiNode *
IrcitUiFind(IrcitUiNode *node_ptr, const char *id_ptr);
/**
 * @brief The node an id names in the model the framework is driving.
 *
 * The tree @ref IrcitUiDataBind supplied, searched by id. A generated model carries an id index,
 * so this is a hash and a short probe; a model assembled without a generator carries none and is
 * walked instead. Either way the answer is the same node.
 *
 * This is the model, not what is on the glass: it holds nodes that are not displayed - the
 * template roots, which have no parent until a clone of one is attached. Use @ref IrcitUiFindBoth
 * when both trees are wanted, or @ref IrcitUiFindScreen for the displayed one alone.
 *
 * @param id_ptr Id to resolve; must not be NULL.
 *
 * @return The model node, or NULL if the model has no node with that id.
 */
PUBLIC_API IrcitUiNode *
IrcitUiFindModel(const char *id_ptr);
/**
 * @brief The node an id names on the glass.
 *
 * The live tree - @ref IrcitUiScreen - searched by identity rather than by text: the id is resolved
 * in the model once, and the live tree is then walked for the node cloned from it. No string is
 * compared during that walk, only node identity.
 *
 * An id that is not in the model - one created at run time - is searched by text instead, because
 * there is no model node to match against.
 *
 * The returned pointer dies when @ref IrcitUiScreenReset replaces the tree, or when the popup
 * holding it is popped. Take it, use it, and do not keep it across either.
 *
 * @param id_ptr Id to resolve; must not be NULL.
 *
 * @return The live node, or NULL if nothing with that id is displayed.
 */
PUBLIC_API IrcitUiNode *
IrcitUiFindScreen(const char *id_ptr);
/**
 * @brief The node an id names in both trees, in one call.
 *
 * One lookup instead of two when both answers are wanted, which is the usual case in an event
 * handler: the model node is what gets compared and configured, the live node is what gets drawn
 * and hit-tested. Each member is NULL for a genuine absence - see @ref IrcitUiNodePair for which
 * combination means what.
 *
 * @param id_ptr Id to resolve; must not be NULL.
 *
 * @return Both nodes; either or both may be NULL.
 */
PUBLIC_API IrcitUiNodePair
IrcitUiFindBoth(const char *id_ptr);
/**
 * @brief The model node a live node was cloned from.
 *
 * The other direction: given a node, reach its entry in the model. Every node in the live tree
 * carries the model node it came from, so this is a field read and not a search.
 *
 * NULL means the node has no model entry, which is the case for a node built at run time
 * (@ref IrcitUiAddChild, or the framework's own context menu) and also for a node that is itself
 * part of the model. The two are told apart by which tree the caller is holding: a model node is
 * its own entry, and a run-time node has none.
 *
 * @param node_ptr Node to map; NULL yields NULL.
 *
 * @return The model node, or NULL.
 */
PUBLIC_API const IrcitUiNode *
IrcitUiModelOf(const IrcitUiNode *node_ptr);
/**
 * @brief Enable mouse reports on stdout and in ncurses.
 *
 * @details Call once after `initscr`, before the first `getch`: the mask deliberately includes
 *          motion events, without which a press-and-drag can never extend a selection.
 *
 * @code{.c}
 * initscr();
 * cbreak();
 * noecho();
 * keypad(stdscr, TRUE);
 *
 * IrcitUiMouseInit();
 *
 * IrcitUiDataBind();
 * IrcitUiScreenReset();
 * IrcitUiLayout(IrcitUiScreen.root, 0, 0, COLS, LINES);
 * IrcitUiRealize(IrcitUiScreen.root);
 * IrcitUiDraw(IrcitUiScreen.root, NULL);
 * doupdate();
 *
 * fputs("\033[?1006l\033[?1003l\033[?1002l\033[?1000l", stdout);
 * fflush(stdout);                       // undo the terminal side
 * IrcitUiDeflate(IrcitUiScreen.root);
 * endwin();
 * @endcode
 */
PUBLIC_API void
IrcitUiMouseInit(void);
/**
 * @brief Deepest visible node containing (@p x,@p y).
 *
 * @param node_ptr Subtree to test, normally the screen root.
 *
 * @param y Row in screen cells.
 *
 * @param x Column in screen cells.
 *
 * @return Deepest matching node, or NULL if the point falls on none.
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
 * IrcitUiNode *hit;
 *
 * IrcitUiLayout(root, 0, 0, COLS, LINES);
 * IrcitUiRealize(root);
 *
 * hit = IrcitUiHitTest(root, LINES / 2, COLS / 2);
 * if (hit)
 *   IrcitUiSetFocus(hit);   // only if it is focusable; test first
 * else
 *   fputs("centre of the canvas is empty\n", stderr);
 * IrcitUiDeflate(root);
 * endwin();
 * @endcode
 */
PUBLIC_API IrcitUiNode *
IrcitUiHitTest(IrcitUiNode *node_ptr, int y, int x);
/**
 * @brief The library's own binder: hands over the generated display model.
 *
 * @details The reference model is complete before `main()` runs. It lives in a
 *          single object in `generated/ui_generated.c` whose pointers are
 *          resolved by the linker, so this constructs nothing and allocates
 *          nothing — and it records each node in the log as it returns, so a run
 *          says what it is working from.
 *
 *          **It is a binder, not an initialiser.** What it returns is a
 *          descriptor, and nothing reads a descriptor until the framework context
 *          holds one — which is what @ref IrcitUiInitialiseFrameworkContext
 *          installs. Pass this function to that call; do not call it on its own.
 *          Called on its own it returns the descriptor and installs nothing, and
 *          the next @ref IrcitUiScreenReset then clones through a NULL
 *          @ref IrcitUiModel.
 *
 *          The `NULL` below is the headless case. An application with a
 *          terminal passes its window registry in that argument instead.
 *
 * @code{.c}
 * IrcitUiNode *root;
 *
 * initscr();
 * cbreak();
 * noecho();
 * keypad(stdscr, TRUE);
 *
 * // The binder is INVOKED inside this call, and installing its result is what
 * // puts the model in the context.
 * if (!IrcitUiInitialiseFrameworkContext(IrcitUiDataBind, NULL)) {
 *   fputs("the framework context could not be initialised\n", stderr);
 *   endwin();
 *   return;
 * }
 *
 * IrcitUiScreenReset();
 * root = IrcitUiScreen.root;   // a clone of the reference model
 *
 * if (!IrcitUiModelRoot()) {
 *   fputs("the framework holds no reference model\n", stderr);
 *   endwin();
 *   return;
 * }
 *
 * IrcitUiDeflate(root);
 * endwin();
 * @endcode
 *
 * @return The generated model descriptor. Never NULL. Calling it more than once
 *         is harmless: it returns the same pointer.
 *
 * @see IrcitUiInitialiseFrameworkContext
 * @see IrcitUiModelRoot
 */
PUBLIC_API struct IrcitUiDataBindDescriptor *
IrcitUiDataBind(void);
/**
 * @brief Open the library's log stream.
 *
 * Records the library's own activity to @p file_ptr: every node as it is
 * constructed, every click and the node the hit-test resolved it to, and every
 * template as it is loaded. Nothing is logged until this call succeeds, and
 * nothing is logged again after @ref IrcitUiLogFini.
 *
 * The library never writes to the terminal. A TUI owns the screen, so a log
 * line on stdout or stderr would corrupt it; the sink is a file and only a
 * file, which is why @p file_ptr has no way to name a stream.
 *
 * @param level_ptr  Minimum level to record: @c debug, @c info, @c notice,
 *                   @c warn, @c error or @c fatal. A level records itself and
 *                   everything more severe, so @c debug records everything and
 *                   @c info omits @c debug. NULL or @c "" means @c info. An
 *                   unrecognised name fails the call rather than silently
 *                   falling back, so a typo on a command line is visible.
 *
 *                   @c nolog means record nothing. It is not a level — it
 *                   selects no rule and opens no file — and it SUCCEEDS, unlike
 *                   an unrecognised name. A caller that asked for silence has
 *                   not made a mistake, and @p file_ptr is not touched, so
 *                   naming a path alongside @c nolog is harmless and writes
 *                   nothing.
 * @param file_ptr   Path to write to, created if absent. NULL or @c "" means
 *                   @c ircituilib.log in the current directory. A relative path
 *                   is resolved against the working directory. The call fails
 *                   for a path containing a double quote or a newline, neither
 *                   of which can be represented in the underlying configuration
 *                   format.
 * @param retention  What becomes of whatever is already at @p file_ptr; see
 *                   @ref IrcitUiLogRetention. Applied once, before the stream is
 *                   opened, so it does not interact with the level: a run that
 *                   records nothing still archives or clears the file, because
 *                   the mode describes the file rather than the writing.
 *
 *                   The mode acts on @p file_ptr *after* the default above has
 *                   been resolved, so @ref IRCITUI_LOG_ARCHIVE with a NULL path
 *                   archives @c ircituilib.log. There is only ever one path
 *                   under consideration.
 *
 *                   Failing to carry out the mode fails the call rather than
 *                   falling back to @ref IRCITUI_LOG_APPEND: an application that
 *                   asked for a cleared log and silently got an appended one
 *                   has been misled, and the caller is told instead.
 *
 * @return 0 on success, non-zero if the level was not recognised, the path was
 *         rejected, the retention could not be applied, or the log stream could
 *         not be opened. A non-zero return is not fatal: the library runs with
 *         logging closed, and every subsequent log call is a no-op.
 *
 * @note Calling this twice is a no-op rather than an error; the second call
 *       returns 0 without disturbing the open stream, and without applying
 *       @p retention a second time.
 *
 * @code{.c}
 * // Record everything, from a clean file each run.
 * if (IrcitUiLogInit("debug", "ircituilib.log", IRCITUI_LOG_OVERWRITE) != 0)
 *   fputs("logging is off\n", stderr);
 *
 * IrcitUiDataBind();
 * IrcitUiScreenReset();
 *
 * // ... run the UI ...
 *
 * IrcitUiLogFini();
 * @endcode
 *
 * @see IrcitUiLogFini
 */
PUBLIC_API int
IrcitUiLogInit(const char *level_ptr, const char *file_ptr, IrcitUiLogRetention retention);
/**
 * @brief Close the library's log stream.
 *
 * Flushes and closes what @ref IrcitUiLogInit opened, and leaves the library
 * with logging off, so every subsequent log call is a no-op. Safe to call when
 * logging was never opened, and safe to call more than once.
 *
 * Call it after the last UI call that can log. A line emitted after this
 * returns is discarded rather than written to a stream being torn down.
 *
 * @code{.c}
 * IrcitUiDeflate(root);
 * IrcitUiLogFini();
 * endwin();
 * @endcode
 *
 * @see IrcitUiLogInit
 */
PUBLIC_API void
IrcitUiLogFini(void);
/**
 * @brief Whether the library is writing to its log stream.
 *
 * @details True between a successful @ref IrcitUiLogInit and @ref IrcitUiLogFini.
 *          The @c IRCITUI_LOG_* macros below test it before calling
 *          @ref IrcitUiLogEmit, so a line costs one load and one branch while
 *          logging is closed and the format arguments are never evaluated.
 *
 *          Framework-maintained, and written at exactly two points, both in
 *          ui_log.c. An application must not write it. A read-only accessor was
 *          the alternative and was rejected: the test is on the path of every
 *          log line in the library, and a call would be paid on each one.
 *
 *          @ref IrcitUiLogInit's @c nolog level returns success and leaves this
 *          false, which is how a caller asks for silence explicitly.
 */
extern int IrcitUiLogEnabled;
/**
 * @brief Write one line to the library's log stream.
 *
 * @details The engine behind the @c IRCITUI_LOG_* macros, and public for the
 *          same reason they are: an application that has opened a log with
 *          @ref IrcitUiLogInit needs a way to write to it. Call the macros rather
 *          than this function -- they fill in @p file_ptr, @p line and
 *          @p func_ptr, and a caller has no way to supply those by hand.
 *
 *          It takes the call site as parameters instead of deriving it, because
 *          zlog captures @c __FILE__, @c __func__ and @c __LINE__ at the macro
 *          that writes the line. A facade taking only a level and a format would
 *          stamp every line in this library with its own file, and what was
 *          replaced could not be recovered afterwards.
 *
 *          Be aware that the library's log format does not currently render
 *          them: its @c %F sits inside a @c %d(...) group, where zlog reads
 *          strftime specifiers, so it prints the date. No line names a source
 *          file today. The location is carried and not shown; @c %F, @c %U and
 *          @c %L are the specifiers that would show it.
 *
 *          A no-op while the library is not logging, so it is safe to call at
 *          any time, including after @ref IrcitUiLogFini.
 *
 * @param level    Severity to record the line at.
 * @param file_ptr The call site's file, i.e. @c __FILE__.
 * @param line     The call site's line, i.e. @c __LINE__.
 * @param func_ptr The call site's function, i.e. @c __func__.
 * @param fmt_ptr  A @c printf format string. Lines written by the library are
 *                 checked against it at compile time.
 *
 * @see IRCITUI_LOG_INFO
 */
PUBLIC_API void
IrcitUiLogEmit(IrcitUiLogLevel level, const char *file_ptr, int line, const char *func_ptr, const char *fmt_ptr, ...)
  __attribute__((format(printf, 5, 6)));
/** @brief Log at @ref IRCITUI_LOG_LEVEL_DEBUG, recorded only under a @c debug rule. */
#define IRCITUI_LOG_DEBUG(...) \
  do { if (IrcitUiLogEnabled) IrcitUiLogEmit(IRCITUI_LOG_LEVEL_DEBUG, __FILE__, __LINE__, __func__, __VA_ARGS__); } while (0)
/** @brief Log at @ref IRCITUI_LOG_LEVEL_INFO, the default rule level. */
#define IRCITUI_LOG_INFO(...) \
  do { if (IrcitUiLogEnabled) IrcitUiLogEmit(IRCITUI_LOG_LEVEL_INFO, __FILE__, __LINE__, __func__, __VA_ARGS__); } while (0)
/** @brief Log at @ref IRCITUI_LOG_LEVEL_NOTICE. */
#define IRCITUI_LOG_NOTICE(...) \
  do { if (IrcitUiLogEnabled) IrcitUiLogEmit(IRCITUI_LOG_LEVEL_NOTICE, __FILE__, __LINE__, __func__, __VA_ARGS__); } while (0)
/** @brief Log at @ref IRCITUI_LOG_LEVEL_WARN. */
#define IRCITUI_LOG_WARN(...) \
  do { if (IrcitUiLogEnabled) IrcitUiLogEmit(IRCITUI_LOG_LEVEL_WARN, __FILE__, __LINE__, __func__, __VA_ARGS__); } while (0)
/** @brief Log at @ref IRCITUI_LOG_LEVEL_ERROR. */
#define IRCITUI_LOG_ERROR(...) \
  do { if (IrcitUiLogEnabled) IrcitUiLogEmit(IRCITUI_LOG_LEVEL_ERROR, __FILE__, __LINE__, __func__, __VA_ARGS__); } while (0)
/**
 * @brief Default button: inflate its template, or pop its own popup closed.
 *
 * @param node_ptr The button node.
 *
 * @param key Key to apply; only `KEY_MOUSE`, Enter, and newline activate.
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
 *   .id = "btn_help", .text = "Help",
 *   .width_mode = IRCITUI_SIZE_MATCH, .height_mode = IRCITUI_SIZE_WRAP,
 *   .flags = IRCITUI_F_VISIBLE | IRCITUI_F_ENABLED | IRCITUI_F_CLICKABLE,
 *   .on_input = IrcitUiButtonInput,
 *   .fg = 7, .bg = 0
 * };
 * IrcitUiNode *btn;
 *
 * IrcitUiLayout(root, 0, 0, COLS, LINES);
 * IrcitUiRealize(root);
 * btn = IrcitUiAddChild(IrcitUiFind(root, "canvas"), &def);
 * if (!btn) {
 *   fputs("canvas is not a container\n", stderr);
 *   IrcitUiDeflate(root);
 *   endwin();
 *   return;
 * }
 *
 * IrcitUiButtonInput(btn, '\n');   // a bare letter would be ignored
 * IrcitUiDraw(root, NULL);
 * doupdate();
 * IrcitUiDeflate(root);
 * endwin();
 * @endcode
 */
PUBLIC_API void
IrcitUiButtonInput(IrcitUiNode *node_ptr, int key);
/**
 * @brief Default emoji cell click: append the clicked glyph via @ref IrcitUiInputAppend.
 *
 * @details The cell is derived from the coordinates of the last mouse event, so this only does
 *          anything inside a `KEY_MOUSE` dispatch.
 *
 * @param node_ptr The emoji_table node.
 *
 * @param key Key to apply; anything but `KEY_MOUSE` is ignored.
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
 * IrcitUiNode *grid;
 * int key;
 *
 * IrcitUiLayout(root, 0, 0, COLS, LINES);
 * IrcitUiRealize(root);
 * grid = IrcitUiAddEmojiTable(IrcitUiFind(root, "canvas"), NULL, 8);
 * if (!grid) {
 *   fputs("canvas is not a container\n", stderr);
 *   IrcitUiDeflate(root);
 *   endwin();
 *   return;
 * }
 *
 * key = getch();
 * if (key == KEY_MOUSE)
 *   IrcitUiEmojiInput(grid, key);
 * IrcitUiDraw(root, NULL);
 * doupdate();
 * IrcitUiDeflate(root);
 * endwin();
 * @endcode
 */
PUBLIC_API void
IrcitUiEmojiInput(IrcitUiNode *node_ptr, int key);
/**
 * @brief Destroy the last STACKED child of the screen root.
 *
 * @details A no-op when no popup is open. Equivalent to @ref IrcitUiTopLayer followed by @ref
 *          IrcitUiPopStacked.
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
 * IrcitUiInflateFloat(root, "tools_panel");
 *
 * IrcitUiCloseTopPopup();   // safe with nothing open
 * IrcitUiFocusTop();
 * IrcitUiDraw(root, NULL);
 * doupdate();
 * IrcitUiDeflate(root);
 * endwin();
 * @endcode
 */
PUBLIC_API void
IrcitUiCloseTopPopup(void);
/**
 * @brief Copy the selection, or the whole text, of @p node_ptr into the process clip.
 *
 * @details The clip is a single process-wide buffer, not the X or Wayland selection.
 *
 * @param node_ptr Node to copy from; NULL is ignored.
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
 *   fputs("no cmdline node\n", stderr);
 *   IrcitUiDeflate(root);
 *   endwin();
 *   return;
 * }
 *
 * IrcitUiLayout(root, 0, 0, COLS, LINES);
 * IrcitUiRealize(root);
 * IrcitUiClipCopy(field);    // whole text when nothing is selected
 * IrcitUiDeflate(root);
 * endwin();
 * @endcode
 */
PUBLIC_API void
IrcitUiClipCopy(IrcitUiNode *node_ptr);
/**
 * @brief Paste the clip into @p node_ptr, or into `cmdline`.
 *
 * @details Pastes into the node this call resolves rather than into whatever holds focus, so a
 *          context-menu paste cannot land in a different field.
 *
 * @param node_ptr Target edit_text; NULL, or a node of another kind, falls back to `cmdline`.
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
 * IrcitUiLayout(root, 0, 0, COLS, LINES);
 * IrcitUiRealize(root);
 * IrcitUiClipCopy(IrcitUiFind(root, "canvas"));
 * IrcitUiClipPaste(field);   // NULL would target cmdline anyway
 * IrcitUiDraw(root, NULL);
 * doupdate();
 * IrcitUiDeflate(root);
 * endwin();
 * @endcode
 */
PUBLIC_API void
IrcitUiClipPaste(IrcitUiNode *node_ptr);
/**
 * @brief Clear @p node_ptr's text and selection.
 *
 * @details An edit field comes back empty with its caret at position 0; other kinds only lose their
 *          selection.
 *
 * @param node_ptr Node to clear; NULL is ignored.
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
 * IrcitUiLayout(root, 0, 0, COLS, LINES);
 * IrcitUiRealize(root);
 * IrcitUiClipClear(field);
 * IrcitUiDraw(root, NULL);
 * doupdate();
 * IrcitUiDeflate(root);
 * endwin();
 * @endcode
 */
PUBLIC_API void
IrcitUiClipClear(IrcitUiNode *node_ptr);
/**
 * @brief Open a STACKED context menu next to the last mouse event.
 *
 * @details An already-open `ctx_menu` is found and reused rather than duplicated.
 *
 * @param target_ptr Node that was right-clicked.
 *
 * @return The menu node, or NULL if there is no screen root or no target.
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
 * IrcitUiNode *target = IrcitUiFind(root, "canvas");
 * IrcitUiNode *menu;
 * int key;
 *
 * IrcitUiLayout(root, 0, 0, COLS, LINES);
 * IrcitUiRealize(root);
 *
 * key = getch();
 * if (key != KEY_MOUSE) {
 *   IrcitUiDeflate(root);
 *   endwin();
 *   return;
 * }
 *
 * menu = IrcitUiShowContextMenu(target);
 * if (!menu) {
 *   fputs("no context menu: screen root or target missing\n", stderr);
 *   IrcitUiDeflate(root);
 *   endwin();
 *   return;
 * }
 * IrcitUiDraw(root, NULL);
 * doupdate();
 * IrcitUiPopStacked(menu);
 * IrcitUiDeflate(root);
 * endwin();
 * @endcode
 */
PUBLIC_API IrcitUiNode *
IrcitUiShowContextMenu(IrcitUiNode *target_ptr);
/**
 * @brief Fill the `canvas` text_view with many lines.
 *
 * @details A demo helper for the F6 scroll and selection example, not part of the widget contract.
 *          A no-op when the screen root or the `canvas` node is absent.
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
 * IrcitUiDemoFill();
 * IrcitUiDraw(root, NULL);
 * doupdate();
 * IrcitUiDeflate(root);
 * endwin();
 * @endcode
 */
PUBLIC_API void
IrcitUiDemoFill(void);
/**
 * @brief Paint an `image_view` with half-blocks or Braille.
 *
 * @details A no-op when the node has no window. The image is loaded on first use, so the first
 *          paint may be the one that reads the file.
 *
 * @param node_ptr The image_view node; must already be realised.
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
 *   .id = "photo", .kind = IRCITUI_IMAGE_VIEW, .text = "assets/logo.png",
 *   .width_mode = IRCITUI_SIZE_WRAP, .height_mode = IRCITUI_SIZE_WRAP,
 *   .flags = IRCITUI_F_VISIBLE | IRCITUI_F_ENABLED | IRCITUI_F_BORDER,
 *   .fg = 7, .bg = 0, .bdr = 2
 * };
 * IrcitUiNode *img;
 *
 * IrcitUiLayout(root, 0, 0, COLS, LINES);
 * IrcitUiRealize(root);
 * img = IrcitUiAddChild(IrcitUiFind(root, "canvas"), &def);
 * if (!img) {
 *   fputs("canvas is not a container\n", stderr);
 *   IrcitUiDeflate(root);
 *   endwin();
 *   return;
 * }
 *
 * IrcitUiImagePrepare(img);   // size it, then load and mark overflow
 * IrcitUiLayout(root, 0, 0, COLS, LINES);
 * IrcitUiRealize(root);
 * IrcitUiImageDraw(img);
 * doupdate();
 * IrcitUiDeflate(root);
 * endwin();
 * @endcode
 */
PUBLIC_API void
IrcitUiImageDraw(struct IrcitUiNode *node_ptr);
/**
 * @brief Load the image if needed, size the node, and mark SCROLLABLE on overflow.
 *
 * @details Call before @ref IrcitUiLayout: it is what writes the wrap_content width and height the
 *          layout pass then reads.
 *
 * @param node_ptr The image_view node.
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
 * IrcitUiNode *img = IrcitUiFind(root, "canvas");
 *
 * if (!img) {
 *   fputs("no canvas node\n", stderr);
 *   IrcitUiDeflate(root);
 *   endwin();
 *   return;
 * }
 *
 * IrcitUiImagePrepare(img);
 * IrcitUiLayout(root, 0, 0, COLS, LINES);
 * IrcitUiRealize(root);
 * IrcitUiImageDraw(img);
 * doupdate();
 * IrcitUiDeflate(root);
 * endwin();
 * @endcode
 */
PUBLIC_API void
IrcitUiImagePrepare(struct IrcitUiNode *node_ptr);
/**
 * @brief Content width in cells for `wrap_content`.
 *
 * @param node_ptr The image_view node.
 *
 * @return Width in cells, or a small default when the image is absent or not yet loaded.
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
 *   .id = "photo", .kind = IRCITUI_IMAGE_VIEW, .text = "assets/logo.png",
 *   .width_mode = IRCITUI_SIZE_WRAP, .height_mode = IRCITUI_SIZE_WRAP,
 *   .flags = IRCITUI_F_VISIBLE | IRCITUI_F_ENABLED,
 *   .fg = 7, .bg = 0
 * };
 * IrcitUiNode *img;
 * int w, h;
 *
 * IrcitUiDataBind();
 * IrcitUiScreenReset();
 * img = IrcitUiAddChild(IrcitUiFind(root, "canvas"), &def);
 * if (!img) {
 *   fputs("canvas is not a container\n", stderr);
 *   return;
 * }
 *
 * w = IrcitUiImageWrapW(img);
 * h = IrcitUiImageWrapH(img);
 * printf("image wants %dx%d cells\n", w, h);
 *
 * IrcitUiDeflate(root);
 * @endcode
 */
PUBLIC_API int
IrcitUiImageWrapW(struct IrcitUiNode *node_ptr);
/**
 * @brief Content height in cells for `wrap_content`.
 *
 * @param node_ptr The image_view node.
 *
 * @return Height in cells, or a small default when the image is absent or not yet loaded.
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
 * IrcitUiNode *img;
 * int h;
 *
 * IrcitUiLayout(root, 0, 0, COLS, LINES);
 * IrcitUiRealize(root);
 * img = IrcitUiFind(root, "canvas");
 * if (!img) {
 *   fputs("no canvas node\n", stderr);
 *   IrcitUiDeflate(root);
 *   endwin();
 *   return;
 * }
 *
 * h = IrcitUiImageWrapH(img);
 * printf("canvas would need %d rows\n", h);
 * IrcitUiDeflate(root);
 * endwin();
 * @endcode
 */
PUBLIC_API int
IrcitUiImageWrapH(struct IrcitUiNode *node_ptr);
/**
 * @brief Exclusive-select this radio inside its group.
 *
 * @details Clears IRCITUI_F_CHECKED on every sibling, so exactly one member of the parent group
 *          stays checked. A node with no parent is ignored.
 *
 * @param node_ptr The radio_button node.
 *
 * @param key Ignored; the operation is unconditional.
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
 * IrcitUiWidgetDef group = {
 *   .id = "mode", .kind = IRCITUI_RADIO_GROUP,
 *   .width_mode = IRCITUI_SIZE_MATCH, .height_mode = IRCITUI_SIZE_WRAP,
 *   .flags = IRCITUI_F_VISIBLE | IRCITUI_F_ENABLED,
 *   .fg = 7, .bg = 0
 * };
 * IrcitUiWidgetDef item = {
 *   .id = "mode_a", .kind = IRCITUI_RADIO_BUTTON, .text = "Mode A",
 *   .width_mode = IRCITUI_SIZE_MATCH, .height_mode = IRCITUI_SIZE_WRAP,
 *   .flags = IRCITUI_F_VISIBLE | IRCITUI_F_ENABLED | IRCITUI_F_CLICKABLE,
 *   .on_input = IrcitUiRadioItemInput,
 *   .fg = 7, .bg = 0
 * };
 * IrcitUiNode *g, *a;
 *
 * IrcitUiLayout(root, 0, 0, COLS, LINES);
 * IrcitUiRealize(root);
 * g = IrcitUiAddChild(IrcitUiFind(root, "canvas"), &group);
 * a = g ? IrcitUiAddChild(g, &item) : NULL;
 * if (!a) {
 *   fputs("radio group not built\n", stderr);
 *   IrcitUiDeflate(root);
 *   endwin();
 *   return;
 * }
 *
 * IrcitUiRadioItemInput(a, 0);
 * printf("selected index %d\n", IrcitUiGroupSelectedIndex(g));
 * IrcitUiDeflate(root);
 * endwin();
 * @endcode
 */
PUBLIC_API void
IrcitUiRadioItemInput(struct IrcitUiNode *node_ptr, int key);
/**
 * @brief Toggle this check box.
 *
 * @param node_ptr The check_box node; NULL is ignored.
 *
 * @param key Key to apply; `KEY_MOUSE` is ignored because dispatch has already toggled the flag.
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
 *   .id = "opt_bold", .kind = IRCITUI_CHECK_BOX, .text = "Bold",
 *   .width_mode = IRCITUI_SIZE_MATCH, .height_mode = IRCITUI_SIZE_WRAP,
 *   .flags = IRCITUI_F_VISIBLE | IRCITUI_F_ENABLED | IRCITUI_F_CLICKABLE,
 *   .on_input = IrcitUiCheckItemInput,
 *   .fg = 7, .bg = 0
 * };
 * IrcitUiNode *box;
 *
 * IrcitUiLayout(root, 0, 0, COLS, LINES);
 * IrcitUiRealize(root);
 * box = IrcitUiAddChild(IrcitUiFind(root, "canvas"), &def);
 * if (!box) {
 *   fputs("canvas is not a container\n", stderr);
 *   IrcitUiDeflate(root);
 *   endwin();
 *   return;
 * }
 *
 * IrcitUiCheckItemInput(box, ' ');    // toggles on
 * IrcitUiCheckItemInput(box, ' ');    // and back off
 * IrcitUiDeflate(root);
 * endwin();
 * @endcode
 */
PUBLIC_API void
IrcitUiCheckItemInput(struct IrcitUiNode *node_ptr, int key);
/**
 * @brief Which child of @p group_ptr is checked?
 *
 * @param group_ptr The group container; NULL yields -1.
 *
 * @return Index of the first CHECKED child, or -1 when none is.
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
 * IrcitUiNode *group = IrcitUiFind(root, "canvas");
 * int idx;
 *
 * IrcitUiLayout(root, 0, 0, COLS, LINES);
 * IrcitUiRealize(root);
 *
 * idx = IrcitUiGroupSelectedIndex(group);
 * if (idx < 0)
 *   fputs("nothing selected yet\n", stderr);
 * else
 *   printf("selected child %d\n", idx);
 * IrcitUiDeflate(root);
 * endwin();
 * @endcode
 */
PUBLIC_API int
IrcitUiGroupSelectedIndex(struct IrcitUiNode *group_ptr);
/**
 * @brief Caption of the checked child of @p group_ptr.
 *
 * @details Never NULL, so the result can be passed straight to `printf`.
 *
 * @param group_ptr The group container; NULL yields an empty string.
 *
 * @return Text of that child, or `""` when nothing is checked.
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
 * IrcitUiNode *group = IrcitUiFind(root, "canvas");
 * const char *value;
 *
 * IrcitUiLayout(root, 0, 0, COLS, LINES);
 * IrcitUiRealize(root);
 *
 * value = IrcitUiGroupSelectedValue(group);
 * printf("group value: %s\n", value);   // never NULL
 * IrcitUiDeflate(root);
 * endwin();
 * @endcode
 */
PUBLIC_API const char *
IrcitUiGroupSelectedValue(struct IrcitUiNode *group_ptr);
/**
 * @brief Collect the captions of every CHECKED child.
 *
 * @details The pointers stored are the nodes' own text, not copies: they stay valid only while
 *          those nodes live.
 *
 * @param group_ptr The group container.
 *
 * @param out_ptr Caller-owned array to fill; never written past @p maxn.
 *
 * @param maxn Capacity of @p out_ptr; fewer than 1 writes nothing.
 *
 * @return Number of captions written, at most @p maxn.
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
 * IrcitUiNode *group = IrcitUiFind(root, "canvas");
 * const char *chosen[8];
 * int n, i;
 *
 * IrcitUiLayout(root, 0, 0, COLS, LINES);
 * IrcitUiRealize(root);
 *
 * n = IrcitUiGroupSelectedValues(group, chosen, 8);
 * if (n == 0)
 *   fputs("nothing selected\n", stderr);
 * for (i = 0; i < n; i++)
 *   printf("selected: %s\n", chosen[i]);
 * IrcitUiDeflate(root);
 * endwin();
 * @endcode
 */
PUBLIC_API int
IrcitUiGroupSelectedValues(struct IrcitUiNode *group_ptr, const char **out_ptr, int maxn);

/**
 * @brief Append an `emoji_table` child to @p container_ptr.
 *
 * @param container_ptr Container to append to.
 *
 * @param emojis_ptr UTF-8 sequence of cells; NULL selects a built-in default.
 *
 * @param columns Grid width in cells; 0 or less selects the grid default.
 *
 * @return The new node, or NULL when the container cannot take children.
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
 * IrcitUiNode *grid;
 *
 * IrcitUiLayout(root, 0, 0, COLS, LINES);
 * IrcitUiRealize(root);
 * grid = IrcitUiAddEmojiTable(IrcitUiFind(root, "canvas"), "\xf0\x9f\x98\x80", 8);
 * if (!grid) {
 *   fputs("canvas is not a container\n", stderr);
 *   IrcitUiDeflate(root);
 *   endwin();
 *   return;
 * }
 * IrcitUiLayout(root, 0, 0, COLS, LINES);
 * IrcitUiRealize(root);
 * IrcitUiDeflate(root);
 * endwin();
 * @endcode
 */
PUBLIC_API IrcitUiNode *
IrcitUiAddEmojiTable(IrcitUiNode *container_ptr, const char *emojis_ptr, int columns);
/**
 * @brief Toggle the `emoji_picker` template, or build one in code.
 *
 * @details The NULL return is therefore not an error: it means the call closed the picker.
 *
 * @param anchor_ptr Node the picker is positioned against.
 *
 * @return The picker node when one was built, or NULL when an open one was closed instead.
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
 * IrcitUiNode *anchor = IrcitUiFind(root, "cmdline");
 * IrcitUiNode *picker;
 *
 * IrcitUiLayout(root, 0, 0, COLS, LINES);
 * IrcitUiRealize(root);
 *
 * picker = IrcitUiShowEmojiPicker(anchor);
 * if (!picker)
 *   puts("picker was open, so this call closed it");
 * IrcitUiDraw(root, NULL);
 * doupdate();
 * IrcitUiDeflate(root);
 * endwin();
 * @endcode
 */
PUBLIC_API IrcitUiNode *
IrcitUiShowEmojiPicker(IrcitUiNode *anchor_ptr);

/**
 * @brief Deep-copy @p ref_ptr.
 *
 * @details Windows are cleared, @ref IrcitUiNode::user is unbound, and the inline edit buffer is
 *          copied with the rest and re-aliased by @ref IrcitUiRealize. The copy is detached: it has
 *          no parent.
 *
 * @param ref_ptr Reference node to copy; NULL yields NULL.
 *
 * @return A fresh heap node, or NULL on allocation failure. The caller owns it.
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
 * IrcitUiNode *copy = IrcitUiClone(IrcitUiModelRoot());
 *
 * if (!copy) {
 *   fputs("clone failed: out of memory\n", stderr);
 *   endwin();
 *   return;
 * }
 *
 * IrcitUiLayout(copy, 0, 0, COLS, LINES);
 * IrcitUiRealize(copy);
 * IrcitUiDraw(copy, NULL);
 * doupdate();
 *
 * IrcitUiDeflate(copy);   // windows down...
 * IrcitUiDeflate(root);
 * endwin();
 * // ...but IrcitUiClone hands back a detached subtree, so nothing frees it
 * // here. Screen trees are freed by IrcitUiScreenReset.
 * @endcode
 */
PUBLIC_API IrcitUiNode *
IrcitUiClone(const IrcitUiNode *ref_ptr);
/**
 * @brief Replace the screen root with a fresh clone of the reference model.
 *
 * @details The previous screen tree and its popups are destroyed, so any node pointer held from
 *          before this call is dangling. @ref IrcitUiFocus is not reset for you: follow with @ref
 *          IrcitUiFocusTop.
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
 * IrcitUiInflateFloat(root, "tools_panel");
 *
 * IrcitUiScreenReset();          // popup and all edits are gone
 * root = IrcitUiScreen.root;     // re-read it; the old pointer is dead
 * IrcitUiFocusTop();
 * IrcitUiLayout(root, 0, 0, COLS, LINES);
 * IrcitUiRealize(root);
 * IrcitUiDeflate(root);
 * endwin();
 * @endcode
 */
PUBLIC_API void
IrcitUiScreenReset(void);
/**
 * @brief Clone template @p ref_id_ptr and attach it to @p parent_ptr as STACKED.
 *
 * @details A template flagged DRAGGABLE is routed to @ref IrcitUiInflateFloat; everything else to
 *          @ref IrcitUiInflateAnchored against @p parent_ptr.
 *
 * @param parent_ptr Node to attach to.
 *
 * @param ref_id_ptr Id of the template root in the reference model.
 *
 * @return The new node, or NULL when no template carries that id.
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
 * IrcitUiNode *popup;
 *
 * IrcitUiLayout(root, 0, 0, COLS, LINES);
 * IrcitUiRealize(root);
 *
 * popup = IrcitUiInflate(root, "file_menu");
 * if (!popup) {
 *   fputs("no template with id file_menu\n", stderr);
 *   IrcitUiDeflate(root);
 *   endwin();
 *   return;
 * }
 * IrcitUiFocusInLayer(popup);
 * IrcitUiDraw(root, NULL);
 * doupdate();
 * IrcitUiPopStacked(popup);   // teardown: detach, deflate, free
 * IrcitUiDeflate(root);
 * endwin();
 * @endcode
 */
PUBLIC_API IrcitUiNode *
IrcitUiInflate(IrcitUiNode *parent_ptr, const char *ref_id_ptr);
/**
 * @brief Inflate @p ref_id_ptr next to @p anchor_ptr, flipping on the right and bottom screen
 *        edges.
 *
 * @param parent_ptr Node to attach to.
 *
 * @param ref_id_ptr Id of the template root.
 *
 * @param anchor_ptr Node the popup is positioned against.
 *
 * @return The new node, or NULL when no template carries that id.
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
 * IrcitUiNode *anchor = IrcitUiFind(root, "menu_file");
 * IrcitUiNode *popup;
 *
 * IrcitUiLayout(root, 0, 0, COLS, LINES);
 * IrcitUiRealize(root);
 *
 * popup = IrcitUiInflateAnchored(root, "file_menu", anchor);
 * if (!popup) {
 *   fputs("no template with id file_menu\n", stderr);
 *   IrcitUiDeflate(root);
 *   endwin();
 *   return;
 * }
 * IrcitUiDraw(root, NULL);
 * doupdate();
 * IrcitUiPopStacked(popup);
 * IrcitUiDeflate(root);
 * endwin();
 * @endcode
 */
PUBLIC_API IrcitUiNode *
IrcitUiInflateAnchored(IrcitUiNode *parent_ptr, const char *ref_id_ptr, IrcitUiNode *anchor_ptr);
/**
 * @brief Inflate @p ref_id_ptr centred on the screen.
 *
 * @details Intended for draggable templates; pair with @ref IrcitUiDragTo.
 *
 * @param parent_ptr Node to attach to.
 *
 * @param ref_id_ptr Id of the template root.
 *
 * @return The new node, or NULL when no template carries that id.
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
 * IrcitUiNode *popup;
 *
 * IrcitUiLayout(root, 0, 0, COLS, LINES);
 * IrcitUiRealize(root);
 *
 * popup = IrcitUiInflateFloat(root, "tools_panel");
 * if (!popup) {
 *   fputs("no template with id tools_panel\n", stderr);
 *   IrcitUiDeflate(root);
 *   endwin();
 *   return;
 * }
 * IrcitUiDragTo(popup, 2, 2);
 * IrcitUiDraw(root, NULL);
 * doupdate();
 * IrcitUiPopStacked(popup);
 * IrcitUiDeflate(root);
 * endwin();
 * @endcode
 */
PUBLIC_API IrcitUiNode *
IrcitUiInflateFloat(IrcitUiNode *parent_ptr, const char *ref_id_ptr);
/**
 * @brief Move stacked node @p node_ptr so its origin is (@p x,@p y).
 *
 * @details Updates the tree, `mvwin`s the newwin, and re-realises the `derwin` children. A zero
 *          delta returns without doing any of it.
 *
 * @param node_ptr Node to move; NULL is ignored.
 *
 * @param x New origin column, clamped to the screen.
 *
 * @param y New origin row, clamped to the screen.
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
 * IrcitUiNode *popup;
 * int key;
 *
 * IrcitUiLayout(root, 0, 0, COLS, LINES);
 * IrcitUiRealize(root);
 * popup = IrcitUiInflateFloat(root, "tools_panel");
 * if (!popup) {
 *   fputs("no template with id tools_panel\n", stderr);
 *   IrcitUiDeflate(root);
 *   endwin();
 *   return;
 * }
 *
 * key = getch();
 * if (key == KEY_RIGHT)
 *   IrcitUiDragTo(popup, COLS / 4, LINES / 4);
 * IrcitUiDraw(root, NULL);
 * doupdate();
 * IrcitUiPopStacked(popup);
 * IrcitUiDeflate(root);
 * endwin();
 * @endcode
 */
PUBLIC_API void
IrcitUiDragTo(IrcitUiNode *node_ptr, int x, int y);
/**
 * @brief Validate the reference model and print ASCII and PPM previews.
 *
 * @details Writes no escape sequences, so it runs without a tty: it is the hook a `--verify-layout`
 *          command-line flag uses. Requires @ref IrcitUiDataBind to have run first, and a UTF-8
 *          locale for display-width measurement.
 *
 * @param cols Preview width in cells.
 *
 * @param rows Preview height in cells.
 *
 * @return 0 if structurally sound, non-zero if any problem was reported.
 *
 * @code{.c}
 * int cols = 80, rows = 24;   // from --cols= / --rows=
 * int status;
 *
 * setlocale(LC_ALL, "");   // display widths need a UTF-8 locale
 * IrcitUiDataBind();
 * status = IrcitUiVerifyLayout(cols, rows);
 * if (status != 0) {
 *   fprintf(stderr, "layout verification reported %d problem(s)\n", status);
 *   return;
 * }
 * puts("reference model is structurally sound");
 * @endcode
 */
PUBLIC_API int
IrcitUiVerifyLayout(int cols, int rows);

/**
 * @brief Heap a linear popup: STACKED, DRAGGABLE, centred.
 *
 * @details The definition's id, text, size modes, colours, and `on_input` are honoured; STACKED,
 *          DRAGGABLE, and visible are forced on.
 *
 * @param def_ptr Description of the window; NULL yields a zeroed default.
 *
 * @return The new node, or NULL when there is no screen root or allocation fails.
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
 * IrcitUiWidgetDef win = {
 *   .id = "code_win", .kind = IRCITUI_LINEAR, .text = "",
 *   .width_mode = IRCITUI_SIZE_EXACT, .width = 24,
 *   .height_mode = IRCITUI_SIZE_EXACT, .height = 6,
 *   .flags = IRCITUI_F_VISIBLE | IRCITUI_F_ENABLED | IRCITUI_F_BORDER | IRCITUI_F_DRAGGABLE,
 *   .fg = 7, .bg = 0, .bdr = 2
 * };
 * IrcitUiNode *win_node;
 *
 * IrcitUiLayout(root, 0, 0, COLS, LINES);
 * IrcitUiRealize(root);
 *
 * win_node = IrcitUiCreateFloatingWindow(&win);
 * if (!win_node) {
 *   fputs("no screen root, or out of memory\n", stderr);
 *   IrcitUiDeflate(root);
 *   endwin();
 *   return;
 * }
 * IrcitUiDraw(root, NULL);
 * doupdate();
 * IrcitUiPopStacked(win_node);
 * IrcitUiDeflate(root);
 * endwin();
 * @endcode
 */
PUBLIC_API IrcitUiNode *
IrcitUiCreateFloatingWindow(const IrcitUiWidgetDef *def_ptr);
/**
 * @brief Build a detached recycler node.
 *
 * @details A recycler is a virtualised list: it keeps a pool of row nodes, each
 *          cloned from @p template_id_ptr, and rebinds them to dataset positions
 *          as the viewport scrolls. A dataset of fifty thousand rows costs a
 *          pool the height of the viewport plus a small margin, not fifty
 *          thousand nodes.
 *
 *          The node is returned unattached and with no rows: the pool is built
 *          by the first layout pass, which is the only place that knows how tall
 *          the viewport is. Attach it with IrcitUiAddChild or sAttach's public
 *          counterpart, and fill in its adapter before the next pass.
 *
 *          THE STRINGS ARE BORROWED, NOT COPIED. Both @p id_ptr and
 *          @p template_id_ptr must outlive the node, which is the same contract
 *          IrcitUiWidgetDef::id carries.
 *
 * @param id_ptr Node id; must not be NULL. The key IrcitUiFind searches on.
 *
 * @param template_id_ptr Id of the row template to clone rows from; must not be
 *        NULL. This is a template id, not a path: the template is an orphan
 *        model node, resolved the way an inflated popup's is.
 *
 * @param row_height Rows each pooled row occupies. Values at or below zero are
 *        floored to one, because it is a divisor in the layout pass.
 *
 * @return The new node, or NULL when either string is NULL or on allocation
 *         failure.
 *
 * @code{.c}
 * IrcitUiNode *list = IrcitUiRecyclerCreate("log_list", "log_row", 1);
 * if (!list) return;
 * IrcitUiRecyclerState *st = list->common.widget_state;   // private header
 * st->adapter.item_count  = 10000;
 * st->adapter.user_dataset = rows;
 * st->adapter.on_bind_view = MyBinder;
 * IrcitUiAddChild(IrcitUiScreen.root, list);
 * @endcode
 */
PUBLIC_API IrcitUiNode *
IrcitUiRecyclerCreate(const char *id_ptr, const char *template_id_ptr, int row_height);
/**
 * @brief Append an already-built node to @p container_ptr.
 *
 * @details The counterpart of @ref IrcitUiAddChild for a node that was not built
 *          from an IrcitUiWidgetDef — @ref IrcitUiRecyclerCreate being the one
 *          constructor with a signature of its own.
 *
 *          A node that already has a parent is refused rather than moved: it sits
 *          in some other container's child array, and re-attaching it here would
 *          leave that array naming a node that is no longer its child, which the
 *          other parent would later free. Detach first, then attach.
 *
 *          A recycler is refused as @p container_ptr: its child array is its
 *          viewport, where entry i is row i, so an appended node would be placed
 *          as a row and handed to the binder. Its rows come from its
 *          item_template, and the recycler builds them itself.
 *
 * @param container_ptr Container to append to; must be a container kind, and not
 *                      a recycler.
 *
 * @param child_ptr Node to append; must not already have a parent.
 *
 * @return 1 on success, 0 when either argument is NULL, when @p container_ptr is
 *         not a container or is a recycler, when @p child_ptr already has a
 *         parent, or on allocation failure.
 *
 * @code{.c}
 * IrcitUiNode *list = IrcitUiRecyclerCreate("log_list", "log_row", 1);
 * if (list && !IrcitUiAddChildNode(IrcitUiScreen.root, list)) {
 *   // refused: the recycler is already placed, or the root is not a container
 * }
 * @endcode
 */
PUBLIC_API int
IrcitUiAddChildNode(IrcitUiNode *container_ptr, IrcitUiNode *child_ptr);
/**
 * @brief Feed a key to a recycler, scrolling its viewport.
 *
 * @details Handles @c KEY_DOWN and @c KEY_UP, moving the dataset position shown
 *          at the top of the viewport by one row. Every other key is refused.
 *
 *          **This is the one per-kind input function that returns a value**, and
 *          the difference is deliberate. @ref IrcitUiRadioItemInput,
 *          @ref IrcitUiCheckItemInput and @ref IrcitUiEditInput are @c void and
 *          answer the same way for every key they are offered; whether they
 *          should act is a predicate on the key alone, which the router can
 *          evaluate for itself. A scroll key is not like that: whether it should
 *          act depends on where the viewport already is. @c KEY_DOWN at the last
 *          screenful is not consumed, so it reaches whatever is behind the list
 *          instead of being swallowed. A predicate cannot express that, because
 *          at the point the router would ask it can see only the key.
 *
 *          The frame's per-kind matrix calls this and returns on a non-zero
 *          result, falling through to the focus tree on zero.
 *
 *          A recycler is focusable, so the keyboard reaches it by traversal or by
 *          a click inside it. A click on a row focuses the recycler rather than
 *          the row's label: a row is a pooled node that is rebound as the list
 *          scrolls, so a caret or a selection on it would name a dataset entry it
 *          is about to stop showing.
 *
 * @param node_ptr The recycler; a node of another kind is refused.
 *
 * @param key Key code to handle.
 *
 * @return 1 if the key was consumed and the viewport moved, 0 if it was not:
 *         wrong kind, no adapter, not a scroll key, or already at that end.
 *
 * @code{.c}
 * if (IrcitUiRecyclerInput(list, KEY_DOWN)) {
 *   // the viewport moved one row down
 * }
 * @endcode
 */
/**
 * @brief Install an application's adapter on a recycler.
 *
 * @details The whole of what an application needs to put data into a recycler:
 *          how many rows there are, a callback to fill one row for one position,
 *          and optionally a callback to be told what the user chose.
 *
 *          **Call it on the model node**, before the first @ref IrcitUiScreenReset.
 *          A screen tree is a clone of the model and the adapter is inherited from
 *          the node a clone came from, so configuring the model once is what makes
 *          every tree the framework builds from then on able to bind. A live node
 *          can be reconfigured too, and the next frame rebinds it.
 *
 *          The node's state is allocated here if it does not exist yet, so a
 *          generator-declared recycler needs nothing else: it already carries its
 *          item_template and row_height.
 *
 * @param node_ptr The recycler; another kind is refused.
 *
 * @param adapter_ptr The adapter to copy in; must not be NULL. It is copied, so
 *                    the caller's struct need not outlive the call.
 *
 * @return 1 on success; 0 if the node is not a recycler, @p adapter_ptr is NULL,
 *         or the state could not be allocated.
 *
 * @code{.c}
 * IrcitUiRecyclerAdapter adapter = {
 *   .item_count   = my_row_count,
 *   .on_bind_view = my_binder,
 *   .user_dataset = my_dataset,
 * };
 * IrcitUiRecyclerSetAdapter(IrcitUiFind(IrcitUiModelRoot(), "log_list"), &adapter);
 * @endcode
 */
PUBLIC_API int
IrcitUiRecyclerSetAdapter(IrcitUiNode *node_ptr, const IrcitUiRecyclerAdapter *adapter_ptr);
/**
 * @brief Give a recycler a view-type registry: rows of different shapes in one
 *        list.
 *
 * @details A recycler with no registry is **homogeneous** — one template, cloned
 *          for every row, which is what a layout that declares `item_template`
 *          gets and what the widget did before this existed. Registering types
 *          lets one dataset mix row shapes, and the application decides which
 *          position is which shape by installing
 *          @ref IrcitUiRecyclerAdapter::get_item_view_type alongside the
 *          adapter.
 *
 *          **The templates are layout; the type of each position is data.** That
 *          split is the whole contract: @p maps_ptr says what a type looks like,
 *          the adapter's discriminator says which type each row is, and the
 *          framework reads neither the layout file nor the dataset to join them.
 *
 *          What it costs: a pool slot whose shape does not match the position it
 *          is about to show is freed and re-cloned, so scrolling across a type
 *          boundary rebuilds the rows that crossed it. The pool's *size* is
 *          unchanged — it is still viewport height plus the cache margin — so
 *          the list stays virtual. Within one run of a single type, nothing is
 *          rebuilt and this is exactly the homogeneous path.
 *
 *          A position whose type is not in @p maps_ptr is left blank for that
 *          pass rather than being given another type's template: a row of the
 *          wrong shape is a wrong picture, and a gap is not.
 *
 *          @p maps_ptr is **borrowed** and must outlive the list. It is never
 *          copied and never freed, which is the same rule `item_template` and
 *          the collapse glyphs follow — a layout's registry is a static array in
 *          the generated model, and a C caller owns what it passes.
 *
 *          Install it on the **model** node, like the adapter and for the same
 *          reason: a screen tree is a clone of the model, so configuring the
 *          model once is what makes every tree the framework builds from then on
 *          answer to the same registry. Installing it on a live node also works
 *          and takes effect on the next layout pass.
 *
 *          A registry and `item_template` are mutually exclusive in a
 *          declaration (C21); this setter does not enforce that, because a node
 *          built through @ref IrcitUiWidgetDef has no declaration to check
 *          against. A registry wins where both are somehow present.
 *
 * @param node_ptr The recycler. Must be kind `IRCITUI_RECYCLER`.
 * @param maps_ptr The registry, @p count entries. NULL only when @p count is 0.
 * @param count    Entries. 0 restores the homogeneous behaviour.
 * @return 1 on success, 0 if the node is not a recycler or the state cannot be
 *         allocated.
 *
 * @code
 * static const IrcitUiViewTypeMapping s_types[] = {
 *   { LOG_INFO,  "log_row"      },
 *   { LOG_WARN,  "log_row_warn" },
 * };
 * IrcitUiRecyclerSetViewTypes(IrcitUiFind(IrcitUiModelRoot(), "log_list"), s_types, 2);
 * @endcode
 */
PUBLIC_API int
IrcitUiRecyclerSetViewTypes(IrcitUiNode *node_ptr, const IrcitUiViewTypeMapping *maps_ptr, int count);
/**
 * @brief Pin a recycler's section headers: the rows of one view type also stay
 *        drawn at the top of the viewport as the list scrolls under them.
 *
 * @details The C counterpart of a layout's `sticky_headers` key (C22). Naming a
 *          type makes **that type's rows section dividers** — drawn in the flow
 *          of the list like any other row — and the one covering the top of the
 *          viewport is cloned a second time and pinned there, over the rows
 *          scrolling beneath it.
 *
 *          The pinned header is a clone of **the divider's own template**, so the
 *          two cannot look different, and the in-flow divider is not displaced:
 *          you will see it at its own place in the list and again at the top.
 *
 *          **The registry must be installed first.** The pin names a type the
 *          registry declares, so @ref IrcitUiRecyclerSetViewTypes has to have run
 *          — a value the registry does not hold is refused rather than accepted
 *          and then silently blank. Pass a negative @p type_id to turn pinning
 *          off; that is always accepted.
 *
 *          Install it on the **model** node, like the adapter and the registry
 *          and for the same reason: a screen tree is a clone of the model.
 *
 * @param node_ptr The recycler. Must be kind `IRCITUI_RECYCLER`.
 * @param type_id  The `type_id` of the divider rows, or negative for none.
 * @return 1 on success; 0 if the node is not a recycler, the state cannot be
 *         allocated, or @p type_id is non-negative and no registry entry declares
 *         it.
 *
 * @code
 * static const IrcitUiViewTypeMapping s_types[] = {
 *   { LOG_INFO,    "log_row"         },
 *   { LOG_SECTION, "log_row_section" },
 * };
 * IrcitUiNode *list = IrcitUiFind(IrcitUiModelRoot(), "log_list");
 * IrcitUiRecyclerSetViewTypes(list, s_types, 2);
 * IrcitUiRecyclerSetStickyHeader(list, LOG_SECTION);   // after the registry
 * @endcode
 *
 * @note There is no way to style the pinned copy differently from the divider
 *       row, and no way to tell them apart in a binder. Both are handed to
 *       @ref IrcitUiRecyclerAdapter::on_bind_view with the same node shape and,
 *       when the divider is on screen, the same position.
 */
PUBLIC_API int
IrcitUiRecyclerSetStickyHeader(IrcitUiNode *node_ptr, int type_id);
/**
 * @brief Name the row type an editing row takes, enabling inline editing.
 *
 * @details A row of the list becomes a text field the user types into. The
 *          mechanism is entirely the row's **shape**: the row at the editing
 *          position is a clone of @p type_id's template instead of the shape the
 *          application's `get_item_view_type` asks for, so the layout pass rebuilds
 *          that one pool slot into a field — with a fresh text buffer — and
 *          rebuilding it back when the edit ends.
 *
 *          **@p type_id names a template in the recycler's registry**, so
 *          @ref IrcitUiRecyclerSetViewTypes has to have run first, and the layout
 *          has to declare that entry. A type the registry does not hold is
 *          refused. The template it names must be an `edit_text`: it is the field
 *          the user types into, and the row's own height is what the field gets,
 *          so a bordered container would have no room for its border.
 *
 *          Install it on the **model** node, like the adapter and the registry.
 *
 *          **There is no layout key for this**, and that is a deliberate split:
 *          the *type* is a declaration's business and the registry already carries
 *          it, while *when a row is being edited* is a run-time fact that arrives
 *          through @ref IrcitUiRecyclerBeginEdit. Compare
 *          @ref IrcitUiRecyclerSetSelectionMode, which is C-API-only for the same
 *          reason.
 *
 * @param node_ptr The recycler. Must be kind `IRCITUI_RECYCLER`.
 *
 * @param type_id  The registry `type_id` of the editing template, or a negative
 *                 value to turn inline editing off.
 *
 * @return 1 on success; 0 if the node is not a recycler, the state cannot be
 *         allocated, or @p type_id is non-negative and no registry entry declares
 *         it.
 *
 * @note An edit in flight is committed by this call, not abandoned: it would
 *       otherwise be rebuilt under the row the user is typing into.
 */
PUBLIC_API int
IrcitUiRecyclerSetEditType(IrcitUiNode *node_ptr, int type_id);
/**
 * @brief Open the row at @p index for inline editing.
 *
 * @details The row becomes a text field, the focus moves to it, and the viewport
 *          scrolls to it if it was not already on screen. The framework then
 *          routes the keyboard to the field — printable characters, backspace and
 *          left/right — while the list itself is frozen: up and down do not scroll
 *          it, and a click on another row commits rather than marking.
 *
 *          **Two gestures open a row, and both belong to the framework** rather
 *          than to the application: a **double left-click** on it, and
 *          **Ctrl+Enter** on the cursor's row. The first click of a double-click
 *          selects the row as any click does — a double-click is not a selection
 *          gesture — and the second opens it. An application that wants a key of
 *          its own binds @ref IrcitUiRecyclerCursor and this call; that is all the
 *          demo's old F11 arm did.
 *
 * @note Ctrl+Enter is only deliverable when the terminal is configured to send a
 *       sequence for it — `XTerm*modifyOtherKeys: 2`, or a terminal speaking the
 *       CSI-u protocol. A default terminal sends a bare CR for it, which is
 *       indistinguishable from Enter and cannot be bound. So an application that
 *       relies on this gesture has no keyboard fallback on such a terminal; the
 *       double-click always works.
 *
 * @details The edit ends in one of three ways, and the application is told about
 *          exactly one of them:
 *
 *          | Ending | Result |
 *          |---|---|
 *          | `Enter` or `Ctrl+Enter` | commit — `on_item_edited` is called, the row reverts |
 *          | `Esc` | cancel — nothing is reported, the row reverts |
 *          | a click away | commit, same as `Enter` |
 *
 *          @ref IrcitUiRecyclerCommitEdit and @ref IrcitUiRecyclerCancelEdit do
 *          the same two things programmatically, and @ref IrcitUiRecyclerIsEditing
 *          asks whether a row is open.
 *
 *          While a row is open, `Enter` commits rather than activating the row: it
 *          does not reach `on_item_clicked`. That is deliberate — an edited row is
 *          not a chosen one — and it is the one place a list's Enter means
 *          something other than "choose this".
 *
 *          **A second `BeginEdit` commits the first.** Starting to edit row B
 *          while row A is open carries A's text home rather than dropping it.
 *
 *          The dataset is never written by the framework: the text lives in the
 *          row's buffer until the commit hands it to `on_item_edited`, which is
 *          the application's only chance to store it — the row it came from is
 *          freed by the layout pass that follows.
 *
 * @param node_ptr The recycler. Must be kind `IRCITUI_RECYCLER`.
 *
 * @param index Dataset index, which must be below the adapter's `item_count`.
 *
 * @return 1 if the row is now open for editing; 0 if the node is not a recycler,
 *         @p index is outside the dataset, no editing type has been set, that type
 *         has no resolvable template, or the adapter has no `on_item_edited` — a
 *         field whose result nobody receives is a user typing into a void.
 *
 * @code{.c}
 * IrcitUiRecyclerSetViewTypes(list, s_types, 3);   // includes the edit template
 * IrcitUiRecyclerSetEditType(list, LOG_EDIT);      // after the registry
 * IrcitUiRecyclerBeginEdit(list, 42);
 * @endcode
 *
 * @note The value being edited has to be put into the field by the binder. The
 *       framework calls `on_bind_view` for the row **once**, on the pass that
 *       builds the field, and never again while the edit is open — so the
 *       application fills it there, and it must not be re-filled on later passes
 *       or the user's typing is overwritten. A row being edited has
 *       `IrcitUiNodeGetKind(row) == IRCITUI_EDIT_TEXT`, which is how a binder tells it
 *       apart from a display row; fill it with
 *       @ref IrcitUiEditTextSetValue rather than @ref IrcitUiNodeSetText, which
 *       cannot reach a field's own buffer.
 */
PUBLIC_API int
IrcitUiRecyclerBeginEdit(IrcitUiNode *node_ptr, size_t index);
/**
 * @brief Finish an edit, handing the typed text to `on_item_edited`.
 *
 * @param node_ptr The recycler; another kind is refused.
 * @return 1 if an edit was open and is now committed; 0 if there was none.
 */
PUBLIC_API int
IrcitUiRecyclerCommitEdit(IrcitUiNode *node_ptr);
/**
 * @brief Abandon an edit, discarding the typed text.
 *
 * @details Nothing is reported to the application, which is what makes `Esc` mean
 *          "forget it": a cancel and a field that was never opened are
 *          indistinguishable from the dataset's point of view.
 *
 * @param node_ptr The recycler; another kind is refused.
 * @return 1 if an edit was open and is now abandoned; 0 if there was none.
 */
PUBLIC_API int
IrcitUiRecyclerCancelEdit(IrcitUiNode *node_ptr);
/**
 * @brief Whether a row of this recycler is open for editing.
 *
 * @param node_ptr The recycler; another kind answers 0.
 * @return 1 while an edit is open, 0 otherwise.
 */
PUBLIC_API int
IrcitUiRecyclerIsEditing(IrcitUiNode *node_ptr);
PUBLIC_API int
IrcitUiRecyclerInput(IrcitUiNode *node_ptr, int key);
/**
 * @brief Select or deselect one dataset row of a recycler.
 *
 * @details The selection is a bit-array keyed by **dataset index**, held in the
 *          recycler's state and never on a row node. That is the point rather
 *          than an implementation detail: rows are pooled and rebound as the
 *          list scrolls, so a mark kept on a row would follow the pool slot and
 *          appear on a different entry after any scroll. A marked row may also be
 *          scrolled out of sight — the selection set is not confined to the
 *          viewport, only the cursor is.
 *
 *          The bit-array is sized to the dataset on first use, so this allocates.
 *          It is the one part of a recycler that is not virtual: 1.25 KB at ten
 *          thousand rows, 6.25 KB at fifty thousand.
 *
 * @param node_ptr The recycler; another kind is refused.
 *
 * @param index Dataset index, which must be below the adapter's `item_count`.
 *
 * @param selected Non-zero to select, zero to deselect.
 *
 * @return 1 if the bit changed, 0 if it did not — wrong kind, no adapter, an
 *         index outside the dataset, allocation failure, or a request that
 *         matched the state it was already in.
 *
 * @code{.c}
 * IrcitUiRecyclerSelect(list, 42, 1);
 * if (IrcitUiRecyclerIsSelected(list, 42)) { ... }
 * @endcode
 */
PUBLIC_API int
IrcitUiRecyclerSelect(IrcitUiNode *node_ptr, size_t index, int selected);
/**
 * @brief Whether a dataset row of a recycler is selected.
 *
 * @param node_ptr The recycler; another kind answers 0.
 *
 * @param index Dataset index.
 *
 * @return 1 if the bit is set, 0 otherwise — including for an index outside the
 *         dataset, which names no row and so can never be selected.
 */
PUBLIC_API int
IrcitUiRecyclerSelectRange(IrcitUiNode *node_ptr, size_t start, size_t end, int selected);
PUBLIC_API int
IrcitUiRecyclerIsSelected(IrcitUiNode *node_ptr, size_t index);
/**
 * @brief Deselect every row, keeping the buffer for the next selection.
 *
 * @param node_ptr The recycler; another kind is ignored.
 */
PUBLIC_API void
IrcitUiRecyclerClearSelection(IrcitUiNode *node_ptr);
/**
 * @brief Copy the selected dataset indices into a caller's buffer, ascending.
 *
 * @details The snapshot surface: a caller that wants to act on the selection
 *          takes it in one pass rather than querying index by index, so the set
 *          cannot change underneath it. Ascending, so the answer does not depend
 *          on word order.
 *
 *          **Call it on the thread that owns the tree.** This is a snapshot of
 *          the selection, not a thread-safe reader of it: it dereferences the
 *          live bit-array with no lock, so a concurrent mutation from Thread 0 —
 *          a selection, a clear, a resize that reallocates the array, or
 *          @ref IrcitUiNodeDestroy — races it. It is the BUFFER that goes to a
 *          worker, and that is the whole of §17's off-thread design: extract
 *          here, hand the copy over. A worker calling this directly is reading
 *          memory Thread 0 may be reallocating or freeing.
 *
 *          This call allocates nothing and transfers nothing: the buffer is
 *          yours. That is where it differs from the allocate-and-hand-over shape
 *          §17.2 sketches, and it is why there is no separate size query — pass
 *          a buffer and read the return value.
 *
 * @param node_ptr The recycler; another kind writes nothing.
 *
 * @param out_ptr Buffer to fill; must not be NULL.
 *
 * @param maxn Capacity of @p out_ptr in indices.
 *
 * @return How many indices were written. A return equal to @p maxn means there
 *         may be more; call again with a larger buffer to find out.
 */
PUBLIC_API size_t
IrcitUiGetSelectedIndices(IrcitUiNode *node_ptr, size_t *out_ptr, size_t maxn);
/**
 * @brief The pooled row node showing a dataset index, or NULL.
 *
 * @details A recycler holds a viewport's worth of row nodes and rebinds them as
 *          it scrolls, so a dataset index has a node only while it is inside the
 *          pool — the viewport plus its small cache margin, so the two rows just
 *          past the visible edge are answered too, clipped rather than absent.
 *          NULL is the honest answer for one outside the pool; it does not mean
 *          the index is invalid.
 *
 *          This is how an application reads back a value its own binder wrote,
 *          without keeping a parallel index of its own. The node is borrowed:
 *          **do not retain it**, because the next scroll rebinds it to a
 *          different dataset position. Prefer @ref IrcitUiGetSelectedIndices and
 *          the adapter's @c on_item_clicked, which hand over the stable index.
 *
 * @param node_ptr The recycler; another kind answers NULL.
 *
 * @param index Dataset index.
 *
 * @return The row node now bound to @p index, or NULL if that row is not
 *         currently realised, the index is outside the dataset, or the node is
 *         not a recycler.
 *
 * @code{.c}
 * IrcitUiNode *row  = IrcitUiRecyclerRowAt(list, IrcitUiRecyclerCursor(list));
 * IrcitUiNode *text = row ? IrcitUiFind(row, "row_text_line") : NULL;
 * if (text) IrcitUiInputAppend(text->common.text);   // borrowed, do not free
 * @endcode
 */
PUBLIC_API IrcitUiNode *
IrcitUiRecyclerRowAt(IrcitUiNode *node_ptr, size_t index);
/**
 * @brief Set how many rows of a recycler may be marked at once.
 *
 * @details Defaults to @ref IRCITUI_SELECT_SINGLE — marking a row clears the
 *          rest. Multi-select is what SS17 needs to snapshot and is opted into
 *          with @ref IRCITUI_SELECT_MULTI. The mode is enforced in
 *          @ref IrcitUiRecyclerSelect, which both a click and @c SPACE go
 *          through, so the two cannot disagree about it.
 *
 *          Narrowing a mode narrows the set that is already there: switching to
 *          @c SINGLE keeps the cursor's row if it was marked and clears the rest;
 *          @c NONE clears all of it. Widening changes nothing.
 *
 *          Set it on the **model** node to have every cloned screen tree inherit
 *          it, the way the adapter and the highlight are configured; this call on
 *          a live node changes only that instance.
 *
 * @param node_ptr The recycler; another kind is ignored.
 *
 * @param mode The wanted mode.
 */
PUBLIC_API void
IrcitUiRecyclerSetSelectionMode(IrcitUiNode *node_ptr, IrcitUiSelectionMode mode);
/**
 * @brief The recycler's current selection mode.
 *
 * @param node_ptr The recycler; another kind answers @ref IRCITUI_SELECT_NONE,
 *                 which is the answer that refuses to change anything.
 *
 * @return The mode in force.
 */
PUBLIC_API IrcitUiSelectionMode
IrcitUiRecyclerSelectionMode(IrcitUiNode *node_ptr);
/**
 * @brief The dataset index the keyboard acts on, i.e. the cursor row.
 *
 * @param node_ptr The recycler; another kind answers 0.
 *
 * @return The cursor's dataset index, 0 before anything has moved it.
 */
PUBLIC_API size_t
IrcitUiRecyclerCursor(IrcitUiNode *node_ptr);
/**
 * @brief Move the cursor to a dataset index, clamped to the dataset.
 *
 * @param node_ptr The recycler; another kind is ignored.
 *
 * @param index Wanted index; one at or past `item_count` lands on the last row.
 */
PUBLIC_API void
IrcitUiRecyclerSetCursor(IrcitUiNode *node_ptr, size_t index);
/**
 * @brief Append a child built from @p def_ptr.
 *
 * @details On a failed attach the freshly built child is freed before returning, so a NULL return
 *          leaks nothing.
 *
 * @param container_ptr Container to append to; must not be a recycler, whose
 *                      child array is its viewport and whose rows it builds
 *                      itself, from its item_template.
 *
 * @param def_ptr Description of the child; must not be NULL.
 *
 * @return The new node, or NULL when @p container_ptr is not a container or is a
 *         recycler, when @p def_ptr is NULL, or on allocation failure.
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
 *   .id = "label", .kind = IRCITUI_TEXT_VIEW, .text = "from C API",
 *   .width_mode = IRCITUI_SIZE_MATCH, .height_mode = IRCITUI_SIZE_WRAP,
 *   .flags = IRCITUI_F_VISIBLE | IRCITUI_F_ENABLED,
 *   .fg = 2, .bg = 0
 * };
 * IrcitUiNode *child;
 *
 * IrcitUiLayout(root, 0, 0, COLS, LINES);
 * IrcitUiRealize(root);
 *
 * child = IrcitUiAddChild(IrcitUiFind(root, "canvas"), &def);
 * if (!child) {
 *   fputs("canvas is not a container, or out of memory\n", stderr);
 *   IrcitUiDeflate(root);
 *   endwin();
 *   return;
 * }
 * IrcitUiLayout(root, 0, 0, COLS, LINES);
 * IrcitUiRealize(root);
 * IrcitUiDeflate(root);
 * endwin();
 * @endcode
 */
PUBLIC_API IrcitUiNode *
IrcitUiAddChild(IrcitUiNode *container_ptr, const IrcitUiWidgetDef *def_ptr);
/**
 * @brief Build a node from a definition, with NO parent.
 *
 * @details THE ONE CONSTRUCTOR THAT NEEDS NO TREE. Every other way a node is born
 *          in this framework attaches it to something: @ref IrcitUiAddChild needs
 *          a container, @ref IrcitUiInflate and @ref IrcitUiCreateFloatingWindow
 *          need a screen root, and @ref IrcitUiRecyclerCreate is recycler-shaped.
 *          That makes a tree impossible to build from nothing -- which is what an
 *          application supplying its own model through the binder
 *          (@ref IrcitUiInitialiseFrameworkContext) has to do, because the model
 *          it hands over has to exist before the framework has a root at all.
 *
 *          It is the same construction @ref IrcitUiAddChild uses internally, so
 *          the node is fully initialised from @p def_ptr: dimensions, colours,
 *          theme class, orientation, caption, and the per-kind state the engine
 *          allocates. Nothing is left for the caller to finish.
 *
 *          **The returned node borrows @p def_ptr's strings**, exactly as
 *          @ref IrcitUiAddChild's do: @c id and @c text must outlive the node, and
 *          a stack buffer is a use-after-scope. Every id in a C-built tree is a
 *          file-scope literal for that reason.
 *
 * @param def_ptr Definition to build from; its strings are borrowed, not copied.
 *
 * @return The new node, or NULL when @p def_ptr is NULL or allocation fails.
 *
 * @note THE CALLER OWNS IT, AND THAT IS A REAL OBLIGATION. The node is not in any
 *       tree, so nothing else will ever free it. Attach it with
 *       @ref IrcitUiAddChildNode, or make it the root of a model a binder returns,
 *       or it leaks. There is deliberately no separate destructor to pair with it:
 *       a node's lifetime in this framework is its tree's, and adding a second
 *       owner would mean a node could be freed twice.
 *
 * @code{.c}
 * static IrcitUiWidgetDef s_root_def = {
 *   .id = "chat_root", .kind = IRCITUI_CONSTRAINT,
 *   .width_mode = IRCITUI_SIZE_MATCH, .height_mode = IRCITUI_SIZE_MATCH,
 *   .flags = IRCITUI_F_VISIBLE | IRCITUI_F_ENABLED
 * };
 *
 * static IrcitUiDataBindDescriptor s_desc;
 *
 * static IrcitUiDataBindDescriptor *MyBinder(void) {
 *   IrcitUiNode *root = IrcitUiNodeFromDef(&s_root_def);
 *   if (root) IrcitUiAddChild(root, &s_child_def);   // and so on
 *   s_desc.root = root;
 *   return &s_desc;
 * }
 * @endcode
 */
PUBLIC_API IrcitUiNode *
IrcitUiNodeFromDef(const IrcitUiWidgetDef *def_ptr);
/**
 * @brief @ref IrcitUiAddChild, then force BUTTON kind and a clickable default.
 *
 * @details Sets kind to BUTTON, adds VISIBLE, ENABLED and CLICKABLE, and binds @ref
 *          IrcitUiButtonInput where @p def_ptr named no `on_input`.
 *
 * @param container_ptr Container to append to.
 *
 * @param def_ptr Description of the caption; may be NULL for a zeroed default.
 *
 * @return The new node, or NULL when @p container_ptr is not a container or allocation fails.
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
 *   .id = "btn_close", .text = "Close",
 *   .width_mode = IRCITUI_SIZE_MATCH, .height_mode = IRCITUI_SIZE_WRAP
 * };
 * IrcitUiNode *btn;
 *
 * IrcitUiLayout(root, 0, 0, COLS, LINES);
 * IrcitUiRealize(root);
 *
 * btn = IrcitUiAddButton(IrcitUiFind(root, "canvas"), &def);
 * if (!btn) {
 *   fputs("canvas is not a container\n", stderr);
 *   IrcitUiDeflate(root);
 *   endwin();
 *   return;
 * }
 * IrcitUiLayout(root, 0, 0, COLS, LINES);
 * IrcitUiRealize(root);
 * IrcitUiDeflate(root);
 * endwin();
 * @endcode
 */
PUBLIC_API IrcitUiNode *
IrcitUiAddButton(IrcitUiNode *container_ptr, const IrcitUiWidgetDef *def_ptr);

/**
 * @brief Remove @p node_ptr from its parent and destroy it.
 *
 * @details Fires `on_deflated`, deflates the subtree, detaches it, and frees it. Any pointer to the
 *          node or to anything under it is dangling afterwards.
 *
 * @param node_ptr The stacked node; NULL is ignored.
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
 * IrcitUiNode *popup;
 *
 * IrcitUiLayout(root, 0, 0, COLS, LINES);
 * IrcitUiRealize(root);
 * popup = IrcitUiInflateFloat(root, "tools_panel");
 * if (!popup) {
 *   fputs("no template with id tools_panel\n", stderr);
 *   IrcitUiDeflate(root);
 *   endwin();
 *   return;
 * }
 *
 * IrcitUiPopStacked(popup);   // popup is dangling from here on
 * popup = NULL;
 * IrcitUiFocusTop();
 * IrcitUiDraw(root, NULL);
 * doupdate();
 * IrcitUiDeflate(root);
 * endwin();
 * @endcode
 */
PUBLIC_API void
IrcitUiPopStacked(IrcitUiNode *node_ptr);

/**
 * @brief Allocate an empty theme.
 *
 * @details The theme starts with a zeroed base style and no rules, so a node it is applied to keeps
 *          the colours it already had. The caller owns the result and releases it with @ref
 *          IrcitUiThemeDestroy or by handing it to @ref IrcitUiThemeSetCurrent.
 *
 * @return A new zeroed theme, or NULL on allocation failure.
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
 * IrcitUiTheme *theme = IrcitUiThemeCreate();
 * IrcitUiStyleOverride base = {0};
 *
 * if (!theme) {
 *   fputs("theme allocation failed\n", stderr);
 *   IrcitUiDeflate(root);
 *   endwin();
 *   return;
 * }
 *
 * base.set_fg = 1; base.fg = 7;
 * base.set_bg = 1; base.bg = 0;
 * if (!IrcitUiThemeSetBase(theme, base)) {
 *   IrcitUiThemeDestroy(theme);
 *   IrcitUiDeflate(root);
 *   endwin();
 *   return;
 * }
 *
 * IrcitUiLayout(root, 0, 0, COLS, LINES);
 * IrcitUiRealize(root);
 * if (!IrcitUiThemeApply(theme, root))
 *   fputs("apply failed\n", stderr);
 * IrcitUiDraw(root, NULL);
 * doupdate();
 *
 * IrcitUiThemeDestroy(theme);
 * IrcitUiDeflate(root);
 * endwin();
 * @endcode
 */
PUBLIC_API IrcitUiTheme *
IrcitUiThemeCreate(void);
/**
 * @brief Release a theme and its rule array.
 *
 * @details Also clears the module's current theme if @p theme_ptr was it, so @ref
 *          IrcitUiThemeCurrent starts returning NULL rather than a dangling pointer. Never destroy
 *          a theme you handed to @ref IrcitUiThemeSetCurrent: that call took ownership and destroys
 *          it for you.
 *
 * @param theme_ptr Theme to free; NULL is a no-op.
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
 * IrcitUiTheme *theme = IrcitUiThemeCreate();
 *
 * if (!theme) {
 *   fputs("theme allocation failed\n", stderr);
 *   IrcitUiDeflate(root);
 *   endwin();
 *   return;
 * }
 *
 * IrcitUiThemeDestroy(theme);   // rules and struct both released
 * theme = NULL;
 * IrcitUiThemeDestroy(NULL);    // and NULL is a safe no-op
 *
 * IrcitUiDeflate(root);
 * endwin();
 * @endcode
 */
PUBLIC_API void
IrcitUiThemeDestroy(IrcitUiTheme *theme_ptr);
/**
 * @brief Set the style every node starts from.
 *
 * @details Only the attributes whose `set_` flags are non-zero take effect, so a base that names no
 *          border colour leaves borders alone. Call it before @ref IrcitUiThemeApply; changing the
 *          base afterwards needs another apply.
 *
 * @param theme_ptr Theme to change.
 *
 * @param style Base override, by value.
 *
 * @return 1 on success, 0 when @p theme_ptr is NULL.
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
 * IrcitUiTheme *theme = IrcitUiThemeCreate();
 * IrcitUiStyleOverride base = {0};
 *
 * if (!theme) {
 *   fputs("theme allocation failed\n", stderr);
 *   IrcitUiDeflate(root);
 *   endwin();
 *   return;
 * }
 *
 * base.set_fg = 1; base.fg = 7;    // black on ...
 * base.set_bg = 1; base.bg = 0;    // ... nothing set for borders
 * if (!IrcitUiThemeSetBase(theme, base)) {
 *   fputs("NULL theme\n", stderr);
 *   IrcitUiThemeDestroy(theme);
 *   IrcitUiDeflate(root);
 *   endwin();
 *   return;
 * }
 *
 * IrcitUiLayout(root, 0, 0, COLS, LINES);
 * IrcitUiRealize(root);
 * IrcitUiThemeApply(theme, root);
 * IrcitUiThemeDestroy(theme);
 * IrcitUiDeflate(root);
 * endwin();
 * @endcode
 */
PUBLIC_API int
IrcitUiThemeSetBase(IrcitUiTheme *theme_ptr, IrcitUiStyleOverride style);
/**
 * @brief Append one selector-plus-style rule.
 *
 * @details Order is significant: later rules of the same specificity win, and a more specific
 *          selector beats a less specific one regardless of position. The rule is copied, so a
 *          local may go out of scope.
 *
 * @param theme_ptr Theme to append to.
 *
 * @param rule Rule to copy in, by value.
 *
 * @return 1 on success, 0 when @p theme_ptr is NULL or the rule array cannot grow.
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
 * IrcitUiTheme *theme = IrcitUiThemeCreate();
 * IrcitUiThemeRule by_id = {0};
 *
 * if (!theme) {
 *   fputs("theme allocation failed\n", stderr);
 *   IrcitUiDeflate(root);
 *   endwin();
 *   return;
 * }
 *
 * by_id.selector = IRCITUI_THEME_ID;
 * by_id.value    = "cmdline";
 * by_id.style.set_fg = 1; by_id.style.fg = 3;
 * if (!IrcitUiThemeAddRule(theme, by_id)) {
 *   fputs("could not append the rule\n", stderr);
 *   IrcitUiThemeDestroy(theme);
 *   IrcitUiDeflate(root);
 *   endwin();
 *   return;
 * }
 *
 * IrcitUiLayout(root, 0, 0, COLS, LINES);
 * IrcitUiRealize(root);
 * IrcitUiThemeApply(theme, root);
 * IrcitUiDraw(root, NULL);
 * doupdate();
 *
 * IrcitUiThemeDestroy(theme);
 * IrcitUiDeflate(root);
 * endwin();
 * @endcode
 */
PUBLIC_API int
IrcitUiThemeAddRule(IrcitUiTheme *theme_ptr, IrcitUiThemeRule rule);
/**
 * @brief Resolve the theme onto @p root_ptr and everything under it.
 *
 * @details Overwrites the nodes' colours in place; it does not repaint, so follow with @ref
 *          IrcitUiDraw. Applying a theme twice is idempotent, because the resolution starts from
 *          the theme's base each time rather than from the node's current colour.
 *
 * @param theme_ptr Theme to resolve.
 *
 * @param root_ptr Root of the subtree to recolour.
 *
 * @return 1 on success, 0 when either argument is NULL.
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
 * IrcitUiTheme *theme = IrcitUiThemeCreate();
 *
 * if (!theme) {
 *   fputs("theme allocation failed\n", stderr);
 *   IrcitUiDeflate(root);
 *   endwin();
 *   return;
 * }
 *
 * IrcitUiLayout(root, 0, 0, COLS, LINES);
 * IrcitUiRealize(root);
 * if (!IrcitUiThemeApply(theme, root)) {
 *   fputs("NULL theme or NULL root\n", stderr);
 *   IrcitUiThemeDestroy(theme);
 *   IrcitUiDeflate(root);
 *   endwin();
 *   return;
 * }
 * IrcitUiDraw(root, NULL);
 * doupdate();
 *
 * IrcitUiThemeDestroy(theme);
 * IrcitUiDeflate(root);
 * endwin();
 * @endcode
 */
PUBLIC_API int
IrcitUiThemeApply(IrcitUiTheme *theme_ptr, IrcitUiNode *root_ptr);
/**
 * @brief Make @p theme_ptr the module's current theme, taking ownership of it.
 *
 * @details Ownership passes to the engine: the previously current theme is destroyed here, and @p
 *          theme_ptr will be destroyed by the next call that replaces it. Do not free it yourself
 *          afterwards. A second call with the same pointer is a no-op.
 *
 * @param theme_ptr Theme to install, or NULL to clear the slot.
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
 * IrcitUiTheme *theme = IrcitUiThemeCreate();
 * IrcitUiStyleOverride base = {0};
 *
 * if (!theme) {
 *   fputs("theme allocation failed\n", stderr);
 *   IrcitUiDeflate(root);
 *   endwin();
 *   return;
 * }
 *
 * base.set_fg = 1; base.fg = 7;
 * IrcitUiThemeSetBase(theme, base);
 *
 * IrcitUiThemeSetCurrent(theme);   // ownership moves to the engine
 * theme = NULL;                    // so do not destroy it here
 *
 * IrcitUiLayout(root, 0, 0, COLS, LINES);
 * IrcitUiRealize(root);
 * IrcitUiThemeApplyCurrent(root);
 * IrcitUiDraw(root, NULL);
 * doupdate();
 *
 * IrcitUiThemeSetCurrent(NULL);    // destroys the installed theme
 * IrcitUiDeflate(root);
 * endwin();
 * @endcode
 */
PUBLIC_API void
IrcitUiThemeSetCurrent(IrcitUiTheme *theme_ptr);
/**
 * @brief The theme installed by @ref IrcitUiThemeSetCurrent.
 *
 * @details Borrowed, not owned: the engine destroys it, so do not free it and do not keep the
 *          pointer past a @ref IrcitUiThemeSetCurrent call.
 *
 * @return The current theme, or NULL when none is installed.
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
 * IrcitUiTheme *current;
 *
 * current = IrcitUiThemeCurrent();
 * if (!current) {
 *   fputs("no theme installed; ApplyCurrent would report 0\n", stderr);
 *   IrcitUiDeflate(root);
 *   endwin();
 *   return;
 * }
 *
 * IrcitUiLayout(root, 0, 0, COLS, LINES);
 * IrcitUiRealize(root);
 * IrcitUiThemeApply(current, root);
 * IrcitUiDraw(root, NULL);
 * doupdate();
 * IrcitUiDeflate(root);
 * endwin();
 * @endcode
 */
PUBLIC_API IrcitUiTheme *
IrcitUiThemeCurrent(void);
/**
 * @brief Resolve the installed theme onto @p root_ptr.
 *
 * @details The convenience form of @ref IrcitUiThemeCurrent followed by @ref IrcitUiThemeApply, for
 *          the redraw path where a theme may or may not have been installed.
 *
 * @param root_ptr Root of the subtree to recolour.
 *
 * @return 1 on success, 0 when no theme is installed or @p root_ptr is NULL.
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
 *
 * if (!IrcitUiThemeApplyCurrent(root))
 *   fputs("no theme installed, or NULL root; colours unchanged\n", stderr);
 * IrcitUiDraw(root, NULL);
 * doupdate();
 * IrcitUiDeflate(root);
 * endwin();
 * @endcode
 */
PUBLIC_API int
IrcitUiThemeApplyCurrent(IrcitUiNode *root_ptr);
/**
 * @brief Tag @p node_ptr with a class name for @ref IRCITUI_THEME_CLASS rules.
 *
 * @details The pointer is stored as given, so the string must outlive the node: pass a literal or a
 *          buffer you own, never a stack array that is about to go out of scope.
 *
 * @param node_ptr Node to tag; NULL is ignored.
 *
 * @param class_name_ptr Class name; stored by pointer, not copied.
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
 *   fputs("no cmdline node\n", stderr);
 *   IrcitUiDeflate(root);
 *   endwin();
 *   return;
 * }
 *
 * IrcitUiSetThemeClass(field, "field");   // a literal: static storage
 * IrcitUiLayout(root, 0, 0, COLS, LINES);
 * IrcitUiRealize(root);
 * IrcitUiThemeApplyCurrent(root);
 * IrcitUiDeflate(root);
 * endwin();
 * @endcode
 */
PUBLIC_API void
IrcitUiSetThemeClass(IrcitUiNode *node_ptr, const char *class_name_ptr);
/**
 * @brief The class name tagged on @p node_ptr.
 *
 * @details Borrowed, not owned: it is the very pointer that was passed to @ref
 *          IrcitUiSetThemeClass.
 *
 * @param node_ptr Node to read; NULL yields NULL.
 *
 * @return The stored class name, or NULL when there is none.
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
 * const char *cls;
 *
 * IrcitUiSetThemeClass(field, "field");
 * cls = IrcitUiThemeClass(field);
 * if (!cls)
 *   fputs("node is untagged\n", stderr);
 * else
 *   printf("class: %s\n", cls);
 *
 * IrcitUiDeflate(root);
 * endwin();
 * @endcode
 */
PUBLIC_API const char *
IrcitUiThemeClass(const IrcitUiNode *node_ptr);

/**
 * @brief The key code an event carries, for a listener bound with @ref IrcitUiBindKey.
 *
 * @details @ref IrcitUiEvent is opaque -- its definition is private to the library -- so a
 *          callback bound through the public binder is handed a pointer it can hold and
 *          cannot read. This is the accessor that closes that: the same shape
 *          @ref IrcitUiNodeGetId and @ref IrcitUiNodeGetKind already have.
 *
 *          **The two together are the whole of the public key path**, and neither is useful
 *          alone: a binder with no way to read the event binds a listener that cannot act,
 *          and an accessor with no binder has nothing to hand it to.
 *
 * @param ev_ptr The event, as passed to an @ref IrcitUiKeyDownCallback.
 *
 * @return The key as ncurses reports it -- an ASCII byte for a printable, a `KEY_*` value for
 *         a function or arrow key. 0 for NULL, and 0 for an event whose tag carries no key,
 *         which the event's own header documents as the convention.
 *
 * @see IrcitUiBindKey
 */
PUBLIC_API int
IrcitUiEventKeyCode(const IrcitUiEvent *ev_ptr);
/**
 * @brief The wheel displacement an event carries, for a listener bound with
 *        @ref IrcitUiBindScroll.
 *
 * @details Without this there is no way for an application to read a scroll at all: the event
 *          capsule holds `scroll_delta`, and the accessor is the whole of the contract, exactly
 *          as @ref IrcitUiEventKeyCode is for a key.
 *
 *          **Sign is a convention, and it is the engine's**: offset grows downwards, so a
 *          positive delta is the wheel moving down and a negative one is it moving up. One notch
 *          is one unit today, and a listener should read the field rather than assume that.
 *
 * @param ev_ptr The event, as passed to an @ref IrcitUiLifecycleCallback.
 *
 * @return The wheel displacement. 0 for NULL, and 0 for an event whose tag carries no scroll,
 *         which the event's own header documents as the convention.
 *
 * @see IrcitUiBindScroll
 */
PUBLIC_API int
IrcitUiEventScrollDelta(const IrcitUiEvent *ev_ptr);

/* The dispatcher, the framework's defaults, and the tree destructor.
 *
 * These seventeen symbols were exported from ircituilib.a and declared only in
 * the library's private header, so a consumer could link against them and could
 * not see them. G29 in IRCITUILIB_DESIGN.md names that as the one state a symbol
 * must not be in: an installed header declares it, or it is static.
 *
 * Declaring them records a surface rather than opening a new one. The dispatcher
 * handle's own header says it exists "for the listener to act back on the engine
 * through", and the generator binds the eight defaults by name in every node it
 * emits (tools/gen_ui.py, DEFAULT_LISTENERS), so a tree built from the
 * generator's output cannot be compiled by anything that cannot name them.
 *
 * A name to read carefully: the three IrcitUiDispatcherPost* calls dispatch their
 * event NOW, on the calling thread, and return whether a listener consumed it.
 * They are not queue posts. The queued post is IrcitUiDispatcherPostMessage,
 * which belongs to the async bridge and is not part of this set. */

/**
 * @brief The process's dispatcher.
 *
 * @details A file-scope singleton in ui_event_dispatcher.c: this revision has one
 *          screen, so one dispatcher, and the singleton is the honest shape while
 *          the question of per-instance state is open.
 *
 *          It is also what a listener is handed as its @p disp parameter, so a
 *          listener that needs it outside a callback takes it from here.
 *
 * @return The dispatcher. Never NULL, and never freed by the caller: it is
 *         static storage.
 */
PUBLIC_API IrcitUiEventDispatcher *
IrcitUiDispatcherInstance(void);
/**
 * @brief Move keyboard focus to @p node_ptr, through the dispatcher.
 *
 * @details Carries out @ref IrcitUiSetFocus and mirrors the result into the
 *          dispatcher's own @c focused_node. The global stays authoritative; the
 *          mirror exists for the readers that have already been moved onto it.
 *
 * @param disp_ptr The dispatcher. NULL still moves the focus, and skips the mirror.
 * @param node_ptr The node to focus, or NULL to clear the focus.
 *
 * @see IrcitUiSetFocus
 */
PUBLIC_API void
IrcitUiDispatcherSetFocus(IrcitUiEventDispatcher *disp_ptr, IrcitUiNode *node_ptr);
/**
 * @brief Ask the application to stop.
 *
 * @details Sets the dispatcher's exit flag. It does not return from anything and
 *          does not tear the tree down: the loop reads the flag with
 *          @ref IrcitUiDispatcherExited after a pass, so this is how a listener
 *          ends the application.
 *
 * @param disp_ptr The dispatcher. Ignored if NULL.
 *
 * @see IrcitUiDispatcherExited
 */
PUBLIC_API void
IrcitUiDispatcherExit(IrcitUiEventDispatcher *disp_ptr);
/**
 * @brief Whether @ref IrcitUiDispatcherExit has been called.
 *
 * @details The flag is set and never cleared, so once this returns non-zero it
 *          keeps returning non-zero.
 *
 * @param disp_ptr The dispatcher. NULL reads as not exited.
 *
 * @return Non-zero once an exit has been requested.
 *
 * @see IrcitUiDispatcherExit
 */
PUBLIC_API int
IrcitUiDispatcherExited(const IrcitUiEventDispatcher *disp_ptr);
/**
 * @brief Install the application's listener for non-UI pipeline traffic.
 *
 * @details The entry point for @c MAIN_MSG_NETWORK_DATA: chunks of bytes posted
 *          by the application's own worker threads, delivered on Thread 0. This
 *          is the framework's whole part in a network stream -- it moves the
 *          bytes across the thread boundary and calls out. It does not parse,
 *          does not frame, and writes no node.
 *
 *          **One slot, not one per stream.** The application registers a single
 *          listener and demultiplexes inside it: @p user_ptr is handed back
 *          unread on every call, and which connection a chunk came from travels
 *          in the message's own token. There is no per-connection registration
 *          and no unregister -- a second call here replaces the first.
 *
 *          Call it at bootstrap, before @ref IrcitUiMainLooper. The listener
 *          runs on Thread 0 from inside the loop's drain, so installing one from
 *          another thread while the loop is running is a race rather than a
 *          reconfiguration.
 *
 *          Pass NULL to clear it, and do that **before** the tree is torn down.
 *          A stale listener reached with a stream token whose storage the
 *          application has already released is a use-after-free that the
 *          framework has no way to detect.
 *
 * @param disp_ptr     The dispatcher. Ignored if NULL. Outside a callback, the
 *                     way to reach it is @ref IrcitUiDispatcherInstance.
 *
 * @param listener_ptr The listener, or NULL to clear. See
 *                     @ref IrcitUiNetworkListener for the delivery contract --
 *                     in particular that @c bytes is borrowed for the duration
 *                     of the call only, and that this callback runs **without**
 *                     the watchdog armed.
 *
 * @param user_ptr     The application's own pointer, stored and handed back
 *                     unread. Never freed here.
 *
 * @see IrcitUiNetworkListener
 * @see IrcitUiDispatcherInstance
 */
PUBLIC_API void
IrcitUiDispatcherRegisterNetworkListener(IrcitUiEventDispatcher *disp_ptr,
                                        IrcitUiNetworkListener   listener_ptr,
                                        void                   *user_ptr);
/**
 * @brief Queue one chunk of a network stream, waiting for room rather than
 *        dropping it.
 *
 * @details The producer half of @ref IrcitUiNetworkListener, and the only way an
 *          application puts bytes into the pipeline. It builds the envelope
 *          itself, so an application never names @c MainLoopMessage, never
 *          reaches into the ring and never writes the wake descriptor.
 *
 *          **It waits.** The non-blocking post drops on a full ring, which is
 *          right for a keystroke and wrong for a byte stream: a lost chunk
 *          desynchronises a reassembler permanently. Here the caller gives a
 *          deadline instead, and the give-up decision stays the caller's --
 *          which is why this takes @p timeout_ms rather than the application's
 *          own shutdown flag, since the framework has no business naming an
 *          application's type.
 *
 *          Waiting is also what makes backpressure work: a producer that stops
 *          reading its socket lets the kernel receive buffer fill, which closes
 *          the sender's window and throttles the source at the transport layer.
 *
 *          **@p bytes_len is bounded here, not trusted.** A chunk larger than
 *          the envelope's inline buffer is refused and logged rather than
 *          truncated, because a silent truncation of a message reads as success
 *          and the producer would never learn. Chunk the read.
 *
 * @param disp_ptr     The dispatcher. Ignored if NULL.
 *
 * @param stream_token Per-stream identity, handed back to the listener unchanged
 *                     and never interpreted. Its storage must outlive every
 *                     chunk posted with it.
 *
 * @param bytes_ptr    The bytes. Need not be null-terminated; @p bytes_len is
 *                     authoritative. Ignored if NULL.
 *
 * @param bytes_len    How many bytes. At most the envelope's capacity minus one.
 *
 * @param timeout_ms   How long to keep trying for room, in milliseconds. 0 tries
 *                     once and returns. Negative is treated as 0.
 *
 * @return Non-zero if the chunk was queued, 0 if it was not -- refused as
 *         oversized, or the deadline expired. **A consumer that reports 0 means
 *         the chunk is gone**; a stream that must not lose it should treat the
 *         0 as fatal for that connection rather than continuing.
 *
 * @see IrcitUiNetworkListener
 * @see IrcitUiDispatcherRegisterNetworkListener
 */
PUBLIC_API int
IrcitUiDispatcherPostNetworkChunk(IrcitUiEventDispatcher *disp_ptr,
                                  void                  *stream_token,
                                  const char            *bytes_ptr,
                                  size_t                 bytes_len,
                                  int                    timeout_ms);

/* The framework's teardown pair, and the order they belong in.
 *
 * IrcitUiMainLooper no longer tears anything down. It used to destroy the
 * dispatcher at its own tail, which freed the envelope ring BEFORE control
 * returned to the caller -- so an application's producer threads, the only party
 * able to quiesce them, lost their pipeline with no way to observe it. A message
 * posted after that point is dropped in silence and logged as a full pipeline.
 *
 * So the sequence is the application's, and it is:
 *
 *   1. stop and join every producer thread the application started
 *   2. IrcitUiDestroyFrameworkContext(ctx)
 *
 * Step 1 is not a formality. Nothing in step 2 can reach an application's
 * threads, and a producer still posting would be pushing into the ring that
 * call is about to free. */
/**
 * @brief Release the dispatcher's own resources: the envelope ring, the wake
 *        descriptor, the watchdog timer and the lock.
 *
 * @details Called by @ref IrcitUiDestroyFrameworkContext, and reachable
 *          directly only by an application that needs the pipeline gone before
 *          the rest of the context. Idempotent: a second call is a no-op rather
 *          than a double free.
 *
 *          **Precondition: every producer thread has been stopped and joined.**
 *          A worker still posting would be writing into the ring this frees.
 *
 * @param disp_ptr The dispatcher. Ignored if NULL.
 *
 * @see IrcitUiDestroyFrameworkContext
 */
PUBLIC_API void
IrcitUiEventDispatcherDestroy(IrcitUiEventDispatcher *disp_ptr);
/**
 * @brief Release everything the framework allocated, so the process can exit
 *        without leaking it.
 *
 * @details Frees the dispatcher and its envelope ring, then the scheduler pool
 *          -- stopped and joined internally by its own destructor, which guards
 *          on whether the pool is running -- and then the input queue. The
 *          context itself is static storage and is not freed, so the handle
 *          stays valid and a second call is a no-op.
 *
 *          Call it **after** @ref IrcitUiMainLooper has returned, and after the
 *          application has stopped and joined its own producer threads: see the
 *          sequence above.
 *
 * @param context_ptr The context. Ignored if NULL.
 *
 * @see IrcitUiInitialiseFrameworkContext
 * @see IrcitUiEventDispatcherDestroy
 */
PUBLIC_API void
IrcitUiDestroyFrameworkContext(IrcitUiContext *context_ptr);

/* --- The palette -----------------------------------------------------------
 *
 * Every colour in the library is asked for as a ROLE, not as a number, and the
 * palette decides what that role means on this terminal. Eight roles are defined;
 * a layout asks for one with its `theme_class` key (see C23) and draws in that
 * slot's colours, and so does a node built from C through IrcitUiWidgetDef::
 * theme_class.
 *
 * What that buys, in order of how much it matters:
 *
 *  - A layout stops naming colours. `text_colour: cyan` is a decision about a
 *    terminal; `theme_class: text_muted` is a decision about intent, and the nine
 *    colour terminal gets white-on-black with A_DIM rather than a cyan it may not
 *    have.
 *
 *  - A role can move at runtime. IrcitUiPaletteApplyProfile rewrites the slot
 *    table, so everything wearing those slots comes back in the new colours on the
 *    next repaint -- no node is restyled and no tree is walked. **Repaint is the
 *    caller's**: a slot resolves to a colour PAIR, and a changed pair reaches the
 *    screen only when the cells holding it are written again, so
 *    IrcitUiPaletteRepaint follows an apply. (The zero-repaint path exists and is a
 *    different mechanism: init_color on a register a pair already points at, which
 *    is what the animated effects use.)
 *
 *  - Two slots naming the same colours share one pair, so a palette costs the
 *    terminal as many pairs as it has distinct colours and no more.
 *
 * Example -- switch to the built-in light palette on a key:
 *
 * @code
 * const IrcitUiPaletteProfile *p = IrcitUiPaletteProfileByName("light");
 * if (p) {
 *   IrcitUiPaletteApplyProfile(p);
 *   IrcitUiPaletteRepaint();
 * }
 * @endcode
 *
 * Thread 0 only: these call into ncurses, and the slot table is unsynchronised for
 * the same reason every other colour call in this library is -- nothing here runs
 * off Thread 0.
 */

/*!
 * @defgroup palette_effects The register-driven effects
 *
 * A pair addresses a colour the terminal already has. A *register* is a colour, and
 * writing one changes what every pair pointing at it renders -- which is how a
 * gradient or a fade is possible at all.
 *
 * **They cost the user something, which is why they are off by default.** Once any
 * register has been written, ncurses emits OSC 104 at exit, and the terminal's palette
 * returns to the emulator's defaults: a hand-tuned colour scheme does not survive the
 * program. So an application opts in, and an application that never opts in never pays
 * it.
 *
 * @{
 */
#define IRCITUI_PALETTE_EFFECT_GRADIENT (1u << 0) ///< The notification box's background as a horizontal ramp.
#define IRCITUI_PALETTE_EFFECT_CANOPY   (1u << 1) ///< A vertical background ramp down the screen, with the foregrounds on it pre-blended to match. Needs a container wearing the `surface` slot.
#define IRCITUI_PALETTE_EFFECT_PULSE    (1u << 2) ///< Animate the canopy: its two endpoints breathe together. Implies CANOPY.
/** @brief Let a widget dissolve into its backdrop instead of vanishing.
 *
 * **The bit is public but the API that uses it is not, yet**: starting a fade needs
 * IrcitUiPaletteFadeStart, which is still private to the library, so an application outside
 * this tree can set this bit and get nothing. Kept here rather than left out because the
 * mask is a public contract and an application reading it back should see every bit that
 * exists. See IRCITUILIB_PROGRESS_INDICATORS_DESIGN.md §9.10. */
#define IRCITUI_PALETTE_EFFECT_FADE     (1u << 3)
/** @} */

/*!
 * @brief Enable or disable the register-driven effects, as a mask of
 *        @c IRCITUI_PALETTE_EFFECT_*.
 *
 * Nothing is enabled by default; see the group comment for the cost of enabling one.
 * Effects that are not enabled do not borrow registers, so the palette is not touched
 * and the exit behaviour is unchanged.
 */
PUBLIC_API void IrcitUiPaletteSetEffects(unsigned mask);

/*!
 * @brief The enabled effects, as last set.
 */
PUBLIC_API unsigned IrcitUiPaletteEffects(void);

/*!
 * @brief Draw every framed container in a glyph profile, or draw it the legacy way.
 *
 * The frame a node asks for with its @c border key is drawn as the ACS box this library
 * has always drawn, until an application asks for something else here. Passing a style
 * changes every frame in the process at once; there is no per-node style, for the reason
 * @ref IrcitUiBorderStyle gives about the primitive -- and because the choice is a
 * property of the application's look rather than of one panel.
 *
 * **This is a whole-tree change and the caller must repaint.** It is not a pair and not
 * a register but a different set of GLYPHS, so no cell that is already on the glass
 * becomes correct by being reinterpreted: every frame has to be written again. Use
 * @ref IrcitUiPaletteRepaint, which is the same demand a slot redefinition needs and for
 * the same reason.
 *
 * The frames this reaches are the engine's own -- a node carrying @ref IRCITUI_F_BORDER --
 * and the notification overlay's box, which is not a node but is framed. The window
 * driver's own @c window_box is a separate subsystem and is not reached by this.
 *
 * @param style The profile, or a value out of range to leave the current one. Nothing is
 *              changed for @ref IRCITUI_BORDER_STANDARD, whose glyphs are @c box()'s.
 *
 * @note Wide glyphs need a UTF-8 locale, which the library documents but does not set,
 *       because the locale is the application's: see @c setlocale(LC_ALL, "") in this
 *       header's own opening example. Without it @c mbstowcs fails and the profile falls
 *       back to the ACS box rather than to mojibake.
 *
 * @note Thread 0 only, like every other call that reaches ncurses.
 */
PUBLIC_API void IrcitUiSetBorderStyle(IrcitUiBorderStyle style);

/*!
 * @brief The border profile frames are drawn in, as last set.
 *
 * @return The current style; @ref IRCITUI_BORDER_STANDARD until something asks otherwise.
 */
PUBLIC_API IrcitUiBorderStyle IrcitUiBorderStyleGet(void);

/*!
 * @brief Let a stacked popup cast a drop shadow, or stop it casting one.
 *
 * A shadow is stamped off-axis behind a popup -- one column to the right, one row below --
 * so the popup reads as a layer rather than as a hole in the panel behind it. It reaches
 * every node carrying @ref IRCITUI_F_STACKED, which is what @ref
 * IrcitUiCreateFloatingWindow builds, so an application asks once and gets the depth on
 * every popup it raises.
 *
 * **Off by default, and it costs a refresh.** The shadow is drawn into the PARENT's cells
 * rather than into a window of its own -- a window would be opaque over the layer beneath
 * it, and would have to be pushed before the popup and after its siblings, which ncurses
 * gives no way to express. Drawing into the parent means the parent must be painted and
 * pushed again whenever a popup moves, so enabling this adds one refresh per stacked child
 * per moving frame. An application that never asks pays nothing.
 *
 * What it does **not** reach, and worth knowing before asking for it:
 *  - the notification overlay, which is not a node and has no parent window to cast onto;
 *  - a popup whose cells the layout clips away, which casts nothing because it has no
 *    rectangle to offset from.
 *
 * @param on Non-zero to cast, 0 to stop. Changing it needs the tree repainted, because the
 *           cells a shadow occupied are the parent's and only a repaint gives them back.
 *
 * @note Thread 0 only, like every other call that reaches ncurses.
 */
PUBLIC_API void IrcitUiSetShadow(int on);

/*!
 * @brief Whether stacked popups cast a drop shadow, as last set.
 */
PUBLIC_API int IrcitUiShadow(void);

/*!
 * @brief Hand every borrowed colour register back, whatever effect holds it.
 *
 * The mitigation for the cost described above. Two things about it are worth knowing
 * before relying on it:
 *
 *  - It restores what ncurses **believed** the registers held. ncurses never queries the
 *    terminal for its palette, so this approximates the user's colours rather than
 *    restoring them exactly.
 *  - It cannot undo OSC 104, which ncurses emits at exit once any register has been
 *    written. To be the last word, call it **after** @c endwin.
 */
PUBLIC_API void IrcitUiPaletteRestore(void);

/*!
 * @brief Whether a canopy is up, so a caller can tell "the effect is on" from "it is on
 *        and it has its registers".
 *
 * False when the effect is enabled but the terminal refused the borrow, which is the
 * difference between a backdrop that failed and a backdrop nobody asked for.
 */
PUBLIC_API int IrcitUiPaletteCanopyActive(void);

/*!
 * @brief The style a role is drawn in: its attributes and its colour pair, ready
 *        for @c wattron or to be OR'd into a @c wbkgd.
 *
 * @param slot The role, one of @ref IrcitUiPaletteSlot.
 * @return The composite style. When the terminal has no colour the attributes are
 *         returned alone, which still says something; a slot out of range is
 *         @c A_NORMAL.
 */
PUBLIC_API chtype IrcitUiPaletteStyle(IrcitUiPaletteSlot slot);

/*!
 * @brief Redefine one slot.
 *
 * @param slot The slot to redefine; out of range is ignored.
 * @param fg   Foreground colour index, or -1 to leave the current one.
 * @param bg   Background colour index, or -1 to leave the current one.
 * @param attr Attributes to set, or @c (chtype)-1 to leave the current ones.
 *
 * Attributes are REPLACED rather than OR'd, so @c A_NORMAL states "plain" -- which
 * is why the "leave it" sentinel cannot be 0.
 *
 * The caller must repaint; see @ref IrcitUiPaletteRepaint.
 */
PUBLIC_API void IrcitUiPaletteSetSlot(IrcitUiPaletteSlot slot, short fg, short bg, chtype attr);

/*!
 * @brief Apply a profile's definitions to every slot at once.
 *
 * The caller must repaint; see @ref IrcitUiPaletteRepaint. @p profile_ptr is copied,
 * so a built-in, a local or an application's own static are all fine.
 */
PUBLIC_API void IrcitUiPaletteApplyProfile(const IrcitUiPaletteProfile *profile_ptr);

/*!
 * @brief A built-in profile by name -- @c "dark" or @c "light" -- or NULL.
 *
 * Dark is the default a caller gets without asking. Every colour in it is the one
 * the widget it belongs to used to hard-code, so applying it is a no-op on a screen
 * that has never had a profile applied.
 */
PUBLIC_API const IrcitUiPaletteProfile *IrcitUiPaletteProfileByName(const char *name_ptr);

/*!
 * @brief Mark the screen for repaint, so an applied palette reaches the glass.
 *
 * Separate from the apply calls on purpose. The palette sits below the screen and
 * does not reach up into it, but a caller who forgets this gets a silent no-op --
 * the colours change and nothing on screen does -- so it is a named call rather
 * than a footnote.
 */
PUBLIC_API void IrcitUiPaletteRepaint(void);

/*!
 * @brief The slot a layout's @c theme_class name refers to, or -1.
 *
 * The names are the YAML spellings: @c surface, @c panel, @c text, @c text_muted,
 * @c text_error, @c edit_active, @c overlay_snackbar, @c overlay_action. A layout
 * cannot spell anything else -- the generator refuses it (C23) -- so a -1 here means
 * a class was set from C.
 */
PUBLIC_API int IrcitUiPaletteSlotForName(const char *name_ptr);

/* --- The transient overlay -------------------------------------------------
 *
 * An Android-style Snackbar: a box that rises out of the footer, holds for a
 * moment and slides away. It is a WINDOW and not a node -- nothing in the layout
 * tree knows it exists, it takes no focus, and it is drawn over whatever is
 * underneath rather than displacing it. There is one of it, because there is one
 * screen.
 *
 * It holds no focus and joins no layout, so no routing path could deliver a key to
 * it. Instead every key is OFFERED to it first, by @ref IrcitUiSnackbarKey, and it
 * answers only for the action hotkey of a notification raised with
 * @ref IrcitUiSnackbarShowAction. Everything else goes on to the tree untouched,
 * which is why typing continues underneath a notification.
 *
 * THREAD 0 ONLY, like everything else that touches ncurses. There is no crossing
 * here and none is wanted: the network listener a notification is typically raised
 * from already runs on Thread 0.
 *
 * @ref IrcitUiMainLooper drives it, so an application that uses the loop has
 * nothing to do but call @ref IrcitUiSnackbarShow -- the loop already offers each
 * key and already answers @ref IrcitUiSnackbarAdvance's repaint demand. An
 * application driving its own frame loop does what the loop does, in this order:
 *
 * @code{.c}
 * // IrcitUiDispatchKey offers the key to the overlay itself, so a loop that
 * // dispatches needs no change. An application that routes keys some other way
 * // asks the same question directly -- see IrcitUiSnackbarKey.
 * IrcitUiDispatchKey(IrcitUiScreen.root, key);
 * if (IrcitUiSnackbarAdvance() && IrcitUiScreen.root)
 *     IrcitUiInvalidate(IrcitUiScreen.root);   // it left cells behind; repaint them
 * IrcitUiRenderFrame();
 * IrcitUiSnackbarRender();                     // after the tree: this draws over it
 * doupdate();
 * @endcode
 */

/**
 * @brief Raise a notification, or replace the one already up.
 *
 * @details The text is **copied**, up to an internal limit; the caller's buffer is
 *          not referenced afterwards. Control characters are replaced with spaces,
 *          because the box is one row tall and a newline written into it is a cursor
 *          move that lands on the border. Text too long for the box is clipped at
 *          the column that fits, by character and not by byte.
 *
 *          Raised while a box is already up, it replaces the text and restarts the
 *          hold **without** taking the box down first, and a box on its way out is
 *          driven back up from where it is rather than restarted from the footer.
 *
 *          Refused, and reported through the log, when there is no screen tree yet
 *          or the terminal is too small to hold the box at all.
 *
 * @param text_ptr      The message. Borrowed for the duration of the call only.
 *                      NULL or empty does nothing.
 * @param duration_ms   How long the box holds once it has settled. **0 means the
 *                      configured default**, not "forever".
 *
 * @see IrcitUiSnackbarDismiss
 */
PUBLIC_API void
IrcitUiSnackbarShow(const char *text_ptr, unsigned int duration_ms);

/**
 * @brief Raise a notification that offers one action, taken by one hotkey.
 *
 * @details Everything @ref IrcitUiSnackbarShow does, plus a label drawn at the
 *          right of the message row and a key that runs @p cb when it arrives
 *          while the box is up. The action is available from the moment the box
 *          starts to rise until it starts to leave; once it is on its way out, or
 *          on its way out sideways, the affordance is gone and the key is inert.
 *
 *          **The action exists iff @p cb is non-NULL.** There is no "has action"
 *          flag and no magic hotkey value: key 0 is a real key (Ctrl-@), so a
 *          sentinel here would make one legal key unaskable. Given a callback, a
 *          label is required -- a hotkey with nothing drawn to advertise it is a
 *          trap rather than a reservation, and it is refused.
 *
 *          **A printable hotkey is refused** (`32`..`126`, and `127`). That is not
 *          style: the loop's edit-field fast path sends every printable character
 *          straight into a focused edit field without ever reaching the
 *          dispatcher, so a printable hotkey would fire only when no edit field
 *          held focus -- intermittently, and with nothing to explain why. Use a
 *          control code or a `KEY_*` code. The three spellings of Enter (`10`,
 *          `13`, `KEY_ENTER`) are treated as one key, as the framework's own
 *          modal already does for the first two.
 *
 *          **And of the control codes, five never arrive at all.** The window-driver
 *          contract has the application's factory run `cbreak()`, not `raw()`, so
 *          `ICANON` and `ECHO` are cleared and **`ISIG` and `IXON` are left set**:
 *          the line discipline keeps `^C`, `^\` and `^Z` as the interrupt, quit and
 *          suspend characters, and `^S`/`^Q` as flow control. A hotkey of `3`, `28`,
 *          `26`, `19` or `17` is **dead** -- it never reaches `getch()`, and it looks
 *          exactly like a live key until a user presses it and nothing happens. All
 *          twenty-six control bytes are swept, and the dropped five confirmed, by
 *          `dev/probes/snackbar/p_hotkeys.c`. This is worth stating because the
 *          obvious choice for an undo is `^Z`, which is one of them.
 *
 *          The callback runs **after** the box has been dismissed, so raising a
 *          second notification from inside it works and is the intended use. It
 *          runs on Thread 0 inside key dispatch; it must not block.
 *
 *          The label is clipped by character, not by byte, and the message is
 *          clipped to leave room for it.
 *
 * @param text_ptr          The message. Borrowed for the duration of the call only.
 *                          NULL or empty does nothing.
 * @param duration_ms       Hold time once settled. **0 means the configured
 *                          default.** @ref IRCITUI_SNACKBAR_LENGTH_SHORT and
 *                          @ref IRCITUI_SNACKBAR_LENGTH_LONG are the conventional
 *                          values.
 * @param action_label_ptr  The label, drawn at the right of the message row.
 *                          Required when @p cb is given; NULL is refused.
 * @param hotkey            The key that takes the action. Must not be printable.
 * @param cb                The action, or NULL for a plain notification.
 * @param user_ptr          Passed to @p cb untouched.
 *
 * @return 1 when the notification is up, 0 when it was refused -- no screen tree
 *         yet, a terminal too small for the box, a printable hotkey, or a callback
 *         with no label. A refused **action** is not a refused **message**: the
 *         notification is still raised when only the action is refused, and 0 is
 *         how the caller learns the affordance is not there. That matters when the
 *         data behind it has already been hidden.
 *
 * @see IrcitUiSnackbarKey
 * @see IrcitUiSnackbarDismiss
 */
PUBLIC_API int
IrcitUiSnackbarShowAction(const char *text_ptr, unsigned int duration_ms, const char *action_label_ptr, int hotkey,
                          IrcitUiSnackbarActionCallback cb, void *user_ptr);

/**
 * @brief Offer a key to the notification overlay before the tree sees it.
 *
 * @details The overlay is not a node: it holds no focus and is in no layout, so
 *          there is no routing path that could deliver a key to it. This is that
 *          path. It is a function rather than a hook the engine installs because
 *          @ref IrcitUiMainLooper is not the only driver -- an application routing
 *          keys its own way needs the same question answered.
 *
 *          **@ref IrcitUiDispatchKey already calls this**, at the very top, before
 *          the recycler's edit intercept and before the whole routing matrix. So an
 *          application that dispatches keys through the engine has nothing to do,
 *          and calling this first as well is harmless but redundant.
 *
 *          It answers for exactly two things: the action hotkey, while the box is
 *          rising or settled, and -- only when swipe dismissal has been asked for
 *          with `IrcitUiSnackbarSetSwipeDismiss` -- the left and right arrows.
 *          Everything else returns 0 and goes on to the tree untouched, which is
 *          why a notification does not stop the user typing underneath it.
 *
 * @param key_code  A key as read from ncurses, after `keypad()` translation.
 *
 * @return Non-zero when the overlay took the key and the caller must not deliver
 *         it to anything else.
 *
 * @see IrcitUiSnackbarShowAction
 */
PUBLIC_API int
IrcitUiSnackbarKey(int key_code);

/**
 * @brief Send the notification away early.
 *
 * @details The box leaves the way it arrived rather than vanishing, so the frame
 *          that draws its last row is also the frame that puts back the cells it
 *          covered. A no-op when nothing is up, and safe to call from a key
 *          listener -- it makes a request, it does not tear anything down.
 */
PUBLIC_API void
IrcitUiSnackbarDismiss(void);

/**
 * @brief Whether a notification is up, or on its way up or down.
 *
 * @details The one call the loop's pacing needs: while this is true,
 *          @ref IrcitUiMainLooper wakes at the overlay's frame interval instead of
 *          the caret's, which is what lets the box move a row at a time.
 *
 * @return Non-zero while the overlay is anything but hidden.
 */
PUBLIC_API int
IrcitUiSnackbarActive(void);

/**
 * @brief Move the overlay one frame forward. Call before the frame pass.
 *
 * @details Integrates the elapsed time into the box's travel, settles it, expires
 *          its hold and takes it down. It draws nothing: everything it changes is
 *          position and state.
 *
 *          **The return value is the contract, and it is about the tree.** Non-zero
 *          means the box has just left cells that the last frame painted it on --
 *          it moved up, it shrank, it went away -- and those cells are in nobody
 *          else's change set, so the caller owes the frame a full repaint. The
 *          engine's lever for that is `IrcitUiInvalidate(IrcitUiScreen.root)`, and
 *          @ref IrcitUiMainLooper applies it. Ignoring it leaves a trail of the box's
 *          old rows on the screen.
 *
 * @return Non-zero when the frame must repaint the tree under the overlay.
 *
 * @see IrcitUiRenderFrame
 * @see IrcitUiSnackbarRender
 */
PUBLIC_API int
IrcitUiSnackbarAdvance(void);

/**
 * @brief Draw the overlay. Call after the frame pass, before the flush.
 *
 * @details Rewrites the box and stages it for the flush, which is what puts it over
 *          the tree -- ncurses resolves stacking by the order windows are refreshed,
 *          so this being after @ref IrcitUiRenderFrame is the whole of the z-order.
 *
 *          It re-stamps on every pass rather than only when the box moves: a frame
 *          that repaints the tree rewrites the overlay's cells in the virtual screen,
 *          so a box that skipped a pass would come back with a hole in it. Draws
 *          nothing when no notification is up.
 */
PUBLIC_API void
IrcitUiSnackbarRender(void);

/**
 * @brief Deliver a key event to @p node_ptr's listeners, now.
 *
 * @details The application's listeners run first and the framework's second, and
 *          the first non-zero answer wins -- the Event Consumption Contract. A
 *          listener that returns non-zero has consumed the key and nothing behind
 *          the node sees it.
 *
 *          Synchronous: this does not queue anything, and it does not read the
 *          keyboard. The key has already been read by the time this is called;
 *          what this does is decide who acts on it.
 *
 * @param disp_ptr The dispatcher handed to the listeners. May be NULL.
 * @param node_ptr The node the event resolved to. NULL returns 0.
 * @param key_code The ncurses key code.
 *
 * @return Non-zero if a listener consumed the event, zero if none did.
 */
PUBLIC_API int
IrcitUiDispatcherPostKey(IrcitUiEventDispatcher *disp_ptr, IrcitUiNode *node_ptr, int key_code);
/**
 * @brief Deliver a click event to @p node_ptr's listeners, now.
 *
 * @details Records the coordinates with @ref IrcitUiRecordMouse before the
 *          listeners run, because a listener that reads the engine's last-mouse
 *          store -- @ref IrcitUiEmojiInput does -- must see the click that
 *          reached it rather than an earlier one.
 *
 *          Synchronous, on the same terms as @ref IrcitUiDispatcherPostKey.
 *
 * @param disp_ptr The dispatcher handed to the listeners. May be NULL.
 * @param node_ptr The node the hit-test resolved the click to. NULL returns 0.
 * @param mouse_x  The column.
 * @param mouse_y  The row.
 *
 * @return Non-zero if a listener consumed the event, zero if none did.
 */
PUBLIC_API int
IrcitUiDispatcherPostClick(IrcitUiEventDispatcher *disp_ptr, IrcitUiNode *node_ptr, int mouse_x, int mouse_y);
/**
 * @brief Deliver a wheel event to @p node_ptr's listeners, now.
 *
 * @details Carries the displacement, because a scroll listener that cannot read one cannot act:
 *          @ref IrcitUiEventScrollDelta is how it reads it back. Positive is down, negative is up.
 *
 *          The node is the one the wheel resolved to, and the listeners' own fallback
 *          (@ref IrcitUiDefaultOnScroll) is what walks to the nearest scrollable ancestor -- so a
 *          caller posting a wheel at a node does not have to find the pane first.
 *
 *          Synchronous, on the same terms as @ref IrcitUiDispatcherPostKey.
 *
 * @param disp_ptr The dispatcher handed to the listeners. May be NULL.
 * @param node_ptr The node the wheel resolved to. NULL returns 0.
 * @param delta    Wheel displacement; negative is up.
 *
 * @return Non-zero if a listener consumed the event, zero if none did.
 */
PUBLIC_API int
IrcitUiDispatcherPostScroll(IrcitUiEventDispatcher *disp_ptr, IrcitUiNode *node_ptr, int delta);
/**
 * @brief Deliver a lifecycle event to @p node_ptr's listeners, now.
 *
 * @details The lifecycle family is @ref IRCITUI_EVENT_LIFECYCLE_INFLATE,
 *          @ref IRCITUI_EVENT_LIFECYCLE_DEFLATE and
 *          @ref IRCITUI_EVENT_LIFECYCLE_FOCUS. The engine posts these itself as a
 *          node is created, stripped and focused; a caller driving the tree by
 *          hand posts them to reach the same listeners the engine would.
 *
 *          Synchronous, on the same terms as @ref IrcitUiDispatcherPostKey.
 *
 * @param disp_ptr The dispatcher handed to the listeners. May be NULL.
 * @param node_ptr The node the event is about. NULL returns 0.
 * @param type     Which lifecycle event.
 *
 * @return Non-zero if a listener consumed the event, zero if none did.
 */
PUBLIC_API int
IrcitUiDispatcherPostLifecycle(IrcitUiEventDispatcher *disp_ptr, IrcitUiNode *node_ptr, IrcitUiEventType type);
/**
 * @brief Record a mouse position in the engine's last-mouse store.
 *
 * @details The store is a file-scope static in ui_engine.c rather than a member
 *          of the event, because the emoji grid reads its clicked column out of
 *          it and did so before events carried coordinates. The dispatcher is the
 *          writer: @ref IrcitUiDispatcherPostClick records the click it is about
 *          to deliver.
 *
 * @param mouse_x The column.
 * @param mouse_y The row.
 */
PUBLIC_API void
IrcitUiRecordMouse(int mouse_x, int mouse_y);
/**
 * @brief Fill @p state_ptr with the framework's eight listeners.
 *
 * @details What the generator emits as a literal initialiser for every node's
 *          @c interaction_defaults block. A node built by hand in code, rather
 *          than cloned from a template, has no generated initialiser, so this is
 *          how it gets the widget set's native behaviour: typing into a text
 *          field, @c Enter activating a button, and the rest.
 *
 *          It overwrites the listener block only. The @c user pointer, the inline
 *          edit buffer and the caret are left as they were.
 *
 * @param state_ptr The state to fill. Ignored if NULL.
 */
PUBLIC_API void
IrcitUiInteractionBindDefaults(IrcitUiInteractionState *state_ptr);
/* The eight, one per listener. All are what a node does when the application has
   not said otherwise, and none of them is reached directly by an application
   except through IrcitUiInteractionBindDefaults above.
 *
 * Every one returns non-zero when it has handled the event and zero when it has
 * not, which is what lets an application listener sit in front of it.
 *
 * Which of the eight are actually reachable in this revision is recorded, and is
 * not all of them: G8 and G31 in IRCITUILIB_DESIGN.md SS21 carry that list, and
 * an application relying on one of them should read it first. */
/** @brief The framework's @c on_key_down: routes the key to the target's per-kind handler. */
PUBLIC_API int
IrcitUiDefaultOnKeyDown(IrcitUiEventDispatcher *disp_ptr, IrcitUiEvent *ev_ptr, void *user_ptr);
/** @brief The framework's @c on_click: activates the target by kind. */
PUBLIC_API int
IrcitUiDefaultOnClick(IrcitUiEventDispatcher *disp_ptr, IrcitUiEvent *ev_ptr, void *user_ptr);
/** @brief The framework's @c on_scroll: scrolls the target's container. */
PUBLIC_API int
IrcitUiDefaultOnScroll(IrcitUiEventDispatcher *disp_ptr, IrcitUiEvent *ev_ptr, void *user_ptr);
/** @brief The framework's @c on_draw, for a widget that paints its own cells. */
PUBLIC_API int
IrcitUiDefaultOnDraw(IrcitUiEventDispatcher *disp_ptr, IrcitUiEvent *ev_ptr, void *user_ptr);
/** @brief The framework's @c on_resize, for a widget that re-measures on a resize. */
PUBLIC_API int
IrcitUiDefaultOnResize(IrcitUiEventDispatcher *disp_ptr, IrcitUiEvent *ev_ptr, void *user_ptr);
/** @brief The framework's @c on_focus_changed. Takes the fourth @p has_focus argument. */
PUBLIC_API int
IrcitUiDefaultOnFocusChanged(IrcitUiEventDispatcher *disp_ptr, IrcitUiEvent *ev_ptr, void *user_ptr, int has_focus);
/** @brief The framework's @c on_inflate, run as a node is instantiated. */
PUBLIC_API int
IrcitUiDefaultOnInflate(IrcitUiEventDispatcher *disp_ptr, IrcitUiEvent *ev_ptr, void *user_ptr);
/** @brief The framework's @c on_deflate, run before a node is stripped and freed. */
PUBLIC_API int
IrcitUiDefaultOnDeflate(IrcitUiEventDispatcher *disp_ptr, IrcitUiEvent *ev_ptr, void *user_ptr);
/**
 * @brief Deflate, detach and free @p node_ptr and everything under it.
 *
 * @details The destructor for a tree that was built by @ref IrcitUiClone or by
 *          hand: it fires @c on_deflated, tears the windows down,
 *          removes the node from its parent's child list, and frees the subtree,
 *          its child arrays and any per-kind state. Every pointer to the node, or
 *          to anything under it, dangles afterwards.
 *
 *          It is safe on a subtree that is already deflated, and safe on a node
 *          with no parent -- a clone that was never attached. It is NOT safe on a
 *          node the framework holds a pointer to the top of, and refuses two
 *          cases rather than corrupting the engine:
 *
 *          - the reference model, or anything in it. The generated model wires
 *            its child tables to file-scope arrays, so freeing them would free
 *            static storage.
 *          - the live screen root, which is @ref IrcitUiScreen's @c root. Use
 *            @ref IrcitUiScreenReset for that, which clears the pointers into the
 *            tree as it goes.
 *
 *          A refusal is reported through the log as an error, in addition to the
 *          return value: a caller that ignores the result should still be told.
 *
 * @param node_ptr The root of the subtree to destroy. NULL does nothing.
 *
 * @return 0 on success, -1 if the node was refused.
 *
 * @code{.c}
 * IrcitUiNode *copy = IrcitUiClone(IrcitUiModelRoot());
 * // ... use it ...
 * if (IrcitUiNodeDestroy(copy) != 0) copy = NULL;   // copy is dangling either way
 * @endcode
 *
 * @see IrcitUiClone
 * @see IrcitUiScreenReset
 */
PUBLIC_API int
IrcitUiNodeDestroy(IrcitUiNode *node_ptr);

#ifdef __cplusplus
}
#endif

#endif /* IRCITUI_UI_H */
