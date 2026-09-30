#!/usr/bin/env python3
"""A minimal terminal emulator, enough to scrape ncurses' output.

The existing PTY check reads the raw byte stream, which is a concatenation of
incremental screen diffs - searching it for a contiguous string tests the diff
stream, not the screen. This keeps a real grid so an assertion is about what a
user would see.

Handles what xterm-256color ncurses actually emits: CUP, relative cursor moves,
erase-in-line/display, erase-character (ECH), CR/LF/BS, SGR and mode-set
(ignored), charset selection.

ECH is not cosmetic and must not be lumped in with "the rest": ncurses uses it
instead of writing spaces whenever a run of cells is cleared, so a model that
ignores it leaves the old text on the grid and reports a perfectly correct
repaint as a stale screen. Measured: three builds that differed only in where
focus went emitted ECH on one path and explicit spaces on the other, which
looked exactly like a repaint regression until this was implemented.
"""
import unicodedata


def _width(ch):
    if unicodedata.combining(ch):
        return 0
    return 2 if unicodedata.east_asian_width(ch) in ("W", "F") else 1


class Screen:
    def __init__(self, rows=24, cols=80):
        self.rows, self.cols = rows, cols
        self.grid = [[" "] * cols for _ in range(rows)]
        self.r = self.c = 0

    # ---- primitive operations ----
    def _put(self, ch):
        w = _width(ch)
        if w == 0:
            return
        if self.c >= self.cols:
            return
        self.grid[self.r][self.c] = ch
        if w == 2 and self.c + 1 < self.cols:
            self.grid[self.r][self.c + 1] = ""
        self.c += w

    def _erase_line(self, mode):
        if mode == 0:
            rng = range(self.c, self.cols)
        elif mode == 1:
            rng = range(0, min(self.c + 1, self.cols))
        else:
            rng = range(self.cols)
        for i in rng:
            self.grid[self.r][i] = " "

    def _erase_display(self, mode):
        if mode == 2:
            self.grid = [[" "] * self.cols for _ in range(self.rows)]
        elif mode == 0:
            self._erase_line(0)
            for r in range(self.r + 1, self.rows):
                self.grid[r] = [" "] * self.cols
        elif mode == 1:
            self._erase_line(1)
            for r in range(0, self.r):
                self.grid[r] = [" "] * self.cols

    # ---- the feed ----
    def feed(self, data):
        s = data.decode("utf-8", "replace")
        i, n = 0, len(s)
        while i < n:
            ch = s[i]
            if ch == "\x1b":
                i = self._escape(s, i)
                continue
            if ch == "\r":
                self.c = 0
            elif ch == "\n":
                self.r = min(self.r + 1, self.rows - 1)
            elif ch == "\b":
                self.c = max(0, self.c - 1)
            elif ch == "\t":
                self.c = min(self.cols - 1, (self.c // 8 + 1) * 8)
            elif ch == "\x0f" or ch == "\x0e":
                pass
            elif ch >= " ":
                self._put(ch)
            i += 1

    def _escape(self, s, i):
        n = len(s)
        i += 1
        if i >= n:
            return i
        c = s[i]
        if c == "[":
            j = i + 1
            while j < n and not ("@" <= s[j] <= "~"):
                j += 1
            if j >= n:
                return n
            self._csi(s[i + 1:j], s[j])
            return j + 1
        if c in "()":
            return min(i + 2, n)
        if c == "]":                        # OSC ... BEL/ST
            j = i + 1
            while j < n and s[j] not in ("\x07",):
                if s[j] == "\x1b" and j + 1 < n and s[j + 1] == "\\":
                    return j + 2
                j += 1
            return min(j + 1, n)
        return i + 1

    def _csi(self, params, final):
        priv = params.startswith("?")
        if priv:
            params = params[1:]
        try:
            nums = [int(p) for p in params.split(";") if p != ""]
        except ValueError:
            nums = []
        a = nums[0] if nums else 0
        b = nums[1] if len(nums) > 1 else 0
        if final in "Hf":
            self.r = max(0, min(self.rows - 1, (a or 1) - 1))
            self.c = max(0, min(self.cols - 1, (b or 1) - 1))
        elif final == "A":
            self.r = max(0, self.r - (a or 1))
        elif final == "B":
            self.r = min(self.rows - 1, self.r + (a or 1))
        elif final == "C":
            self.c = min(self.cols - 1, self.c + (a or 1))
        elif final == "D":
            self.c = max(0, self.c - (a or 1))
        elif final == "G":
            self.c = max(0, min(self.cols - 1, (a or 1) - 1))
        elif final == "d":
            self.r = max(0, min(self.rows - 1, (a or 1) - 1))
        elif final == "K":
            self._erase_line(a)
        elif final == "J":
            self._erase_display(a)
        elif final == "X":
            # Erase Character: blank n cells from the cursor, WITHOUT moving the
            # cursor. No parameter means 1. The cursor must be left alone -
            # advancing it silently shifts every following write by n columns.
            for k in range(a or 1):
                col = self.c + k
                if col >= self.cols:
                    break
                self.grid[self.r][col] = " "
        # SGR (m), modes (h/l), scroll regions (r) and the rest are ignored:
        # they affect appearance, not content.

    # ---- reading ----
    def line(self, r):
        return "".join(self.grid[r]).rstrip()

    def dump(self):
        return "\n".join(self.line(r) for r in range(self.rows)).rstrip("\n")

    def find(self, needle):
        """(row, col) of the first occurrence, or None."""
        for r in range(self.rows):
            c = self.grid[r]
            s = "".join(c)
            p = s.find(needle)
            if p >= 0:
                return (r, p)
        return None


# The exact bytes the engine emitted when a context-menu action cleared the
# command line, captured from a live run. Fed to the grid model so its ECH
# handling is pinned by measurement rather than by the same reasoning that
# produced the fix - a "fix" tuned to make the suite pass would not survive
# being handed the real byte stream.
#
# ESC[22;3H   cursor to row 22 col 3 (1-based) = row 21, col 2
# > <caret>   writes the prompt and the caret glyph
# ESC[72X     ECH: blank 72 cells from col 5, cursor does not move
# ESC[78G*    cursor to col 77 (1-based) = col 76, then the focus marker
ECH_DELTA = (b"\x1b[22;3H\x1b(B\x1b[0;1m\x1b[37m\x1b[40m> \xe2\x94\x83"
             b"\x1b[72X\x1b[78G*\x1b(B\x1b[m\x1b[39;49m\x1b[37m\x1b[40m")


def _selftest():
    """ECH must blank cells and must not move the cursor."""
    ok = True

    def want(label, got, exp):
        nonlocal ok
        good = got == exp
        ok = ok and good
        print("  %-4s %-52s got=%r exp=%r"
              % ("PASS" if good else "FAIL", label, got, exp))

    s = Screen(24, 80)
    s.feed(b"\x1b[22;3H> hello\xe2\x94\x83")   # the row as it looked before
    before = s.line(21)
    want("the starting row really holds 'ello'", "> hello" in before, True)

    s.feed(ECH_DELTA)
    want("the erased run is gone", "".join(s.grid[21][5:10]), " " * 5)
    want("the prompt and caret survive", "".join(s.grid[21][2:5]), "> ┃")
    # ESC[78G is 1-based, so it lands on 0-based col 77 - and 72 cells erased
    # from col 5 stop short of it at col 76, so the marker is not erased.
    want("the marker sits at col 77", s.grid[21][77], "*")
    want("nothing was erased past the marker", "".join(s.grid[21][78:80]), "  ")

    # The cursor must NOT move: after 'ab' the cursor is on col 2, so an ECH of
    # 2 cells blanks cols 2-3 and the 'c' must land back on col 2. If ECH
    # advanced the cursor this would read 'ab  ' with the 'c' pushed to col 4.
    u = Screen(24, 80)
    u.feed(b"\x1b[1;1Hab\x1b[2Xc")
    want("ECH leaves the cursor where it was", "".join(u.grid[0][:4]), "abc ")

    # Negative control: without the ECH the text is still there, so the
    # assertion above is actually testing the ECH and not the setup.
    t = Screen(24, 80)
    t.feed(b"\x1b[22;3H> hello\xe2\x94\x83")
    t.feed(b"\x1b[22;3H> \xe2\x94\x83")
    want("control: without ECH the stale text remains",
         "".join(t.grid[21][5:10]), "ello┃")

    print("  ---- ECH self-test: %s ----" % ("all passed" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    import sys
    sys.exit(_selftest())
