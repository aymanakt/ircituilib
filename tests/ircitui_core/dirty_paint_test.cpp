#include "gtest/gtest.h"

#include <ncurses.h> /* the fake shim: fkReset / fkCount / fkIndexOf */

#include "ircituilib/ui.h"
#include "ircituilib/ircitui_context.h"

// What a frame actually does to the windows.
//
// The sibling dirty_test.cpp asserts on flags. This file asserts on the calls
// those flags are supposed to save: how many nodes painted, whether any window
// was torn down, and in what order windows were refreshed. It needs the
// fake-curses build, and for two reasons rather than one.
//
// First, the counts come from the shim's instrumentation -- there is nothing to
// count without it.
//
// Second, and less obviously: with real curses a window cannot exist before
// initscr(), so IrcitUiRealize would leave every node with win == NULL and
// IrcitUiDraw would return at its guard for all of them. Every assertion here
// would be vacuously true. The fake shim hands out a distinct non-NULL window
// per node, which is what gives the paint gate something to gate.
#include "ui_priv.h"

namespace {

// A node is painted only if it has a window and is visible: IrcitUiDraw returns
// at its guard otherwise and never reaches wbkgd. Deriving the expected count
// this way rather than hard-coding it keeps these tests honest when the
// reference layout changes.
int CountDrawable(IrcitUiNode *node_ptr) {
    int c = (node_ptr->common.win && (node_ptr->common.flags & IRCITUI_F_VISIBLE)) ? 1 : 0;
    for (int i = 0; i < node_ptr->container.n_children; i++) c += CountDrawable(node_ptr->container.children[i]);
    return c;
}

// Every drawable node in the subtree is marked painted. A drawable node left
// without the flag after a pass is one the next frame paints again -- and if it
// was skipped without having been painted, its cells are blank.
bool AllDrawableAreDrawn(IrcitUiNode *node_ptr) {
    if (node_ptr->common.win && (node_ptr->common.flags & IRCITUI_F_VISIBLE) &&
        !(node_ptr->common.flags & IRCITUI_F_DRAWN))
        return false;
    for (int i = 0; i < node_ptr->container.n_children; i++)
        if (!AllDrawableAreDrawn(node_ptr->container.children[i])) return false;
    return true;
}

class IrcituiPaint : public ::testing::Test {
protected:
    // Once per suite: the context owns a scheduler pool and a ring buffer that
    // nothing releases, so a per-test call would leave each earlier one
    // unreachable and LSan would report it.
    static void SetUpTestSuite() { IrcitUiInitialiseFrameworkContext(IrcitUiDataBind, NULL); }

    void SetUp() override {
        IrcitUiScreenReset();
        // One frame to settle. It measures the freshly cloned tree, realises a
        // window for every node, and paints everything once, so both the flags
        // and the windows are current before a test changes anything. Counting
        // starts after it.
        IrcitUiRenderFrame();
        fkReset();
    }

    static IrcitUiNode *Root() { return IrcitUiScreen.root; }
};

} // namespace

TEST_F(IrcituiPaint, ASettledTreePaintsNothing) {
    IrcitUiRenderFrame();

    // Nothing was invalidated, so no node repainted. This is the whole point of
    // the model: a frame with no changes costs no widget work at all.
    EXPECT_EQ(0, fkCount(FK_OP_WBKGD)) << "a clean tree still ran the painter";
    EXPECT_EQ(0, fkCount(FK_OP_WERASE));

    // BUT EVERY NODE IS PUSHED, and that is deliberate rather than a regression:
    // b22ba1a found that wgetch() refreshes a window it finds touched, so a read
    // of a touched-but-blank stdscr staged a blank screen and cleared the glass --
    // fatal while the painter published deltas only. The draw pass now touches
    // before it stages, so the touch counts every node every pass.
    //
    // This assertion used to read EXPECT_EQ(0, ...) with the note that it had
    // tightened when "the refresh gate landed". That gate is the one b22ba1a
    // removed, and this file stopped being run the same week the fake build
    // stopped compiling, which is why the expectation outlived its contract.
    EXPECT_GT(fkCount(FK_OP_REFRESH), 0) << "the untouched windows were not restaged";
    EXPECT_EQ(fkCount(FK_OP_TOUCH), fkCount(FK_OP_REFRESH))
        << "a window was pushed without being touched first, which is the bug b22ba1a fixed";
}

