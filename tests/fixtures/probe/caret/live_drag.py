#!/usr/bin/env python3
"""Does a left-click drag produce inverse video?

The user reports it does not, in input fields and text views. The unit probe
(probe_selnode section 2) shows the press+motion state DOES form a real range -
sel0=1, sel1=3 - so either the motion never arrives live, or the painter never
repaints the range.

pty_screen.py deliberately ignores SGR, so it cannot see attributes. This reads
the raw byte stream instead and looks for the reverse-video SGR that ncurses
emits for A_REVERSE.

Diagnostic first: it counts reverse-video runs before and during a drag rather
than asserting, so the shape of the failure is visible.

Usage: live_drag.py /path/to/ui_demo
"""
import os, pty, re, select, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import Screen

CMDROW = 21          # cmdline row, 0-based, from --verify-layout at 80x24

# ncurses does not emit a bare "\x1b[7m" for A_REVERSE: it folds the attribute
# into one SGR with everything else, e.g. "\x1b[0;1;7m". Counting the literal
# "\x1b[7m" therefore always reads zero however well the highlight works - which
# is exactly the false negative this file produced on its first run. Parse the
# parameter list instead.
SGR = re.compile(rb"\x1b\[([0-9;]*)m")


def rev_runs(data):
    """How many SGR sequences turn reverse video on."""
    n = 0
    for m in SGR.finditer(data):
        params = [p for p in m.group(1).split(b";") if p]
        if b"7" in params:
            n += 1
    return n


def pump(fd, sc, secs):
    """Read for @secs, feeding the screen model. Returns the raw bytes."""
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
    """SGR 1006 mouse: @kind 0 = press/release, 32 = motion with button 1."""
    return b"\x1b[<%d;%d;%d%s" % (kind, col + 1, row + 1,
                                  b"M" if kind == 0 else b"M")


def main():
    binp = sys.argv[1]
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

    line = sc.line(CMDROW)
    print("cmdline row after typing : %r" % line)
    i = line.find("abcdef")
    if i < 0:
        print("FAIL: the typed text is not on the cmdline row")
        os.write(fd, b"\x03")
        return 1
    col_b, col_e = i + 1, i + 4        # the b and the e in "abcdef"

    # --- the drag, with a real press, several motions, then release ---------
    os.write(fd, sgr(0, col_b, CMDROW))         # press on the b
    raw = pump(fd, sc, 0.10)
    print("\npress   : %d bytes, %d reverse runs" % (len(raw), rev_runs(raw)))

    move = bytearray()
    for c in range(col_b + 1, col_e + 1):
        os.write(fd, sgr(32, c, CMDROW))        # motion, button 1 held
        move += pump(fd, sc, 0.10)
    print("motion  : %d bytes, %d reverse runs" % (len(move), rev_runs(move)))
    print("          raw: %r" % bytes(move[:180]))

    # SGR 1006: 'M' is a press or a motion, 'm' is the release. The earlier
    # version of this line wrote 'M' here, sending a second press where the
    # comment said release; the real release was the line below, so the result
    # was unaffected, but the sequence was not what it claimed to be.
    os.write(fd, b"\x1b[<%d;%d;%dm" % (0, col_e + 1, CMDROW + 1))
    rel = pump(fd, sc, 0.40)
    print("release : %d bytes, %d reverse runs" % (len(rel), rev_runs(rel)))

    line = sc.line(CMDROW)
    print("\ncmdline row after drag  : %r" % line)
    print("total reverse runs in the drag window: %d"
          % (rev_runs(raw) + rev_runs(move) + rev_runs(rel)))

    os.write(fd, b"\x03")
    pump(fd, sc, 0.3)
    try:
        os.close(fd)
    except OSError:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
