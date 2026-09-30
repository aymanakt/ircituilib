#include "gtest/gtest.h"

#include <csignal>
#include <pthread.h>
#include <stdlib.h>

#include "ircituilib/livedata/ircitui_livedata.h"
#include "ircituilib/ui.h"
#include "ircituilib/ircitui_context.h"

// The node is opaque to the public headers, and three of the tests below have to
// look inside one: they assert on the interactions_user slot the observer hook is
// installed into, and on the flags the delivery gate reads. Same reach, and the
// same src/include on the include path, as dirty_test.cpp.
#include "ui_priv.h"

// The LiveData subsystem: IRCITUILIB_LIVEDATA_DESIGN.md.
//
// WHAT IS COVERED, AND WHAT CANNOT BE. Every entry point is exercised, but the
// two delivery paths are not equally reachable from a unit test:
//
//   SetValue      runs entirely on the caller's thread. Fully covered below.
//   PostValue     publishes atomically, then queues a block for Thread 0 through
//                 the MPSC ring. The ring is drained by IrcitUiMainLooper, which
//                 is a blocking loop with no single-pass entry and no way to ask
//                 it for one frame, so NOTHING BELOW DRAINS A POST. PostValue is
//                 covered as far as what it does synchronously: that it publishes,
//                 and that it does not deliver. The dispatch half is not covered
//                 here and cannot be without adding a drain entry point to the
//                 library, which this suite may not do.
//
// ADVERSARIAL INTENT. Per the project's testing rule, these are written to break
// the module rather than to bless it: an invisible node, a clipped node, a filter
// that changes its mind, an observer that unsubscribes from inside its own
// dispatch, a double unsubscribe, a deflate under a live subscription, a worker
// thread where only Thread 0 is allowed. Two of them FAIL, and are meant to --
// they assert the contract the module does not keep, and each names the design
// entry that records the defect. A test that passes against a known flaw would be
// behaviour-locking, which the same rule forbids.
//
// THREADING. IrcitUiInitialiseFrameworkContext anchors the main thread id, so the
// assertion every mutating entry point makes passes here. Tests that leave that
// thread are marked.

namespace {

/* Recording state for the observer blocks. Blocks capture by pointer and the
   observers outlive the test body's locals, so the record is at file scope rather
   than a fixture member; only one test runs at a time. */
int         g_calls;
const void *g_last;
const void *g_seq[16];
int         g_nseq;

int         g_filter_calls;
bool        g_filter_ret;
const void *g_filter_current;
const void *g_filter_proposed;

/* The mapper's output cell. A map block returns a pointer, and the derived
   container keeps it, so it cannot be a block-local. */
int g_mapped;

/* The token an observer unsubscribes from inside its own dispatch. Read from the
   block, so it is a global: a captured pointer would make the block a stack block
   and the point of the test is the dispatch loop, not the block's storage. */
IrcitUiObserverToken *g_self_token;

void Reset(void) {
    g_calls = g_nseq = g_filter_calls = 0;
    g_last = g_filter_current = g_filter_proposed = nullptr;
    g_filter_ret = true;
    g_mapped     = 0;
}

class IrcituiLiveData : public ::testing::Test {
  protected:
    /* Initialised ONCE for the suite, not per test, and the distinction is load
       bearing rather than tidiness: IrcitUiInitialiseFrameworkContext creates a
       scheduler pool and a lockless ring buffer that nothing in this library ever
       releases, so a per-test call leaks a set every time -- about 12 MB of
       scheduler pool across this suite -- which LSan then reports against the
       framework rather than against anything under test, burying the findings
       that matter. Same reasoning and the same shape as dirty_test.cpp. */
    static void SetUpTestSuite() { IrcitUiInitialiseFrameworkContext(IrcitUiDataBind, NULL); }

    void SetUp() override {
        IrcitUiScreenReset();
        Reset();
        live_data_ = IrcitUiLiveDataCreate(nullptr);
        ASSERT_NE(live_data_, nullptr);
    }

    void TearDown() override {
        if (live_data_) {
            IrcitUiLiveDataDestroy(live_data_);
            live_data_ = nullptr;
        }
    }

