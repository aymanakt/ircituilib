#!/usr/bin/env python3
"""Why does only the first click move the caret?

Diagnostic, not a test. Runs the same click at three different columns and
prints the cmdline row after each, varying only the gap between the press and
the release. If the gap is what decides it, the release is being folded into
something the engine does not recognise as a release.
"""
import os, pty, select, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import Screen

CMDROW = 21


def pump(fd, sc, secs):
    out = bytearray()
    end = time.time() + secs
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.03)
        if fd in r:
            try:
                c = os.read(fd, 65536)
            except OSError:
                break
            if not c:
                break
            out += c
            sc.feed(c)
    return bytes(out)


def main():
    binp = sys.argv[1]
    gap = float(sys.argv[2])
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm-256color"
        os.environ["LINES"], os.environ["COLUMNS"] = "24", "80"
        os.execv(binp, [binp])
    sc = Screen(24, 80)
    pump(fd, sc, 1.2)
    for ch in "abcdef":
        os.write(fd, ch.encode())
    pump(fd, sc, 0.5)
    print("after typing      : %r" % sc.line(CMDROW))

    # columns of c, e, a in the rendered "││> abcdef┃"
    for label, col in (("c at 6", 6), ("e at 9", 9), ("a at 4", 4), ("f at 10", 10)):
        os.write(fd, b"\x1b[<0;%d;%dM" % (col + 1, CMDROW + 1))
        if gap:
            pump(fd, sc, gap)
        os.write(fd, b"\x1b[<0;%d;%dm" % (col + 1, CMDROW + 1))
        pump(fd, sc, 0.5)
        print("click %-8s    : %r" % (label, sc.line(CMDROW)))

    os.write(fd, b"\x03")
    pump(fd, sc, 0.2)
    try:
        os.close(fd)
    except OSError:
        pass


if __name__ == "__main__":
    main()
