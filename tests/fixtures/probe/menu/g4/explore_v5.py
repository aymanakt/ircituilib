#!/usr/bin/env python3
"""Exploratory for V5/A10: find a staleness-immune discriminator for
"exactly one layer went"."""
import os
import sys

sys.path.insert(0, "/tmp/irctui-caret")
sys.path.insert(0, "/tmp/irctui-menu/g3")
from g3_menu import spawn, key, read, DOWN, ENTER, F8, CMDLINE_ROW, ITEMS  # noqa

fails = 0


def check(label, cond, detail):
    global fails
    print(("  PASS  %-56s %s" if cond else "  FAIL  %-56s %s") % (label, detail))
    if not cond:
        fails += 1


def click(fd, sc, sgr, row, col, button=0, wait=0.35):
    os.write(fd, b"\x1b[<%d;%d;%dM" % (button, col + 1, row + 1))
    read(fd, sc, sgr, 0.25)
    os.write(fd, b"\x1b[<%d;%d;%dm" % (button, col + 1, row + 1))
    return read(fd, sc, sgr, wait)


def rows(sc):
    return [(r, sc.line(r).rstrip()) for r in range(24) if sc.line(r).strip()]


def show(sc, label):
    print("=== %s ===" % label)
    for r, l in rows(sc):
        print("%2d %r" % (r, l))
    print()


def hi(sgr):
    return [sgr.last_is_reversed(t) for t in ITEMS]


binp = os.path.abspath(sys.argv[1])
os.chdir("/tmp/irctui-menu/cur")
pid, fd, sc, sgr = spawn(binp)

print("--- baseline: what does a character do with NO menu open? ---")
key(fd, sc, sgr, b"a", 0.5)
check("'a' with no menu lands in the cmdline", "a" in sc.line(CMDLINE_ROW),
      "row %d: %r" % (CMDLINE_ROW, sc.line(CMDLINE_ROW).strip()))
key(fd, sc, sgr, b"\x15", 0.4)   # ctrl-U, in case it clears

print("\n--- file menu open: does a character reach the cmdline? ---")
key(fd, sc, sgr, F8, 0.9)
check("file menu open, Help is the cursor", hi(sgr)[0] is True, "states=%s" % (hi(sgr),))
before = sc.line(CMDLINE_ROW)
key(fd, sc, sgr, b"z", 0.6)
after = sc.line(CMDLINE_ROW)
check("'z' is DROPPED while the file menu holds focus (not in the cmdline)",
      "z" not in after, "row %d before=%r after=%r" % (CMDLINE_ROW, before.strip(), after.strip()))

print("\n--- now stack the ctx menu (right-click the quit button) ---")
q = sc.find("quit")
check("the quit button is locatable", q is not None, "quit at %s" % (q,))
if q:
    click(fd, sc, sgr, q[0], q[1], button=2)
    show(sc, "after right-click quit")
    print("file_menu cursor after stacking: %s" % (hi(sgr),))
    print("ctx Close reversed: %s" % (sgr.last_is_reversed(b"Close"),))

    print("\n--- Enter: ctx_close ---")
    key(fd, sc, sgr, ENTER, 1.0)
    show(sc, "after Enter")
    print("file_menu cursor after Enter: %s" % (hi(sgr),))
    print("ctx Close reversed: %s" % (sgr.last_is_reversed(b"Close"),))

    print("\n--- the discriminator: type 'z' now ---")
    key(fd, sc, sgr, b"z", 0.7)
    check("'z' is STILL dropped => the file menu survived and holds focus",
          "z" not in sc.line(CMDLINE_ROW),
          "row %d: %r" % (CMDLINE_ROW, sc.line(CMDLINE_ROW).strip()))

    print("\n--- and the arrows still drive it ---")
    key(fd, sc, sgr, DOWN, 0.6)
    print("file_menu cursor after Down: %s" % (hi(sgr),))
    check("Down moved the file menu's cursor to item 2",
          hi(sgr) == [False, True, False], "states=%s" % (hi(sgr),))

try:
    os.write(fd, b"\x03")
    os.close(fd)
except OSError:
    pass
print("\n---- %s ----" % ("all passed" if not fails else "%d FAILED" % fails))
