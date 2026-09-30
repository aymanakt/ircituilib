import os, pty, select, sys, time, re
b = sys.argv[1]
pid, fd = pty.fork()
if pid == 0:
    os.environ["TERM"]="xterm-256color"; os.environ["LINES"],os.environ["COLUMNS"]="24","80"
    os.execv(b,[b])
data = bytearray()
t0 = time.time()
while time.time() - t0 < 1.5:
    r, _, _ = select.select([fd], [], [], 0.05)
    if fd in r:
        try: c = os.read(fd, 65536)
        except OSError: break
        if not c: break
        data += c
os.write(fd, b"\x03")
try: os.close(fd)
except OSError: pass
s = data.decode("utf-8", "replace")
# list the escape sequences in order, with what plain text sits between them
parts = re.split(r"(\x1b(?:\[[0-9;?]*[a-zA-Z]|\][^\x07]*\x07|[()][A-Z0-9]|[=>78]))", s)
n = 0
for i, p in enumerate(parts):
    if not p: continue
    if p.startswith("\x1b"):
        if n < 40:
            print("ESC %-14r" % p)
        n += 1
    else:
        t = p.replace("\n", "\\n").replace("\r", "\\r")
        if t.strip() and n < 40:
            print("    text %r" % t[:60])
print("... total escapes: %d, total bytes: %d" % (n, len(data)))
print("tail 120 bytes: %r" % data[-120:])
