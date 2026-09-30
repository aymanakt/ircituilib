#!/usr/bin/env python3
"""How long does a lone ESC take to be resolved, and does it close a popup?

N2 says timeout() makes ncurses resolve a lone ESC against ESCDELAY, so a
1000 ms default would hold Esc for up to a whole blink tick. This measures the
delay to the byte, for a build with set_escdelay(25) and one without, so the
claim is a measurement rather than a reading of the source.

Usage: esc_latency.py BIN [--expect-close|--expect-noclose]
"""
import os, pty, select, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import Screen

F7 = b"\x1b[18~"
PANEL = "☐"          # an unchecked box only exists in the tools panel


def pump(fd, sc, secs):
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
        sc.feed(c)
    return bytes(got)


def main():
    binp = sys.argv[1]
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm-256color"
        os.environ["LINES"], os.environ["COLUMNS"] = "24", "80"
        os.execv(binp, [binp])

    sc = Screen(24, 80)
    pump(fd, sc, 1.2)
    os.write(fd, F7)
    pump(fd, sc, 1.2)
    print("panel open:", PANEL in sc.dump())

    # Send ESC and time how long until *anything* comes back. A closing popup
    # must repaint, and a repaint is output - so the first byte after the ESC is
    # the resolution moment plus its repaint, both well under a blink tick.
    t0 = time.time()
    os.write(fd, b"\x1b")
    first = None
    out = bytearray()
    end = t0 + 4.0
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.02)
        if fd not in r:
            continue
        c = os.read(fd, 65536)
        if not c:
            break
        if first is None:
            first = time.time() - t0
        out += c
        sc.feed(c)
    print("first byte after ESC: %s" % ("%.0f ms" % (first * 1000) if first is not None else "NONE"))
    print("bytes after ESC: %d" % len(out))
    print("panel still open:", PANEL in sc.dump())
    os.write(fd, b"\x03")
    pump(fd, sc, 0.2)
    try:
        os.close(fd)
    except OSError:
        pass


if __name__ == "__main__":
    main()
