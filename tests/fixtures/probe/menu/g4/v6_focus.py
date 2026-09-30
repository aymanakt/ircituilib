#!/usr/bin/env python3
"""V6: the mouse path. What it can and cannot demonstrate.

A10 comes in two halves:
  (i)  a click on a menu ITEM dismisses the menu and echoes that item;
  (ii) the cursor moves onto the clicked row and focus goes to the CONTAINER
       rather than to the row.

Half (ii) has no independent observable: "any activation dismisses" pops the
container in the same event, and ui_pop_stacked clears the focus, so the end
state is the same with or without it. This probe does not claim to show it.

The plan's V6 wording ("click item 2, reopen, press Down - if focus went to the
child the highlight will not move") cannot show it either: reopening fires
on_inflated -> ui_popup_focus_on_open, which focuses the container regardless of
what the click did. It is kept as a control that does discriminate the FEATURE
from the pre-change build, which is still worth having.

A route that looks like it would test (ii) - click the menu's FRAME, which is not
an activation, then see whether the keyboard came along - does not work, and the
reason is pre-existing rather than a defect of this change: the press/click path
gates its activation-and-focus block on kind BUTTON/EMOJI_TABLE or the CLICKABLE
flag or an on_input hook (ui_engine.c:1466), so a plain click on a UI_LINEAR
container is consumed and returned from without ever calling ui_set_focus
(ui_engine.c:1561). That is measured below as an explicit negative result, and
must read the same on the pre-change build.

Usage: v6_focus.py <binary> <label>
"""
import os
import sys

sys.path.insert(0, "/tmp/irctui-caret")
sys.path.insert(0, "/tmp/irctui-menu/g3")
from g3_menu import spawn, key, read, DOWN, F8, CMDLINE_ROW, ITEMS  # noqa

ESC = b"\x1b"
CARET = "┃"
fails = 0


def check(label, cond, detail):
    global fails
    print(("  PASS  %-58s %s" if cond else "  FAIL  %-58s %s") % (label, detail))
    if not cond:
        fails += 1


def field(sc):
    """The command line as a user reads it: past the '> ' prompt, before the
    focus marker at col 77, with the caret glyph dropped. The command line is a
    precise focus indicator because a focused menu swallows plain characters."""
    return sc.line(CMDLINE_ROW)[4:76].rstrip().replace(CARET, "")


def click(fd, sc, sgr, row, col):
    os.write(fd, b"\x1b[<%d;%d;%dM" % (0, col + 1, row + 1))
    read(fd, sc, sgr, 0.25)
    os.write(fd, b"\x1b[<%d;%d;%dm" % (0, col + 1, row + 1))
    read(fd, sc, sgr, 0.5)


def states(sgr):
    return [sgr.last_is_reversed(t) for t in ITEMS]


binp = os.path.abspath(sys.argv[1])
os.chdir("/tmp/irctui-menu/cur")
pid, fd, sc, sgr = spawn(binp)
print(sys.argv[2])

print("\nA. THE FEATURE (discriminates new build from pre-change; FAILS on PRE)")
key(fd, sc, sgr, F8, 1.0)
h = sc.find("Help")
check("the menu opened and Help is locatable", h is not None, "Help=%s" % (h,))
if h is None:
    print("  ---- cannot proceed ----")
    sys.exit(1)
check("on open, item 1 is the highlighted one", states(sgr)[0] is True,
      "states = %s" % (states(sgr),))
click(fd, sc, sgr, h[0], h[1])          # click the "Help" ITEM
check("clicking an item dismisses the menu", sc.find("Close") is None,
      "Close still present: %s" % (sc.find("Close") is not None,))
key(fd, sc, sgr, F8, 0.9)               # reopen
check("a reopened menu highlights item 1 again", states(sgr)[0] is True,
      "states = %s" % (states(sgr),))
key(fd, sc, sgr, DOWN, 0.6)
check("...and Down then moves the highlight to item 2",
      states(sgr) == [False, True, False], "states = %s" % (states(sgr),))

print("\nB. NEGATIVE RESULT, pre-existing: a FRAME click does not focus a container")
key(fd, sc, sgr, ESC, 0.6)
key(fd, sc, sgr, F8, 0.9)
click(fd, sc, sgr, CMDLINE_ROW, 20)     # give the command line the keyboard
key(fd, sc, sgr, b"z", 0.5)
check("positive polarity: 'z' lands in the command line", field(sc) == "z",
      "field = %r" % (field(sc),))
h3 = sc.find("Help")
frow, fcol = h3[0] - 1, h3[1] - 1        # the menu's top border, left of the title
print("  clicking the menu frame at row %d col %d (cell %r)"
      % (frow, fcol, sc.grid[frow][fcol]))
click(fd, sc, sgr, frow, fcol)
check("a frame click is not an activation: the menu stays open",
      sc.find("Close") is not None,
      "Close on screen: %s" % (sc.find("Close") is not None,))
key(fd, sc, sgr, b"z", 0.5)
# Deliberately a MEASUREMENT, not an assertion: whether an inert background
# click moves focus is existing behaviour, and pinning it would be
# behaviour-locking. What matters is that it reads identically on the pre-change
# build - compare this line across builds.
print("  MEASURED  after a frame click the field is %r (%s)"
      % (field(sc),
         "focus did NOT move - container clicks never call ui_set_focus"
         if field(sc) == "zz" else "focus moved to the menu"))

print("\n  menu region:")
for r in range(max(0, frow - 1), min(24, h3[0] + 5)):
    print("  %2d %r" % (r, sc.line(r).rstrip()))
try:
    os.close(fd)
except OSError:
    pass
print("  ---- %s ----" % ("all passed" if not fails else "%d FAILED" % fails))
