/**
 * @file ircit_ui_widget_def_type.h
 * @brief Argument bag for the C widget factory.
 */
#ifndef IRCITUI_IRCIT_UI_WIDGET_DEF_TYPE_H
#define IRCITUI_IRCIT_UI_WIDGET_DEF_TYPE_H

#include "ircituilib/ircit_ui_kind_enum.h"
#include "ircituilib/ircit_ui_orient_enum.h"
#include "ircituilib/ircit_ui_size_mode_enum.h"

/*!
 * @struct IrcitUiWidgetDef
 * @brief Argument bag for the C widget factory.
 *
 * Passed by pointer to @ref IrcitUiAddChild, @ref IrcitUiAddButton and
 * @ref IrcitUiCreateFloatingWindow. A caller fills in the whole struct, so a
 * designated initializer with only the wanted members listed is the intended
 * use: every member left out is zero, which is the documented "unset" value
 * for each.
 *
 * This bag carries layout and appearance only. It has no listener members, and
 * that is deliberate rather than unfinished: the framework binds its own
 * defaults to every node it constructs, and an application that wants to change
 * what a node does registers that decision against the node's id with
 * @ref IrcitUiBindClick, or writes the node's interactions_user block after the
 * tree exists. A node's behaviour is therefore always discoverable in one of
 * those two places, never split between them and an argument passed at
 * construction.
 */
typedef struct IrcitUiWidgetDef {
    const char             *id;          ///< Node id; the key @ref IrcitUiFind searches on.
    IrcitUiKind             kind;        ///< Kind to construct.
    const char             *text;        ///< Caption, or path for an image_view.
    IrcitUiOrient           orientation; ///< Packing axis, for a container built
                                         ///< here. Meaningful on the kinds
                                         ///< @ref IrcitUiIsContainer accepts;
                                         ///< ignored elsewhere. Zero is
                                         ///< @ref IRCITUI_VERT, so a designated
                                         ///< initializer that omits it gets the
                                         ///< top-to-bottom default the runtime
                                         ///< has always applied. Also picks the
                                         ///< axis a collapsible container folds
                                         ///< along - see @ref
                                         ///< IrcitUiSetCollapsed.
    IrcitUiSizeMode         width_mode;  ///< How @ref width is interpreted.
    int                     width;       ///< EXACT width in cells.
    int                     width_percent;  ///< Share of the parent's width when
                                            ///< @ref width_mode is
                                            ///< @ref IRCITUI_SIZE_PERCENT; 0-100.
    IrcitUiSizeMode         height_mode; ///< How @ref height is interpreted.
    int                     height;      ///< EXACT height in cells.
    int                     height_percent; ///< Share of the parent's height when
                                            ///< @ref height_mode is
                                            ///< @ref IRCITUI_SIZE_PERCENT; 0-100.
    unsigned                flags;       ///< Initial IRCITUI_F_* bits.
    short                   fg;          ///< Foreground colour 0-7.
    short                   bg;          ///< Background colour 0-7.
    short                   bdr;         ///< Border colour 0-7.
    short                   mark_fg;     ///< Colour of a toggle's mark -- the
                                         ///< tick, the circle, the switch's knob --
                                         ///< or -1 for "use @ref fg". The YAML key
                                         ///< is @c mark_colour, and it is refused
                                         ///< on a kind that has no mark (C20).
                                         ///< **-1 is the unset value, not 0**:
                                         ///< black is a colour a mark may be.
    const char *            theme_class; ///< The palette slot this node wears, by
                                         ///< name, or NULL for none. The name is one
                                         ///< of the nine in
                                         ///< ircituilib/ircit_ui_palette_slot_enum.h
                                         ///< -- see C23 -- and the node draws in
                                         ///< that slot's colours, resolved at paint
                                         ///< time. **Borrowed, never copied**: like
                                         ///< @ref id and @ref text this must point at
                                         ///< storage that outlives the node, and it
                                         ///< must be zero-initialised, because a
                                         ///< garbage pointer is indistinguishable
                                         ///< from a good one and the palette will
                                         ///< read it. ///
    const void *            declarative_attributes_aux; ///< The kind's declarative
                                         ///< attributes, as the generated const
                                         ///< block a layout would have produced,
                                         ///< or NULL to declare none of them.
                                         ///< Borrowed and never freed, like @ref
                                         ///< theme_class: it must outlive the
                                         ///< node. The members above are the
                                         ///< runtime route for the same values --
                                         ///< a setter writes the model's state,
                                         ///< not the block -- so a caller that
                                         ///< has no block to name leaves this
                                         ///< NULL and sets them instead.
} IrcitUiWidgetDef;

#endif /* IRCITUI_IRCIT_UI_WIDGET_DEF_TYPE_H */
