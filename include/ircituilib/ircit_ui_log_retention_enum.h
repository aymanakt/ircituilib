/**
 * @file ircit_ui_log_retention_enum.h
 * @brief What to do with an existing log file when the stream is opened.
 */
#ifndef IRCITUI_IRCIT_UI_LOG_RETENTION_ENUM_H
#define IRCITUI_IRCIT_UI_LOG_RETENTION_ENUM_H

/*!
 * @enum IrcitUiLogRetention
 * @brief What becomes of the log file named to @ref IrcitUiLogInit.
 *
 * The underlying logger appends, always: every file it opens carries
 * @c O_APPEND and it exposes no switch to the contrary. So these modes are
 * applied to the file by this library, before the logger is handed the path —
 * which is also why they are expressed here rather than as configuration text.
 *
 * They answer one question, asked once per open: what happens to whatever is
 * already at that path?
 */
typedef enum {
    /*!
     * @brief Keep the file and add to it.
     *
     * The default, and the only mode that preserves the history of previous
     * runs in one place. Nothing is done to the file: it is created if absent
     * and appended to if present, so an application started repeatedly
     * accumulates every session in a single file that grows without bound.
     */
    IRCITUI_LOG_APPEND = 0,

    /*!
     * @brief Empty the file before writing to it.
     *
     * The file keeps its name and loses its contents, so a run starts from a
     * clean log and no history survives it. The file is created if absent.
     * Truncation happens once, at open; nothing is emptied again while the
     * stream is open.
     */
    IRCITUI_LOG_OVERWRITE = 1,

    /*!
     * @brief Move the file aside, then start a new one under the same name.
     *
     * The existing file is renamed to @c &lt;file&gt;.&lt;YYYYmmdd-HHMMSS&gt; and a
     * fresh file is opened at the original path. The archived name carries the
     * time of the *rename*, not of the original file's creation, and the
     * original extension position is kept — @c app.log becomes
     * @c app.log.20260915-093015.
     *
     * Nothing is deleted: each open adds one archive, so the directory grows by
     * one file per run. Rotation and pruning of old archives are left to
     * whatever already manages the log directory.
     *
     * If the archive name already exists — two opens within the same second —
     * the rename still succeeds on POSIX and silently replaces it. That is the
     * one case where an archive can be lost.
     */
    IRCITUI_LOG_ARCHIVE = 2
} IrcitUiLogRetention;

#endif /* IRCITUI_IRCIT_UI_LOG_RETENTION_ENUM_H */
