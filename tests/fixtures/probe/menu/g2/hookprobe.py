#!/usr/bin/env python3
"""What does the G-2 on_inflated hook focus, and does anything already navigate?

Arrow keys for TERM=xterm-256color are kcuu1=\x1bOA and kcud1=\x1bOB, NOT the
\x1b[A / \x1b[B of a plain xterm. Sending \x1b[B is read by ncurses as a bare ESC,
which closes the top popup - so an earlier version of this probe "proved" the
panel closed on Down, which was the probe's fault, not the engine's.

Usage: hookprobe.py <binary> <label>
"""
import os, pty, select, sys, time
sys.path.insert(0, "/tmp/irctui-caret")
from pty_screen import Screen

UP, DOWN = b"\x1bOA", b"\x1bOB"
F7, F8 = b"\x1b[18~", b"\x1b[19~"

def pump(fd, sc, secs):
    end = time.time() + secs
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.05)
        if fd not in r: continue
        try: c = os.read(fd, 65536)
        except OSError: return False
        if not c: return False
        sc.feed(c)
    return True

def stars(sc):
    return [(r, c) for r in range(24) for c, ch in enumerate(sc.line(r)) if ch == "*"]

def panel(sc):
    """The rows of the tools panel / file menu, as (row, text) with content."""
    out = []
    for r in range(24):
        l = sc.line(r).rstrip()
        if l.strip("│ "):
            out.append((r, l))
    return out

def spawn(binp):
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm-256color"
        os.environ["LINES"], os.environ["COLUMNS"] = "24", "80"
        os.execv(binp, [binp])
    sc = Screen(24, 80)
    pump(fd, sc, 1.6)
    return pid, fd, sc

def radio_row(sc):
    for r in range(24):
        l = sc.line(r)
        for m in ("●", "○"):
            if m in l:
                return l.strip()
    return None

def run(binp, label, key, openname, rows_of_interest):
    print("\n=== %s : %s ===" % (label, openname))
    pid, fd, sc = spawn(binp)
    os.write(fd, key); pump(fd, sc, 0.9)
    print("  on open        stars=%s" % (stars(sc),))
    for r in rows_of_interest:
        print("    row %2d %r" % (r, sc.line(r).rstrip()))
    before = radio_row(sc)
    for _ in range(2):
        os.write(fd, DOWN); pump(fd, sc, 0.5)
    print("  after 2x DOWN  stars=%s" % (stars(sc),))
    for r in rows_of_interest:
        print("    row %2d %r" % (r, sc.line(r).rstrip()))
    after = radio_row(sc)
    if before or after:
        print("  radio row: %r -> %r   MOVED=%s" % (before, after, before != after))
    os.write(fd, UP); pump(fd, sc, 0.5)
    print("  after 1x UP    radio row: %r" % (radio_row(sc),))
    try:
        os.write(fd, b"\x03"); pump(fd, sc, 0.3); os.close(fd)
    except OSError: pass

def main():
    binp = os.path.abspath(sys.argv[1]); label = sys.argv[2]
    os.chdir("/tmp/irctui-menu/cur")
    run(binp, label, F7, "F7 tools_panel", range(3, 13))
    run(binp, label, F8, "F8 file_menu", range(3, 8))
    return 0

if __name__ == "__main__":
    sys.exit(main())
