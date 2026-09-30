#!/usr/bin/env python3
"""Why do Select all / Copy report the context menu still present?

g0f asserts "context menu closed" by searching the screen for the item's own
label. That failed for the first two items and passed for the last two, which is
exactly the pattern a stale-screen artefact would make: the earlier labels may
still be painted from the earlier menu. This dumps the whole screen after each
click so the difference is observed.
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


def right_click(fd, sc, row, col):
    os.write(fd, b"\x1b[<2;%d;%dM" % (col + 1, row + 1))
    pump(fd, sc, 0.20)
    os.write(fd, b"\x1b[<2;%d;%dm" % (col + 1, row + 1))
    pump(fd, sc, 0.30)


def click(fd, sc, row, col):
    os.write(fd, b"\x1b[<0;%d;%dM" % (col + 1, row + 1))
    pump(fd, sc, 0.20)
    os.write(fd, b"\x1b[<0;%d;%dm" % (col + 1, row + 1))
    pump(fd, sc, 0.30)


def dump(sc, label):
    print("  --- %s ---" % label)
    for r in range(13, 23):
        print("    row %2d %r" % (r, sc.line(r).rstrip()))


def main():
    binp = os.path.abspath(sys.argv[1])
    os.chdir(os.environ.get("PROBE_CWD", "/tmp/irctui-menu/cur"))
    os.environ.setdefault("ASAN_OPTIONS", "detect_leaks=0")
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm-256color"
        os.environ["LINES"], os.environ["COLUMNS"] = "24", "80"
        os.execv(binp, [binp])
    sc = Screen(24, 80)
    pump(fd, sc, 1.6)

    for ch in "hello":
        os.write(fd, ch.encode())
    pump(fd, sc, 0.6)
    os.write(fd, F8)
    pump(fd, sc, 0.7)
    dump(sc, "file menu open")

    for name in ["Select all", "Copy", "Clear", "Paste"]:
        print("\n=== right-click the cmdline, then click %r ===" % name)
        right_click(fd, sc, CMDLINE_ROW, 4)
        dump(sc, "context menu open")
        pos = None
        for r in range(13, 23):
            l = sc.line(r)
            x = l.find(name)
            if x >= 0 and x < 44:          # left of the file menu's band
                pos = (r, x)
                break
        print("  %r located at %s" % (name, pos))
        if pos is None:
            break
        click(fd, sc, pos[0], pos[1])
        dump(sc, "after clicking %r" % name)
        print("  labels still on screen: %s"
              % [n for n in ["Select all", "Copy", "Clear", "Paste"]
                 if any(n in sc.line(r) and sc.line(r).find(n) < 44
                        for r in range(13, 23))])

    os.write(fd, b"\x03")
    pump(fd, sc, 0.3)
    try:
        os.close(fd)
    except OSError:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
