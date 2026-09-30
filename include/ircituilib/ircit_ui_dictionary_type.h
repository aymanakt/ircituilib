/**
 * @file ircit_ui_dictionary_type.h
 * @brief A source of word predictions, and the two questions a text field asks it.
 *
 * **THE POINT OF THIS HEADER IS THAT A FIELD DOES NOT KNOW WHAT IS BEHIND IT.** A
 * dictionary may be a hand-written array, a trie of a hundred thousand words, a table of
 * emoji shortcodes, or two of those chained. All four answer the same two questions, and
 * the field that displays their answers is written against the questions rather than
 * against any of the four. That is not tidiness: the alternative is what
 * IRCITUILIB_PREDICTIVE_AUTO_COMPLETE_DESIGN_PROPOSAL.md's section 11 does, reaching through this struct into a
 * trie's private children array to harvest candidates, which makes every future source a
 * change to the popup.
 *
 * **TWO QUERY ENTRIES, NOT ONE, AND THE SECOND IS NOT A CONVENIENCE.** @ref
 * IrcitUiDictionaryInterface::best answers "what one word completes this prefix" -- the
 * inline hint, which needs exactly one. @ref IrcitUiDictionaryInterface::next answers "what
 * is the nth word that completes this prefix" -- the suggestion list, which needs six and
 * must be able to walk them in rank order. A single greedy entry cannot serve a list, and a
 * list-shaped entry alone would make the common case -- every keystroke, for one string --
 * pay for an enumeration it discards.
 *
 * **THE INTERFACE IS THE HOST'S TO IMPLEMENT.** The factories in ui.h (a static list, a
 * trie, an emoji table, a composite) are the ones this library ships; a host that wants a
 * dictionary over its own data structure fills in this struct and attaches it, and the
 * widget cannot tell the difference. @ref IrcitUiDictionaryInterface::destroy is what makes
 * that safe: the library calls it when the host asks it to, and never on its own.
 */
#ifndef IRCITUI_IRCIT_UI_DICTIONARY_TYPE_H
#define IRCITUI_IRCIT_UI_DICTIONARY_TYPE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An opaque prediction source: a private instance and the questions it answers.
 *
 * The two members are public because a host constructing its own dictionary has to fill
 * them in. What @ref instance points at is the host's business; nothing in this library
 * reads it except through @ref api.
 */
typedef struct IrcitUiDictionary IrcitUiDictionary;

/**
 * @brief The vtable a dictionary answers through.
 *
 * Every entry is optional; a NULL entry is answered as "no candidates", so a dictionary
 * that cannot enumerate is still a usable dictionary.
 */