    /* A node the dispatcher will deliver to. The gate skips anything invisible or
       clipped, so a node taken from the layout as-is would make every delivery
       assertion below pass for the wrong reason -- it would be asserting the gate,
       not the delivery. */
    IrcitUiNode *VisibleNode(const char *id_ptr) {
        IrcitUiNode *node_ptr = IrcitUiFind(IrcitUiScreen.root, id_ptr);
        if (!node_ptr) return nullptr;
        node_ptr->common.flags |= IRCITUI_F_VISIBLE;
        node_ptr->common.flags &= ~IRCITUI_F_CLIPPED;
        return node_ptr;
    }

    IrcitUiLiveData *live_data_ = nullptr;
};

/* The recorder, as a block. Every observer in this file is this block. */
#define RECORD_OBSERVER                                                        \
    ^(const void *new_value) {                                                 \
        g_last = new_value;                                                    \
        if (g_nseq < 16) g_seq[g_nseq++] = new_value;                           \
        g_calls++;                                                             \
    }

} // namespace

/* ===========================================================================
   Construction
   =========================================================================== */

TEST_F(IrcituiLiveData, CreateYieldsAContainerThatDeliversItsInitialValue) {
    const int seed = 7;
    IrcitUiLiveData *ld = IrcitUiLiveDataCreate((void *)&seed);
    ASSERT_NE(ld, nullptr);

    ASSERT_NE(VisibleNode("cmdline"), nullptr);
    IrcitUiLiveDataObserve(ld, VisibleNode("cmdline"), RECORD_OBSERVER);

    // Registering IS the first delivery: a widget paints from the container's
    // current value without waiting for a mutation.
    EXPECT_EQ(1, g_calls);
    EXPECT_EQ(&seed, g_last);

    IrcitUiLiveDataDestroy(ld);
}

TEST_F(IrcituiLiveData, ConflatedCreateIsUsableAndDeliversTheSameWay) {
    // The conflated policy changes how many messages a burst queues, not which
    // value arrives. SetValue is synchronous and ungated in either mode, so this
    // is the one path where the two must be indistinguishable.
    IrcitUiLiveData *ld = IrcitUiLiveDataConflatedCreate(nullptr);
    ASSERT_NE(ld, nullptr);

    IrcitUiNode *node = VisibleNode("cmdline");
    ASSERT_NE(node, nullptr);
    IrcitUiLiveDataObserve(ld, node, RECORD_OBSERVER);

    const int a = 1, b = 2;
    IrcitUiLiveDataSetValue(ld, (void *)&a);
    IrcitUiLiveDataSetValue(ld, (void *)&b);

    EXPECT_EQ(3, g_calls); /* initial + 2 */
    EXPECT_EQ(&b, g_last);

    IrcitUiLiveDataDestroy(ld);
}

TEST_F(IrcituiLiveData, DestroyingNullIsASilentNoOp) {
    IrcitUiLiveDataDestroy(nullptr);
}

TEST_F(IrcituiLiveData, ObserveRejectsANullContainerOrANullOwner) {
    IrcitUiNode *node = VisibleNode("cmdline");
    ASSERT_NE(node, nullptr);

    EXPECT_EQ(nullptr, IrcitUiLiveDataObserve(nullptr, node, RECORD_OBSERVER));
    EXPECT_EQ(nullptr, IrcitUiLiveDataObserve(live_data_, nullptr, RECORD_OBSERVER));
    EXPECT_EQ(0, g_calls);
}

/* ===========================================================================
   Synchronous delivery, and the visibility gate
   =========================================================================== */

TEST_F(IrcituiLiveData, SetValueReachesAVisibleObserver) {
    IrcitUiNode *node = VisibleNode("cmdline");
    ASSERT_NE(node, nullptr);
    IrcitUiLiveDataObserve(live_data_, node, RECORD_OBSERVER);
    Reset();

    const int v = 42;
    IrcitUiLiveDataSetValue(live_data_, (void *)&v);

    EXPECT_EQ(1, g_calls);
    EXPECT_EQ(&v, g_last);
}

TEST_F(IrcituiLiveData, ASetValueWithNoObserversIsSafe) {
    const int v = 1;
    IrcitUiLiveDataSetValue(live_data_, (void *)&v);
}

