/**
 * @file ircitui_conversation.h
 * @brief The conversation window: the application's side of it.
 *
 * A conversation_window node is declared in a layout and its semantics are the
 * widget's. What the application does is hand it messages, and tell it the things
 * that are not messages: that someone is typing, whether the stamps are showing,
 * how wide the avatar gutter is, and -- by resolving a cell -- which token a click
 * landed on.
 *
 * EVERY MUTATOR TAKES THE NODE, by id lookup at the call site: IrcitUiFind. That
 * is the tree's convention for a node the application owns rather than one the
 * framework routes to (see IrcitUiBindClick, IrcitUiBindKey, IrcitUiBindSeek,
 * which take ids because they are REGISTRATIONS and must survive a clone).
 *
 * WHICH NODE TO PASS: the MODEL, not the screen tree. IrcitUiMainLooper calls
 * IrcitUiScreenReset() before its first frame, which re-clones the tree, so a
 * node captured from the screen is discarded by the next reset. This is the rule
 * the recycler's adapter already follows (ircit_ui_recycler_adapter_type.h:17-22)
 * and it is not discoverable from the outside, so it is stated here.
 */
#ifndef IRCITUI_IRCITUI_CONVERSATION_H
#define IRCITUI_IRCITUI_CONVERSATION_H

#include <ncurses.h>
#include <stddef.h>

#include "ircituilib/ircituilib_defs.h"
#include "ircituilib/ircit_ui_node_type.h"
#include "ircituilib/ircit_ui_conversation_token_target_enum.h"
#include "ircituilib/ircit_ui_palette_slot_enum.h"
#include "ircituilib/ircitui_conversation_message_type.h"

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * @struct IrcitUiConversationAdapter
 * @brief Where a conversation window's messages come from, and how to read one.
 *
 * @details THE HOST OWNS THE DATASET. The widget keeps an owned SNAPSHOT of it and
 *          never reads the host's storage outside a reconcile, which copies what it
 *          keeps. That is not a zero-copy view and it is deliberate: this widget
 *          measures each message once and then paints from the measurement, so the
 *          text it measured must not move before it is drawn. A pull inside the paint
 *          pass would read the same message twice and could draw it at the height
 *          another read measured.
 *
 *          ZERO-INITIALISE IT -- @c "IrcitUiConversationAdapter a = {0};" -- or use a
 *          designated initialiser. Every field but @c get_item_count and
 *          @c get_item_info is optional, and "not set" is spelled NULL, so a member
 *          left indeterminate is not a default: it is a callback the widget may
 *          invoke. The recycler's adapter carries the same warning for the same
 *          reason (ircit_ui_recycler_adapter_type.h:40-47).
 */
typedef struct IrcitUiConversationAdapter {
  /*! How many messages the dataset holds. Required: without it or
   *  @ref get_item_info the widget declines the adapter whole. */
  size_t (*get_item_count)(IrcitUiNode *node_ptr, void *host_ctx);

  /*! Fill @p out_ptr with message @p index. Return 1, or 0 to decline the index.
   *
   *  Called once per index per reconcile, and the widget COPIES the two strings on
   *  return, so @p out_ptr's pointers may be into a buffer the host reuses -- which
   *  is what a network reader has (ircitui_conversation_message_type.h:54-60).
   *
   *  @p out_ptr arrives zeroed. @c message_id IS REQUIRED and must be STABLE for a
   *  message's lifetime: it is how a reconcile recognises a message it already
   *  holds, so that a refresh does not throw the user's collapse toggles away.
   *  @c sender_name and @c timestamp are inline arrays, copied by value, and are NOT
   *  borrowed pointers. @c is_collapsed is carried across from the message it
   *  matches and is otherwise computed; @c cluster_pos is computed and overwritten
   *  (ircit_ui_cluster_position_enum.h). */
  int (*get_item_info)(IrcitUiNode *node_ptr, void *host_ctx, size_t index,
                       IrcitUiConversationMessage *out_ptr);
} IrcitUiConversationAdapter;

/*!
 * @struct IrcitUiAvatarProvider
 * @brief Where the gutter's one cell of ink comes from.
 *
 * @details PAINT ONLY. The avatar block is a fixed width and height and the glyph
 *          fills one cell of its middle row, so nothing here can move a row's height
 *          or a box's column: installing or replacing a provider invalidates paint
 *          and does not recompute geometry. That is what makes this seam cheap, and
 *          it is also its limit -- an avatar cannot be made wider without becoming a
 *          geometry input.
 */
