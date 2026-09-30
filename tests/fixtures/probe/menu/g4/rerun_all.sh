#!/bin/bash
# Re-run every suite with the ECH-corrected screen model, on both the G-4 build
# and the pre-change build. The pre-change run is the negative control: a suite
# that passes on both proves nothing.
G4=/tmp/irctui-menu/g4/g4_demo
PRE=/tmp/irctui-menu/cur_demo
G3=/tmp/irctui-menu/g3/g3_demo
cd /tmp/irctui-menu
echo "############ g0_uaf (updated contract) ############"
echo "  PRE: $(timeout 200 python3 g0_uaf.py $PRE 2>&1 | tail -1)"
echo "  G-4: $(timeout 200 python3 g0_uaf.py $G4 2>&1 | tail -1)"
echo "############ g3_menu.py ############"
echo "  PRE: $(timeout 200 python3 g3/g3_menu.py $PRE 2>&1 | tail -1)"
echo "  G-3: $(timeout 200 python3 g3/g3_menu.py $G3 2>&1 | tail -1)"
echo "  G-4: $(timeout 200 python3 g3/g3_menu.py $G4 2>&1 | tail -1)"
echo "############ g3_v5.py (A7 double-pop) ############"
echo "  PRE: $(timeout 200 python3 g3/g3_v5.py $PRE 2>&1 | tail -1)"
echo "  G-4: $(timeout 200 python3 g3/g3_v5.py $G4 2>&1 | tail -1)"
echo "############ g3_v9.py (inertness) ############"
echo "  PRE: $(timeout 200 python3 g3/g3_v9.py $PRE 2>&1 | tail -1)"
echo "  G-4: $(timeout 200 python3 g3/g3_v9.py $G4 2>&1 | tail -1)"
echo "############ repo test_pty.py ############"
echo "  G-4: $(cd /home/devops/repos/ircit/packages/ircituilib && timeout 120 python3 test_pty.py $G4 2>&1 | tail -1)"
