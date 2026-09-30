#
# tests/CMakeLists.txt — gtest harness for ircituilib
#
# Mirrors the host repo's tests/CMakeLists.txt.  Provides:
#   gtest_shared_main      OBJECT library linked into every test executable
#   add_c_test()           one-call test registration (executable + CTest)
#
# Only reached when _PACKAGE_TESTS=ON; see the root CMakeLists.txt.
#


add_library(gtest_shared_main OBJECT main.cpp)
target_link_libraries(gtest_shared_main PUBLIC GTest::gtest GTest::gtest_main)
ircitui_apply_build_options(gtest_shared_main)


function(add_c_test)
    set(options "")
    set(oneValueArgs NAME TEST_SOURCE)
    set(multiValueArgs C_SOURCES INCLUDE_DIRS LIBRARIES)
    cmake_parse_arguments(TEST "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    add_executable(${TEST_NAME}
        ${TEST_TEST_SOURCE}
        $<TARGET_OBJECTS:gtest_shared_main>
        ${TEST_C_SOURCES}
    )
    set_target_properties(${TEST_NAME} PROPERTIES LINKER_LANGUAGE CXX)
    target_include_directories(${TEST_NAME} PRIVATE ${TEST_INCLUDE_DIRS})
    target_link_libraries(${TEST_NAME} PRIVATE
        GTest::gtest GTest::gtest_main stdc++ m
        ${TEST_LIBRARIES}
    )
    ircitui_apply_build_options(${TEST_NAME})
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endfunction()


add_subdirectory(ircitui_core)

# No gtest equivalent, so it stays a plain CTest test rather than going through
# add_c_test() (which is for compiled targets).
#
# test_generator.py computes ROOT = Path(__file__).resolve().parents[1] and then
# ROOT/'tools/gen_ui.py', so it must stay directly in tests/ -- moving it into
# ircitui_core/ would silently break that path.
add_test(NAME ircitui_core_generator_tests
         COMMAND ${CMAKE_COMMAND} -E env python3
                 ${PROJECT_SOURCE_DIR}/tests/test_generator.py)
