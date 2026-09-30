/**
 * @file harness_client.c
 * @brief The MSP harness client: a TUI that renders the simulation server's stream.
 *
 * @details The other half of the pair.  It is a SEPARATE PROGRAM from the demo, which is the
 *          whole point of the track: the simulation used to live in `src/examples/ui_main.c`,
 *          and this is where it went.  The demo is untouched by it.
 *
 *          ## What it renders, and what it cannot
 *
 *          §28's channel table maps three channels onto a three-state media bar with a
 *          pre-fetch track, a circular dial gauge, and a standard bar.  Measured before this
 *          file was written (`dev/probes/harness/`): **two of those three widgets do not
 *          exist in this tree.**  There is no dial kind and no media-bar kind -- no `dial`,
 *          no `gauge`, no `prefetch` field anywhere in `src/`, `include/`, `layout/` or
 *          `tools/`.  So the channels are mapped onto the bars the tree has, and the mapping
 *          is the table below rather than anything in the protocol.  When §21 and §24/§25
 *          land, this table changes and the wire does not.
 *
 *          | channel          | value            | buffered           | readout             |
 *          |------------------|------------------|--------------------|---------------------|
 *          | CH_MEDIA_YOUTUBE | played           | pre-fetch          | time, buffer, label |
 *          | CH_DIAL_CPU      | core load        | --                 | throttle, label     |
 *          | CH_DL_LARGE      | download         | --                 | throughput, label   |
 *
 *          The media row is the one that earns its keep: §21's pre-fetch track is exactly the
 *          bar's SECOND position, so that channel drives both fields of one bar and the band
 *          between them is the cache.  That is what the buffered track was built for.
 *
 *          ## It is a genuine API consumer
 *
 *          The target is handed `include/` through the library's public interface and
 *          **deliberately not `src/include`** -- the boundary `ircitui_window_demo` holds.  It
 *          cannot reach a private header even by accident, so its compiling is the assertion
 *          that everything it does is public.  The one exception is the compile-time constant
 *          it copies for its exchange buffer, which is why the copy exists rather than an
 *          include.
 *
 *          ## The three threads
 *
 *          Thread 0 owns the tree and everything that touches it.  A reader thread owns the
 *          socket's read side and does nothing but hand bytes over.  A worker never sees a
 *          node: it posts an id and a number, and Thread 0 resolves it.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "msp_protocol.h"

#include <curses.h>
#include <errno.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <threads.h>
#include <unistd.h>

#include "ircituilib/ui.h"
#include "ircituilib/main_looper.h"
#include "ircituilib/window_driver/ircitui_window_registry.h"

/* AFTER ircituilib, not before: ircituilib_defs.h defines PUBLIC_API empty and uflib
   defines it as a visibility attribute, so reaching uflib first is a
   -Wmacro-redefined error under this target's -Werror.  The library's own files
   carry the same note. */
#include <ufsrvrxlib/ufsrv_future/ufsrv_future.h>
#include "ircituilib/window_driver/ircitui_window_registry.h"

/* The server's exchange buffer is PRIV_CONFIG_DEFAULT_CLIPBUF_SZ - 1 = 511 bytes, and that
 * number is private to the library.  The copy is deliberate: a probe file could include the
 * private header, this one may not, and hard-coding the value here is what makes the
 * dependency visible rather than hidden behind an include.  It is only a read size -- the
 * framework refuses anything larger than its own envelope, so a mismatch is a refused chunk
 * and a logged error rather than a silent fault. */
#define CLIENT_READ_MAX 511

/* The gallery is layout_width 40 and its content runs to about 26 rows. */
#define PANEL_W 40
#define PANEL_H 30

/** @brief The default rendezvous; argv[1] overrides it, as on the server. */
static const char *s_sock_path = MSP_SOCKET_PATH;

/** @brief The locked backend.  Public API: the registry owns the terminal. */
static IrcitUiWindowRegistry *s_registry;

/**
 * @brief Called once the backend has taken the terminal.
 *
 * @details THIS IS WHERE THE APPLICATION'S TERMINAL SETUP BELONGS, and the first version of
 *          this file got it wrong in a way worth recording: it called `start_color()` in
 *          `main`, before any backend existed, and never initialised a screen at all -- the
 *          program ran, connected to the server, and drew nothing, because ncurses' screen
 *          did not exist yet.  `ui_palette.c` is explicit that `start_color` is the
 *          APPLICATION's call to make, and this hook is the first moment at which making it
 *          means anything.
 */
