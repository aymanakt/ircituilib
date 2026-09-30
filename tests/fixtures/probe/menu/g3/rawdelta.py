#!/usr/bin/env python3
"""What do the bytes after F8 actually look like, for the highlight assertion?

pty_screen.Screen keeps characters only, so a reverse-video row cannot be read
off it. Before writing an assertion that looks for A_REVERSE, look at the real
bytes: guessing the SGR spelling is how a probe ends up asserting its own
assumption.
"""
import os, pty, select, sys, time
def pump(fd, secs):
    out = b""
    end = time.time() + secs
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.05)
        if fd in r:
            try: c = os.read(fd, 65536)
            except OSError: break
            if not c: break
            out += c
    return out

binp = os.path.abspath(sys.argv[1])
os.chdir("/tmp/irctui-menu/cur")
pid, fd = pty.fork()
if pid == 0:
    os.environ["TERM"] = "xterm-256color"
    os.environ["LINES"], os.environ["COLUMNS"] = "24", "80"
    os.execv(binp, [binp])
pump(fd, 1.6)
os.write(fd, b"\x1b[19~")          # F8
d = pump(fd, 1.0)
print("delta bytes: %d" % len(d))
print("repr:")
print(repr(d))
print()
print("contains \\x1b[7m :", b"\x1b[7m" in d)
print("occurrences of 'Help':", d.count(b"Help"))
i = d.find(b"Help")
if i >= 0:
    print("64 bytes before 'Help':", repr(d[max(0, i-64):i]))
os.write(fd, b"\x03"); pump(fd, 0.3)
try: os.close(fd)
except OSError: pass