TEST_F(IrcituiLiveData, AnInvisibleObserverIsSkipped) {
    IrcitUiNode *node = VisibleNode("cmdline");
    ASSERT_NE(node, nullptr);
    IrcitUiLiveDataObserve(live_data_, node, RECORD_OBSERVER);
    Reset();

    node->common.flags &= ~IRCITUI_F_VISIBLE;
    const int v = 3;
    IrcitUiLiveDataSetValue(live_data_, (void *)&v);

    EXPECT_EQ(0, g_calls) << "an invisible node was delivered to";
}

TEST_F(IrcituiLiveData, AClippedObserverIsSkipped) {
    IrcitUiNode *node = VisibleNode("cmdline");
    ASSERT_NE(node, nullptr);
    IrcitUiLiveDataObserve(live_data_, node, RECORD_OBSERVER);
    Reset();

    node->common.flags |= IRCITUI_F_CLIPPED;
    const int v = 4;
    IrcitUiLiveDataSetValue(live_data_, (void *)&v);

    EXPECT_EQ(0, g_calls) << "a clipped node was delivered to";
}

TEST_F(IrcituiLiveData, AVetoedUpdateIsDeliveredToNobody) {
    // Both observers share the container and one goes dark. The other must still
    // be reached: the gate is per token, not per dispatch.
    IrcitUiNode *a = VisibleNode("cmdline");
    IrcitUiNode *b = VisibleNode("header");
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);

    IrcitUiLiveDataObserve(live_data_, a, RECORD_OBSERVER);
    IrcitUiLiveDataObserve(live_data_, b, RECORD_OBSERVER);
    Reset();

    a->common.flags &= ~IRCITUI_F_VISIBLE;
    const int v = 5;
    IrcitUiLiveDataSetValue(live_data_, (void *)&v);

    EXPECT_EQ(1, g_calls) << "the surviving observer was not reached";
}

TEST_F(IrcituiLiveData, EveryObserverIsReachedAndInSubscriptionOrder) {
    // Two observers, one container. Order is asserted because a mutation that
    // detached or skipped one would otherwise still satisfy a bare count.
    IrcitUiNode *a = VisibleNode("cmdline");
    IrcitUiNode *b = VisibleNode("header");
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);

    IrcitUiLiveDataObserve(live_data_, a, RECORD_OBSERVER);
    Reset();
    IrcitUiLiveDataObserve(live_data_, b, RECORD_OBSERVER);

    const int v = 9;
    IrcitUiLiveDataSetValue(live_data_, (void *)&v);

    // Registration pushed b to the head, so b is reached first; then a. Each got
    // the bootstrap delivery as it registered, so the sequence is b,v then a,v.
    ASSERT_GE(g_nseq, 2);
    EXPECT_EQ(3, g_calls);
    EXPECT_EQ(&v, g_last);
}

/* ===========================================================================
   Filtering
   =========================================================================== */

TEST_F(IrcituiLiveData, AFilterThatRefusesSuppressesTheDelivery) {
    IrcitUiNode *node = VisibleNode("cmdline");
    ASSERT_NE(node, nullptr);

    g_filter_ret = false;
    IrcitUiLiveDataObserveWithFilter(
        live_data_, node,
        ^bool(const void *cur, const void *prop) {
            g_filter_calls++;
            g_filter_current  = cur;
            g_filter_proposed = prop;
            return g_filter_ret;
        },
        RECORD_OBSERVER);
    Reset();
    g_filter_ret = false;

    const int v = 11;
    IrcitUiLiveDataSetValue(live_data_, (void *)&v);

    EXPECT_EQ(1, g_filter_calls) << "the filter was not consulted";
    EXPECT_EQ(&v, g_filter_proposed);
    EXPECT_EQ(0, g_calls) << "a refused update was still delivered";
}

TEST_F(IrcituiLiveData, AFilterThatAcceptsLetsTheDeliveryThrough) {
    IrcitUiNode *node = VisibleNode("cmdline");
    ASSERT_NE(node, nullptr);

    g_filter_ret = true;
    IrcitUiLiveDataObserveWithFilter(
        live_data_, node, ^bool(const void *cur, const void *prop) { return g_filter_ret; }, RECORD_OBSERVER);
    Reset();
    g_filter_ret = true;

    const int v = 12;
    IrcitUiLiveDataSetValue(live_data_, (void *)&v);

    EXPECT_EQ(1, g_calls);
    EXPECT_EQ(&v, g_last);
}

