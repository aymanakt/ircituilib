#!/usr/bin/env python3
"""Differential driver: send an identical key sequence to a binary under a PTY
and print the RECONSTRUCTED screen (not a byte-stream substring match).
Usage: diffdrv.py <binary> <sequence-name>"""
import os, pty, sys, time, select, signal
sys.path.insert(0, "/tmp/irctui-caret")
from pty_screen import Screen  # noqa

ROWS, COLS = 24, 80

SEQ = {
    "idle":        [],
    "f7_tools":    [b"\x1b[18~"],
    "f8_menu":     [b"\x1b[19~"],
    "f9_emoji":    [b"\x1b[20~"],
    "f5_float":    [b"\x1b[15~"],
    "f6_fill":     [b"\x1b[17~"],
    "typed":       [b"h", b"e", b"l", b"l", b"o"],
    "f7_esc":      [b"\x1b[18~", b"\x1b"],
    "f8_move_ent": [b"\x1b[19~", b"\x1b[B", b"\x1b[B", b"\r"],
    "f8_then_f9":  [b"\x1b[19~", b"\x1b[20~"],
    "ftab":        [b"\t"],
    "f7_tab":      [b"\x1b[18~", b"\t"],
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
    binp, name = os.path.abspath(sys.argv[1]), sys.argv[2]
    os.chdir("/home/devops/repos/ircit/packages/ircituilib")   # assets live here
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm-256color"
        os.environ["LINES"] = str(ROWS); os.environ["COLUMNS"] = str(COLS)
        os.execv(binp, [binp])
    sc = Screen(rows=ROWS, cols=COLS)
    drain(fd, sc, 1.0)
    for k in SEQ[name]:
        os.write(fd, k)
        drain(fd, sc, 0.6)
    for r in range(ROWS):
        line = sc.line(r).rstrip()
        print("%2d|%s" % (r, line))
    try: os.close(fd)
    except OSError: pass
    try: os.kill(pid, signal.SIGKILL)
    except OSError: pass

main()