TEST_F(IrcituiPaint, AKeystrokeRebuildsNoWindowsAndPaintsOneNode) {
    IrcitUiNode *field = IrcitUiFind(Root(), "cmdline");
    ASSERT_NE(field, nullptr);
    ASSERT_NE(field->common.win, nullptr);

    IrcitUiInvalidate(field);
    IrcitUiRenderFrame();

    // The geometry did not move, so nothing is measured and no window is torn
    // down or rebuilt. Before the frame's measure pass was gated, this frame
    // rebuilt every window in the tree.
    EXPECT_EQ(0, fkCount(FK_OP_DELWIN)) << "a keystroke rebuilt windows";
    EXPECT_EQ(0, fkCount(FK_OP_NEWWIN));
    EXPECT_EQ(0, fkCount(FK_OP_DERWIN));

    // And exactly one node painted -- the field, not the tree.
    ASSERT_EQ(1, fkCount(FK_OP_WBKGD)) << "the keystroke painted more (or less) than one node";
    EXPECT_GE(fkIndexOf(field->common.win, FK_OP_WBKGD), 0) << "the node painted was not the field";
}

TEST_F(IrcituiPaint, PaintingAContainerRepaintsItsWholeSubtree) {
    IrcitUiNode *header = IrcitUiFind(Root(), "header");
    ASSERT_NE(header, nullptr);
    ASSERT_GT(header->container.n_children, 0);

    int expected = CountDrawable(header);
    ASSERT_GT(expected, 1); // the container and at least one child

    IrcitUiInvalidate(header);
    IrcitUiRenderFrame();

    // A derwin child shares its parent's cell storage, so painting the container
    // erases every cell its descendants occupy. Without the cascade the children
    // would be skipped as already-drawn and left blank for good -- nothing would
    // clear their flag to bring them back.
    EXPECT_EQ(expected, fkCount(FK_OP_WBKGD)) << "the paint did not cascade to the subtree";
    EXPECT_TRUE(AllDrawableAreDrawn(header));
}

TEST_F(IrcituiPaint, AnOverlappingNodeRepaintsWhenWhatItCoversRepaints) {
    IrcitUiNode *root = Root();
    IrcitUiNode *body = IrcitUiFind(root, "body");
    ASSERT_NE(body, nullptr);

    // A popup is a separate top-level window grafted onto the screen root, drawn
    // after the tree it covers.
    IrcitUiInflateFloat(root, "file_menu");
    IrcitUiRenderFrame(); // settle the inflated tree; everything repaints
    IrcitUiNode *popup = IrcitUiFind(root, "file_menu");
    ASSERT_NE(popup, nullptr);
    ASSERT_NE(popup->common.win, nullptr);

    // The test is only meaningful while the popup actually sits over the body.
    // Asserted rather than assumed, so a layout change fails here instead of
    // quietly turning the rest of this test into a tautology.
    ASSERT_LT(body->common.x, popup->common.x + popup->common.w);
    ASSERT_GT(body->common.x + body->common.w, popup->common.x);
    ASSERT_LT(body->common.y, popup->common.y + popup->common.h);
    ASSERT_GT(body->common.y + body->common.h, popup->common.y);

    fkReset();
    IrcitUiInvalidate(body);
    IrcitUiRenderFrame();

    // The body repainted, so it was pushed.
    int i_body_paint = fkIndexOf(body->common.win, FK_OP_WBKGD);
    ASSERT_GE(i_body_paint, 0);

    // AND THE POPUP REPAINTED TOO, which reverses what this test asserted when it
    // was written. The old expectation -- pushed but not repainted, on the theory
    // that a refresh is enough to keep an overlapped window visible -- is the
    // claim sMarkOverlapSiblingsPaintDirty's own comment records as measured
    // false in 1ddf067: a window wins its cells only by REWRITING them, so a
    // sibling that repaints underneath one clears its DRAWN flag and forces the
    // repaint. Refreshing alone leaves the covered node showing the other's text.
    int i_popup_paint = fkIndexOf(popup->common.win, FK_OP_WBKGD);
    ASSERT_GE(i_popup_paint, 0) << "the overlapped popup was not repainted, so the body shows through it";

    // And it repainted AFTER the node it covers -- the popup is drawn later in the
    // child order, which is the only reason its rewrite wins the shared cells.
    EXPECT_GT(i_popup_paint, i_body_paint) << "the popup repainted before the node covering it";

    int i_popup = fkIndexOf(popup->common.win, FK_OP_REFRESH);
    int i_body  = fkIndexOf(body->common.win, FK_OP_REFRESH);
    ASSERT_GE(i_popup, 0) << "the overlapped popup was never refreshed";
    ASSERT_GE(i_body, 0);

    // And pushed AFTER the node that trampled it: ncurses resolves stacking by
    // refresh order, so the order is the mechanism, not an implementation
    // detail. Refreshing the popup first would leave the body's cells on top.
    EXPECT_GT(i_popup, i_body) << "the popup was refreshed before the repaint that covered it";
}

