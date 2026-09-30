#!/usr/bin/env python3
"""What does delivering buttonless motion cost?

ui_engine.c:1159 already forces DECSET 1003, so the terminal sends motion
whether or not ncurses is listening. Today ALL_MOUSE_EVENTS excludes
REPORT_MOUSE_POSITION and ncurses drops every one of them. Adding the bit makes
ncurses deliver them - including motion with NO button held, which is what the
user does whenever they move the mouse across the window.

The demo's main loop (ui_main.c) runs ui_mark_overlap + ui_draw + doupdate for
every non-ERR key it receives and ignores the return value, so a delivered
motion event is a FULL SCREEN REPAINT.

Measures bytes written for 40 buttonless motions, per binary. Diagnostic.

Usage: live_motioncost.py binA binB ...
"""
import os, pty, select, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import Screen

CMDROW = 21


def drain(fd, secs):
    got = bytearray()
    end = time.time() + secs
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.02)
        if fd not in r:
            continue
        try:
            c = os.read(fd, 65536)
        except OSError:
            break
        if not c:
            break
        got += c
    return bytes(got)


def motion(col, row, btn):
    """SGR 1006 motion. btn 32 = button 1 held; btn 35 = no button held."""
    return b"\x1b[<%d;%d;%dM" % (btn, col + 1, row + 1)


def measure(binp, n=40):
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm-256color"
        os.environ["LINES"], os.environ["COLUMNS"] = "24", "80"
        os.execv(binp, [binp])
    sc = Screen(24, 80)
    drain(fd, 1.5)
    drain(fd, 0.4)                       # let the first paint settle

    idle = drain(fd, 1.0)                # control: same wall time, no input
    for i in range(n):
        os.write(fd, motion(5 + (i % 20), CMDROW, 35))
    moved = drain(fd, 1.0)

    # A held-button drag, for contrast: this is the case we WANT.
    os.write(fd, motion(6, CMDROW, 32))
    drag = drain(fd, 0.5)

    os.write(fd, b"\x03")
    drain(fd, 0.3)
    try:
        os.close(fd)
    except OSError:
        pass
    return len(idle), len(moved), len(drag)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    for b in sys.argv[1:]:
        idle, moved, drag = measure(b)
        print("%-16s idle %5d B   %d buttonless motions %6d B   held-drag %5d B"
              % (os.path.basename(b), idle, 40, moved, drag))
    return 0


if __name__ == "__main__":
    sys.exit(main())
