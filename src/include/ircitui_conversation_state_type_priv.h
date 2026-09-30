/**
 * @file ircitui_conversation_state_type_priv.h
 * @brief The conversation window's model and its computed geometry. Private: not installed.
 *
 * THE MODEL IS THE DELIVERABLE, NOT A BY-PRODUCT. Every layout decision lives in
 * one array of computed rows -- where each message starts, how tall it is, how
 * wide its body may be, how many lines that body takes, and which corners its
 * cluster shape rounds. The painter, the hit test and the model dump all read
 * that one array and no formula is written twice.
 *
 * That is not tidiness. The design document writes the same arithmetic THREE
 * times -- in the painter, in a row-height evaluator, and again in a click router
 * -- and the three disagree: the router's origin rule differs from the painter's
 * for a single-message inbound bubble, so every token below a name tag is hit one
 * row off. One geometry, three consumers, is the answer to that.
 *
 * NO CURSES IN HERE. The model is arithmetic on the dataset plus the widget's
 * width, so it can be computed, dumped and diffed with no terminal at all -- which
 * is what makes the frozen model dump a reference rather than a screenshot.
 */
#ifndef IRCITUI_IRCITUI_CONVERSATION_STATE_TYPE_PRIV_H
#define IRCITUI_IRCITUI_CONVERSATION_STATE_TYPE_PRIV_H

#include <stdbool.h>
#include <stddef.h>

#include "ircituilib/ircit_ui_node_type.h"
#include "ircituilib/ircitui_conversation.h"
#include "ircituilib/ircitui_conversation_message_type.h"

typedef struct IrcitUiConversationState {
    /* The dataset. Grown by realloc, and NOTHING HOLDS A POINTER INTO IT: the rows
     * below name a message by INDEX, because a realloc moves the array and a
     * retained pointer would be a use-after-free at the next append. */
    IrcitUiConversationMessage *msgs;
    size_t                      count;
    size_t                      cap;

    /* The geometry, recomputed whenever the dataset or the width changes. */
    IrcitUiConversationRow     *rows;
    bool                        rows_valid;
    int                         rows_width; ///< the width rows were computed at
    /* THE OFFSET THE ROWS WERE COMPUTED AT, and it is part of the validity test
     * rather than a cache. The engine's wheel and thumb-drag write
     * container.scroll_y and then ask for a layout -- they do not go through this
     * widget at all -- so a rows-are-valid test that ignored the offset would
     * happily repaint the OLD offset and the bar would move while the text did
     * not. Rows are a function of (width, height, offset); all three are tested. */
    int                         rows_scroll;

    /* The height rows were fitted to. THE VIEWPORT OFFSET IS NOT HERE: it moved to
     * the container's own container.scroll_y, because that is the field the
     * engine's wheel handler and thumb-drag already write, and adopting it is what
     * makes the widget scrollable by machinery nobody wrote. This state owns the
     * CLAMP (the ceiling is geometry, and the geometry is here) and the container
     * owns the number. */
    int                         content_h; ///< the height rows were fitted to

    bool                        is_friend_typing;
    char                        typing_sender_name[IRCITUI_SENDER_NAME_MAX];
    bool                        is_showing_timestamps;
    // 0 lit, 1 dimmed: the pulse's phase, written by Animate and read by the painter.
    int                         anim_phase;

    /* The node's configuration, resolved once: 0 in the node means "not given",
     * and these hold the defaults so no reader has to know them. */
    int                         bubble_max_pct;
    int                         bubble_collapse_lines;
    int                         avatar_gutter_cols;

    // The config the rows were computed at, compared beside rows_width: a value
    // that changed under a live widget must recompute, not be silently ignored.
    int                         rows_gutter;
    int                         rows_max_pct;
    int                         rows_collapse_lines;
    bool                        rows_timestamps;

    // The host's dataset as an ingest contract, zeroed until a host binds one. The
    // widget still owns msgs[]: this says where the snapshot is refreshed FROM.
    IrcitUiConversationAdapter  adapter;
    void                       *host_dataset_ctx;
    // A clone that inherited a bound adapter holds no messages, so its first model
    // pass reconciles before it paints. Cleared by the reconcile.
    bool                        needs_reconcile;
    // The gutter's ink, when a host supplies one. Zeroed means "resolve it here",
    // which is the default and leaves the glyph exactly as it was.
    IrcitUiAvatarProvider       avatar_provider;

    // The reader's watermark: the count the viewport last showed AT ITS TAIL. Only a pass
    // that found the offset at the ceiling moves it, so a parked reader keeps it.
    size_t                      seen_count;
    // Per-direction arrivals past the mark. Only the first is displayed: an OUTBOUND one is
    // the host's own message, and a SYSTEM notice counts as inbound.
    int                         unread_in;
    int                         unread_out;
    // The banner's box, computed here and read flat by the painter AND the hit test, as
    // rows[] is. banner_w == 0 means "no banner".
    int                         banner_y;
    int                         banner_x;
    int                         banner_w;

    /* --- what the two setters wrote, now that the layout's members are gone
     * (IRCITUILIB_GENERATOR_REFACTOR_V1.md §7, the conversation family).
     *
     * The gutter and the ink are the two a host can retarget after the tree
     * exists, and their flat members left IrcitUiCommonState with the rest of
     * the census's 106 B. Zero means "not given" for all three and is the same
     * answer as the default, so no sentinel is needed: a gutter of none and an
     * unstyled banner are what an unseeded node means anyway. */
    int    avatar_gutter_override; /**< IrcitUiConversationWindowSetAvatarGutter's columns, or 0. */
    int    banner_slot;            /**< the banner's ink: the palette slot PLUS ONE, or 0 for the default role. */
    chtype banner_attrs;           /**< and the application's own attribute mask, 0 = plain. */
} IrcitUiConversationState;

