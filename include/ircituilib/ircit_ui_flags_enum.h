/**
 * @file ircit_ui_flags_enum.h
 * @brief Bit flags stored in @ref IrcitUiNode::flags.
 *
 * The enumeration is unnamed: these are flag values, always used by their
 * IRCITUI_F_* spelling and never as an enumerated type, so no tag is declared.
 */
#ifndef IRCITUI_IRCIT_UI_FLAGS_ENUM_H
#define IRCITUI_IRCIT_UI_FLAGS_ENUM_H

/*!
 * @brief Bit flags stored in @ref IrcitUiNode::flags.
 */
enum {
    IRCITUI_F_VISIBLE    = 1u << 0, ///< Participates in hit-test and draw.
    IRCITUI_F_ENABLED    = 1u << 1, ///< May receive input: the framework's kind-routed defaults and @ref IrcitUiBindClick both decline on a node without it.
    IRCITUI_F_CLICKABLE  = 1u << 2, ///< Prefer click dispatch over drag.
    IRCITUI_F_FOCUSED    = 1u << 3, ///< Equals the global @ref IrcitUiFocus; set by @ref IrcitUiSetFocus.
    IRCITUI_F_CHECKED    = 1u << 4, ///< On for check/radio/switch.
    IRCITUI_F_BORDER     = 1u << 5, ///< Draw an ACS box; shrinks content box.
    IRCITUI_F_DRAWN      = 1u << 6, ///< Set after a successful paint.
    IRCITUI_F_STACKED    = 1u << 7, ///< Popup layer; uses its own newwin.
    IRCITUI_F_DRAGGABLE  = 1u << 8, ///< Container may be moved with the mouse.
    IRCITUI_F_SCROLLABLE = 1u << 9, ///< Overflow may scroll; optional bars.
    IRCITUI_F_CLIPPED    = 1u << 10, ///< Given no space; gets no window and no paint.
    IRCITUI_F_CHILDREN_SELECTABLE = 1u << 11, /**< Container whose children are a
                                             keyboard-navigable list: @ref
                                             IrcitUiNode::sel0 is the cursor child,
                                             Up/Down move it and Enter activates
                                             it. The container-side counterpart
                                             of a group's highlight row, and
                                             meaningful only on container kinds;
                                             a group kind that also carries it
                                             keeps group semantics, since the
                                             group key block runs first. Not
                                             supported together with @ref
                                             IRCITUI_F_SCROLLABLE, whose child offset
                                             breaks the child-index-to-row
                                             mapping. */
    IRCITUI_F_COLLAPSIBLE = 1u << 12, /**< Container may be folded to a one-cell
                                       strip along its cross axis (height for a
                                       horizontal container, width for a
                                       vertical one), with a clickable
                                       indicator glyph that is visible in both
                                       states. Set from a layout file's
                                       `collapsible`. Meaningful only on
                                       container kinds; the folded state
                                       itself is @ref
                                       IrcitUiContainerState::is_collapsed. */
};

#endif /* IRCITUI_IRCIT_UI_FLAGS_ENUM_H */
