#include "gtest/gtest.h"

#include "ircituilib/ui.h"
#include "ircituilib/ircitui_context.h"

// Port of tests/test_api.c.
//
// The model is one object in generated/ui_generated.c, initialised by the linker
// rather than by a call, so IrcitUiDataBind() constructs nothing and is
// repeat-callable. Every test feeds the framework itself: gtest gives no ordering
// guarantee across translation units, so nothing may depend on another test
// having run first.
//
// gtest's ASSERT_*/EXPECT_* are never compiled out, unlike the assert() this
// replaces, which NDEBUG removes entirely in a Release build.
TEST(IrcituiCoreApi, ReferenceTreeBinds) {
    // The framework is fed before anything reads the model: the library resolves ids
    // against the context, so a test that skipped this would be driving an uninitialised
    // framework. IrcitUiDataBind supplies this library's own generated tree.
        // No window backend: these tests exercise the model and the theme, not a
    // driver. The context retains the registry and does not own it, so NULL is
    // the honest answer here and IrcitUiContextWindowDriver reports it back.
IrcitUiInitialiseFrameworkContext(IrcitUiDataBind, NULL);
    ASSERT_NE(IrcitUiModelRoot(), nullptr);
    EXPECT_NE(IrcitUiFind(IrcitUiModelRoot(), "main"), nullptr);
    EXPECT_TRUE(IrcitUiIsContainer(IRCITUI_LINEAR));
}
