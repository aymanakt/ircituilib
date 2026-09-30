#include "gtest/gtest.h"

#include <stdlib.h>

#include "ircituilib/ui.h"
#include "ircituilib/ircitui_context.h"

// The invalidation model's flag semantics.
//
// These tests read the private node state directly -- common.is_dirty and the
// IRCITUI_F_* bits -- so they need src/include on the include path, and no
// window backend at all: nothing here reaches ncurses. That is deliberate.
// IrcitUiLayout is arithmetic over node fields and IrcitUiRealize guards every
// call on a non-NULL window, so a tree that was never realised can be measured
// and its flags inspected with no terminal in sight.
//
// What this file does NOT prove is the performance claim: that a settled frame
// skips the measure pass. That needs a count of delwin/derwin calls, which needs
// the instrumented fake-curses build. What it proves is the mechanism the claim
// rests on -- which flag is set by which mutation, what clears it, and what
// reaches the root.
#include "ui_priv.h"

namespace {

// The framework is initialised once for the whole suite, not per test.
// IrcitUiInitialiseFrameworkContext creates a scheduler pool and a lockless ring
// buffer that nothing in this library ever releases, so a per-test call leaks a
// set each time -- which LSan reports, correctly, against the framework rather
// than against anything here. The tree, by contrast, is cheap and disposable:
// IrcitUiScreenReset frees the last one before installing the next.
class IrcituiDirty : public ::testing::Test {
protected:
    static void SetUpTestSuite() { IrcitUiInitialiseFrameworkContext(IrcitUiDataBind, NULL); }

    void SetUp() override { IrcitUiScreenReset(); }

    static IrcitUiNode *Root() { return IrcitUiScreen.root; }
};

} // namespace

TEST_F(IrcituiDirty, ScreenResetInstallsATreeThatNeedsMeasuring) {
    IrcitUiNode *root = Root();

    ASSERT_NE(root, nullptr);
    // The reset replaces the tree, so the replacement has no geometry and must
    // be measured before anything can be painted. The clone cannot carry the
    // flag -- it masks it, and the model it came from never set it -- so the
    // reset has to say so itself.
    EXPECT_EQ(root->common.is_dirty, 1);
    EXPECT_EQ(root->common.win, nullptr);
}

TEST_F(IrcituiDirty, LayoutClearsTheFlagAsItResolvesGeometry) {
    IrcitUiNode *root = Root();
    IrcitUiNode *field = IrcitUiFind(root, "cmdline");
    ASSERT_NE(field, nullptr);

    IrcitUiRequestLayout(field);
    EXPECT_EQ(root->common.is_dirty, 1);

    IrcitUiLayout(root, 0, 0, 80, 24);

    // Resolved, so no longer stale -- and that is what stops the frame's
    // measure pass running again on the next tick.
    EXPECT_EQ(root->common.is_dirty, 0);
    EXPECT_EQ(field->common.is_dirty, 0);
}

TEST_F(IrcituiDirty, InvalidateIsPaintOnlyAndDoesNotReachTheRoot) {
    IrcitUiNode *root = Root();
    IrcitUiNode *field = IrcitUiFind(root, "cmdline");
    ASSERT_NE(field, nullptr);

    IrcitUiLayout(root, 0, 0, 80, 24);
    ASSERT_EQ(root->common.is_dirty, 0);

    IrcitUiInvalidate(field);

    // The geometry did not move, so nothing needs re-measuring: not the field
    // and not any ancestor of it. A keystroke must not cost a layout pass.
    EXPECT_EQ(field->common.is_dirty, 0);
    EXPECT_EQ(root->common.is_dirty, 0);
}

TEST_F(IrcituiDirty, RequestLayoutReachesTheRootThroughEveryAncestor) {
    IrcitUiNode *root = Root();
    IrcitUiNode *field = IrcitUiFind(root, "cmdline");
    ASSERT_NE(field, nullptr);

    IrcitUiLayout(root, 0, 0, 80, 24);
    ASSERT_EQ(root->common.is_dirty, 0);

    IrcitUiRequestLayout(field);

    // The sweep that resolves a request is top-down, so only a root that knows
    // can start one. Every node between the field and the root has to carry it.
    for (IrcitUiNode *p = field; p; p = p->common.parent) EXPECT_EQ(p->common.is_dirty, 1);
    EXPECT_EQ(root->common.is_dirty, 1);
}

TEST_F(IrcituiDirty, BothInvalidatorsClearPaintOnTheNamedNode) {
    IrcitUiNode *root = Root();
    IrcitUiNode *field = IrcitUiFind(root, "cmdline");
    ASSERT_NE(field, nullptr);

    IrcitUiLayout(root, 0, 0, 80, 24);
    field->common.flags |= IRCITUI_F_DRAWN;

    IrcitUiInvalidate(field);
    EXPECT_EQ(field->common.flags & IRCITUI_F_DRAWN, 0u);

    field->common.flags |= IRCITUI_F_DRAWN;
    IrcitUiRequestLayout(field);
    EXPECT_EQ(field->common.flags & IRCITUI_F_DRAWN, 0u);
}

TEST_F(IrcituiDirty, RequestLayoutDoesNotInvalidatePaintBeneathTheNamedNode) {
    IrcitUiNode *root = Root();
    IrcitUiNode *header = IrcitUiFind(root, "header");
    ASSERT_NE(header, nullptr);
    ASSERT_GT(header->container.n_children, 0);

    IrcitUiLayout(root, 0, 0, 80, 24);
    IrcitUiNode *child = header->container.children[0];
    child->common.flags |= IRCITUI_F_DRAWN;

    IrcitUiRequestLayout(header);

    // Deliberately not cascaded. A move or a scroll shifts a subtree as a unit
    // and the children's cells are still good; whether that survives is
    // IrcitUiRealize's call, and it clears what it actually rebuilds.
    EXPECT_NE(child->common.flags & IRCITUI_F_DRAWN, 0u);
}

TEST_F(IrcituiDirty, ACloneDoesNotInheritALayoutRequest) {
    IrcitUiNode *root = Root();
    IrcitUiNode *field = IrcitUiFind(root, "cmdline");
    ASSERT_NE(field, nullptr);
    // A leaf, so the clone allocates no child table. It does still allocate the
    // per-kind state IrcitUiClone attaches for an edit field (IrcitUiEditTextStateAttach
    // -> sAllocWidgetState, 552 bytes), which a plain free() would strand -- hence
    // IrcitUiNodeDestroy below rather than the free() that used to be here. The
    // comment that justified that free() claimed the public surface had no
    // counterpart to the engine's tree-freeing helper; IrcitUiNodeDestroy is it.
    ASSERT_EQ(field->container.n_children, 0);

    IrcitUiRequestLayout(field);
    ASSERT_EQ(field->common.is_dirty, 1);

    // The whole-struct copy inside IrcitUiClone would carry the flag; the mask
    // is what stops a model node's stale request reaching every clone of it.
    IrcitUiNode *copy = IrcitUiClone(field);
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy->common.is_dirty, 0);

    // A detached clone, so this is the case the destructor documents as safe.
    // ASSERT rather than EXPECT: a refusal would leave the leak this test exists
    // to disprove, and the next line would be no evidence of anything.
    ASSERT_EQ(IrcitUiNodeDestroy(copy), 0);
}
