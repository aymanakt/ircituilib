/** @file ui_main.c @brief Demo application policy. F9 toggle Tools permission,
    F7 tools, F8 file, F6 fill, F5 window, F2 async fetch that fails (raises the
    watchdog alert), F3 async fetch that succeeds (writes the result), F10 quit.
    Ctrl-N raises a notification, Ctrl-T cycles the colour palette, Ctrl-O
    deletes the cursor row and offers an undo.
    The loop is the framework's -- see IrcitUiMainLooper. */
#include "ui_priv.h"
#include "ui_log.h"
/* The alert window. The demo never raises it -- F2 starts an async operation
   and Thread 0 raises the alert when that operation's failure comes back
   through the pipeline. Only the framework's own consumer calls into this. */
#include "ui_watchdog_alert.h"
/* The recycler's per-node state, for the selection count below. This file already
   reaches past the public surface for the alert and for the loop, so one more is
   the same kind of dependency rather than a new one. */
#include "ircitui_recycler_type_priv.h"
/* The bar's per-node state, for the gallery's readout -- the sixth reach past the public
   surface in this file, and for the same kind of reason as the five above it: the readout
   exists to print the public getter and the private field side by side, so that the split
   between the API surface and the framework internals is visible on the glass rather than
   asserted in a comment. Nothing in this file MOVES a bar through it. */
#include "ircitui_bar_state_type_priv.h"
/* The predictive field's readout. The seventh private include in this file and the same
   reason as the six above it, stated at sPredictReport: the panel prints what the PUBLIC API
   answers next to what the framework's own state holds, and only a private header can reach
   the second. The dictionary itself is NOT reached this way -- it is attached, read back and
   reported through ui.h alone, which is the point of the seam. */
#include "ircitui_text_edit_state_type_priv.h"
/* F2 and F3 drive the async path: a coroutine on the worker pool, a future
   carrying its outcome back, and the bridge that posts it to Thread 0. */
#include "ui_async_bridge.h"
#include <ufsrvrxlib/ufsrv_coroutine/ufsrv_coroutine.h>
#include "ircituilib/config_defs.h"
#include "ircituilib/ircitui_context.h"
/* The framework loop. This application no longer runs one of its own. */
#include "ircituilib/main_looper.h"
/* The registry, named here as well as through ircitui_context.h: this file
   calls IrcitUiWindowRegistryCreate/RequestDriver/Destroy directly, and the
   coding standard is that a unit includes what it names. */
#include "ircituilib/window_driver/ircitui_window_registry.h"
#include <ufsrvrxlib/ufsrv_scheduler/ufsrv_scheduler.h>
#include <uflib/buffer_descriptor/buffer_descriptor.h>
#include <getopt.h>
#include <locale.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
/* The demo's own half of the network pipeline: a socket, a worker thread and a
   name lookup. Nothing under src/ opens a socket; the framework moves bytes it is
   handed and never reads a descriptor itself. */
#include <pthread.h>
#include <errno.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
/* Still needed, and that is the honest state of the migration: the calls below
   go through the translation table, but the KEY_* values they are compared
   against are still ncurses'. Defining a backend-neutral key space is a
   separate piece of work; until it lands the loop reads ncurses keycodes from
   whichever backend it is driving. */
#include <ncurses.h>
/* Ctrl-D downloads a real image over the network. libcurl is linked into the
   demo only; nothing under src/ fetches anything. */
#include <curl/curl.h>

/* Long-option codes for getopt_long(). Values below 256 are reserved for short
   options, and this demo has none, so the codes start clear of that range. */
enum
{
  IRCITUI_OPT_LOG_LEVEL = 256,
  IRCITUI_OPT_LOG_FILE,
  IRCITUI_OPT_LOG_RETENTION,
  IRCITUI_OPT_WINDOW_DRIVER,
  IRCITUI_OPT_VERIFY_LAYOUT,
  IRCITUI_OPT_COLS,
  IRCITUI_OPT_ROWS,
  IRCITUI_OPT_FETCH_URL,
  IRCITUI_OPT_CONNECT,
  IRCITUI_OPT_HELP
};

static const struct option sLongOpts[] = {
  {"log-level", required_argument, NULL, IRCITUI_OPT_LOG_LEVEL},
  {"log-file", required_argument, NULL, IRCITUI_OPT_LOG_FILE},
  {"log-retention", required_argument, NULL, IRCITUI_OPT_LOG_RETENTION},
  {"window-driver", required_argument, NULL, IRCITUI_OPT_WINDOW_DRIVER},
  {"verify-layout", no_argument, NULL, IRCITUI_OPT_VERIFY_LAYOUT}, {"cols", required_argument, NULL, IRCITUI_OPT_COLS},
  {"rows", required_argument, NULL, IRCITUI_OPT_ROWS},
  {"fetch-url", required_argument, NULL, IRCITUI_OPT_FETCH_URL},
  {"connect", required_argument, NULL, IRCITUI_OPT_CONNECT},
  {"help", no_argument, NULL, IRCITUI_OPT_HELP}, {NULL, 0, NULL, 0}
};

/* The retention names this command line accepts, and the mode each selects.
 *
 * The table lives here rather than in the library because it is the command
 * line's vocabulary and not the API's: IrcitUiLogInit takes the enum. Keeping
 * it at the edge is also what lets the library add a mode without this file
 * having to learn a name for it, or refuse one without this file changing. */
static const struct
{
  const char *        name;
  IrcitUiLogRetention mode;
} sRetentions[] = {
  {"append", IRCITUI_LOG_APPEND}, {"overwrite", IRCITUI_LOG_OVERWRITE}, {"archive", IRCITUI_LOG_ARCHIVE}
};

/* Returns 0 and writes @p mode_ptr, or non-zero if @p name_ptr names no mode. */
static int
sParseRetention(const char *name_ptr, IrcitUiLogRetention *mode_ptr)
{
  size_t i;

  for (i = 0; i < sizeof sRetentions / sizeof sRetentions[0]; i++)
    if (strcmp(name_ptr, sRetentions[i].name) == 0) {
      *mode_ptr = sRetentions[i].mode;
      return 0;
    }
  return -1;
}

static void
sUsage(FILE *out_ptr, const char *prog_ptr)
{
  fprintf(out_ptr,
          "usage: %s [options]\n" "\n" "  --log-level=<level>   debug, info, notice, warn, error, fatal, or\n"
          "                        nolog for no log at all. A level records itself\n"
          "                        and everything more severe. Default: info.\n"
          "  --log-file=<path>     Log sink; created if absent. Default:\n"
          "                        ircituilib.log in the working directory.\n"
          "  --log-retention=<mode>  append, overwrite or archive. What becomes of\n"
          "                        an existing --log-file: appended to, emptied,\n"
          "                        or renamed to <file>.<YYYYmmdd-HHMMSS> so a new\n"
          "                        one starts in its place. Default: append.\n"
          "  --window-driver=<name>  Backend to ask the window registry for:\n"
          "                        ncurses or ansi. Default: ncurses. A backend\n"
          "                        that cannot start falls back, and which one was\n"
          "                        locked is reported at INFO in the log.\n"
          "  --verify-layout       Check the reference model against the layout\n"
          "                        rules and exit. Does not start ncurses.\n"
          "  --cols=<n>            Width for --verify-layout. Default: 80.\n"
          "  --rows=<n>            Height for --verify-layout. Default: 24.\n"
          "  --connect=<host:port> Stream CRLF-terminated lines from a TCP endpoint\n"
          "                        and append each one to the log list as it arrives.\n"
          "                        This is the network pipeline end to end: a worker\n"
          "                        reads the socket, the framework carries each chunk\n"
          "                        to Thread 0, and the demo reassembles and routes it.\n"
          "  --fetch-url=<url>     What Ctrl-D fetches. Any URL libcurl understands,\n"
          "                        including file:// , which is how the async image\n"
          "                        path is exercised without a network. Default: a\n"
          "                        a PNG from kernel.org. The image loader is\n"
          "                        libpng, so the payload must be a PNG.\n" "  --help                This text.\n"
          "\n" "  F9 toggles the Tools permission, F7 tools, F8 file, F6 fill,\n"
          "  F5 window, F2 async fetch that fails, F3 async fetch that succeeds,\n" "  F10 quits.\n"
          "  Double-click a log row, or Ctrl+Enter on it, to edit it in place.\n"
          "  Ctrl+Enter needs a terminal configured to send a sequence for it.\n"
          "  Ctrl-N raises a notification. Ctrl-T cycles the colour palette: two\n"
          "  profiles from the library and two of this application's own.\n"
          "  Ctrl-O removes the log list's cursor row and offers it back; pressing\n"
          "  it again while the notification is up undoes the removal.\n",
          prog_ptr);
}

/* Everything the command line can set.
 *
 * The two strings default to NULL rather than to a value: NULL is what
 * IrcitUiLogInit reads as "use your own default", so the demo does not carry a
 * second copy of those defaults that could fall out of step with the library's.
 * The geometry, by contrast, is the demo's own -- the library has no opinion on
 * what size to verify at. */
typedef struct
{
  const char *        log_level;     /**< NULL lets IrcitUiLogInit choose. */
  const char *        log_file;      /**< NULL lets IrcitUiLogInit choose. */
  IrcitUiLogRetention retention;     /**< What becomes of an existing log file. */
  const char *        window_driver; /**< Backend to ask the registry for. Never NULL;
                                          "ncurses" unless --window-driver said otherwise. */
  int                 verify_layout; /**< Run the headless check instead of the TUI. */
  int                 cols;          /**< Geometry for --verify-layout. */
  int                 rows;
  /** What Ctrl-D fetches. Any URL libcurl understands, including file:// - which
      is how the async image path is demonstrated on a machine with no network. */
  const char *        fetch_url;
  /** `host:port` of a TCP endpoint to stream lines from, or NULL to run with no
      connection. This is the demo's half of the network pipeline: a worker reads
      the socket, the framework carries each chunk to Thread 0, and the demo
      reassembles and routes it. */
  const char *        connect_to;
} DemoOptions;

/* Outcome of sParseArgs.
 *
 * Neither failure needs anything from the caller: getopt_long's own diagnostic
 * and the usage text have already been written to stderr by the time either is
 * returned. They are distinguished only so that --help exits successfully and a
 * usage error does not. */
enum
{
  DEMO_ARGS_OK = 0,
  DEMO_ARGS_HELP,
  DEMO_ARGS_BAD
};

static int
sParseArgs(int argc, char **argv_ptr, DemoOptions *opts_ptr)
{
  int opt;

  opts_ptr->log_level     = NULL;
  opts_ptr->log_file      = NULL;
  opts_ptr->retention     = IRCITUI_LOG_APPEND;
  /* ncurses is the default because it is the backend this demo is built to
     exercise: the engine draws through ncurses' own WINDOW handles, so the
     other backends are reachable and truthful but not yet what paints here. */
  opts_ptr->window_driver = "ncurses";
  opts_ptr->verify_layout = 0;
  opts_ptr->cols          = 80;
  opts_ptr->rows          = 24;
  opts_ptr->fetch_url     = "https://www.kernel.org/theme/images/logos/tux.png";
  opts_ptr->connect_to    = NULL;

  /* GNU getopt permutes, so the options may appear in any order and
     --verify-layout need not come first. Neither --cols nor --rows is
     meaningful without it, which is why they are accepted and ignored rather
     than refused.

     The optstring is empty: this demo offers no short options, and every long
     option carries a code above 255, so the two ranges cannot collide. */
  while ((opt = getopt_long(argc, argv_ptr, "", sLongOpts, NULL)) != -1) {
    switch (opt) {
    case IRCITUI_OPT_LOG_LEVEL:
      opts_ptr->log_level = optarg;
      break;
    case IRCITUI_OPT_LOG_FILE:
      opts_ptr->log_file = optarg;
      break;
    case IRCITUI_OPT_LOG_RETENTION:
      /* Refused here rather than passed on, because the library takes the
         enum and has no name for these. An unknown mode is a usage error
         in the same way an unknown option is. */
      if (sParseRetention(optarg, &opts_ptr->retention) != 0) {
        fprintf(stderr, "%s: unrecognised --log-retention '%s'\n", argv_ptr[0], optarg);
        sUsage(stderr, argv_ptr[0]);
        return DEMO_ARGS_BAD;
      }
      break;
    case IRCITUI_OPT_WINDOW_DRIVER:
      opts_ptr->window_driver = optarg;
      break;
    case IRCITUI_OPT_VERIFY_LAYOUT:
      opts_ptr->verify_layout = 1;
      break;
    case IRCITUI_OPT_COLS:
      opts_ptr->cols = atoi(optarg);
      break;
    case IRCITUI_OPT_ROWS:
      opts_ptr->rows = atoi(optarg);
      break;
    case IRCITUI_OPT_FETCH_URL:
      opts_ptr->fetch_url = optarg;
      break;
    case IRCITUI_OPT_CONNECT:
      opts_ptr->connect_to = optarg;
      break;

    case IRCITUI_OPT_HELP:
      sUsage(stdout, argv_ptr[0]);
      return DEMO_ARGS_HELP;
    default:
      /* getopt_long has already named the offending option on stderr. */
      sUsage(stderr, argv_ptr[0]);
      return DEMO_ARGS_BAD;
    }
  }

  /* Anything left after the options is an argument this demo does not take,
     which usually means an option was misspelt into something that does not
     begin with a dash. Refusing is better than ignoring it silently. */
  if (optind < argc) {
    fprintf(stderr, "%s: unexpected argument '%s'\n", argv_ptr[0], argv_ptr[optind]);
    sUsage(stderr, argv_ptr[0]);
    return DEMO_ARGS_BAD;
  }
  return DEMO_ARGS_OK;
}

static void
sLogSchedulerStartup(void)
{
  BufferDescriptor bd;

  BufferDescriptorInit(&bd, 512);
  if (DescribeScheduler(IrcitUiScheduler(), NULL, &bd) != NULL)
    IRCITUI_LOG_INFO("scheduler at startup: %s", bd.data);
  BufferDescriptorRelease(&bd);
}

/* The application's verdict on the Tools button, and the two fates it can hand
   back to the engine. Both are registered against the same node id; only one is
   bound at a time, and F9 swaps which. */

static IrcitUiClickDisposition
sOnToolsAllowed(IrcitUiNode *node_ptr, void *user_ptr)
{
  (void)node_ptr;
  (void)user_ptr;
  /* Carry: the engine goes on and does what it would have done anyway, which
     for this button is to inflate its layout_template, tools_panel. */
  return IRCITUI_CLICK_CARRY;
}

static IrcitUiClickDisposition
sOnToolsDenied(IrcitUiNode *node_ptr, void *user_ptr)
{
  IrcitUiWidgetDef win = {
    .id = "denied_win", .kind = IRCITUI_LINEAR, .text = "", .width_mode = IRCITUI_SIZE_EXACT, .width = 24,
    .height_mode = IRCITUI_SIZE_EXACT, .height = 6,
    .flags = IRCITUI_F_VISIBLE | IRCITUI_F_ENABLED | IRCITUI_F_BORDER | IRCITUI_F_DRAGGABLE, .fg = 7, .bg = 0, .bdr = 2
  };
  IrcitUiWidgetDef lab = {
    .id          = "denied_lab", .kind = IRCITUI_TEXT_VIEW, .text = "action denied", .width_mode = IRCITUI_SIZE_MATCH,
    .height_mode = IRCITUI_SIZE_WRAP, .flags = IRCITUI_F_VISIBLE | IRCITUI_F_ENABLED, .fg = 3, .bg = 0
  };
  IrcitUiWidgetDef btn = {
    .id = "denied_close", .text = "Close", .width_mode = IRCITUI_SIZE_MATCH, .height_mode = IRCITUI_SIZE_WRAP
  };
  IrcitUiNode *w;

  (void)node_ptr;
  (void)user_ptr;

  /* The node is live and already on screen, and this runs inside dispatch, so
     the window is created against IrcitUiScreen's root rather than against
     the container being walked - which is what makes this safe to do from
     inside an activation. */
  w = IrcitUiCreateFloatingWindow(&win);
  if (w) {
    IrcitUiAddChild(w, &lab);
    IrcitUiAddButton(w, &btn);
    IrcitUiLayout(w, w->common.x, w->common.y, w->common.w, w->common.h);
    IrcitUiRealize(w);
  }
  /* Sink: the engine does nothing further, so tools_panel never opens. */
  return IRCITUI_CLICK_SINK;
}

