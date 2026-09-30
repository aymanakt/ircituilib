/**
 * @file ui_palette.c
 * @brief The colour pair registry and the semantic slot table.
 *        See ui_palette_priv.h for the pair contract and
 *        ircituilib/ircit_ui_palette_profile_type.h for what a profile is.
 */
#include "ui_palette_priv.h"
#include "ircituilib/config_defs.h"
#include "ui_log.h"

#include <ncurses.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* --- The registry.
 *
 * Allocated once and never resized. A linear scan rather than a hash, and the reason is
 * the shape of the data rather than an oversight: the scan is as long as the number of
 * DISTINCT pairs in use, not as long as the capacity, so a layout with fourteen colours
 * pays fourteen comparisons. A hash would put a fixed cost on the common case to improve
 * a case that does not occur. */
static short *sFg;
static short *sBg;
static int    sCount;
static int    sCap;      /* 0 until resolved, then the capacity, or -1 for "no colour" */
static int    sReported; /* exhaustion is reported once, not once per frame */

/* --- The slots.
 *
 * The vocabulary is sSlotNames: the spellings a layout writes, in ENUM ORDER, because
 * the order is the ABI and a slot's value never moves.
 *
 * The definitions live in PROFILES. There is one role-to-colour table and it is a profile
 * rather than a private array, so the palette a caller gets by default and the palette
 * they can name and switch to are the same kind of thing -- the alternative is a default
 * no profile can express and a profile that cannot reproduce the default.
 */
static const char *sSlotNames[IRCITUI_SLOT_COUNT] = {
    "surface", "panel", "text", "text_muted", "text_error", "edit_active", "overlay_snackbar", "overlay_action", "accent",
};

/* Dark is what the tree shipped before there were slots: every 256-colour number here is
 * the one the widget it belongs to used to hard-code, which is why overlay_snackbar is
 * 255 on 236 and its eight-colour row is the A_REVERSE the snackbar fell back to. */
static const IrcitUiPaletteProfile sProfileDark = {
    "dark",
    {
        // fg256 bg256 fg8 bg8 attr
        {252,  234,   COLOR_WHITE, COLOR_BLACK, A_NORMAL         },
        {252,  236,   COLOR_WHITE, COLOR_BLUE,  A_NORMAL         },
        {252,  234,   COLOR_WHITE, COLOR_BLACK, A_NORMAL         },
        {244,  234,   COLOR_WHITE, COLOR_BLACK, A_DIM            },
        {196,  234,   COLOR_RED,   COLOR_BLACK, A_BOLD           },
        {45,   234,   COLOR_GREEN, COLOR_BLACK, A_UNDERLINE      },
        {255,  236,   COLOR_WHITE, COLOR_BLACK, A_REVERSE        },
        {45,   236,   COLOR_YELLOW, COLOR_BLACK, A_REVERSE | A_BOLD},
        /* accent: a bright cyan, and the only slot whose job is to be the one
           loud cell on a row that is otherwise a surface. Bold because on an
           eight-colour terminal cyan-on-black without it carries the same weight
           as everything around it, which is the opposite of the role. */
        {51,   234,   COLOR_CYAN,  COLOR_BLACK, A_BOLD           },
    }
};

/* Light. The two overlay rows are deliberately dark's: a notification is a block that has
 * to stand off whatever is behind it, and a light terminal is exactly where a dark block
 * does that. Everything else inverts. */
static const IrcitUiPaletteProfile sProfileLight = {
    "light",
    {
        {234,  255,   COLOR_BLACK, COLOR_WHITE, A_NORMAL         },
        {234,  252,   COLOR_BLACK, COLOR_CYAN,  A_NORMAL         },
        {234,  255,   COLOR_BLACK, COLOR_WHITE, A_NORMAL         },
        {240,  255,   COLOR_BLACK, COLOR_WHITE, A_DIM            },
        {160,  255,   COLOR_RED,   COLOR_WHITE, A_BOLD           },
        {25,   255,   COLOR_BLUE,  COLOR_WHITE, A_BOLD           },
        {255,  236,   COLOR_WHITE, COLOR_BLACK, A_REVERSE        },
        {45,   236,   COLOR_YELLOW, COLOR_BLACK, A_REVERSE | A_BOLD},
        /* accent: the dark profile's cyan darkened until it reads against white.
           30 is the 256-cube's (0,175,175), which is the same hue as dark's 51
           (0,255,255) at the weight a light background needs. */
        {30,   255,   COLOR_CYAN,  COLOR_WHITE, A_BOLD           },
    }
};

static const IrcitUiPaletteProfile *sProfiles[] = {&sProfileDark, &sProfileLight};

/* The live slot state: what the applied profile put there, at this terminal's level. */
static short  sSlotFg[IRCITUI_SLOT_COUNT];
static short  sSlotBg[IRCITUI_SLOT_COUNT];
static chtype sSlotAttr[IRCITUI_SLOT_COUNT];
static int    sSlotsReady;

/* 0 until resolved, then -1 for no colour, 1 for eight, 2 for 256.
 *
 * Not reset by IrcitUiPalettePairReset: the terminal's capability is not something a
 * caller changes by asking the registry to forget its pairs. */
