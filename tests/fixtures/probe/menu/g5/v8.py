#!/usr/bin/env python3
"""V8: the emoji grid's keyboard cursor.

The mouse is the specification, not an analogy. Measured on the pre-change build
first (mouse_contract.py): a click on a cell inserts that emoji into the command
line, the picker stays open so several can be picked in a row, and focus stays on
the grid - so a plain key afterwards is swallowed. Enter must do exactly that.

The plan's V8 said "a keypress afterwards still edits the field normally". That
is wrong: it contradicts the mouse, which is what the cursor is supposed to
match. Asserting it would encode a contract the mouse does not have.

The cursor is observed through reverse-video on the cell's own glyph. Cell 0 is
the same glyph as the header's emoji button, so it is pinned by elimination: on
open, no grid-only glyph is reversed AND the last write of the grid's own glyph
is. ncurses updates the screen in row order, so within a frame the grid's copy at
row 10 is written after the header's at row 2, which is what makes "the last
write" mean "the grid's".

Usage: v8.py <binary> <label>
"""
import os
import sys

sys.path.insert(0, "/tmp/irctui-caret")
sys.path.insert(0, "/tmp/irctui-menu/g3")
from g3_menu import spawn, key, read, CMDLINE_ROW  # noqa

CARET = "┃"
ESC = b"\x1b"
UP = b"\x1bOA"
DOWN = b"\x1bOB"
RIGHT = b"\x1bOC"
LEFT = b"\x1bOD"
ENTER = b"\r"
fails = 0

# Row 1 of the grid, then row 2's first cell. Cell width is 2 columns.
ROW1 = "\U0001f600\U0001f601\U0001f602\U0001f923\U0001f603\U0001f604\U0001f605\U0001f606"
CELL0 = "\U0001f600".encode()
ROW1B = [c.encode() for c in ROW1]
CELL7 = "\U0001f606".encode()          # row 1, cell 7 (end of row)
CELL9 = "\U0001f60a".encode()          # row 2, cell 1
CELL24 = "\u2764".encode()               # row 4, cell 0 - bare U+2764: the
                                       # painter drops the U+FE0F variation
                                       # selector (wcwidth 0), so the bytes
                                       # on screen are U+2764 alone
CELL27 = "\U0001f389".encode()          # row 4, cell 3 - the very last cell


def check(label, cond, detail):
    global fails
    print(("  PASS  %-58s %s" if cond else "  FAIL  %-58s %s") % (label, detail))
    if not cond:
        fails += 1


def field(sc):
    return sc.line(CMDLINE_ROW)[4:76].rstrip().replace(CARET, "")


def click(fd, sc, sgr, row, col):
    os.write(fd, b"\x1b[<%d;%d;%dM" % (0, col + 1, row + 1))
    read(fd, sc, sgr, 0.25)
    os.write(fd, b"\x1b[<%d;%d;%dm" % (0, col + 1, row + 1))
    read(fd, sc, sgr, 0.5)


def cursor(sgr):
    """Which cell the highlight is on, by elimination over the grid's glyphs.
    Returns an index, or None if the highlight is missing or ambiguous."""
    rev = [sgr.last_is_reversed(b) for b in ROW1B]
    if rev[0] is not True:
        return None if not any(rev[1:]) else rev.index(True)
    if any(rev[1:]):
        return None                      # two cells reversed: no single cursor
    return 0


binp = os.path.abspath(sys.argv[1])
os.chdir("/tmp/irctui-menu/cur")
print(sys.argv[2])

print("\nA. THE CURSOR EXISTS AND STARTS ON THE FIRST CELL")
pid, fd, sc, sgr = spawn(binp)
e = sc.find("\U0001f600")
click(fd, sc, sgr, e[0], e[1])           # open the picker
grid = sc.find(ROW1)
check("the picker opened with its grid", grid is not None, "grid row 1 at %s" % (grid,))
if grid is None:
    print("  ---- cannot proceed ----")
    sys.exit(1)
grow, gcol = grid
check("on open the cursor is on cell 0", cursor(sgr) == 0,
      "cursor = %s (states %s)" % (cursor(sgr), [sgr.last_is_reversed(b) for b in ROW1B[:3]]))

print("\nB. ARROWS MOVE IT, AND CLAMP RATHER THAN WRAP")
key(fd, sc, sgr, RIGHT, 0.5)
check("Right moves the cursor one cell, to cell 1", cursor(sgr) == 1,
      "cursor = %s" % (cursor(sgr),))
key(fd, sc, sgr, DOWN, 0.5)
check("Down moves it one ROW, to cell 9 (a row-2 glyph, checked directly)",
      sgr.last_is_reversed(CELL9) is True and cursor(sgr) != 1,
      "cell 9 reversed: %s" % (sgr.last_is_reversed(CELL9),))