TEST_F(IrcituiPaint, AResizeRefreshesAnOverlappingPopup) {
    IrcitUiNode *root = Root();
    IrcitUiInflateFloat(root, "file_menu");
    IrcitUiRenderFrame();

    IrcitUiNode *popup = IrcitUiFind(root, "file_menu");
    ASSERT_NE(popup, nullptr);
    ASSERT_NE(popup->common.win, nullptr);

    fkReset();

    // A resize raises a layout request on the root, which is the case where the
    // change set would be empty if only the invalidators fed it: the repaint
    // here comes from IrcitUiRealize clearing paint on everything it rebuilds,
    // not from IrcitUiInvalidate. The popup must survive it.
    IrcitUiRequestLayout(root);
    IrcitUiRenderFrame();

    EXPECT_GE(fkIndexOf(popup->common.win, FK_OP_WBKGD), 0) << "the popup did not repaint after a relayout";
    EXPECT_GE(fkIndexOf(popup->common.win, FK_OP_REFRESH), 0) << "the popup was not refreshed after a relayout";
}

TEST_F(IrcituiPaint, AnOverflowedChangeSetRefreshesEverything) {
    IrcitUiNode *root = Root();

    fkReset();

    // The shipped reference model cannot fill the change set -- it has fewer
    // drawable nodes than IRCITUI_DIRTY_LIST_CAPACITY, so overflow is
    // unreachable from the outside and this exercises the rule directly. A host
    // model with more nodes than the cap reaches it the ordinary way.
    IrcitUiFrameDirtyList()->overflow = 1;
    IrcitUiRenderFrame();

    // Nothing repainted, but every drawable node was pushed: the cap gives up on
    // the overlap question and takes the conservative answer.
    EXPECT_EQ(0, fkCount(FK_OP_WBKGD));
    EXPECT_EQ(CountDrawable(root), fkCount(FK_OP_REFRESH)) << "an overflowed frame under-drew";
}

TEST_F(IrcituiPaint, NoWindowlessNodeIsEverHandedToCurses) {
    IrcitUiNode *field = IrcitUiFind(Root(), "cmdline");
    ASSERT_NE(field, nullptr);
    ASSERT_NE(field->common.win, nullptr);

    // A folded container deflates its children, so their windows are gone while
    // they are still in the tree. Drawing past that would hand NULL to
    // wnoutrefresh.
    IrcitUiNode *panel = IrcitUiFind(Root(), "side_panel");
    if (panel) IrcitUiSetCollapsed(panel, 1);
    IrcitUiInvalidate(field);
    IrcitUiRenderFrame();

    EXPECT_EQ(0, fk_null_win_calls) << "a stub was handed a NULL window";

    if (panel) IrcitUiSetCollapsed(panel, 0);
    IrcitUiRenderFrame();
    EXPECT_EQ(0, fk_null_win_calls);
}