/* --- F2 and F3: two asynchronous operations, two outcomes -------------------
 *
 * The shape is the document's integration blueprint, and both keys run it. A
 * coroutine runs on the framework's worker pool and yields while it "waits"; a
 * future carries its outcome back; the continuation that receives that outcome
 * runs on a worker and does the one thing a worker is allowed to do, which is
 * post to the ring. Thread 0 consumes it and acts.
 *
 * The two differ only in how they end, which is the point of having both:
 *
 *   F2  a fetch that FAILS  -> an error result -> Thread 0 raises the alert
 *   F3  a fetch that SUCCEEDS after several stages -> a value result -> Thread 0
 *       writes it into the focused field
 *
 * Neither touches a node, and neither raises anything. The demo contains no
 * call to IrcitUiWatchdogAlertShow at all.
 */
typedef struct
{
  UfsrvCoroutineEntry entry;   /**< Which body this task runs. */
  UfsrvPromise *      promise; /**< Completed by the coroutine, on a worker. */
  /**
   * @brief Set by the body as it exits, polled by the job to know when to stop.
   *
   * Deliberately NOT the future. UfsrvFutureIsReady would be the obvious thing
   * to poll and it is wrong: completing a promise releases the future, so by
   * the time the job looks, the object it is looking at may already be gone -
   * which is a use-after-free the sanitizer caught on the worker thread. This
   * flag is allocated by the starter, owned by the job, and outlives both the
   * body and the future.
   */
  int *               done_ptr;
  char                url[256];/**< The endpoint the "fetch" was for. */
  int                 stages;  /**< How many times the body yields, if it is a staged one. */
  int                 kind;    /**< IRCITUI_BRIDGE_TEXT or _IMAGE. */
} DemoFetchTask;

/* Where the last download landed.
 *
 * File-scope, and that is not laziness: the image window borrows its path (see
 * common.text - nodes never own their text), so the string has to outlive the
 * node. This buffer lives as long as the process does. */
static char sDemoImagePath[PRIV_CONFIG_DEFAULT_PATH_BUF_SZ];

/* What Ctrl-D fetches, copied out of main's option struct for the worker side -
   a worker cannot see a local of main. */
static const char *sFetchUrl = "https://www.kernel.org/theme/images/logos/tux.png";

/* libcurl's write callback: straight to the file the worker opened. */
static size_t
sDemoDownloadWrite(char *ptr, size_t size, size_t nmemb, void *userdata)
{
  return fwrite(ptr, size, nmemb, (FILE *)userdata);
}

/* F2's body: one wait, then failure. */
static void
sDemoFetchFailEntry(void)
{
  DemoFetchTask *task_ptr = UfsrvCoroutineGetArg();

  /* Yield: the point of a coroutine is that the wait does not hold a thread.
     The worker goes back to the pool here and picks up other work. */
  UfsrvCoroutineYield();

  /* The operation failed. The promise carries that out. */
  UfsrvPromiseSetError(task_ptr->promise, -1);

  *task_ptr->done_ptr = 1;
  free(task_ptr);
  UfsrvCoroutineExit();
}

/* F3's body: several waits, then a value. This is what a coroutine buys that a
   job callback does not - the work is written as one straight-line function
   even though it is suspended and resumed three times in between, with the
   worker free to do something else on every suspension. */
static void
sDemoStagedFetchEntry(void)
{
  static char    payload[] = "staged_fetch_ok.png  (24 KB)";
  DemoFetchTask *task_ptr  = UfsrvCoroutineGetArg();
  int            i;

  for (i = 0; i < task_ptr->stages; i++) {
    /* Each yield is a suspension point: the state of this function survives
       across it, which is the whole difference from a plain callback. */
    UfsrvCoroutineYield();
  }

  /* The value is borrowed from this file and lives as long as the process, so
     there is nothing to free and NULL is the honest release function. */
  UfsrvPromiseSetValue(task_ptr->promise, payload, NULL);

  *task_ptr->done_ptr = 1;
  free(task_ptr);
  UfsrvCoroutineExit();
}

/* Ctrl-D's body: a real download over the network.
 *
 * This is the case the coroutine model is actually for. curl_multi drives the
 * transfer; the body yields every time there is nothing to read yet, so the
 * pool worker goes back and serves other jobs while the bytes are in flight.
 * The transfer is written as one straight-line function even though it is
 * suspended and resumed an arbitrary number of times - which is the difference
 * between this and a blocking fetch on a worker thread.
 *
 * The body writes a FILE and reports its path. It never allocates the image
 * into the UI and never touches a node: the only thing a worker is allowed to
 * do here is post. */