IrcitUiConversationState *IrcitUiConversationStateAlloc(void);
void                      IrcitUiConversationStateRelease(IrcitUiConversationState *state_ptr);

/* --- the composite's two internal seams ----------------------------------- */

/**
 * @brief The container's viewport child, made if it is not there yet.
 *
 * THE CONTAINER OWNS EXACTLY ONE CHILD and this is it. It is synthesized rather
 * than declared: it appears in neither @c WidgetType nor @c ContainerType, so a
 * layout naming one is a validator error, and a layout that simply names
 * @c conversation_window gets it.
 *
 * Idempotent, and it LOOKS FOR the child rather than caching a pointer. The
 * recycler's sticky header caches its node in per-clone state, which is safe for
 * the same reason this is: the state is rebuilt with the node. Searching the child
 * array is available here and removes the question entirely -- there is no pointer
 * that can outlive the tree it points into.
 *
 * @param owner_ptr A node of kind @ref IRCITUI_CONVERSATION_WINDOW.
 *
 * @return The child, or NULL when @p owner_ptr is not a conversation window or
 *         the allocation failed.
 */
IrcitUiNode *IrcitUiConversationViewportEnsure(IrcitUiNode *owner_ptr);

/**
 * @brief Compute the model at the box the viewport is about to be given.
 *
 * Called by the engine's packer for this kind, before the child is placed. It
 * primes the geometry AND publishes the container's scroll state -- the clamp and
 * the overflow -- so that the engine's scrollbar, wheel and thumb-drag all read
 * numbers this model produced rather than deriving their own.
 *
 * @param owner_ptr A node of kind @ref IRCITUI_CONVERSATION_WINDOW.
 *
 * @param vp_w Viewport width in cells.
 *
 * @param vp_h Viewport height in cells.
 */
void IrcitUiConversationViewportPrime(IrcitUiNode *owner_ptr, int vp_w, int vp_h);

/**
 * @brief The smallest box in which this widget still reads as a conversation.
 *
 * ONE BUBBLE at the narrowest body the model allows, its walls and padding, the
 * ceiling's worth of lines, and the bar's column where the bar costs one.
 *
 * It exists so the engine's wrap_content measurement ASKS rather than keeping a
 * second copy of these numbers: without a branch, a conversation window falls
 * through to the engine's text-based default and answers ONE COLUMN, which is a
 * wrong answer rather than a conservative one.
 *
 * @param node_ptr A node of kind @ref IRCITUI_CONVERSATION_WINDOW.
 *
 * @param w_out Content width in cells, excluding box chrome.
 *
 * @param h_out Content height in cells, excluding box chrome.
 */
void IrcitUiConversationMinSize(const IrcitUiNode *node_ptr, int *w_out, int *h_out);

#endif /* IRCITUI_IRCITUI_CONVERSATION_STATE_TYPE_PRIV_H */
