/**
 * @file ui_log.c
 * @brief Opens and closes the library's log stream.
 *
 * The whole of this file exists so that src/ never includes <zlog.h> and an
 * application never includes it either: the caller states a level and a file,
 * and the zlog configuration that satisfies them is assembled here.
 */
#include "ui_priv.h"
#include "ui_log.h"
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <version.h>

zlog_category_t *IrcitUiLogCategory = NULL;
int              IrcitUiLogEnabled  = 0;

/* The category name that appears in the log line and in the rule below. Rule
   matching is by category prefix, so "ircitui" covers "ircitui" exactly. */
#define LOG_CATEGORY_NAME "ircitui"

/* Applied when the caller passes NULL or an empty string. */
#define LOG_LEVEL_DEFAULT "info"
#define LOG_FILE_DEFAULT  "ircituilib.log"

/* The level that means "do not log at all".
 *
 * It is spelled out here rather than added to sLevels below because it is not a
 * level: it selects no zlog rule and opens nothing. It is the one value that
 * succeeds without producing a stream, which is what lets a caller ask for
 * silence explicitly instead of relying on the level failing. */
#define LOG_LEVEL_NONE "nolog"

/* Room for the configuration text plus the longest path this will accept. */
#define LOG_CONFIG_CAP 4096
#define LOG_PATH_CAP   1024

/* Accepted --log-level values, and the zlog rule level each selects.
 *
 * A zlog rule matches a level and everything ABOVE it (rule.c: `event->level >=
 * a_rule->level`), so "info" means INFO, NOTICE, WARN, ERROR and FATAL, while
 * "debug" means every level there is. That is the behaviour a reader expects of
 * a level switch, and it is the reason the names are spelled out here rather
 * than passed to zlog untouched. */
static const struct
{
  const char *name;
  const char *rule;
} sLevels[] = {
  { "debug",  "DEBUG"  },
  { "info",   "INFO"   },
  { "notice", "NOTICE" },
  { "warn",   "WARN"   },
  { "error",  "ERROR"  },
  { "fatal",  "FATAL"  },
};

/* The suffix an archived log takes, between the file name and its extension
   position: ircituilib.log.20260915-093015. */
#define LOG_ARCHIVE_STAMP "%Y%m%d-%H%M%S"

/* Room for the path plus "." plus the stamp above. */
#define LOG_ARCHIVE_CAP (LOG_PATH_CAP + 32)

/* Carry out the caller's retention choice on @p file_ptr, before the logger is
   handed the path.
 *
 * The logger appends unconditionally, so nothing here can be asked of it: an
 * overwrite is a truncation performed first, and an archive is a rename
 * performed first. Both leave a file at @p file_ptr for the logger to open, or
 * no file at all when there was none.
 *
 * Returns 0 on success, -1 if the mode could not be carried out. A failure is
 * reported rather than absorbed: silently appending when the caller asked for a
 * clean file is a worse outcome than not logging.
 */
static int
sApplyRetention(const char *file_ptr, IrcitUiLogRetention retention)
{
  char        archive[LOG_ARCHIVE_CAP];
  struct tm * tm_ptr;
  time_t      now;
  int         written;

  /* A switch rather than an if-chain so that a value outside the enumeration —
     reachable by a cast from an int — is refused instead of falling through to
     whichever branch happens to be last. */
  switch (retention) {
  case IRCITUI_LOG_APPEND:
    return 0;

  case IRCITUI_LOG_OVERWRITE: {
    /* Opened and closed rather than truncate(2), because fopen("w") also
       CREATES the file. A mode whose whole job is to leave nothing behind must
       still leave an empty file rather than none, or the logger's own open
       would be the thing that creates it and the mode would be untestable. */
    FILE *f = fopen(file_ptr, "w");
    if (!f) return -1;
    return fclose(f) == 0 ? 0 : -1;
  }

  case IRCITUI_LOG_ARCHIVE:
    break;

  default:
    return -1;
  }

  // localtime() is not thread-safe, but this runs once, at open, before any thread the application has started can be logging.
  now = time(NULL);
  tm_ptr = localtime(&now);
  if (!tm_ptr) return -1;

  written = snprintf(archive, sizeof archive, "%s.", file_ptr);
  if (written < 0 || (size_t)written >= sizeof archive) return -1;
  if (strftime(archive + written, sizeof archive - (size_t)written,
               LOG_ARCHIVE_STAMP, tm_ptr) == 0) return -1;

  if (rename(file_ptr, archive) == 0) return 0;
  /* Nothing at the path to move aside. The first run of an archiving
     application is not a failure, and neither is a path the caller has never
     written to. */
  return errno == ENOENT ? 0 : -1;
}

