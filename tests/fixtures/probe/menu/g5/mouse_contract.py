#!/usr/bin/env python3
"""What does clicking an emoji cell do TODAY, on the pre-change build?

The canonical principle is that a keyboard cursor must match what the mouse
already does. So measure the mouse first and make the keyboard agree, rather
than inventing a contract and calling it canonical.

Measured here: insert target, whether the picker stays open, and what happens to
the keyboard afterwards (a plain key either lands in the command line or is
swallowed - that is the focus indicator used throughout these probes).
"""
import os
import sys

sys.path.insert(0, "/tmp/irctui-caret")
sys.path.insert(0, "/tmp/irctui-menu/g3")
from g3_menu import spawn, key, read, CMDLINE_ROW  # noqa

CARET = "┃"
ESC = b"\x1b"
fails = 0


def field(sc):
    return sc.line(CMDLINE_ROW)[4:76].rstrip().replace(CARET, "")


def click(fd, sc, sgr, row, col):
    os.write(fd, b"\x1b[<%d;%d;%dM" % (0, col + 1, row + 1))
    read(fd, sc, sgr, 0.25)
    os.write(fd, b"\x1b[<%d;%d;%dm" % (0, col + 1, row + 1))
    read(fd, sc, sgr, 0.5)


def check(label, cond, detail):
    global fails
    print(("  PASS  %-56s %s" if cond else "  FAIL  %-56s %s") % (label, detail))
    if not cond:
        fails += 1


binp = os.path.abspath(sys.argv[1])
os.chdir("/tmp/irctui-menu/cur")
pid, fd, sc, sgr = spawn(binp)
print(sys.argv[2])

e = sc.find("\U0001f600")
click(fd, sc, sgr, e[0], e[1])          # open the picker
check("the picker opened", sc.find("emoji_picker") is not None,
      "emoji_picker at %s" % (sc.find("emoji_picker"),))
grid = sc.find("\U0001f600\U0001f601")   # first two cells of row 1
check("the grid is locatable", grid is not None, "grid row1 at %s" % (grid,))
if grid is None:
    sys.exit(1)
grow, gcol = grid
print("  grid row 1 at row %d col %d; cell width 2" % (grow, gcol))

# Click cell 1 (the second emoji), which is unambiguous.
click(fd, sc, sgr, grow, gcol + 2)
after_insert = field(sc)
check("a cell click inserts that emoji into the command line",
      "\U0001f601" in after_insert, "field = %r" % (after_insert,))
check("the picker does NOT close on a cell click", sc.find("emoji_picker") is not None,
      "emoji_picker present: %s" % (sc.find("emoji_picker") is not None,))
key(fd, sc, sgr, b"z", 0.5)
print("  MEASURED  after a cell click, typing 'z' gives %r" % field(sc))
# Close it and confirm the keyboard comes back.
key(fd, sc, sgr, ESC, 0.6)
print("  MEASURED  after Esc the picker is %s"
      % ("gone" if sc.find("emoji_picker") is None else "STILL OPEN"))
key(fd, sc, sgr, b"y", 0.5)
print("  MEASURED  after Esc, typing 'y' gives %r" % field(sc))
try:
    os.close(fd)
except OSError:
    pass
print("  ---- %s ----" % ("all passed" if not fails else "%d FAILED" % fails))