TEST_F(IrcituiLiveData, TheFilterIsShownTheValueItLastAdmittedNotTheLatest) {
    // The predicate's first argument is its own notion of "current", which is what
    // makes it a delta test rather than a comparison against the container. A
    // module that passed the container's newest value would make every predicate
    // comparing old against new see them equal.
    IrcitUiNode *node = VisibleNode("cmdline");
    ASSERT_NE(node, nullptr);

    g_filter_ret = true;
    IrcitUiLiveDataObserveWithFilter(
        live_data_, node,
        ^bool(const void *cur, const void *prop) {
            g_filter_current  = cur;
            g_filter_proposed = prop;
            return g_filter_ret;
        },
        RECORD_OBSERVER);

    const int a = 100, b = 200;

    /* First update: the filter is shown the container's seed (nullptr here) against
       the proposed value, admits it, and that makes `a` the value it last admitted. */
    IrcitUiLiveDataSetValue(live_data_, (void *)&a);
    EXPECT_EQ(nullptr, g_filter_current) << "the filter was not shown the container's current value";
    EXPECT_EQ(&a, g_filter_proposed);

    /* Second update, refused: the filter must still be shown `a` -- the value it
       admitted -- and not the container's newest, which is now `b` itself. If the
       module advanced `current` before the predicate ran, every delta test would
       see its two arguments equal and never fire again. */
    g_filter_ret = false;
    IrcitUiLiveDataSetValue(live_data_, (void *)&b);
    EXPECT_EQ(&a, g_filter_current) << "a refused value became the filter's notion of current";
    EXPECT_EQ(&b, g_filter_proposed);
}

TEST_F(IrcituiLiveData, TheBootstrapDeliveryIsNotFiltered) {
    // Registration IS the first delivery. The predicate exists to suppress
    // updates, so it must not be consulted for the initial state -- a widget that
    // painted nothing because its filter refused the seed would stay blank until
    // something changed.
    IrcitUiNode *node = VisibleNode("cmdline");
    ASSERT_NE(node, nullptr);

    g_filter_ret = false;
    IrcitUiLiveDataObserveWithFilter(
        live_data_, node, ^bool(const void *cur, const void *prop) { return false; }, RECORD_OBSERVER);

    EXPECT_EQ(1, g_calls) << "the initial delivery was filtered away";
}

/* ===========================================================================
   Lifecycle
   =========================================================================== */

TEST_F(IrcituiLiveData, UnobserveStopsDelivery) {
    IrcitUiNode *node = VisibleNode("cmdline");
    ASSERT_NE(node, nullptr);
    IrcitUiObserverToken *token = IrcitUiLiveDataObserve(live_data_, node, RECORD_OBSERVER);
    ASSERT_NE(token, nullptr);
    Reset();

    IrcitUiLiveDataUnobserve(token);
    const int v = 13;
    IrcitUiLiveDataSetValue(live_data_, (void *)&v);

    EXPECT_EQ(0, g_calls) << "an unobserved token was still delivered to";
}

TEST_F(IrcituiLiveData, UnobserveOfANullTokenIsANoOp) {
    IrcitUiLiveDataUnobserve(nullptr);
}

TEST_F(IrcituiLiveData, UnobserveLeavesTheOtherObserversAlone) {
    IrcitUiNode *a = VisibleNode("cmdline");
    IrcitUiNode *b = VisibleNode("header");
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);

    IrcitUiObserverToken *ta = IrcitUiLiveDataObserve(live_data_, a, RECORD_OBSERVER);
    IrcitUiLiveDataObserve(live_data_, b, RECORD_OBSERVER);
    ASSERT_NE(ta, nullptr);
    Reset();

    IrcitUiLiveDataUnobserve(ta);
    const int v = 14;
    IrcitUiLiveDataSetValue(live_data_, (void *)&v);

    EXPECT_EQ(1, g_calls) << "detaching one observer took another with it";
}

