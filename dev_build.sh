#!/usr/bin/env bash
# dev_build.sh — configure (+ optional tests) → build.
#
# Fast development loop: configure the package with the chosen CMake preset and
# build it.  No install, verify, or push — this is the inner build-only loop.
#
# Usage:
#   dev_build.sh [PRESET] [--fresh] [--test] [--jobs N] [--dry-run] [--help]
#
# PRESET (default: dev) selects the CMake preset and names build/<PRESET>
# (or build/<PRESET>-test when --test is given, so a tests-ON build never
# disturbs the tests-OFF build directory).
#   --fresh  fresh configure (cmake --fresh): full clean rebuild
#   --test   enable gtest (-D_PACKAGE_TESTS=ON) in build/<PRESET>-test
#
# NOTE: --test configures and builds the test targets but does NOT run them.
# Run them with:  ctest --test-dir build/<PRESET>-test --output-on-failure
#
# Exit status: 0 on success; non-zero if any stage fails (set -euo pipefail).
#
# NOTE: this script builds the tree it is invoked from.  packages/ircituilib in
# the ircit host repo is a subtree copy carrying its own sources, so a run there
# builds that copy and reports success even when the work you are looking for
# lives in the standalone repo.  The banner therefore prints the absolute source
# tree, and a subtree relationship is called out explicitly, so the wrong tree
# is visible before the build rather than after it.
set -euo pipefail

# ── Defaults ────────────────────────────────────────────────────────────────

PRESET="dev"
JOBS="$(nproc)"
FRESH=0
RUN_TESTS=0
DRY_RUN=0

usage() {
    cat <<EOF
Usage: $0 [PRESET] [options]

Configure and build (no install / verify / push).

  $0 dev              # configure + build the 'dev' preset
  $0 dev --test       # same, with gtest enabled (-D_PACKAGE_TESTS=ON)

Options:
  --preset NAME  Explicit preset (same as the positional PRESET)
  --jobs N       Parallel build jobs (default: \$(nproc))
  --fresh        Fresh configure (cmake --fresh): full clean rebuild
  --test         Enable gtest (-D_PACKAGE_TESTS=ON) in build/<preset>-test
  --dry-run      Print commands without executing them
  -h, --help     Show this help
EOF
}

# ── Argument parsing ────────────────────────────────────────────────────────

while [ $# -gt 0 ]; do
    case "$1" in
        --preset)  PRESET="$2"; shift 2 ;;
        --jobs)    JOBS="$2"; shift 2 ;;
        --fresh)   FRESH=1; shift ;;
        --test)    RUN_TESTS=1; shift ;;
        --dry-run) DRY_RUN=1; shift ;;
        -h|--help) usage; exit 0 ;;
        --*)       echo "error: unknown option '$1'" >&2; usage >&2; exit 2 ;;
        *)         PRESET="$1"; shift ;;
    esac
done

if [ "$RUN_TESTS" -eq 1 ]; then
    BUILD_DIR="build/${PRESET}-test"
else
    BUILD_DIR="build/${PRESET}"
fi

# ── Helpers ─────────────────────────────────────────────────────────────────

step() { printf '\n==> %s\n' "$*"; }
run() {
    printf '+ %s\n' "$*"
    if [ "$DRY_RUN" -eq 0 ]; then
        "$@"
    fi
}

# ── Which tree is this? ─────────────────────────────────────────────────────
# BUILD_DIR is relative to the working directory, so a run from a subtree copy
# builds that copy.  Resolve the tree up front and name it: "done" printed at
# the end of a build of the wrong tree reads exactly like a build of the right
# one, which is how a run in packages/ircituilib can pass for a run here.

SRC_DIR="$(pwd -P)"

SUBTREE_OF=""
if command -v git >/dev/null 2>&1; then
    repo_root="$(git -C "$SRC_DIR" rev-parse --show-toplevel 2>/dev/null || true)"
    if [ -n "$repo_root" ] && [ "$repo_root" != "$SRC_DIR" ]; then
        SUBTREE_OF="$repo_root"
    fi
fi

printf '==> dev_build: preset=%s build=%s tests=%s fresh=%s jobs=%s\n' \
    "$PRESET" "$BUILD_DIR" \
    "$([ "$RUN_TESTS" -eq 1 ] && echo ON || echo OFF)" \
    "$([ "$FRESH" -eq 1 ] && echo yes || echo no)" \
    "$JOBS"
printf '==> source tree: %s\n' "$SRC_DIR"
if [ -n "$SUBTREE_OF" ]; then
    printf '==> NOTE: that tree is a SUBTREE of %s\n' "$SUBTREE_OF"
    printf '==>       It carries its own copy of the sources. Work done in the\n'
    printf '==>       standalone repo is NOT visible here until this copy is re-synced.\n'
fi

# ── 1. Configure ────────────────────────────────────────────────────────────

step "[1/2] Configure (preset=$PRESET)"
configure_args=(--preset "$PRESET")
if [ "$FRESH" -eq 1 ]; then
    configure_args=(--fresh "${configure_args[@]}")
fi
if [ "$RUN_TESTS" -eq 1 ]; then
    configure_args+=(-D_PACKAGE_TESTS=ON)
fi
# -B pins the build dir (a test build uses build/<preset>-test rather than the
# preset's own build/<preset>), so the configure and build below agree.
run cmake "${configure_args[@]}" -B "$BUILD_DIR"

# ── 2. Build ────────────────────────────────────────────────────────────────

step "[2/2] Build ($JOBS jobs)"
run cmake --build "$BUILD_DIR" -j"$JOBS"

printf '\n==> done\n'
printf '==> binary: %s/%s/ircitui_demo\n' "$SRC_DIR" "$BUILD_DIR"
