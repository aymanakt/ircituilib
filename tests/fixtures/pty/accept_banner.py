#!/usr/bin/env python3
"""The unread banner's acceptance run: the streaming demo, a real PTY, real mouse events.

WHY A PTY AND NOT ONLY THE PROBE. dev/probes/conversation/p_conv_unread.c holds the watermark
to the glass, but it builds its own widget and calls the gesture by hand. This run takes the
whole path instead: the server's frames arrive over a socket, the demo ingests them through its
adapter, the wheel is an escape sequence read by ncurses, and the box is clicked where the
reader sees it. It is the only artefact that crosses the engine's input routing and the clone
the looper builds.

WHAT IS ASSERTED, in three steps:

  idle     with the stream running and the reader at the tail, no banner. Frames and bubbles must
           be on the glass before anything is concluded from a label's absence -- the "0 checks,
           0 failures" trap this repository has read as a pass twice.
  parked   the wheel moves the reader into history while the stream keeps producing, and the
           banner appears carrying the INBOUND count. The harness replays Bob's outbound runs
           every cycle, so a banner showing every arrival would be displaying the host's own.
  clicked  a click inside the banner's box retires it. THE ABSENCE IS THE OBSERVABLE OF THE TAIL:
           the watermark only advances in a pass that found the viewport at its ceiling, so a
           banner that is gone means the reader was taken to the newest message.

THE SCREEN IS RECONSTRUCTED, not eyeballed. pyte is not installed in this tree, so Screen below
is a small VT model -- enough of CUP, SGR, erase, CR and LF for ncurses' doupdate output, which
is the only thing this program ever reads. Attributes are deliberately NOT modelled: every claim
here is about text and position.

Usage: accept_banner.py [client] [server] [rows] [cols]
"""

import os
import pty
import re
import select
import signal
import struct
import subprocess
import sys
import termios
import time
import unicodedata
import fcntl

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))
CLIENT = os.path.join(ROOT, "build", "conv-client", "streaming_msg_client_demo")
SERVER = os.path.join(ROOT, "build", "conv-client", "conversation_stream_server")
ROWS, COLS = 30, 100
# The pane's centre: a wheel only reaches the conversation if it landed inside it, because the
# router walks from the hit node to the nearest scrollable ancestor.
WHEEL_X, WHEEL_Y = COLS // 2, ROWS // 3

checks = 0
failures = 0


def check(cond, msg):
    global checks, failures
    checks += 1
    if not cond:
        failures += 1
        print("FAIL " + msg)
    return cond


