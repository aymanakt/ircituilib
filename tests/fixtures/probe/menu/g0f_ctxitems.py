#!/usr/bin/env python3
"""G-1 behaviour-preservation: the four text context-menu items must still work.

G-1 changes where the pop happens for every id that reaches the close branch. The
claim is that it is behaviour-preserving for ctx_copy / ctx_selall / ctx_paste /
ctx_clear and only fixes the double pop. That claim is traced in the plan; this
measures it.

The items are exercised as a clipboard round-trip against the command line, which
makes each one observable in the rendered screen rather than inferred:

    type "hello"  ->  Select all  ->  Copy  ->  Clear  ->  Paste  ->  "hello"

Copy needs a selection, so Select all runs first; Clear is what makes the final
Paste meaningful. After every click the context menu must be gone AND the file
menu underneath must still be there - that second half is the G-1 fix, and it also
proves each action still pops exactly one layer.

The command line is the right-click target because it is an edit_text: all four
items act on it and none of them can disturb the canvas.
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


def status_line(sc):
    for r in range(24):
        l = sc.line(r)
        if "click " in l:
            return l.strip("│| ")
    return ""


def find_not_at(sc, needle, avoid_col_band):
    """First occurrence outside the file menu's column band, so the file menu's
    own items are never mistaken for the context menu's."""
    lo, hi = avoid_col_band
    for r in range(24):
        l = sc.line(r)
        x = l.find(needle)
        if x >= 0 and not (lo <= x < hi):
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

    print("1. TYPE INTO THE COMMAND LINE")
    for ch in "hello":
        os.write(fd, ch.encode())
    pump(fd, sc, 0.6)
    check("the field holds the text", "hello" in sc.line(CMDLINE_ROW),
          "row %d: %r" % (CMDLINE_ROW, sc.line(CMDLINE_ROW).strip()))

    print("\n2. OPEN THE FILE MENU (the layer that must survive)")
    os.write(fd, F8)
    pump(fd, sc, 0.7)
    fm = sc.find("Close")
    check("file menu open", fm is not None, "mi_close at %s" % (fm,))
    if fm is None:
        return 1
    # the file menu's column band, so context-menu hits are unambiguous
    fmband = (fm[1] - 6, fm[1] + 10)

    print("\n3. THE FOUR TEXT ITEMS, AS A CLIPBOARD ROUND-TRIP")
    # "The context menu disappeared" is NOT asserted by looking for its label:
    # closing a popup leaves the vacated cells stale on screen (a pre-existing
    # repaint defect, unrelated to G-1), so the labels of the upper rows survive
    # the close as residue. Two observables that are immune to that:
    #   - the status echo, which names the item the click resolved to, proving the
    #     branch ran and the clip action executed;
    #   - the file menu, which must still be live (checked at the end by clicking
    #     it, not by reading pixels).
    sequence = [("Select all", "ctx_selall"), ("Copy", "ctx_copy"),
                ("Clear", "ctx_clear"), ("Paste", "ctx_paste")]
    for name, eid in sequence:
        right_click(fd, sc, CMDLINE_ROW, 4)
        pos = find_not_at(sc, name, fmband)
        if pos is None:
            check("context menu offers %r" % name, False,
                  "not on screen; menu rows: %s"
                  % [sc.line(r).strip() for r in range(14, 21)])
            return 1
        click(fd, sc, pos[0], pos[1])
        shown = status_line(sc)
        check("%-10s -> reached %s and ran its action" % (name, eid),
              shown == "click " + eid, "status row reads %r" % (shown,))

    print("\n4. THE CLIPBOARD ROUND-TRIP COMPLETED")
    line = sc.line(CMDLINE_ROW)
    check("the text came back through Copy/Clear/Paste", "hello" in line,
          "row %d: %r" % (CMDLINE_ROW, line.strip()))

    print("\n5. THE FILE MENU IS STILL A LIVE LAYER (not stale pixels)")
    # Clicking its own item is the staleness-immune check: a residue of the file
    # menu would not answer a click, and a second pop would have taken it away.
    mo = sc.find("Open")
    check("the file menu's Open item is on screen", mo is not None, "at %s" % (mo,))
    if mo is not None:
        click(fd, sc, mo[0], mo[1])
        shown = status_line(sc)
        check("clicking it is answered by the engine (it is really there)",
              "mi_open" in shown, "status row reads %r" % (shown,))

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