static void
sOnDriverInitialised(IrcitUiWindowDriver *driver_ptr)
{
  (void)driver_ptr;

  /* THE REGISTRY LOCKS A BACKEND; IT DOES NOT TAKE THE TERMINAL.  This hook is where the
   * application does that, and the first two versions of this file got it wrong in the same
   * way twice: the first never called these at all, so the program connected to its server,
   * ran its loop, and emitted not one escape sequence -- a TUI that drew nothing because
   * ncurses' screen did not exist.  The demo does exactly this in its own hook, which is
   * where the sequence below was read from rather than invented.
   *
   * THE ORDER MATTERS.  NCURSES_NO_UTF8_ACS is set before initscr because ncurses reads it
   * as it initialises and afterwards has no effect -- and without it ncurses substitutes its
   * own line-drawing glyphs for the ACS characters, displacing the box-drawing the engine
   * emits.  curs_set(0) because the engine paints the only caret there is, and the hardware
   * one would sit in an edit field as a second, stationary one.  set_escdelay(25) because a
   * lone ESC is otherwise held for ESCDELAY -- a full second by default -- before ncurses
   * decides it is not the head of a sequence, and this loop wakes on the caret's tick. */
  setenv("NCURSES_NO_UTF8_ACS", "1", 0);

  initscr();
  cbreak();
  noecho();
  keypad(stdscr, TRUE);
  if (has_colors()) start_color();
  curs_set(0);
  set_escdelay(25);
}

/* ── The channel mapping ────────────────────────────────────────────────────── */

/** @brief Which gallery node each channel drives, and where its readout goes. */
static const struct {
  int         channel;
  const char *bar_id;      /**< primary -> IrcitUiBarSetValue */
  const char *buffered_id; /**< secondary -> IrcitUiBarSetBuffered, or NULL */
  const char *readout_id;  /**< the frame's summary and label, as text */
} s_channels[] = {
  { CH_MEDIA_YOUTUBE, "bar_buffered", "bar_buffered", "prog_buffered_label" },
  { CH_DIAL_CPU,      "bar_accent",   NULL,           "prog_accent_label"   },
  { CH_DL_LARGE,      "bar_wide",     NULL,           "prog_wide_label"     },
};
#define CHANNEL_COUNT ((int)(sizeof s_channels / sizeof s_channels[0]))

/**
 * @brief The last frame seen on each channel, so the readout can be rebuilt without one.
 *
 * @details §32's expiry clears the label and must leave the NUMBERS standing -- a label
 *          expiring is a message going away, not the widget losing its value.  Rebuilding the
 *          readout therefore needs the frame that produced it, and this is that frame.
 */
static MspFrame s_last[CHANNEL_COUNT];

/**
 * @brief One armed expiry per channel, and STATIC rather than heap.
 *
 * @details §32 models a label's death as a promise: armed when the label arrives, usually
 *          cancelled before it fires (the next label cancels it), armed again.  The obvious
 *          shape gives each arming a `malloc`'d context freed by its continuation -- and that
 *          leaks on every cancellation, because a continuation of an ABANDONED promise never
 *          runs and never frees.
 *
 *          dev/probes/harness/p_expiry.c measures both halves of the alternative: the
 *          continuation runs on whichever thread completes the promise, and it is handed
 *          whatever context the armer chose.  A static array indexed by channel has no
 *          lifetime to get wrong, and one expiry per channel is all this program can have --
 *          a second label on one channel REPLACES the first, it does not expire beside it.
 */
typedef struct {
  int           armed;
  int           ticks_left;
  UfsrvPromise *promise;   /**< NULL when not armed; the promise frees itself on completion */
  int           channel;
} LabelExpiry;

static LabelExpiry s_expiry[CHANNEL_COUNT];

/* One buffer per channel, and FILE-SCOPE because a node BORROWS its text: the painter
 * resolves the node's alias on every frame, so whatever it points at must outlive the node
 * and must not be a stack frame that has returned.  The demo carries the same note over its
 * own readout. */
static char s_readout[CHANNEL_COUNT][96];

/**
 * @brief What each readout currently SAYS, so an unchanged one is not set again.
 *
 * @details THIS GUARD IS THE DIFFERENCE BETWEEN A STEADY PANEL AND A STROBING ONE, and it was
 *          found by the panel strobing.  `IrcitUiNodeSetText` calls `IrcitUiRequestLayout` --
 *          deliberately, because a text change can change a node's wrapped height -- and a
 *          layout request re-runs the pass and RE-REALIZES the subtree, tearing windows down
 *          and rebuilding them.  Setting a readout on every frame therefore rebuilt a 40x30
 *          panel about nineteen times a second, and the region visibly flashed.
 *
 *          The stream carries about nineteen frames a second across three channels; the
 *          readout TEXT changes about once a second, because it is a whole number of percent
 *          and a whole second of clock.  Comparing first turns ~95% of those layout passes
 *          into a `strcmp`. */
static char s_readout_shown[CHANNEL_COUNT][96];

/* ── The socket ─────────────────────────────────────────────────────────────── */

/* Forward, because the panel is raised before the control section is defined and the binding
   wants the handler at the moment the id can be resolved. */