PUBLIC_API int
IrcitUiLogInit(const char *level_ptr, const char *file_ptr, IrcitUiLogRetention retention)
{
  char                       cfg[LOG_CONFIG_CAP];
  const char *               want_level;
  const char *               want_file;
  const char *               rule_level = NULL;
  size_t                     i;
  int                        written;

  if (IrcitUiLogCategory) return 0;   /* already open; re-opening is a no-op */

  /* Checked before the level table, because it is not a level. Succeeding
     without opening anything is the whole of it: no configuration is built, no
     rule is parsed, and the category stays NULL, which leaves the library in
     exactly the state an application that never called this function is in.
     Reported as success rather than as a refusal, so a caller that asked for
     silence is not told something went wrong. */
  if (level_ptr && strcmp(level_ptr, LOG_LEVEL_NONE) == 0) return 0;

  want_level = (level_ptr && level_ptr[0]) ? level_ptr : LOG_LEVEL_DEFAULT;
  want_file  = (file_ptr  && file_ptr[0])  ? file_ptr  : LOG_FILE_DEFAULT;

  for (i = 0; i < sizeof sLevels / sizeof sLevels[0]; i++)
    if (strcmp(want_level, sLevels[i].name) == 0) rule_level = sLevels[i].rule;
  if (!rule_level) return -1;

  /* The path is interpolated into the configuration as a quoted string, so a
     quote in it would end the string early and corrupt every rule after it.
     Refusing is better than writing a configuration that parses into something
     other than what was asked for. */
  if (strlen(want_file) >= LOG_PATH_CAP) return -1;
  if (strchr(want_file, '"') || strchr(want_file, '\n')) return -1;

  /* Last, after both refusals above: a mistyped level or a path that cannot be
     represented must not destroy or move aside a log the caller still wanted.
     Applied once, here, because this is the last moment before the logger opens
     the file and the first at which the path is known to be usable. */
  if (sApplyRetention(want_file, retention) != 0) return -1;

  /* strict init = false: a rule naming a file this process cannot open must not
     abort the program. A chart-topping TUI that refuses to start because its
     log directory is read-only would be a worse outcome than a silent log. */
  written = snprintf(cfg, sizeof cfg,
    "[global]\n"
    "strict init = false\n"
    "buffer min = 1024\n"
    "buffer max = 1MB\n"
    "rotate lock file = self\n"
    "file perms = 600\n"
    "default format = \"%%d(%%F %%T.%%l) %%-6V [%%c] %%m%%n\"\n"
    "\n"
    "[rules]\n"
    LOG_CATEGORY_NAME ".%s\t\"%s\"\n",
    rule_level, want_file);

  if (written < 0 || (size_t)written >= sizeof cfg) return -1;
  if (zlog_init_from_string(cfg) != 0) return -1;

  IrcitUiLogCategory = zlog_get_category(LOG_CATEGORY_NAME);
  if (!IrcitUiLogCategory) {
    zlog_fini();
    return -1;
  }

  /* After the category, never before it: the flag is the macros' fast path in
     front of IrcitUiLogEmit, and a flag that led the pointer it stands for would
     let the banner below reach a function with nothing to write through. The two
     are cleared in the opposite order in IrcitUiLogFini, for the same reason. */
  IrcitUiLogEnabled = 1;

  /* The preamble every log opens with.
   *
   * Emitted here rather than by each application, because this is the only
   * place that knows the EFFECTIVE level and path: the caller may have passed
   * NULL for either and had the defaults above stand in, so an application
   * writing its own banner would need a second copy of those defaults and the
   * two would drift. It also means the banner cannot be forgotten by a caller.
   *
   * It reports what was asked for, not what zlog filter-matches at runtime: the
   * level is a rule threshold, and which lines survive it is zlog's business. */
  IRCITUI_LOG_INFO("===== %s -- log opened =====", IrcitUiLibVersion());
  IRCITUI_LOG_INFO("  level : %s  (this level and above)", want_level);
  IRCITUI_LOG_INFO("  file  : %s", want_file);
  IRCITUI_LOG_INFO("  sink  : file, appended, no rotation; perms 600");
  return 0;
}

PUBLIC_API void
IrcitUiLogFini(void)
{
  if (!IrcitUiLogCategory) return;
  /* The flag first, then the category, so there is no instant in which the fast
     path lets a line through to a category that is gone. Set in the opposite
     order in IrcitUiLogInit for the same reason. */
  IrcitUiLogEnabled = 0;
  // Cleared before zlog_fini so that a log line emitted from a listener during teardown cannot reach a category that is being torn down.
  IrcitUiLogCategory = NULL;
  zlog_fini();
}

/* The zlog level an IrcitUiLogLevel is recorded as.
 *
 * A switch rather than an array indexed by the enumerator so that a value cast in
 * from an int is not read out of bounds. It has no `default:` and falls through
 * to the return below instead, because every enumerator is covered above and the
 * compiler should say so if one is added and forgotten here. */
static int
sZlogLevel(IrcitUiLogLevel level)
{
  switch (level) {
  case IRCITUI_LOG_LEVEL_DEBUG:  return ZLOG_LEVEL_DEBUG;
  case IRCITUI_LOG_LEVEL_INFO:   return ZLOG_LEVEL_INFO;
  case IRCITUI_LOG_LEVEL_NOTICE: return ZLOG_LEVEL_NOTICE;
  case IRCITUI_LOG_LEVEL_WARN:   return ZLOG_LEVEL_WARN;
  case IRCITUI_LOG_LEVEL_ERROR:  return ZLOG_LEVEL_ERROR;
  }

  /* Reachable only through a cast. Recorded at ERROR rather than dropped: a line
     someone asked for at an unknown level is still a line someone asked for, and
     the most severe level is the one that survives the widest range of rules. */
  return ZLOG_LEVEL_ERROR;
}

PUBLIC_API void
IrcitUiLogEmit(IrcitUiLogLevel level, const char *file_ptr, int line, const char *func_ptr, const char *fmt_ptr, ...)
{
  va_list args;

  /* Guarded on the category and not on IrcitUiLogEnabled, because the category is
     what the call below dereferences. The flag the macros test is a fast path in
     front of this, not a second source of truth. */
  if (!IrcitUiLogCategory || !fmt_ptr) return;

  /* The location is the caller's, passed in rather than derived, because zlog
     records whatever this frame hands it. A NULL one is empty rather than fatal:
     the macros always supply both, and a caller reaching this directly should not
     be able to crash the process over a log line. */
  if (!file_ptr) file_ptr = "";
  if (!func_ptr) func_ptr = "";

  va_start(args, fmt_ptr);
  vzlog(IrcitUiLogCategory, file_ptr, strlen(file_ptr), func_ptr, strlen(func_ptr), line, sZlogLevel(level), fmt_ptr, args);
  va_end(args);
}