typedef struct IrcitUiAvatarProvider {
  /*! Return 1 and put ONE DISPLAY COLUMN of UTF-8 in @p out_ptr, or return 0 to
   *  decline and take the widget's own resolution.
   *
   *  THE BUDGET IS COLUMNS, NOT BYTES, AND IT IS ONE. The widget re-validates with
   *  mbrtowc + wcwidth exactly as its own resolver does and falls back on a
   *  violation, so a provider cannot break the grid -- but a two-column glyph is
   *  simply never used, and an emoji is two columns (U+1F469 and its family are all
   *  wide). @p n is the byte capacity, which is 8: one four-byte sequence and its
   *  terminator.
   *
   *  @p msg_ptr carries what identity there is: @c sender_name is where the widget's
   *  own resolution takes the initial from, and @c message_id is what it falls back
   *  to. Both are read-only and borrowed for the call. */
  int (*resolve_avatar_glyph)(const IrcitUiConversationMessage *msg_ptr,
                              char *out_ptr, size_t n, void *host_ctx);
  void *host_ctx;
} IrcitUiAvatarProvider;

/*!
 * @brief Append one message. The widget COPIES the fields it keeps.
 *
 * @details The strings @c text_content and @c reply_snippet are copied, so the
 *          caller owns what it passed and may free it the moment this returns --
 *          which is what a network reader with a reused buffer needs. The node's
 *          @c cluster_pos is computed here and overwrites whatever the caller
 *          set; see ircit_ui_cluster_position_enum.h.
 *
 * @return 1 on success, 0 if the node is not a conversation window or the stub's
 *         fixed capacity is full. The capacity is a Gate 2 property and goes with
 *         the stub.
 */
PUBLIC_API int IrcitUiConversationWindowAppend(IrcitUiNode *node_ptr,
                                               const IrcitUiConversationMessage *msg_ptr);

/*!
 * @brief Show or hide the typing row, and name who is typing.
 *
 * @param node_ptr    The conversation window.
 * @param is_typing   Non-zero to show the row.
 * @param sender_ptr  Who is typing; NULL keeps the name already held.
 */
PUBLIC_API void IrcitUiConversationWindowSetTyping(IrcitUiNode *node_ptr, int is_typing, const char *sender_ptr);

/*!
 * @brief Reveal or hide the timestamps: one dimmed row inside every bubble.
 *
 * @details A flag rather than a per-message value, and a row rather than a margin:
 *          the stamp closes each bubble, at the column the body's text starts at.
 *          A system notice has no box, so it carries none.
 */
PUBLIC_API void IrcitUiConversationWindowToggleTimestamps(IrcitUiNode *node_ptr, int visible);

/*!
 * @brief Reserve @p cols columns at the viewport's left edge for inbound avatars.
 *
 * @details 0 means none, which is the default: a widget no layout has given a gutter
 *          draws exactly as it did before avatars existed. Clamped, like the key.
 */
PUBLIC_API void IrcitUiConversationWindowSetAvatarGutter(IrcitUiNode *node_ptr, int cols);

/*!
 * @brief Whether anything wants a tick: 1 while the typing sender's own avatar
 *        pulses, 0 otherwise. The cadence belongs to the caller.
 */
PUBLIC_API int IrcitUiConversationWindowAnimationDue(const IrcitUiNode *node_ptr);

/*!
 * @brief Set the pulse's phase: 0 lit, 1 dimmed.
 *
 * @details Paint only: the phase moves no cell, so the geometry it was computed at
 *          stands and the model dump is identical across a flip.
 */
PUBLIC_API void IrcitUiConversationWindowAnimate(IrcitUiNode *node_ptr, int phase);

/*!
 * @struct IrcitUiConversationRow
 * @brief One message's computed geometry. THE model, and what the reference dumps.
 *
 * Every field is derived: the widget computes it from the dataset and its own
 * width, and an application reads it rather than setting it. Two consumers exist
 * today -- the painter and the model dump -- and the hit test will be the third,
 * which is why the row origins are here and not inside the painter.
 */
