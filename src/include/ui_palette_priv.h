/**
 * @file ui_palette_priv.h
 * @brief The palette registry: the single owner of colour pair indices.
 *
 * Private to the library. A pair index is a finite resource the terminal grants
 * (COLOR_PAIRS of them), and before this header there were three allocators handing
 * them out from overlapping ranges without knowing about each other -- the engine's
 * from 1, the image module's from 32, the ncurses adapter's from 1 again. The audit in
 * dev/probes/palette/README.md measured what that cost.
 *
 * There is one entry point now, and everything that needs a pair goes through it:
 * the engine for every node it paints, the image module for its half-cell glyphs, the
 * snackbar, and the ncurses adapter for the superset window API. Two properties
 * matter and both are the registry's reason for existing:
 *
 *  - **A pair is a function of its colours.** Asking twice for the same (fg,bg) yields
 *    the same index, so a caller may ask on every paint without a cache of its own.
 *    That is why the image module's and the adapter's caches were deleted rather than
 *    repointed: the registry is the cache.
 *
 *  - **Exhaustion is reported, and returns a pair that renders sanely.** Pair 0 is the
 *    terminal's own default and is what a refusal returns, so a caller that ignores the
 *    distinction still draws something legible. The allocator this replaced returned
 *    pair 1 -- whichever colour had been asked for first -- so the sixty-third distinct
 *    colour rendered as the first, silently.
 */
#ifndef IRCITUI_UI_PALETTE_PRIV_H
#define IRCITUI_UI_PALETTE_PRIV_H

#include <ncurses.h>

#include "ircituilib/ircit_ui_palette_slot_enum.h"
#include "ircituilib/ircit_ui_palette_profile_type.h"

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * @brief The pair whose foreground and background are @p fg and @p bg, allocating it
 *        the first time that combination is seen.
 *
 * @return A pair index usable in @c COLOR_PAIR, or **0** when the terminal has no
 *         colour, when @c start_color has not been called, when the pair could not be
 *         registered, or when the pair space is exhausted. Pair 0 is the terminal's
 *         default and is always safe to draw with.
 *
 * Thread 0 only: it calls into ncurses, and the registry's own arrays are unsynchronised
 * because nothing in this library allocates a pair off Thread 0.
 */
short IrcitUiColourPair(short fg, short bg);

/*!
 * @brief The style a slot is drawn in: its attributes and its pair, ready for
 *        @c wattron or to be OR'd into a @c wbkgd.
 *
 * Resolved on every call rather than cached by the caller. What that buys is that no
 * NODE has to be restyled: there is no per-node write and no tree walk to propagate a
 * look, because the lookup happens where the node is drawn.
 *
 * What it does NOT buy is a free repaint, and an earlier version of this comment claimed
 * it did. A slot's style is a PAIR, and a changed pair reaches the glass only when the
 * cells holding it are written again -- so a caller that redefines a slot must mark the
 * nodes wearing it for repaint, exactly as it would after any other appearance change.
 * The zero-repaint path is the register one (init_color on a register a pair already
 * points at), which is what the effects use and what
 * dev/probes/palette/p_register_propagation.c measures. The distinction is worth
 * keeping straight: pairs need a repaint, registers do not.
 *
 * A slot is resolved against the terminal's capability once, at first use, and keeps
 * that answer: the eight-colour and monochrome definitions are not the 256-colour ones
 * degraded, they are chosen for those terminals, and @c COLOR_PAIRS is not something
 * that changes under a running program.
 *
 * @return The composite style. When the terminal has no colour the return is the slot's
 *         attributes alone, which is a legible rendering rather than an empty one.
 *         Out-of-range slots return @c A_NORMAL.
 */
chtype IrcitUiPaletteStyle(IrcitUiPaletteSlot slot);

