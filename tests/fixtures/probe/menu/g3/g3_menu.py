#!/usr/bin/env python3
"""G-3: the menu keyboard contract, measured.

The ask: posting a menu highlights its FIRST item, Up/Down move the highlight
over the items, and Enter dispatches the item and makes the menu disappear -
canonically, for any container that declares its children selectable.

Reverse video is the observable for "highlighted", and pty_screen.Screen keeps
characters only, so this tracks SGR state from the byte stream. The spelling is
taken from the bytes, not guessed: ncurses emits the highlight as

    \\x1b[5;48H \\x1b(B\\x1b[0;7m\\x1b[37m\\x1b[40m[ Help ]  ...

so the test is "is attribute 7 active just before this label", accumulated
across escapes rather than read off the single last one, because a following
\\x1b[37m does not clear reverse but would look like the current state.

Arrow keys are kcuu1=\\x1bOA / kcud1=\\x1bOB for TERM=xterm-256color; \\x1b[A and
\\x1b[B are read by ncurses as a bare ESC, which closes the popup.

Usage: g3_menu.py <binary>
"""
import os
import pty
import re
import select
import sys
import time

sys.path.insert(0, "/tmp/irctui-caret")
from pty_screen import Screen

UP, DOWN, ENTER = b"\x1bOA", b"\x1bOB", b"\r"
F8 = b"\x1b[19~"
CMDLINE_ROW = 21
TOKEN = re.compile(rb"\x1b\[([0-9;?]*)([A-Za-z])")

ITEMS = [b"[ Help ]", b"[ Open ]", b"[ Close ]"]

fails = 0


def check(label, cond, detail):
    global fails
    if cond:
        print("  PASS  %-58s %s" % (label, detail))
    else:
        fails += 1
        print("  FAIL  %-58s %s" % (label, detail))


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


class Sgr:
    """Accumulated SGR state at any byte offset of the whole session."""

    def __init__(self):
        self.buf = b""

    def feed(self, d):
        self.buf += d

    def _state(self, end):
        rev = False
        for m in TOKEN.finditer(self.buf[:end]):
            if m.group(2) != b"m":
                continue
            raw = m.group(1)
            # "\x1b[m" - an SGR with NO parameters - is a full reset, not a
            # no-op. ncurses ends every repaint delta with "\x1b(B\x1b[m", so
            # treating empty params as "nothing to do" leaves reverse latched on
            # forever and reports a highlight that has already been taken away.
            if raw == b"":
                rev = False
                continue
            for x in raw.split(b";"):
                if not x.isdigit():
                    continue
                v = int(x)
                if v in (0, 27):
                    rev = False
                elif v == 7:
                    rev = True
        return rev

    def last_is_reversed(self, needle):
        """Is the LAST occurrence of needle on screen currently reversed?"""
        i = self.buf.rfind(needle)
        if i < 0:
            return None
        return self._state(i)

    def count(self, needle):
        return self.buf.count(needle)


def stars(sc):
    return [(r, c) for r in range(24)
            for c, ch in enumerate(sc.line(r)) if ch == "*"]


def spawn(binp):
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm-256color"
        os.environ["LINES"], os.environ["COLUMNS"] = "24", "80"
        os.execv(binp, [binp])
    sc = Screen(24, 80)
    sgr = Sgr()
    pump(fd, sc, 1.6)
    return pid, fd, sc, sgr


def read(fd, sc, sgr, secs):
    """pump, but route the raw bytes into the SGR model as well."""
    start = len(sgr.buf)
    end = time.time() + secs
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.05)
        if fd not in r:
            continue
        try:
            c = os.read(fd, 65536)
        except OSError:
            return b""
        if not c:
            return b""
        sc.feed(c)
        sgr.feed(c)
    return sgr.buf[start:]


def key(fd, sc, sgr, k, secs=0.45):
    os.write(fd, k)
    return read(fd, sc, sgr, secs)


def kill(fd, sc, sgr):
    try:
        os.write(fd, b"\x03")
        read(fd, sc, sgr, 0.3)
    except OSError:
        pass
    try:
        os.close(fd)
    except OSError:
        pass


# The exact bytes the engine emitted for the first two Downs, captured from a
# live run. Feeding them to the parser pins its behaviour independently of the
# engine, so a later "fix" to the parser cannot be tuned to make the suite pass.
SELFTEST_DOWN1 = (b'\x1b[5;49H\x1b[37m\x1b[40m[ Help ]  '
                  b'\x1b[6;49H\x1b(B\x1b[0;7m\x1b[37m\x1b[40m[ Open ]  '
                  b'\x1b(B\x1b[m\x1b[39;49m\x1b[37m\x1b[40m')
SELFTEST_DOWN2 = (b'\x1b[6;49H\x1b[37m\x1b[40m[ Open ]  '
                  b'\x1b[7;49H\x1b(B\x1b[0;7m\x1b[37m\x1b[40m[ Close ] '
                  b'\x1b(B\x1b[m\x1b[39;49m\x1b[37m\x1b[40m')


def selftest():
    print("V0. the reverse-video reader itself (parser self-test)")
    s = Sgr()
    s.feed(SELFTEST_DOWN1)
    got = [s.last_is_reversed(t) for t in ITEMS]
    # None, not False, for Close: Down 1's delta never touches that row, so the
    # label is not in the buffer at all. None means "no evidence", which is the
    # honest answer and must not be confused with "painted without reverse".
    check("after Down 1 the parser reads Help plain, Open reversed",
          got == [False, True, None], "states = %s (None = row not repainted)" % (got,))
    s.feed(SELFTEST_DOWN2)
    got = [s.last_is_reversed(t) for t in ITEMS]
    check("after Down 2 the parser reads ONLY Close reversed",
          got == [False, False, True], "states = %s" % (got,))
    print()


