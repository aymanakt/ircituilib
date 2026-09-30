#!/usr/bin/env python3
"""Does the stale text survive the next repaint, or is it a one-frame lag?"""
import os
import sys

sys.path.insert(0, "/tmp/irctui-caret")
sys.path.insert(0, "/tmp/irctui-menu/g3")
from g3_menu import spawn, key, read, CMDLINE_ROW  # noqa


def click(fd, sc, sgr, row, col, button=0):
    os.write(fd, b"\x1b[<%d;%d;%dM" % (button, col + 1, row + 1))
    read(fd, sc, sgr, 0.25)
    os.write(fd, b"\x1b[<%d;%d;%dm" % (button, col + 1, row + 1))
    read(fd, sc, sgr, 0.4)


def probe(label, binp):
    pid, fd, sc, sgr = spawn(binp)
    for ch in "hello":
        key(fd, sc, sgr, ch.encode(), 0.15)
    click(fd, sc, sgr, CMDLINE_ROW, 4, button=2)
    pos = None
    for r in range(24):
        x = sc.line(r).find("Clear")
        if x >= 0:
            pos = (r, x)
            break
    click(fd, sc, sgr, pos[0], pos[1])
    print("%s" % label)
    print("  right after    : %r" % sc.line(CMDLINE_ROW))
    read(fd, sc, sgr, 1.6)          # several blink ticks
    print("  after 1.6s idle: %r" % sc.line(CMDLINE_ROW))
    key(fd, sc, sgr, b"y", 0.6)
    print("  after typing y : %r" % sc.line(CMDLINE_ROW))
    try:
        os.close(fd)
    except OSError:
        pass


probe("PRE-CHANGE, mouse Clear", os.path.abspath(sys.argv[2]))
probe("G-4, mouse Clear", os.path.abspath(sys.argv[1]))
