cmake_minimum_required(VERSION 3.25)

macro(getVersionPatchLevel name)
  set(VERSION_REGEX "^#define[ \t]+${name}[ \t]+([^ \t\r\n]+)[ \t\r]*$")
  file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/src/include/version.h"
       VERSION_BIT REGEX ${VERSION_REGEX})
  string(REGEX REPLACE ${VERSION_REGEX} "\\1" ${name} "${VERSION_BIT}")
endmacro()

getVersionPatchLevel(IRCITUI_MAJOR)
getVersionPatchLevel(IRCITUI_MINOR)
getVersionPatchLevel(IRCITUI_PATCH)
set(IRCITUI_VERSION "${IRCITUI_MAJOR}.${IRCITUI_MINOR}.${IRCITUI_PATCH}")
message(STATUS "Detected IRCITUI version: ${IRCITUI_VERSION}")

project(ircituilib VERSION ${IRCITUI_VERSION} LANGUAGES C)

list(PREPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake-modules)
include(ircitui_build_options)

option(_PACKAGE_TESTS "Enable test targets" OFF)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(IRCITUI_USE_FAKE_CURSES "Use bundled fake curses backend for CI/unit tests" OFF)
option(IRCITUI_BUILD_EXAMPLE "Build ncurses example" ON)

if(NOT IRCITUI_USE_FAKE_CURSES)
  # The library uses wide-character curses entry points: waddnwstr, wadd_wch, setcchar.
  set(CURSES_NEED_WIDE TRUE)
  set(CURSES_NEED_NCURSES TRUE)
  find_package(Curses REQUIRED)
  if(NOT CURSES_LIBRARIES MATCHES "ncursesw")
    message(FATAL_ERROR
      "FindCurses resolved to non-wide curses (${CURSES_LIBRARIES}). ircituilib requires "
      "ncursesw; delete the build directory to clear the cached CURSES_LIBRARIES.")
  endif()
  set(IRCITUI_CURSES_INCLUDES ${CURSES_INCLUDE_DIRS})
  set(IRCITUI_CURSES_LIBS ${CURSES_LIBRARIES})
else()
  set(IRCITUI_CURSES_INCLUDES ${PROJECT_SOURCE_DIR}/tests/fake_ncurses)
  # The shim's instrumentation counters need one object, not a copy per
  # translation unit.
  add_library(ircitui_fake_curses STATIC ${PROJECT_SOURCE_DIR}/tests/fake_ncurses/fake_ncurses.c)
  target_include_directories(ircitui_fake_curses PUBLIC ${IRCITUI_CURSES_INCLUDES})
  ircitui_apply_build_options(ircitui_fake_curses)
  set(IRCITUI_CURSES_LIBS ircitui_fake_curses)
endif()

find_package(PNG REQUIRED)

# ── Dependency resolution: uflib, then ufsrvrxlib ───────────────────────────
#
# The chain is ircituilib -> {uflib, ufsrvrxlib} and ufsrvrxlib -> uflib. uflib is the
# leaf, so it is resolved first, and whatever it resolves to is what ufsrvrxlib uses.
#
# Both go through the same three stages, in this order:
#
#   1. an in-source checkout at packages/<name>/   a copy carried in the tree
#   2. the system install                          pkg-config
#   3. a network fetch                             IRCITUI_<NAME>_SOURCE
#
set(IRCITUI_UFLIB_SOURCE "https://github.com/unfacd/uflib" CACHE STRING
    "Where to fetch uflib from when it is neither in packages/uflib nor installed. A git URL, or a path to a local checkout.")
set(IRCITUI_UFSRVRXLIB_SOURCE "https://github.com/unfacd/ufsrvrxlib" CACHE STRING
    "Where to fetch ufsrvrxlib from when it is neither in packages/ufsrvrxlib nor installed. A git URL, or a path to a local checkout.")
set(IRCITUI_DEP_GIT_TAG "master" CACHE STRING
    "Branch or tag checked out when a dependency is fetched. Pin it for a reproducible build.")

find_package(PkgConfig REQUIRED)
include(FetchContent)