static void
sDemoDownloadEntry(void)
{
  DemoFetchTask *task_ptr = UfsrvCoroutineGetArg();
  CURLM *        multi_ptr;
  CURL *         easy_ptr;
  FILE *         fp;
  long           http_code   = 0;
  CURLcode       curl_result = CURLE_OK;
  int            running     = 0;
  int            guard     = 0;
  int            ok        = 0;

  /* Named before the transfer: the path is what the result carries back. */
  snprintf(sDemoImagePath, sizeof sDemoImagePath, "/tmp/ircituilib_download.png");

  multi_ptr = curl_multi_init();
  easy_ptr  = curl_easy_init();
  fp        = fopen(sDemoImagePath, "wb");

  if (multi_ptr && easy_ptr && fp) {
    curl_easy_setopt(easy_ptr, CURLOPT_URL, task_ptr->url);
    curl_easy_setopt(easy_ptr, CURLOPT_WRITEFUNCTION, sDemoDownloadWrite);
    curl_easy_setopt(easy_ptr, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(easy_ptr, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(easy_ptr, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(easy_ptr, CURLOPT_USERAGENT, "ircituilib-demo/1.0");
    curl_multi_add_handle(multi_ptr, easy_ptr);

    do {
      curl_multi_perform(multi_ptr, &running);
      if (running) {
        curl_multi_poll(multi_ptr, NULL, 0, 100, NULL);
        /* Suspension point. The worker is free from here until the resume. */
        UfsrvCoroutineYield();
      }
    } while (running && ++guard < 400);

    /* curl_multi_info_read is where the per-transfer CURLcode lives; without
       it a failure is indistinguishable from an empty 200. */
    {
      CURLMsg *m_ptr;
      int      msgs_left = 0;
      while ((m_ptr = curl_multi_info_read(multi_ptr, &msgs_left)) != NULL) {
        if (m_ptr->msg == CURLMSG_DONE) {
          curl_result = m_ptr->data.result;
          curl_easy_getinfo(easy_ptr, CURLINFO_RESPONSE_CODE, &http_code);
        }
      }
    }
    /* file:// and the other non-HTTP schemes have no response code at all, so
       for those a clean transfer is the whole test. */
    ok = (curl_result == CURLE_OK) && (http_code == 0 || http_code == 200);

    curl_multi_remove_handle(multi_ptr, easy_ptr);
  }

  if (fp) fclose(fp);

  {
    struct stat st;
    long        len = (stat(sDemoImagePath, &st) == 0) ? (long)st.st_size : -1;
    IRCITUI_LOG_INFO("demo: download finished ok=%d curl=%d (%s) http=%ld guard=%d bytes=%ld path=%s",
                     ok, (int)curl_result, curl_easy_strerror(curl_result), http_code, guard, len, sDemoImagePath);
  }

  /* The framework's image loader is libpng, so it is PNG-only. Checking the
     signature here turns "downloaded fine, cannot be shown" into an ordinary
     transfer failure: it travels the error path, the partial file is removed,
     and Thread 0 raises an alert naming it - rather than a decode failing two
     steps later behind an empty window. */
  if (ok) {
    unsigned char sig[8] = {0};
    FILE *        probe_ptr = fopen(sDemoImagePath, "rb");

    if (!probe_ptr || fread(sig, 1, sizeof sig, probe_ptr) != sizeof sig ||
        sig[0] != 0x89 || sig[1] != 'P' || sig[2] != 'N' || sig[3] != 'G') {
      IRCITUI_LOG_WARN("demo: fetched content is not a PNG, and the image loader is PNG-only: %s", sDemoImagePath);
      ok = 0;
    }
    if (probe_ptr) fclose(probe_ptr);
  }

  if (ok) {
    UfsrvPromiseSetValue(task_ptr->promise, sDemoImagePath, NULL);
  } else {
    remove(sDemoImagePath);
    UfsrvPromiseSetError(task_ptr->promise, -1);
  }

  if (easy_ptr) curl_easy_cleanup(easy_ptr);
  if (multi_ptr) curl_multi_cleanup(multi_ptr);

  *task_ptr->done_ptr = 1;
  free(task_ptr);
  UfsrvCoroutineExit();
}

/* The pool job: it is the worker that owns the coroutine.
 *
 * stages + 1 resumes. The first runs the body up to its first yield, which
 * hands the worker back; each later one delivers the stage after it. A yield
 * with nothing left to resume it is a coroutine that never finishes and never
 * reports - the first cut of this did exactly that and delivered nothing.
 *
 * entry and stages are read into locals BEFORE the first resume, because the
 * body frees the task on its way out. */
static void
sDemoFetchJob(void *context_ptr)
{
  static const struct timespec stage_pause = { .tv_sec = 0, .tv_nsec = 60 * 1000 * 1000 };
  DemoFetchTask *              task_ptr    = context_ptr;
  /* Read into locals BEFORE the first resume: the body frees the task on its
     way out, and the loop below still needs these after that. */
  UfsrvCoroutineEntry entry     = task_ptr->entry;
  int *               done_ptr  = task_ptr->done_ptr;
  UfsrvCoroutine *    coroutine_ptr;
  int                 guard = 0;

  coroutine_ptr = UfsrvCoroutineCreate(entry, task_ptr);
  if (!coroutine_ptr) {
    IRCITUI_LOG_ERROR("demo: coroutine could not be created on the worker");
    free(task_ptr);
    return;
  }

  /* Resume until the body reports. A staged body yields a known number of
     times; a download yields once per poll that finds the socket not ready, so
     the count is the transfer's, not ours - which is why completion is read
     from the future rather than counted. */
  UfsrvCoroutineResume(coroutine_ptr);
  while (!*done_ptr && ++guard < 500) {
    nanosleep(&stage_pause, NULL);
    UfsrvCoroutineResume(coroutine_ptr);
  }

  if (!*done_ptr)
    IRCITUI_LOG_WARN("demo: async operation did not report within %d resumes", guard);

  free(done_ptr);
}

/* Both keys start here. `fail` picks the body and its ending. */
/* Ctrl-D. Same chain as the two above, with a different body and a different
   delivery: the result is a file path, and Thread 0 turns it into a window. */
static void
sDemoStartDownload(IrcitUiEventDispatcher *disp_ptr)
{
  IrcitUiBridgeContext *bridge_ptr;
  DemoFetchTask *       task_ptr;
  UfsrvFuture *         future_ptr = NULL;

  bridge_ptr = calloc(1, sizeof(*bridge_ptr));
  task_ptr   = calloc(1, sizeof(*task_ptr));
  if (!bridge_ptr || !task_ptr) {
    free(bridge_ptr);
    free(task_ptr);
    return;
  }

  bridge_ptr->dispatcher  = disp_ptr;
  bridge_ptr->target_node = NULL; /* nothing is written into: a window is raised */
  bridge_ptr->target_id   = "async_download";
  bridge_ptr->kind        = IRCITUI_BRIDGE_IMAGE;

  task_ptr->entry   = sDemoDownloadEntry;
  task_ptr->kind    = IRCITUI_BRIDGE_IMAGE;
  task_ptr->promise = UfsrvPromiseCreate(&future_ptr);
  if (!task_ptr->promise) {
    free(bridge_ptr);
    free(task_ptr);
    return;
  }
  task_ptr->done_ptr = calloc(1, sizeof(*task_ptr->done_ptr));
  strncpy(task_ptr->url, sFetchUrl, sizeof(task_ptr->url) - 1);

  UfsrvFutureThen(future_ptr, IrcitUiPostFutureContinuation, bridge_ptr);

  if (!UfsrvSchedulerPoolSubmit(IrcitUiScheduler(), sDemoFetchJob, task_ptr)) {
    free(bridge_ptr);
    free(task_ptr);
  }

  UfsrvFutureRelease(future_ptr);
}

static void
sDemoStartFetch(IrcitUiEventDispatcher *disp_ptr, int fail)
{
  IrcitUiNode *         focus_ptr = IrcitUiFocusedNode();
  IrcitUiBridgeContext *bridge_ptr;
  DemoFetchTask *       task_ptr;
  UfsrvFuture *         future_ptr = NULL;

  bridge_ptr = calloc(1, sizeof(*bridge_ptr));
  task_ptr   = calloc(1, sizeof(*task_ptr));
  if (!bridge_ptr || !task_ptr) {
    free(bridge_ptr);
    free(task_ptr);
    return;
  }

  bridge_ptr->dispatcher  = disp_ptr;
  bridge_ptr->target_node = focus_ptr;
  /* Captured now, while focus is certainly alive. The failure path reports by
     id and never by node - see ui_async_bridge.h. */
  bridge_ptr->target_id   = (focus_ptr && focus_ptr->common.id) ? focus_ptr->common.id : "cmdline";

  task_ptr->entry   = fail ? sDemoFetchFailEntry : sDemoStagedFetchEntry;
  task_ptr->stages  = fail ? 1 : 3;
  task_ptr->kind    = IRCITUI_BRIDGE_TEXT;
  task_ptr->promise = UfsrvPromiseCreate(&future_ptr);
  task_ptr->done_ptr = calloc(1, sizeof(*task_ptr->done_ptr));
  if (!task_ptr->promise) {
    free(bridge_ptr);
    free(task_ptr);
    return;
  }
  strncpy(task_ptr->url, fail ? "https://ircit.internal/missing.png" : "https://ircit.internal/staged.png",
          sizeof(task_ptr->url) - 1);

  /* The continuation is where the worker's involvement ends. */
  UfsrvFutureThen(future_ptr, IrcitUiPostFutureContinuation, bridge_ptr);

  if (!UfsrvSchedulerPoolSubmit(IrcitUiScheduler(), sDemoFetchJob, task_ptr)) {
    free(bridge_ptr);
    free(task_ptr);
  }

  UfsrvFutureRelease(future_ptr);
}

/* --- The bars gallery: one asynchronous run driving six bars -----------------
 *
 * PUBLIC API ONLY, on the host side. Everything this section calls to move a bar is in
 * ui.h: IrcitUiPostBarProgress to report from the worker, IrcitUiBarValue and
 * IrcitUiBarSetValue to read and write on Thread 0, IrcitUiFind and IrcitUiInflateFloat to
 * reach the panel. Nothing here reaches past the public surface to MOVE a bar -- and the
 * readout below reads the private field as well, deliberately, so the two views can be
 * printed side by side rather than one of them being taken on trust.
 *
 * WHAT MAKES IT ASYNC, AND WHY THAT IS NOT DECORATION. The work runs as a coroutine on the
 * framework's worker pool. Each stage yields -- UfsrvCoroutineYield suspends the body and
 * hands the worker back to the pool, so the wait costs no thread -- and the job that owns
 * the coroutine sleeps between resumes. At every stage the body posts a percentage through
 * IrcitUiPostBarProgress, which copies an id and a number into a fixed envelope and pushes
 * it on the lock-free ring. Thread 0 drains that ring on its next pass and applies it.
 *
 * THE WORKER NEVER TOUCHES A NODE. It cannot: it holds an id string and an int. The tree is
 * owned by the loop's thread, and a worker writing to it would race every paint and every
 * layout pass that thread is running.
 *
 * THE RUN NEEDS NO CANCELLATION PROTOCOL, and that falls out of the id rather than being
 * arranged. Panel closed while the run is in flight? The reports already on the ring
 * resolve to nothing and are dropped. That IS the cancellation. s_bars_stop only makes the
 * coroutine give up early instead of posting into the void for the rest of its stages.
 *
 * The promise carries the OUTCOME and resolves once, which is what a promise is. The
 * increments are not resolutions and do not travel on it -- a promise that resolved thirty
 * times would not be one. That is why the two channels are separate, and it is the whole
 * difference between this and a future delivering a value. */
static const struct
{
  const char *id;
  int         ceiling; /**< Where this bar finishes, so the six end visibly different. */
} sBarTargets[] = {
  { "bar_plain",   100 }, { "bar_glyphs",   88 }, { "bar_bordered", 76 },
  { "bar_padded",   64 }, { "bar_accent",   52 }, { "bar_wide",    100 },
  { "bar_adjacent", 92 }, { "bar_center",   68 }, { "bar_grad",     84 },
  { "bar_para",     96 }, { "bar_buffered", 100 },
};
#define DEMO_BAR_COUNT ((int)(sizeof sBarTargets / sizeof sBarTargets[0]))

/* How many stages a run has. Twenty at the job's 90 ms pause is a shade under two seconds,
   which is long enough to watch and short enough not to be in the way. */
#define DEMO_BAR_STEPS 20

/* Set by Thread 0 when the panel closes; read by the coroutine on a worker. An int, and not
   a pointer to anything: it is the only thing a worker is allowed to know about the tree. */
static int s_bars_stop;

/* Where the readout lands. A file-scope buffer, and that is forced rather than lazy: the
   node borrows its display string (nodes never own their text), and the status label's
   painter resolves that alias first, so whatever this points at must outlive the node. */
static char s_bars_readout[160];

/* --- The seek: the host owns the value, the bar only reports -------------------
 *
 * The callback does exactly what the model says it should: it decides a number and hands it
 * back through the public setter. Nothing here is a special case for the demo -- this is the
 * whole of the contract, and an application's version would differ only in what it does
 * instead of `value * 100`.
 *
 * WHY user_data CARRIES THE NODE. The callback receives an id, not a pointer, and the
 * obvious way to act on it is IrcitUiFind -- a tree walk on EVERY motion report, hundreds of
 * times for one gesture, to re-resolve a node that cannot change mid-gesture. The binding
 * already resolved it once, so the demo hands it through user_data, which is what that
 * argument is for.
 *
 * That is safe here and the reason is worth stating rather than assuming: the bars live in a
 * float the demo owns, and when that float is popped the engine's deflate sweep nulls
 * sDragNode, so no later gesture can reach a freed node. The binding is re-made when the
 * panel is next raised.
 *
 * THE RUN STOPS ON THE FIRST DRAGGING REPORT. Not a nicety -- it is the two-writers problem
 * this whole model exists to avoid, met head-on: the gallery's bars are being advanced by a
 * worker coroutine, and a user seeking one of them would fight it, with the winner decided
 * by which wrote last. A media player's real answer is to pause on seek, so that is what
 * this does, and it makes the demo's behaviour legible rather than flaky. */
static void
sOnSeek(const char *node_id_ptr, IrcitUiSeekPhase phase, float raw_value, void *user_data)
{
  IrcitUiNode *bar = user_data;
  int          pct = (int)(raw_value * 100.0f + 0.5f);

  if (!bar) return;

  /* The user has taken the value. See the note above. */
  if (phase == IRCITUI_PHASE_DRAGGING) s_bars_stop = 1;

  /* HOST-OWNED. The bar has not moved and will not move until this call -- which is the
     whole of the difference from a self-seeking widget. */
  IrcitUiBarSetValue(bar, pct);

  /* The readout says which phase arrived and what was set, so a reviewer can see the
     preview/commit split rather than take it on trust: a drag shows DRAGGING for every
     column the pointer crosses and one RELEASED at the end. */
  snprintf(s_bars_readout, sizeof s_bars_readout,
           "seek %s: %s raw=%.3f -> api IrcitUiBarSetValue=%d",
           node_id_ptr ? node_id_ptr : "(no id)",
           phase == IRCITUI_PHASE_DRAGGING ? "DRAGGING (preview)" : "RELEASED (commit)",
           (double)raw_value, pct);
  {
    IrcitUiNode *st = IrcitUiFind(IrcitUiScreen.root, "status");
    if (st) {
      st->common.interaction_defaults.user = s_bars_readout;
      IrcitUiInvalidate(st);
    }
  }
}

/* Bind every bar in the gallery. Called AFTER the float is raised, and that is forced rather
 * than convenient: IrcitUiBindSeek resolves the id once and fails on a bar that does not
 * exist yet, which is the documented cost of not walking the tree on every motion. */
static void
sBarsBindSeek(IrcitUiNode *root_ptr)
{
  int i;

  for (i = 0; i < DEMO_BAR_COUNT; i++) {
    IrcitUiNode *bar = IrcitUiFind(root_ptr, sBarTargets[i].id);
    if (!bar) continue;
    /* The node itself as user_data -- see sOnSeek. */
    IrcitUiBindSeek(sBarTargets[i].id, sOnSeek, bar);
  }
}


typedef struct
{
  UfsrvCoroutineEntry     entry;
  UfsrvPromise *          promise;
  IrcitUiEventDispatcher *disp;     /**< Carried so the body can post. A worker may hold it:
                                         IrcitUiDispatcherPostMessage is the lock-free push. */
  int *                   done_ptr; /**< Same contract as DemoFetchTask's - see its comment. */
  int                     steps;
} DemoBarTask;

/* The private view of a bar's value: its own state struct, which no application outside
   this file can name. It is what the painter reads. -1 means "no bar state", which is
   outside the 0..100 range and so cannot be confused with a real position. */
static int
sBarValueInternal(IrcitUiNode *node_ptr)
{
  IrcitUiBarState *state_ptr = node_ptr ? node_ptr->common.widget_state : NULL;
  return state_ptr ? state_ptr->value : -1;
}

/* The readout, and the one place the API surface and the framework internals are printed
   SIDE BY SIDE rather than described.
 *
 * The left half calls IrcitUiBarValue, which is public, and the right half reads the node's
 * widget_state directly, which is not. They are two views of one int, and printing both is
 * what makes the split visible rather than asserted: the words say which is the
 * framework's business and which is the application's, the equality is checkable at a
 * glance, and MISMATCH is printed when they disagree so the readout cannot quietly agree
 * with itself.
 *
 * Reached through a private header, and ui_main.c already does that five times, each with
 * its reason stated in the includes block. This is the sixth and the reason is the same as
 * the other five: a readout that could only call public functions could not show the
 * difference, and showing the difference is what it is for. NOTHING HERE MOVES A BAR -- the
 * read is the whole of it, and every write in this section goes through the public setter. */
static void
sBarsReport(IrcitUiNode *root_ptr)
{
  IrcitUiNode *st = IrcitUiFind(root_ptr, "status");
  IrcitUiNode *n, *g;
  int          api, internal, req, live;

  if (!st) return;

  /* bar_wide, because its width differs from the rest and so its filled count is the one a
     reviewer will want to compare against its neighbours. */
  n = IrcitUiFind(root_ptr, "bar_wide");
  if (!n) {
    st->common.interaction_defaults.user = "bars: panel is not raised";
    IrcitUiInvalidate(st);
    return;
  }

  api      = IrcitUiBarValue(n);   /* PUBLIC getter */
  internal = sBarValueInternal(n); /* private state  */

  /* And the gradient, which is the case where the two views legitimately DIFFER and the
     difference is the interesting part. The application asks for a gradient; whether one
     is drawn depends on a colour register being free, which depends on what the canopy,
     the fade and the notification box are holding at that instant. So "requested" and
     "live" are two different questions, both public, and printing both is what makes a
     bar that is silently flat legible as "out of ramps" rather than as "subtle". */
  g    = IrcitUiFind(root_ptr, "bar_grad");
  req  = g ? IrcitUiBarGradientRequested(g) : 0;
  live = g ? IrcitUiBarGradientLive(g) : 0;

  snprintf(s_bars_readout, sizeof s_bars_readout,
           "api bar_wide=%d/%d grad req=%d live=%d | internal widget_state->value=%d%s",
           api, 100, req, live, internal,
           api == internal ? "" : "  MISMATCH");
  st->common.interaction_defaults.user = s_bars_readout;
  IrcitUiInvalidate(st);
}

/* The coroutine body. Written as one straight line even though it is suspended `steps`
   times in the middle, which is what a coroutine buys over a callback. */
static void
sDemoBarsEntry(void)
{
  DemoBarTask *task_ptr = UfsrvCoroutineGetArg();
  int          i, b;

  for (i = 1; i <= task_ptr->steps; i++) {
    /* The suspension point. The worker goes back to the pool here. */
    UfsrvCoroutineYield();
    if (s_bars_stop) break;

    for (b = 0; b < DEMO_BAR_COUNT; b++) {
      char id_buf[64];
      int  pct = i * sBarTargets[b].ceiling / task_ptr->steps;
      /* Copied into a local before posting. The ids are literals and do outlive the report,
         but the report crosses a thread boundary and nothing in IrcitUiPostBarProgress's
         contract should have to rely on where the string came from -- so this makes the
         requirement explicit at the call site. */
      strncpy(id_buf, sBarTargets[b].id, sizeof(id_buf) - 1);
      id_buf[sizeof(id_buf) - 1] = '\0';
      IrcitUiPostBarProgress(task_ptr->disp, id_buf, pct);

      /* And the buffered position, which is what the gallery's `bar_buffered` node is for.
         A lead ahead of playback that COLLAPSES at three points in the run, so one pass
         shows every state a streaming bar has: buffer ahead of the play head (the band
         between them), buffer caught up after a stall (no band at all -- the clamp), and
         buffer at the end. Both reports are posted every stage; the poster clamps the sum,
         so `pct + lead` past 100 is not this file's arithmetic to get right.
         A worker posts it for the reason the line above is a post and not a call: it holds
         an id and an int and never a node. */
      {
        int lead = (i == 5 || i == 6 || i == 12) ? 0 : 16;
        IrcitUiPostBarBuffered(task_ptr->disp, id_buf, pct + lead);
      }
    }
  }

  /* The outcome, resolved once. A short literal, so the release function is NULL. */
  UfsrvPromiseSetValue(task_ptr->promise, "bars_complete", NULL);
  *task_ptr->done_ptr = 1;
  free(task_ptr);
  UfsrvCoroutineExit();
}

/* The pool job: resume the body, pause, resume. The pacing is the JOB's and not the
   coroutine's, which is the same split the fetch demos use -- a yield suspends without
   waiting, so something has to decide when to resume. */
static void
sDemoBarsJob(void *context_ptr)
{
  static const struct timespec stage_pause = { .tv_sec = 0, .tv_nsec = 90 * 1000 * 1000 };
  DemoBarTask *                task_ptr    = context_ptr;
  /* Read into locals before the first resume: the body frees the task on its way out. */
  UfsrvCoroutineEntry entry     = task_ptr->entry;
  int *               done_ptr  = task_ptr->done_ptr;
  UfsrvCoroutine *    co_ptr;
  int                 guard = 0;

  co_ptr = UfsrvCoroutineCreate(entry, task_ptr);
  if (!co_ptr) {
    IRCITUI_LOG_ERROR("demo: the bars coroutine could not be created on the worker");
    free(task_ptr);
    return;
  }

  UfsrvCoroutineResume(co_ptr);
  /* steps + 1 resumes exhaust a body that yields once per stage; the guard is set well clear
     of that, and exists to bound the loop rather than to pace it. */
  while (!*done_ptr && ++guard < DEMO_BAR_STEPS * 4) {
    nanosleep(&stage_pause, NULL);
    UfsrvCoroutineResume(co_ptr);
  }

  if (!*done_ptr) IRCITUI_LOG_WARN("demo: the bars run did not report within %d resumes", guard);

  free(done_ptr);
}

/* Start a run. The continuation is the framework's, on the same terms as the two fetch
   demos: it exists so a FAILURE raises the alert, and its success path is deliberately
   inert -- a NULL target node means the delivery returns without writing anything, because
   the values that matter already arrived through the progress channel. */
static void
sDemoBarsStart(IrcitUiEventDispatcher *disp_ptr)
{
  IrcitUiBridgeContext *bridge_ptr;
  DemoBarTask *         task_ptr;
  UfsrvFuture *         future_ptr = NULL;

  bridge_ptr = calloc(1, sizeof(*bridge_ptr));
  task_ptr   = calloc(1, sizeof(*task_ptr));
  if (!bridge_ptr || !task_ptr) {
    free(bridge_ptr);
    free(task_ptr);
    return;
  }

  s_bars_stop = 0;

  bridge_ptr->dispatcher  = disp_ptr;
  bridge_ptr->target_node = NULL; /* see above: the success path is meant to be inert */
  bridge_ptr->target_id   = "progress_panel";
  bridge_ptr->kind        = IRCITUI_BRIDGE_TEXT;

  task_ptr->entry    = sDemoBarsEntry;
  task_ptr->disp     = disp_ptr;
  task_ptr->steps    = DEMO_BAR_STEPS;
  task_ptr->done_ptr = calloc(1, sizeof(*task_ptr->done_ptr));
  task_ptr->promise  = UfsrvPromiseCreate(&future_ptr);
  if (!task_ptr->promise || !task_ptr->done_ptr) {
    free(bridge_ptr);
    free(task_ptr);
    return;
  }

  UfsrvFutureThen(future_ptr, IrcitUiPostFutureContinuation, bridge_ptr);

  if (!UfsrvSchedulerPoolSubmit(IrcitUiScheduler(), sDemoBarsJob, task_ptr)) {
    free(bridge_ptr);
    free(task_ptr);
    IRCITUI_LOG_WARN("demo: the bars run could not be submitted to the pool");
    return;
  }

  UfsrvFutureRelease(future_ptr);
  IRCITUI_LOG_INFO("demo: bars run started, %d stages over %d bars", DEMO_BAR_STEPS, DEMO_BAR_COUNT);
}

/* Raise or drop the gallery, and take the run with it.
 *
 * Opening starts a run, so the panel is never up and idle -- there is nothing to see in a
 * gallery of bars frozen at zero. Closing stops it, and the reports already in flight are
 * dropped by the id lookup, which is the cancellation the design leans on rather than a
 * protocol of its own. */
static void
sToggleBarsPanel(IrcitUiNode *root_ptr, IrcitUiEventDispatcher *disp_ptr)
{
  IrcitUiNode *ex = IrcitUiFind(root_ptr, "progress_panel");

  if (ex) {
    s_bars_stop = 1;
    IrcitUiPopStacked(ex);
    IrcitUiFocusTop();
    return;
  }

  IrcitUiInflateFloat(root_ptr, "progress_panel");

  /* The gradient, through the C API, on the one node the layout left without any styling
   * of its own. This is the other half of the split bar_glyphs demonstrates: a template
   * can name glyphs (C24) but there is no `gradient:` key, because two endpoint colours
   * are live colour data rather than layout, and a key for them would mean two places
   * could set one thing.
   *
   * Carbon to cyan, which is far enough apart on a 256-colour terminal to be unmistakable
   * when it works and unmistakable when it does not. */
  {
    IrcitUiNode *g = IrcitUiFind(root_ptr, "bar_grad");
    if (g) IrcitUiBarSetGradient(g, 255, 88, 0, 0, 200, 255);
  }

  /* Every bar takes a seek binding. After the inflate, because IrcitUiBindSeek resolves the
     id once -- see its declaration. */
  sBarsBindSeek(root_ptr);

  sDemoBarsStart(disp_ptr);
  /* After the inflate, so the nodes are there to be read -- and after the gradient is set,
     so the readout reports the state the reviewer is about to look at rather than the one
     before it. */
  sBarsReport(root_ptr);
}

/* Which of the two is bound. Starts allowed, so the Tools button opens its
   layout_template - tools_panel - the way it did before this binding existed,
   and F9 flips it to the refusal. The refusal is the application overriding the
   framework, so it is the one that has to be asked for rather than the default:
   the menu is what the demo is for. */
static int s_tools_denied = 0;
/* Counts the notifications Ctrl-N raises, so a second press is visibly a
   replacement rather than a repeat. */
static int s_notify_count = 0;

/* The fade's completion callback: it runs on Thread 0 from the frame loop, once the
   colour has arrived, and it is the only place the box is taken away. */
static void
sFadeDone(void *user_ptr)
{
  (void)user_ptr;
  IrcitUiSnackbarDismiss();
}

/* The application's terminal, brought up on its own terms.
 *
 * Asking the registry for "ncurses" is this application declaring that it knows
 * how to initialise ncurses for what it wants to do, and this is where it says
 * so. Every call below is a choice, not a requirement: noecho because the
 * engine draws its own echo, curs_set(0) because the engine paints its own
 * caret, set_escdelay because the blink tick below is 250 ms and the 1000 ms
 * default would hold a lone Esc for a whole tick, and the mouse mask because
 * this demo wants drag motion.
 *
 * None of that belongs in the driver. A different application wants a different
 * set - line input, a visible hardware cursor, ncurses' press/release-to-click
 * synthesis - and a driver that fixed the sequence would impose one answer on
 * every application that ever embedded it.
 *
 * Runs from IrcitUiInitialiseFrameworkContext, after the registry has locked a
 * driver and before the framework is used, which is the first point at which
 * the application knows what backend it is initialising. */
static void
sOnDriverInitialised(IrcitUiWindowDriver *driver_ptr)
{
  (void)driver_ptr;

  /* Before initscr: ncurses reads it as it initialises, so afterwards has no
     effect. It stops ncurses substituting its own line-drawing glyphs for the
     ACS characters, which would displace the box-drawing the engine emits. */
  setenv("NCURSES_NO_UTF8_ACS", "1", 0);

  initscr();
  cbreak();
  noecho();
  keypad(stdscr, TRUE);
  if (has_colors()) start_color();
  /* The engine paints the only cursor there is, so the hardware one is turned
     off rather than left sitting in the edit window as a second, stationary
     caret. */
  curs_set(0);
  /* ncurses resolves a lone ESC against ESCDELAY, 1000 ms by default. The blink
     tick asks for a key every PRIV_CONFIG_DEFAULT_CARET_BLINK_MS, so under that
     default a lone Esc would be held for up to a whole tick before ncurses
     decided it was not the head of a sequence. */
  set_escdelay(25);
  IrcitUiMouseInit();
}

/* The application's key policy.
 *
 * This is what the demo's own loop used to be: everything it did with a
 * keystroke that was not the framework's business. The loop is the framework's
 * now, so the policy moves to where the framework looks for it -- a listener on
 * a node.
 *
 * ON THE REFERENCE MODEL'S ROOT, NOT THE SCREEN TREE'S. There is no screen tree
 * when this is installed, and IrcitUiScreenReset replaces it whenever it runs --
 * including once inside IrcitUiMainLooper. IrcitUiClone copies interactions_user
 * wholesale, so a listener on the model root is carried by every screen root the
 * framework builds, however many times it resets. Installing it on the screen
 * root would work until the first reset and then silently stop.
 *
 * Returns 1 for a key it handled and 0 for one it did not, which is the contract
 * every listener in this API follows. That matters here: an F-key that the
 * focused widget ignores BUBBLES, so this sees it -- while a printable character
 * is consumed by an edit field before it ever gets here. 'q' therefore cannot
 * quit from this position, and no longer does; a pre-dispatch quit was the demo
 * loop's, not the framework's.
 */
/* The demo's delete/undo key.
 *
 * ^O, and the choice is measured rather than tasteful -- see the case arm below and
 * dev/probes/snackbar/p_hotkeys.c. Named, so that the key the notification's action is
 * bound to and the key this listener answers cannot drift apart: the overlay compares
 * the int it was handed, so a mismatch would be a hotkey that silently never fires. */
#define IRCITUI_DEMO_KEY_DELETE 15

/* The frame's two switches.
 *
 * Ctrl-B for the profile, Ctrl-P for the shadow -- "P" for the pseudo-3D the section that
 * asked for it named it. Both are chosen against two lists, and the second one is the check
 * that caught a first attempt:
 *
 *  - the demo's own arms are 4, 5, 7, 14, 15, 20 and 25, and the five control codes the line
 *    discipline keeps -- ^C, ^Q, ^S, ^Z, ^\ -- are the ones dev/probes/snackbar/p_hotkeys.c
 *    swept out of the delivered set. Control codes rather than letters, because the loop's
 *    edit fast path hands every printable character straight to a focused field without
 *    reaching a dispatcher, so a printable key here would work only while nothing held focus;
 *  - and the FRAMEWORK's own keys, because a key being delivered by the terminal does not
 *    mean the tree lets it through. **`IRCITUI_KEY_BS` is 8**, so Ctrl-H is the framework's
 *    Backspace and is consumed by whatever holds focus: bound there, the toggle produced
 *    nothing at all, with no error and no visible difference from a keypress that never
 *    arrived. Measured, by driving the demo on a pty and looking for the change on the glass.
 *    Ctrl-P (16) is in neither list.
 *
 *    Ctrl-L (12) IS the third gallery key, chosen by the same sweep rather than by taste.
 *    F12 is the framework loop's own exit (main_looper.c returns 0 on it), so the gallery
 *    could not take it; F11 does not survive the terminal; and the five line-discipline
 *    codes (^C, ^Q, ^S, ^Z, ^\) never arrive. Ctrl-L is in none of those lists, is bound
 *    nowhere in this tree, and survives -- verified on a pty, which is the only place the
 *    claim can be checked.
 *
 * The measure is worth repeating for any demo key: the pty says the terminal delivered it,
 * and only the glass says the tree acted on it. */
#define IRCITUI_DEMO_KEY_FRAME  2
#define IRCITUI_DEMO_KEY_SHADOW 16
/* The bars gallery: raise it, run it, drop it. */
#define IRCITUI_DEMO_KEY_BARS   12

/* The predictive gallery: raise it, type in it, drop it.
 *
 * Ctrl-A (1), chosen by the same sweep the two above were, and it is the only one of the
 * three whose name is also its mnemonic. It is in neither of the lists that disqualified the
 * others: the demo's own arms are 1 now and were 4, 5, 7, 12, 14, 15, 16, 20 and 25, and the
 * five control codes the line discipline keeps are ^C, ^Q, ^S, ^Z and ^\ -- so Ctrl-A is
 * delivered, is bound nowhere in this tree, and reaches the listener. Verified on a pty
 * rather than inferred: the pty says the terminal delivered it, and only the glass says the
 * tree acted on it. */
#define IRCITUI_DEMO_KEY_PREDICT 1

/* Ctrl-R: teach the panel's field its own word, and recompute the readouts.
 *
 * ONE KEY FOR TWO EFFECTS, because the panel has no clickable controls -- see the note in
 * sTogglePredictPanel. Learn first and report second, so the readout the user reads is the one
 * that includes what they just taught: the reverse order would show the state before it. */
#define IRCITUI_DEMO_KEY_PREDICT_LEARN 18

/* Ctrl-F: show or hide the suggestion list.
 *
 * NOT Ctrl-S, which is the obvious mnemonic and does not work: ^S is one of the five control
 * codes the line discipline keeps (with ^C, ^Q, ^Z and ^\), so it is never delivered to the
 * application at all -- a key that looks free and is not. Ctrl-F is in none of the lists. */
#define IRCITUI_DEMO_KEY_PREDICT_LIST 6

/* The one forward declaration in this file, and it is here because the key policy is
   stated before the dataset it acts on: Ctrl-O's delete is defined with the rest of the
   recycler policy, below the ten-thousand-row arrays it shifts. */
static int sDeleteCursorRow(void);

/* What the frame is drawn with, as a notification.
 *
 * THE NOTIFICATION OVERLAY RATHER THAN THE FOOTER'S STATUS LABEL, and that is a correction
 * rather than a preference. The first version wrote the same string with
 * IrcitUiNodeSetText into the node called "status", and **nothing appeared** -- because that
 * node is an ECHO channel: the demo points its interaction_defaults.user at an echo buffer,
 * and the painter resolves `user ? user : text`, so IrcitUiNodeSetText's write to
 * common.text is shadowed and invisible from the moment the alias is set. The engine says so
 * where it implements it, calling it "a real blind spot ... documented on the declaration
 * instead of being papered over". Found by driving the demo on a pty and looking for the
 * text on the glass.
 *
 * The overlay has none of that: it is the framework's own transient channel, it re-stamps
 * itself every frame, and its box wears the profile being reported -- so the message
 * demonstrates the setting as well as naming it. The text is COPIED, so a local is correct
 * here and a static would only hide that the contract is copy.
 *
 * The four spellings are the enum's own, in its order, so the message and the cycle agree by
 * construction rather than by two lists kept in step. */
static void
sSayFrame(void)
{
  static const char *const sNames[IRCITUI_BORDER_COUNT] = {"standard box", "rounded", "thick", "double"};
  char                     text[64];

  snprintf(text, sizeof text, "frame: %s, shadow: %s", sNames[IrcitUiBorderStyleGet()],
           IrcitUiShadow() ? "on" : "off");
  IrcitUiSnackbarShow(text, IRCITUI_SNACKBAR_LENGTH_SHORT);
}

/* ---- the predictive field -------------------------------------------------------- */

/* The demo's dictionary, and the words in it.
 *
 * A FILE-SCOPE ARRAY, deliberately: IrcitUiDictionaryCreateList borrows the word array
 * rather than copying it, which is the contract the header states, so the array has to
 * outlive the dictionary. A dictionary built over a temporary list would be a use-after-free
 * the compiler cannot see.
 *
 * The words are chosen for what they let the demo show rather than for their meaning:
 * "app", "irci" and "pred" all have more than one completion in this list, so the inline hint
 * has something to be the BEST of, and "predictive"/"prediction" differ past the point the
 * hint first appears -- which is what makes the break matrix visible when a keystroke
 * narrows the field. */
static const char *const sPredictWords[] = {
  "apple", "application", "apply", "append",
  "ircitui", "ircit", "icon",
  "predictive", "prediction", "predicate", "prefix",
  "terminal", "template", "temporary",
  "ncurses", "network", "notification"
};
#define N_PREDICT_WORDS (sizeof(sPredictWords) / sizeof(sPredictWords[0]))

/* THE FOUR SOURCES, AND THE COMPOSITE THAT CHAINS THEM.
 *
 * This is the seam's whole argument made concrete: four objects with nothing in common but
 * the interface -- a compiled-in array, a trie built by reading a file, a trie the user
 * teaches by clicking Learn, and a tag table that substitutes a glyph on accept -- asked in
 * the order they are chained. The field holds ONE pointer and cannot tell which of them
 * answered.
 *
 * THE ORDER IS THE PRIORITY, and it is chosen to be observable: the learned trie is asked
 * FIRST, so a word the user has taught beats every file and every compile-time list; the emoji
 * table second, because a `:` prefix is unambiguous; the file third and the embedded list
 * last. Type `pre` and you get the embedded list's word; click Learn twice on `prediction` and
 * type `pre` again -- the answer changes, and nothing about the field changed.
 *
 * Built once and borrowed by every field that predicts. */
static IrcitUiDictionary *sPredictLearned;   /* the user's own words, weighted          */
static IrcitUiDictionary *sPredictEmoji;     /* the tag table, substituting glyphs      */
static IrcitUiDictionary *sPredictFile;      /* assets/predictive_words.txt, read once  */
static IrcitUiDictionary *sPredictEmbedded;  /* the array below, compiled in            */
static IrcitUiDictionary *sPredictDict;      /* the composite of all four               */

/* What the file source read, so the demo can report it rather than assert it. */
static size_t sPredictFileWords, sPredictFileRefused;

/* The panel's readouts, held here because the alias the painter resolves -- see the note on
 * the status label, which is the same mechanism -- needs a buffer that outlives the call. */
static char s_pred_api[192];
static char s_pred_internal[192];
static char s_pred_sources[192];
static char s_pred_learned[192];
static char s_pred_footer[192];
/* Whether the suggestion list is on, so Ctrl-F toggles rather than only enabling. */
static int  s_predict_list_on;

/* Forward, because the panel's raise binds Learn's callback and the callback needs the
   panel's own helpers: the demo's key policy and the gallery that acts on it are defined in
   one order, and the one forward declaration is cheaper than reordering the section. */
static IrcitUiClickDisposition sOnPredictLearn(IrcitUiNode *node_ptr, void *user_ptr);

/* Compose both readouts and push them onto the panel's labels.
 *
 * THE TWO LINES ARE THE DEMONSTRATION THIS SECTION EXISTS FOR. The first is written from the
 * PUBLIC API alone -- IrcitUiEditTextHint and IrcitUiEditTextGetDictionary, both in ui.h --
 * and the second reads the field's own state through the private header, which is what the
 * framework decided rather than what it was asked. An "api" line claiming a hint while the
 * field drew none would be the separation made visible on the glass; MISMATCH is that case,
 * spelled out rather than left for the eye to catch. */
static void
sPredictReport(void)
{
  IrcitUiNode *field = IrcitUiFind(IrcitUiScreen.root, "predict_input");
  IrcitUiNode *a, *b;
  const char  *hint;
  int          have_dict;

  if (!field) return;

  /* PUBLIC API ONLY. */
  hint      = IrcitUiEditTextHint(field);
  have_dict = IrcitUiEditTextGetDictionary(field) != NULL;

  snprintf(s_pred_api, sizeof s_pred_api,
           "api hint=%s dict=%s embedded=%zu",
           hint ? hint : "(none)", have_dict ? "attached" : "none", N_PREDICT_WORDS);

  /* FRAMEWORK INTERNALS, read for contrast only: the same two facts as the framework holds
   * them, from the private state struct. This is the seventh private include in this file and
   * it has the same reason as the other six -- a readout that could only call public
   * functions could not show the difference between the two views, which is the point. */
  {
    IrcitUiTextEditState *st = (IrcitUiTextEditState *)field->common.widget_state;
    int                   len = st ? (int)strlen(st->text_buffer) : 0;
    int                   tok = st ? st->hint_token_at : 0;

    snprintf(s_pred_internal, sizeof s_pred_internal,
             "internal hint_len=%d token_at=%d caret=%d bytes=%d%s",
             st ? st->hint_len : -1, tok, st ? (int)st->cursor_position : -1, len,
             (!!hint != (st && st->hint_len > 0)) ? "  MISMATCH" : "");
  }

  {
    IrcitUiNode *c = IrcitUiFind(IrcitUiScreen.root, "pred_sources");
    char         text[192];
    snprintf(text, sizeof text, "sources learned=%zu emoji=9 file=%zu (refused %zu) embedded=%zu",
             IrcitUiDictionarySize(sPredictLearned), sPredictFileWords, sPredictFileRefused,
             N_PREDICT_WORDS);
    if (c) {
      c->common.interaction_defaults.user = s_pred_sources;
      snprintf(s_pred_sources, sizeof s_pred_sources, "%s", text);
      IrcitUiInvalidate(c);
    }
  }
  {
    IrcitUiNode *c = IrcitUiFind(IrcitUiScreen.root, "pred_learned");
    char         text[192];
    /* The composite's own answer for the token under the caret, beside the learned trie's
       node count -- so "the weight changed" is a number on the glass and not a claim. */
    snprintf(text, sizeof text, "learned trie=%zu nodes, weight-driven: %s",
             IrcitUiDictionarySize(sPredictLearned), hint ? hint : "(no candidate)");
    if (c) {
      c->common.interaction_defaults.user = s_pred_learned;
      snprintf(s_pred_learned, sizeof s_pred_learned, "%s", text);
      IrcitUiInvalidate(c);
    }
  }

  a = IrcitUiFind(IrcitUiScreen.root, "pred_api");
  if (a) {
    a->common.interaction_defaults.user = s_pred_api;
    IrcitUiInvalidate(a);
  }
  b = IrcitUiFind(IrcitUiScreen.root, "pred_internal");
  if (b) {
    b->common.interaction_defaults.user = s_pred_internal;
    IrcitUiInvalidate(b);
  }
}

/* Attach the dictionary to both fields that predict.
 *
 * CALLED FROM INSIDE THE LOOP, NOT AT WIRING TIME, and that is forced rather than chosen:
 * IrcitUiMainLooper calls IrcitUiScreenReset before it loops, and a reset reinstalls a fresh
 * clone of the model -- so a dictionary attached to the model would be a pointer carried into
 * nodes that are then thrown away, and a dictionary attached to a screen node before the
 * reset would be gone with it. The header states the contract (an attachment does not survive
 * a reset; re-attach) and this is the demo honouring it. */
static void
sPredictBuildSources(void)
{
  size_t refused = 0;

  if (sPredictDict) return;

  sPredictLearned  = IrcitUiDictionaryCreateTrie();
  sPredictEmoji    = IrcitUiDictionaryCreateEmoji();
  sPredictFile     = IrcitUiDictionaryCreateTrie();
  sPredictEmbedded = IrcitUiDictionaryCreateList(sPredictWords, N_PREDICT_WORDS);

  /* THE FILE IS READ HERE, ON THREAD 0, AND THAT IS A CHOICE RATHER THAN AN OVERSIGHT: it is
     sixty-odd lines, and a worker round-trip to read a file this size would cost more than it
     saved. The asynchronous path exists for the case this is not -- see the gallery's own
     note -- and the split is honest: this is the synchronous reader doing what it is for.
   *
     The path is relative to the working directory, which is where the demo is run from. A file
     that is not there is logged and contributes nothing. */
  sPredictFileWords = IrcitUiDictionaryIngestFile(sPredictFile, "assets/predictive_words.txt", 45, &refused);
  sPredictFileRefused = refused;

  sPredictDict = IrcitUiDictionaryCreateComposite(sPredictLearned, sPredictEmoji);
  if (sPredictDict) IrcitUiDictionaryChain(sPredictDict, sPredictFile);
  if (sPredictDict) IrcitUiDictionaryChain(sPredictDict, sPredictEmbedded);
}

static void
sPredictAttach(void)
{
  IrcitUiNode *panel_field = IrcitUiFind(IrcitUiScreen.root, "predict_input");
  IrcitUiNode *cmdline     = IrcitUiFind(IrcitUiScreen.root, "cmdline");

  sPredictBuildSources();
  if (!sPredictDict) return;

  /* The panel's own field, and the command line -- the field the demo is actually used
   * through. Both borrowing one dictionary is the contract in the header working. */
  if (panel_field) {
    IrcitUiEditTextSetDictionary(panel_field, sPredictDict);
    /* THE LIST, as well as the inline hint. On the panel's field only: a suggestion popup over
       the command line would follow the user around the main screen, which is a behaviour
       nobody asked for and the reason this is a per-field switch rather than a global one. */
    IrcitUiEditTextShowSuggestions(panel_field, 1);
  }
  /* The command line predicts too, so the feature is visible on the field a reviewer types
   * into without opening anything. Attached unconditionally and idempotently: this runs once
   * per loop entry, and attaching the same borrowed pointer twice is not a leak. */
  if (cmdline) IrcitUiEditTextSetDictionary(cmdline, sPredictDict);
}

/* The Report button's callback: recompute both readouts and carry on.
 *
 * It returns CARRY rather than SINK, which is this tree's convention for a handler that has
 * done its work and has no reason to stop the click travelling on -- the same disposition
 * sOnToolsAllowed returns. Nothing is being vetoed here; the button's other effects, if it
 * grows any, should still happen. */
static IrcitUiClickDisposition
sOnPredictReport(IrcitUiNode *node_ptr, void *user_ptr)
{
  (void)node_ptr;
  (void)user_ptr;
  sPredictReport();
  /* Focus back to the field, for the reason sOnPredictLearn states: a click moves it to the
     button, and a field without focus does not receive what is typed. */
  {
    IrcitUiNode *field = IrcitUiFind(IrcitUiScreen.root, "predict_input");
    if (field) IrcitUiSetFocus(field);
  }
  return IRCITUI_CLICK_CARRY;
}

/* Raise or drop the predictive gallery. */
static void
sTogglePredictPanel(IrcitUiNode *root_ptr)
{
  IrcitUiNode *ex = IrcitUiFind(root_ptr, "predictive_panel");

  if (ex) {
    IrcitUiPopStacked(ex);
    IrcitUiFocusTop();
    return;
  }

  IrcitUiInflateFloat(root_ptr, "predictive_panel");
  /* AFTER the raise, because the nodes do not exist before it. The float is realized by
   * IrcitUiInflateFloat, so the fields are there to attach to by the time this returns. */
  sPredictAttach();
  sPredictReport();
  /* The Report button, bound by id through the public binding API. Bound here rather than at
   * wiring time because the id only exists once the panel is in the tree, and IrcitUiBindClick
   * resolves through the model's reference tree -- the template is in it, so this succeeds
   * every time the panel is raised. Binding the same id twice is refused rather than
   * duplicated, so a second raise re-binds harmlessly. */
  /* BOUND HERE RATHER THAN AT WIRING TIME, because the ids only exist once the panel is in the
     tree: IrcitUiBindClick resolves through the model's reference tree, and the template is in
     it, so this succeeds every time the panel is raised. Binding the same id twice is refused
     rather than duplicated, so a second raise re-binds harmlessly.

     AN EARLIER VERSION OF THIS FILE CLAIMED THESE BINDINGS DID NOT FIRE. They do. That claim
     came from clicking the wrong rows: a hand-rolled terminal parser was used to read the
     panel's layout off the glass, it was off by two rows, and the clicks landed on the readout
     text above each button. The engine was right the whole time -- when the diagnostic was
     finally put at the hit test rather than at the dispatch, the buttons resolved exactly as
     they should. The lesson is the one this repository keeps re-learning: read the engine's
     view of an event, not a screen scraper's. */
  IrcitUiBindClick("pred_report", sOnPredictReport, NULL);
  IrcitUiBindClick("pred_learn", sOnPredictLearn, NULL);
  (void)root_ptr;
}

/* The Learn button: teach the learned source the word the field is showing.
 *
 * THIS IS THE ONLY WAY THE DEMO CAN LEARN, and the constraint is the framework's rather than a
 * shortcut: a printable character goes to the field through the loop's fast path and never
 * reaches an application listener, so the demo is never told that a word was typed, let alone
 * that one was finished. A click is the event that does reach it. The engine's own Enter
 * branch would be the natural hook and it is not one -- it echoes and clears, and a
 * text-changed notification is a field this tree declares but does not call.
 *
 * The word taught is the token under the caret, which is what the prediction is about: click
 * Learn with `prediction` in the field, twice, and `pre` starts completing to `prediction`
 * instead of to the embedded list's `predictive`. */
static IrcitUiClickDisposition
sOnPredictLearn(IrcitUiNode *node_ptr, void *user_ptr)
{
  IrcitUiNode *field = IrcitUiFind(IrcitUiScreen.root, "predict_input");
  (void)node_ptr;
  (void)user_ptr;

  if (!field || !sPredictLearned) return IRCITUI_CLICK_CARRY;

  /* THE TOKEN UNDER THE CARET, read from the field's own state.
   *
   * THE FIRST VERSION OF THIS LEARNED THE HINT INSTEAD, and it could not learn the one thing a
   * user would want to teach: a word typed in full has no hint -- the prediction declines to
   * offer a candidate equal to what was typed -- so `IrcitUiEditTextHint` returned NULL and the
   * button did nothing, silently, with the readout still reading `learned=1`. Measured on the
   * glass rather than reasoned about.
   *
   * This is the demo's internals-side read, and it is read for the reason the readouts are:
   * the public API has no token accessor, because a host does not need one -- the field's
   * prediction is the only thing a host is meant to consume. The demo needs the raw token to
   * demonstrate learning, so it reads the private state, as it does in six other places. */
  {
    IrcitUiTextEditState *st  = (IrcitUiTextEditState *)field->common.widget_state;
    const char           *buf = st ? st->text_buffer : NULL;
    size_t                end, start;
    char                  token[128];

    if (!st || !buf) return IRCITUI_CLICK_CARRY;
    end = st->cursor_position;
    if (end > strlen(buf)) end = strlen(buf);
    start = end;
    while (start > 0 && buf[start - 1] != ' ') start--;
    token[0] = 0;
    if (end > start && (end - start) < sizeof token) {
      memcpy(token, buf + start, end - start);
      token[end - start] = 0;
      IrcitUiDictionaryLearn(sPredictLearned, token);
    }
    /* A NOTIFICATION, NOT THE STATUS LINE, and the reason is measured: writing the status
       node's interaction_defaults alias -- the mechanism sBarsReport uses -- produced nothing
       on the glass from inside a click handler, while the panel's own readouts, written by the
       same sPredictReport a line later, did. So the confirmation goes through the demo's other
       established channel, IrcitUiSnackbarShow, which is what every other "what just happened"
       in this file uses.

       The message exists because a button that learns a word silently is indistinguishable
       from a broken one, and the panel's own line is cut by the panel's own width. */
    if (token[0]) {
      snprintf(s_pred_footer, sizeof s_pred_footer,
               "learned \"%s\": %zu nodes, and it is asked first",
               token, IrcitUiDictionarySize(sPredictLearned));
    }
    else snprintf(s_pred_footer, sizeof s_pred_footer, "nothing to learn: the field is empty");
    IrcitUiSnackbarShow(s_pred_footer, IRCITUI_SNACKBAR_LENGTH_SHORT);
  }
  sPredictReport();
  /* FOCUS GOES BACK TO THE FIELD, and that is not a nicety: a click puts focus on the button
     that was clicked, and the loop's edit fast path only fires for a focused EDIT_TEXT -- so
     without this line, clicking Learn once leaves the panel unable to accept another
     character, silently and with no visible cause. Measured: typing after a Learn click did
     nothing at all, and the readouts looked exactly like a prediction that had stopped
     working. */
  IrcitUiSetFocus(field);
  return IRCITUI_CLICK_CARRY;
}

/* The Predict header button's callback: raise the gallery the same way Ctrl-A does.
 *
 * SINK, NOT CARRY, and that is the whole reason this exists. A header button carrying a
 * layout_template is inflated by the engine when the click travels on -- so a CARRY would
 * build the panel from the template and never run the demo's own setup, leaving a field with
 * no dictionary and readouts still reading "api -". Measured: that is exactly what the first
 * version did, and the panel looked plausible enough to be missed.
 *
 * The same shape as sOnToolsDenied, which also declines to let the click carry. */
static IrcitUiClickDisposition
sOnPredictButton(IrcitUiNode *node_ptr, void *user_ptr)
{
  (void)node_ptr;
  (void)user_ptr;
  if (IrcitUiScreen.root) sTogglePredictPanel(IrcitUiScreen.root);
  return IRCITUI_CLICK_SINK;
}

static int
sOnAppKey(IrcitUiEventDispatcher *disp, IrcitUiEvent *ev, void *user)
{
  IrcitUiNode *root_ptr = IrcitUiScreen.root;
  (void)user;

  if (!root_ptr) return 0;

  switch (ev->key_code) {
  case KEY_F(10):
    IrcitUiDispatcherExit(disp);
    return 1;

  /* F2 and F3 start asynchronous operations. Neither raises anything and
     neither touches a node: the outcome travels back through the future's
     continuation and the lock-free pipeline, and Thread 0 acts on it. Which is
     the whole point - a worker never reaches the tree.

     F2 fails, and Thread 0 raises the watchdog alert for it.
     F3 succeeds after three stages, and Thread 0 writes the result into the
     focused field. */
  case KEY_F(2):
    sDemoStartFetch(disp, 1);
    return 1;

  case KEY_F(3):
    sDemoStartFetch(disp, 0);
    return 1;

  /* Ctrl-D: a real download. Nothing among F1-F10 is free - the demo claims
     every one of them - and F11/F12 are the loop's own, so the trigger is a
     control key rather than a function key.  does not collide with typing,
     because it is not a printable character. */
  case 4:
    sDemoStartDownload(disp);
    return 1;

  /* Ctrl-B cycles the frame's glyph profile; Ctrl-H turns the popup drop shadow on and off.
   *
   * Both are ONE setting for the whole process rather than a property of a node, which is
   * why they are demo keys and not layout keys: a profile is a different set of GLYPHS, so
   * no cell already on the glass becomes correct by being reinterpreted. That is what the
   * IrcitUiPaletteRepaint below is for, and forgetting it is invisible -- the setting
   * changes, nothing repaints, and the frames keep the old glyphs until something unrelated
   * dirties them.
   *
   * Read the standard box first, then cycle: the corners go from ACS glyphs whose
   * A_CHARTEXT is a letter to real U+256D-family codepoints, and the notification box --
   * which is not a node and asks for the profile itself -- changes with them. */
  case IRCITUI_DEMO_KEY_FRAME:
    IrcitUiSetBorderStyle((IrcitUiBorderStyle)((IrcitUiBorderStyleGet() + 1) % IRCITUI_BORDER_COUNT));
    IrcitUiPaletteRepaint();
    sSayFrame();
    return 1;

  /* The shadow only shows on a stacked popup, and this demo has two: the File menu and the
     emoji picker, both IrcitUiCreateFloatingWindow templates raised over the tree. Turn this
     on, then open one -- it casts onto the panel behind it, and it takes its shadow with it
     when dragged. */
  case IRCITUI_DEMO_KEY_SHADOW:
    IrcitUiSetShadow(!IrcitUiShadow());
    IrcitUiPaletteRepaint();
    sSayFrame();
    return 1;

  /* F6 and F1 fill the canvas for the scroll and selection demos. */
  case KEY_F(6):
  case KEY_F(1):
    IrcitUiDemoFill();
    return 1;

  /* F4 folds the side panel through the public accessor - the programmatic
     half of the feature, where the indicator's click is the other half.
     IrcitUiSetCollapsed raises the layout request; the frame that follows
     measures and repaints. */
  case KEY_F(4): {
    IrcitUiNode *panel = IrcitUiFind(root_ptr, "side_panel");
    if (panel) IrcitUiSetCollapsed(panel, !IrcitUiIsCollapsed(panel));
    return 1;
  }

  /* F7 and F8 open the two templated layers, and close them on a second
     press. Which one floats and which one anchors is the layered
     template's own IRCITUI_F_DRAGGABLE flag, not this decision. */
  case KEY_F(7): {
    IrcitUiNode *ex = IrcitUiFind(root_ptr, "tools_panel");
    if (ex) {
      IrcitUiPopStacked(ex);
      IrcitUiFocusTop();
    }
    else IrcitUiInflateFloat(root_ptr, "tools_panel");
    return 1;
  }
  case KEY_F(8): {
    IrcitUiNode *ex = IrcitUiFind(root_ptr, "file_menu");
    if (ex) {
      IrcitUiPopStacked(ex);
      IrcitUiFocusTop();
    }
    else IrcitUiInflateAnchored(root_ptr, "file_menu", IrcitUiFind(root_ptr, "menu_file"));
    return 1;
  }

  /* Ctrl-L raises the bars gallery and starts the asynchronous run that drives it; a
     second press drops the panel and stops the run. It sits here because it is the same
     shape as F7 and F8 -- a templated layer, raised and dropped -- with one thing they
     have not got: opening it also submits work to the worker pool, and the bars move
     while you watch because a coroutine is reporting to them from another thread.
     See the gallery section above for why that needs no cancellation protocol. */
  case IRCITUI_DEMO_KEY_BARS:
    sToggleBarsPanel(root_ptr, disp);
    return 1;

  /* Ctrl-A: the predictive gallery. The inline hint needs no key of its own -- it appears as
   * you type -- so this raises the panel the readouts live on and puts a dictionary on the
   * command line, which is where a reviewer will see it without opening anything. */
  case IRCITUI_DEMO_KEY_PREDICT:
    sTogglePredictPanel(root_ptr);
    return 1;

  /* Ctrl-F shows or hides the suggestion list on the panel's field. */
  case IRCITUI_DEMO_KEY_PREDICT_LIST: {
    IrcitUiNode *field = IrcitUiFind(root_ptr, "predict_input");
    if (!field) {
      IrcitUiSnackbarShow("Ctrl-A first: the predictive panel is not up", IRCITUI_SNACKBAR_LENGTH_SHORT);
      return 1;
    }
    s_predict_list_on = !s_predict_list_on;
    IrcitUiEditTextShowSuggestions(field, s_predict_list_on);
    IrcitUiSnackbarShow(s_predict_list_on ? "suggestion list: on (walk it with Up/Down, Tab accepts)"
                                          : "suggestion list: off", IRCITUI_SNACKBAR_LENGTH_SHORT);
    return 1;
  }

  /* Ctrl-R teaches the panel's field the word it is showing and refreshes the readouts. Raise
   * the panel first: the field it teaches is the panel's own. */
  case IRCITUI_DEMO_KEY_PREDICT_LEARN: {
    IrcitUiNode *field = IrcitUiFind(root_ptr, "predict_input");
    if (!field) {
      IrcitUiSnackbarShow("Ctrl-A first: the predictive panel is not up", IRCITUI_SNACKBAR_LENGTH_SHORT);
      return 1;
    }
    sOnPredictLearn(field, NULL);
    return 1;
  }

  /* F9 flips this application's verdict on the Tools button - the whole point
     of the demo: one button, one click, two outcomes, and the application
     decides which. The table holds one registration per id, so changing the
     verdict means resetting and registering again. */
  case KEY_F(9): {
    IrcitUiNode *st = IrcitUiFind(root_ptr, "status");
    s_tools_denied  = !s_tools_denied;
    IrcitUiBindingReset();
    IrcitUiBindClick("menu_tools", s_tools_denied ? sOnToolsDenied : sOnToolsAllowed, NULL);
    /* RE-REGISTERED, because the reset above empties the WHOLE table -- there is no
     * single-row removal. Without this line F9 silently disarms the Predict button, and the
     * next click on it would fall through to the template inflate and build a panel with no
     * dictionary and no readouts. Nothing would report the difference. */
    IrcitUiBindClick("menu_predict", sOnPredictButton, NULL);
    if (st) {
      st->common.interaction_defaults.user = s_tools_denied ?
                                               "Tools: click will be denied" :
                                               "Tools: click will open the panel";
      /* Points the alias at a different literal; no field of the node moved. */
      IrcitUiInvalidate(st);
    }
    return 1;
  }

  /* Ctrl-N raises a notification: the snackbar, which is a WINDOW and not a node.
     Nothing in the tree knows it exists, it takes no focus, and the layout does not
     move to make room for it. That is why the trigger is a key arm rather than an
     IrcitUiBindClick -- a binding hangs off a node, and this has none.

     It is offered every key before the tree is, by IrcitUiSnackbarKey at the top of
     IrcitUiDispatchKey, and it answers for exactly one: the action hotkey of a
     notification raised with IrcitUiSnackbarShowAction. This one carries no action, so
     it answers for nothing and typing goes on underneath it.

     Ctrl-N and not a function key: F1-F10 are all claimed above, F12 is the loop's
     exit, and F11 does not survive the terminal (see the note below). 14 is not
     bound anywhere in this tree or in the engine's router.

     THE BUFFER IS A LOCAL, deliberately. The call copies the text, so this array is
     gone before the box is ever painted and the notification still shows what it was
     given -- passing a local is the check on that contract, and passing a static
     would hide it. Pressing the key twice in quick succession takes the replacement
     path: the counter is there so the second message is visibly a different one. */
  case 14: {
    char msg[64];
    snprintf(msg, sizeof msg, "notification %d", ++s_notify_count);
    IrcitUiSnackbarShow(msg, 2000);
    return 1;
  }

  /* Ctrl-Y hides the notification box by dissolving it, rather than letting it fall.
   *
   * This is what the fade is FOR and why it has a callback: a widget cannot simply stop
   * being drawn, because its cells have to stay painted while the colour moves. So the
   * fade owns a register, the box is painted through that register's pair, and the
   * callback -- which runs from the frame loop when the colour has arrived -- is what
   * takes the box away.
   *
   * The box is the notification overlay, which is a WINDOW and not a node: the cheapest
   * thing here that owns its own cells and can therefore be dissolved and then dismissed
   * without touching the tree. */
  case 25: {
    if (!IrcitUiSnackbarActive()) {
      IrcitUiSnackbarShow("Ctrl-Y dissolves this box", 60000);
      return 1;
    }
    IrcitUiPaletteSetEffects(IrcitUiPaletteEffects() | IRCITUI_PALETTE_EFFECT_FADE);
    /* 255 on 234 is the overlay slot's own pair, so the fade starts where the box is
       already drawn and moves toward the panel behind it. */
    if (IrcitUiPaletteFadeStart(255, 234, 700, sFadeDone, NULL) < 0) {
      /* No register could be had. Tear it down now, which is what the header says a
         caller must do rather than leaving a widget undrawn. */
      IrcitUiSnackbarDismiss();
    }
    return 1;
  }

  /* Ctrl-O removes the log list's cursor row and offers it back. The whole of the
     notification's action feature, end to end, on the case it exists for.
   *
   * ^O AND NOT ^Z, and the reason is a measured property of the terminal rather than a
   * preference. This library's window-driver contract has the factory run cbreak(), not
   * raw() -- so ICANON and ECHO go and ISIG and IXON STAY, and the line discipline keeps
   * ^C, ^\, ^Z as the interrupt, quit and suspend characters and ^S/^Q as flow control.
   * None of those five ever reaches getch(). The design that prompted this feature names
   * Ctrl-Z for exactly this action; Ctrl-Z cannot be typed at it. Measured, 28 of the 31
   * C0 codes swept, in dev/probes/snackbar/p_hotkeys.c -- and the same sweep shows
   * the engine's own IRCITUI_KEY_CTRL_C (copy) is unreachable for the same reason.
   *
   * 15 is free as well as deliverable: this switch claims 4, 5, 7, 14, 20 and 25, and
   * neither the engine's router nor its clipboard block names 15.
   *
   * THE SAME KEY DOES BOTH JOBS, and that is the design rather than a collision. While a
   * notification carrying an action is up, the overlay answers Ctrl-O first -- it is
   * offered every key at the top of IrcitUiDispatchKey, before this listener is reached
   * at all -- so the second press undoes the first. Once the box is gone the key comes
   * back here and deletes again. Delete, undo, delete, undo: one key, and which half it
   * is is decided by whether an affordance is on screen, which is exactly what the user
   * is looking at when they press it.
   *
   * Not Ctrl-Y: that one dissolves whatever box is up, and would have taken the
   * affordance away on the press meant to use it. */
  case IRCITUI_DEMO_KEY_DELETE:
    sDeleteCursorRow();
    return 1;

  /* Ctrl-G toggles the notification box's gradient.
   *
   * The register-driven effects are OFF by default and that is deliberate: writing any
   * colour register makes ncurses emit OSC 104 at exit, so the terminal's palette returns
   * to the emulator's defaults and a hand-tuned scheme does not survive the program. An
   * application opts in, and this key is the opt-in -- an application that never presses
   * it never pays the cost.
   *
   * The gradient is a horizontal ramp behind the box, one borrowed register per column.
   * It is borrowed when the box is first painted and handed back when it goes, so the
   * registers are not held while the box is down. */
  case 7: {
    /* Four states on one key, so every effect can be seen against the others:
     *
     *   off            nothing borrows a register, and the palette is untouched
     *   gradient       the notification box's horizontal ramp
     *   canopy         a vertical ramp down the screen, with the text on it blended to
     *                  match -- the `surface` and `text` slots are what opt in
     *   canopy + pulse the same ramp, breathing
     *
     * The mask is cumulative rather than exclusive because CANOPY and PULSE are meant to
     * compose: the pulse animates the canopy, it is not a fourth backdrop. */
    static const char *sLabels[] = {"effects off", "gradient", "canopy", "canopy + pulse"};
    static const unsigned sMasks[] = {
      0u, IRCITUI_PALETTE_EFFECT_GRADIENT, IRCITUI_PALETTE_EFFECT_CANOPY,
      IRCITUI_PALETTE_EFFECT_CANOPY | IRCITUI_PALETTE_EFFECT_PULSE
    };
    static unsigned s_ix;

    unsigned ix = (s_ix++) % (sizeof sMasks / sizeof sMasks[0]);
    IrcitUiPaletteSetEffects(sMasks[ix]);
    /* The canopy arrives with the next frame's Ensure, but the tree has to be told to
       repaint for it: a slot's colours are a PAIR, and a changed pair reaches the glass
       only when the cells holding it are written again. The pulse needs no such thing --
       see the loop's stage 3.6. */
    IrcitUiPaletteRepaint();
    IrcitUiSnackbarShow(sLabels[ix], 1500);
    return 1;
  }

  /* Ctrl-T redefines the palette slots the demo's widgets wear.
   *
   * The property a slot exists for is that redefining it changes every node wearing it
   * on the next paint -- with no tree walk, nothing invalidated, and no node touched.
   * This is the shortest thing that shows it: two slots move, and the note in the left
   * panel (a LAYOUT wearing text_muted through C23) and the watchdog alert's lines (C
   * code wearing it through IrcitUiWidgetDef) both recolour together.
   *
   * Press F2 first, or after: the alert is the half that proves a slot is not a layout
   * feature. The snackbar names the palette, which also shows the two overlays apart.
   *
   * The demo reaches the slot API through the private header, as it reaches the
   * reusable types. The interface is not in ui.h yet -- this is the seam where it gets
   * promoted -- so today the demo is the only consumer that can call it. */
  case 20: {
    static const char *sNames[] = {"dark", "warm-ish", "light", "high contrast"};
    static const IrcitUiPaletteProfile sWarm = {
      "warm-ish",
      {
        {180, 234, COLOR_YELLOW, COLOR_BLACK, A_DIM            },
        {180, 236, COLOR_YELLOW, COLOR_BLUE,  A_NORMAL         },
        {180, 234, COLOR_YELLOW, COLOR_BLACK, A_NORMAL         },
        {180, 234, COLOR_YELLOW, COLOR_BLACK, A_DIM            },
        {208, 234, COLOR_RED,    COLOR_BLACK, A_BOLD           },
        {214, 234, COLOR_GREEN,  COLOR_BLACK, A_UNDERLINE      },
        {255, 236, COLOR_WHITE,  COLOR_BLACK, A_REVERSE        },
        {214, 236, COLOR_YELLOW, COLOR_BLACK, A_REVERSE | A_BOLD},
      }
    };
    static const IrcitUiPaletteProfile sHighContrast = {
      "high contrast",
      {
        {255, 232, COLOR_WHITE, COLOR_BLACK, A_NORMAL         },
        {255, 232, COLOR_WHITE, COLOR_BLUE,  A_NORMAL         },
        {255, 232, COLOR_WHITE, COLOR_BLACK, A_NORMAL         },
        {255, 232, COLOR_WHITE, COLOR_BLACK, A_NORMAL         },
        {196,  16, COLOR_RED,   COLOR_BLACK, A_BOLD           },
        {51,   232, COLOR_GREEN, COLOR_BLACK, A_BOLD           },
        {255, 236, COLOR_WHITE, COLOR_BLACK, A_REVERSE        },
        {51,   236, COLOR_YELLOW, COLOR_BLACK, A_REVERSE | A_BOLD},
      }
    };
    static unsigned s_ix;

    /* Two of the four are the library's ("dark" is the default and "light" is its
       inverse); two are the application's own, which is what a profile being plain data
       is for -- an application states a theme in the same shape the library does. */
    const IrcitUiPaletteProfile *profiles[] = {
      IrcitUiPaletteProfileByName("dark"), &sWarm, IrcitUiPaletteProfileByName("light"), &sHighContrast,
    };
    const unsigned n  = sizeof profiles / sizeof profiles[0];
    const unsigned ix = s_ix++ % n;

    IrcitUiPaletteApplyProfile(profiles[ix]);
    /* The repaint is REQUIRED and is not a formality. A slot resolves to a colour PAIR,
       and a changed pair reaches the glass only when the cells holding it are written
       again -- so without this the note keeps its old colour until something unrelated
       dirties a node. It was found exactly that way: pressing Ctrl-T with the watchdog
       alert up produced no SGR at all, because nothing repainted.

       Stacked popups are children of the root, so this reaches them too -- which is why
       a palette applied here and THEN raised (F2) arrives in the new colours. */
    IrcitUiPaletteRepaint();

    IrcitUiSnackbarShow(sNames[ix], 1500);
    return 1;
  }

  /* SS26's editor has no arm here any more. Both of its gestures are the
     framework's now -- a double left-click on a row, and Ctrl+Enter on the cursor's
     row -- so an application that wants the standard behaviour writes nothing, and
     one that wants its own key binds it against the same public call:
     IrcitUiRecyclerBeginEdit(list, IrcitUiRecyclerCursor(list)). F11 was the
     demo's own choice and is gone because it is a fullscreen toggle in most
     terminals and window managers, so the terminal eats it before the application
     ever sees it. */

  /* Ctrl-E edits the log list's cursor row in place -- the keyboard half of SS26.
   *
   * THE LIBRARY'S GESTURE FOR THIS IS Ctrl+Enter (IRCITUI_KEY_CTRL_ENTER), and
   * this application binds that too by not binding it at all: the recycler opens
   * its cursor row on that key itself. What the demo adds here is a key that
   * arrives on a terminal which cannot send it. Most terminals put a bare CR on
   * the wire for Ctrl+Enter -- byte-identical to Enter, and no code can tell them
   * apart -- so the framework's gesture exists only where the terminal is
   * configured for modifyOtherKeys or CSI-u. Ctrl-E is one byte and arrives
   * everywhere; an application that wants a different substitute binds the same
   * public call. */
  case 5: {
    IrcitUiNode *list = IrcitUiFind(root_ptr, "log_list");
    if (list) IrcitUiRecyclerBeginEdit(list, IrcitUiRecyclerCursor(list));
    return 1;
  }

  /* F5 builds a floating window through the C API rather than from a
     template, which is the other way a layer can arrive. */
  case KEY_F(5): {
    IrcitUiWidgetDef win = {
      .id          = "code_win", .kind = IRCITUI_LINEAR, .text = "", .width_mode = IRCITUI_SIZE_EXACT, .width = 24,
      .height_mode = IRCITUI_SIZE_EXACT, .height = 6,
      .flags       = IRCITUI_F_VISIBLE | IRCITUI_F_ENABLED | IRCITUI_F_BORDER | IRCITUI_F_DRAGGABLE, .fg = 7, .bg = 0,
      .bdr         = 2
    };
    IrcitUiWidgetDef btn = {
      .id = "code_close", .text = "Close", .width_mode = IRCITUI_SIZE_MATCH, .height_mode = IRCITUI_SIZE_WRAP
    };
    IrcitUiNode *w = IrcitUiCreateFloatingWindow(&win);
    if (w) {
      IrcitUiWidgetDef lab = {
        .id          = "code_lab", .kind = IRCITUI_TEXT_VIEW, .text = "from C API", .width_mode = IRCITUI_SIZE_MATCH,
        .height_mode = IRCITUI_SIZE_WRAP, .flags = IRCITUI_F_VISIBLE | IRCITUI_F_ENABLED, .fg = 2, .bg = 0
      };
      IrcitUiAddChild(w, &lab);
      IrcitUiAddButton(w, &btn);
    }
    return 1;
  }

  default:
    break;
  }
  return 0;
}

/* --- the recycler's dataset and binder ------------------------------------
 *
 * The list declared as `log_list` in layout/ircitui.yaml holds ten thousand
 * entries. It is bound to about twenty nodes, because the recycler keeps a pool
 * the height of its viewport and rebinds those rows as it scrolls rather than
 * allocating one node per entry.
 *
 * The dataset is deliberately larger than anything the pool could hold: if the
 * recycler were not virtual this is where it would show, as a slow start and a
 * lot of memory rather than as a wrong picture.
 */
#define DEMO_LOG_ENTRIES 10000

static char sDemoLog[DEMO_LOG_ENTRIES][96];

/* The position strings are pre-rendered for the same reason the entry text is:
 * whatever row is showing one borrows it, and the recycler rebinds rows many
 * times, so a stack buffer would leave the row pointing at nothing. */
static char sDemoIndex[DEMO_LOG_ENTRIES][8];

static void
sSeedDemoLog(void)
{
  int i;
  for (i = 0; i < DEMO_LOG_ENTRIES; i++) {
    snprintf(sDemoLog[i], sizeof sDemoLog[i],
             "connection authorised on port %d, session %06d", 6000 + (i % 40), i);
  }
}

/* Called by the recycler for every visible row on every layout pass, including
 * rows whose position has not changed -- so this must be idempotent, and it is:
 * it writes the entry for whatever position it is handed and reads nothing
 * back. */
static void
sSeedDemoIndex(void)
{
  int i;
  for (i = 0; i < DEMO_LOG_ENTRIES; i++) snprintf(sDemoIndex[i], sizeof sDemoIndex[i], "%04d", i);
}

/* The band labels, one per section, for the same reason the two arrays above
 * exist: the pinned header is rebound every time the pin moves, so its text has
 * to be a string that outlives the node borrowing it. A stack buffer here would
 * leave the pinned header pointing at a dead frame -- which ASan reports as a
 * stack-use-after-return in the painter, a long way from the cause. */
#define DEMO_SECTIONS (DEMO_LOG_ENTRIES / 25 + 1)
static char sDemoSection[DEMO_SECTIONS][40];

/* The LIVE row count, which stops being DEMO_LOG_ENTRIES the first time Ctrl-O takes a
 * row away.
 *
 * The array is fixed at DEMO_LOG_ENTRIES and stays that way -- these are 96-byte rows
 * and not pointers, so the storage cannot shrink. What varies is how much of it is the
 * dataset, and that is this: the adapter's item_count, the bounds in the three
 * callbacks below, and nothing else. Mixing the two is how a recycler ends up asked
 * about a position the application has already forgotten. */
static size_t sDemoLogCount = DEMO_LOG_ENTRIES;

/* The one row Ctrl-O has taken, kept so the notification's action can put it back.
 *
 * ONE, and not a stack, because that is what the overlay offers: it shows a single
 * action, and a second deletion replaces the first -- which is honest, since it is the
 * second deletion that is described on screen. The copy is BY VALUE rather than a saved
 * pointer: sDemoLog's rows are arrays, so the pointer a delete would keep is a pointer
 * into the very storage the shift is about to overwrite. */
static char   sUndoText[sizeof sDemoLog[0]];
static char   sUndoIndex[sizeof sDemoIndex[0]];
static size_t sUndoPosition;
static int    sUndoValid;

/* The adapter, kept rather than rebuilt, because the count in it changes.
 *
 * It is written once by sWireDemoRecycler and re-published by sApplyLogCount, so the
 * six fields are stated in one place. A second literal copy of them in the delete path
 * is how a binder quietly stops being called after a row is removed. */
static IrcitUiRecyclerAdapter sLogAdapter;

/* How many rows are marked, into the footer's status label.
 *
 * The highlight is the selection's real feedback, and it is invisible to anything
 * that cannot see attributes -- a screenshot, a screen-scraper, a test. This is the
 * same fact in characters, which is why it is here rather than left to the reverse
 * video.
 *
 * IT COUNTS THE WORDS, not the indices. Walking ten thousand rows through
 * IrcitUiRecyclerIsSelected is a public call with a bounds check each, and this runs
 * from the binder, so it would be ten thousand calls on every layout pass. The
 * bit-array is right there behind the private header the demo already includes, and
 * a popcount over three hundred words is free by comparison.
 *
 * The count is the POPULATION of bits below item_count. Bits above it are left set
 * by a dataset that shrank -- selected_bits only ever grows -- so counting the whole
 * array would report marks on rows that no longer exist. */
static void
sShowSelectionCount(void)
{
  const IrcitUiRecyclerState *st;
  IrcitUiNode                *status;
  IrcitUiNode                *list_ptr;
  size_t                      i, marked = 0;
  static char                 text[48];

  if (!IrcitUiScreen.root) return;
  /* Both are looked up rather than cached: IrcitUiMainLooper calls
     IrcitUiScreenReset once before it loops, so a pointer taken at wiring time
     would name a tree that had already been thrown away. */
  list_ptr = IrcitUiFind(IrcitUiScreen.root, "log_list");
  status   = IrcitUiFind(IrcitUiScreen.root, "status");
  if (!list_ptr || !status) return;
  st = (const IrcitUiRecyclerState *)list_ptr->common.widget_state;
  if (!st) return;

  for (i = 0; i < st->adapter.item_count && st->selected_bits; i++) {
    if (st->selected_bits[i >> 5] & (1u << (i & 31u))) marked++;
  }
  snprintf(text, sizeof text, "%zu selected", marked);
  IrcitUiNodeSetText(status, text);
}

static void
sSeedDemoSections(void)
{
  int s;
  for (s = 0; s < DEMO_SECTIONS; s++) {
    snprintf(sDemoSection[s], sizeof sDemoSection[s], " entries %05d-%05d ", s * 25, s * 25 + 24);
  }
}

/* --- the view types: two row shapes in one list -----------------------------
 *
 * One entry in twenty-five is a warning, and a warning row is a DIFFERENT NODE
 * TREE -- log_row_warn.yaml carries a marker column log_row.yaml has no node for
 * -- so the list is genuinely heterogeneous rather than one shape recoloured. The
 * recycler frees a slot and re-clones it from the other template when a scroll
 * carries it across the boundary, which is the cost this demo exists to show.
 *
 * The framework calls this once per pooled slot per layout pass, so it is written
 * to be as cheap as the arithmetic it is: no state, no lookup, and nothing that
 * could answer differently for the same position on a second call -- a tag that
 * changed between two calls in one pass would have that slot rebuilt twice. */
#define DEMO_VIEW_INFO    0
#define DEMO_VIEW_WARN    1
/* The divider, and the id sticky_headers names in layout/ircitui.yaml. It is not
 * an index into a list of types -- the engine only ever compares it -- so the
 * value is chosen to read as what it is when it appears in a log line. */
#define DEMO_VIEW_SECTION 10
/* The EDITING shape, and the id IrcitUiRecyclerSetEditType is handed. It is a
 * registry entry like any other: the layout declares it, and nothing there says
 * it is the edit template -- which row takes this shape is a run-time fact. */
#define DEMO_VIEW_EDIT    20

/* One entry in twenty-five opens a section; every seventh is a warning. The
 * section test comes first so a position satisfying both is a divider, which is
 * the priority a reader would expect. */
static int
sDemoRowType(size_t position)
{
  if (position % 25 == 0) return DEMO_VIEW_SECTION;
  if (position % 7 == 3) return DEMO_VIEW_WARN;
  return DEMO_VIEW_INFO;
}

static int
sOnLogRowViewType(size_t position, void *user_dataset)
{
  (void)user_dataset;
  if (position >= sDemoLogCount) return DEMO_VIEW_INFO;
  return sDemoRowType(position);
}

static void
sOnBindLogRow(IrcitUiNode *row_ptr, size_t position, void *user_dataset)
{
  IrcitUiNode *index_label;
  IrcitUiNode *text_label;

  (void)user_dataset;
  if (position >= sDemoLogCount) return;
  /* Refreshed here rather than from a listener: a range calls
     IrcitUiRequestLayout, so a layout pass follows every one of them, and the
     binder is the only app-facing code that runs on that path. Cheap enough to do
     on each call -- see the note on the function. */
  sShowSelectionCount();
  /* SS26: a row open for editing is an EDIT FIELD rather than a display row, and
     the framework calls this for it exactly once -- on the pass that builds the
     field. Every later pass leaves the row alone, because its contents are the
     user's from that point on, so this is the application's only chance to say
     what the field should start out holding.
   *
   * Filled through the field's OWN setter. IrcitUiNodeSetText cannot do it: an
   * edit field's display resolves through the alias the engine points at the
   * field's buffer, and a value written the other way would show until the first
   * keystroke and then be typed over from an empty buffer.
   *
   * The KIND is what tells the two apart, and it is read through the public
   * accessor because that is the whole of what an out-of-tree binder has. */
  if (IrcitUiNodeGetKind(row_ptr) == IRCITUI_EDIT_TEXT) {
    IrcitUiEditTextSetValue(row_ptr, sDemoLog[position]);
    return;
  }
  /* The row's shape is NOT handed to the binder -- it keeps its three-argument
     form -- so the binder asks the same predicate the framework did. That is not
     a loss: the application wrote the predicate, so it already knows the answer,
     and passing it back would have meant changing a published callback's
     signature for a convenience the caller can supply itself. What it does mean
     is that these two must agree, which is why there is one predicate and not a
     second copy of the arithmetic here. */
  if (sDemoRowType(position) == DEMO_VIEW_SECTION) {
    /* A divider. THE SAME BRANCH FILLS THE PINNED HEADER, because the header is a
       second clone of this same template and the framework hands its binder the
       SECTION's position -- so this is reached with the divider's own index both
       for the row in the flow of the list and for the copy pinned above it. */
    if (position / 25 < DEMO_SECTIONS) {
      IrcitUiNode *title = IrcitUiFind(row_ptr, "section_title");
      if (title) IrcitUiNodeSetText(title, sDemoSection[position / 25]);
    }
    return;
  }
  if (sDemoRowType(position) == DEMO_VIEW_WARN) {
    index_label = IrcitUiFind(row_ptr, "warn_index");
    text_label  = IrcitUiFind(row_ptr, "warn_text");
  } else {
    index_label = IrcitUiFind(row_ptr, "row_index");
    text_label  = IrcitUiFind(row_ptr, "row_text_line");
  }
  /* The strings must outlive the node that borrows them: common.text is a
   * borrowed pointer, and the recycler rebinds these rows many times, so a
   * stack buffer or a freed string would leave the row pointing at nothing. */
  /* Through the PUBLIC setter, from inside a layout pass -- which is what a binder
     is. Both properties matter: it proves an out-of-tree application can fill a row
     at all, and it exercises the setter on the one path where a layout request
     would otherwise re-dirty the root the pass is clearing. */
  if (index_label) IrcitUiNodeSetText(index_label, sDemoIndex[position]);
  if (text_label) IrcitUiNodeSetText(text_label, sDemoLog[position]);
}

/* The activation callback: what the user chose, echoed into the command line.
 *
 * The row node handed over is BORROWED -- rows are pooled and rebound -- so this
 * reads the entry out of the demo's own array by POSITION and keeps nothing. That
 * is the whole contract: the framework never reads the dataset, it hands back a
 * position and the application looks up what it owns. */
static void
sOnLogRowClicked(IrcitUiNode *row_ptr, size_t position, void *user_dataset)
{
  (void)row_ptr;
  (void)user_dataset;
  if (position >= sDemoLogCount) return;
  /* Into the focused field, or into the one ided "cmdline" when focus is on the
   * list -- which it is, since clicking a row focuses the recycler. */
  IrcitUiInputAppend(sDemoIndex[position]);
  IrcitUiInputAppend(" ");
  IrcitUiInputAppend(sDemoLog[position]);
  /* The click changed the selection too, and it repaints WITHOUT a layout pass --
     IrcitUiRecyclerSelect invalidates rather than requesting -- so the binder's
     refresh does not run for it. This is the third place a selection can change,
     after the range keys and SPACE, and it is the only one with a post-hoc hook
     the application owns: this runs after the click's own effect is settled. */
  sShowSelectionCount();
}

/* The commit callback: what the user typed, stored where the list reads from.
 *
 * THE STRING IS BORROWED AND DIES WITH THE CALL. It points into the editing row's
 * own text buffer, and the layout pass that follows the commit frees that row --
 * so anything kept past this line would be a pointer into released memory. The
 * demo copies it into its own array, which is the only reason the new value is on
 * screen a frame later: the row reverts to its display shape, the binder fills it
 * from sDemoLog, and that is the round trip the whole feature is.
 *
 * A cancel never reaches here, so there is nothing to distinguish. */
static void
sOnLogRowEdited(size_t position, const char *revised_text, void *user_dataset)
{
  (void)user_dataset;
  if (position >= sDemoLogCount) return;
  snprintf(sDemoLog[position], sizeof sDemoLog[position], "%s", revised_text);
  /* Echoed so the commit is visible as an event and not only as a changed row --
     the same reason the click callback echoes the entry it was handed. */
  IrcitUiInputAppend(sDemoIndex[position]);
  IrcitUiInputAppend(" := ");
  IrcitUiInputAppend(sDemoLog[position]);
}

/* Bind the MODEL node, not the screen tree's.
 *
 * IrcitUiMainLooper calls IrcitUiScreenReset before it loops, and that clones
 * the model afresh -- so anything wired on the screen tree's recycler would be
 * thrown away before the first frame. sAllocWidgetState inherits the adapter
 * from the node a clone came from, so configuring the model is what makes every
 * screen tree the framework builds able to bind. The same rule the demo already
 * follows for its key policy, a few lines below. */
static int
sWireDemoRecycler(void)
{
  IrcitUiNode *model_list = IrcitUiFind(IrcitUiModelRoot(), "log_list");

  if (!model_list) {
    fputs("demo: log_list is not in the reference tree\n", stderr);
    return 0;
  }
  sSeedDemoLog();
  sSeedDemoIndex();
  sSeedDemoSections();
  /* PUBLIC API ONLY. This used to allocate an IrcitUiRecyclerState by hand out of
   * a private header, because there was no other way to reach the adapter -- which
   * meant no application outside this repository could bind a list at all. The
   * setter allocates the state itself and copies the adapter in. */
  sLogAdapter.item_count      = sDemoLogCount;
  sLogAdapter.user_dataset    = sDemoLog;
  /* The discriminator is what turns the registry declared in ircitui.yaml --
     `view_types:` on this recycler -- from a list of templates into a list of
     SHAPES. Without it the framework has no way to know which position is which,
     and would render the registry's first entry everywhere. */
  sLogAdapter.get_item_view_type = sOnLogRowViewType;
  sLogAdapter.on_bind_view    = sOnBindLogRow;
  sLogAdapter.on_item_clicked = sOnLogRowClicked;
  /* SS26. Without it IrcitUiRecyclerBeginEdit refuses: a field whose result
     nobody receives is a user typing into a void. */
  sLogAdapter.on_item_edited  = sOnLogRowEdited;
  /* MULTI, because the framework default is SINGLE and a single-selection list
     has nothing for a range to extend. Set on the MODEL, like the adapter, so
     every screen tree the framework clones from then on inherits it. */
  if (!IrcitUiRecyclerSetAdapter(model_list, &sLogAdapter)) return 0;
  IrcitUiRecyclerSetSelectionMode(model_list, IRCITUI_SELECT_MULTI);
  /* SS26, AFTER the registry: the editing type names an entry in it, so the
     registry is what has to exist first -- the same ordering the sticky header
     requires, and for the same reason. */
  IrcitUiRecyclerSetEditType(model_list, DEMO_VIEW_EDIT);
  return 1;
}

/* Publish the live count to the framework, on the MODEL and on the live tree.
 *
 * BOTH, and that is not belt-and-braces. The model is what the next
 * IrcitUiScreenReset clones, so a count written only to the live node would be lost by
 * the next reset; the live node is what is on screen now, so a count written only to
 * the model would leave ten thousand rows showing after a row had been taken away. The
 * demo deletes at run time, after the screen exists, which is why it needs both and why
 * sWireDemoRecycler's single call is not enough.
 *
 * IrcitUiRecyclerSetAdapter raises the layout request itself, so the rebind is
 * scheduled by the call rather than by anything here. */
static void
sApplyLogCount(void)
{
  IrcitUiNode *model = IrcitUiFind(IrcitUiModelRoot(), "log_list");
  IrcitUiNode *live  = IrcitUiScreen.root ? IrcitUiFind(IrcitUiScreen.root, "log_list") : NULL;

  sLogAdapter.item_count = sDemoLogCount;
  if (model) IrcitUiRecyclerSetAdapter(model, &sLogAdapter);
  if (live) IrcitUiRecyclerSetAdapter(live, &sLogAdapter);
}

/* The notification's action: put the deleted row back.
 *
 * Runs from the overlay's key path, on Thread 0, AFTER the box has been dismissed --
 * which is why it can raise the confirmation below and have it survive. The overlay
 * documents that ordering as the reason a callback may raise a second notification, and
 * this is the case that documents.
 *
 * The shift is the exact inverse of the delete's: the tail moves up to open a hole at
 * the remembered position, and the saved row goes into it. Positional, so the row types
 * either side come back exactly as they were -- which matters here in a way it would
 * not for a plain text log, because this list's shape is a function of position. */
static void
sOnUndoDelete(void *user_ptr)
{
  IrcitUiNode *live;

  (void)user_ptr;
  if (!sUndoValid) return;

  memmove(sDemoLog[sUndoPosition + 1], sDemoLog[sUndoPosition], (sDemoLogCount - sUndoPosition) * sizeof sDemoLog[0]);
  memmove(sDemoIndex[sUndoPosition + 1], sDemoIndex[sUndoPosition],
          (sDemoLogCount - sUndoPosition) * sizeof sDemoIndex[0]);
  snprintf(sDemoLog[sUndoPosition], sizeof sDemoLog[0], "%s", sUndoText);
  snprintf(sDemoIndex[sUndoPosition], sizeof sDemoIndex[0], "%s", sUndoIndex);
  sDemoLogCount++;
  sUndoValid = 0;

  sApplyLogCount();

  live = IrcitUiScreen.root ? IrcitUiFind(IrcitUiScreen.root, "log_list") : NULL;
  if (live) IrcitUiRecyclerSetCursor(live, sUndoPosition);

  /* Terminated, because the two operations run together on the one input line: without
     a separator "0000 removed0000 restored" is what the footer shows. */
  IrcitUiInputAppend(sUndoIndex);
  IrcitUiInputAppend(" restored. ");
  /* A second notification from inside the first one's callback. It rises from where the
     first is falling rather than from the footer, which is the replacement path the
     overlay already had and this is the first caller to exercise it. */
  IrcitUiSnackbarShow("row restored", IRCITUI_SNACKBAR_LENGTH_SHORT);
}

/* Ctrl-O: take the cursor's row out of the dataset, and offer it back.
 *
 * The deletion is REAL -- the row leaves the array, the count drops, every later row
 * shifts down and the row's SHAPE moves with it, because sDemoRowType is arithmetic
 * over the position. An "undo" over a row that was never removed would demonstrate
 * nothing about an application holding state it may have to put back, which is the
 * whole point of the pattern.
 *
 * The selection is cleared rather than shifted. The marks are keyed by dataset index, so
 * a row leaving re-points every mark after it at its neighbour -- leaving them would
 * make the footer's count describe rows the user never picked.
 *
 * WHAT THE RETURN VALUE IS FOR. If the affordance cannot be raised -- no screen tree, a
 * terminal too small for the box, a hotkey the overlay refuses -- the row goes back
 * immediately. A caller that hid data behind an undo it never managed to show has lost
 * the data; this is the branch that does not. */
static int
sDeleteCursorRow(void)
{
  IrcitUiNode *live = IrcitUiScreen.root ? IrcitUiFind(IrcitUiScreen.root, "log_list") : NULL;
  size_t       idx;
  char         msg[64];

  if (!live || sDemoLogCount == 0) return 0;

  idx = IrcitUiRecyclerCursor(live);
  if (idx >= sDemoLogCount) idx = sDemoLogCount - 1;

  snprintf(sUndoText, sizeof sUndoText, "%s", sDemoLog[idx]);
  snprintf(sUndoIndex, sizeof sUndoIndex, "%s", sDemoIndex[idx]);
  sUndoPosition = idx;
  sUndoValid    = 1;

  memmove(sDemoLog[idx], sDemoLog[idx + 1], (sDemoLogCount - idx - 1) * sizeof sDemoLog[0]);
  memmove(sDemoIndex[idx], sDemoIndex[idx + 1], (sDemoLogCount - idx - 1) * sizeof sDemoIndex[0]);
  sDemoLogCount--;

  IrcitUiRecyclerClearSelection(live);
  sApplyLogCount();
  IrcitUiRecyclerSetCursor(live, idx < sDemoLogCount ? idx : sDemoLogCount - 1);

  IrcitUiInputAppend(sUndoIndex);
  IrcitUiInputAppend(" removed. ");

  snprintf(msg, sizeof msg, "removed %s", sUndoIndex);
  if (!IrcitUiSnackbarShowAction(msg, IRCITUI_SNACKBAR_LENGTH_LONG, "[UNDO]", IRCITUI_DEMO_KEY_DELETE, sOnUndoDelete, NULL)) {
    /* Refused, and the notification that would have carried the affordance is not up.
       Put the row back rather than lose it -- see the note above. */
    sOnUndoDelete(NULL);
  }
  return 1;
}

/* --- The network pipeline, end to end ---------------------------------------
 *
 * The framework's half of this feature is the pipeline; everything below is the
 * application's half, and where the line falls is the design. Bytes arrive on a
 * worker thread, cross to Thread 0 as an opaque chunk with a stream token, and
 * are reassembled and routed HERE. The framework never parsed anything, never
 * framed anything, and never touched a node.
 *
 * ONE CONNECTION, ONE TOKEN. The listener is registered once and demultiplexes
 * on the token -- that is why the token exists at all. This demo has a single
 * stream, so the token only has to be non-NULL and the reassembly state can be
 * file-scope rather than a map keyed by it. A client with several servers keys
 * that state by the token instead; the listener does not change shape.
 *
 * FRAMING IS THE APPLICATION'S, and it is the reason the partial buffer below
 * exists. TCP has no message boundaries: a line can arrive split across any
 * number of chunks, and several lines can arrive inside one. So the durable state
 * is (partial, length) carried ACROSS callbacks -- which is exactly the state a
 * watchdog eviction would tear mid-update, and the reason this listener is the
 * one callback in the tree that runs without one.
 */

#define DEMO_NET_PARTIAL_MAX 4096

typedef struct
{
  int            fd;      /**< Socket, or -1 when there is no connection. */
  volatile int   stop;    /**< Set on Thread 0; read by the worker. */
  pthread_t      thread;
  int            started;
} DemoNetStream;

static DemoNetStream sNet = { .fd = -1 };
static char          sNetPartial[DEMO_NET_PARTIAL_MAX];
static size_t        sNetPartialLen;
static size_t        sNetLines;

/* One complete line, on Thread 0. The demo's backend is sDemoLog, so a received
   line is written into the next slot and the list is asked to re-lay-out; no node
   is touched here, which is the framework's rule for this path.

   The layout request is what makes it appear: a pass re-binds the pool rows, and
   for a homogeneous list the bind gate is unconditionally true, so the binder
   re-reads sDemoLog and the new line is on screen at the next frame. */
static void
sNetOnLine(const char *line_ptr)
{
  if (sNetLines >= DEMO_LOG_ENTRIES) sNetLines = 0; /* wrap: the demo's store is fixed */

  snprintf(sDemoLog[sNetLines], sizeof sDemoLog[sNetLines], "%s", line_ptr);
  sNetLines++;

  if (IrcitUiScreen.root) IrcitUiRequestLayout(IrcitUiScreen.root);
}

/* The listener. Runs on Thread 0, synchronously, with the watchdog unarmed.

   The bytes are BORROWED for the duration of this call -- they point into the
   loop's own envelope slot, which the next message of any kind reuses -- so
   nothing here may retain them. The partial buffer above is what survives, and
   it is a copy. */
static int
sOnNetworkChunk(IrcitUiEventDispatcher *disp_ptr, void *stream_token, const char *bytes_ptr, void *user_ptr)
{
  size_t i;

  (void)disp_ptr; (void)user_ptr;

  /* A chunk with no token is one this listener cannot attribute. Dropping it
     beats guessing which connection it belongs to. */
  if (!stream_token) return 0;

  for (i = 0; bytes_ptr[i] != '\0'; i++) {
    char ch = bytes_ptr[i];

    if (ch == '\n') {
      /* CRLF, and a bare LF both end a line. The CR is framing, not content. */
      while (sNetPartialLen > 0 &&
             (sNetPartial[sNetPartialLen - 1] == '\r' || sNetPartial[sNetPartialLen - 1] == '\n'))
        sNetPartialLen--;

      sNetPartial[sNetPartialLen] = '\0';
      if (sNetPartialLen > 0) sNetOnLine(sNetPartial);
      sNetPartialLen = 0;
      continue;
    }

    /* Full without a terminator: the line is not going to fit, so it is cut
       here rather than grown. A reassembler that grows without bound is a
       memory leak an unauthenticated peer chooses the size of. */
    if (sNetPartialLen >= DEMO_NET_PARTIAL_MAX - 1) {
      sNetPartial[DEMO_NET_PARTIAL_MAX - 1] = '\0';
      sNetOnLine(sNetPartial);
      sNetPartialLen = 0;
    }

    sNetPartial[sNetPartialLen++] = ch;
  }

  return 1;
}

/* The worker. Reads the socket and hands each chunk to the framework, which owns
   the crossing: this thread never touches a node, a window or the tree.

   THE POST WAITS RATHER THAN DROPS. A lost chunk desynchronises the reassembler
   permanently, so a full pipeline must slow the reader down instead of discarding
   -- and slowing the reader is also what pushes back on the sender, because the
   kernel's receive buffer fills and the TCP window closes. */
static void *
sNetWorker(void *arg)
{
  DemoNetStream *st = arg;
  char           buf[PRIV_CONFIG_DEFAULT_CLIPBUF_SZ - 1];

  while (!st->stop) {
    ssize_t n = recv(st->fd, buf, sizeof buf, 0);

    if (n <= 0) {
      if (n < 0 && (errno == EINTR || errno == EAGAIN)) continue;
      break;   /* EOF or a real error: either way the stream is over */
    }

    if (!IrcitUiDispatcherPostNetworkChunk(IrcitUiDispatcherInstance(), st, buf, (size_t)n, 200)) {
      /* The pipeline was full for the whole deadline. That is a lost chunk and
         the reassembler cannot recover its framing from it, so the stream ends
         rather than continuing desynchronised. */
      IRCITUI_LOG_WARN("demo: network chunk refused; ending the stream rather than desynchronising it");
      break;
    }
  }

  return NULL;
}

/* Connect and start the worker. Returns non-zero on success. */
static int
sNetStart(const char *host_port_ptr)
{
  char                host[256];
  const char         *colon_ptr;
  const char         *port_ptr;
  struct addrinfo     hints;
  struct addrinfo    *res_ptr = NULL;
  size_t              host_len;
  int                 rc;

  if (!host_port_ptr) return 0;

  colon_ptr = strrchr(host_port_ptr, ':');
  if (!colon_ptr || colon_ptr == host_port_ptr) {
    fputs("demo: --connect wants host:port\n", stderr);
    return 0;
  }

  host_len = (size_t)(colon_ptr - host_port_ptr);
  if (host_len >= sizeof host) host_len = sizeof host - 1;
  memcpy(host, host_port_ptr, host_len);
  host[host_len] = '\0';
  port_ptr       = colon_ptr + 1;

  memset(&hints, 0, sizeof hints);
  hints.ai_family   = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  rc = getaddrinfo(host, port_ptr, &hints, &res_ptr);
  if (rc != 0) {
    fprintf(stderr, "demo: %s:%s: %s\n", host, port_ptr, gai_strerror(rc));
    return 0;
  }

  sNet.fd = socket(res_ptr->ai_family, res_ptr->ai_socktype, res_ptr->ai_protocol);
  if (sNet.fd < 0 || connect(sNet.fd, res_ptr->ai_addr, res_ptr->ai_addrlen) != 0) {
    fprintf(stderr, "demo: could not connect to %s:%s\n", host, port_ptr);
    if (sNet.fd >= 0) { close(sNet.fd); sNet.fd = -1; }
    freeaddrinfo(res_ptr);
    return 0;
  }
  freeaddrinfo(res_ptr);

  sNet.stop = 0;
  if (pthread_create(&sNet.thread, NULL, sNetWorker, &sNet) != 0) {
    fputs("demo: could not start the network worker\n", stderr);
    close(sNet.fd);
    sNet.fd = -1;
    return 0;
  }
  sNet.started = 1;

  fprintf(stderr, "demo: streaming from %s\n", host_port_ptr);
  return 1;
}

/* Stop and join the worker, then drop the registration.

   THE ORDER IS THE CONTRACT, not tidiness. The framework allocates the pipeline
   and the application decides when it dies, so the producer has to be gone
   BEFORE the pipeline is: a worker still posting would be inside the ring when
   IrcitUiDestroyFrameworkContext frees it. Clearing the listener first is the
   other half -- a callback reached with a token whose storage is already
   released is a use-after-free the framework cannot detect. */
static void
sNetStop(void)
{
  if (sNet.fd >= 0) {
    /* shutdown(), not close(): closing a descriptor another thread is blocked in
       recv() on is undefined, and it does not reliably wake that thread. */
    shutdown(sNet.fd, SHUT_RDWR);
  }

  if (sNet.started) {
    sNet.stop = 1;
    pthread_join(sNet.thread, NULL);
    sNet.started = 0;
  }

  if (sNet.fd >= 0) { close(sNet.fd); sNet.fd = -1; }

  IrcitUiDispatcherRegisterNetworkListener(IrcitUiDispatcherInstance(), NULL, NULL);
}

int
main(int argc, char **argv_ptr)
{
  DemoOptions            opts;
  int                    rc;
  /* Declared here rather than at its first use, and initialised. The curl
     failure below jumps to framework_shutdown over that point, and the shutdown
     block hands this to IrcitUiDestroyFrameworkContext: a declaration that has
     been jumped over holds an indeterminate value, and NULL is the honest one
     for "no context was ever created". */
  IrcitUiContext        *ircitui_context = NULL;

  // Locale must be set before the verifier runs: it measures display columns, so with the C locale active it would fall back to counting UTF-8 bytes.
  setlocale(LC_ALL, "");

  rc = sParseArgs(argc, argv_ptr, &opts);
  if (rc == DEMO_ARGS_HELP) return 0;
  if (rc == DEMO_ARGS_BAD) return 2;

  /* The async bodies run on worker threads, so they cannot reach a local of
     main. This is the one option they need. */
  sFetchUrl = opts.fetch_url;

  if (IrcitUiLogInit(opts.log_level, opts.log_file, opts.retention) != 0)
    fputs("demo: logging is off -- unrecognised --log-level, or the log file could not be opened\n", stderr);

  if (opts.verify_layout) {
    /* No backend, because --verify-layout is headless by definition -- no terminal. */
    IrcitUiInitialiseFrameworkContext(IrcitUiDataBind, NULL);
    rc              = IrcitUiVerifyLayout(opts.cols, opts.rows);
    IrcitUiLogFini();
    return rc;
  }

  /* The framework is fed here: the binder hands over the display model to drive
     and the listeners that bind this application's domain to it. IrcitUiDataBind
     is the model this library generates, which is the whole of what the demo
     supplies. The binder is invoked inside this call, so the reference model is
     in place before anything below resolves an id against it, and the explicit
     IrcitUiDataBind() that used to follow is gone - the binder is that call. */
  IrcitUiWindowRegistry *window_registry = IrcitUiWindowRegistryCreate((IrcitUiDriverRegistryUserCallbacks){
    .on_driver_initialised = sOnDriverInitialised,
  });
  if (!window_registry || !IrcitUiWindowRegistryRequestDriver(window_registry, opts.window_driver)) {
    fprintf(stderr, "%s: no window backend could be locked for '%s'\n", argv_ptr[0], opts.window_driver);
    IrcitUiLogFini();
    return 1;
  }

  /* libcurl must be initialised once, from one thread, before any worker uses
     it. Its own implicit initialisation is documented as not thread-safe, and
     every transfer in this demo starts on a worker - so the first Ctrl-D was
     relying on a race. */
  if (curl_global_init(CURL_GLOBAL_DEFAULT) != 0) {
    fputs("demo: libcurl could not be initialised\n", stderr);
    goto framework_shutdown;
  }

  ircitui_context = IrcitUiInitialiseFrameworkContext(IrcitUiDataBind, window_registry);
  sLogSchedulerStartup();

  /* From here the loop drives whatever the registry locked, and names no
     backend. The driver comes back through the context rather than from the
     registry directly, because the context is what the framework handed to the
     application and it is where the driver lives for the process's life. */
  IrcitUiBindingReset();
  if (!IrcitUiBindClick("menu_tools", s_tools_denied ? sOnToolsDenied : sOnToolsAllowed, NULL))
    fputs("demo: menu_tools is not in the reference tree\n", stderr);

  /* The application's key policy, installed on the REFERENCE MODEL's root.
     IrcitUiClone copies interactions_user wholesale, so every screen tree the
     framework clones from here on carries it -- including the one
     IrcitUiMainLooper installs at startup. On the screen tree's root it would
     work until the first reset and then silently stop. */
  /* Configured on the model, before IrcitUiMainLooper resets the screen tree
     from it -- see sWireDemoRecycler. */
  if (!sWireDemoRecycler()) fputs("demo: the log list will render empty\n", stderr);

  /* The predictive dictionary, on the command line.
   *
   * THE MODEL NODE, NOT THE SCREEN TREE'S, and that is forced by the loop rather than chosen:
   * IrcitUiMainLooper calls IrcitUiScreenReset before its first frame, and that reinstalls the
   * screen tree by cloning the model -- so an attachment made on a live screen node is
   * replaced before the user can type a character. A clone inherits its dictionary from the
   * node it was cloned from (sAllocWidgetState, the same route the recycler's adapter takes),
   * so the model is where an attachment has to live to outlive a reset. The same rule
   * sWireDemoRecycler follows, and for the same reason.
   *
   * Measured, not reasoned: the first version of this attached to IrcitUiScreen.root's
   * cmdline and the field predicted nothing at all, with no error and nothing on the glass to
   * say why. */
  sPredictBuildSources();
  {
    IrcitUiNode *model_cmdline = IrcitUiFind(IrcitUiModelRoot(), "cmdline");
    if (!model_cmdline || !sPredictDict) fputs("demo: the command line will not predict\n", stderr);
    else IrcitUiEditTextSetDictionary(model_cmdline, sPredictDict);
  }

  IrcitUiModelRoot()->interactions_user.lifecycle_listeners.on_key_down = sOnAppKey;

  /* The network pipeline, wired in the order the contract requires: the
     listener first, the producer second, and both BEFORE the loop. The listener
     runs on Thread 0 from inside the drain, so installing one while the loop is
     already running would be a race rather than a bootstrap. */
  IrcitUiDispatcherRegisterNetworkListener(IrcitUiDispatcherInstance(), sOnNetworkChunk, NULL);
  if (opts.connect_to) (void)sNetStart(opts.connect_to);

  // And the loop is the framework's from here. Measuring, realising, drawing, flushing, the pacing, the blink tick and the resize are all
  IrcitUiMainLooper(ircitui_context);

  framework_shutdown:
  /* Tell the bars coroutine to give up at its next stage, before anything is torn down.
   *
   * This is NOT a complete join and does not pretend to be. The gallery shares the
   * framework's worker pool with the two fetch demos, and that pool is what
   * IrcitUiDestroyFrameworkContext stops and joins on the next line -- so a stage already
   * in flight finishes and posts before the pipeline goes, which is the same exposure the
   * fetches have and is bounded by the job's own resume guard rather than by anything
   * here. What this flag buys is the common case: quitting while the gallery is open now
   * stops the run at its next stage instead of letting it post twenty more reports into a
   * pipeline that is being taken down. Raising the ceiling on that would mean a join
   * primitive the pool does not expose, which is a framework gap rather than this file's
   * to invent. */
  s_bars_stop = 1;

  /* Producers first, then the pipeline: sNetStop joins the worker and clears the
     listener, and only then is the framework's own memory released. See its
     comment -- the order is the contract, not tidiness. */
  sNetStop();

  /* The application's teardown, and the reason IrcitUiMainLooper no longer does
     it: the framework's own worker pool is stopped and joined inside this call,
     so nothing is still posting into the pipeline when the pipeline itself is
     released. Ordering matters and this preserves what already happened -- the
     loop used to destroy the dispatcher at its own tail, i.e. before the deflate
     below -- so nothing that follows sees a different framework state than it
     did before.

     On the one path that reaches this label before a context exists, this is a
     guarded no-op. */
  IrcitUiDestroyFrameworkContext(ircitui_context);

  curl_global_cleanup();
  IrcitUiDeflate(IrcitUiScreen.root);
  IrcitUiWindowRegistryDestroy(window_registry);

  // This application ran initscr() and turned mouse reporting on in sOnDriverInitialised, so it runs the disable escapes and endwin() here
  fputs("\033[?1006l\033[?1003l\033[?1002l\033[?1000l", stdout);
  fflush(stdout);

  /* After the last call that can log and before endwin(), so a line emitted during teardown is still written rather than discarded by the close. */
  IrcitUiLogFini();
  endwin();

  return 0;
}