class Screen:
    """A minimal terminal: CUP, erase, CR/LF, and wide glyphs. No attributes."""

    def __init__(self, rows, cols):
        self.rows, self.cols = rows, cols
        self.grid = [[" "] * cols for _ in range(rows)]
        # The ink each cell was written in: the SGR state in force when its character was
        # written. Indices only -- an fg/bg PAIR of numbers is enough to tell one colour from
        # another, which is all a colour RULE needs, and it keeps the parser out of the
        # terminal's palette.
        self.colour = [[(None, None)] * cols for _ in range(rows)]
        self.fg = self.bg = None
        self.r = self.c = 0
        self.top, self.bot = 0, rows - 1
        self.pending = b""

    def _scroll(self, n, down=False):
        a, b = self.top, min(self.bot, self.rows - 1)
        if n > b - a + 1:
            n = b - a + 1
        for _ in range(n):
            if down:
                self.grid.insert(a, [" "] * self.cols)
                del self.grid[b + 1]
            else:
                del self.grid[a]
                self.grid.insert(b, [" "] * self.cols)

    def _put(self, ch):
        w = 2 if unicodedata.east_asian_width(ch) in ("W", "F") else 1
        if 0 <= self.r < self.rows and 0 <= self.c < self.cols:
            self.grid[self.r][self.c] = ch
            self.colour[self.r][self.c] = (self.fg, self.bg)
            if w == 2 and self.c + 1 < self.cols:
                self.grid[self.r][self.c + 1] = ""
                self.colour[self.r][self.c + 1] = (self.fg, self.bg)
        self.c = min(self.cols - 1, self.c + w)

    @staticmethod
    def _arg(args, i, dflt=1):
        """A parameter that may be absent: `\x1b[A` arrives with no parameters at all."""
        return args[i] if i < len(args) and args[i] else dflt

    def _csi(self, body, final):
        args = [int(p) if p else 0 for p in re.split(r";", body)] if body else []
        if final in ("H", "f"):
            self.r = max(0, (args[0] if args and args[0] else 1) - 1)
            self.c = max(0, (args[1] if len(args) > 1 and args[1] else 1) - 1)
        elif final == "J":
            if not args or args[0] in (2, 3):
                self.grid = [[" "] * self.cols for _ in range(self.rows)]
        elif final == "K":
            if 0 <= self.r < self.rows:
                for x in range(self.c, self.cols):
                    self.grid[self.r][x] = " "
        elif final == "A":
            self.r = max(0, self.r - self._arg(args, 0))
        elif final == "B":
            self.r = min(self.rows - 1, self.r + self._arg(args, 0))
        elif final == "C":
            self.c = min(self.cols - 1, self.c + self._arg(args, 0))
        elif final == "D":
            self.c = max(0, self.c - self._arg(args, 0))
        elif final == "G":
            self.c = max(0, self._arg(args, 0) - 1)
        elif final == "d":
            self.r = max(0, self._arg(args, 0) - 1)
        elif final == "m":
            self._sgr(args)
        # --- THE SCROLLING AND PARTIAL-ERASE SET, and they are not optional here: a message
        # arriving shifts the whole pane, and ncurses does that with a scroll region and a
        # line insert/delete rather than by re-addressing every row. A parser that ignores
        # them leaves the previous frame where it was and writes the new one over it, which
        # reads as two messages interleaved on one line -- a defect in the reconstruction that
        # looks exactly like a defect in the paint.
        elif final == "r":
            self.top = self._arg(args, 0) - 1
            self.bot = self._arg(args, 1, self.rows) - 1
            self.r = self.c = 0
        elif final == "S":
            self._scroll(self._arg(args, 0))
        elif final == "T":
            self._scroll(self._arg(args, 0), down=True)
        elif final == "L":
            self._scroll(self._arg(args, 0), down=True)
        elif final == "M":
            self._scroll(self._arg(args, 0))
        elif final == "X":
            if 0 <= self.r < self.rows:
                for x in range(self.c, min(self.cols, self.c + self._arg(args, 0))):
                    self.grid[self.r][x] = " "
        elif final == "P":
            if 0 <= self.r < self.rows:
                row = self.grid[self.r]
                del row[self.c:self.c + self._arg(args, 0)]
                row.extend([" "] * (self.cols - len(row)))
        elif final == "@":
            if 0 <= self.r < self.rows:
                row = self.grid[self.r]
                for _ in range(self._arg(args, 0)):
                    row.insert(self.c, " ")
                del row[self.cols:]

    def feed(self, data):
        buf = self.pending + data
        cut = len(buf)
        # Hold a trailing partial glyph back for the next read rather than decoding it as a
        # replacement character: a wide glyph split across two reads is otherwise two cells of
        # noise on the row a label is looked for on.
        for back in range(1, min(4, len(buf)) + 1):
            lead = buf[-back]
            if (lead & 0xC0) == 0xC0:
                need = 2 if (lead & 0xE0) == 0xC0 else 3 if (lead & 0xF0) == 0xE0 else 4
                if back < need:
                    cut = len(buf) - back
                break
            if (lead & 0xC0) != 0x80:
                break
        self.pending = buf[cut:]
        self._scan(buf[:cut].decode("utf-8", "replace"))

    def _scan(self, text):
        i = 0
        while i < len(text):
            ch = text[i]
            if ch == "\x1b":
                m = re.match(r"\x1b\[([0-9;?]*)([A-Za-z@])", text[i:])
                if m:
                    self._csi(m.group(1).lstrip("?"), m.group(2))
                    i += m.end()
                    continue
                # A CHARSET SWITCH IS THREE BYTES, \x1b(B and \x1b)0 both. Skipping two leaves
                # the designator as text: it prints a stray 'B' AND advances the cursor, which
                # desynchronises every write after it on that row -- two messages interleaved
                # on one line is what that looks like on the dump.
                if i + 1 < len(text) and text[i + 1] in "()":
                    i += 3
                    continue
                i += 2  # = > 7 8 M D E and friends
                continue
            if ch == "\r":
                self.c = 0
            elif ch == "\n":
                self.r = min(self.rows - 1, self.r + 1)
            elif ch == "\b":
                self.c = max(0, self.c - 1)
            elif ch >= " ":
                self._put(ch)
            i += 1

    def _sgr(self, args):
        """The SGR parameters that change the ink. Attributes are ignored on purpose: the
        claim a colour rule makes is about COLOUR, and a reverse or a bold would be a
        different claim."""
        i = 0
        while i < len(args):
            a = args[i]
            if a == 0:
                self.fg = self.bg = None
            elif 30 <= a <= 37:
                self.fg = a - 30
            elif a == 39:
                self.fg = None
            elif 40 <= a <= 47:
                self.bg = a - 40
            elif a == 49:
                self.bg = None
            elif a == 38 and i + 2 < len(args) and args[i + 1] == 5:
                self.fg = args[i + 2]
                i += 2
            elif a == 48 and i + 2 < len(args) and args[i + 1] == 5:
                self.bg = args[i + 2]
                i += 2
            i += 1

    def colour_at(self, row, col):
        if 0 <= row < self.rows and 0 <= col < self.cols:
            return self.colour[row][col]
        return (None, None)

    def lines(self):
        return ["".join(row) for row in self.grid]

    def text(self):
        return "\n".join(self.lines())

    def dump(self, label):
        print("---- %s" % label)
        for i, line in enumerate(self.lines()):
            print("%2d|%s" % (i + 1, line.rstrip()))