static IrcitUiClickDisposition sOnControlClick(IrcitUiNode *node_ptr, void *user_ptr);
static int sOnKey(IrcitUiEventDispatcher *disp, IrcitUiEvent *ev, void *user);
static void sPanelFit(void);

typedef struct {
  int            fd;
  atomic_bool    stop;
  thrd_t         thread;
  int            started;
} Net;

static Net s_net = { .fd = -1 };

/* ── The value mapper ───────────────────────────────────────────────────────── */

/**
 * @brief A frame's `[0,1]` metric as a whole percentage, or a refusal.
 *
 * @details THE ORDER IS THE GUARD and it is the whole of it:
 *
 *            1. non-finite FIRST, before any arithmetic that could be undefined;
 *            2. clamp, so the operand of the cast is provably inside `[0,1]`;
 *            3. only then multiply and cast.
 *
 *          `IrcitUiBarSetValue` cannot defend this conversion -- it clamps an `int` it is
 *          handed, and the conversion happens here, one line earlier.  On a `nan` or an
 *          infinity that is undefined behaviour, and this build has UBSan on.
 *          `dev/probes/harness/p_cast.c` measures both halves: the unguarded conversion
 *          reports `float-is outside the range of representable values`, and this one
 *          reports nothing over a 1001-point sweep.
 *
 *          Round-half-up rather than truncation, so 0.999 renders as 100 rather than a
 *          finish that never quite arrives.
 *
 * @return 0 if @p v was rejected, 1 if @p out_ptr holds a percentage.
 */
static int
sPercent(float v, int *out_ptr)
{
  if (!isfinite(v)) return 0;
  if (v < 0.0f) v = 0.0f;
  if (v > 1.0f) v = 1.0f;
  *out_ptr = (int)(v * 100.0f + 0.5f);
  return 1;
}

/** @brief Counters, so a stream that is being rejected says so rather than looking idle. */
static unsigned long s_dropped_frames, s_dropped_nonfinite, s_rejected_channels;

/* ── Applying a frame ───────────────────────────────────────────────────────── */

static IrcitUiNode *s_panel;

/** @brief How many expiry ticks a label survives.  At the media cadence, about 3.5 seconds. */
#define LABEL_TICKS 14

static void sReadoutText(int idx, int with_label);

/**
 * @brief Build a channel's readout text, with or without the frame's label.
 *
 * @details One function for both, because §32's expiry needs the SAME text minus the label --
 *          and two builders that had to agree would be two places for the numbers to drift
 *          apart, visible only after a label expired.
 */
static void
sReadoutText(int idx, int with_label)
{
  const MspFrame *f = &s_last[idx];
  char           *buf = s_readout[idx];
  int             pct = 0, bpct = 0, have_buf = 0, mins, secs;

  (void)sPercent(f->primary, &pct);
  if (s_channels[idx].buffered_id) have_buf = sPercent(f->secondary, &bpct);

  if (f->channel == CH_MEDIA_YOUTUBE) {
    mins = (int)(f->time_current / 60);
    secs = (int)(f->time_current % 60);
    snprintf(buf, sizeof s_readout[0], "media %02d:%02d/%llds buf %d%%",
             mins, secs, f->time_total, have_buf ? bpct : 0);
  } else if (f->channel == CH_DIAL_CPU) {
    snprintf(buf, sizeof s_readout[0], "cpu load=%d%% throttle=%.2f", pct, (double)f->secondary);
  } else {
    snprintf(buf, sizeof s_readout[0], "download %d%% %.2f MB/s %lld/%lld",
             pct, (double)f->secondary, f->time_current, f->time_total);
  }

  if (with_label && f->label_len > 0) {
    strncat(buf, "  ", sizeof s_readout[0] - strlen(buf) - 1);
    strncat(buf, f->label, sizeof s_readout[0] - strlen(buf) - 1);
  }
}

/**
 * @brief The expiry's continuation.  Runs on Thread 0, inside the drain, when the promise is
 *        completed -- which dev/probes/harness/p_expiry.c measures rather than assumes.
 */
static void
sOnLabelExpired(UfsrvFutureResult *result, void *ctx)
{
  LabelExpiry *e = ctx;
  IrcitUiNode *readout;

  (void)result;

  e->armed   = 0;
  e->promise = NULL;   /* SetValue freed it; see p_expiry's `inline` case */

  if (!s_panel) return;

  readout = IrcitUiFind(s_panel, s_channels[e->channel].readout_id);
  if (!readout) return;

  /* THE NUMBERS STAY AND THE LABEL GOES.  A label expiring is a message going away, not the
   * widget losing its value -- §32's `log_overlay` is an overlay over a bar that keeps
   * drawing.  This line is the whole of what this sub-stage exists for. */
  sReadoutText(e->channel, 0);
  if (strcmp(s_readout[e->channel], s_readout_shown[e->channel]) != 0) {
    snprintf(s_readout_shown[e->channel], sizeof s_readout_shown[0], "%s", s_readout[e->channel]);
    IrcitUiNodeSetText(readout, s_readout_shown[e->channel]);
  }
}

