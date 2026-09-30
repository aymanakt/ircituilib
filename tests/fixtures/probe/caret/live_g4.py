#!/usr/bin/env python3
"""G-4 live checks against the shipped demo binary.

Stage 2b is the only stage whose behaviour lives in the demo's main loop:
timeout() turning idle time into getch()==ERR, the ERR branch ticking the blink
instead of dispatching, and curs_set(0) + leaveok handing the one cursor to the
engine. A unit probe cannot see any of that - it calls ui_blink_advance()
directly and never runs a getch() loop. This drives the real binary over a PTY
and reads a reconstructed screen, not the raw diff stream.

What this proves, and what it does not:

  - PROVES the blink reaches the screen on the real loop, that a focused caret
    alternates, that an unfocused field keeps a steady caret, that no second
    caret ever appears, that a non-edit focus produces no output at all over
    three seconds, and that the hardware cursor is switched off and never back
    on while running.
  - DOES NOT prove the N1 guard. ui_main.c filters ERR before calling
    ui_dispatch_key, so the shipped demo cannot reach the dispatcher with an ERR
    even without the guard. That hazard lives in the engine API for other
    callers, and its evidence is probe_s2b: 16/16 with the guard, 12/16 against
    a build with the three guard lines removed and nothing else changed.

Usage: live_g4.py /path/to/ui_demo
"""
import os, pty, select, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import Screen

# From infocmp for this TERM: kf7=\E[18~. (kcub1/kcuf1 are \EOD/\EOC, not the
# normal-mode \E[D - that one matches nothing here and falls through as text.)
F7 = b"\x1b[18~"
ESC = b"\x1b"
CARET = "┃"
CMDROW = 21          # cmdline, from --verify-layout at 80x24

# A check box inside a check_group is painted by the *group*, not by the
# UI_CHECK_BOX case - that one's "[x]"/"[ ]" only applies to a box outside a
# group, and the header buttons also render as "[ ... ]". So the marker for a
# grouped box is the ballot glyph.
UNCHECKED, CHECKED = "☐", "☑"


def pump(fd, sc, secs):
    """Drain the PTY for secs; feed the screen; return the bytes seen."""
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


def observe(fd, sc, secs, interval=0.06):
    """Sample the screen while draining. Returns (bytes, [(caret_on_row, total_carets)])."""
    out = bytearray()
    samples = []
    end = time.time() + secs
    while time.time() < end:
        out += pump(fd, sc, interval)
        samples.append((CARET in sc.line(CMDROW), sc.dump().count(CARET)))
    return bytes(out), samples


def flips(samples):
    return sum(1 for i in range(1, len(samples))
               if samples[i][0] != samples[i - 1][0])


def force_repaint(fd, sc):
    """Resize the pty one row down and back. KEY_RESIZE makes the demo relayout
    and re-realize, and ncurses redraws everything, so the screen is rebuilt
    from the tree rather than patched by a diff."""
    import fcntl, termios, struct
    for rows in (23, 24):
        fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, 80, 0, 0))
        pump(fd, sc, 0.4)


def click(fd, sc, row, col, settle=0.8):
    """SGR 1006 press+release at a 1-based column/row, then drain."""
    os.write(fd, b"\x1b[<0;%d;%dM" % (col + 1, row + 1))
    os.write(fd, b"\x1b[<0;%d;%dm" % (col + 1, row + 1))
    return pump(fd, sc, settle)