# Resolve one dependency through the three stages. On return <name>_ORIGIN is SOURCE, SYSTEM or FETCH, and <name>_TARGET names what to link.
macro(ircitui_resolve_dependency name pkgname)
  string(TOUPPER "${name}" _dep_upper)
  set(_dep_src "${IRCITUI_${_dep_upper}_SOURCE}")
  set(${name}_ORIGIN "")
  set(${name}_TARGET "")

  set(_dep_saved_tests   ${_PACKAGE_TESTS})
  set(_dep_saved_install ${_PACKAGE_INSTALL})
  set(_PACKAGE_TESTS   OFF CACHE BOOL "" FORCE)
  set(_PACKAGE_INSTALL OFF CACHE BOOL "" FORCE)

  if(EXISTS "${PROJECT_SOURCE_DIR}/packages/${name}/CMakeLists.txt")
    message(STATUS "${name}: in-source checkout at packages/${name}")
    add_subdirectory("${PROJECT_SOURCE_DIR}/packages/${name}"
                     "${PROJECT_BINARY_DIR}/packages/${name}")
    set(${name}_ORIGIN SOURCE)
  else()
    pkg_check_modules(${pkgname} IMPORTED_TARGET ${name})
    if(${pkgname}_FOUND)
      message(STATUS "${name}: system install (pkg-config, ${${pkgname}_VERSION})")
      set(${name}_ORIGIN SYSTEM)
    elseif(IS_DIRECTORY "${_dep_src}")
      message(STATUS "${name}: fetching from the local path ${_dep_src}")
      FetchContent_Declare(${name} SOURCE_DIR "${_dep_src}")
      FetchContent_MakeAvailable(${name})
      set(${name}_ORIGIN FETCH)
    else()
      message(STATUS "${name}: not installed; fetching ${_dep_src} (${IRCITUI_DEP_GIT_TAG})")
      FetchContent_Declare(${name}
        GIT_REPOSITORY "${_dep_src}"
        GIT_TAG        "${IRCITUI_DEP_GIT_TAG}"
        GIT_SHALLOW    TRUE)
      FetchContent_MakeAvailable(${name})
      set(${name}_ORIGIN FETCH)
    endif()
  endif()

  set(_PACKAGE_TESTS   ${_dep_saved_tests}   CACHE BOOL "" FORCE)
  set(_PACKAGE_INSTALL ${_dep_saved_install} CACHE BOOL "" FORCE)

  # The alias each library defines for itself when it is not the root project. Named
  # here rather than referenced directly so a rename upstream fails at configure with
  # the origin in the message, not at link with an unresolved target.
  if(${name}_ORIGIN STREQUAL "SYSTEM")
    set(${name}_TARGET "PkgConfig::${pkgname}")
  elseif(TARGET ${name}::${name})
    set(${name}_TARGET "${name}::${name}")
  else()
    message(FATAL_ERROR
      "${name} was resolved from ${${name}_ORIGIN} but defines no ${name}::${name} target.")
  endif()
endmacro()

ircitui_resolve_dependency(uflib UFLIB)

# Two copies of uflib in one link is the one combination worth refusing: a system
# ufsrvrxlib was built against the system uflib, so pairing it with a uflib built here
# resolves uflib's symbols from whichever copy the linker reaches first.
ircitui_resolve_dependency(ufsrvrxlib UFSRVRXLIB)

if(ufsrvrxlib_ORIGIN STREQUAL "SYSTEM" AND NOT uflib_ORIGIN STREQUAL "SYSTEM")
  message(FATAL_ERROR
    "ufsrvrxlib came from the system install but uflib came from ${uflib_ORIGIN}.\n"
    "The system ufsrvrxlib was built against the system uflib, so the two cannot be "
    "mixed without two copies of uflib in the link.\n"
    "Install uflib, or supply both through packages/ or IRCITUI_UFLIB_SOURCE and "
    "IRCITUI_UFSRVRXLIB_SOURCE.")
endif()

file(MAKE_DIRECTORY ${PROJECT_BINARY_DIR}/src)
add_custom_target(ircitui_version
  COMMAND ${CMAKE_COMMAND} -E make_directory ${PROJECT_BINARY_DIR}/src
  COMMAND chmod 755 ${PROJECT_SOURCE_DIR}/src/version.c.sh
  COMMAND ${PROJECT_SOURCE_DIR}/src/version.c.sh
  WORKING_DIRECTORY ${PROJECT_BINARY_DIR}/src
  BYPRODUCTS ${PROJECT_BINARY_DIR}/src/version.c
  COMMENT "Generating version.c with a fresh compilation timestamp"
  VERBATIM)

set_source_files_properties(${PROJECT_BINARY_DIR}/src/version.c PROPERTIES GENERATED TRUE)

find_library(IRCITUI_BLOCKS_RUNTIME REQUIRED NAMES BlocksRuntime)

add_library(ircituilib STATIC
  src/main_looper.c
  src/ircitui_context.c
  src/ircitui_edit_text.c
  src/ui_engine.c
  src/ui_image.c
  src/ui_verify.c
  src/ui_theme.c
  src/ui_binding.c
  src/ui_dictionary.c
  src/ui_event_dispatcher.c
  src/ui_async_bridge.c
  src/ui_watchdog_alert.c
  src/ui_snackbar.c
  src/ui_defaults_router.c
  src/ui_log.c
  src/ui_palette.c
  src/window_driver/ircitui_window_driver.c
  src/window_driver/ircitui_window_registry.c
  src/window_driver/adapters/ansi/ircitui_window_adapter_ansi.c
  src/window_driver/adapters/ncurses/ircitui_window_adapter_ncurses.c
  src/ui_conversation.c
  src/generated/ui_generated.c
  ${PROJECT_BINARY_DIR}/src/version.c
  src/livedata/ircitui_livedata.c)
add_dependencies(ircituilib ircitui_version)

target_compile_definitions(ircituilib PRIVATE _GNU_SOURCE)

set_target_properties(ircituilib PROPERTIES
  PREFIX ""
  OUTPUT_NAME ircituilib)