typedef struct IrcitUiConversationRow {
    size_t                 msg_index;   ///< Which message this row is.
    int                    origin_y;    ///< Its top, in the widget's content box. May be negative.
    int                    height;      ///< Its height in cells.
    int                    max_width;   ///< The reflow ceiling: the bubble's widest allowed body.
    int                    body_cols;   ///< The body's actual width.
    int                    body_lines;  ///< Lines the body occupies, after any truncation.
    int                    full_lines;  ///< Lines it would take untruncated. Equal to body_lines unless truncated.
    int                    name_row;    ///< 1 when a sender name tag is drawn above the body.
    int                    reply_rows;  ///< 1 when a reply header is drawn, else 0.
    int                    badge_row;   ///< 1 when a reaction badge row is allocated, else 0.
    int                    box_top;     ///< 1 when the bubble's top rule is drawn.
    int                    box_bottom;  ///< rows below the body: the bottom rule, and the tail when the run ends.
    /* THE BUBBLE'S HORIZONTAL EXTENT, computed with everything else and for the
     * same reason: the painter and the hit test both need it, and a formula kept
     * in two places is the defect this model exists to prevent.
     *
     * It was previously left out on the grounds that it "depends on painter-time
     * state" -- the timestamps margin shifts it and an outbound bubble reserves
     * three cells for its status mark. Both of those are known when the model runs:
     * the margin is the widget's own flag and the mark follows from the message's
     * direction and status. They are read here instead of being recomputed later.
     *
     * A SYSTEM notice uses the pair too: its centred line's span, which is what the
     * painter centres on. */
    int                    box_x;       ///< The bubble's left column, in the viewport.
    int                    box_w;       ///< Its width in cells.
    IrcitUiClusterPosition cluster;     ///< COMPUTED. The caller's value is overwritten.
    bool                   truncated;   ///< The body exceeds the collapse ceiling.
    // Appended, so no existing offset moves. The avatar's ink may extend past
    // origin_y + height: a one-line block top is three rows, and so is the avatar.
    int                    avatar_x;    ///< Its left column, or -1 when none is drawn.
    int                    avatar_y;    ///< Its first content row; may be negative.
    int                    avatar_rows; ///< 3 when drawn, 0 when not.
    int                    avatar_pulse;///< 1 on the typing sender's own avatar.
    int                    name_x;      ///< The name tag's left column, or 0.
    int                    name_w;      ///< Its width, capped at the box's own edge.
    int                    stamp_row;   ///< 1 when the timestamp row is drawn, else 0.
} IrcitUiConversationRow;

/*!
 * @brief Read message @p index as the widget holds it.
 *
 * @details The two string pointers are the widget's own copies, BORROWED for the
 *          duration of the call and valid until the message is released -- which
 *          is what makes a model dump possible without the dump re-reading the
 *          fixture. Copy them if they are to outlive the node.
 *
 * @return 1 when the message exists and @p out_ptr was filled, 0 otherwise.
 */
PUBLIC_API int IrcitUiConversationWindowMessageInfo(const IrcitUiNode *node_ptr, int index,
                                                    IrcitUiConversationMessage *out_ptr);

/*!
 * @brief The row a cell falls in, or -1 for none.
 *
 * @details @p y and @p x are LOCAL to the widget's content box: subtract the
 *          node's geometry before calling. This is the THIRD consumer of the
 *          geometry the model computes -- the painter and the row accessors are
 *          the other two -- and it exists so that a click can name a message
 *          without anybody recomputing where the messages are.
 *
 *          @p x IS NOW USED, against the row's own @ref IrcitUiConversationRow::box_x
 *          and @c box_w. It previously was not, and the comment here said the bubble's
 *          horizontal extent could not be modelled because it depends on painter-time
 *          state; both of those inputs turned out to be known when the model runs, so
 *          the extent moved into the model and this became a real hit box. A cell
 *          outside the bubble now names no row, where it used to name one.
 */
PUBLIC_API int IrcitUiConversationWindowRowAt(const IrcitUiNode *node_ptr, int y, int x);

