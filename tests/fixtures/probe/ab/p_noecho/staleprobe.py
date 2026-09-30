#!/usr/bin/env python3
"""Is the leftover focus marker stale pixels, or a flag that was never cleared?

focusprobe.py showed that after F8 (G-2 build) there are TWO '*' markers: one on
the first menu item and one still on the command line. ui_set_focus clears
UI_F_FOCUSED on the outgoing node (ui_engine.c:71-72) and the marker paint is
gated on exactly that flag (:1122), so a marker that survives means the cell was
never repainted - not that the flag is wrong.

This isolates the repaint question with NO popup in the picture, so the answer
cannot be about the new flag or the new hook. Clicking a header button moves
focus with ui_set_focus (ui_engine.c:1343) on the unmodified code path. If the
command line keeps its '*' afterwards, the stale marker is a pre-existing
repaint defect that merely never showed before, because before G-2 focus never
left the command line with a menu open.

Run against the PRE-CHANGE binary: anything it shows is pre-existing by
construction, since it contains none of this change.

Usage: staleprobe.py <binary> <label>
"""
import os
import pty
import select
import sys
import time

sys.path.insert(0, "/tmp/irctui-ab/noech")
from pty_screen import Screen

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
    out = []
    for r in range(24):
        for c, ch in enumerate(sc.line(r)):
            if ch == "*":
                out.append((r, c))
    return out


def click(fd, sc, row, col):
    os.write(fd, b"\x1b[<0;%d;%dM" % (col + 1, row + 1))
    pump(fd, sc, 0.25)
    os.write(fd, b"\x1b[<0;%d;%dm" % (col + 1, row + 1))
    pump(fd, sc, 0.35)


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
    print("0. at rest: stars=%s" % (stars(sc),))

    # Find a header button. "File" is one, and it is not a popup, so clicking it
    # exercises only ui_set_focus + a normal repaint.
    pos = sc.find("File")
    print("1. the File header button is at %s" % (pos,))
    if pos is None:
        print("   cannot proceed")
        return 1

    click(fd, sc, pos[0], pos[1])
    print("2. after clicking it (focus has moved off the command line)")
    print("   stars=%s" % (stars(sc),))
    for r in (0, 1, CMDLINE_ROW):
        print("   row %2d %r" % (r, sc.line(r).rstrip()))

    cmd_marker = any(r == CMDLINE_ROW for r, _ in stars(sc))
    print("\n   command line still shows a marker? %s" % (cmd_marker,))
    print("   -> %s" % ("STALE PIXELS: the cell was never repainted"
                        if cmd_marker else
                        "no stale marker on this path"))

    # Does a repaint clear it? Typing forces the editor to redraw its own window.
    os.write(fd, b"x")
    pump(fd, sc, 0.6)
    print("\n3. after typing 'x' (forces the command line to repaint)")
    print("   stars=%s" % (stars(sc),))
    print("   row %2d %r" % (CMDLINE_ROW, sc.line(CMDLINE_ROW).rstrip()))

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
