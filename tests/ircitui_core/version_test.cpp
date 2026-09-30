#include "gtest/gtest.h"

#include <cstring>
#include <string>

#include <version.h>

// The accessors are defined in libircuiti.a itself: version.c is a source of the
// static library, not of this test.  So this links ircitui and calls them
// through <version.h>, rather than re-compiling version.c the way ircit's
// version_tests.cpp has to.
//
// The names carry the IrcitUiLib prefix rather than mirroring ircit's
// c_version/u_compiled/t_compiled: an archive member is pulled exactly when one
// of its symbols is referenced, so an unprefixed name would collide precisely
// when it mattered.
//
// <version.h> carries the extern "C" guard these declarations need, so nothing
// is declared locally here -- the header is the single declaration site.

TEST(IrcituiCoreVersion, AccessorsArePopulated) {
    ASSERT_NE(IrcitUiLibVersion(), nullptr);
    EXPECT_GT(std::strlen(IrcitUiLibVersion()), 0u);

    ASSERT_NE(IrcitUiLibVersionMajor(), nullptr);
    EXPECT_GT(std::strlen(IrcitUiLibVersionMajor()), 0u);

    ASSERT_NE(IrcitUiLibVersionMinor(), nullptr);
    EXPECT_GT(std::strlen(IrcitUiLibVersionMinor()), 0u);

    ASSERT_NE(IrcitUiLibVersionPatch(), nullptr);
    EXPECT_GT(std::strlen(IrcitUiLibVersionPatch()), 0u);

    ASSERT_NE(IrcitUiLibVersionInternal(), nullptr);
    EXPECT_GT(std::strlen(IrcitUiLibVersionInternal()), 0u);

    ASSERT_NE(IrcitUiLibCompiledBy(), nullptr);
    EXPECT_GT(std::strlen(IrcitUiLibCompiledBy()), 0u);

    ASSERT_NE(IrcitUiLibCompiledAt(), nullptr);
    EXPECT_GT(std::strlen(IrcitUiLibCompiledAt()), 0u);
}

// The generated string must agree with the checked-in version.h that CMake also
// reads for project(VERSION ...).  If the generator and the header ever drift,
// this is what catches it.
TEST(IrcituiCoreVersion, MatchesTheCheckedInHeader) {
    // Four fields, not three: ircituilib carries an incremental IRCITUI_INTERNAL
    // alongside the release triple, and the generated string reports it so a
    // running binary can be matched to the gate that produced it.
    const std::string expected = "IRCITUI " STRINGISE(IRCITUI_MAJOR) "."
                                 STRINGISE(IRCITUI_MINOR) "."
                                 STRINGISE(IRCITUI_PATCH) "."
                                 STRINGISE(IRCITUI_INTERNAL);
    ASSERT_NE(IrcitUiLibVersion(), nullptr);
    EXPECT_EQ(expected, std::string(IrcitUiLibVersion()));
}

// The whole string is the four components joined.  A component accessor that
// drifted from the string it is meant to compose still passes the test above if
// the string itself is right, so it is checked against the components here
// rather than only against the macro literals.
TEST(IrcituiCoreVersion, ComponentsComposeTheVersionString) {
    const std::string composed = std::string("IRCITUI ") + IrcitUiLibVersionMajor() + "." +
                                 IrcitUiLibVersionMinor() + "." + IrcitUiLibVersionPatch() + "." +
                                 IrcitUiLibVersionInternal();
    ASSERT_NE(IrcitUiLibVersion(), nullptr);
    EXPECT_EQ(std::string(IrcitUiLibVersion()), composed);
}

// Every component accessor must yield the macro it names.  Without this the
// composition test above stays green if two accessors are swapped for each
// other -- minor and patch are both single digits at this version.
TEST(IrcituiCoreVersion, EachComponentMatchesItsMacro) {
    EXPECT_STREQ(STRINGISE(IRCITUI_MAJOR), IrcitUiLibVersionMajor());
    EXPECT_STREQ(STRINGISE(IRCITUI_MINOR), IrcitUiLibVersionMinor());
    EXPECT_STREQ(STRINGISE(IRCITUI_PATCH), IrcitUiLibVersionPatch());
    EXPECT_STREQ(STRINGISE(IRCITUI_INTERNAL), IrcitUiLibVersionInternal());
}
