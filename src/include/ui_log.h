/**
 * @file ui_log.h
 * @brief The library's log category, private to the library.
 *
 * The macros that write through it used to be defined here. They are public now
 * -- ircituilib/ui.h, beside IrcitUiLogInit -- because an application that opens
 * a log has to be able to write to it, and a header that defines a macro a
 * consumer may not include declares nothing. So this header includes the public
 * one, and including this header is still all a translation unit needs.
 *
 * What stays private is the thing the macros expand to: the category pointer.
 * Nothing else in src/ includes <zlog.h>, so closing the log is a matter of
 * leaving @ref IrcitUiLogCategory NULL, and reading everything the library logs
 * is a matter of looking at one file.
 *
 * Two values say whether logging is open, and both are written together at the
 * two transition points in ui_log.c: the category above, which is what a line is
 * actually written through, and @ref IrcitUiLogEnabled in the public header,
 * which is the fast path the macros test before calling @ref IrcitUiLogEmit. The
 * category is set before the flag on open, and the flag is cleared before the
 * category on close, so the flag never leads the pointer it stands for.
 */
#ifndef IRCITUI_UI_LOG_H
#define IRCITUI_UI_LOG_H

#include <uflib/zlog/zlog.h>
#include "ircituilib/ui.h"

/*!
 * @brief The category every log line in this library is written through.
 *
 * NULL until @ref IrcitUiLogInit succeeds, and NULL again after
 * @ref IrcitUiLogFini. It is deliberately a plain variable rather than a
 * function call: the log macros are on hot paths - a node clone, a click - and
 * a call through a function pointer would be paid on every one of them even
 * when logging is closed.
 */
extern zlog_category_t *IrcitUiLogCategory;

#endif /* IRCITUI_UI_LOG_H */
