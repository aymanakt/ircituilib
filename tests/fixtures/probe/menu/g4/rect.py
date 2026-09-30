#!/usr/bin/env python3
"""Read the menu's rect off the screen, cell by cell."""
import os
import sys

sys.path.insert(0, "/tmp/irctui-caret")
sys.path.insert(0, "/tmp/irctui-menu/g3")
from g3_menu import spawn, key, F8  # noqa

binp = os.path.abspath(sys.argv[1])
os.chdir("/tmp/irctui-menu/cur")
pid, fd, sc, sgr = spawn(binp)
key(fd, sc, sgr, F8, 1.0)

h = sc.find("Help")
print("Help at %s ; Open at %s ; Close at %s"
      % (h, sc.find("Open"), sc.find("Close")))
lo, hi = 42, 62
print("\n     " + "".join("%d" % (c // 10) for c in range(lo, hi)))
print("     " + "".join("%d" % (c % 10) for c in range(lo, hi)))
for r in range(2, 9):
    print("  %2d %s" % (r, "".join(sc.grid[r][c] for c in range(lo, hi))))
# Which cells in the menu's neighbourhood belong to which node? Report the
# characters that are box-drawing, so the frame is unambiguous.
box = set("┌┐└┘─│├┤┬┴┼")
print("\n  frame cells (box-drawing) near the menu:")
for r in range(2, 9):
    cells = [c for c in range(lo, hi) if sc.grid[r][c] in box]
    print("  %2d %s" % (r, cells))
try:
    os.close(fd)
except OSError:
    pass
