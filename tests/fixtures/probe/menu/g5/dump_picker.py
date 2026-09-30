#!/usr/bin/env python3
"""Open the emoji picker with the mouse and dump the screen, to learn the
existing contract: where the grid lands, and what a click on a cell does to
focus and to the command line."""
import os
import sys

sys.path.insert(0, "/tmp/irctui-caret")
sys.path.insert(0, "/tmp/irctui-menu/g3")
from g3_menu import spawn, key, read, CMDLINE_ROW  # noqa

CARET = "┃"


def field(sc):
    return sc.line(CMDLINE_ROW)[4:76].rstrip().replace(CARET, "")


def click(fd, sc, sgr, row, col):
    os.write(fd, b"\x1b[<%d;%d;%dM" % (0, col + 1, row + 1))
    read(fd, sc, sgr, 0.25)
    os.write(fd, b"\x1b[<%d;%d;%dm" % (0, col + 1, row + 1))
    read(fd, sc, sgr, 0.5)


binp = os.path.abspath(sys.argv[1])
os.chdir("/tmp/irctui-menu/cur")
pid, fd, sc, sgr = spawn(binp)
print(sys.argv[2])

e = sc.find("\U0001f600")            # the header emoji button
print("header emoji button at %s" % (e,))
click(fd, sc, sgr, e[0], e[1])
print("\nafter clicking it:")
for r in range(24):
    if sc.line(r).strip():
        print("  %2d %r" % (r, sc.line(r).rstrip()))
print("\n  cmdline field before any cell click: %r" % field(sc))
key(fd, sc, sgr, b"z", 0.5)
print("  typing 'z' with the picker open:    %r" % field(sc))
try:
    os.close(fd)
except OSError:
    pass
