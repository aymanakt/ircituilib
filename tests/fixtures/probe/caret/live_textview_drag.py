#!/usr/bin/env python3
"""The text-view half of the report: drag-select on the canvas.

The user said "input_field, text_field etc". The cmdline covers the edit field.
The canvas is a UI_TEXT_VIEW at 2,5 76x13 with no draggable ancestor, so it
reaches the sel_node path at ui_engine.c:1361 (a draggable ancestor would take
the drag_node branch at :1352 first - neither emoji_picker nor tools_panel
contains it). This measures whether the drag paints inverse video there.

Same detector as live_drag.py: ncurses folds A_REVERSE into a combined SGR
(\\x1b[0;1;7m), so the parameter list must be parsed, not the literal "\\x1b[7m".
"""
import os, pty, re, select, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import Screen

CANVAS_Y = 6          # one row inside the canvas at 2,5 76x13
SGR = re.compile(rb"\x1b\[([0-9;]*)m")


def rev_runs(data):
    n = 0
    for m in SGR.finditer(data):
        params = [p for p in m.group(1).split(b";") if p]
        if b"7" in params:
            n += 1
    return n


def pump(fd, sc, secs):
    got = bytearray()
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
        got += c
        sc.feed(c)
    return bytes(got)


def sgr(kind, col, row):
    return b"\x1b[<%d;%d;%dM" % (kind, col + 1, row + 1)


def main():
    binp = sys.argv[1]
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm-256color"
        os.environ["LINES"], os.environ["COLUMNS"] = "24", "80"
        os.execv(binp, [binp])
    sc = Screen(24, 80)
    pump(fd, sc, 1.4)
    # The canvas starts empty, and a selection over empty cells paints nothing -
    # the first version of this test read 0 reverse runs on BOTH builds for that
    # reason and proved nothing. F6 runs ui_demo_fill (ui_engine.c:2189), which
    # is what gives the canvas text to select.
    os.write(fd, b"\x1b[17~")            # F6
    pump(fd, sc, 0.9)
    print("canvas row %d : %r" % (CANVAS_Y, sc.line(CANVAS_Y).rstrip()))

    c0, c1 = 4, 12
    os.write(fd, sgr(0, c0, CANVAS_Y))          # press
    raw = pump(fd, sc, 0.12)
    move = bytearray()
    for c in range(c0 + 1, c1 + 1):
        os.write(fd, sgr(32, c, CANVAS_Y))      # motion with button 1 held
        move += pump(fd, sc, 0.10)
    os.write(fd, b"\x1b[<%d;%d;%dm" % (0, c1 + 1, CANVAS_Y + 1))   # release
    rel = pump(fd, sc, 0.40)

    print("press   : %d bytes, %d reverse runs" % (len(raw), rev_runs(raw)))
    print("motion  : %d bytes, %d reverse runs" % (len(move), rev_runs(move)))
    print("release : %d bytes, %d reverse runs" % (len(rel), rev_runs(rel)))
    print("TOTAL reverse runs on the canvas drag: %d"
          % (rev_runs(raw) + rev_runs(move) + rev_runs(rel)))
    if rev_runs(move):
        print("raw: %r" % bytes(move[:200]))

    os.write(fd, b"\x03")
    pump(fd, sc, 0.3)
    try:
        os.close(fd)
    except OSError:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
