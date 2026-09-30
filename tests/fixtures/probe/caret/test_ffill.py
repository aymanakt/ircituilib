#!/usr/bin/env python3
"""The letter 'f' must type; F6 must still fill.

Adversarial framing: the interesting case is not "does F6 work" but "can any
printable character the user types reach a screen-destroying command". So this
types a string containing 'f' and several other letters, then checks the canvas
stayed empty and every character landed in the field - and separately that F6
still does its job, so the fix cannot have been "delete the feature".

Usage: test_ffill.py /path/to/ui_demo
"""
import os, pty, select, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import Screen

CMDROW = 21
CANVAS_ROWS = range(5, 18)
PROBE = "abcdef"          # the string that tripped it: contains 'f'
OTHER = "wolf fable of"   # several f's, plus spaces and other letters

fails = 0


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


def fox_rows(sc):
    return sum(1 for r in CANVAS_ROWS if "brown fox" in sc.line(r))


def check(label, cond, detail):
    global fails
    if cond:
        print("  PASS  %-56s %s" % (label, detail))
    else:
        fails += 1
        print("  FAIL  %-56s %s" % (label, detail))


def main():
    binp = sys.argv[1]
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm-256color"
        os.environ["LINES"], os.environ["COLUMNS"] = "24", "80"
        os.execv(binp, [binp])
    sc = Screen(24, 80)
    pump(fd, sc, 1.6)

    print("1. THE STRING THAT TRIPPED IT")
    for ch in PROBE:
        os.write(fd, ch.encode())
    pump(fd, sc, 0.7)
    check("typing %r leaves the canvas empty" % PROBE, fox_rows(sc) == 0,
          "brown fox rows: %d" % fox_rows(sc))
    check("every character landed in the field",
          PROBE in sc.line(CMDROW), "row %d: %r" % (CMDROW, sc.line(CMDROW).strip()))

    print("\n2. MORE f's, AND SPACES")
    os.write(fd, b"\x15")                      # Ctrl-U clear, if bound
    pump(fd, sc, 0.3)
    for ch in OTHER:
        os.write(fd, ch.encode())
    pump(fd, sc, 0.7)
    check("typing %r leaves the canvas empty" % OTHER, fox_rows(sc) == 0,
          "brown fox rows: %d" % fox_rows(sc))

    print("\n3. F6 STILL WORKS (the fix must not delete the feature)")
    os.write(fd, b"\x1b[17~")
    pump(fd, sc, 0.9)
    check("F6 still fills the canvas", fox_rows(sc) > 0,
          "brown fox rows: %d" % fox_rows(sc))

    print("\n4. AND THE POINT OF THE EXERCISE: DRAG STILL HIGHLIGHTS")
    # on the now-filled canvas, drag across a row
    c0, c1, y = 4, 11, 6
    os.write(fd, b"\x1b[<%d;%d;%dM" % (0, c0 + 1, y + 1))
    pump(fd, sc, 0.15)
    import re
    SGR = re.compile(rb"\x1b\[([0-9;]*)m")
    rev = 0
    for c in range(c0 + 1, c1 + 1):
        os.write(fd, b"\x1b[<32;%d;%dM" % (c + 1, y + 1))
        room = bytearray()
        end = time.time() + 0.10
        while time.time() < end:
            r, _, _ = select.select([fd], [], [], 0.02)
            if fd in r:
                d = os.read(fd, 65536)
                if not d:
                    break
                room += d
                sc.feed(d)
        for m in SGR.finditer(bytes(room)):
            if b"7" in [p for p in m.group(1).split(b";") if p]:
                rev += 1
    check("dragging the filled canvas paints inverse video", rev > 0,
          "%d reverse runs" % rev)

    print("\n---- %s ----" % ("all passed" if not fails else "%d FAILED" % fails))
    os.write(fd, b"\x03")
    pump(fd, sc, 0.3)
    try:
        os.close(fd)
    except OSError:
        pass
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
