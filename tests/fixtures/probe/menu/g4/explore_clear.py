#!/usr/bin/env python3
"""Is "removed characters are not erased" pre-existing?

^X (key 24) calls ui_clip_clear on the focused node with no menu anywhere - it is
the plain keyboard route to the same buffer operation the ctx_clear item uses.
Driving it on BOTH the pre-change binary and the G-4 binary separates "my change
did this" from "this was always so".
"""
import os
import sys

sys.path.insert(0, "/tmp/irctui-caret")
sys.path.insert(0, "/tmp/irctui-menu/g3")
from g3_menu import spawn, key, CMDLINE_ROW  # noqa


def probe(label, binp, clear):
    pid, fd, sc, sgr = spawn(binp)
    for ch in "hello":
        key(fd, sc, sgr, ch.encode(), 0.15)
    before = sc.line(CMDLINE_ROW)
    key(fd, sc, sgr, clear, 0.8)
    after = sc.line(CMDLINE_ROW)
    print("%-22s before %r" % (label, before))
    print("%-22s after  %r" % ("", after))
    stale = "ello" in after
    print("%-22s => stale 'ello' on screen: %s\n" % ("", stale))
    try:
        os.close(fd)
    except OSError:
        pass


binp = os.path.abspath(sys.argv[1])
os.chdir("/tmp/irctui-menu/cur")
probe("G-4 build, ^X", binp, b"\x18")

other = os.path.abspath(sys.argv[2])
probe("PRE-CHANGE build, ^X", other, b"\x18")
