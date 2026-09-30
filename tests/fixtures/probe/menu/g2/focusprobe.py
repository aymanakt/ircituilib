#!/usr/bin/env python3
"""Does the G-2 on_inflated hook change where the keyboard goes?

The plan says of G-2 "nothing consumes the flag yet, so behaviour is unchanged".
That is true of UI_F_CHILDREN_SELECTABLE - nothing reads it. It is NOT
necessarily true of A3: file_menu.yaml, tools_panel.yaml and emoji_picker.yaml
each gained `on_inflated: ui_popup_focus_on_open`, and that hook is live code
(ui_engine.c:164 -> ui_focus_in_layer). So the question this answers, by
measurement rather than by reading:

  - Before G-2 (pre-change binary), with the file menu open, where is the focus
    marker and where does a typed character go?
  - After G-2, same two questions.

Two observables, both staleness-immune:
  - the `*` focus marker, painted at (0, w-1) of the focused node (ui_engine.c
    :1122), i.e. the top-right corner of a bordered node;
  - the command line contents after typing, which shows whether the keystroke
    reached the editor or was swallowed by a focused button.

Usage: focusprobe.py <binary> <label>
"""
import os
import pty
import select
import sys
import time

sys.path.insert(0, "/tmp/irctui-caret")
from pty_screen import Screen

F8 = b"\x1b[19~"
CMDLINE_ROW = 21


def pump(fd, sc, secs):
    end = time.time() + secs
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.05)
        if fd not in r:
            continue
        try:
            c = os.read(fd, 65536)
        except OSError:
            return False
        if not c:
            return False
        sc.feed(c)
    return True


def stars(sc):
    """Every focus marker on screen, as (row, col)."""
    out = []
    for r in range(24):
        l = sc.line(r)
        for c, ch in enumerate(l):
            if ch == "*":
                out.append((r, c))
    return out


def dump(sc, label):
    print("  --- %s ---" % label)
    print("    focus markers '*': %s" % (stars(sc),))
    for r in (3, 4, 5, 6, 7, 8, 21):
        print("    row %2d %r" % (r, sc.line(r).rstrip()))


def main():
    binp = os.path.abspath(sys.argv[1])
    label = sys.argv[2] if len(sys.argv) > 2 else binp
    os.chdir("/tmp/irctui-menu/cur")
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm-256color"
        os.environ["LINES"], os.environ["COLUMNS"] = "24", "80"
        os.execv(binp, [binp])
    sc = Screen(24, 80)
    pump(fd, sc, 1.6)

    print("=== %s ===" % label)

    # Control point 1: type BEFORE any menu is open. This must always work; it
    # proves the keystroke path itself is live, so a later failure means the
    # menu took the keyboard rather than that the probe is broken.
    os.write(fd, b"a")
    pump(fd, sc, 0.5)
    print("\n1. typing 'a' with nothing open (positive control)")
    print("   cmdline row %d: %r" % (CMDLINE_ROW, sc.line(CMDLINE_ROW).rstrip()))
    print("   focus markers: %s" % (stars(sc),))

    print("\n2. F8 -> open the file menu")
    os.write(fd, F8)
    pump(fd, sc, 0.8)
    dump(sc, "file menu open")

    print("\n3. type 'z' with the file menu open")
    os.write(fd, b"z")
    pump(fd, sc, 0.6)
    row = sc.line(CMDLINE_ROW)
    print("   cmdline row %d: %r" % (CMDLINE_ROW, row.rstrip()))
    print("   'z' reached the command line? %s" % ("az" in row,))
    print("   focus markers: %s" % (stars(sc),))

    print("\n4. Esc, then type 'b' (does the keyboard come back?)")
    os.write(fd, b"\x1b")
    pump(fd, sc, 0.6)
    os.write(fd, b"b")
    pump(fd, sc, 0.6)
    print("   cmdline row %d: %r" % (CMDLINE_ROW, sc.line(CMDLINE_ROW).rstrip()))
    print("   focus markers: %s" % (stars(sc),))

    try:
        os.write(fd, b"\x03")
        pump(fd, sc, 0.3)
    except OSError:
        pass
    try:
        os.close(fd)
    except OSError:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