def main():
    binp = sys.argv[1]
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm-256color"
        os.environ["LINES"], os.environ["COLUMNS"] = "24", "80"
        os.execv(binp, [binp])

    sc = Screen(24, 80)
    checks = []
    raw = bytearray()

    def ok(name, cond, detail=""):
        checks.append((name, cond))
        print(("PASS" if cond else "FAIL"), name, detail)

    raw += pump(fd, sc, 1.2)

    # ---- 1. the caret is on screen and blinks -------------------------------
    print("1. THE FOCUSED CARET BLINKS ON THE REAL LOOP")
    ok("cmdline starts focused with a caret", CARET in sc.line(CMDROW),
       repr(sc.line(CMDROW)))
    ok("the hardware cursor was switched off",
       b"\x1b[?25l" in bytes(raw), "saw ?25l")

    out, s = observe(fd, sc, 3.6)
    raw += out
    lit = sum(1 for c, _ in s if c)
    ok("both blink phases reached the screen",
       lit > 0 and lit < len(s), "%d/%d samples lit" % (lit, len(s)))
    ok("the caret alternated on the real timer", flips(s) >= 4,
       "%d phase changes over 3.6s" % flips(s))
    ok("never more than one caret on screen",
       max(n for _, n in s) <= 1, "max %d" % max(n for _, n in s))

    # ---- 2. lose focus: steady caret, and no output at all ------------------
    print("\n2. FOCUS LEAVES THE FIELD  (steady caret, silent ticks)")
    raw += pump(fd, sc, 0.3)
    os.write(fd, F7)
    raw += pump(fd, sc, 1.0)
    cb = None
    for r in range(24):
        c = sc.line(r).find(UNCHECKED)
        if c >= 0:
            cb = (r, c)
            break
    ok("F7 opened the tools panel with a check box", cb is not None,
       "unchecked row at %r" % (cb,))
    if cb is None:
        print(sc.dump())
        sys.exit(1)

    # click the unchecked box: this moves focus onto the check group, which is
    # not an edit field, so nothing on screen should be animating afterwards
    raw += click(fd, sc, cb[0], cb[1] + 2)
    rowafter = sc.line(cb[0])
    ok("the click landed on the check box", CHECKED in rowafter, repr(rowafter))
    ok("the now-unfocused field still shows a caret",
       CARET in sc.line(CMDROW), repr(sc.line(CMDROW)))

    out, s = observe(fd, sc, 3.0)
    raw += out
    ok("a non-edit focus produces no output at all", len(out) == 0,
       "%d bytes over 3.0s" % len(out))
    ok("the unfocused caret stayed steady, never blinking",
       all(c for c, _ in s), "%d/%d samples lit" % (sum(1 for c, _ in s if c), len(s)))
    ok("the check box did not toggle while idle",
       CHECKED in sc.line(cb[0]), repr(sc.line(cb[0])))
    ok("still no second caret anywhere",
       max(n for _, n in s) <= 1, "max %d" % max(n for _, n in s))

    # ---- 3. focus back: it blinks again ------------------------------------
    print("\n3. FOCUS RETURNS  (it blinks again)")
    raw += click(fd, sc, CMDROW, 10)
    out, s = observe(fd, sc, 2.4)
    raw += out
    ok("the caret is back and alternating", flips(s) >= 2,
       "%d phase changes over 2.4s" % flips(s))

    # ---- 4. a lone ESC neither stalls nor leaks ----------------------------
    # The ESC path is checked through a forced full repaint, not the ordinary
    # one. A popup that is popped leaves its image on screen: ui_pop_stacked
    # does remove it (F7 F7 then a resize shows it gone), but the normal
    # ui_draw+doupdate pass does not clear the region it occupied. That is the
    # pre-existing blank-on-shrink repaint defect, unrelated to the caret and
    # out of scope here - so asserting on the un-repainted screen would be
    # testing that bug rather than ESC.
    print("\n4. A LONE ESC CLOSES THE POPUP  (checked through a forced repaint)")
    os.write(fd, ESC)
    raw += pump(fd, sc, 0.8)
    ok("ESC produced no text in the field",
       "[D" not in sc.line(CMDROW) and "27" not in sc.line(CMDROW),
       repr(sc.line(CMDROW)))
    force_repaint(fd, sc)
    ok("ESC closed the popup",
       UNCHECKED not in sc.dump() and CHECKED not in sc.dump(),
       "panel gone" if CHECKED not in sc.dump() else "STILL OPEN")
    out, s = observe(fd, sc, 1.8)
    raw += out
    ok("the blink survived the ESC", flips(s) >= 1,
       "%d phase changes over 1.8s" % flips(s))

    # ---- 5. one cursor, and it is the engine's -----------------------------
    print("\n5. ONE CURSOR  (P16)")
    ok("the hardware cursor was never switched back on",
       b"\x1b[?25h" not in bytes(raw), "no ?25h in %d bytes" % len(raw))

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
