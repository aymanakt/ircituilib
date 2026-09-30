#!/usr/bin/env python3
"""Does a shrink blank the UI only when a popup is open, or always?"""
import os, fcntl, struct, termios, sys, pty
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vt import Screen, drain

def setsize(fd, rows, cols):
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))

def run(binp, to_r, to_c, popup):
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm-256color"
        os.execv(binp, [binp])
    setsize(fd, 24, 80)
    sc = Screen(24, 80)
    sc.feed(drain(fd, 1.2))
    if popup:
        os.write(fd, b"\x1b[18~")               # F7 tools panel
        sc.feed(drain(fd, 1.2))
    setsize(fd, to_r, to_c)
    raw = drain(fd, 1.5)
    sc2 = Screen(to_r, to_c)
    sc2.feed(raw)
    clears = raw.count(b"\x1b[H\x1b[J") + raw.count(b"\x1b[H\x1b[2J")
    print("popup=%-5s %2dx%-3d -> %2dx%-3d : bytes=%4d clears=%d nonblank=%4d 'IRCIT'=%s"
          % (popup, 24, 80, to_r, to_c, len(raw), clears, sc2.nonblank(), sc2.find("IRCIT")))
    os.write(fd, b"\x1b[21~")
    drain(fd, 0.3)
    try:
        os.close(fd)
    except OSError:
        pass
    os.waitpid(pid, os.WNOHANG)

b = sys.argv[1]
for popup in (False, True):
    for (r, c) in ((10, 30), (20, 60), (30, 100)):
        run(b, r, c, popup)
