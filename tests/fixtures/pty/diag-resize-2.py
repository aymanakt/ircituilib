#!/usr/bin/env python3
"""Is the blank screen after a resize a stale state, or is the app wedged?"""
import os, fcntl, struct, termios, sys, pty
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vt import Screen, drain

def setsize(fd, rows, cols):
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))

def run(binp, to_r, to_c, probe_key=None, label=""):
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm-256color"
        os.execv(binp, [binp])
    setsize(fd, 24, 80)
    sc = Screen(24, 80)
    sc.feed(drain(fd, 1.2))
    os.write(fd, b"\x1b[18~")                    # F7 tools panel
    sc.feed(drain(fd, 1.2))
    before = sc.find("Close")
    setsize(fd, to_r, to_c)
    raw = drain(fd, 1.5)
    sc2 = Screen(to_r, to_c)
    sc2.feed(raw)
    print("%-22s -> %dx%d: bytes=%4d  nonblank=%4d  'Close'=%s" %
          (label, to_c, to_r, len(raw), sc2.nonblank(), sc2.find("Close")))
    if sc2.nonblank() == 0:
        note = "BLANK"
    else:
        note = "painted"
    print("    screen after resize: %s" % note)
    if probe_key is not None:
        os.write(fd, probe_key)
        sc2.feed(drain(fd, 1.2))
        print("    after key %r: nonblank=%4d  'Close'=%s  'IRCIT'=%s"
              % (probe_key, sc2.nonblank(), sc2.find("Close"), sc2.find("IRCIT")))
    os.write(fd, b"\x1b[21~")
    drain(fd, 0.3)
    try:
        os.close(fd)
    except OSError:
        pass
    os.waitpid(pid, os.WNOHANG)

b = sys.argv[1]
run(b, 10, 30, b"\x1b[17~", "shrink 30x10")
run(b, 30, 100, b"\x1b[17~", "grow 100x30")
run(b, 20, 60, b"\x1b[17~", "shrink 60x20")
