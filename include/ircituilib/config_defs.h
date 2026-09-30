/**
 * @file config_defs.h
 * @brief The single source of truth for ircituilib's compile-time defaults.
 *
 * Per the project convention, every `#define` that sets a compile-time default
 * lives here rather than at its point of use, so that a reader has one place to
 * look and a maintainer has one place to change. Implementation files include
 * this header and use the named constants — never bare magic numbers.
 *
 * Everything here is `PRIV_CONFIG_DEFAULT_`: private implementation limits that
 * are never overridable at configure time. The prefix is a deliberate promise,
 * not decoration — if a value is ever promoted to a user-facing knob it is
 * renamed `CONFIG_DEFAULT_` and wired through CMake in the same change.
 *
 * The values are unchanged from the literals they replace: this header names
 * existing behaviour, it does not alter it.
 *
 * Deliberately *not* here, because the convention's rule is about limits and
 * defaults rather than every integer in the source: array indices, enum flag
 * bit positions (`1u << n`), the UTF-8 lead-byte masks and sequence lengths,
 * the xterm 256-colour cube and Rec.601 luma weights (fixed by their
 * specifications), the RGB channel and Braille dot counts (fixed by the pixel
 * and terminal formats), ASCII control-key codes, and the layout verifier's
 * preview colour ramp (fixture data, not configuration).
 */
#ifndef IRCITUI_CONFIG_DEFS_H
#define IRCITUI_CONFIG_DEFS_H

/** @brief Capacity in bytes of the single-line edit buffer, terminator
    included. Sizes @ref IrcitUiNode's inline `ebuf`. */
#define PRIV_CONFIG_DEFAULT_EDIT_CAP             256
/** @brief Blink half-period in milliseconds suggested to the main loop's
    `timeout()`. The engine never reads it; the cadence stays the caller's. */
#define PRIV_CONFIG_DEFAULT_CARET_BLINK_MS       450
/** @brief Wall-clock deadline in milliseconds past which a single network
    listener call is reported as over budget.

    A **log threshold, not a pre-emption.** The network listener runs with the
    watchdog deliberately unarmed -- an eviction abandons its stack mid-update,
    and a stream reassembler cannot survive that -- so this log line is the
    whole of what replaces the eviction every other callback gets. Nothing
    else measures that path.

    Generous on purpose: it is there to catch a stall, which is orders of
    magnitude, not a merely slow call. **Provisional and unmeasured** -- the
    value wants setting from a real reassembler plus its layout request, which
    has not been measured. See IRCITUILIB_EVENTS_TCP_IP_DESIGN.md A.5 and A.9. */
#define PRIV_CONFIG_DEFAULT_NETWORK_LISTENER_BUDGET_MS   250
/** @brief Wall-clock deadline in milliseconds past which a single LiveData
    observer block is reported as over budget.

    The same trade as the network listener's above, for the same reason and with
    the same shape: a **log threshold, not a pre-emption.** The proposal specified
    a POSIX timer and a `siglongjmp` out of a hung observer; that is not built, and
    §16 of IRCITUILIB_LIVEDATA_DESIGN.md records why. This log line is what stands
    in its place.

    Generous on purpose: an observer is meant to paint from a value it is handed,
    so a legitimately slow one is already a contract breach, and the threshold is
    there to catch a stall rather than to police a budget. **Provisional and
    unmeasured** -- nothing in tree has run an observer at all, so this wants
    setting from a real one. */
#define PRIV_CONFIG_DEFAULT_OBSERVER_BUDGET_MS           40
/** @brief Largest number of pipeline envelopes the main loop processes in one
    pass before it yields to the render stage.

    The drain used to run until the ring was empty, which was correct while every
    producer was human-rate. A network worker posting at line rate makes it a
    starvation path: the ring never empties, so the frame pass never runs, and
    because `IrcitUiDispatcherExited` is read at the END of a pass, the
    application cannot shut down either while the flood lasts.

    What the cap does and does not buy, stated exactly: it bounds how long the
    loop goes without rendering. It does **not** bound keystroke LATENCY -- input
    shares the same FIFO, so a key still waits behind everything already queued,
    now at (position / cap) passes instead of one unbounded drain. If input
    latency ever needs bounding, keys want their own path rather than a smaller
    cap; `IrcitUiMainLooper` already bypasses the ring entirely for fast-path
    typing, which is the precedent.

    **Provisional**: the value is a starting point, not a measurement. */
