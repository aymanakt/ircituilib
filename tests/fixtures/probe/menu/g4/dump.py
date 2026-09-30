#!/usr/bin/env python3
"""Exploratory: where does the context menu actually land, and what is on it?"""
import os
import sys

sys.path.insert(0, "/tmp/irctui-caret")
sys.path.insert(0, "/tmp/irctui-menu/g3")
from g3_menu import spawn, key, read, DOWN, ENTER, F8, CMDLINE_ROW  # noqa


def dump(sc, label):
    print("=== %s ===" % label)
    for r in range(24):
        l = sc.line(r).rstrip()
        if l.strip():
            print("%2d %r" % (r, l))
    print()


def click(fd, sc, sgr, row, col, button=0):
    os.write(fd, b"\x1b[<%d;%d;%dM" % (button, col + 1, row + 1))
    read(fd, sc, sgr, 0.25)
    os.write(fd, b"\x1b[<%d;%d;%dm" % (button, col + 1, row + 1))
    read(fd, sc, sgr, 0.35)


binp = os.path.abspath(sys.argv[1])
os.chdir("/tmp/irctui-menu/cur")
pid, fd, sc, sgr = spawn(binp)

dump(sc, "start")
for ch in "hello":
    key(fd, sc, sgr, ch.encode(), 0.12)
dump(sc, "after typing hello")

click(fd, sc, sgr, CMDLINE_ROW, 4, button=2)
dump(sc, "after right-click on the cmdline")

key(fd, sc, sgr, DOWN, 0.5)
print("reverse states after Down: %s" % (
    [(t, sgr.last_is_reversed(t)) for t in
     (b"Copy", b"Select all", b"Paste", b"Clear")],))
dump(sc, "after Down")

key(fd, sc, sgr, b"\x1b", 0.6)
dump(sc, "after Esc")

key(fd, sc, sgr, F8, 0.9)
dump(sc, "after F8 (file menu)")

click(fd, sc, sgr, 2, 30, button=2)
dump(sc, "after right-click header (ctx menu is Close)")

try:
    os.write(fd, b"\x03")
    os.close(fd)
except OSError:
    pass