/*!
 * @brief What the cell at @p y , @p x belongs to, and which message it is on.
 *
 * @details The same geometry @ref IrcitUiConversationWindowRowAt reads, taken one
 *          step further: a message is not one target. The name tag, the reply
 *          header, the body, the badges and the box's rules are stacked at heights
 *          the model computes, and this says which of them a cell landed on.
 *
 *          @p y and @p x are LOCAL to the widget's content box, as @ref
 *          IrcitUiConversationWindowRowAt's are.
 *
 *          @ref IRCITUI_TOKEN_NONE IS A REAL ANSWER. A cell outside every bubble --
 *          the padding, the gutter, the timestamps margin -- belongs to nothing, and
 *          returning the nearest message instead would route clicks to a bubble the
 *          pointer never touched. That is a defect this tree has already recorded
 *          once, in the design document's own router.
 *
 * @param node_ptr The conversation window, or its viewport child.
 *
 * @param y Row, local to the widget's content box.
 *
 * @param x Column, local to the same box.
 *
 * @param msg_out Set to the message id under the cell when one is found; left
 *        untouched when the return is @ref IRCITUI_TOKEN_NONE. May be NULL.
 *
 * @return What the cell belongs to.
 */
PUBLIC_API IrcitUiConversationTokenTarget
IrcitUiConversationWindowResolveTokenAt(const IrcitUiNode *node_ptr, int y, int x, uint64_t *msg_out);

/*!
 * @brief Act on a click at a SCREEN cell: open or collapse the message under it.
 *
 * @details The engine's click dispatch calls this when a click lands on a
 *          conversation window, and it is public because a host may legitimately
 *          want to synthesise one -- from a key, from a scripted walkthrough, or
 *          from its own input handling. It takes screen cells rather than local
 *          ones so the engine does not have to know the widget's box, and it does
 *          the geometry subtraction itself.
 *
 * @details A cell on an avatar is declined, as a status indicator's is: the widget's
 *          only gesture is collapse and an avatar has no collapse state, so the tap
 *          belongs to the application's own handling.
 *
 * @return 1 when a row was found and its collapse toggled, 0 when the cell is
 *         outside the widget, on no row, or on an avatar.
 */
PUBLIC_API int IrcitUiConversationWindowHandleClick(IrcitUiNode *node_ptr, int screen_y, int screen_x);

/*!
 * @brief Collapse or expand one message in place.
 *
 * @details A collapsed message is drawn to the collapse ceiling with an ellipsis;
 *          an expanded one is drawn in full however long it is. The geometry is
 *          recomputed, so every row below moves.
 *
 * @return 1 when the message exists and the flag was flipped.
 */
PUBLIC_API int IrcitUiConversationWindowToggleCollapse(IrcitUiNode *node_ptr, int index);

/*!
 * @brief Set the unread banner's ink: a palette role for its colours, an attribute mask for
 *        its emphasis.
 *
 * @details A ROLE AND A MASK, NOT A COLOUR. The slot is one of the palette's own roles, so
 *          the same call draws on a 256-colour terminal, on an eight-colour one and on one
 *          with no colour at all. The mask REPLACES what the role would have carried rather
 *          than adding to it, so a mask of 0 is the role's colours and nothing more -- which
 *          is what the banner is until this is called. The role's own attributes are not
 *          applied: an attribute the application did not ask for is one it did not choose.
 *
 *          TAKES EFFECT ON THE NEXT FRAME, and nothing else needs to be called: a style
 *          moves no cell, so there is no relayout, only a repaint of the banner and of every
 *          cell it covers. A caller that wants the glass updated before this returns asks
 *          for a frame itself (@ref IrcitUiRenderFrame).
 *
 *          SET IT ON THE NODE YOU PAINT for an immediate effect. The style is a node field,
 *          so a call on the model reaches a clone built afterwards and not one already on
 *          the glass -- the rule every node field in this widget follows.
 *
 * @param node_ptr The conversation window, or its viewport.
 *
 * @param slot The palette role for the banner's colours. A value that names no role is
 *             refused and the ink is left as it was.
 *
 * @param attrs Attributes OR'd onto the role's colours -- @c A_BOLD, @c A_REVERSE,
 *              @c A_DIM -- or 0 for plain.
 */
PUBLIC_API void IrcitUiConversationWindowSetNewMsgBannerStyle(IrcitUiNode *node_ptr, IrcitUiPaletteSlot slot,
                                                              chtype attrs);

/*!
 * @brief How many rows the model holds: one per message.
 */
PUBLIC_API int IrcitUiConversationWindowRowCount(const IrcitUiNode *node_ptr);

