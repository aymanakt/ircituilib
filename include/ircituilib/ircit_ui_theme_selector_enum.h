/**
 * @file ircit_ui_theme_selector_enum.h
 * @brief What a theme rule matches on.
 */
#ifndef IRCITUI_IRCIT_UI_THEME_SELECTOR_ENUM_H
#define IRCITUI_IRCIT_UI_THEME_SELECTOR_ENUM_H

/*!
 * @enum IrcitUiThemeSelector
 * @brief Selector stored in @ref IrcitUiThemeRule::selector.
 *
 * Every selector but @ref IRCITUI_THEME_ALL reads its comparison value from a
 * different member of the rule: @ref IRCITUI_THEME_KIND from
 * @ref IrcitUiThemeRule::kind, @ref IRCITUI_THEME_ID and
 * @ref IRCITUI_THEME_CLASS from @ref IrcitUiThemeRule::value.
 */
typedef enum {
    IRCITUI_THEME_ALL = 0,  ///< Matches every node; the value members are ignored.
    IRCITUI_THEME_KIND,     ///< Matches @ref IrcitUiNode::kind against the rule's kind.
    IRCITUI_THEME_ID,       ///< Matches @ref IrcitUiNode::id against the rule's value.
    IRCITUI_THEME_CLASS     ///< Matches the node's theme class against the rule's value.
} IrcitUiThemeSelector;

#endif /* IRCITUI_IRCIT_UI_THEME_SELECTOR_ENUM_H */