#define PRIV_CONFIG_DEFAULT_MAX_EVENTS_PER_PASS          64

/** @brief Clipboard buffer. Holds a copy of the focused field's selection. */
#define PRIV_CONFIG_DEFAULT_CLIPBUF_SZ           512
/** @brief Path scratch buffer, used to retry a relative image path with a
    `./` prefix. */
#define PRIV_CONFIG_DEFAULT_PATH_BUF_SZ          512
/** @brief Status-line echo buffer for a message built from a node id, as
    `"click %s"` or `"btn %s"`. */
#define PRIV_CONFIG_DEFAULT_STATUS_ECHO_SZ        64
/** @brief Status-line echo buffer for a submitted edit field, which also
    carries a prefix and a truncated copy of the field's text. */
#define PRIV_CONFIG_DEFAULT_EDIT_ECHO_SZ         280
/** @brief Scratch buffer for one node's text while painting it. */
#define PRIV_CONFIG_DEFAULT_DRAW_SCRATCH_SZ      128
/** @brief Maximum bytes inspected or held for a single UTF-8 character.
    Used both as the `mbrtowc` limit and as the size of the small copy buffer,
    which must therefore be able to hold a whole character. */
#define PRIV_CONFIG_DEFAULT_UTF8_MAX_BYTES         8
/** @brief Wide-character scratch buffer for display-width measurement and
    wide painting. Note that the `mbstowcs` limit passed alongside it is one
    less than this, so that the terminator the caller writes at index `n`
    always lands inside the buffer. */
#define PRIV_CONFIG_DEFAULT_WCS_BUF_SZ           256

/** @brief Cell extent assumed when measuring a preview whose real cell size
    is unknown. */
#define PRIV_CONFIG_DEFAULT_VERIFY_CELL_W          8
/** @brief Row extent assumed for a preview whose real cell size is unknown. */
#define PRIV_CONFIG_DEFAULT_VERIFY_CELL_H         14
/** @brief Capacity of the id table used to detect duplicate ids. Bounds the
    walk: ids beyond it cannot be reported as duplicates. */
#define PRIV_CONFIG_DEFAULT_VERIFY_SEEN_SZ       256
/** @brief Label scratch buffer for a preview heading. */
#define PRIV_CONFIG_DEFAULT_LABEL_SZ              64
/** @brief Column cap for a preview laid out at a caller-chosen size. */
#define PRIV_CONFIG_DEFAULT_VERIFY_PREVIEW_COLS   40
/** @brief Row cap for a preview laid out at a caller-chosen size. */
#define PRIV_CONFIG_DEFAULT_VERIFY_PREVIEW_ROWS   20

/** @brief The most colour pairs the registry will allocate.
 *
 * A ceiling on the registry's own arrays rather than a statement about the
 * terminal: the real limit is COLOR_PAIRS, whichever is smaller. Pair numbers run
 * 1..cap-1, because pair 0 is the terminal's own default and is never allocated --
 * which is what lets an exhausted registry return a pair that still renders.
 *
 * This replaces four constants that divided the pair space between three
 * allocators. The division was documented as if it prevented collisions and did
 * not: the engine allocated 1..62 and the image module started at 32, so they
 * overlapped by 31 pairs for any application that used both. See
 * dev/probes/palette/README.md for the measurements.
 */
#define PRIV_CONFIG_DEFAULT_PALETTE_PAIR_CAP     4096