def main():
    binp = os.path.abspath(sys.argv[1])
    selftest()
    os.chdir(os.environ.get("PROBE_CWD", "/tmp/irctui-menu/cur"))
    os.environ.setdefault("ASAN_OPTIONS", "detect_leaks=0")
    pid, fd, sc, sgr = spawn(binp)

    # ---- V1: the first item is highlighted, on open -------------------------
    print("V1. posting the menu highlights its FIRST item")
    d = key(fd, sc, sgr, F8, 0.9)
    states = [sgr.last_is_reversed(t) for t in ITEMS]
    check("the first item is highlighted", states[0] is True,
          "reverse(Help,Open,Close) = %s" % (states,))
    check("the second item is NOT highlighted", states[1] is False, "Open plain")
    check("the third item is NOT highlighted", states[2] is False, "Close plain")

    # The cursor-move immediately preceding the highlight must land on the Help
    # row (screen row 5, 1-based), not on Open or Close. This is what separates
    # "the cursor is on child 0" from "child 0 happens to be highlighted".
    i = sgr.buf.rfind(ITEMS[0])
    lastrow = None
    for m in TOKEN.finditer(sgr.buf[:i]):
        if m.group(2) in (b"H", b"f"):
            p = m.group(1).split(b";")
            if p and p[0].isdigit():
                lastrow = int(p[0])
    check("the highlight is written on the Help row (5), not a later row",
          lastrow == 5, "last CUP row before the reverse run = %s" % (lastrow,))

    # A9: the container is focused but must not stamp '*' on its own frame.
    menu_rows = [r for r in range(3, 9)]
    marks = [(r, c) for (r, c) in stars(sc) if r in menu_rows]
    check("no stray '*' on the focused menu frame", not marks,
          "markers in the menu rows: %s" % (marks,))

    # ---- V2: the clamp ------------------------------------------------------
    print("\nV2. Down/Down and Up clamp instead of wrapping or running away")
    key(fd, sc, sgr, DOWN)
    check("one Down moves the highlight to item 2",
          [sgr.last_is_reversed(t) for t in ITEMS] == [False, True, False],
          "states = %s" % ([sgr.last_is_reversed(t) for t in ITEMS],))
    key(fd, sc, sgr, DOWN)
    check("two Downs move it to item 3",
          [sgr.last_is_reversed(t) for t in ITEMS] == [False, False, True],
          "states = %s" % ([sgr.last_is_reversed(t) for t in ITEMS],))
    key(fd, sc, sgr, DOWN)
    key(fd, sc, sgr, DOWN)
    check("further Downs clamp on item 3 (no wrap, no 4th row)",
          [sgr.last_is_reversed(t) for t in ITEMS] == [False, False, True]
          and not any(sc.line(r).strip("| ") .startswith("4") for r in range(3, 10)),
          "states = %s" % ([sgr.last_is_reversed(t) for t in ITEMS],))
    key(fd, sc, sgr, UP)
    key(fd, sc, sgr, UP)
    key(fd, sc, sgr, UP)
    check("Ups clamp on item 1",
          [sgr.last_is_reversed(t) for t in ITEMS] == [True, False, False],
          "states = %s" % ([sgr.last_is_reversed(t) for t in ITEMS],))

    # ---- V3: Enter dispatches the right item and dismisses -------------------
    print("\nV3. Enter on item 2 dispatches Open and takes the menu away")
    key(fd, sc, sgr, DOWN)
    d = key(fd, sc, sgr, ENTER, 0.9)
    check("the item's own handler ran", b"btn mi_open" in sgr.buf,
          "the status echo 'btn mi_open' was written")
    # Staleness-immune dismissal check: with the menu gone focus must be back on
    # the command line, so a typed character must land in the field. Reading
    # pixels cannot show this - a closed popup leaves its cells stale.
    key(fd, sc, sgr, b"z", 0.6)
    check("the menu is gone and the keyboard is back in the field",
          "z" in sc.line(CMDLINE_ROW),
          "row %d: %r" % (CMDLINE_ROW, sc.line(CMDLINE_ROW).strip()))

    # ---- V7: reopen resets; Esc still closes --------------------------------
    print("\nV7. reopening resets the cursor to item 1; Esc still dismisses")
    key(fd, sc, sgr, F8, 0.8)
    check("a fresh menu highlights item 1 again",
          [sgr.last_is_reversed(t) for t in ITEMS] == [True, False, False],
          "states = %s" % ([sgr.last_is_reversed(t) for t in ITEMS],))
    key(fd, sc, sgr, DOWN)
    check("the arrows still work on the reopened menu",
          [sgr.last_is_reversed(t) for t in ITEMS] == [False, True, False],
          "states = %s" % ([sgr.last_is_reversed(t) for t in ITEMS],))
    key(fd, sc, sgr, b"\x1b", 0.8)
    key(fd, sc, sgr, b"q", 0.5)
    check("Esc dismissed it and left the engine responsive",
          "z" in sc.line(CMDLINE_ROW),
          "row %d: %r" % (CMDLINE_ROW, sc.line(CMDLINE_ROW).strip()))

    kill(fd, sc, sgr)
    print("\n---- %s ----" % ("all passed" if not fails else "%d FAILED" % fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