static int sLevel;

static int
sColourLevel(void)
{
  if (sLevel) return sLevel;
  if (!has_colors()) {
    sLevel = -1;
    return sLevel;
  }
  sLevel = COLORS >= 256 ? 2 : 1;
  return sLevel;
}

/* Copy a profile's definitions into the live state, at this terminal's level. */
static void
sApplyProfile(const IrcitUiPaletteProfile *profile_ptr)
{
  int i;
  int lvl = sColourLevel();

  for (i = 0; i < IRCITUI_SLOT_COUNT; i++) {
    sSlotFg[i]   = lvl == 2 ? profile_ptr->slots[i].fg256 : profile_ptr->slots[i].fg8;
    sSlotBg[i]   = lvl == 2 ? profile_ptr->slots[i].bg256 : profile_ptr->slots[i].bg8;
    sSlotAttr[i] = profile_ptr->slots[i].attr;
  }
  sSlotsReady = 1;
}

/* The default, applied lazily on first use.
 *
 * Lazy because the application is the one that calls start_color(), in its own
 * initialisation, and this library is not told when -- so a decision taken before then
 * would be taken about the wrong terminal. The ncurses adapter learned this first and
 * its colour_state is the same idea. */
static void
sSlotsInit(void)
{
  if (sSlotsReady) return;
  sApplyProfile(&sProfileDark);
}

/* Resolve the registry's capacity, once.
 *
 * @return the capacity, or -1 when colour is unavailable. */
static int
sCapacity(void)
{
  int cap;

  if (sCap) return sCap;

  if (sColourLevel() < 0) {
    sCap = -1;
    return sCap;
  }

  cap = COLOR_PAIRS;
  if (cap > PRIV_CONFIG_DEFAULT_PALETTE_PAIR_CAP) cap = PRIV_CONFIG_DEFAULT_PALETTE_PAIR_CAP;

  // Indices run 1..cap-1: pair 0 is the terminal's own default and is never allocated, which is also what makes 0 a safe refusal.
  if (cap < 2) {
    sCap = -1;
    return sCap;
  }

  sFg = calloc((size_t)cap, sizeof(*sFg));
  sBg = calloc((size_t)cap, sizeof(*sBg));
  if (!sFg || !sBg) {
    free(sFg);
    free(sBg);
    sFg = NULL;
    sBg = NULL;
    sCap = -1;
    return sCap;
  }

  sCap = cap;
  return sCap;
}

short
IrcitUiColourPair(short fg, short bg)
{
  int i;
  int cap = sCapacity();

  if (cap < 0) return 0;

  /* Compared as two shorts rather than as one packed key. The allocator this replaced
     packed the pair into `(short)((fg << 8) | bg)`, so any colour index above 255 lost
     its high bits and aliased an unrelated pair -- reachable the moment anything authors
     a 256-colour or RGB value, which is what this palette exists to allow. See the
     `packing` case in dev/probes/palette/. */
  for (i = 0; i < sCount; i++)
    if (sFg[i] == fg && sBg[i] == bg) return (short)(i + 1);

  if (sCount + 1 >= cap) {
    if (!sReported) {
      sReported = 1;
      IRCITUI_LOG_WARN("palette: pair space exhausted at %d pairs (COLOR_PAIRS=%d, cap=%d); returning the terminal default", sCount, COLOR_PAIRS,
                       cap);
    }
    return 0;
  }

  if (init_pair((short)(sCount + 1), fg, bg) == ERR) return 0;

  sFg[sCount] = fg;
  sBg[sCount] = bg;
  sCount++;
  return (short)sCount;
}

chtype
IrcitUiPaletteStyle(IrcitUiPaletteSlot slot)
{
  if (slot < 0 || slot >= IRCITUI_SLOT_COUNT) return A_NORMAL;

  sSlotsInit();

  // No colour at all: the attributes still say something, and A_REVERSE on the overlay * slots is what the snackbar has always fallen back to.
  if (sColourLevel() < 0) return sSlotAttr[slot];

  // A pair that cannot be had resolves to 0, the terminal's default, which leaves the * attributes doing the work -- the same outcome as the no-colour case.
  return sSlotAttr[slot] | (chtype)COLOR_PAIR(IrcitUiColourPair(sSlotFg[slot], sSlotBg[slot]));
}

void
IrcitUiPaletteSetSlot(IrcitUiPaletteSlot slot, short fg, short bg, chtype attr)
{
  if (slot < 0 || slot >= IRCITUI_SLOT_COUNT) return;

  sSlotsInit();

  if (fg >= 0) sSlotFg[slot] = fg;
  if (bg >= 0) sSlotBg[slot] = bg;
  if (attr != (chtype)-1) sSlotAttr[slot] = attr;
}

int
IrcitUiPaletteSlotForName(const char *name_ptr)
{
  int i;

  if (!name_ptr) return -1;
  for (i = 0; i < IRCITUI_SLOT_COUNT; i++)
    if (!strcmp(sSlotNames[i], name_ptr)) return i;
  return -1;
}

