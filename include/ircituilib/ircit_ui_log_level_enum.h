/**
 * @file ircit_ui_log_level_enum.h
 * @brief The severity a log line is emitted at.
 */
#ifndef IRCITUI_IRCIT_UI_LOG_LEVEL_ENUM_H
#define IRCITUI_IRCIT_UI_LOG_LEVEL_ENUM_H

/*!
 * @enum IrcitUiLogLevel
 * @brief The severity of one log line, for @ref IrcitUiLogEmit.
 *
 * @details This is the level a *line* is written at, and it is a different thing
 *          from @ref IrcitUiLogRetention, which says what becomes of the file.
 *          One enumerator per @c IRCITUI_LOG_* macro, so the macro and the
 *          function it expands to cannot disagree about what was asked for.
 *
 *          It stops at ERROR because it mirrors the macros: the levels above it
 *          are threshold names a caller passes to @ref IrcitUiLogInit, not
 *          severities the library writes a line at. @c fatal is accepted there
 *          and has no member here, which is the asymmetry and is deliberate: a
 *          library that aborted on a fatal line would take the application's
 *          screen with it, so it has no fatal line to write.
 *
 *          The values are ordered by severity, and DEBUG is the zero value, so a
 *          state that was never configured logs the least: a level that has to
 *          be chosen before anything is recorded is the safe way round.
 */
typedef enum {
    IRCITUI_LOG_LEVEL_DEBUG = 0, ///< Development detail, off unless the rule asked for it.
    IRCITUI_LOG_LEVEL_INFO,      ///< Ordinary activity, and the default rule level.
    IRCITUI_LOG_LEVEL_NOTICE,    ///< Unusual but not wrong.
    IRCITUI_LOG_LEVEL_WARN,      ///< Something was refused or fell back.
    IRCITUI_LOG_LEVEL_ERROR      ///< The call failed.
} IrcitUiLogLevel;

#endif /* IRCITUI_IRCIT_UI_LOG_LEVEL_ENUM_H */
