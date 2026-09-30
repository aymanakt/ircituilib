#!/usr/bin/env python3
"""After F7 F7, force a full repaint (resize) and see if the panel was
really removed from the tree or only left un-erased on screen."""
import os, pty, select, sys, time, fcntl, termios, struct
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
    os.write(fd, b"\x1b[18~"); pump(fd, sc, 1.2)
    print("after F7 open :", "☐" in sc.dump())
    os.write(fd, b"\x1b[18~"); pump(fd, sc, 1.2)
    print("after F7 close:", "☐" in sc.dump())
    # force a full repaint the way a terminal resize does
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", 23, 80, 0, 0))
    pump(fd, sc, 0.3)
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 80, 0, 0))
    pump(fd, sc, 1.2)
    print("after resize  :", "☐" in sc.dump())
    os.write(fd, b"\x03"); pump(fd, sc, 0.2)
    try: os.close(fd)
    except OSError: pass
main()
