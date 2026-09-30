import os, pty, select, sys, time
sys.path.insert(0, "/tmp/irctui-caret")
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
b = sys.argv[1]
pid, fd = pty.fork()
if pid == 0:
    os.environ["TERM"]="xterm-256color"; os.environ["LINES"],os.environ["COLUMNS"]="24","80"
    os.execv(b,[b])
sc = Screen(24,80)
pump(fd, sc, 2.0)
print(sc.dump())
os.write(fd, b"\x03"); pump(fd, sc, 0.2)
try: os.close(fd)
except OSError: pass
