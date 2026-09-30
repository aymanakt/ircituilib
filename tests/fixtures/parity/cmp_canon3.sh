#!/bin/bash
# usage: cmp_set3.sh <driver> <cwd> <seq>...
cd /tmp/parity
drv="$1"; cwd="$2"; shift 2
pass=0; fail=0
for s in "$@"; do
  python3 "$drv" /tmp/parity/base/ui_demo "$s" "$cwd" > /tmp/parity/s_base.txt 2>/tmp/parity/s_base.err; rcb=$?
  python3 "$drv" /tmp/parity/canon/ircitui_demo   "$s" "$cwd" > /tmp/parity/s_v3.txt   2>/tmp/parity/s_v3.err; rcv=$?
  if [ $rcb -ne 0 ] || [ $rcv -ne 0 ]; then
    fail=$((fail+1)); printf "  ERROR   %-16s rc base=%s v3=%s\n" "$s" "$rcb" "$rcv"
    sed -n '1,3p' /tmp/parity/s_base.err | sed 's/^/          base: /'
    sed -n '1,3p' /tmp/parity/s_v3.err   | sed 's/^/          v3  : /'
  elif cmp -s /tmp/parity/s_base.txt /tmp/parity/s_v3.txt; then
    pass=$((pass+1)); printf "  SAME    %s\n" "$s"
  else
    fail=$((fail+1)); printf "  DIFFER  %s\n" "$s"
    diff /tmp/parity/s_base.txt /tmp/parity/s_v3.txt | head -14 | sed 's/^/          /'
  fi
done
echo "  ==> $pass same, $fail differ"