void
IrcitUiPaletteApplyProfile(const IrcitUiPaletteProfile *profile_ptr)
{
  if (!profile_ptr) return;

  /* Deliberately does NOT repaint, and the caller is told so in the header. A profile
     change moves pairs, and a changed pair reaches the glass only when the cells holding
     it are written again -- so the repaint is the caller's, and putting it here would
     make the palette reach up into the screen, which is the layer it sits below. */
  sApplyProfile(profile_ptr);
}

const IrcitUiPaletteProfile *
IrcitUiPaletteProfileByName(const char *name_ptr)
{
  size_t i;

  if (!name_ptr) return NULL;
  for (i = 0; i < sizeof sProfiles / sizeof sProfiles[0]; i++)
    if (!strcmp(sProfiles[i]->name, name_ptr)) return sProfiles[i];
  return NULL;
}

void
IrcitUiPaletteRepaint(void)
{
  /* Named rather than left to the caller to remember, because forgetting it is
     invisible: the palette changes, nothing repaints, and the screen keeps the old
     colours until something unrelated dirties it. That is exactly how this was found --
     see the Ctrl-T note in dev/probes/palette/README.md. */
  if (IrcitUiScreen.root) IrcitUiInvalidate(IrcitUiScreen.root);
}

static unsigned sEffects = PRIV_CONFIG_DEFAULT_PALETTE_EFFECTS;

/* The frame's glyph profile, and it is not an effect: nothing here borrows a register,
   nothing here costs the user their palette at exit, and the default is not "off" but
   "the box that always shipped". So it is deliberately NOT a bit in sEffects -- a caller
   who wants rounded frames should not have to enable the register machinery to get them.

   IRCITUI_BORDER_STANDARD is answered with NULL from the table below rather than with six
   ACS-glyph entries, and the reason is that box() does something a table cannot: it draws
   the corners ncurses chooses for the terminal's acsc capability. Writing ACS_ULCORNER
   into six strings would look equivalent and would not be, so the default stays a call. */
static IrcitUiBorderStyle sBorderStyle = IRCITUI_BORDER_STANDARD;

/* Indexed by IrcitUiBorderStyle, in that enum's declared order: top-left, top-right,
   bottom-left, bottom-right, horizontal, vertical. One glyph per string, which is the
   shape the engine's own glyph writer takes and the shape the layout grammar's Glyph
   rule already requires of a per-node glyph (C15). */
static const char *const sBorderRounded[6] = {"╭", "╮", "╰", "╯", "─", "│"};
static const char *const sBorderThick[6]   = {"┏", "┓", "┗", "┛", "━", "┃"};
static const char *const sBorderDouble[6]  = {"╔", "╗", "╚", "╝", "═", "║"};

static const char *const *const sBorderProfiles[IRCITUI_BORDER_COUNT] = {
    NULL, /* IRCITUI_BORDER_STANDARD: draw the ACS box, which is not a table lookup */
    sBorderRounded,
    sBorderThick,
    sBorderDouble,
};

/* Read the mask without the side effects IrcitUiPaletteSetEffects has -- the fade's
   gate must not take the canopy down on its way past. */
static unsigned
IrcitUiEffectsRaw(void)
{
  return sEffects;
}

/* IrcitUiPaletteSetEffects and IrcitUiPaletteEffects are defined after the canopy
   section below: switching the canopy off has to take it down, so they need its state. */


/* --- The ramps.
 *
 * A small table rather than a single range, because the effects need two at once: the
 * canopy is a vertical ramp of backgrounds and the alpha layer is a ramp of foregrounds
 * derived from it, and a fade needs its own on top. The cap is four -- the effects that
 * exist plus one spare -- and exhausting it is a refusal rather than a queue.
 */
/* THE INITIALISER IS LOAD-BEARING, and leaving it off cost an hour.
 *
 * `base` is -1 when a slot is free, and a static array without an initialiser is
 * ZERO-initialised -- so every slot looked occupied and Acquire returned -1 for every
 * request, forever, with nothing said. It is the same trap this tree documents twice
 * already: mark_fg's "-1 is the unset value, not 0, because black is a colour a mark may
 * be", and sticky_headers' "-1 is off, because 0 is a legal type_id". A field whose
 * invalid value is a legal value must never be left to zero-initialisation. */
static struct {
  int         base;  /* -1 when the slot is free */
  int         count;
  const char *owner; /* a literal, compared by POINTER -- see Release */
  short *     savedR;
  short *     savedG;
  short *     savedB;
} sRamps[PRIV_CONFIG_DEFAULT_PALETTE_RAMPS] = {
    {.base = -1},
    {.base = -1},
    {.base = -1},
    {.base = -1},
};

/* The saved contents live on the heap: a ramp is a screen's worth of registers, and a
   fixed array sized for the worst case would be most of a kilobyte per ramp held for
   the life of the process to serve the seconds a ramp is live. */