/**
 * @brief Arm -- or re-arm -- a channel's label expiry.
 *
 * @details THE CANCEL IS THE INTERESTING HALF.  A promise already armed is destroyed before a
 *          new one is created, because an expiry that fired after a later label arrived would
 *          clear information that is still true.  p_expiry's `rearm` case is exactly this
 *          sequence, and asserts the cancelled arming stays cancelled.
 *
 *          UfsrvPromiseDestroy ABANDONS rather than completes, so the old continuation never
 *          runs -- which is why the context is a static in an array rather than allocated per
 *          arming: nothing has to be freed on the cancel path, and no path leaks.
 */
static void
sArmExpiry(int idx, int ticks)
{
  UfsrvFuture *future = NULL;

  if (s_expiry[idx].promise) {
    UfsrvPromiseDestroy(s_expiry[idx].promise);
    s_expiry[idx].promise = NULL;
  }

  s_expiry[idx].promise = UfsrvPromiseCreate(&future);
  if (!s_expiry[idx].promise) { s_expiry[idx].armed = 0; return; }

  UfsrvFutureThen(future, sOnLabelExpired, &s_expiry[idx]);
  UfsrvFutureRelease(future);

  s_expiry[idx].channel    = idx;
  s_expiry[idx].ticks_left = ticks;
  s_expiry[idx].armed      = 1;
}

/**
 * @brief One tick of the expiry clock.
 *
 * @details THE CLOCK IS THE STREAM.  The framework's loop exposes no application tick hook, so
 *          the honest clock this program has is a chunk arriving -- at the server's three
 *          cadences that is roughly thirteen a second, which is a clock.  §32 counts
 *          `log_expiration_ticks` and this counts the same way; what changed is that the
 *          countdown's death is a PROMISE BEING RESOLVED rather than a branch that clears a
 *          buffer, so the clear happens through the same continuation for every path.
 */
static void
sTickExpiry(void)
{
  int i;

  for (i = 0; i < CHANNEL_COUNT; i++) {
    UfsrvPromise *p;

    if (!s_expiry[i].armed) continue;
    if (--s_expiry[i].ticks_left > 0) continue;

    /* UN-ARMED BEFORE THE COMPLETION, not by the continuation.  SetValue runs the continuation
     * INLINE -- p_expiry measures that -- so the fields go first and the continuation then has
     * only the widget to touch.  Relying on the continuation would be a line shorter and would
     * also mean a continuation that returned early left the channel armed for ever. */
    p = s_expiry[i].promise;
    s_expiry[i].promise = NULL;
    s_expiry[i].armed   = 0;

    if (p) UfsrvPromiseSetValue(p, NULL, NULL);
  }
}

static void
sApplyFrame(const MspFrame *f)
{
  int i, pct, bpct = 0, have_buf = 0;
  int mins, secs;

  for (i = 0; i < CHANNEL_COUNT; i++) {
    IrcitUiNode *bar, *readout;

    if (s_channels[i].channel != f->channel) continue;

    bar = IrcitUiFind(s_panel, s_channels[i].bar_id);
    if (!bar) { s_rejected_channels++; return; }

    if (sPercent(f->primary, &pct)) {
      IrcitUiBarSetValue(bar, pct);
      IrcitUiInvalidate(bar);
    } else {
      /* Counted, not drawn: the bar keeps its last good value rather than being reset to
       * zero, which is the honest picture of a source that sent rubbish once. */
      s_dropped_nonfinite++;
    }

    if (s_channels[i].buffered_id) {
      IrcitUiNode *buf = IrcitUiFind(s_panel, s_channels[i].buffered_id);
      if (buf && sPercent(f->secondary, &bpct)) {
        IrcitUiBarSetBuffered(buf, bpct);
        IrcitUiInvalidate(buf);
        have_buf = 1;
      }
    }

    /* Remembered BEFORE the readout is built, because §32's expiry rebuilds the readout from
     * it later, with no frame in hand -- see sOnLabelExpired. */
    s_last[i] = *f;

    /* A label ARMS the expiry; its absence does not cancel one.  That asymmetry is the point:
     * a label is an event and the frames between events carry none, so cancelling on an empty
     * label would mean a message died the moment the next frame arrived -- about 250 ms. */
    if (f->label_len > 0) sArmExpiry(i, LABEL_TICKS);

    readout = IrcitUiFind(s_panel, s_channels[i].readout_id);
    if (readout) {
      /* ONLY WHEN IT CHANGED.  See s_readout_shown: the setter requests a layout pass, and a
       * layout pass at frame rate is a strobing panel.  Note also that no IrcitUiInvalidate
       * follows -- the setter's own IrcitUiRequestLayout already marks the node for repaint,
       * and a second call would only be a second entry for the same node. */
      sReadoutText(i, f->label_len > 0);
      if (strcmp(s_readout[i], s_readout_shown[i]) != 0) {
        snprintf(s_readout_shown[i], sizeof s_readout_shown[0], "%s", s_readout[i]);
        IrcitUiNodeSetTextLive(readout, s_readout_shown[i]);
      }
    }
    return;
  }

  /* A channel the protocol defines but this table does not map.  Counted rather than
   * ignored, because it means the two ends disagree about the vocabulary. */
  s_rejected_channels++;
}

