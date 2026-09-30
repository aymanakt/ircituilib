#!/usr/bin/env python3
"""Re-check the "stale focus marker" finding with ECH implemented.

The claim was: move focus off the command line and its '*' marker stays behind,
because ui_blink_advance returns 0 unless focus is an edit_text, so nothing
repaints the command line. If that were true it would be a real defect.

But the '*' is erased by a repaint, and a repaint that clears a single cell can
be spelled as ECH - which the screen model used to drop. So the finding has to be
re-measured now that ECH is modelled.

The pre-change binary is the important one: it needs a path that moves focus away
from the command line WITHOUT any of the new code. Clicking a header button does
that (ui_set_focus(hit)), and that path is unchanged by this work.

Usage: stale_marker.py <binary>
"""
import os
import sys

sys.path.insert(0, "/tmp/irctui-caret")
sys.path.insert(0, "/tmp/irctui-menu/g3")
from g3_menu import spawn, key, read, F8, CMDLINE_ROW  # noqa


def click(fd, sc, sgr, row, col, button=0):
    os.write(fd, b"\x1b[<%d;%d;%dM" % (button, col + 1, row + 1))
    read(fd, sc, sgr, 0.3)
    os.write(fd, b"\x1b[<%d;%d;%dm" % (button, col + 1, row + 1))
    read(fd, sc, sgr, 0.5)


def star_col(sc):
    return sc.line(CMDLINE_ROW).find("*")


binp = os.path.abspath(sys.argv[1])
os.chdir("/tmp/irctui-menu/cur")
pid, fd, sc, sgr = spawn(binp)

print("%s" % sys.argv[2])
print("  at startup, cmdline '*' at col %s (focused)" % star_col(sc))
f = sc.find("File")
click(fd, sc, sgr, f[0], f[1])
read(fd, sc, sgr, 0.6)
print("  after clicking the File header, '*' at col %s" % star_col(sc))
print("  row 21: %r" % sc.line(CMDLINE_ROW))
print("  => marker still present: %s" % (star_col(sc) >= 0,))
try:
    os.close(fd)
except OSError:
    pass