static void
sRampForget(int i)
{
  free(sRamps[i].savedR);
  free(sRamps[i].savedG);
  free(sRamps[i].savedB);
  sRamps[i].savedR = NULL;
  sRamps[i].savedG = NULL;
  sRamps[i].savedB = NULL;
  sRamps[i].base   = -1;
  sRamps[i].count  = 0;
  sRamps[i].owner  = NULL;
}

/* Is this register one a live slot definition names, or one a live ramp already holds?
   Writing either would repaint somebody else's cells. */
static int
sRegisterTaken(int reg)
{
  int i;

  for (i = 0; i < IRCITUI_SLOT_COUNT; i++)
    if (sSlotFg[i] == reg || sSlotBg[i] == reg) return 1;

  for (i = 0; i < PRIV_CONFIG_DEFAULT_PALETTE_RAMPS; i++)
    if (sRamps[i].base >= 0 && reg >= sRamps[i].base && reg < sRamps[i].base + sRamps[i].count) return 1;

  return 0;
}

/* Test that a register can actually be written, by writing it and reading it back.
 *
 * The capability strings are not enough: this library's own probe found initc present on
 * a terminal where a direct-colour entry is absent, so the only honest test is
 * behavioural. The original contents are restored before returning. */
static int
sRegisterWritable(int reg)
{
  short r, g, b, r2, g2, b2;

  color_content((short)reg, &r, &g, &b);
  if (init_color((short)reg, 1000, 0, 500) == ERR) return 0;
  color_content((short)reg, &r2, &g2, &b2);
  init_color((short)reg, r, g, b);
  return r2 == 1000 && g2 == 0 && b2 == 500;
}

static int
sRampIndex(int base)
{
  int i;

  for (i = 0; i < PRIV_CONFIG_DEFAULT_PALETTE_RAMPS; i++)
    if (sRamps[i].base == base) return i;
  return -1;
}

int
IrcitUiPaletteRampAcquire(int count, const char *owner_ptr)
{
  int i, slot = -1, base = -1;

  if (count <= 0) return -1;
  if (sColourLevel() < 0 || COLORS <= 16) return -1;

  sSlotsInit();

  for (i = 0; i < PRIV_CONFIG_DEFAULT_PALETTE_RAMPS; i++)
    if (sRamps[i].base < 0) {
      slot = i;
      break;
    }
  if (slot < 0) return -1;

  /* The first run of `count` free registers at or above 16.
   *
   * NOT from the top down, which is what this did first and what refused itself: the
   * palette's own colours are the xterm greys, and those sit at 232-255, so a ramp taken
   * from the top landed on 224-255 and every register in it was already in use. The
   * palette owns the top of the space; a ramp finds its own gap.
   *
   * A contiguous run rather than a scatter of free registers, because a ramp is addressed
   * as base + step and a map would be a second lookup in the effects' inner loops for no
   * gain -- the gaps between the palette's colours are wide enough. */
  for (i = 16; i + count <= COLORS; i++) {
    int j, run = 1;
    for (j = 0; j < count; j++)
      if (sRegisterTaken(i + j)) {
        run = 0;
        break;
      }
    if (run) {
      base = i;
      break;
    }
  }
  if (base < 0) return -1;

  if (!sRegisterWritable(base)) return -1;

  sRamps[slot].savedR = malloc((size_t)count * sizeof(short));
  sRamps[slot].savedG = malloc((size_t)count * sizeof(short));
  sRamps[slot].savedB = malloc((size_t)count * sizeof(short));
  if (!sRamps[slot].savedR || !sRamps[slot].savedG || !sRamps[slot].savedB) {
    sRampForget(slot);
    return -1;
  }

  for (i = 0; i < count; i++) color_content((short)(base + i), &sRamps[slot].savedR[i], &sRamps[slot].savedG[i], &sRamps[slot].savedB[i]);

  sRamps[slot].base  = base;
  sRamps[slot].count = count;
  sRamps[slot].owner = owner_ptr;
  IRCITUI_LOG_INFO("palette: borrowed %d colour registers at %d for %s", count, base, owner_ptr ? owner_ptr : "(unnamed)");
  return base;
}

void
IrcitUiPaletteRampSet(int base, int step, int r, int g, int b)
{
  int i = sRampIndex(base);

  if (i < 0 || step < 0 || step >= sRamps[i].count) return;

  // 8-bit to ncurses' 0-1000, in integer arithmetic: the effects loop over these.
  init_color((short)(base + step), (short)(r * 1000 / 255), (short)(g * 1000 / 255), (short)(b * 1000 / 255));
}

chtype
IrcitUiPaletteRampStyle(int base, int step, short fg)
{
  int i = sRampIndex(base);

  if (i < 0 || step < 0 || step >= sRamps[i].count) return A_NORMAL;

  return (chtype)COLOR_PAIR(IrcitUiColourPair(fg, (short)(base + step)));
}