/* ── The reassembler, on Thread 0 ───────────────────────────────────────────── */

static MspRx s_rx;
static int   s_panel_up;
static unsigned long s_frames_applied;

/**
 * @brief Lay the panel out to fit the TERMINAL, not to a constant.
 *
 * @details This was a hard-coded 40x30 at (1,1), and it is the reason the dashboard vanished on a
 *          normal terminal: the panel's content is about 26 rows, so on an 80x24 screen -- the
 *          default, and what most people actually run -- the bottom rows are laid out off-screen
 *          and never drawn. The symptom is a mostly blank panel with a few bars visible, which is
 *          exactly what it looked like.
 *
 *          IT ALSO HAS TO BE REDONE ON A RESIZE, and that is not an optimisation. The panel is a
 *          STACKED node, and a stacked node "owns its position rather than deriving it from a
 *          parent's box" -- the engine's own words at the inflate site -- so the framework's
 *          resize handling re-lays the ROOT and leaves this node at whatever rectangle it was
 *          given. Without an explicit re-fit a terminal that shrinks leaves the dashboard
 *          hanging off the edge for the rest of the session.
 */
static void
sPanelFit(void)
{
  int w, h;

  if (!s_panel) return;

  w = COLS  - 2;
  h = LINES - 2;
  if (w > PANEL_W) w = PANEL_W;
  if (h > PANEL_H) h = PANEL_H;
  if (w < 12) w = 12;
  if (h < 5)  h = 5;

  IrcitUiLayout(s_panel, 1, 1, w, h);
}

/** @brief Raise the dashboard.  Runs on Thread 0, inside the drain, with a live tree. */
static void
sRaisePanel(void)
{
  s_panel_up = 1;

  if (!IrcitUiScreen.root) return;

  /* INFLATE, THEN LAY IT OUT AGAIN, and the second call is not redundant.
   *
   * Every IrcitUiInflate* route funnels through sInflateAt, which caps the node at
   * PRIV_CONFIG_DEFAULT_POPUP_W/H (34x18) and lays it out at that size -- the anchored route
   * included, which is the opposite of what its name suggests.  The gallery is 40 wide with
   * about 26 rows of content, so the cap cuts BOTH axes and the tail of the panel -- the
   * last bars and the Close button -- is never drawn.  Measured in
   * dev/probes/harness/p_cap.c: the capped float leaves the tail row's ink unchanged, and
   * re-laying the same node at 40x30 puts it on the glass (3 -> 36 ink).
   *
   * IrcitUiLayout is public and a STACKED node "owns its position rather than deriving it
   * from a parent's box", so assigning the rectangle again is what makes the panel the size
   * this program wants.  It is a WORKAROUND for a pre-existing defect, not a feature. */
  s_panel = IrcitUiInflateFloat(IrcitUiScreen.root, "progress_panel");
  if (!s_panel) {
    fprintf(stderr, "[client] the gallery template could not be raised\n");
    return;
  }

  sPanelFit();
  IrcitUiRealize(s_panel);

  /* The one control binding the public API can make -- see sOnControlClick for why it is a
     click and not a key.  Bound here rather than at bootstrap for the reason the demo gives
     over its own seek bindings: the id is resolved once, against the live tree, and there is
     no live tree until this runs. */
  if (!IrcitUiBindClick("prog_close", sOnControlClick, NULL)) {
    fprintf(stderr, "[client] the control binding did not resolve\n");
  }

  /* The keys, on the PANEL rather than on a leaf: a key listener is consulted for the focused
     node and its ancestors, so binding the panel's root is how the panel takes the keys
     inside it.  Bound here for the reason the click above is -- the id is resolved once,
     against the live tree, and there is no live tree until this runs. */
  if (!IrcitUiBindKey("main", sOnKey, NULL) && !IrcitUiBindKey("progress_panel", sOnKey, NULL)) {
    fprintf(stderr, "[client] the key binding did not resolve\n");
  }

  IrcitUiInvalidate(s_panel);
}

