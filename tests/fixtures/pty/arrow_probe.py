#!/usr/bin/env python3
import os, pty, select, sys, time
sys.path.insert(0, "/tmp/irctui-caret")
from pty_screen import Screen

def pump(fd, sc, secs=0.6):
    end = time.time() + secs
    while time.time() < end:
        r,_,_ = select.select([fd],[],[],0.1)
        if fd in r:
            try: c = os.read(fd, 8192)
            except OSError: break
            if not c: break
            sc.feed(c)

binp = sys.argv[1]
scenario = sys.argv[2]
pid, fd = pty.fork()
if pid == 0:
    os.environ["TERM"] = "xterm-256color"
    os.environ["LINES"], os.environ["COLUMNS"] = "24", "80"
    os.execv(binp, [binp])
sc = Screen(24, 80)
pump(fd, sc, 1.0)
os.write(fd, b"abcdef"); pump(fd, sc, 0.6)
print("before:", repr(sc.line(21)))
if scenario == "arrow":
    os.write(fd, b"\x1bOD"); pump(fd, sc, 0.6)
    os.write(fd, b"\x1bOD"); pump(fd, sc, 0.6)
    os.write(fd, b"\x1bOD"); pump(fd, sc, 0.6)
elif scenario == "batched":
    os.write(fd, b"\x1bOD\x1bOD\x1bOD"); pump(fd, sc, 1.0)
elif scenario == "f7":
    os.write(fd, b"\x1b[18~"); pump(fd, sc, 1.0)
print("after :", repr(sc.line(21)))
if scenario == "f7":
    print("popup?:", "Close" in sc.dump())
os.write(fd, b"\x03"); pump(fd, sc, 0.3)
try: os.close(fd)
except OSError: pass