void
IrcitUiPaletteRampRelease(int base, int count, const char *owner_ptr)
{
  int i = sRampIndex(base), k;

  if (i < 0) return;

  /* OWNERSHIP, and this is the check that a stale handle cannot get past.
   *
   * A caller that remembers a base across a configuration change can find that base
   * belongs to somebody else by the time it lets go: the notification box held 46, the
   * gradient was switched off, the canopy took 46 -- and the box's release then restored
   * ncurses' model colours over the canopy's registers and freed the canopy's table entry.
   * The canopy went on painting in the model's defaults, and its pulse wrote nothing,
   * because a released base makes RampSet a no-op.
   *
   * The owner is a borrowed literal compared by POINTER, not by strcmp: every caller passes
   * a string literal, so identity is what is meant, and a strcmp in a release path would
   * be a comparison of content nobody varies. */
  if (sRamps[i].owner != owner_ptr) {
    IRCITUI_LOG_WARN("palette: %s tried to release the ramp at %d, which belongs to %s", owner_ptr ? owner_ptr : "(unnamed)", base,
                     sRamps[i].owner ? sRamps[i].owner : "(unnamed)");
    return;
  }

  for (k = 0; k < count && k < sRamps[i].count; k++)
    init_color((short)(base + k), sRamps[i].savedR[k], sRamps[i].savedG[k], sRamps[i].savedB[k]);

  sRampForget(i);
}

/* --- The canopy: a vertical ramp of backgrounds, and the foregrounds derived from it.
 *
 * Two ramps over the same axis, one step per screen row. The canopy is what a container
 * wearing the `surface` slot is filled with; the alpha ramp is the foreground a node
 * wearing `text` is drawn in, pre-blended against the canopy row it sits on -- the
 * blending is arithmetic on a register, not a per-cell computation, which is the only
 * reason a blended foreground is affordable at all.
 *
 * Held once for the screen rather than per node: the ramp is a function of the ROW, and
 * two nodes on the same row must agree about it or the backdrop would seam at every
 * boundary.
 */
static int sCanopyBase = -1;
static int sCanopyRows;
static int sAlphaBase = -1;
static int sAlphaRows;

/* The two endpoints, as 8-bit RGB. Deliberately close together: the canopy is a backdrop
   and a backdrop that competes with the text on it is a bug, not a feature. */
#define CANOPY_TOP_R 16
#define CANOPY_TOP_G 24
#define CANOPY_TOP_B 48
#define CANOPY_BOT_R 6
#define CANOPY_BOT_G 10
#define CANOPY_BOT_B 22
#define CANOPY_TEXT_R 236
#define CANOPY_TEXT_G 240
#define CANOPY_TEXT_B 248

static void
sCanopyWrite(int base, int rows, int phase)
{
  int i;

  for (i = 0; i < rows; i++) {
    int t = rows > 1 ? i * 255 / (rows - 1) : 0;
    /* A triangular wave on `phase`, so the pulse is a modifier of the SAME write rather
       than a second code path that could drift from the static one. At phase 0 the ramp
       is exactly the static canopy. */
    int lift = phase;
    IrcitUiPaletteRampSet(base, i, CANOPY_TOP_R + (CANOPY_BOT_R - CANOPY_TOP_R) * t / 255 + lift / 4,
                          CANOPY_TOP_G + (CANOPY_BOT_G - CANOPY_TOP_G) * t / 255 + lift / 6,
                          CANOPY_TOP_B + (CANOPY_BOT_B - CANOPY_TOP_B) * t / 255 + lift / 3);
  }
}

/* Bring the canopy and the alpha layer up to date for a screen of @p rows, and return the
   canopy base (or -1). Called from the paint path, so it must be cheap when nothing has
   changed -- which is the common case, and why the rows are compared first. */
int
IrcitUiPaletteCanopyEnsure(int rows)
{
  int i;

  if (!(IrcitUiPaletteEffects() & IRCITUI_PALETTE_EFFECT_CANOPY)) return -1;
  if (rows <= 0) return -1;
  if (sCanopyBase >= 0 && sCanopyRows == rows) return sCanopyBase;

  if (sCanopyBase >= 0) {
    IrcitUiPaletteRampRelease(sCanopyBase, sCanopyRows, "canopy");
    sCanopyBase = -1;
  }
  if (sAlphaBase >= 0) {
    IrcitUiPaletteRampRelease(sAlphaBase, sAlphaRows, "canopy-alpha");
    sAlphaBase = -1;
  }

  sCanopyBase = IrcitUiPaletteRampAcquire(rows, "canopy");
  if (sCanopyBase < 0) return -1;
  sCanopyRows = rows;
  sCanopyWrite(sCanopyBase, rows, 0);

  /* The alpha layer is not optional: without it the text on the canopy is drawn in the
     slot's flat colour, which reads as text that does not belong to its backdrop. If it
     cannot be had, the canopy comes down rather than shipping half the effect. */
  sAlphaBase = IrcitUiPaletteRampAcquire(rows, "canopy-alpha");
  if (sAlphaBase < 0) {
    IrcitUiPaletteRampRelease(sCanopyBase, sCanopyRows, "canopy");
    sCanopyBase = -1;
    sCanopyRows = 0;
    return -1;
  }
  sAlphaRows = rows;

  /* The blend, pre-computed per row: text at 88% against the canopy row behind it. The
     arithmetic is on a register and runs once per acquisition, not per frame and never
     per cell. */
  for (i = 0; i < rows; i++) {
    int t   = rows > 1 ? i * 255 / (rows - 1) : 0;
    int bgR = CANOPY_TOP_R + (CANOPY_BOT_R - CANOPY_TOP_R) * t / 255;
    int bgG = CANOPY_TOP_G + (CANOPY_BOT_G - CANOPY_TOP_G) * t / 255;
    int bgB = CANOPY_TOP_B + (CANOPY_BOT_B - CANOPY_TOP_B) * t / 255;
    IrcitUiPaletteRampSet(sAlphaBase, i, (88 * CANOPY_TEXT_R + 12 * bgR) / 100, (88 * CANOPY_TEXT_G + 12 * bgG) / 100,
                          (88 * CANOPY_TEXT_B + 12 * bgB) / 100);
  }

  IRCITUI_LOG_INFO("palette: canopy up over %d rows (backgrounds at %d, blended foregrounds at %d)", rows, sCanopyBase, sAlphaBase);
  return sCanopyBase;
}