/**
 * @brief The application's network listener.
 *
 * @details Runs on Thread 0, synchronously, with the watchdog deliberately unarmed -- the
 *          contract the framework's own header states, and the reason the reassembler's
 *          durable state is safe to keep here rather than in an allocation.
 *
 *          THE CHUNK IS NUL-FREE BY CONSTRUCTION AND THAT IS LOAD-BEARING.  The listener
 *          receives a `const char *` and NO LENGTH, so `strlen` is the only route to how
 *          many bytes arrived -- which is why the wire is printable text and not §28's
 *          packed binary.  `dev/probes/harness/p_nul.c` measures it: the section's own
 *          struct posted at 56 bytes arrives as 5, while a frame of this shape posted at 64
 *          arrives as 64.
 */
static int
sOnChunk(IrcitUiEventDispatcher *disp, void *token, const char *bytes, void *user)
{
  char frame[MSP_FRAME_LEN];
  size_t n;

  (void)disp; (void)token; (void)user;

  if (!s_panel_up) sRaisePanel();
  if (!s_panel) return 1;

  /* A STACKED NODE IS NOT RE-LAID BY THE FRAMEWORK, so a resize has to be noticed here.  The
   * listener runs on every chunk, which is often enough to be imperceptible and cheap: two
   * comparisons. */
  {
    static int last_rows, last_cols;

    if (LINES != last_rows || COLS != last_cols) {
      last_rows = LINES;
      last_cols = COLS;
      sPanelFit();
      IrcitUiRealize(s_panel);
      IrcitUiInvalidate(s_panel);
    }
  }

  /* THE FIRST CALL IS THIS PROGRAM'S STARTUP HOOK, and it needs one: IrcitUiMainLooper calls
   * IrcitUiScreenReset before its first frame, so anything raised during bootstrap is freed
   * before it is ever drawn, and the framework posts no root lifecycle event to hang a hook
   * on.  The listener is the application's one guaranteed Thread-0 callback, so the client
   * posts itself a chunk before entering the loop to guarantee this runs on pass one --
   * deterministic, and independent of whether a server is reachable. */

  n = strlen(bytes);
  if (n == 0) return 1;

  if (!MspRxPush(&s_rx, bytes, n)) {
    fprintf(stderr, "[client] a chunk did not fit the reassembler; stream reset\n");
    return 1;
  }

  /* ONCE PER CHUNK, not per frame: the countdown approximates wall-clock time, and a burst of
   * seven frames in one chunk is one moment, not seven.  Ticking per frame would make a
   * label's life depend on how the socket happened to be chopped -- exactly the coupling the
   * text framing exists to avoid. */
  sTickExpiry();

  while (MspRxNext(&s_rx, MSP_FRAME_LEN, frame, MspRxValidFrame, NULL)) {
    MspFrame f;

    if (!MspFrameParse(frame, &f)) { s_dropped_frames++; continue; }
    sApplyFrame(&f);
    s_frames_applied++;
  }

  return 1;
}

/* ── The reader thread ──────────────────────────────────────────────────────── */

/**
 * @brief The socket's read side.  Reads, hands the bytes over, and touches nothing else.
 *
 * @details THE POST WAITS RATHER THAN DROPS.  A lost chunk desynchronises the reassembler,
 *          and there is no recovery from that short of reconnecting -- so a full pipeline
 *          slows this thread down instead of discarding, which is also what pushes back on
 *          the sender: the kernel's receive buffer fills and the server's window closes.
 *          A zero from the post therefore ends the stream rather than continuing it.
 */
static int
sReaderThread(void *arg)
{
  Net *net = arg;
  char buf[CLIENT_READ_MAX];

  while (!atomic_load(&net->stop)) {
    ssize_t n = recv(net->fd, buf, sizeof buf, 0);

    if (n > 0) {
      if (!IrcitUiDispatcherPostNetworkChunk(IrcitUiDispatcherInstance(), net, buf, (size_t)n, 200)) {
        fprintf(stderr, "[client] the pipeline was full for the whole deadline; ending the stream "
                        "rather than desynchronising it\n");
        break;
      }
      continue;
    }
    if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
    break;   /* EOF, or a real error: either way the stream is over */
  }

  return 0;
}