/*!
 * @brief How many unread inbound arrivals the banner is counting, as of now.
 *
 * @details The same number the banner displays, so a host can act on it -- style the banner,
 *          badge its own chrome, or decide to follow the tail -- without deriving a second
 *          count of its own that could drift from the widget's.
 *
 *          IT BRINGS THE MODEL CURRENT, which is what makes it answer about the dataset a
 *          host has just notified about rather than about the previous frame.
 *
 * @param node_ptr The conversation window, or its viewport.
 *
 * @return The arrivals past the reader's watermark that are not the host's own: 0 when the
 *         reader is at the tail, and 0 for a node holding no model.
 */
PUBLIC_API int IrcitUiConversationWindowUnreadCount(const IrcitUiNode *node_ptr);

/*!
 * @brief Read row @p index's computed geometry.
 *
 * @return 1 when the row exists and @p out_ptr was filled, 0 otherwise.
 */
PUBLIC_API int IrcitUiConversationWindowRowInfo(const IrcitUiNode *node_ptr, int index,
                                                IrcitUiConversationRow *out_ptr);

/*!
 * @brief Scroll the viewport by @p delta_rows, positive being back into history.
 *
 * @details The scroll is in ROWS and not messages, because rows have different
 *          heights -- so no caller can be asked to know how far one message moves
 *          the viewport. The clamp is "the first message is at the top", which is
 *          the only ceiling that cannot scroll into blank space.
 *
 * @return the offset actually taken.
 */
PUBLIC_API int IrcitUiConversationWindowScroll(IrcitUiNode *node_ptr, int delta_rows);

/*!
 * @brief Point a conversation window at a host-owned dataset.
 *
 * @details BIND THE MODEL NODE, not the screen tree. IrcitUiMainLooper calls
 *          IrcitUiScreenReset() before its first frame, which re-clones the tree, so
 *          a binding made on a node taken from the screen is discarded -- the same
 *          rule the recycler's adapter follows (ircit_ui_recycler_adapter_type.h:17-22).
 *          A screen clone inherits the binding when its state is allocated and reads
 *          the dataset on its first model pass.
 *
 *          NULL @p adapter_ptr detaches and releases the snapshot, leaving the widget
 *          as a widget no host has bound one to.
 *
 * @return 1 when installed, 0 when the node is not a conversation window with state.
 */
PUBLIC_API int IrcitUiConversationWindowBindAdapter(IrcitUiNode *node_ptr,
                                                    const IrcitUiConversationAdapter *adapter_ptr,
                                                    void *host_ctx);

/*!
 * @brief The host's dataset has changed. Re-read it.
 *
 * @details TAKES EITHER NODE -- a model node or a screen node; the id is resolved to
 *          both, so a host never has to know which tree it is holding, and every live
 *          state is reconciled.
 *
 *          Reconciling is KEYED ON @c message_id: a message the widget already holds
 *          keeps its collapse state and has its strings replaced; one it does not hold
 *          is appended under the same rule @ref IrcitUiConversationWindowAppend
 *          applies; one it holds and the dataset no longer names is released.
 *
 *          Cheap by contract: both callbacks must be side-effect free, must not mutate
 *          the dataset, and must return the same content for the same index within one
 *          call.
 */
PUBLIC_API void IrcitUiConversationWindowNotifyDatasetChanged(IrcitUiNode *node_ptr);

/*!
 * @brief Where the gutter's ink comes from, or NULL to take the widget's own.
 *
 * @details Paint only, and it needs no geometry pass: the block's size does not
 *          depend on the glyph. The widget re-validates whatever the provider returns
 *          on the same one-column rule its own resolution obeys, so a wider glyph is
 *          declined rather than allowed to break the grid. See @ref
 *          IrcitUiAvatarProvider.
 *
 *          The provider is inherited by a screen clone exactly as the adapter is, so
 *          install it on the model node for the same reason.
 *
 * @return 1 when installed, 0 when the node is not a conversation window with state.
 */
PUBLIC_API int IrcitUiConversationWindowSetAvatarProvider(IrcitUiNode *node_ptr,
                                                          const IrcitUiAvatarProvider *provider_ptr);

/*!
 * @brief Draw the widget. Called by the engine's paint dispatch, not by an application.
 *
 * @details Declared here rather than in a private header because the engine's
 *          unit needs it at the switch that routes painting by kind, and it is
 *          the only entry point in this file that is not the application's.
 */
PUBLIC_API void IrcitUiConversationWindowPaint(IrcitUiNode *node_ptr);

#ifdef __cplusplus
}
#endif

#endif /* IRCITUI_IRCITUI_CONVERSATION_H */
