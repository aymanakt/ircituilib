#!/usr/bin/env python3
"""Drive the real demo over a PTY and check the caret end-to-end.

The unit probes call ui_edit_input directly. This goes through the whole path a
user actually touches: a keystroke into getch, ui_dispatch_key, the focus node's
on_input, the painter, doupdate. Its job is to catch what a direct call cannot
show - an arrow key swallowed by the dispatcher before it reaches the field, the
demo stealing a key, or the caret never reaching the screen.

The screen is reconstructed with a real grid (pty_screen.py), not by searching
the raw diff stream.

Usage: live_caret.py /path/to/ui_demo
"""
import os, pty, select, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import Screen

# This terminfo puts the terminal in application cursor mode: kcub1=\EOD,
# kcuf1=\EOC. The normal-mode \E[D matches nothing and falls through as
# literal "[D" text into the field, so the sequence here must be the one
# ncurses actually advertised for TERM=xterm-256color.
LEFT, RIGHT, BS = b"\x1bOD", b"\x1bOC", b"\x7f"
CARET = "┃"


def pump(fd, sc, secs=0.5):
    end = time.time() + secs
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.1)
        if fd in r:
            try:
                c = os.read(fd, 8192)
            except OSError:
                break
            if not c:
                break
            sc.feed(c)


def main():
    binp = sys.argv[1]
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm-256color"
        os.environ["LINES"], os.environ["COLUMNS"] = "24", "80"
        os.execv(binp, [binp])

    sc = Screen(24, 80)
    checks = []

    def ok(name, cond, detail=""):
        checks.append((name, cond))
        print(("PASS" if cond else "FAIL"), name, detail)

    pump(fd, sc, 1.0)

    # Click the command line, the way a user would.
    row, col = 21, 10
    os.write(fd, b"\x1b[<0;%d;%dM" % (col + 1, row + 1))
    os.write(fd, b"\x1b[<0;%d;%dm" % (col + 1, row + 1))
    pump(fd, sc, 0.5)

    os.write(fd, b"abcdef")
    pump(fd, sc, 0.6)
    line = sc.line(row)
    ok("typing lands in the field", "abcdef" in line, repr(line))

    for _ in range(3):
        os.write(fd, LEFT)
        pump(fd, sc, 0.3)

    before = sc.line(row)
    os.write(fd, b"X")
    pump(fd, sc, 0.6)
    line = sc.line(row)
    bare = line.replace(CARET, "")
    ok("arrows moved the caret (X lands mid-string)",
       "abcXdef" in bare, repr(line))
    ok("the X was not appended", "abcdefX" not in bare, repr(line))
    ok("no arrow sequence leaked into the field as text",
       "[D" not in bare and "[C" not in bare, repr(line))

    # the caret must own the cell immediately before the d, with the X on its
    # left - i.e. it sits between them rather than replacing either
    if CARET in line:
        cc = line.index(CARET)
        ok("the caret is between X and d",
           line[cc - 1:cc] == "X" and line[cc + 1:cc + 2] == "d",
           "col=%d neighbours=%r" % (cc, line[cc - 1:cc + 2]))

    os.write(fd, BS)
    pump(fd, sc, 0.6)
    line = sc.line(row)
    bare = line.replace(CARET, "")
    ok("backspace removes the char left of the caret",
       "abcdef" in bare and "abcXdef" not in bare, repr(line))

    ok("the caret glyph reached the screen", CARET in sc.dump(),
       repr([l for l in sc.dump().split("\n") if CARET in l]))

    # Where exactly is the caret on the line?
    if CARET in line:
        cc = line.index(CARET)
        ok("the caret sits between c and d", line[cc - 3:cc] == "abc",
           "col=%d" % cc)

    os.write(fd, b"\x03")
    pump(fd, sc, 0.3)
    try:
        os.close(fd)
    except OSError:
        pass

    failed = [n for n, c in checks if not c]
    print("summary", f"{len(checks)-len(failed)}/{len(checks)}")
    if failed:
        print("final screen:")
        print(sc.dump())
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