/** @brief Connect and start the reader.  A client with no server still runs. */
static int
sNetStart(void)
{
  struct sockaddr_un addr;
  int                fd;

  fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) return 0;

  memset(&addr, 0, sizeof addr);
  addr.sun_family = AF_UNIX;
  if (strlen(s_sock_path) >= sizeof addr.sun_path) { close(fd); return 0; }
  strncpy(addr.sun_path, s_sock_path, sizeof addr.sun_path - 1);

  if (connect(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
    /* NOT an error worth stopping for.  The server is a separate program that may be started
     * after this one, and a TUI that refuses to open because a simulation is not running
     * would be a worse instrument than one that says so on its own status line. */
    fprintf(stderr, "[client] no server at %s (%s); the dashboard will stay at rest\n",
            s_sock_path, strerror(errno));
    close(fd);
    return 0;
  }

  /* The fd stays BLOCKING.  O_NONBLOCK is per-descriptor and not per-direction, so setting
   * it to make reads non-blocking would also make writes non-blocking -- which is what the
   * control path below must not have, because its whole discipline is a zero-timeout poll
   * followed by a send that cannot block Thread 0. */
  s_net.fd = fd;
  atomic_store(&s_net.stop, false);
  if (thrd_create(&s_net.thread, sReaderThread, &s_net) != thrd_success) {
    close(fd);
    s_net.fd = -1;
    return 0;
  }
  s_net.started = 1;
  fprintf(stderr, "[client] attached to %s\n", s_sock_path);
  return 1;
}

/**
 * @brief Stop the reader and close the socket, in the order the demo's own teardown states.
 *
 * @details THE ORDER IS THE CONTRACT, not tidiness: a descriptor closed under a thread
 *          blocked in `recv` is undefined, and shutting it down first is what reliably wakes
 *          that thread.  The listener is cleared LAST, because a callback reached with a
 *          token whose storage is released is a use-after-free the framework cannot detect.
 */
static void
sNetStop(void)
{
  if (s_net.fd >= 0) shutdown(s_net.fd, SHUT_RDWR);
  atomic_store(&s_net.stop, true);
  if (s_net.started) thrd_join(s_net.thread, NULL);
  if (s_net.fd >= 0) { close(s_net.fd); s_net.fd = -1; }
  s_net.started = 0;
  IrcitUiDispatcherRegisterNetworkListener(IrcitUiDispatcherInstance(), NULL, NULL);
}

/* ── Control frames, Thread 0 ───────────────────────────────────────────────── */

/**
 * @brief Send one control frame, or give up rather than block.
 *
 * @details THREAD 0 MUST NOT BLOCK IN send().  The framework's listener contract says so in
 *          as many words -- a callback that blocks stops the drain, which stops the
 *          producer's backpressure from clearing, and the whole application wedges -- and a
 *          send on a full buffer with a slow reader is exactly that.  So writability is
 *          asked for first with a zero timeout, and a frame that cannot go is DROPPED and
 *          counted.  The commands are advisory (pause, speed, inject), so a dropped one is
 *          survivable where a stalled UI is not.
 *
 *          SIGPIPE is ignored at bootstrap and MSG_NOSIGNAL is passed here: without both, a
 *          server that has gone away kills the client outright, which looks like a crash
 *          rather than a lost peer.
 */
static void
sSendCommand(int channel, int command, float value)
{
  char        frame[MSP_CMD_LEN];
  struct pollfd pfd;
  ssize_t     sent;

  if (s_net.fd < 0) return;

  pfd.fd      = s_net.fd;
  pfd.events  = POLLOUT;
  pfd.revents = 0;
  if (poll(&pfd, 1, 0) <= 0) {
    fprintf(stderr, "[client] control frame dropped: the socket is not writable\n");
    return;
  }

  MspCmdBuild(frame, channel, command, value);
  sent = send(s_net.fd, frame, MSP_CMD_LEN, MSG_NOSIGNAL);
  /* A SHORT COUNT IS NOT A BOOLEAN.  The frame is fixed-width and the far side reassembles by
   * the terminator, so half a frame is discarded there rather than misread -- which is the
   * property the text framing buys and the reason a binary parser could not do this. */
  if (sent != MSP_CMD_LEN) {
    fprintf(stderr, "[client] a control frame went out short (%zd of %d); the server discards "
                    "the tail\n", sent, MSP_CMD_LEN);
  }
}

/**
 * @brief The client's one control binding, and a placeholder for a gap.
 *
 * @details §30 wants the keyboard to drive the server -- space to pause, a key to inject an
 *          error.  **The public API has no way to bind a key.**  It has
 *          `IrcitUiBindClick` and `IrcitUiBindSeek` and nothing else; a key handler is
 *          reachable only through `IrcitUiCommonState::interactions_user
 *          .lifecycle_listeners.on_key_down`, which lives in `src/include/ui_priv.h` -- the
 *          private header this target is built to prove it does not need.  The callback
 *          TYPE is public (`ircit_ui_key_event_callback_type.h`); the binder is not.
 *
 *          Rather than reach past the boundary for it, this program binds the one public
 *          gesture it has to the one command that makes the effect visible, and the gap is
 *          recorded instead of hidden: `IrcitUiBindKey(const char *id, IrcitUiKeyEventCallback,
 *          void *)`, declared beside `IrcitUiBindClick`, is the whole of the fix.
 *
 *          What this DOES prove is the transmit path: a control frame built by the shared
 *          codec, gated on a zero-timeout writability poll, sent with MSG_NOSIGNAL, and
 *          accepted by the server -- which `dev/probes/harness/server_cases.py` measures from
 *          the other end.
 */