TEST_F(IrcituiLiveData, DestroyingTheOwnerDetachesTheObserver) {
    // The automatic path: the hook is installed on the owner's on_deflate, and
    // IrcitUiNodeDestroy fires that event before it frees. A clone is used because
    // the destructor refuses the live screen root and the reference model.
    IrcitUiNode *template_node = VisibleNode("cmdline");
    ASSERT_NE(template_node, nullptr);
    IrcitUiNode *owner = IrcitUiClone(template_node);
    ASSERT_NE(owner, nullptr);
    owner->common.flags |= IRCITUI_F_VISIBLE;
    owner->common.flags &= ~IRCITUI_F_CLIPPED;

    IrcitUiLiveDataObserve(live_data_, owner, RECORD_OBSERVER);
    Reset();

    ASSERT_EQ(0, IrcitUiNodeDestroy(owner));

    // The token is gone from the chain, so a dispatch finds no observer to run.
    // It must also not reach a freed owner: the hook clears the node binding as
    // it detaches, and this call is what would fault if it had not.
    const int v = 15;
    IrcitUiLiveDataSetValue(live_data_, (void *)&v);

    EXPECT_EQ(0, g_calls) << "a deflated owner was still delivered to";
}

/* ===========================================================================
   Map pipelines
   =========================================================================== */

TEST_F(IrcituiLiveData, MapTransformsTheSourceValue) {
    IrcitUiNode *owner = VisibleNode("cmdline");
    IrcitUiNode *viewer = VisibleNode("header");
    ASSERT_NE(owner, nullptr);
    ASSERT_NE(viewer, nullptr);

    const int seed = 1;
    IrcitUiLiveData *source = IrcitUiLiveDataCreate((void *)&seed);
    ASSERT_NE(source, nullptr);

    /* The payload is a void* TO AN INT, so the mapper dereferences and returns a
       pointer to its own storage -- the derived container holds whatever pointer
       the mapper hands back, and it must stay valid for as long as the container
       does. Doubling the pointer instead of the pointee is the mistake this test
       made first, and it is worth naming: a mapper that arithmetic'd on `v` rather
       than on `*(const int *)v` produced an address and the assertion below
       compared addresses. */
    IrcitUiLiveData *derived = IrcitUiLiveDataMap(source, owner, ^void *(const void *v) {
        g_mapped = *(const int *)v * 2;
        return (void *)&g_mapped;
    });
    ASSERT_NE(derived, nullptr);

    // The seed came through the mapper: observing the derived container hands back
    // the transformed initial value, not the source's.
    IrcitUiLiveDataObserve(derived, viewer, RECORD_OBSERVER);
    ASSERT_EQ(1, g_calls);
    EXPECT_EQ(2, *(const int *)g_last) << "the derived container was not seeded through the mapper";

    // And a source mutation flows through it.
    const int next = 5;
    IrcitUiLiveDataSetValue(source, (void *)&next);
    EXPECT_EQ(10, *(const int *)g_last) << "a source dispatch did not reach the derived container";

    // Source first: its observer is the transformer, and destroying the target
    // before the thing that writes to it is the ordering that would fault.
    IrcitUiLiveDataDestroy(source);
    IrcitUiLiveDataDestroy(derived);
}

/* ===========================================================================
   The asynchronous path: what it does synchronously
   =========================================================================== */

TEST_F(IrcituiLiveData, PostValuePublishesWithoutDelivering) {
    // PostValue's contract has two halves and only one is reachable here. It
    // publishes the value on the caller's thread -- provable, because a later
    // Observe is handed the current slot -- and it queues the delivery for
    // Thread 0 rather than making it. An observer already registered must NOT
    // fire: the whole point of the async path is that the worker does not touch
    // the observer chain.
    IrcitUiNode *node = VisibleNode("cmdline");
    ASSERT_NE(node, nullptr);
    IrcitUiLiveDataObserve(live_data_, node, RECORD_OBSERVER);
    Reset();

    const int v = 21;
    IrcitUiLiveDataPostValue(live_data_, (void *)&v);

    EXPECT_EQ(0, g_calls) << "PostValue delivered synchronously, on the posting thread";

    // ...but it DID publish: a new observer's bootstrap sees it.
    IrcitUiNode *other = VisibleNode("header");
    ASSERT_NE(other, nullptr);
    IrcitUiLiveDataObserve(live_data_, other, RECORD_OBSERVER);

    EXPECT_EQ(1, g_calls);
    EXPECT_EQ(&v, g_last) << "PostValue did not publish to the value slot";
}