/*!
 * @brief Redefine a slot.
 *
 * @param slot  The slot to redefine; out of range is ignored.
 * @param fg    Foreground colour index, or -1 to leave the current one.
 * @param bg    Background colour index, or -1 to leave the current one.
 * @param attr  Attributes to set, or `(chtype)-1` to leave the current ones.
 *              Attributes are REPLACED rather than OR'd, because @c A_NORMAL is 0 and
 *              a slot has to be able to become plain again -- which is why the
 *              "unchanged" sentinel cannot be 0.
 *
 * The profile work will drive this from a named profile; until then it is how a caller
 * states what a role means. The pair is acquired from the registry, so two slots that
 * name the same colours share a pair.
 */
void IrcitUiPaletteSetSlot(IrcitUiPaletteSlot slot, short fg, short bg, chtype attr);

/*!
 * @brief The slot a layout's @c theme_class name refers to.
 *
 * The names are the YAML spellings -- "surface", "panel", "text", "text_muted",
 * "text_error", "edit_active", "overlay_snackbar", "overlay_action" -- and they are one
 * vocabulary with @ref IrcitUiPaletteSlot, checked by post_generation_verifier.py.
 *
 * @return The slot, or **-1** when the name is not a slot. A name that is not a slot is
 *         a layout error rather than a third case: the generator refuses it, so a -1
 *         here means a class was set from C rather than from a layout.
 */
int IrcitUiPaletteSlotForName(const char *name_ptr);

/*!
 * @brief Apply a named profile's definitions to every slot.
 *
 * **The caller must repaint.** This moves pairs, and a changed pair reaches the screen
 * only when the cells holding it are written again, so call @ref IrcitUiPaletteRepaint
 * straight after -- or the palette will have changed while the screen still shows the
 * old colours, until something unrelated happens to dirty a node.
 *
 * Does not allocate, and copies: @p profile_ptr may be a built-in, a local, or an
 * application's own static, and does not have to outlive the call.
 */
void IrcitUiPaletteApplyProfile(const IrcitUiPaletteProfile *profile_ptr);

/*!
 * @brief A built-in profile by name -- "dark" or "light" -- or NULL.
 *
 * Darkness is the default a caller gets without asking, and it is the palette the tree
 * shipped before there were slots: every colour in it is the one the widget it belongs
 * to used to hard-code.
 */
const IrcitUiPaletteProfile *IrcitUiPaletteProfileByName(const char *name_ptr);

/*!
 * @brief Mark the screen for repaint, so an applied profile reaches the glass.
 *
 * One line, and separate from @ref IrcitUiPaletteApplyProfile on purpose: the palette
 * sits below the screen and does not reach up into it, but a caller who forgets this
 * gets a silent no-op rather than an error -- so it is a named call and not a footnote.
 */
void IrcitUiPaletteRepaint(void);

/*!
 * @brief Which register-driven effects are enabled: a mask of IRCITUI_PALETTE_EFFECT_*.
 *
 * A runtime switch rather than a compile-time one, so an application can offer effects
 * as a preference -- and so the default (nothing) is what a program gets without asking.
 */
void IrcitUiPaletteSetEffects(unsigned mask);

/*!
 * @brief The six glyphs of a border profile, in the order the frame painter draws them:
 *        top-left, top-right, bottom-left, bottom-right, horizontal, vertical.
 *
 * Each is a one-glyph UTF-8 string, so a caller draws it with whatever it already uses
 * for text outside ASCII. @ref IRCITUI_BORDER_STANDARD and any out-of-range value return
 * **NULL**, which is not an error: it is the instruction to draw the legacy ACS box, and
 * keeping that as a NULL rather than as six table entries is what makes "the default is
 * exactly what shipped" a property of the code path rather than of the table's contents.
 *
 * Lives beside the effects rather than in the engine because the frame is drawn in two
 * places -- the engine's containers and the notification overlay's box -- and the overlay
 * may not reach into the engine.
 */
const char *const *IrcitUiBorderGlyphs(int style);

