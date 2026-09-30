#!/usr/bin/env python3
"""Extended differential driver: broader interaction surface.
Usage: diffdrv2.py <binary> <sequence> <cwd>"""
import os, pty, sys, time, select, signal
sys.path.insert(0, "/tmp/irctui-caret")
from pty_screen import Screen  # noqa
ROWS, COLS = 24, 80
ESC = b"\x1b"
SEQ = {
    "esc_only":      [ESC],
    "f10_quit":      [b"\x1b[21~"],
    "f5_f6":         [b"\x1b[15~", b"\x1b[17~"],
    "emoji_nav":     [b"\x1b[20~", b"\x1b[C", b"\x1b[C", b"\r"],
    "menu_nav_esc":  [b"\x1b[19~", b"\x1b[B", ESC],
    "menu_wrap_up":  [b"\x1b[19~", b"\x1b[A", b"\x1b[A", b"\x1b[A"],
    "tools_arrows":  [b"\x1b[18~", b"\x1b[B", b"\x1b[B"],
    "type_bs":       [b"a", b"b", b"\x7f", b"c"],
    "type_esc":      [b"x", ESC],
    "clip_copy":     [b"h", b"i", b"\x03", b"\x16"],
    "enter_cmd":     [b"z", b"\r"],
    "ctrl_l":        [b"\x0c"],
    "double_menu":   [b"\x1b[19~", ESC, b"\x1b[19~"],
    "all_panels":    [b"\x1b[18~", ESC, b"\x1b[19~", ESC, b"\x1b[20~"],
    "f5_then_esc":   [b"\x1b[15~", ESC],
    "f6_twice":      [b"\x1b[17~", b"\x1b[17~"],
}
def drain(fd, sc, secs):
    end = time.time() + secs
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.05)
        if fd in r:
            try: d = os.read(fd, 65536)
            except OSError: return
            if not d: return
            sc.feed(d)
def main():
    binp, name, cwd = os.path.abspath(sys.argv[1]), sys.argv[2], sys.argv[3]
    os.chdir(cwd)
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm-256color"
        os.environ["LINES"] = str(ROWS); os.environ["COLUMNS"] = str(COLS)
        os.execv(binp, [binp])
    sc = Screen(rows=ROWS, cols=COLS)
    drain(fd, sc, 1.0)
    for k in SEQ[name]:
        os.write(fd, k); drain(fd, sc, 0.5)
    for r in range(ROWS):
        print("%2d|%s" % (r, sc.line(r).rstrip()))
    try: os.close(fd)
    except OSError: pass
    try: os.kill(pid, signal.SIGKILL)
    except OSError: pass
main()