/** @brief Which register-driven effects are on at startup: a bitmask of
    IRCITUI_PALETTE_EFFECT_*, and **0 by default**.
 *
 * Zero because using a register at all costs the user something real and measured:
 * once any register has been written, ncurses emits OSC 104 at exit and the
 * terminal's palette returns to the emulator's defaults, so a hand-tuned colour
 * scheme does not survive the program. That is a poor default to impose for a
 * cosmetic effect, so the effects are opt-in -- IrcitUiPaletteSetEffects turns them
 * on at runtime, and this constant is what an application that wants them always
 * sets by hand. */
#define PRIV_CONFIG_DEFAULT_PALETTE_EFFECTS          0

/** @brief Whether a stacked popup casts a drop shadow: 0 by default.
 *
 * Zero because a shadow is not free and is not always wanted: it is drawn into the
 * PARENT's cells, which means the parent has to be repainted and pushed again whenever a
 * popup moves -- one extra refresh per stacked child per moving frame. A default of off
 * keeps every existing frame's work identical, and an application that wants the depth
 * asks for it with IrcitUiSetShadow. */
#define PRIV_CONFIG_DEFAULT_SHADOW                    0

/** @brief How many colour-register ranges may be borrowed at once.
 *
 * Four: the canopy needs two (its backgrounds and the blended foregrounds derived from
 * them), the notification gradient one, and a fade one. Exhausting it is a refusal, not
 * a queue -- an effect that cannot have its registers does not run. */
#define PRIV_CONFIG_DEFAULT_PALETTE_RAMPS            4

/** @brief How many fades may be in flight at once.
 *
 * Each one holds a register, and they all come out of ONE borrowed range -- the ramp
 * table's four slots are for effects that need a screen's worth of registers, and a fade
 * needs exactly one. */
#define PRIV_CONFIG_DEFAULT_PALETTE_FADES            4
/** @brief Milliseconds a fade takes by default, when a caller does not say. */
#define PRIV_CONFIG_DEFAULT_PALETTE_FADE_MS        400
/** @brief Luma above which a sampled pixel counts as ink rather than paper
    when converting an image region to a Braille or block glyph. */
#define PRIV_CONFIG_DEFAULT_LUMA_THRESHOLD        96
/** @brief Grey level used when a region has no ink pixels, so that an empty
    cell draws as background rather than black. */
#define PRIV_CONFIG_DEFAULT_IMG_FALLBACK_RGB      80

/** @brief Default popup width, in cells, before content is measured. */
#define PRIV_CONFIG_DEFAULT_POPUP_W               34
/** @brief Default popup height, in rows, before content is measured. */
#define PRIV_CONFIG_DEFAULT_POPUP_H               18
/** @brief Narrowest a popup may be sized, in cells. */
#define PRIV_CONFIG_DEFAULT_POPUP_MIN_W           12
/** @brief Shortest a popup may be sized, in rows. */
#define PRIV_CONFIG_DEFAULT_POPUP_MIN_H            5
/** @brief Screen edge inset a popup keeps, in cells and rows. A popup is
    never allowed to reach the very edge of the canvas. */
#define PRIV_CONFIG_DEFAULT_POPUP_EDGE_MARGIN      4

/** @brief Display width charged for a check, radio, or switch indicator. */
#define PRIV_CONFIG_DEFAULT_GLYPH_W                5
/** @brief Narrowest a progress or seek bar wraps to, in cells. */
#define PRIV_CONFIG_DEFAULT_BAR_MIN_W             12
/** @brief Columns an ADJACENT label reserves, in cells.
 *
 *  The label is formatted "%3d%%" between two spaces, so it is always six columns wide
 *  whatever the value: `%3d` pads a one- or two-digit percentage out to three. That
 *  constancy is the reason the reservation can be a compile-time number at all -- a
 *  format whose width moved with the value would make the BAR jitter as it filled, which
 *  is the failure this constant exists to prevent.
 *
 *  It is a constant and a format string in two different places, so the two are tied
 *  together by assertion rather than by hope: dev/probes/progress/p_bar_attrs.c's `label`
 *  case formats every boundary value (0, 5, 50, 95, 100) and checks the display width is
 *  exactly this, so a change to either that broke the agreement fails there. */