chtype
IrcitUiPaletteCanopyStyle(int row)
{
  if (sCanopyBase < 0 || row < 0 || row >= sCanopyRows) return A_NORMAL;
  return IrcitUiPaletteRampStyle(sCanopyBase, row, COLOR_WHITE);
}

chtype
IrcitUiPaletteCanopyTextStyle(int row)
{
  if (sCanopyBase < 0 || sAlphaBase < 0 || row < 0 || row >= sCanopyRows) return A_NORMAL;

  /* The pair is (foreground, background), and BOTH halves are registers: the blended
     foreground for this row over the canopy's background for this row. Written the other
     way round first, which put the backdrop in the text's channel -- the cells then
     carried the alpha register as their background, and the capture showed it. */
  return IrcitUiPaletteRampStyle(sCanopyBase, row, (short)(sAlphaBase + row));
}

/* Advance the pulse by a phase in 0..255, rewriting both ramps.
 *
 * The two endpoints breathe TOGETHER -- one lift applied to the whole ramp rather than a
 * moving highlight -- because a band travelling down a backdrop reads as a progress
 * indicator, which is a different widget. A triangular wave rather than a sine: it is two
 * comparisons and no libm, and at this amplitude the eye cannot tell them apart.
 *
 * Costs 2 x rows init_color calls per call and NO cell work: the cells already point at
 * these registers, so the terminal recolours them. That is the whole reason the pulse is
 * affordable, and it is measured -- see p_register_propagation.c. */
void
IrcitUiPaletteCanopyPulse(void)
{
  static int sPhase = 0;
  int        rows   = sCanopyRows;
  int        lift;

  if (sCanopyBase < 0) return;
  if (!(IrcitUiPaletteEffects() & IRCITUI_PALETTE_EFFECT_PULSE)) return;

  // A full cycle every 128 calls. At the loop's 20 Hz cadence that is six seconds, which is slow enough to read as breathing and fast enough to notice.
  sPhase = (sPhase + 1) & 0x7f;
  lift   = sPhase < 64 ? sPhase * 4 : (127 - sPhase) * 4;

  sCanopyWrite(sCanopyBase, rows, lift);

  /* The blended foregrounds follow the backdrop they were derived from. Without this the
     text would keep the colours of the canopy's previous phase and drift out of step
     with the band behind it. */
  {
    int i;
    for (i = 0; i < rows; i++) {
      int t   = rows > 1 ? i * 255 / (rows - 1) : 0;
      int bgR = CANOPY_TOP_R + (CANOPY_BOT_R - CANOPY_TOP_R) * t / 255 + lift / 4;
      int bgG = CANOPY_TOP_G + (CANOPY_BOT_G - CANOPY_TOP_G) * t / 255 + lift / 6;
      int bgB = CANOPY_TOP_B + (CANOPY_BOT_B - CANOPY_TOP_B) * t / 255 + lift / 3;
      IrcitUiPaletteRampSet(sAlphaBase, i, (88 * CANOPY_TEXT_R + 12 * bgR) / 100, (88 * CANOPY_TEXT_G + 12 * bgG) / 100,
                            (88 * CANOPY_TEXT_B + 12 * bgB) / 100);
    }
  }
}

int
IrcitUiPaletteAnimating(void)
{
  /* The cadence question, and the only honest answer to it: an animation is running when
     the effect is on AND the ramp it animates is actually up. Enabling the effect on a
     terminal that refused the registers must NOT spin the loop at frame rate. */
  if (!(IrcitUiPaletteEffects() & IRCITUI_PALETTE_EFFECT_PULSE)) return 0;
  return sCanopyBase >= 0;
}