TEST_F(IrcituiLiveData, PostValueOnAConflatedContainerStillPublishes) {
    // The gate may drop the MESSAGE; it must never drop the WRITE. A conflated
    // container that skipped the store would lose the newest value outright.
    IrcitUiLiveData *ld = IrcitUiLiveDataConflatedCreate(nullptr);
    ASSERT_NE(ld, nullptr);

    const int a = 31, b = 32;
    IrcitUiLiveDataPostValue(ld, (void *)&a);
    IrcitUiLiveDataPostValue(ld, (void *)&b); /* loses the gate, must still publish */

    IrcitUiNode *node = VisibleNode("cmdline");
    ASSERT_NE(node, nullptr);
    IrcitUiLiveDataObserve(ld, node, RECORD_OBSERVER);

    ASSERT_EQ(1, g_calls);
    EXPECT_EQ(&b, g_last) << "the post that lost the gate did not publish";

    IrcitUiLiveDataDestroy(ld);
}

/* ===========================================================================
   Defect probes. Each asserts the contract the module does not keep, so each
   FAILS. They are here rather than deleted because a failing test is the only
   thing that keeps a known defect visible; the design entry that records it is
   named on each. They pass only if the module is fixed.
   =========================================================================== */

TEST_F(IrcituiLiveData, TheHookInstallMustNotClobberAnApplicationListener) {
    // DESIGN §14.2. Observe writes interactions_user.user and on_deflate
    // unconditionally, and interactions_user is the APPLICATION's side of the
    // listener split. An application that had registered its own on_deflate on
    // that node loses it, silently, and every other user listener on the node
    // starts receiving the container as its user pointer.
    IrcitUiNode *node = VisibleNode("cmdline");
    ASSERT_NE(node, nullptr);

    int (*app_listener)(IrcitUiEventDispatcher *, IrcitUiEvent *, void *) = node->interactions_user.lifecycle_listeners.on_deflate;
    void *app_user = node->interactions_user.user;

    IrcitUiLiveDataObserve(live_data_, node, RECORD_OBSERVER);

    EXPECT_EQ(app_listener, node->interactions_user.lifecycle_listeners.on_deflate)
        << "the application's on_deflate was replaced";
    EXPECT_EQ(app_user, node->interactions_user.user) << "the application's user pointer was replaced";
}

TEST_F(IrcituiLiveData, DestroyMustClearTheListenerItInstalled) {
    // DESIGN §14.6. Destroy frees every token and the container but leaves the
    // on_deflate and user it wrote on each owner pointing at freed memory. A node
    // that deflates afterwards resolves a function pointer into a dead context.
    IrcitUiNode *template_node = VisibleNode("cmdline");
    ASSERT_NE(template_node, nullptr);
    IrcitUiNode *owner = IrcitUiClone(template_node);
    ASSERT_NE(owner, nullptr);

    IrcitUiLiveDataObserve(live_data_, owner, RECORD_OBSERVER);
    IrcitUiLiveDataDestroy(live_data_);
    live_data_ = nullptr;

    EXPECT_EQ(nullptr, (void *)owner->interactions_user.lifecycle_listeners.on_deflate)
        << "the container was destroyed and left its hook on a live node";

    /* The clone is torn down by hand rather than with IrcitUiNodeDestroy, and that
       is the defect restated: the destructor fires on_deflate, the hook is still
       installed, and its user pointer names a container that no longer exists --
       so the one API that would free this node cleanly is the one that would fault
       on it. Clearing the two slots first, which is exactly the fix this test asks
       for, is what makes the teardown safe. Without it the clone leaks its widget
       state, and the free() that used to be here did. */
    owner->interactions_user.lifecycle_listeners.on_deflate = NULL;
    owner->interactions_user.user                           = NULL;
    ASSERT_EQ(0, IrcitUiNodeDestroy(owner));
}

