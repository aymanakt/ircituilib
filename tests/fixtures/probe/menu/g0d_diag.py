#!/usr/bin/env python3
"""Diagnostic: in the 20-round loop, where does the status echo actually go?

Section C of g0b shows "click mi_close" in the stream; section D shows none, yet
every round dismisses the menu. This dumps the footer rows and greps the raw
stream for every echo spelling, so the difference is observed rather than guessed.
"""
import os
import pty
import select
import sys
import time

sys.path.insert(0, "/tmp/irctui-caret")
from pty_screen import Screen

F8 = b"\x1b[19~"


def pump(fd, sc, secs, sink=None):
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
        if sink is not None:
            sink += c
    return True


def footer(sc):
    return [(r, sc.line(r)) for r in range(19, 24)]


def main():
    binp = os.path.abspath(sys.argv[1])
    os.chdir(os.environ.get("PROBE_CWD", "/tmp/irctui-menu/cur"))
    os.environ["ASAN_OPTIONS"] = "detect_leaks=0"
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm-256color"
        os.environ["LINES"], os.environ["COLUMNS"] = "24", "80"
        os.execv(binp, [binp])
    sc = Screen(24, 80)
    out = bytearray()
    pump(fd, sc, 1.6, out)

    print("=== baseline footer ===")
    for r, l in footer(sc):
        print("  row %2d %r" % (r, l.rstrip()))

    print("\n=== ROUND 0: F8, then click Help ===")
    os.write(fd, F8)
    pump(fd, sc, 0.4, out)
    h = sc.find("Help")
    print("  Help found at %s" % (h,))
    raw0 = bytearray()
    os.write(fd, b"\x1b[<0;%d;%dM" % (h[1] + 1, h[0] + 1))
    pump(fd, sc, 0.25, raw0)
    os.write(fd, b"\x1b[<0;%d;%dm" % (h[1] + 1, h[0] + 1))
    pump(fd, sc, 0.25, raw0)
    print("  stream for this click: %d bytes; 'click mi_help': %s; 'btn mi_help': %s"
          % (len(raw0), b"click mi_help" in bytes(raw0), b"btn mi_help" in bytes(raw0)))
    for r, l in footer(sc):
        print("  row %2d %r" % (r, l.rstrip()))

    print("\n=== ROUND 0 continued: click Close ===")
    c = sc.find("Close")
    print("  Close found at %s" % (c,))
    raw1 = bytearray()
    os.write(fd, b"\x1b[<0;%d;%dM" % (c[1] + 1, c[0] + 1))
    pump(fd, sc, 0.25, raw1)
    os.write(fd, b"\x1b[<0;%d;%dm" % (c[1] + 1, c[0] + 1))
    pump(fd, sc, 0.25, raw1)
    print("  stream for this click: %d bytes; 'click mi_close': %s; 'btn mi_close': %s"
          % (len(raw1), b"click mi_close" in bytes(raw1), b"btn mi_close" in bytes(raw1)))
    print("  menu gone: %s" % (sc.find("Close") is None,))
    for r, l in footer(sc):
        print("  row %2d %r" % (r, l.rstrip()))

    print("\n=== every echo spelling present in the WHOLE stream so far ===")
    t = bytes(out)
    for pat in (b"click mi_help", b"click mi_close", b"btn mi_help", b"btn mi_close",
                b"click ", b"btn "):
        print("  %-16s %d" % (pat.decode(), t.count(pat)))

    os.write(fd, b"\x03")
    pump(fd, sc, 0.3)
    try:
        os.close(fd)
    except OSError:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
