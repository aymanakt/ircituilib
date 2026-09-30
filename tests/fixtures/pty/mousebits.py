#!/usr/bin/env python3
"""Drive mousebits.c over a PTY and report the event shapes it receives.

Three ways a terminal can deliver one click:
  press+release back to back   (what a fast click looks like on the wire)
  press, pause, release        (a slow click, longer than any click interval)
  release only                 (should not happen, but cheap to rule out)
"""
import os, pty, select, sys, time

BIN = sys.argv[1] if len(sys.argv) > 1 else "/tmp/irctui-caret/mousebits"
INTERVAL = sys.argv[2] if len(sys.argv) > 2 else "0"

PRESS = b"\x1b[<0;10;5M"
RELEASE = b"\x1b[<0;10;5m"


def pump(fd, secs):
    out = bytearray()
    end = time.time() + secs
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.05)
        if fd in r:
            try:
                c = os.read(fd, 65536)
            except OSError:
                break
            if not c:
                break
            out += c
    return bytes(out)


def case(name, seq, gap):
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm-256color"
        os.environ["LINES"], os.environ["COLUMNS"] = "24", "80"
        os.execv(BIN, [BIN, INTERVAL])
    out = pump(fd, 0.6)
    for chunk in seq:
        os.write(fd, chunk)
        out += pump(fd, gap)
    out += pump(fd, 0.6)
    os.write(fd, b"q")
    pump(fd, 0.2)
    try:
        os.close(fd)
    except OSError:
        pass
    lines = [l for l in out.decode("utf-8", "replace").splitlines()
             if "bstate=" in l]
    print("%-28s -> %s" % (name, " | ".join(lines) if lines else "(nothing)"))


print("mouseinterval=%s" % INTERVAL)
case("press+release, immediate", [PRESS, RELEASE], 0.02)
case("press, 0.5s, release", [PRESS, RELEASE], 0.5)
case("release only", [RELEASE], 0.02)