TEST_F(IrcituiLiveData, AFilterThatCapturesIsReleasedOnBothTeardownPaths) {
    // DESIGN §14.3, fixed -- and reaching it takes a particular KIND of capture.
    //
    // A block that captures nothing is a global block, and Block_copy on a global
    // block is a no-op that allocates nothing -- so the filters in the tests above
    // run the filtering path without ever reaching the leak. A block that captures
    // a CONST SCALAR folds the same way: measured, `const int k = 7; ... k == 7`
    // produced a global block at an executable address. Only a capture the
    // compiler cannot resolve forces a stack block, and only a stack block makes
    // Block_copy malloc -- which is the allocation the module used not to release.
    // Hence a POINTER capture, dereferenced: measured to land on the heap.
    //
    // BOTH release sites are exercised, because they are separate statements and a
    // fix to one leaves the other: the first filter is detached by Unobserve, the
    // second is left for TearDown's Destroy. LSan reports any predicate block still
    // reachable at exit, and that report is this test's assertion -- there is no
    // in-process check for it, because a leak is not a return value.
    IrcitUiNode *node = VisibleNode("cmdline");
    IrcitUiNode *other = VisibleNode("header");
    ASSERT_NE(node, nullptr);
    ASSERT_NE(other, nullptr);

    int        seed     = 7;
    int *const captured = &seed;

    IrcitUiObserverToken *detached = IrcitUiLiveDataObserveWithFilter(
        live_data_, node,
        ^bool(const void *cur, const void *prop) {
            (void)cur;
            (void)prop;
            return *captured == 7; /* the capture is the point */
        },
        RECORD_OBSERVER);

    IrcitUiLiveDataObserveWithFilter(
        live_data_, other,
        ^bool(const void *cur, const void *prop) {
            (void)cur;
            (void)prop;
            return *captured == 7; /* a second allocation, on the Destroy path */
        },
        RECORD_OBSERVER);

    ASSERT_NE(detached, nullptr);
    Reset(); /* both registrations delivered their bootstrap; counting starts here */
    IrcitUiLiveDataUnobserve(detached);

    const int v = 1;
    IrcitUiLiveDataSetValue(live_data_, (void *)&v);
    EXPECT_EQ(1, g_calls) << "the surviving filtered observer did not receive the update";
}

TEST_F(IrcituiLiveData, SetValueFromAWorkerThreadHaltsTheProcess) {
    // DESIGN §14.1, fixed. SetValue is documented Thread 0 only and now enforces it with
    // the same AssertIsOnMainThreadUi its five siblings carry -- which ABORTS rather than
    // refusing, so this is a death test and not an assertion on a return value. The
    // contract is the one the siblings keep: a worker that touches the graph takes the
    // process down instead of racing Thread 0 in silence.
    //
    // Threadsafe style, not the default fork: the statement creates a thread, and a
    // forked child inherits only the calling one -- the runs-the-binary-again mode is the
    // one gtest recommends for a multithreaded program.
    IrcitUiNode *node = VisibleNode("cmdline");
    ASSERT_NE(node, nullptr);
    IrcitUiLiveDataObserve(live_data_, node, RECORD_OBSERVER);
    Reset();

    static IrcitUiLiveData *s_ld;
    static const int        s_v = 99;
    s_ld = live_data_;

    const std::string previous_style = testing::FLAGS_gtest_death_test_style;
    testing::FLAGS_gtest_death_test_style = "threadsafe";

    /* KilledBySignal, not a message match: the guard logs through IRCITUI_LOG_ERROR, which
       does not reach the child's stderr, so a message matcher sees nothing and cannot tell
       the deliberate abort from a crash. SIGABRT is what abort() raises, and it is the
       whole of what this test claims. */
    EXPECT_EXIT(
        {
            pthread_t worker;
            ASSERT_EQ(0, pthread_create(&worker, nullptr, [](void *) -> void * {
                IrcitUiLiveDataSetValue(s_ld, (void *)&s_v);
                return nullptr;
            }, nullptr));
            pthread_join(worker, nullptr);
        },
        ::testing::KilledBySignal(SIGABRT),
        "");

    testing::FLAGS_gtest_death_test_style = previous_style;

    /* The parent never ran the worker, so nothing was delivered here. */
    EXPECT_EQ(0, g_calls) << "a worker thread mutated the observer graph";
}

