/**
 * @file ircit_ui_theme_rule_type.h
 * @brief One selector plus the style it applies.
 */
#ifndef IRCITUI_IRCIT_UI_THEME_RULE_TYPE_H
#define IRCITUI_IRCIT_UI_THEME_RULE_TYPE_H

#include "ircituilib/ircit_ui_kind_enum.h"
#include "ircituilib/ircit_ui_style_override_type.h"
#include "ircituilib/ircit_ui_theme_selector_enum.h"

/*!
 * @brief One selector plus the style it applies.
 *
 * A rule is only as complete as its selector needs: @ref IRCITUI_THEME_ALL
 * reads neither @ref kind nor @ref value, and @ref IRCITUI_THEME_KIND reads
 * only @ref kind. The members a selector does not read may be left zero, and
 * the same holds member-wise for @ref style.
 */
typedef struct {
    IrcitUiThemeSelector selector;  ///< What the rule matches on.
    const char          *value;     ///< Id or class to match; NULL when unused.
    IrcitUiKind          kind;      ///< Kind to match, for @ref IRCITUI_THEME_KIND.
    IrcitUiStyleOverride style;     ///< Colours to apply to a match.
} IrcitUiThemeRule;

#endif /* IRCITUI_IRCIT_UI_THEME_RULE_TYPE_H */