target_include_directories(ircituilib
  PUBLIC  ${PROJECT_SOURCE_DIR}/include
  PRIVATE
          ${PROJECT_SOURCE_DIR}/src/include
          ${IRCITUI_CURSES_INCLUDES})
target_link_libraries(ircituilib PUBLIC PNG::PNG ${IRCITUI_CURSES_LIBS} ${ufsrvrxlib_TARGET} ${uflib_TARGET} ${IRCITUI_BLOCKS_RUNTIME})

target_compile_options(ircituilib PRIVATE -Wall -Wextra -Wpedantic -Werror)
ircitui_apply_build_options(ircituilib)

if(IRCITUI_BUILD_EXAMPLE)
  add_executable(ircitui_demo src/examples/ui_main.c)

  pkg_check_modules(LIBCURL REQUIRED IMPORTED_TARGET libcurl)
  target_link_libraries(ircitui_demo PRIVATE ircituilib PkgConfig::LIBCURL)
  target_include_directories(ircitui_demo PRIVATE
    ${PROJECT_SOURCE_DIR}/src/include
    ${IRCITUI_CURSES_INCLUDES})
  ircitui_apply_build_options(ircitui_demo)

add_executable(ircitui_conversation_demo src/examples/conversation/conversation_demo.c)
target_link_libraries(ircitui_conversation_demo PRIVATE ircituilib)
target_compile_definitions(ircitui_conversation_demo PRIVATE _GNU_SOURCE)
ircitui_apply_build_options(ircitui_conversation_demo)

# The interactive chat, on the framework's own loop so the input fast lane runs.
add_executable(ircitui_interactive_msg_demo src/examples/interactive_msg_demo.c)
target_link_libraries(ircitui_interactive_msg_demo PRIVATE ircituilib)
target_compile_definitions(ircitui_interactive_msg_demo PRIVATE _GNU_SOURCE)
ircitui_apply_build_options(ircitui_interactive_msg_demo)

add_executable(ircitui_window_demo src/examples/window_demo.c)
  target_link_libraries(ircitui_window_demo PRIVATE ircituilib)
  target_compile_definitions(ircitui_window_demo PRIVATE _GNU_SOURCE)
  ircitui_apply_build_options(ircitui_window_demo)
endif()

# ── The conversation streaming server (opt-in) ──────────────────────────────
option(IRCITUI_BUILD_CONVERSATION_STREAM "Build the conversation streaming test server" OFF)

if(IRCITUI_BUILD_CONVERSATION_STREAM)
  find_package(Threads REQUIRED)
  add_executable(conversation_stream_server
    src/examples/conversation_streaming/conversation_stream_server.c)
  target_include_directories(conversation_stream_server PRIVATE
    ${PROJECT_SOURCE_DIR}/src/examples/conversation_streaming)
  target_compile_definitions(conversation_stream_server PRIVATE _GNU_SOURCE)
  target_link_libraries(conversation_stream_server PRIVATE Threads::Threads)
  ircitui_apply_build_options(conversation_stream_server)

  add_executable(streaming_msg_client_demo
    src/examples/conversation_streaming/streaming_msg_client_demo.c)
  target_include_directories(streaming_msg_client_demo PRIVATE
    ${PROJECT_SOURCE_DIR}/src/examples/conversation_streaming)
  target_compile_definitions(streaming_msg_client_demo PRIVATE _GNU_SOURCE)
  target_link_libraries(streaming_msg_client_demo PRIVATE ircituilib Threads::Threads)
  ircitui_apply_build_options(streaming_msg_client_demo)
endif()

# ── The MSP streaming test harness (opt-in) ─────────────────────────────────
option(IRCITUI_BUILD_HARNESS "Build the msp streaming test harness (the simulation server)" OFF)

if(IRCITUI_BUILD_HARNESS)
  # The simulation server.  It links NOTHING from this project -- no ircituilib, no curses,
  # no PNG
  find_package(Threads REQUIRED)
  add_executable(msp_server src/examples/stream_server/msp_server.c)
  target_include_directories(msp_server PRIVATE ${PROJECT_SOURCE_DIR}/src/examples/stream_server)
  target_compile_definitions(msp_server PRIVATE _GNU_SOURCE)
  target_link_libraries(msp_server PRIVATE Threads::Threads m)
  ircitui_apply_build_options(msp_server)

  add_executable(ircitui_harness_client
    src/examples/stream_server/client/harness_client.c)
  target_include_directories(ircitui_harness_client PRIVATE ${PROJECT_SOURCE_DIR}/src/examples/stream_server)
  target_compile_definitions(ircitui_harness_client PRIVATE _GNU_SOURCE)
  target_link_libraries(ircitui_harness_client PRIVATE ircituilib Threads::Threads)
  ircitui_apply_build_options(ircitui_harness_client)
endif()

# ── Tests (gtest) ───────────────────────────────────────────────────────────
if(_PACKAGE_TESTS)
  enable_language(CXX)
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)

  include(FetchContent)
  FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG release-1.12.0)
  set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)

  enable_testing()
  add_subdirectory(tests)
endif()