key(fd, sc, sgr, UP, 0.5)
check("Up brings it back to cell 1", cursor(sgr) == 1, "cursor = %s" % (cursor(sgr),))
key(fd, sc, sgr, LEFT, 0.5)
check("Left returns to cell 0", cursor(sgr) == 0, "cursor = %s" % (cursor(sgr),))
key(fd, sc, sgr, LEFT, 0.5)
check("Left at the row start clamps - it does NOT wrap to the row above",
      cursor(sgr) == 0, "cursor = %s" % (cursor(sgr),))
key(fd, sc, sgr, UP, 0.5)
check("Up at cell 0 clamps", cursor(sgr) == 0, "cursor = %s" % (cursor(sgr),))
for _ in range(7):
    key(fd, sc, sgr, RIGHT, 0.35)
check("cursor reached the end of row 1 (cell 7)", sgr.last_is_reversed(CELL7) is True,
      "cell 7 reversed: %s" % (sgr.last_is_reversed(CELL7),))
key(fd, sc, sgr, RIGHT, 0.5)
check("Right at the row end clamps - it does NOT wrap to the next row",
      sgr.last_is_reversed(CELL7) is True, "cell 7 still reversed: %s" % (sgr.last_is_reversed(CELL7),))
# Back to the start of the row, then down a row at a time: from cell 7 a Down
# is +8, so it takes three of them AND a return to column 0 to name cell 24.
for _ in range(7):
    key(fd, sc, sgr, LEFT, 0.35)
check("back at cell 0", cursor(sgr) == 0, "cursor = %s" % (cursor(sgr),))
for _ in range(3):
    key(fd, sc, sgr, DOWN, 0.35)
check("three Downs reach the last row (cell 24)", sgr.last_is_reversed(CELL24) is True,
      "cell 24 reversed: %s" % (sgr.last_is_reversed(CELL24),))
key(fd, sc, sgr, DOWN, 0.5)
check("Down past the SHORT last row clamps onto its last cell, cell 27",
      sgr.last_is_reversed(CELL27) is True, "cell 27 reversed: %s" % (sgr.last_is_reversed(CELL27),))
key(fd, sc, sgr, UP, 0.4)
key(fd, sc, sgr, UP, 0.4)
key(fd, sc, sgr, UP, 0.4)
key(fd, sc, sgr, UP, 0.4)

print("\nC. ENTER MATCHES THE MOUSE, CELL FOR CELL")
key(fd, sc, sgr, RIGHT, 0.5)
check("cursor is on cell 1", cursor(sgr) == 1, "cursor = %s" % (cursor(sgr),))
key(fd, sc, sgr, ENTER, 0.6)
check("Enter inserts cell 1's emoji into the command line",
      "\U0001f601" in field(sc), "field = %r" % (field(sc),))
check("the picker stays open, exactly as after a mouse click",
      sc.find(ROW1) is not None, "grid present: %s" % (sc.find(ROW1) is not None,))
check("the grid's own text is unchanged - the insert did not go into the grid",
      sc.find(ROW1) == grid, "grid at %s, was %s" % (sc.find(ROW1), grid))
key(fd, sc, sgr, b"z", 0.5)
check("a plain key is swallowed, exactly as after a mouse click",
      "z" not in field(sc), "field = %r" % (field(sc),))
before = field(sc)
key(fd, sc, sgr, ENTER, 0.6)
check("a second Enter inserts again - the picker is still usable",
      field(sc) != before and field(sc).count("\U0001f601") == 2,
      "field = %r" % (field(sc),))
key(fd, sc, sgr, ESC, 0.6)
check("Esc closes the picker", sc.find(ROW1) is None,
      "grid present: %s" % (sc.find(ROW1) is not None,))
key(fd, sc, sgr, b"w", 0.5)
check("...and typing resumes in the field", "w" in field(sc), "field = %r" % (field(sc),))

print("\nD. THE SAME THREE FACTS VIA THE MOUSE, FOR THE EQUIVALENCE CLAIM")
pid2, fd2, sc2, sgr2 = spawn(binp)
e2 = sc2.find("\U0001f600")
click(fd2, sc2, sgr2, e2[0], e2[1])
g2 = sc2.find(ROW1)
click(fd2, sc2, sgr2, g2[0], g2[1] + 2)  # cell 1
check("mouse: clicking cell 1 inserts the same emoji",
      "\U0001f601" in field(sc2), "field = %r" % (field(sc2),))
check("mouse: the picker stays open", sc2.find(ROW1) is not None,
      "grid present: %s" % (sc2.find(ROW1) is not None,))
key(fd2, sc2, sgr2, b"z", 0.5)
check("mouse: a plain key afterwards is swallowed too",
      "z" not in field(sc2), "field = %r" % (field(sc2),))
for f in (fd, fd2):
    try:
        os.close(f)
    except OSError:
        pass
print("  ---- %s ----" % ("all passed" if not fails else "%d FAILED" % fails))