void
IrcitUiPaletteSetEffects(unsigned mask)
{
  /* Switching the canopy OFF takes it down here rather than at the next Ensure, because
     Ensure's job is to bring it up and it returns early when the effect is off -- so the
     registers would stay borrowed for the life of the process, the cells would keep
     pointing at them, and IrcitUiPaletteCanopyActive would keep saying yes, which would
     keep the pulse running and the loop at frame rate for an effect nobody is using. */
  if (!(mask & IRCITUI_PALETTE_EFFECT_CANOPY)) {
    if (sCanopyBase >= 0) {
      IrcitUiPaletteRampRelease(sCanopyBase, sCanopyRows, "canopy");
      sCanopyBase = -1;
      sCanopyRows = 0;
    }
    if (sAlphaBase >= 0) {
      IrcitUiPaletteRampRelease(sAlphaBase, sAlphaRows, "canopy-alpha");
      sAlphaBase = -1;
      sAlphaRows = 0;
    }
  }

  sEffects = mask;
}

unsigned
IrcitUiPaletteEffects(void)
{
  return sEffects;
}

/* --- The frame's glyph profile.

   Two public calls and one private accessor, and no state beyond a single enum: the
   profile is read where the frame is drawn, which is the engine and the overlay, so
   neither of them has to be told that the style changed -- they have to be REPAINTED,
   which is the caller's half and is what IrcitUiSetBorderStyle's own comment says. */

const char *const *
IrcitUiBorderGlyphs(int style)
{
  if (style < 0 || style >= (int)IRCITUI_BORDER_COUNT) return NULL;
  return sBorderProfiles[style];
}

void
IrcitUiSetBorderStyle(IrcitUiBorderStyle style)
{
  /* Out of range leaves the current one rather than snapping to STANDARD: a caller
     cycling a counter that overshot would otherwise get a silent reset to the ACS box,
     which is a visible change and the wrong one. Same reasoning as
     IrcitUiPaletteSetSlot's out-of-range slot being ignored. */
  if (style < 0 || style >= IRCITUI_BORDER_COUNT) return;
  sBorderStyle = style;
}

IrcitUiBorderStyle
IrcitUiBorderStyleGet(void)
{
  return sBorderStyle;
}

/* --- The popup drop shadow's switch.
 *
 * One flag, and the drawing is the engine's: the shadow is stamped into a PARENT's cells,
 * which only the draw pass may do, so there is nothing here to implement beyond the switch
 * and nothing for the overlay to ask for either -- the overlay is not a node and has no
 * parent to cast onto. */

static int sShadow = PRIV_CONFIG_DEFAULT_SHADOW;

void
IrcitUiSetShadow(int on)
{
  sShadow = on ? 1 : 0;
}

int
IrcitUiShadow(void)
{
  return sShadow;
}

/* --- Fades.
 *
 * A fade is one register interpolated from a widget's own colour toward the backdrop it
 * is disappearing into, stepped by the frame loop, with a callback when it arrives. It
 * exists because a widget cannot simply stop being drawn: something has to keep painting
 * its cells while the colour moves, and something has to say when the last frame is done
 * so the application can tear the widget down. That second half is the callback, and it
 * is the reason this is an API rather than a decoration.
 *
 * All of them share one borrowed range: a fade needs a single register, and taking a ramp
 * per fade would exhaust the table that the screen-wide effects need.
 */
typedef struct {
  int               live;
  short             from_fg;
  short             from_bg;
  unsigned          duration_ms;
  long long         started_ms;
  IrcitUiFadeDone   on_done;
  void *            user_ptr;
} FadeSlot;

static FadeSlot sFades[PRIV_CONFIG_DEFAULT_PALETTE_FADES];
static int      sFadeBase = -1;

static long long
sPaletteNowMs(void)
{
  struct timespec ts;

  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (long long)ts.tv_sec * 1000LL + (long long)(ts.tv_nsec / 1000000L);
}

int
IrcitUiPaletteFadeStart(short from_fg, short from_bg, unsigned duration_ms, IrcitUiFadeDone on_done_ptr, void *user_ptr)
{
  int i;

  if (!(IrcitUiEffectsRaw() & IRCITUI_PALETTE_EFFECT_FADE)) return -1;

  // The shared range comes up on first use, and stays up: the fades in flight change, but the registers they use do not.
  if (sFadeBase < 0) {
    sFadeBase = IrcitUiPaletteRampAcquire(PRIV_CONFIG_DEFAULT_PALETTE_FADES, "fade");
    if (sFadeBase < 0) return -1;
  }

  for (i = 0; i < PRIV_CONFIG_DEFAULT_PALETTE_FADES; i++) {
    if (sFades[i].live) continue;

    sFades[i].live        = 1;
    sFades[i].from_fg     = from_fg;
    sFades[i].from_bg     = from_bg;
    sFades[i].duration_ms = duration_ms ? duration_ms : PRIV_CONFIG_DEFAULT_PALETTE_FADE_MS;
    sFades[i].started_ms  = sPaletteNowMs();
    sFades[i].on_done     = on_done_ptr;
    sFades[i].user_ptr    = user_ptr;

    /* It starts at the widget's own colour, so the first frame of the fade is identical
       to the last frame before it -- a fade that starts somewhere else jumps.

       The colour is passed as 8-BIT and NOT pre-scaled, because RampSet scales it. This
       was written the other way first -- `from_fg * 1000 / 255` -- and the double scaling
       clamped every tick to 1000, so the register never changed value, ncurses emitted
       the one OSC 4 and then nothing, and a working fade looked like a mechanism that
       never reached the terminal. Measured: one write where twenty were due. */
    IrcitUiPaletteRampSet(sFadeBase, i, from_fg, from_fg, from_fg);
    return i + 1;
  }

  return -1;
}

