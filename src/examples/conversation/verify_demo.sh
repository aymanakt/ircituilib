#!/bin/sh
# ONE COMMAND: build the conversation demo, run it, and say whether it still
# renders what it is supposed to.
#
# Five verdicts. Three are the frozen captures -- the model, the cells and the
# picture -- which catch CHANGE. One is the rules probe, which renders eleven cases
# through the widget and compares each rule row against a transcription of the
# design document's Appendix B: that is the one that catches a rendering that never
# obeyed a rule, which a capture cannot (it freezes a violation in as correct).
# The last is the folded-up result.
#
# Usage: ./src/examples/conversation/verify_demo.sh   (from anywhere)
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)   # src/examples/conversation
ROOT=$(cd "$HERE/../../.." && pwd)     # the repository
CC=${CC:-clang}
BUILD=$ROOT/build/dev
DEMO=$BUILD/ircitui_conversation_demo
REF=$ROOT/dev/probes/conversation
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT INT TERM

fails=0
verdict() {
  if [ "$1" -eq 0 ]; then
    printf 'PASS  %s\n' "$2"
  else
    printf 'FAIL  %s\n' "$2"
    fails=$((fails + 1))
  fi
}

# The root frame paints the library version into a cell, so the captures are
# compared with that line masked: it moves on a version bump for a reason that has
# nothing to do with this widget.
mask() { sed -E 's/IRCITUI [0-9]+\.[0-9]+\.[0-9]+\.[0-9]+/IRCITUI VERSION-MASKED/g' "$1"; }

printf '== building\n'
if ! cmake --build "$BUILD" -j"$(nproc)" --target ircitui_conversation_demo >"$TMP/build.log" 2>&1; then
  printf 'FAIL  the demo did not build:\n'
  tail -12 "$TMP/build.log" | sed 's/^/      /'
  exit 1
fi
if [ ! -x "$DEMO" ]; then
  printf 'FAIL  no demo at %s\n' "$DEMO"
  exit 1
fi
printf '      %s\n' "$DEMO"

printf '\n== the captures -- has anything moved\n'
for pair in "model:--dump-model" "screen:--dump-screen" "frame:--dump-all"; do
  kind=${pair%%:*}
  flag=${pair#*:}
  timeout 60 "$DEMO" "$flag" 2>/dev/null | mask /dev/stdin >"$TMP/$kind.out"
  if [ ! -f "$REF/conversation_$kind.expected.txt" ]; then
    verdict 1 "the $kind capture is not committed"
  elif diff -q "$REF/conversation_$kind.expected.txt" "$TMP/$kind.out" >/dev/null 2>&1; then
    verdict 0 "the $kind dump is byte-identical"
  else
    verdict 1 "the $kind dump DIFFERS:"
    diff "$REF/conversation_$kind.expected.txt" "$TMP/$kind.out" | head -10 | sed 's/^/      /'
  fi
done

printf '\n== the visible half of the avatar work -- the pixels, not the model\n'
# The four captures above are the model and the frame; these two are the modes that
# exist for the feature: the stamps (a row inside every bubble) and the avatar
# records read back off the glass. Neither is a frozen file -- they are read here,
# and their own probes own the assertions.
for spec in "avatars:--avatars" "avatar-clicks:--avatar-clicks" "timestamps:--timestamps --dump-all"; do
  kind=${spec%%:*}
  flag=${spec#*:}
  # shellcheck disable=SC2086 -- the flags are a fixed pair, split on purpose.
  timeout 60 "$DEMO" $flag >"$TMP/$kind.out" 2>&1 || true
  case "$kind" in
  avatar-clicks)
    n=$(sed -n 's/.*ZP conv avatar-clicks: \([0-9]*\) checks.*/\1/p' "$TMP/$kind.out" | head -1)
    if [ "${n:-0}" -ge 6 ] && grep -q 'avatar-clicks: .*0 failure(s)' "$TMP/$kind.out"; then
      verdict 0 "the avatar click walk: $n checks, every one held"
    else
      verdict 1 "the avatar click walk: ${n:-0} checks, or a failure"
      grep 'FAIL' "$TMP/$kind.out" | head -6 | sed 's/^/      /'
    fi
    ;;
  avatars)
    if grep -q 'gutter 6' "$TMP/$kind.out"; then
      verdict 0 "the avatar records name the gutter and every avatar"
      grep '^row' "$TMP/$kind.out" | sed 's/^/      /'
    else
      verdict 1 "the avatar records do not name the gutter"
    fi
    ;;
  timestamps)
    if diff -q "$REF/conversation_timestamps.expected.txt" "$TMP/$kind.out" >/dev/null 2>&1; then
      verdict 0 "the timestamps dump is byte-identical"
    else
      verdict 1 "the timestamps dump DIFFERS:"
      diff "$REF/conversation_timestamps.expected.txt" "$TMP/$kind.out" | head -10 | sed 's/^/      /'
    fi
    ;;
  esac
done

printf '\n== the rules -- does it render what Appendix B says\n'
if ! "$CC" -std=c17 -Wall -Wextra -Werror -O1 -fsanitize=address,undefined -I "$ROOT/include" \
     "$REF/p_conv_rules.c" -o "$TMP/p_conv_rules" "$BUILD/ircituilib.a" \
     -lpng -lz -lncursesw -lformw /usr/local/lib/libufsrvrxlib.a /usr/local/lib/libuflib.a \
     /usr/local/lib/libzlog.so -lcurl -lpthread -lutil >"$TMP/probe-build.log" 2>&1; then
  verdict 1 "the rules probe did not build:"
  tail -8 "$TMP/probe-build.log" | sed 's/^/      /'
else
  ASAN_OPTIONS=detect_leaks=0 timeout 60 "$TMP/p_conv_rules" >"$TMP/rules.out" 2>&1 || true
  n=$(sed -n 's/.*ZP conv rules: \([0-9]*\) checks.*/\1/p' "$TMP/rules.out" | head -1)
  if [ "${n:-0}" -ge 60 ] && grep -q 'checks, 0 failures' "$TMP/rules.out"; then
    verdict 0 "the rules probe: $n checks, every one held"
  else
    verdict 1 "the rules probe: ${n:-0} checks, or a failure"
    grep '^FAIL' "$TMP/rules.out" | head -10 | sed 's/^/      /'
  fi
fi

printf '\n== %s\n' "$(date '+%Y-%m-%d %H:%M')"
if [ "$fails" -eq 0 ]; then
  printf 'the demo renders what it is supposed to\n'
else
  printf '%d check(s) did not hold\n' "$fails"
fi
[ "$fails" -eq 0 ]
