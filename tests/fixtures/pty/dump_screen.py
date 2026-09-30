#!/usr/bin/env python3
"""Dump the whole screen at startup, and after each of the keys that touch the
canvas, so "the brown fox text floods the main screen" can be pinned to a
trigger instead of guessed at.

Usage: dump_screen.py /path/to/ui_demo
"""
import os, pty, select, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import Screen


def pump(fd, sc, secs):
    end = time.time() + secs
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.05)
        if fd not in r:
            continue
        try:
            c = os.read(fd, 65536)
        except OSError:
            break
        if not c:
            break
        sc.feed(c)


def show(sc, label):
    fox = sum(1 for r in range(24) if "brown fox" in sc.line(r))
    print("\n===== %s   (rows containing 'brown fox': %d/24) =====" % (label, fox))
    for r in range(24):
        print("%2d %r" % (r, sc.line(r).rstrip()))


def main():
    binp = sys.argv[1]
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm-256color"
        os.environ["LINES"], os.environ["COLUMNS"] = "24", "80"
        os.execv(binp, [binp])
    sc = Screen(24, 80)
    pump(fd, sc, 1.6)
    show(sc, "AT STARTUP - nothing pressed")

    os.write(fd, b"f")                      # the 'f' key
    pump(fd, sc, 1.0)
    show(sc, "AFTER PRESSING 'f'")

    os.write(fd, b"\x03")
    pump(fd, sc, 0.3)
    try:
        os.close(fd)
    except OSError:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