#define PRIV_CONFIG_DEFAULT_BAR_LABEL_W             6
/** @brief Register steps a bar's gradient ramp is acquired at.
 *
 *  FIXED, and deliberately not the bar's width. The design document acquires one register
 *  per filled column, which makes the ramp's size track the value -- so every value change
 *  is a release and a re-acquire, at the cost of a malloc and a color_content() per
 *  register on the way in and an init_color() per register on the way out. Sixteen steps
 *  is past the point a terminal gradient reads as banded and small enough that acquiring
 *  one contiguous run of free registers succeeds on a 256-colour terminal even with the
 *  canopy holding two of the four ramp slots. */
#define PRIV_CONFIG_DEFAULT_BAR_RAMP_STEPS         16
/** @brief Horizontal chrome a button caption carries, in cells. */
#define PRIV_CONFIG_DEFAULT_BUTTON_PAD             4
/** @brief Narrowest an edit field wraps to, in cells, so that a short or
    empty field still has room for its caret. */
#define PRIV_CONFIG_DEFAULT_EDIT_MIN_W            16
/** @brief Grid columns assumed for a grid container that names none. */
#define PRIV_CONFIG_DEFAULT_GRID_COLS              8
/** @brief Layout refinement passes. Placing children can change the space
    available to their siblings, so the engine re-runs placement until
    positions settle; this bounds that loop. */
#define PRIV_CONFIG_DEFAULT_LAYOUT_PASSES          3
/** @brief Width of the hand-built context menu, in cells. */
#define PRIV_CONFIG_DEFAULT_CTXMENU_W             16

/** @brief Column cap for a rendered image, in cells. */
#define PRIV_CONFIG_DEFAULT_IMG_CAP_COLS          48
/** @brief Row cap for a rendered image, in cells. */
#define PRIV_CONFIG_DEFAULT_IMG_CAP_ROWS          20
/** @brief Columns assumed when the canvas is too small to derive a width. */
#define PRIV_CONFIG_DEFAULT_IMG_FALLBACK_COLS     40
/** @brief Rows assumed when the canvas is too small to derive a height. */
#define PRIV_CONFIG_DEFAULT_IMG_FALLBACK_ROWS     16
/** @brief Columns reserved beside an image for its container's chrome. */
#define PRIV_CONFIG_DEFAULT_IMG_HMARGIN            6
/** @brief Rows reserved above and below an image for its container. */
#define PRIV_CONFIG_DEFAULT_IMG_VMARGIN            8
/** @brief Canvas extent below which a width or height is not derived from it
    at all, because the subtraction would leave nothing usable. */
#define PRIV_CONFIG_DEFAULT_IMG_MIN_CANVAS         8
/** @brief Cell width assumed for an image that is absent or not yet
    loaded. */
#define PRIV_CONFIG_DEFAULT_IMG_CELL_W             8
/** @brief Cell height assumed for an image that is absent or not yet
    loaded. */
#define PRIV_CONFIG_DEFAULT_IMG_CELL_H             4

/** @brief Rules a theme allocates room for before its first grow. */
#define PRIV_CONFIG_DEFAULT_THEME_RULE_CAP         8

/** @brief Slots in the dispatcher's input staging ring. Bounds the queue that
    holds events between the moment they are parsed and the moment the pump
    dispatches them, so its size is the ceiling on how far input may run ahead
    of the frame that consumes it. */
#define PRIV_CONFIG_DEFAULT_INPUT_QUEUE_CAPACITY  64

#define PRIV_CONFIG_DEFAULT_ASYNC_QUEUE_CAPACITY    64

/** @brief Rows in the application callback table: how many node ids an
    application may attach an on_click callback to at once. A refusal beyond it
    is reported by @ref IrcitUiBindClick returning 0, never by overwriting a row
    the application already registered. */
#define PRIV_CONFIG_DEFAULT_BINDING_CAP           32