/**
 * @brief The application's key policy.  Runs on Thread 0.
 *
 * @details This is §30's keyboard half, and it is a public-API consumer throughout:
 *          @ref IrcitUiBindKey routes the key here and @ref IrcitUiEventKeyCode reads it,
 *          because the event itself is opaque.  Both were absent until this gate; a client
 *          could connect and render, and could not be told to do anything.
 */
static int
sOnKey(IrcitUiEventDispatcher *disp, IrcitUiEvent *ev, void *user)
{
  (void)user;

  switch (IrcitUiEventKeyCode(ev)) {
    case ' ': sSendCommand(CH_MEDIA_YOUTUBE, CMD_TOGGLE_PAUSE, 0.0f); return 1;
    case 'x': sSendCommand(CH_MEDIA_YOUTUBE, CMD_INJECT_ERROR, 0.0f); return 1;
    case 'd': sSendCommand(CH_DL_LARGE,      CMD_INJECT_ERROR, 0.0f); return 1;
    case '1': sSendCommand(CH_MEDIA_YOUTUBE, CMD_ADJUST_SPEED, 1.0f); return 1;
    case '4': sSendCommand(CH_MEDIA_YOUTUBE, CMD_ADJUST_SPEED, 4.0f); return 1;
    default: break;
  }
  return 0;
}

static IrcitUiClickDisposition
sOnControlClick(IrcitUiNode *node_ptr, void *user_ptr)
{
  (void)node_ptr; (void)user_ptr;
  sSendCommand(CH_MEDIA_YOUTUBE, CMD_TOGGLE_PAUSE, 0.0f);
  /* CARRY, not SINK: the engine must still do its own dismissal, or Close stops closing. */
  return IRCITUI_CLICK_CARRY;
}

/* ── main ───────────────────────────────────────────────────────────────────── */

int
main(int argc, char **argv)
{
  IrcitUiContext *ctx;
  char            hello[8] = "hello\r\n";

  if (argc > 1) s_sock_path = argv[1];

  /* Both belts for the same failure: a peer that has gone away must not kill the process. */
  signal(SIGPIPE, SIG_IGN);

  /* THE BACKEND FIRST, before anything that draws.  A zeroed callbacks struct means "no
     application hooks", which the registry's own header states. */
  s_registry = IrcitUiWindowRegistryCreate((IrcitUiDriverRegistryUserCallbacks){
    .on_driver_initialised = sOnDriverInitialised,
  });
  if (!s_registry || !IrcitUiWindowRegistryRequestDriver(s_registry, "ncurses")) {
    fprintf(stderr, "[client] no window backend could be locked\n");
    return EXIT_FAILURE;
  }

  ctx = IrcitUiInitialiseFrameworkContext(IrcitUiDataBind, s_registry);
  if (!ctx) {
    fprintf(stderr, "[client] the framework context could not be initialised\n");
    IrcitUiWindowRegistryDestroy(s_registry);
    return EXIT_FAILURE;
  }

  MspRxInit(&s_rx);

  /* THE LISTENER FIRST, THE PRODUCER SECOND, BOTH BEFORE THE LOOP -- the order the demo's own
   * bootstrap states and the reason it states it: the listener runs on Thread 0 from inside
   * the drain, so installing one while the loop is running would be a race rather than a
   * bootstrap. */
  IrcitUiDispatcherRegisterNetworkListener(IrcitUiDispatcherInstance(), sOnChunk, NULL);

  /* And the startup chunk, posted before the reader exists so that nothing can precede it.
   * This is what raises the dashboard on pass one -- see sOnChunk. */
  (void)IrcitUiDispatcherPostNetworkChunk(IrcitUiDispatcherInstance(), NULL, hello, 7, 200);

  (void)sNetStart();

  IrcitUiMainLooper(ctx);

  /* Producers first, then the pipeline, then the tree, then the backend -- the demo's own
     teardown order, and the reason it is that order: the framework's worker pool is stopped
     and joined inside the context destroy, so nothing is still posting into a pipeline that
     has been released. */
  sNetStop();
  IrcitUiDestroyFrameworkContext(ctx);
  IrcitUiDeflate(IrcitUiScreen.root);
  IrcitUiWindowRegistryDestroy(s_registry);

  fprintf(stderr, "[client] %lu frames applied, %lu dropped by the parser, %lu with a "
                  "non-finite metric, %lu for an unmapped channel; %lu resyncs\n",
          s_frames_applied, s_dropped_frames, s_dropped_nonfinite, s_rejected_channels,
          s_rx.resyncs);
  return EXIT_SUCCESS;
}
