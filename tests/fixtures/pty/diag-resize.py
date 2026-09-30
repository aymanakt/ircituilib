#!/usr/bin/env python3
"""Measure what ncurses actually emits when the PTY shrinks."""
import os, fcntl, struct, termios, sys, time, pty, select

def setsize(fd, rows, cols):
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))

def drain(fd, secs):
    out = b""
    end = time.time() + secs
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.1)
        if fd in r:
            try:
                c = os.read(fd, 65536)
            except OSError:
                break
            if not c:
                break
            out += c
    return out

binp = sys.argv[1]
pid, fd = pty.fork()
if pid == 0:
    os.environ["TERM"] = "xterm-256color"
    os.execv(binp, [binp])
setsize(fd, 24, 80)
drain(fd, 1.2)
os.write(fd, b"\x1b[18~")   # F7 tools_panel
r1 = drain(fd, 1.2)
print("pre-resize bytes: %d, smkx=%d, clear=%d"
      % (len(r1), r1.count(b"\x1b[?1h\x1b="), r1.count(b"\x1b[H\x1b[J")))

setsize(fd, 10, 30)
raw = drain(fd, 3.0)
print("post-resize bytes: %d" % len(raw))
print("  smkx  \\x1b[?1h\\x1b=  count:", raw.count(b"\x1b[?1h\x1b="))
print("  ED    \\x1b[H\\x1b[J    count:", raw.count(b"\x1b[H\x1b[J"))
print("  ED2   \\x1b[H\\x1b[2J  count:", raw.count(b"\x1b[H\x1b[2J"))
print("  VPA   \\x1b[<n>d      count:", len([1 for _ in __import__('re').finditer(rb"\x1b\[\d+d", raw)]))
i = raw.rfind(b"\x1b[H\x1b[J")
print("  last clear at offset %d of %d (%d bytes after it)" % (i, len(raw), len(raw) - i))
print("  tail after last clear:", repr(raw[i:i+80]))
alive = os.waitpid(pid, os.WNOHANG)
print("  child:", alive)
