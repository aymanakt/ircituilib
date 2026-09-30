
/**
 * @file ircit_ui_palette_slot_enum.h
 * @brief The semantic colour slots: the vocabulary styling is asked for in.
 *
 * A slot is a ROLE, not a colour. A widget asks to be drawn as @ref
 * IRCITUI_SLOT_TEXT_MUTED and the palette decides what that means on this terminal --
 * which is what lets one layout render on a 256-colour terminal, on an eight-colour
 * one, and on a terminal with no colour at all without the layout naming a colour
 * number anywhere.
 *
 * Before this vocabulary existed, colour reached the screen as bare indices: a layout
 * could only choose from eight base names, and the library's own widgets hard-coded
 * numbers with no meaning attached -- the watchdog alert asked for foreground 3 for its
 * title, 6 for two descriptive lines, 8 for its hint, on background 0 with border 2.
 * Nothing said which of those was the warning and which was the muted detail.
 *
 * These slots are asked for by NAME in a layout's `theme_class:` key, so the YAML
 * spellings in tools/validate_layout.py and tools/gen_ui.py are the same vocabulary as
 * the enumerators below, and post_generation_verifier.py asserts the two agree rather
 * than trusting them to.
 */
#ifndef IRCITUI_IRCIT_UI_PALETTE_SLOT_ENUM_H
#define IRCITUI_IRCIT_UI_PALETTE_SLOT_ENUM_H

/*!
 * @enum IrcitUiPaletteSlot
 * @brief A colour role, resolved to a colour pair by the palette.
 *
 * The enumerators are ordered as they are declared and the order is the ABI: new slots
 * are appended before @ref IRCITUI_SLOT_COUNT, so a slot's value never moves.
 */
typedef enum {
    IRCITUI_SLOT_SURFACE = 0,     ///< The backdrop a layout's containers sit on.
    IRCITUI_SLOT_PANEL,           ///< A raised surface: the default widget content.
    IRCITUI_SLOT_TEXT,            ///< Standard text lines.
    IRCITUI_SLOT_TEXT_MUTED,      ///< Secondary or descriptive text: hints, timestamps, ids.
    IRCITUI_SLOT_TEXT_ERROR,      ///< A warning or a failure, meant to be noticed.
    IRCITUI_SLOT_EDIT_ACTIVE,     ///< The focused edit field.
    IRCITUI_SLOT_OVERLAY_SNACKBAR,///< The transient notification box.
    IRCITUI_SLOT_OVERLAY_ACTION,  ///< A transient overlay's control label.
    IRCITUI_SLOT_ACCENT,          ///< The highlight on a value's leading edge: a
                                  ///< progress bar's scrubber head, the filled
                                  ///< half of a dial's arc. Its own role rather
                                  ///< than a reuse, because none of the eight
                                  ///< above names it -- a scrubber is not a
                                  ///< surface, not a text line, and not an
                                  ///< overlay -- so before this existed the
                                  ///< highlight was unaskable and every caller
                                  ///< hard-coded a colour number instead, which
                                  ///< is exactly what this vocabulary is for.
    IRCITUI_SLOT_COUNT            ///< The fence: one past the last slot.
} IrcitUiPaletteSlot;

#endif /* IRCITUI_IRCIT_UI_PALETTE_SLOT_ENUM_H */
