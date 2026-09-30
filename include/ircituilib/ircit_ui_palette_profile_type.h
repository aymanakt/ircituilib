/**
 * @file ircit_ui_palette_profile_type.h
 * @brief A named set of slot definitions: what a theme is, concretely.
 *
 * A profile is the whole palette stated in one place, so switching themes is one call
 * rather than eight, and so an application can name what it is switching to. It is
 * deliberately a plain data structure with no allocation and no ownership: a profile
 * may live in static storage, on the stack of the function that applies it, or in an
 * application's own table, and the palette copies what it needs.
 *
 * The struct is public because a caller building its own profile has to be able to
 * write one. It is also why the slot count is part of the contract: a profile names a
 * definition for every slot, because a profile that covered some of them would leave
 * the rest carrying whatever the previous profile left behind -- which is a state
 * nobody asked for and no test would think to look for.
 */
#ifndef IRCITUI_IRCIT_UI_PALETTE_PROFILE_TYPE_H
#define IRCITUI_IRCIT_UI_PALETTE_PROFILE_TYPE_H

#include <ncurses.h>

#include "ircituilib/ircit_ui_palette_slot_enum.h"

/*!
 * @brief What one slot means in one profile.
 *
 * The colours are terminal colour indices, not RGB: this layer names a role's colour in
 * the palette the terminal already has, which is what makes it work on an eight-colour
 * terminal as readily as a 256-colour one.
 *
 * BOTH LEVELS ARE CARRIED, and that is not redundancy: the eight-colour definition is
 * not the 256-colour one clamped, it is the same role re-chosen for a terminal that has
 * eight colours and attributes to spare -- a muted note is 244 on a 256-colour terminal
 * and white-on-black with A_DIM on an eight-colour one. A profile carrying only the
 * 256-colour numbers would leave an eight-colour terminal with a role it cannot render.
 *
 * The attribute is REPLACED rather than OR'd, so @c A_NORMAL states "plain" and there is
 * no way to express "leave whatever was there" -- a profile says everything.
 */
typedef struct {
    short  fg256, bg256; ///< On a terminal that has 256 colours or more.
    short  fg8, bg8;     ///< On one that has eight: the same role, chosen for it.
    chtype attr;         ///< Attributes, which apply at every level including none.
} IrcitUiPaletteSlotDef;

/*!
 * @brief A named palette.
 *
 * @c name is borrowed and must outlive the profile while it is in use; it is what
 * @ref IrcitUiPaletteProfileByName searches on, and it is the string a caller shows a
 * user after switching.
 */
typedef struct {
    const char *             name;                    ///< The profile's name.
    IrcitUiPaletteSlotDef    slots[IRCITUI_SLOT_COUNT]; ///< One definition per slot, in enum order.
} IrcitUiPaletteProfile;

#endif /* IRCITUI_IRCIT_UI_PALETTE_PROFILE_TYPE_H */
