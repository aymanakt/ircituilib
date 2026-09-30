#!/usr/bin/env python3
"""G-0e: does a ctx_* click pop one layer, or two?

ui_button_input's close branch runs two independent pops:

    :1884   else ui_close_top_popup();                        <- pop #1
    :1885   if (n->id && strncmp(n->id, "ctx_", 4) == 0)
    :1886       ui_close_top_popup();                         <- pop #2

For the four text items the chain at :1872-1883 takes a clip action, so :1884 is
the else and does NOT run - they pop once, which is correct. But the non-text
context menu's only item is id "ctx_close" (ui_engine.c:2187), whose id contains
"close", so :1864 matches, the clip chain all falls through, :1884 pops, and then
:1885 matches "ctx_" and :1886 pops AGAIN.

ui_close_top_popup pops whatever ui_top_layer() returns - the last STACKED root
child. So the second pop should take out the layer underneath: the file menu.

This measures it: open the file menu, right-click a header button so the context
menu stacks over it, click the context menu's Close, then see how many layers are
left. Correct behaviour leaves the file menu; the bug takes both.
"""
import os
import pty
import select
import sys
import time

sys.path.insert(0, "/tmp/irctui-caret")
from pty_screen import Screen

F8 = b"\x1b[19~"

fails = 0


def check(label, cond, detail):
    global fails
    if cond:
        print("  PASS  %-56s %s" % (label, detail))
    else:
        fails += 1
        print("  FAIL  %-56s %s" % (label, detail))


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


def click(fd, sc, row, col, button=0):
    os.write(fd, b"\x1b[<%d;%d;%dM" % (button, col + 1, row + 1))
    pump(fd, sc, 0.25)
    os.write(fd, b"\x1b[<%d;%d;%dm" % (button, col + 1, row + 1))
    pump(fd, sc, 0.25)


def find_in_cols(sc, needle, cmin, cmax, rmin=0, rmax=24):
    """find() returns the first hit anywhere; this confines the search to the
    context menu's column band so the file menu's own 'Close' is not mistaken
    for it."""
    for r in range(rmin, rmax):
        l = sc.line(r)
        x = l.find(needle)
        if x >= 0 and cmin <= x < cmax:
            return (r, x)
    return None


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

    print("1. OPEN THE FILE MENU (layer 1)")
    os.write(fd, F8)
    pump(fd, sc, 0.8)
    fm = find_in_cols(sc, "Close", 0, 64)      # the file menu sits left of col 64
    check("file menu open", fm is not None, "mi_close at %s" % (fm,))
    if fm is None:
        return 1

    print("\n2. RIGHT-CLICK A HEADER BUTTON, STACKING THE CONTEXT MENU (layer 2)")
    q = sc.find("quit")
    check("a header button is locatable", q is not None, "quit=%s" % (q,))
    if q is None:
        return 1
    click(fd, sc, q[0], q[1], button=2)
    cm = find_in_cols(sc, "Close", 64, 80)     # the ctx menu is clamped to col 64+
    check("the context menu opened to the right of the file menu", cm is not None,
          "ctx Close at %s (file menu still at %s)" % (cm, find_in_cols(sc, "Close", 0, 64)))
    print("     screen:")
    for r in range(2, 9):
        print("       row %2d %r" % (r, sc.line(r).rstrip()))
    if cm is None:
        return 1
    check("both layers are on screen before the click",
          find_in_cols(sc, "Close", 0, 64) is not None,
          "file menu present: %s" % (find_in_cols(sc, "Close", 0, 64),))

    print("\n3. CLICK THE CONTEXT MENU'S Close - exactly one layer should go")
    click(fd, sc, cm[0], cm[1])
    ctx_left = find_in_cols(sc, "Close", 64, 80)
    fm_left = find_in_cols(sc, "Close", 0, 64)
    print("     screen after the click:")
    for r in range(2, 9):
        print("       row %2d %r" % (r, sc.line(r).rstrip()))
    check("the context menu is gone", ctx_left is None,
          "ctx Close at %s" % (ctx_left,))
    check("the file menu is STILL there (one pop, not two)", fm_left is not None,
          "file menu mi_close at %s" % (fm_left,))
    print("\n     => %s" % (
        "ONE pop - the double-pop does not occur here"
        if fm_left is not None else
        "TWO pops - the ctx_close click took the file menu with it"))

    print("\n---- %s ----" % ("all passed" if not fails else "%d FAILED" % fails))
    try:
        os.write(fd, b"\x03")
        pump(fd, sc, 0.3)
    except OSError:
        pass
    try:
        os.close(fd)
    except OSError:
        pass
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
