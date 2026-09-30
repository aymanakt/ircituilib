/**
 * @file ircitui_decl_override_type_priv.h
 * @brief The declared values a setter writes for a kind that keeps no state of
 *        its own.
 *
 * WHY THIS EXISTS. Two of the census's members -- a toggle's mark ink and a
 * container's packing axis -- are declared in the layout's block, which is const
 * and shared between nodes that declared the same thing, so a setter cannot write
 * there. Their flat members left IrcitUiCommonState with the rest of the 106 B
 * (IRCITUILIB_GENERATOR_REFACTOR_V1.md §7), and the kinds that carry them -- the
 * five toggles and the six containers -- are kinds that have no widget_state:
 * a bar, a recycler, a conversation window and an edit field each have one
 * because each has real per-node state, and these do not.
 *
 * So this is the smallest state the tree has: one int per member, allocated for
 * those eleven kinds and for nothing else. A node of any other kind carries the
 * block and the defaults, and never a setter's override, because no setter can
 * reach it: IrcitUiWidgetDef's mark_fg is refused on a kind that draws no mark
 * (C20) and its orientation on a kind that does not pack along one.
 *
 * -1 MEANS "NOT GIVEN" FOR BOTH, and for the reason every other sentinel in this
 * tree gives: 0 is a legal value for each -- IRCITUI_VERT is 0, and COLOR_BLACK
 * is a colour a mark may legitimately be -- so a zeroed state would shadow a
 * declared value rather than defer to it.
 */
#ifndef IRCITUI_IRCITUI_DECL_OVERRIDE_TYPE_PRIV_H
#define IRCITUI_IRCITUI_DECL_OVERRIDE_TYPE_PRIV_H

typedef struct {
  int mark_fg;     /**< Colour of a toggle's mark, or -1: the mark takes text_colour. */
  int orientation; /**< The axis a container packs along, or -1: the block's value, then IRCITUI_VERT. */
} IrcitUiDeclOverrides;

#endif /* IRCITUI_IRCITUI_DECL_OVERRIDE_TYPE_PRIV_H */