typedef struct
{
  /**
   * @brief The single best completion of @p prefix_ptr, or 0 if there is none.
   *
   * @details The candidate is written into @p out_ptr and must be the **whole word**, not
   *          the missing tail: the widget derives the tail itself, because only it knows how
   *          much of the word the user has already typed at the caret. A dictionary that
   *          returned the tail would be encoding the widget's token rule in every
   *          implementation of this interface.
   *
   *          **A candidate that does not begin with @p prefix_ptr is refused by the caller**,
   *          so a dictionary need not re-check it -- but a dictionary that returns one wastes
   *          the keystroke rather than corrupting the display.
   *
   *          Matching is the dictionary's own policy: case-sensitive, case-folded, or
   *          approximate. The widget does not care, and cannot tell.
   *
   * @param instance_ptr The @ref IrcitUiDictionary::instance this dictionary was built with.
   * @param prefix_ptr   The token under the caret. Not NUL-terminated at the token's end --
   *                     it is a borrowed NUL-terminated string, so a token in the middle of a
   *                     line is passed as its own string by the caller. Never NULL.
   * @param out_ptr      Destination for the whole candidate. Never NULL.
   * @param out_sz       Its capacity in bytes, including the terminator. Never 0.
   *
   * @return 1 if a candidate was written, 0 if there is none.
   */
  int (*best)(void *instance_ptr, const char *prefix_ptr, char *out_ptr, size_t out_sz);

  /**
   * @brief The @p ordinal-th completion of @p prefix_ptr in rank order, or 0 if there is none.
   *
   * @details Rank order is the dictionary's to define -- by weight, by length, by
   *          alphabetical order -- and must be **stable between calls for an unchanged
   *          prefix**, because a caller walks the ordinals in a loop and would otherwise see
   *          a candidate twice and miss another.
   *
   *          Ordinal 0 is the same candidate @ref best returns, so a list can be built
   *          without asking the same question twice in two ways.
   *
   * @param instance_ptr The @ref IrcitUiDictionary::instance this dictionary was built with.
   * @param prefix_ptr   The token under the caret, as for @ref best.
   * @param ordinal      Which match to return, counting from 0.
   * @param out_ptr      Destination for the whole candidate. Never NULL.
   * @param out_sz       Its capacity in bytes, including the terminator. Never 0.
   *
   * @return 1 if a candidate was written, 0 if there is no match at that ordinal.
   */
  int (*next)(void *instance_ptr, const char *prefix_ptr, int ordinal, char *out_ptr, size_t out_sz);

  /**
   * @brief What an accept should write for @p candidate_ptr, or 0 to write the candidate's
   *        own missing tail.
   *
   * @details **A PURE FUNCTION OF THE CANDIDATE, and that is why it is its own entry rather
   *          than an extra out-parameter on the two query calls.** The alternative -- each
   *          query handing the replacement back alongside the candidate -- makes every source
   *          carry a value only one of them has, and makes the widget hold it from one call to
   *          the next. Asked this way a source with no replacements omits this entry
   *          entirely, and the widget writes the candidate's tail, which is the whole of the
   *          default behaviour.
   *
   *          It exists for the emoji case: the candidate a user reads is `:fire:` and the text
   *          they should get is a fire. Without an entry for it the widget would have to know
   *          that some dictionaries substitute, which is the abstraction this header is.
   *
   *          Called once, at accept time, with the candidate the field is showing.
   *
   * @param instance_ptr  The @ref IrcitUiDictionary::instance this dictionary was built with.
   * @param candidate_ptr The candidate being accepted, as a query returned it.
   * @param out_ptr       Destination for the replacement text.
   * @param out_sz        Its capacity in bytes, including the terminator.
   *
   * @return 1 if a replacement was written, 0 to write the candidate's own missing tail.
   */
  int (*replace)(void *instance_ptr, const char *candidate_ptr, char *out_ptr, size_t out_sz);

  /**
   * @brief Release this dictionary's instance and everything it owns.
   *
   * @details Called by @ref IrcitUiDictionaryDestroy, which is called by whoever built the
   *          dictionary. **A field never calls it.** Attaching a dictionary to an edit field
   *          is a borrow, exactly as IrcitUiNodeSetText's string is: the owner outlives every
   *          field that points at it, and a field that freed what it borrowed would take the
   *          dictionary out from under the second field that was attached to it.
   *
   *          **A composite does not destroy its members**, for the same reason: a source may
   *          be in two chains. Destroy each member after every composite naming it is gone.
   *
   * @param instance_ptr The @ref IrcitUiDictionary::instance this dictionary was built with.
   *                     May be NULL, in which case nothing happens.
   */
  void (*destroy)(void *instance_ptr);
} IrcitUiDictionaryInterface;

/**
 * @brief A prediction source: an instance, and the vtable that answers for it.
 *
 * Constructed by the host -- directly, or through one of this library's factories -- and
 * attached to a field with @ref IrcitUiEditTextSetDictionary. The struct itself is the
 * host's to allocate; the library only ever stores a pointer to it.
 */
struct IrcitUiDictionary
{
  void                    *instance; ///< The host's data. Opaque to this library.
  IrcitUiDictionaryInterface api;    ///< The questions it answers.
};

#ifdef __cplusplus
}
#endif

#endif /* IRCITUI_IRCIT_UI_DICTIONARY_TYPE_H */
