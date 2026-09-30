#!/usr/bin/env python3
"""A10 sits immediately next to the radio-group click branch, and both are
reached by a click on a button-like node whose parent is a container. A mistake
in the new condition would be silent: the group branch runs first and normally
returns, so the radio path must not have started falling into the menu path.

The tools panel's `plan` radio group is a container of radio buttons. Its own
flags do NOT include UI_F_CHILDREN_SELECTABLE (that is deferred), so the new
branch must not engage for it - and even if it did, the group branch above
returns before it. Either way the observable is the same: clicking a radio
option selects it.

Usage: radio_click.py <binary> <label>
"""
import os
import sys

sys.path.insert(0, "/tmp/irctui-caret")
sys.path.insert(0, "/tmp/irctui-menu/g3")
from g3_menu import spawn, key, read  # noqa

F7 = b"\x1b[18~"
fails = 0


def check(label, cond, detail):
    global fails
    print(("  PASS  %-56s %s" if cond else "  FAIL  %-56s %s") % (label, detail))
    if not cond:
        fails += 1


def click(fd, sc, sgr, row, col, button=0):
    os.write(fd, b"\x1b[<%d;%d;%dM" % (button, col + 1, row + 1))
    read(fd, sc, sgr, 0.3)
    os.write(fd, b"\x1b[<%d;%d;%dm" % (button, col + 1, row + 1))
    read(fd, sc, sgr, 0.5)


def marked(sc):
    """(row, col) of every selected radio dot on screen."""
    return [(r, sc.line(r).find("●")) for r in range(24)
            if "●" in sc.line(r)]


binp = os.path.abspath(sys.argv[1])
os.chdir("/tmp/irctui-menu/cur")
pid, fd, sc, sgr = spawn(binp)
key(fd, sc, sgr, F7, 1.0)
before = marked(sc)
print("%s" % sys.argv[2])
for r in range(24):
    if sc.line(r).strip():
        print("  %2d %r" % (r, sc.line(r).rstrip()))
check("the panel shows exactly one selected radio", len(before) == 1,
      "dots at %s" % (before,))
if len(before) != 1:
    # Module level: there is no function to return from. Bail out with a
    # verdict the caller can read, rather than falling through to a click on a
    # row that was never located.
    print("  ---- cannot proceed: no single selected radio ----")
    sys.exit(1)
# Click the LAST radio row of the group, which is a different option.
row0 = before[0][0]
cands = [r for r in range(row0, min(row0 + 6, 24))
         if "○" in sc.line(r) or "(" in sc.line(r) or "●" in sc.line(r)]
target = cands[-1] if cands else row0
col = 0
for c, ch in enumerate(sc.line(target)):
    if ch in "○●(":
        col = c
        break
print("  clicking the radio row %d at col %d" % (target, col))
click(fd, sc, sgr, target, col)
after = marked(sc)
check("the selection moved to the clicked row", len(after) == 1 and after[0][0] != row0,
      "dots now at %s (was %s)" % (after, before))
# The '*' that used to be stamped here was the whole-UI focus marker of the
# `plan` radio GROUP - a container that carries an id and holds focus, so
# ui_draw wrote it at the group's own (0, n->w-1) cell, landing mid-row inside
# the panel. That marker has since been removed from ui_draw deliberately, for
# every node, so this assertion is re-baselined: a '*' reappearing on screen now
# means the marker has come back. A10's own claim is narrower and is still
# carried by the selection assertions above - the click branch added no marker
# of its own and did not detach the group's selection.
stars = [r for r in range(24) if "*" in sc.line(r)]
dots = [r for r in range(24) if "●" in sc.line(r)]
check("the removed focus marker has not come back", not stars,
      "'*' rows %s (selected rows %s)" % (stars, dots))
print("  post-click panel:")
for r in range(2, 20):
    if sc.line(r).strip():
        print("  %2d %r" % (r, sc.line(r).rstrip()))
try:
    os.close(fd)
except OSError:
    pass
print("  ---- %s ----" % ("all passed" if not fails else "%d FAILED" % fails))