/** @brief Capacity in bytes of the snackbar's message buffer, terminator
    included. The text is COPIED in: the overlay outlives the caller's buffer,
    which is the opposite of a node's `common.text`. */
#define PRIV_CONFIG_DEFAULT_SNACKBAR_MSG_SZ       256
/** @brief Capacity in bytes of a notification's action label, terminator
    included. Copied in for the message's reason, and clipped to the cells the
    message row has left rather than refused if it is longer. */
#define PRIV_CONFIG_DEFAULT_SNACKBAR_LABEL_SZ      32
/** @brief Rows in the snackbar box: a message row between two borders. */
#define PRIV_CONFIG_DEFAULT_SNACKBAR_ROWS           3
/** @brief Rows of clearance kept below the settled box, so the terminal's bottom
    row is never written. Writing the bottom-right cell is what scrolls a terminal,
    and a box resting on the last row would do it at its own corner. */
#define PRIV_CONFIG_DEFAULT_SNACKBAR_MARGIN         1
/** @brief Columns of clearance kept at each side on a screen too narrow to
    centre a clamped box. */
#define PRIV_CONFIG_DEFAULT_SNACKBAR_EDGE_X         2
/** @brief Widest the box grows on a wide screen, in columns. */
#define PRIV_CONFIG_DEFAULT_SNACKBAR_MAX_W         60
/** @brief Screen width above which the box is clamped to
    @ref PRIV_CONFIG_DEFAULT_SNACKBAR_MAX_W and centred. */
#define PRIV_CONFIG_DEFAULT_SNACKBAR_WIDE_COLS    100
/** @brief Narrowest the box wraps to before it gives up and spans the screen. */
#define PRIV_CONFIG_DEFAULT_SNACKBAR_MIN_COLS       8

/** @brief Rows per second the box travels.

    One row per 83 ms, so the whole four-row journey is a third of a second and
    takes about five passes per row at the frame interval below. A cell terminal
    has no fractional rows: "sliding" is a discrete row step, and this is how often
    one is taken. */
#define PRIV_CONFIG_DEFAULT_SNACKBAR_ROWS_PER_SEC  12
/** @brief Wake-up interval in milliseconds while the overlay is on screen.

    The main loop otherwise sleeps for the caret's half-period, which is far too
    coarse to move a box a row at a time. Only while the overlay is up. */
#define PRIV_CONFIG_DEFAULT_SNACKBAR_FRAME_MS      16
/** @brief How long a notification stays up when the caller names no duration. */
#define PRIV_CONFIG_DEFAULT_SNACKBAR_DURATION_MS 3000
/** @brief Largest frame interval the animation will integrate, in milliseconds.

    A pass can be late for reasons that have nothing to do with the animation -- a
    stop, a debugger, a starved scheduler -- and integrating a two-second gap would
    teleport the box instead of sliding it. Anything beyond this is treated as one
    ordinary frame. */
#define PRIV_CONFIG_DEFAULT_SNACKBAR_TICK_CAP_MS  150
/* The notification box's colours were 236 and 255 here, as xterm palette indices
   with reverse video below 256 colours.

   They are the overlay_snackbar slot's now -- the same two numbers, and the same
   reverse-video fallback, but in the palette: reachable from a layout as
   `theme_class: overlay_snackbar`, redefinable at runtime, and shared with anything
   else that wants the role. These two constants were the audit's example of a colour
   written down in C that no layout could name and no theme could move. */

/** @brief Buffer holding the demo's filler text. */
#define PRIV_CONFIG_DEFAULT_DEMO_BLOB_SZ        4096
/** @brief Bytes of the demo blob held back so the last `snprintf` cannot
    fill the buffer exactly and lose its terminator. */
#define PRIV_CONFIG_DEFAULT_DEMO_BLOB_RESERVE     80
/** @brief Lines of filler text the demo generates. */
#define PRIV_CONFIG_DEFAULT_DEMO_LINES            60

#endif /* IRCITUI_CONFIG_DEFS_H */