chtype
IrcitUiPaletteFadeStyle(int handle)
{
  int i = handle - 1;

  if (i < 0 || i >= PRIV_CONFIG_DEFAULT_PALETTE_FADES || !sFades[i].live || sFadeBase < 0) return A_NORMAL;

  /* The register is the FOREGROUND, fading into the background it sits on: text that
     dissolves into its backdrop. The pair is (fading register, the widget's own
     background), so the cells it is painted on keep their backdrop. */
  return IrcitUiPaletteRampStyle(sFadeBase, i, (short)(sFadeBase + i));
}

void
IrcitUiPaletteFadeTick(void)
{
  long long now;
  int       i;

  if (sFadeBase < 0) return;
  now = sPaletteNowMs();

  for (i = 0; i < PRIV_CONFIG_DEFAULT_PALETTE_FADES; i++) {
    long long elapsed;
    int       t, fg;

    if (!sFades[i].live) continue;

    elapsed = now - sFades[i].started_ms;
    t       = elapsed >= (long long)sFades[i].duration_ms ? 255 : (int)(elapsed * 255 / (long long)sFades[i].duration_ms);

    /* Toward the background's grey, which is what "dissolving into the backdrop" means
       when the backdrop's colour is not known here: the register ends up matching the
       luminance of where it sits rather than an arbitrary black. */
    fg = sFades[i].from_fg + (sFades[i].from_bg - sFades[i].from_fg) * t / 255;
    if (fg < 0) fg = 0;
    if (fg > 255) fg = 255;

    // 8-bit, not pre-scaled -- see the note in FadeStart.
    IrcitUiPaletteRampSet(sFadeBase, i, fg, fg, fg);

    if (t >= 255) {
      /* Done. The callback runs BEFORE the slot is released, so a callback that asks
         whether its own fade is still live gets a truthful answer, and one that starts
         another fade finds the slot it is in still occupied -- which is what stops a
         chained fade from reusing a register the frame is still painting. */
      sFades[i].on_done = sFades[i].on_done;
      if (sFades[i].on_done) sFades[i].on_done(sFades[i].user_ptr);
      sFades[i].live     = 0;
      sFades[i].on_done  = NULL;
      sFades[i].user_ptr = NULL;
    }
  }
}

/* Hand every borrowed register back, whatever is holding it.
 *
 * The mitigation R-3 names for the exit cost, with its fidelity stated rather than
 * implied: it restores what ncurses BELIEVED the registers held, which is not necessarily
 * what the emulator had, because ncurses never queries the terminal. So it is an
 * approximation of the user's palette, not a guarantee of it.
 *
 * It also cannot undo the one thing that matters most: ncurses emits OSC 104 (reset
 * palette) at exit once any register has been written, and that lands after whatever a
 * caller does here. A caller that wants this to be the last word must call it AFTER
 * endwin -- and whether ncurses honours a register write outside curses mode is a property
 * of ncurses, not of this function; p_ramp's `restore` case measures it rather than
 * assuming either way.
 */
void
IrcitUiPaletteRestore(void)
{
  int i;

  for (i = 0; i < PRIV_CONFIG_DEFAULT_PALETTE_RAMPS; i++)
    if (sRamps[i].base >= 0) IrcitUiPaletteRampRelease(sRamps[i].base, sRamps[i].count, sRamps[i].owner);

  /* Everything that held a range is now holding nothing, so the state that said "a canopy
     is up" or "a fade is running" has to go with it -- otherwise the pulse would keep
     animating registers nobody owns, and the loop would keep its frame cadence for an
     effect that is no longer on the screen. */
  sCanopyBase = -1;
  sCanopyRows = 0;
  sAlphaBase  = -1;
  sAlphaRows  = 0;
  sFadeBase   = -1;
  for (i = 0; i < PRIV_CONFIG_DEFAULT_PALETTE_FADES; i++) {
    sFades[i].live     = 0;
    sFades[i].on_done  = NULL;
    sFades[i].user_ptr = NULL;
  }
}

int
IrcitUiPaletteFadeHandle(void)
{
  int i;

  for (i = 0; i < PRIV_CONFIG_DEFAULT_PALETTE_FADES; i++)
    if (sFades[i].live) return i + 1;
  return 0;
}

int
IrcitUiPaletteFadeActive(void)
{
  int i;

  for (i = 0; i < PRIV_CONFIG_DEFAULT_PALETTE_FADES; i++)
    if (sFades[i].live) return 1;
  return 0;
}

int
IrcitUiPaletteCanopyActive(void)
{
  return sCanopyBase >= 0;
}

int
IrcitUiPalettePairCount(void)
{
  return sCount;
}

void
IrcitUiPalettePairReset(void)
{
  free(sFg);
  free(sBg);
  sFg       = NULL;
  sBg       = NULL;
  sCount    = 0;
  sCap      = 0;
  sReported = 0;
}
