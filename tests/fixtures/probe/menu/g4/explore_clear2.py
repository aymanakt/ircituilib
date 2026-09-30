#!/usr/bin/env python3
"""The same Clear, driven by MOUSE, on the pre-change build and the G-4 build.

The pre-change build has no A10/A11, so its ctx_menu is not flagged and not
focused on open; the Clear item still works by mouse. If the stale text appears
there too, it predates G-4.
"""
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
    print("%s" % label)
    print("  typed  : %r" % sc.line(CMDLINE_ROW))
    click(fd, sc, sgr, CMDLINE_ROW, 4, button=2)
    for r in range(14, 22):
        l = sc.line(r).rstrip()
        if l.strip():
            print("  menu r%d: %r" % (r, l))
    pos = None
    for r in range(24):
        x = sc.line(r).find("Clear")
        if x >= 0:
            pos = (r, x)
            break
    print("  Clear item at %s" % (pos,))
    if pos:
        click(fd, sc, sgr, pos[0], pos[1])
    print("  after  : %r" % sc.line(CMDLINE_ROW))
    print("  => stale: %s\n" % ("ello" in sc.line(CMDLINE_ROW),))
    try:
        os.close(fd)
    except OSError:
        pass


probe("PRE-CHANGE build, mouse Clear", os.path.abspath(sys.argv[2]))
probe("G-4 build, mouse Clear", os.path.abspath(sys.argv[1]))
