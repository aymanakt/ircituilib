/**
 * @file ircit_ui_kind_enum.h
 * @brief Widget and container kinds.
 */
#ifndef IRCITUI_IRCIT_UI_KIND_ENUM_H
#define IRCITUI_IRCIT_UI_KIND_ENUM_H

/*!
 * @enum IrcitUiKind
 * @brief Widget or container type stored in @ref IrcitUiNode::kind.
 */
typedef enum {
    IRCITUI_TEXT_VIEW = 0,   ///< Static or selectable text block.
    IRCITUI_BUTTON,          ///< Clickable caption; may name a layout_template.
    IRCITUI_IMAGE_VIEW,      ///< PNG drawn as half-blocks or Braille.
    IRCITUI_EDIT_TEXT,       ///< Single-line editor; buffer in @ref IrcitUiNode::ebuf.
    IRCITUI_CHECK_BOX,       ///< Independent on/off item (usually inside a group).
    IRCITUI_RADIO_BUTTON,    ///< Exclusive item (usually inside a group).
    IRCITUI_SWITCH,          ///< Two-state toggle widget.
    IRCITUI_PROGRESS_BAR,    ///< Determinate bar (decorative).
    IRCITUI_SEEK_BAR,        ///< Horizontal value track (decorative).
    IRCITUI_EMOJI_TABLE,     ///< Grid of emoji; click appends to the input field.
    IRCITUI_LINEAR,          ///< Pack children in @ref IrcitUiNode::orientation.
    IRCITUI_FRAME,           ///< Children stacked in the content box.
    IRCITUI_CONSTRAINT,      ///< Android-style edge constraints.
    IRCITUI_RADIO_GROUP,     ///< Linear of radio_button; one IRCITUI_F_CHECKED.
    IRCITUI_CHECK_GROUP,     ///< Linear of check_box; many IRCITUI_F_CHECKED.
    IRCITUI_RECYCLER,        ///< Virtualised list: a pool of row nodes, each
                             ///< cloned from common.item_template and rebound to
                             ///< a dataset index as the viewport scrolls. The
                             ///< children are the pool, not the dataset.
    IRCITUI_CONVERSATION_WINDOW,
                             /**< A messaging conversation: asymmetric bubbles
                              *  anchored to the sender's side, cluster tails,
                              *  reply headers, edge-fused reaction badges,
                              *  delivery marks and a typing row -- every one of
                              *  them derived from the messages the application
                              *  appends, so nothing about an individual message
                              *  appears in a layout. See C25.
                              *
                              *  A CONTAINER, and it owns exactly one child:
                              *  @ref IRCITUI_MESSAGE_VIEWPORT, which it
                              *  synthesizes in the layout pass. The rows are
                              *  still not nodes -- a conversation's message
                              *  count is unbounded and its rows have their own
                              *  heights -- so the one child is the whole
                              *  matrix, and a layout declaring @c children: on
                              *  it is still refused. What the container buys is
                              *  the engine's own frame (a bordered LEAF is never
                              *  framed) and the engine's own scrollbar, wheel
                              *  and thumb-drag, which key on this kind answering
                              *  true to @ref IrcitUiIsContainer.
                              *
                              *  It is therefore the FIRST kind that is an engine
                              *  container while not being a grammar
                              *  @c ContainerType. The two lists are otherwise
                              *  kept in step; here they must not be, because the
                              *  child is the widget's own. */
    IRCITUI_MESSAGE_VIEWPORT
                             /**< The conversation's message matrix, and the ONLY
                              *  thing that paints bubbles. Always a child of an
                              *  @ref IRCITUI_CONVERSATION_WINDOW, synthesized by
                              *  that widget and never declared in a layout: it
                              *  appears in neither @c WidgetType nor
                              *  @c ContainerType, so a layout naming it is a
                              *  validator error.
                              *
                              *  STATELESS, deliberately. The model lives on its
                              *  parent, so this node owns nothing and nothing has
                              *  to be allocated, freed or re-bound for it across
                              *  @ref IrcitUiScreenReset. It exists so that the
                              *  matrix has its own window -- which clips the
                              *  bubbles by construction, gives the hit test a node
                              *  to resolve to, and leaves the container's last
                              *  column to the scrollbar. */
} IrcitUiKind;

#endif /* IRCITUI_IRCIT_UI_KIND_ENUM_H */
