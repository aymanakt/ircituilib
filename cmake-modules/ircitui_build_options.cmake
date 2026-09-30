# cmake-modules/ircitui_build_options.cmake
#
# Build-options module for ircituilib, mirroring the host repo's
# cmake-modules/ircit_build_options.cmake.  Like ircit this is C-only, so it is
# the trimmed-down equivalent of the shared ufsrv build-options module.
#
# Every identifier is namespaced ircitui_* rather than copied verbatim from the
# host.  Reason: CMake's option() is a silent no-op when the cache variable
# already exists, and add_library() is guarded by if(NOT TARGET).  Under
# add_subdirectory() a verbatim copy would therefore let the host's sanitizer
# settings and host's INTERFACE target win, and this package's build options
# would vanish with no error and no warning.
#
# Contract:
#   * include() this AFTER project() and BEFORE creating any target.
#   * Call  ircitui_apply_build_options(<tgt>)  on EVERY target (the ircitui
#     static lib and the ircitui_demo executable) so the sanitizer/coverage
#     compile+link flags land.
#
#   cmake --preset asan && cmake --build --preset asan

include_guard(GLOBAL)

# Package root, so the preset guardrail below always validates THIS package's
# CMakePresets.json even under add_subdirectory() (where CMAKE_SOURCE_DIR is the
# host's source dir).  CMAKE_CURRENT_LIST_DIR here is <pkg>/cmake-modules.
get_filename_component(_ircitui_pkg_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

# ── Sanitizer + coverage options ────────────────────────────────────────────
# Primary sanitizers — MUTUALLY EXCLUSIVE (address/memory/thread cannot combine).
# ASan + UBSan + LSan are ON by default: every build is sanitized unless a
# preset (or -D on a throwaway configure) explicitly disables them.
option(IRCITUI_BUILD_WITH_ASAN     "Build with AddressSanitizer instrumentation" ON)
option(IRCITUI_BUILD_WITH_MSAN     "Build with MemorySanitizer instrumentation"  OFF)
option(IRCITUI_BUILD_WITH_TSAN     "Build with ThreadSanitizer instrumentation"  OFF)
# Additive sanitizers — combine with any primary (or stand alone).
option(IRCITUI_BUILD_WITH_UBSAN    "Build with UndefinedBehaviorSanitizer"       ON)
option(IRCITUI_BUILD_WITH_LSAN     "Build with LeakSanitizer (implied by ASan)"  ON)
option(IRCITUI_BUILD_WITH_COVERAGE "Build with gcov source coverage"             OFF)

# Configure-time mutual-exclusivity check (mirrors the Python preset guardrail).
set(_ircitui_primary_count 0)
foreach(_s ASAN MSAN TSAN)
    if(IRCITUI_BUILD_WITH_${_s})
        math(EXPR _ircitui_primary_count "${_ircitui_primary_count}+1")
    endif()
endforeach()
if(_ircitui_primary_count GREATER 1)
    message(FATAL_ERROR
        "Enable at most ONE primary sanitizer (IRCITUI_BUILD_WITH_{ASAN,MSAN,TSAN}) — "
        "address/memory/thread are mutually exclusive. UBSan/LSan combine with any of them.")
endif()

# ── Accumulate all enabled sanitizers into one -fsanitize= ──────────────────
set(_fs "")                 # -fsanitize= check names
set(_san_extra "")          # extra sanitizer flags (compile + link)
set(_san_compile_only "")
set(_san_link_only "")
set(_ircitui_active_sanitizers "")

if(IRCITUI_BUILD_WITH_ASAN)
    list(APPEND _fs address)
    list(APPEND _ircitui_active_sanitizers "ASan")
elseif(IRCITUI_BUILD_WITH_MSAN)
    list(APPEND _fs memory)
    list(APPEND _san_extra -fsanitize-memory-track-origins=2)
    list(APPEND _san_compile_only -fPIE)
    list(APPEND _san_link_only -pie)
    list(APPEND _ircitui_active_sanitizers "MSan")
elseif(IRCITUI_BUILD_WITH_TSAN)
    list(APPEND _fs thread)
    list(APPEND _ircitui_active_sanitizers "TSan")
endif()
if(IRCITUI_BUILD_WITH_UBSAN)
    list(APPEND _fs undefined)
    list(APPEND _ircitui_active_sanitizers "UBSan")
endif()
if(IRCITUI_BUILD_WITH_LSAN)
    list(APPEND _fs leak)
    list(APPEND _ircitui_active_sanitizers "LSan")
endif()

set(_san_core "")
if(_fs)
    list(JOIN _fs "," _fs_csv)
    set(_san_core -fno-omit-frame-pointer -fsanitize=${_fs_csv} ${_san_extra})
endif()

set(_cov_flags "")
if(IRCITUI_BUILD_WITH_COVERAGE)
    set(_cov_flags -fprofile-arcs -ftest-coverage)
endif()

# ── Objective-C blocks ──────────────────────────────────────────────────────
# uflib's headers use a blocks construct -- utils_str.h:78 declares
#   char *strbufdup_nullable(const char *s, size_t n, char *(^on_null)(void));
# -- and that header is reachable from <ufsrvrxlib/ufsrv_scheduler/ufsrv_scheduler.h>,
# which this library includes in order to create its scheduler pool.  Clang
# parses the declaration only with -fblocks.
#
# GCC has no blocks support and rejects the flag outright ("unrecognized
# command-line option: -fblocks"), so it stays conditional rather than
# unconditional: on GCC the failure stays the honest one from the header itself
# rather than becoming a complaint about a flag.  Either way, any translation
# unit that reaches those headers is clang-only.
#
# COMPILE-only.  Nothing here *creates* a block, so no BlocksRuntime link
# dependency is added.  If a block literal is ever written, that changes.
set(_blocks_flags "")
if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    set(_blocks_flags -fblocks)
endif()

# ── The single INTERFACE target every target consumes ───────────────────────
if(NOT TARGET ircitui_build_options)
    add_library(ircitui_build_options INTERFACE)
    add_library(ircitui::build_options ALIAS ircitui_build_options)
    target_compile_options(ircitui_build_options INTERFACE
        ${_san_core} ${_san_compile_only} ${_cov_flags} ${_blocks_flags})
    target_link_options(ircitui_build_options INTERFACE
        ${_san_core} ${_san_link_only} ${_cov_flags})
endif()

# ── Apply helper — call on EVERY target ─────────────────────────────────────
# Build-only link ($<BUILD_INTERFACE:>) so these flags never leak into an
# install(EXPORT) set.
function(ircitui_apply_build_options tgt)
    target_link_libraries(${tgt} PRIVATE $<BUILD_INTERFACE:ircitui_build_options>)
endfunction()

# ── Build digest — echoed at configure time ─────────────────────────────────
if(_ircitui_active_sanitizers)
    list(JOIN _ircitui_active_sanitizers "+" _ircitui_san_summary)
else()
    set(_ircitui_san_summary "none")
endif()
message(STATUS "[ircitui] build options:  sanitizers=${_ircitui_san_summary}  coverage=${IRCITUI_BUILD_WITH_COVERAGE}")

# ── Preset guardrail — no preset may enable more than one primary sanitizer ──
option(IRCITUI_VALIDATE_PRESETS "Validate CMakePresets sanitizer mutual-exclusivity" ON)
find_program(IRCITUI_PYTHON3 NAMES python3 python)
if(IRCITUI_VALIDATE_PRESETS AND IRCITUI_PYTHON3 AND EXISTS "${_ircitui_pkg_root}/CMakePresets.json")
    execute_process(
        COMMAND "${IRCITUI_PYTHON3}" "${CMAKE_CURRENT_LIST_DIR}/check_preset_exclusivity.py"
                "${_ircitui_pkg_root}/CMakePresets.json"
        RESULT_VARIABLE _ircitui_presets_rc
        OUTPUT_VARIABLE _ircitui_presets_msg
        ERROR_VARIABLE  _ircitui_presets_msg)
    if(NOT _ircitui_presets_rc EQUAL 0)
        message(FATAL_ERROR "ircitui preset guardrail — ${_ircitui_presets_msg}")
    endif()
    if(NOT TARGET ircitui-check-presets)          # on-demand / CI entry point
        add_custom_target(ircitui-check-presets
            COMMAND "${IRCITUI_PYTHON3}" "${CMAKE_CURRENT_LIST_DIR}/check_preset_exclusivity.py"
                    "${_ircitui_pkg_root}/CMakePresets.json"
            COMMENT "Validating CMakePresets sanitizer mutual-exclusivity"
            VERBATIM)
    endif()
endif()