/*!
 * @brief Bring the canopy up for a screen of @p rows and return its base register, or -1.
 *
 * Screen-wide rather than per node: the ramp is a function of the ROW, so two nodes on the
 * same row must agree about it or the backdrop would seam at every boundary. Called from
 * the paint path, so the unchanged case -- the common one -- is a comparison and a return.
 *
 * Brings the blended-foreground layer up with it, and takes the canopy down again if that
 * layer cannot be had: text in a flat colour on a ramped backdrop reads as text that does
 * not belong to it, so half the effect is not shipped.
 */
int IrcitUiPaletteCanopyEnsure(int rows);

/*!
 * @brief The background style for a screen row, or @c A_NORMAL when no canopy is up.
 */
chtype IrcitUiPaletteCanopyStyle(int row);

/*!
 * @brief The style for text on a screen row: the pre-blended foreground over that row's
 *        canopy background.
 */
chtype IrcitUiPaletteCanopyTextStyle(int row);

/*!
 * @brief Advance the canopy's pulse by one step, rewriting both of its ramps.
 *
 * Called from the frame loop while @ref IrcitUiPaletteAnimating says an animation is
 * running. Costs two init_color calls per screen row and no cell work at all: the cells
 * already point at these registers, so the terminal recolours them.
 */
void IrcitUiPaletteCanopyPulse(void);

/*!
 * @brief Whether an animation is running, so the loop knows to use its frame cadence.
 *
 * Non-zero only when the pulse is enabled AND the canopy actually holds its registers --
 * asking for an effect on a terminal that refused them must not spin the loop.
 */
int IrcitUiPaletteAnimating(void);

/*!
 * @brief Called when a fade reaches its end, on Thread 0, from the frame loop.
 *
 * @param user_ptr Whatever the caller passed to @ref IrcitUiPaletteFadeStart.
 */
typedef void (*IrcitUiFadeDone)(void *user_ptr);

/*!
 * @brief Start a fade from a widget's own colour toward its background.
 *
 * @param from_fg     The foreground the widget is drawn in now, as a colour index.
 * @param from_bg     Its background, which the fade moves toward.
 * @param duration_ms How long it takes; 0 means the configured default.
 * @param on_done_ptr Called when it arrives, or NULL. **This is where the widget is torn
 *                    down** -- the fade keeps painting cells, so nothing may free them
 *                    before it finishes.
 * @param user_ptr    Passed back to the callback.
 *
 * @return A handle for @ref IrcitUiPaletteFadeStyle, or -1 -- when the effect is off, or
 *         when every fade slot is in use. A caller that gets -1 must tear its widget down
 *         immediately rather than leaving it undrawn.
 */
int IrcitUiPaletteFadeStart(short from_fg, short from_bg, unsigned duration_ms, IrcitUiFadeDone on_done_ptr, void *user_ptr);

/*!
 * @brief The style a fading widget is painted in, or @c A_NORMAL when the handle is stale.
 */
chtype IrcitUiPaletteFadeStyle(int handle);

/*!
 * @brief Advance every live fade by the clock. Called once per frame by the loop.
 */
void IrcitUiPaletteFadeTick(void);

/*!
 * @brief Whether any fade is in flight, so the loop knows to use its frame cadence.
 */
int IrcitUiPaletteFadeActive(void);

/*!
 * @brief Hand every borrowed register back, whatever holds it.
 *
 * The mitigation for the exit cost R-3 describes. It restores what ncurses **believed** the
 * registers held, which is not necessarily what the emulator had -- ncurses never queries
 * the terminal -- so it is an approximation of the user's palette rather than a guarantee.
 *
 * It also cannot undo OSC 104, which ncurses emits at exit once any register has been
 * written and which lands after whatever a caller does before that. A caller that wants
 * this to be the last word calls it **after** `endwin()`.
 */
void IrcitUiPaletteRestore(void);

