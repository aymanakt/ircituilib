#include "gtest/gtest.h"

#include "ircituilib/ui.h"
#include "ircituilib/ircitui_context.h"

// Port of tests/test_lifecycle.c.
//
// The original carried a `static void hidden(IrcitUiNode *)` plus a `(void)hidden;`
// to justify its existence.  That is a no-op, and in C++ it trips
// -Wunused-function under -Wall, so it is dropped rather than ported.
TEST(IrcituiCoreLifecycle, ResetClonesAndDeflate) {
    // The framework is fed before anything reads the model: the library resolves ids
    // against the context, so a test that skipped this would be driving an uninitialised
    // framework. IrcitUiDataBind supplies this library's own generated tree.
        // No window backend: these tests exercise the model and the theme, not a
    // driver. The context retains the registry and does not own it, so NULL is
    // the honest answer here and IrcitUiContextWindowDriver reports it back.
IrcitUiInitialiseFrameworkContext(IrcitUiDataBind, NULL);

    IrcitUiNode *n = IrcitUiFind(IrcitUiModelRoot(), "main");
    EXPECT_NE(n, nullptr);

    // The generated tree is static; verify reset/clone and lookup paths are live.
    IrcitUiScreenReset();
    ASSERT_NE(IrcitUiScreen.root, nullptr);
    EXPECT_NE(IrcitUiFind(IrcitUiScreen.root, "main"), nullptr);

    IrcitUiDeflate(IrcitUiScreen.root);
}
