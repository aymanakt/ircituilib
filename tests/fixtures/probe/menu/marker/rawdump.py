"""Ground truth: list the absolute-positioned writes ncurses actually made."""
import os, re, sys
sys.path.insert(0, "/tmp/irctui-caret"); sys.path.insert(0, "/tmp/irctui-menu/g3")
from g3_menu import spawn

binp = os.path.abspath(sys.argv[1])
os.chdir("/tmp/irctui-menu/marker")
pid, fd, sc, sgr = spawn(binp)
import time
time.sleep(1.2)
os.write(fd, b"\x1b[<0;58;3M"); time.sleep(0.3)
os.write(fd, b"\x1b[<0;58;3m"); time.sleep(0.7)
buf = b""
while True:
    import select
    r, _, _ = select.select([fd], [], [], 0.2)
    if not r:
        break
    try:
        c = os.read(fd, 65536)
    except OSError:
        break
    if not c:
        break
    buf += c
os.close(fd)
print("%s: %d bytes in the picker frame" % (os.path.basename(binp), len(buf)))
s = buf.decode("utf-8", "replace")
i, row, col = 0, 0, 0
out, pend = [], []
while i < len(s):
    if s[i] == "\x1b":
        m = re.match(r"\x1b\[(\d+);(\d+)H", s[i:])
        if m:
            if pend:
                out.append("   r%d c%d: %r" % (row, col, "".join(pend))); pend = []
            row, col = int(m.group(1)) - 1, int(m.group(2)) - 1
            i += m.end(); continue
        m = re.match(r"\x1b\[[0-9;?]*[A-Za-z]", s[i:]) or re.match(r"\x1b.", s[i:])
        i += m.end() if m else 1
        continue
    ch = s[i]
    if ch in "\r\n":
        if pend:
            out.append("   r%d c%d: %r" % (row, col, "".join(pend))); pend = []
        if ch == "\r": col = 0
        else: row, col = row + 1, 0
        i += 1; continue
    pend.append(ch)
    col += 1
    i += 1
if pend:
    out.append("   r%d c%d: %r" % (row, col, "".join(pend)))
for line in out:
    if re.search(r"r(8|9|10|11|12|13) ", line):
        print(line)
