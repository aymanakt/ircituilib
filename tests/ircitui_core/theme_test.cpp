#include "gtest/gtest.h"

#include "ircituilib/ui.h"
#include "ircituilib/ircitui_context.h"

// Port of tests/test_theme.c.
//
// ASSERT_NE on the theme handle is deliberate: IrcitUiThemeSetBase() on a NULL
// handle is a segfault, not a failed assertion, so the test must stop there.
TEST(IrcituiCoreTheme, BaseRuleAndCurrentTheme) {
    // The framework is fed before anything reads the model: the library resolves ids
    // against the context, so a test that skipped this would be driving an uninitialised
    // framework. IrcitUiDataBind supplies this library's own generated tree.
        // No window backend: these tests exercise the model and the theme, not a
    // driver. The context retains the registry and does not own it, so NULL is
    // the honest answer here and IrcitUiContextWindowDriver reports it back.
IrcitUiInitialiseFrameworkContext(IrcitUiDataBind, NULL);

    IrcitUiTheme *t = IrcitUiThemeCreate();
    ASSERT_NE(t, nullptr);

    // C++ aggregate initialisation; the original used a C99 compound literal.
    EXPECT_TRUE(IrcitUiThemeSetBase(t, IrcitUiStyleOverride{1, 1, 1, 2, 3, 4}));

    IrcitUiThemeRule r = {IRCITUI_THEME_ID, "main", IRCITUI_LINEAR, {1, 0, 0, 7, 0, 0}};
    EXPECT_TRUE(IrcitUiThemeAddRule(t, r));

    IrcitUiNode *n = IrcitUiFind(IrcitUiModelRoot(), "main");
    EXPECT_NE(n, nullptr);

    EXPECT_TRUE(IrcitUiThemeApply(t, IrcitUiModelRoot()));

    IrcitUiThemeSetCurrent(t);
    EXPECT_EQ(IrcitUiThemeCurrent(), t);
    EXPECT_TRUE(IrcitUiThemeApplyCurrent(IrcitUiModelRoot()));
}