/*!
 * @brief The handle of a live fade, or 0.
 *
 * For a painter that has no way to be told about a fade and should still honour one: the
 * overlay asks this and paints through the fade when one is running. Returns the first
 * live handle rather than a set, because the caller that uses this is painting ONE thing
 * and a set would invite it to guess which.
 */
int IrcitUiPaletteFadeHandle(void);

/*!
 * @brief The enabled effects, as the caller last set them.
 */
unsigned IrcitUiPaletteEffects(void);

/*!
 * @brief Borrow @p count consecutive colour REGISTERS, saving what they held.
 *
 * The ramp is the other half of the palette: pairs address a colour that is already in
 * the terminal's palette, while a register IS a colour, writable at runtime. Borrowing
 * one is what makes a gradient or a fade possible at all -- there is no pair arithmetic
 * that produces a colour that does not exist yet.
 *
 * Registers are taken from the TOP of the colour space downward, skipping the sixteen
 * the terminal owns and every register a live slot definition names -- because writing
 * a register a slot's pair points at would silently repaint that slot's users, which is
 * the aliasing hazard this whole layer exists to avoid. The borrowed set is recomputed
 * on each acquisition, so a ramp taken after a profile change avoids the new colours.
 *
 * The three conditions for success, all checked rather than assumed: colour is
 * available, @p count registers are free, and the terminal actually accepts a write to
 * one of them -- tested by writing and reading back, because a terminfo string claiming
 * a capability the emulator ignores is exactly the failure mode that would otherwise
 * ship as "the gradient does nothing".
 *
 * @param owner_ptr A borrowed literal naming the holder. Compared by POINTER at release,
 *                  where it is what stops a stale handle -- one remembered across a
 *                  configuration change, when the base may since have been granted to
 *                  somebody else -- from restoring colours over another effect's registers.
 *
 * @return The base register, or **-1**. A caller that gets -1 must draw without the
 *         ramp: the fallback is the slot's flat colour, not a broken gradient.
 */
int IrcitUiPaletteRampAcquire(int count, const char *owner_ptr);

/*!
 * @brief Set one step of a borrowed ramp to a colour, given as 8-bit RGB.
 *
 * The 8-bit value is scaled to ncurses' 0-1000 register range here, so a caller states
 * a colour the way every other tool states one.
 */
void IrcitUiPaletteRampSet(int base, int step, int r, int g, int b);

/*!
 * @brief A style drawing @p fg on the ramp's @p step.
 *
 * Allocates the pair the first time that combination is seen, through the same registry
 * as everything else -- so a ramp costs pairs the way any other colour does, and the
 * budget is the one place that decides whether it can.
 */
chtype IrcitUiPaletteRampStyle(int base, int step, short fg);

/*!
 * @brief Hand the registers back exactly as they were found.
 *
 * Restores from the contents read at acquisition. It does **not** undo ncurses' exit
 * behaviour: once any register has been written, ncurses emits OSC 104 at exit and the
 * terminal's palette returns to the emulator's defaults. That cost belongs to using a
 * register at all and is documented in dev/probes/palette/README.md.
 */
void IrcitUiPaletteRampRelease(int base, int count, const char *owner_ptr);

/*!
 * @brief The number of distinct pairs allocated so far.
 *
 * For diagnostics and for probes: it is the quantity that grows monotonically for the
 * life of the process, which is what makes the pair space a budget rather than a
 * per-frame concern.
 */
int IrcitUiPalettePairCount(void);

/*!
 * @brief Release the registry's arrays.
 *
 * Exists so a test or a probe can start from a known state. It does **not** un-register
 * anything from ncurses -- a pair index, once handed out, is ncurses' to hold -- so a
 * caller that resets and re-asks gets fresh indices rather than the old ones.
 */
void IrcitUiPalettePairReset(void);

#ifdef __cplusplus
}
#endif

#endif /* IRCITUI_UI_PALETTE_PRIV_H */
