#!/usr/bin/env python3
"""Which cells belong to the menu container, and which hand it the keyboard?

A click on a frame cell should not activate (nothing pops) but should focus the
menu container. 'z' is the focus indicator: it lands in the command line when
cmdline has the keyboard, and vanishes when the menu does.

For each candidate cell: focus the command line, type 'z' (expect 'z'), click the
candidate, type 'z' again. field == 'z' means the click took the keyboard.
^X clears the field between candidates (the clipboard block sits above on_input,
so it works whatever holds focus).
"""
import os
import sys

sys.path.insert(0, "/tmp/irctui-caret")
sys.path.insert(0, "/tmp/irctui-menu/g3")
from g3_menu import spawn, key, read, F8, CMDLINE_ROW  # noqa

CARET = "┃"


def field(sc):
    return sc.line(CMDLINE_ROW)[4:76].rstrip().replace(CARET, "")


def click(fd, sc, sgr, row, col):
    os.write(fd, b"\x1b[<%d;%d;%dM" % (0, col + 1, row + 1))
    read(fd, sc, sgr, 0.2)
    os.write(fd, b"\x1b[<%d;%d;%dm" % (0, col + 1, row + 1))
    read(fd, sc, sgr, 0.4)


binp = os.path.abspath(sys.argv[1])
os.chdir("/tmp/irctui-menu/cur")
pid, fd, sc, sgr = spawn(binp)
key(fd, sc, sgr, F8, 1.0)

h = sc.find("Help")
print("Help at %s" % (h,))
print("\nmenu region, with column ruler:")
for r in range(2, 9):
    line = sc.line(r).rstrip()
    print("  %2d %r" % (r, line))
ruler = "".join(str(c % 10) for c in range(80))
print("     %r" % ruler[38:62])

# Candidate frame cells: top border row, bottom border row, and the left/right
# border columns of each child row.
rowtop, colh = h[0] - 1, h[1]
cands = [(rowtop, colh - 1), (rowtop, colh + 2),
         (h[0], colh - 2), (h[0] + 2, colh - 2),
         (h[0] + 3, colh), (h[0] + 3, colh - 3)]
print("\nclicking each candidate, then typing 'z':")
for (r, c) in cands:
    click(fd, sc, sgr, CMDLINE_ROW, 20)      # give cmdline the keyboard
    key(fd, sc, sgr, b"\x18", 0.3)           # ^X clears whatever is there
    key(fd, sc, sgr, b"z", 0.4)
    baseline = field(sc)
    click(fd, sc, sgr, r, c)
    key(fd, sc, sgr, b"z", 0.4)
    after = field(sc)
    ch = sc.grid[r][c] if c < 80 else "?"
    print("  (%2d,%2d) cell=%r  before=%r after=%r  %s"
          % (r, c, ch, baseline, after,
             "TOOK THE KEYBOARD" if after == baseline else "still on cmdline"))
try:
    os.close(fd)
except OSError:
    pass
