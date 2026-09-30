# tests/ircitui_core/CMakeLists.txt
#
# The ircituilib core tests.  Each is registered with add_c_test() from
# ../CMakeLists.txt, which builds the executable and adds the CTest entry.
#
# The tests include only "ircituilib/ui.h", whose directory is a PUBLIC include dir
# on the ircituilib target, so linking ircituilib is enough -- no INCLUDE_DIRS.

add_c_test(
    NAME ircitui_core_api_tests
    TEST_SOURCE api_test.cpp
    LIBRARIES ircituilib
)

add_c_test(
    NAME ircitui_core_theme_tests
    TEST_SOURCE theme_test.cpp
    LIBRARIES ircituilib
)

add_c_test(
    NAME ircitui_core_lifecycle_tests
    TEST_SOURCE lifecycle_test.cpp
    LIBRARIES ircituilib
)

# The version globals come from the library's own version.c, so this needs only
# the library.  It does need src/include on the include path to compare the
# generated string against the checked-in version.h.
add_c_test(
    NAME ircitui_core_version_tests
    TEST_SOURCE version_test.cpp
    INCLUDE_DIRS ${PROJECT_SOURCE_DIR}/src/include
    LIBRARIES ircituilib
)

# The invalidation flags live in the private node state, so this needs
# src/include for the same reason the version test does. It needs nothing else:
# no window backend, and no fake-curses instrumentation -- every assertion is on
# a flag or a measured coordinate, which IrcitUiLayout produces without a
# terminal. The performance claim those flags exist to make needs call counting
# and is not asserted here.
add_c_test(
    NAME ircitui_core_dirty_tests
    TEST_SOURCE dirty_test.cpp
    INCLUDE_DIRS ${PROJECT_SOURCE_DIR}/src/include
    LIBRARIES ircituilib
)

# The LiveData subsystem. Needs src/include for the same reason dirty_test does:
# the delivery gate reads the node's flags and the hook tests assert on its
# interactions_user slot, and both live in the private node definition. No window
# backend -- every assertion is on a callback count, a value or a flag, none of
# which needs a terminal.
add_c_test(
    NAME ircitui_core_livedata_tests
    TEST_SOURCE livedata_test.cpp
    INCLUDE_DIRS ${PROJECT_SOURCE_DIR}/src/include
    LIBRARIES ircituilib
)

# The same model, asserted on curses calls rather than on flags: how many nodes
# painted, whether any window was rebuilt, and in what order windows were
# refreshed. Only meaningful under the fake-curses build, and gated rather than
# merely skipped: with real curses IrcitUiRealize leaves every node without a
# window, IrcitUiDraw returns at its guard for all of them, and every assertion
# here would pass while proving nothing. The shim comes in through ircituilib's
# own PUBLIC link to IRCITUI_CURSES_LIBS, so no include path is named here.
if(IRCITUI_USE_FAKE_CURSES)
    add_c_test(
        NAME ircitui_core_dirty_paint_tests
        TEST_SOURCE dirty_paint_test.cpp
        INCLUDE_DIRS ${PROJECT_SOURCE_DIR}/src/include
        LIBRARIES ircituilib
    )
endif()
