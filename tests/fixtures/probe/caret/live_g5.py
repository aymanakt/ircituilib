#!/usr/bin/env python3
"""Stage 3 live check: click-to-position on the shipped demo binary.

The unit probe injects events with ungetmouse(); this one writes the escape
sequences a terminal actually sends and reads a reconstructed screen, so it
covers the path the user's requirement names - click mid-string and type.

The assertion is deliberately screen-level rather than positional: after
clicking the column a character occupies, that character must be the one
immediately right of the caret. That is what "the click put the cursor where I
pointed" means to a user, and it is immune to how many cells the caret itself
takes, which is the thing a positional assertion would have to guess.

The caret blinks, so a sample may catch the dark half. The cell is reserved
either way, so the glyph is the only thing that comes and goes - wait for a lit
sample before reading the position.

Usage: live_g5.py /path/to/ui_demo
"""
import os, pty, select, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import Screen

CARET = "┃"
CMDROW = 21          # cmdline, from --verify-layout at 80x24


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


def click(fd, sc, row, col, settle=0.35):
    """SGR 1006 press and release, separated like a real click.

    The gap is not cosmetic. A press and release that are pending in the same
    ncurses read are folded into a lone BUTTON1_CLICKED even with
    mouseinterval(0) (measured with mousebits.c); a lone CLICKED sets the
    engine's sel_node and no RELEASED ever arrives to clear it, after which
    every mouse event is taken by the drag branch. Writing them back to back
    makes the test click faster than any hand can, and it would then be
    measuring that engine hazard rather than click-to-position. Any gap at all
    avoids it - a real click is 50-150 ms."""
    os.write(fd, b"\x1b[<0;%d;%dM" % (col + 1, row + 1))
    pump(fd, sc, 0.08)
    os.write(fd, b"\x1b[<0;%d;%dm" % (col + 1, row + 1))
    return pump(fd, sc, settle)


def text_origin(line):
    """Screen column where the field's text starts: past the '> ' prompt."""
    i = line.find("> ")
    return None if i < 0 else i + 2


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

    def lit_line(timeout=3.0):
        """The cmdline row, sampled during a lit half of the blink."""
        end = time.time() + timeout
        while time.time() < end:
            pump(fd, sc, 0.05)
            if CARET in sc.line(CMDROW):
                return sc.line(CMDROW)
        return sc.line(CMDROW)

    def text_of(line):
        """The field's text as the user reads it. The caret owns a cell and is
        drawn in it, so in the lit phase removing the glyph leaves the text; in
        the dark phase the cell is a blank, which is why callers sample a lit
        line first."""
        return line.replace(CARET, "")

    pump(fd, sc, 1.2)

    line = sc.line(CMDROW)
    ok("the cmdline row renders a prompt", text_origin(line) is not None,
       repr(line))

    # ---- type six characters ------------------------------------------------
    print("\n1. TYPE A STRING, THEN CLICK ON THE CHARACTER YOU WANT")
    for ch in "abcdef":
        os.write(fd, ch.encode())
    pump(fd, sc, 0.4)
    line = lit_line()
    ok("the six characters landed", "abcdef" in text_of(line), repr(line))

    # ---- click on the 'c', from a caret parked at the end -------------------
    # The caret takes the cell that was clicked and pushes what was there one
    # column right, so clicking the c gives ab|caret|cdef, not abc|caret|def.
    line = lit_line()
    i = line.index("abcdef")
    col_c = i + 2                        # index of 'c' in "abcdef"
    click(fd, sc, CMDROW, col_c)
    line = lit_line()
    ok("clicking the c takes the c's cell", "ab" + CARET + "cdef" in line,
       repr(line))

    # ---- and now a click to the RIGHT of where the caret already is ---------
    # This is the case the plan's literal index_for_col() gets wrong: the caret
    # owns a cell, so everything right of it is drawn one column further right
    # than its own column. Re-read the line, because the c just moved.
    print("\n2. CLICK AGAIN, TO THE RIGHT OF THE CARET  (the off-by-one trap)")
    line = lit_line()
    j = line.index(CARET)
    col_e = line.index("e", j)           # 'e' as the user currently sees it
    ok("the e is where the caret shift puts it",
       line[col_e] == "e" and col_e == j + 3,
       "caret at %d, e at %d" % (j, col_e))
    click(fd, sc, CMDROW, col_e)
    line = lit_line()
    ok("clicking the e lands before the e, not the f",
       "abcd" + CARET + "ef" in line, repr(line))

    # ---- the acceptance test ------------------------------------------------
    print("\n3. THE STATED SCENARIO  (click between c and d, type X)")
    line = lit_line()
    col_d = line.index("d")
    click(fd, sc, CMDROW, col_d)
    line = lit_line()
    ok("the click put the caret between c and d", "abc" + CARET + "def" in line,
       repr(line))
    os.write(fd, b"X")
    pump(fd, sc, 0.4)
    line = lit_line()
    ok("typing X mid-string gives abcXdef", "abcXdef" in text_of(line),
       repr(line))

    os.write(fd, b"\x7f")                # backspace
    pump(fd, sc, 0.3)
    line = lit_line()
    ok("backspace removes it again", "abcdef" in text_of(line), repr(line))

    # ---- one caret, and the hardware cursor stays off ------------------------
    print("\n4. ONE CURSOR")
    ok("exactly one caret is on screen", sc.dump().count(CARET) == 1,
       "%d carets" % sc.dump().count(CARET))

    os.write(fd, b"\x03")
    pump(fd, sc, 0.3)
    try:
        os.close(fd)
    except OSError:
        pass

    failed = [n for n, c in checks if not c]
    print("\nsummary %d/%d" % (len(checks) - len(failed), len(checks)))
    if failed:
        print("failed:", *failed, sep="\n  ")
        print("final screen:")
        print(sc.dump())
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
