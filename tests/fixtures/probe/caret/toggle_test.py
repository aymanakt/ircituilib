#!/usr/bin/env python3
"""Does F7 (the demo's own toggle) close the panel, while ESC does not?
That separates a broken ui_pop_stacked from a key that never arrives as 27."""
import os, pty, select, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import Screen
def pump(fd, sc, s):
    end = time.time() + s
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.02)
        if fd in r:
            try: c = os.read(fd, 65536)
            except OSError: break
            if not c: break
            sc.feed(c)
def main():
    b = sys.argv[1]
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"]="xterm-256color"; os.environ["LINES"],os.environ["COLUMNS"]="24","80"
        os.execv(b,[b])
    sc = Screen(24,80)
    pump(fd, sc, 1.2)
    for label, seq in (("F7 open", b"\x1b[18~"), ("F7 again", b"\x1b[18~"),
                       ("F7 open 2", b"\x1b[18~"), ("ESC", b"\x1b")):
        os.write(fd, seq)
        pump(fd, sc, 1.5)
        print("%-10s panel present: %s" % (label, "☐" in sc.dump()))
    os.write(fd, b"\x03"); pump(fd, sc, 0.2)
    try: os.close(fd)
    except OSError: pass
main()
