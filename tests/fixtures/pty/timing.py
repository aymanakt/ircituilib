import os, pty, select, sys, time
sys.path.insert(0, "/tmp/irctui-caret")
from pty_screen import Screen
b = sys.argv[1]
pid, fd = pty.fork()
if pid == 0:
    os.environ["TERM"]="xterm-256color"; os.environ["LINES"],os.environ["COLUMNS"]="24","80"
    os.execv(b,[b])
sc = Screen(24,80)
t0 = time.time()
total = 0
while time.time() - t0 < 3.0:
    r, _, _ = select.select([fd], [], [], 0.05)
    if fd in r:
        try: c = os.read(fd, 65536)
        except OSError: break
        if not c: break
        total += len(c)
        sc.feed(c)
        print("%6.0f ms  chunk=%5d  total=%6d  row21=%r" %
              ((time.time()-t0)*1000, len(c), total, sc.line(21)[:20]))
print("final row21:", repr(sc.line(21)[:30]))
print("final rows painted:", sum(1 for r in range(24) if sc.line(r)))
os.write(fd, b"\x03")
try: os.close(fd)
except OSError: pass