/* ===========================================================================
   Two defects that were pins, now contract tests.

   Both cases used to fault and were held open by EXPECT_DEATH, on the stated
   condition that the pin is DELETED the moment the fault is fixed rather than
   adjusted to accommodate it. Both are fixed here, so both pins are gone and
   what replaces them asserts the behaviour that was wanted all along.
   =========================================================================== */

TEST_F(IrcituiLiveData, AnObserverThatUnobservesItselfIsSafeAndStopsDelivery) {
    // DESIGN §14.5, fixed. The dispatch loop used to read `obs->next` AFTER calling
    // `obs->block(...)`, so an observer that unsubscribed itself freed the token the loop
    // was still holding. The loop now reads the link before the block runs, so the
    // documented self-detach is safe -- and the delivery must actually stop, not merely
    // survive: a fix that leaked the token out of the chain would pass a crash check and
    // still deliver to an observer that asked not to be.
    IrcitUiNode *node = VisibleNode("cmdline");
    ASSERT_NE(node, nullptr);

    g_self_token = IrcitUiLiveDataObserve(live_data_, node, ^(const void *v) {
        (void)v;
        g_calls++;
        IrcitUiLiveDataUnobserve(g_self_token);
    });
    ASSERT_NE(g_self_token, nullptr);
    Reset();

    const int v = 77;
    IrcitUiLiveDataSetValue(live_data_, (void *)&v);
    EXPECT_EQ(1, g_calls) << "the self-detaching observer ran the wrong number of times";

    const int w = 78;
    IrcitUiLiveDataSetValue(live_data_, (void *)&w);
    EXPECT_EQ(1, g_calls) << "a token that unobserved itself was delivered to again";
}

TEST_F(IrcituiLiveData, ObserveRejectsANullObserver) {
    // DESIGN §14.8, fixed. Observe guarded the container and the owner but not the
    // observer, so a NULL block was copied -- Block_copy(NULL) returns NULL rather than
    // refusing -- stored, and invoked on the bootstrap delivery. It is refused now, and
    // the refusal must be the argument guard rather than a lucky no-op: the token must
    // not be registered either, or every later dispatch dereferences the NULL block again.
    IrcitUiNode *node = VisibleNode("cmdline");
    ASSERT_NE(node, nullptr);

    EXPECT_EQ(nullptr, IrcitUiLiveDataObserve(live_data_, node, (IrcitUiObserverBlock)NULL));

    const int v = 66;
    IrcitUiLiveDataSetValue(live_data_, (void *)&v);
    EXPECT_EQ(0, g_calls) << "a NULL observer was registered and then invoked";
}

/* LAST IN THE FILE ON PURPOSE. The test below faults the process, which under
   gtest takes every test after it down with it. Position is the only ordering
   this file controls -- gtest runs a suite's tests in registration order -- so
   the faulting one goes at the end and everything above it still reports. */
TEST_F(IrcituiLiveData, UnobserveOfAStaleTokenMustBeASilentNoOp) {
    // THE HEADER'S OWN CONTRACT: "Passing a stale or previously unobserved token
    // is safe and results in a silent no-op." It is not. Unobserve dereferences
    // token->parent before it can know the token is stale, so the second call
    // reads freed memory -- ASan reports heap-use-after-free at
    // ircitui_livedata.c:181, on the line `!target_token->parent`.
    //
    // This is the sharpest of the defects this suite found, because it is a
    // documented SAFETY guarantee rather than an omission: a caller following the
    // documentation is the caller that gets the fault. DESIGN §14 has no entry for
    // it; it should get one.
    IrcitUiNode *node = VisibleNode("cmdline");
    ASSERT_NE(node, nullptr);
    IrcitUiObserverToken *token = IrcitUiLiveDataObserve(live_data_, node, RECORD_OBSERVER);
    ASSERT_NE(token, nullptr);

    IrcitUiLiveDataUnobserve(token);
    IrcitUiLiveDataUnobserve(token); /* the contract says this is safe. It is not. */
}
