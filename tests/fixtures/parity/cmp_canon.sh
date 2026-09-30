#!/bin/bash
cd /tmp/parity
pass=0; fail=0
for s in "$@"; do
  python3 diffdrv.py /tmp/parity/base/ui_demo $s > /tmp/parity/c_base.txt 2>/dev/null
  python3 diffdrv.py /tmp/parity/canon/ircitui_demo $s > /tmp/parity/c_can.txt 2>/dev/null
  if cmp -s /tmp/parity/c_base.txt /tmp/parity/c_can.txt; then pass=$((pass+1)); else fail=$((fail+1)); echo "  DIFFER $s"; diff /tmp/parity/c_base.txt /tmp/parity/c_can.txt | head -6 | sed 's/^/      /'; fi
done
echo "  ==> $pass same, $fail differ"
