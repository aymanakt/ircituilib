/**
 * @file ircit_ui_bar_label_mode_enum.h
 * @brief Where a progress bar puts its percentage: the vocabulary the label is asked for in.
 *
 * A mode is a decision about LAYOUT, not about painting, and that is the whole reason it
 * is an enum on the node rather than a flag consulted while drawing. Two of the three
 * modes change how much of the row the bar itself may use:
 *
 *   off        the bar takes the whole content box
 *   adjacent   the label is reserved to the right, so the BAR IS NARROWER by exactly the
 *              string's width -- and a hit test that seeks by column must divide by that
 *              same reduced width or it lands on the wrong cell
 *   centered   the label is drawn over the bar, so the bar keeps the full width
 *
 * Encoding that as a paint-time flag instead would leave the bar's width to be decided
 * during the render pass, which is after IrcitUiRealize has already sized the window and
 * after any hit test that ran against the node's geometry. The three modes therefore
 * resolve to a bar width at layout time, and the painter only draws.
 *
 * **A fourth mode is deliberately absent.** The media player's time label ("02:43 /
 * 05:00") is a label mode in the design document, but nothing implements it yet, and a
 * key that resolves to nothing is a trap rather than a reservation -- the rule C23 states
 * for `theme_class`. It is added when it is drawn.
 *
 * @ref IRCITUI_BAR_LABEL_OFF is 0 and is the default, so a node that never mentions the
 * key -- a hand-built def, a clone, a zeroed pool entry -- is a bar with no label, which
 * is what a progress bar was before this vocabulary existed.
 */
#ifndef IRCITUI_IRCIT_UI_BAR_LABEL_MODE_ENUM_H
#define IRCITUI_IRCIT_UI_BAR_LABEL_MODE_ENUM_H

/*!
 * @enum IrcitUiBarLabelMode
 * @brief Where a bar's percentage is drawn, and so how much of the row the bar keeps.
 *
 * The YAML spelling is `label_mode:`; see C24 in tools/layout-grammar.ebnf.
 */
typedef enum {
    IRCITUI_BAR_LABEL_OFF = 0, ///< No label. The bar takes the whole content box. **The default.**
    IRCITUI_BAR_LABEL_ADJACENT,///< `" 47% "` to the right of the bar, which shrinks by that width.
    IRCITUI_BAR_LABEL_CENTERED,///< `" 47% "` drawn over the bar's middle, which keeps its width.
    IRCITUI_BAR_LABEL_COUNT    ///< The fence: one past the last mode.
} IrcitUiBarLabelMode;

#endif /* IRCITUI_IRCIT_UI_BAR_LABEL_MODE_ENUM_H */
