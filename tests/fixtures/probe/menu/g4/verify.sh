#!/bin/bash
# --verify-layout over the 12 sizes, run from a scratch CWD so the verifier's
# layout-preview.ppm does not land in the repo.
bin="$1"; out="$2"; err="$3"
cd /tmp/irctui-menu/g4/scratch || exit 1
: > "$out"; : > "$err"
for s in 80x24 100x30 120x40 60x20 200x50 40x15 132x43 90x25 110x35 72x22 160x48 50x18; do
  W=${s%x*}; H=${s#*x}
  echo "### $s" >> "$out"; echo "### $s" >> "$err"
  "$bin" --verify-layout --cols="$W" --rows="$H" >> "$out" 2>> "$err"
done
