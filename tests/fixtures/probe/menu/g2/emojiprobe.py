#!/usr/bin/env python3
"""Where does focus land when the emoji picker opens (G-2 bound it too)?

emoji_picker's children are emoji_title (text_view, not focusable), emoji_grid
(UI_EMOJI_TABLE, focusable via the switch at ui_engine.c:96) and emoji_close
(button). So the hook should land on the grid - which is exactly what Part B
needs. Measured, not assumed.
"""
import os, pty, select, sys, time
sys.path.insert(0, "/tmp/irctui-caret")
from pty_screen import Screen

DOWN = b"\x1bOB"

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

def click(fd, sc, row, col):
    os.write(fd, b"\x1b[<0;%d;%dM" % (col + 1, row + 1)); pump(fd, sc, 0.25)
    os.write(fd, b"\x1b[<0;%d;%dm" % (col + 1, row + 1)); pump(fd, sc, 0.35)

def main():
    binp = os.path.abspath(sys.argv[1]); label = sys.argv[2]
    os.chdir("/tmp/irctui-menu/cur")
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm-256color"
        os.environ["LINES"], os.environ["COLUMNS"] = "24", "80"
        os.execv(binp, [binp])
    sc = Screen(24, 80)
    pump(fd, sc, 1.6)
    print("=== %s : emoji picker ===" % label)
    e = sc.find("\U0001f600")
    print("  the header emoji button is at %s" % (e,))
    if e is None:
        return 1
    click(fd, sc, e[0], e[1])
    print("  on open       stars=%s" % (stars(sc),))
    for r in range(3, 12):
        l = sc.line(r).rstrip()
        if l.strip("│ "): print("    row %2d %r" % (r, l))
    os.write(fd, DOWN); pump(fd, sc, 0.5)
    print("  after 1x DOWN stars=%s" % (stars(sc),))
    for r in range(3, 12):
        l = sc.line(r).rstrip()
        if l.strip("│ "): print("    row %2d %r" % (r, l))
    try:
        os.write(fd, b"\x03"); pump(fd, sc, 0.3); os.close(fd)
    except OSError: pass
    return 0

if __name__ == "__main__":
    sys.exit(main())
