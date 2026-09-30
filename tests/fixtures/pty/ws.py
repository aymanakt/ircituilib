import os, pty, select, sys, time, fcntl, termios, struct
sys.path.insert(0, "/tmp/irctui-caret")
from pty_screen import Screen
b = sys.argv[1]
pid, fd = pty.fork()
if pid == 0:
    os.environ["TERM"]="xterm-256color"
    os.execv(b,[b])
# set the winsize on the master before the child refreshes
fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 80, 0, 0))
sc = Screen(24,80)
data = bytearray()
t0 = time.time()
while time.time() - t0 < 1.0:
    r, _, _ = select.select([fd], [], [], 0.05)
    if fd in r:
        try: c = os.read(fd, 65536)
        except OSError: break
        if not c: break
        data += c; sc.feed(c)
print("bytes:", len(data), "tail:", bytes(data[-12:]))
print("rows painted:", sum(1 for r in range(24) if sc.line(r)))
print("row21:", repr(sc.line(21)[:30]))
os.write(fd, b"\x03")
try: os.close(fd)
except OSError: pass
