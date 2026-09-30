/**
 * @file ircit_ui_border_style_enum.h
 * @brief The glyph profile a frame is drawn in: the vocabulary a border is asked for in.
 *
 * A style is a ROLE for the frame, not a set of characters. A widget asks to be framed
 * in @ref IRCITUI_BORDER_ROUNDED and the engine decides what that means on this
 * terminal -- which is what lets one layout render as a rounded modern frame or as the
 * legacy ACS box without the layout naming a codepoint anywhere.
 *
 * **Why the primitive had to be replaced rather than parameterised.** `box(win, v, h)`
 * is `wborder(win, v, v, h, h, 0, 0, 0, 0)` -- the four CORNER arguments are
 * hard-coded zeros in the macro (`/usr/include/ncurses.h`), so the two parameters set
 * only the four edges and ncurses substitutes its own ACS corner for each zero. There
 * is no ncurses entry point that draws a rounded frame, so a profile is drawn glyph by
 * glyph. Measured, and the measurement is the first thing
 * `dev/probes/border/p_frame_chars.c` asserts: `box(win, 0x2502, 0x2500)` still leaves
 * an ACS corner whose `A_CHARTEXT` is the letter `l`.
 *
 * **@ref IRCITUI_BORDER_STANDARD is the default and is not merely "the square one".**
 * It is the whole of the behaviour that shipped before this enum existed -- the
 * `box()` call, the ACS cells, the `A_ALTCHARSET` corners -- so an application that
 * never asks for a style gets a frame that is byte-for-byte what it always had. Every
 * other value is drawn as glyphs and is a change to the glass.
 *
 * The enumerators are ordered as declared and that order is part of the contract: the
 * engine's glyph table is indexed by this value, and @ref IRCITUI_BORDER_COUNT is the
 * fence a cycling caller counts against.
 */
#ifndef IRCITUI_IRCIT_UI_BORDER_STYLE_ENUM_H
#define IRCITUI_IRCIT_UI_BORDER_STYLE_ENUM_H

/*!
 * @enum IrcitUiBorderStyle
 * @brief How a frame is drawn.
 *
 * Ask for one with @ref IrcitUiSetBorderStyle, or read the current one back with
 * @ref IrcitUiBorderStyleGet.
 */
typedef enum {
    IRCITUI_BORDER_STANDARD = 0, ///< The legacy ACS box, drawn with `box()`. **The default**: unchanged behaviour.
    IRCITUI_BORDER_ROUNDED,      ///< Smooth corners: `╭ ╮ ╰ ╯ ─ │` (U+256D, U+256E, U+2570, U+256F, U+2500, U+2502).
    IRCITUI_BORDER_THICK,        ///< Heavy rules: `┏ ┓ ┗ ┛ ━ ┃` (U+250F, U+2513, U+2517, U+251B, U+2501, U+2503).
    IRCITUI_BORDER_DOUBLE,       ///< Double rules: `╔ ╗ ╚ ╝ ═ ║` (U+2554, U+2557, U+255A, U+255D, U+2550, U+2551).
    IRCITUI_BORDER_COUNT         ///< The fence: one past the last style, for a caller that cycles them.
} IrcitUiBorderStyle;

#endif /* IRCITUI_IRCIT_UI_BORDER_STYLE_ENUM_H */