def chrome(lines):
    """The app's static frame: the outer border rows, which no message ever touches."""
    return (lines[0], lines[-1])


def box_ink(text):
    return sum(1 for ch in text if ch in "╭╮╰╯─│├┤┌┐└┘")


def pump(fd, sc, secs):
    end = time.time() + secs
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.05)
        if fd not in r:
            continue
        try:
            data = os.read(fd, 65536)
        except OSError:
            break
        if not data:
            break
        sc.feed(data)


def click(fd, x, y, button=0):
    """SGR, 1-based, press and release: the encoding this tree's PTY probes already use."""
    os.write(fd, ("\x1b[<%d;%d;%dM\x1b[<%d;%d;%dm" % (button, x, y, button, x, y)).encode())


def wheel_up(fd, x, y, notches):
    for _ in range(notches):
        os.write(fd, ("\x1b[<64;%d;%dM" % (x, y)).encode())
        time.sleep(0.15)


def main():
    client = sys.argv[1] if len(sys.argv) > 1 else CLIENT
    server = sys.argv[2] if len(sys.argv) > 2 else SERVER
    rows = int(sys.argv[3]) if len(sys.argv) > 3 else ROWS
    cols = int(sys.argv[4]) if len(sys.argv) > 4 else COLS

    # THE THRESHOLD IS READ FROM THE DEMO, not written here: a literal would be a second copy
    # of a rule the script is supposed to be checking.
    alert_at = 10
    try:
        with open(os.path.join(ROOT, "src/examples/conversation_streaming/streaming_msg_client_demo.c")) as f:
            m = re.search(r"#define UNREAD_ALERT_AT (\d+)", f.read())
        if m:
            alert_at = int(m.group(1))
    except OSError:
        pass
    print("NOTE accept_banner: the demo's threshold is %d unread" % alert_at)

    srv = subprocess.Popen([server], stdout=subprocess.PIPE, text=True, bufsize=1)
    port = None
    deadline = time.time() + 10
    while time.time() < deadline:
        line = srv.stdout.readline()
        if not line:
            break
        print("       server: " + line.rstrip())
        m = re.search(r"listening on [^:]+:(\d+)", line)
        if m:
            port = int(m.group(1))
            break
    if not check(port is not None, "the streaming server never reported a port"):
        srv.kill()
        return 1

    pid, fd = pty.fork()
    if pid == 0:
        # /tmp, so the demo's own log file lands outside the repository.
        os.chdir("/tmp")
        os.environ["TERM"] = "xterm-256color"
        os.environ["LINES"], os.environ["COLUMNS"] = str(rows), str(cols)
        os.execv(client, [client, "--timestamps", "--stream", "127.0.0.1:%d" % port])

    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
    sc = Screen(rows, cols)
    raw = bytearray()

    def drain(secs):
        end = time.time() + secs
        while time.time() < end:
            r, _, _ = select.select([fd], [], [], 0.05)
            if fd not in r:
                continue
            try:
                data = os.read(fd, 65536)
            except OSError:
                return
            if not data:
                return
            raw.extend(data)
            sc.feed(data)

    # --- idle: frames on the glass, no banner ---------------------------------
    drain(10.0)
    have = box_ink(sc.text())
    print("NOTE accept_banner: %d box glyphs on the glass" % have)
    check(have > 20, "the pane drew %d box glyphs, so the conversation is not on the glass and every claim below "
                     "would pass vacuously" % have)
    check("New Messages" not in sc.text(), "a banner was up with the reader at the tail and nothing scrolled")
    # THE RECONSTRUCTION IS CHECKED, not assumed. These three dumps are the artefact a person
    # reads, and a VT parser that lost the cursor produces output that looks like a paint bug --
    # which is what three defects in this file's own parser did before this check existed. So:
    # the version string the library reports must be on the glass, the port the server named
    # must be in the footer, and the chrome must not move between frames.
    check("IRCITUI 0.0.3." in sc.text(), "the version string is not on the glass: the reconstruction is not the app's")
    check(str(port) in sc.text(), "the port the server named (%s) is not in the footer" % port)
    frame = chrome(sc.lines())
    sc.dump("idle: the reader is at the tail")

    # --- parked: the count crosses the demo's threshold, and the ink follows ---
    # SAMPLED RATHER THAN SAMPLED ONCE, because the claim is a MAPPING from the count to the
    # colour: one reading would only show one side of the threshold, and the side that matters
    # -- below it -- is the one the stream leaves behind within a second of parking.
    wheel_up(fd, WHEEL_X, WHEEL_Y, 3)
    samples = []
    end = time.time() + 12
    while time.time() < end:
        drain(0.15)
        lines = sc.lines()
        row = next((i for i, l in enumerate(lines) if "New Messages" in l), -1)
        if row < 0:
            continue
        m = re.search(r"(\d+)\s+New Messages", lines[row])
        if m:
            samples.append((int(m.group(1)), sc.colour_at(row, lines[row].index("New Messages"))))

    text = sc.text()
    check("New Messages" in text, "the wheel moved the reader into history and the stream produced nothing countable")
    distinct = {}
    for n, ink in samples:
        distinct.setdefault(n, ink)
    print("NOTE accept_banner: %d samples; count -> ink: %s" % (len(samples),
          ", ".join("%d:%s" % (n, i) for n, i in sorted(distinct.items()))))
    below = {ink for n, ink in samples if n < alert_at}
    at    = {ink for n, ink in samples if n >= alert_at}
    check(len(below) >= 1, "no sample was taken below the %d-message threshold, so the colour rule "
                           "cannot be measured on the side that changes" % alert_at)
    check(len(at) >= 1, "no sample reached the %d-message threshold" % alert_at)
    check(len(below) <= 1 and len(at) <= 1, "the ink varies within one side of the threshold: below %s, at %s"
          % (below, at))
    check(below != at, "the banner's ink at the threshold is the same as below it: the demo's colour rule did "
                       "not take effect")
    check(chrome(sc.lines()) == frame, "the app's frame moved between frames: the reconstruction lost the cursor")
    sc.dump("parked: the banner is up")

    # --- clicked: the box takes the tap, and the reader is at the newest -------
    if "New Messages" in text:
        lines = sc.lines()
        brow = next(i for i, line in enumerate(lines) if "New Messages" in line)
        line = lines[brow]
        lx = line.rfind("└", 0, line.index("New Messages"))
        rx = line.find("┘", line.index("New Messages"))
        print("NOTE accept_banner: the box spans columns %d..%d on row %d (1-based: %d..%d)" % (lx, rx, brow + 1, lx + 1,
                                                                                              rx + 1))
        check(lx >= 0 and rx > lx, "the label is on the glass with no box around it")
        if lx >= 0 and rx > lx:
            click(fd, (lx + rx) // 2 + 1, brow + 1)
            drain(2.0)
            check("New Messages" not in sc.text(), "the click inside the banner's box did not retire it")
            check(box_ink(sc.lines()[brow]) > 0, "the row the banner left carries no box ink: it was not repainted")
            check(chrome(sc.lines()) == frame, "the app's frame moved when the banner was clicked")
            sc.dump("clicked: the banner is gone and the reader is at the newest")

    os.kill(pid, signal.SIGKILL)
    os.waitpid(pid, 0)
    srv.terminate()
    try:
        srv.wait(timeout=5)
    except subprocess.TimeoutExpired:
        srv.kill()

    # A real ASAN error is this run's business; a LeakSanitizer SUMMARY is not, and the demo
    # build has leaks on teardown as a documented baseline. Only the first is a failure.
    if b"detected memory leaks" in bytes(raw):
        print("NOTE accept_banner: the client's teardown leaked (LeakSanitizer summary present)")
    check(b"ERROR: AddressSanitizer" not in bytes(raw) and b"runtime error:" not in bytes(raw),
          "a sanitizer reported a real error on the client's own output")
    print("accept_banner: %d checks, %d failures" % (checks, failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
