import os, re, sys, collections
sys.path.insert(0, "/tmp/irctui-caret"); sys.path.insert(0, "/tmp/irctui-menu/g3")
from g3_menu import spawn, key, read, DOWN, ENTER, F8, CMDLINE_ROW
TOK = re.compile(rb"\x1b\[([0-9;?]*)([@-~])")
def click(fd, sc, sgr, row, col, button=0):
    os.write(fd, b"\x1b[<%d;%d;%dM" % (button, col+1, row+1)); read(fd, sc, sgr, 0.25)
    os.write(fd, b"\x1b[<%d;%d;%dm" % (button, col+1, row+1)); read(fd, sc, sgr, 0.4)
pid, fd, sc, sgr = spawn(os.path.abspath(sys.argv[1]))
for ch in "hello": key(fd, sc, sgr, ch.encode(), 0.15)
key(fd, sc, sgr, F8, 0.8)
key(fd, sc, sgr, DOWN, 0.5); key(fd, sc, sgr, ENTER, 0.8)
key(fd, sc, sgr, b"\x1b", 0.5)
click(fd, sc, sgr, CMDLINE_ROW, 4, button=2)
pos = None
for r in range(24):
    x = sc.line(r).find("Clear")
    if x >= 0: pos = (r, x); break
click(fd, sc, sgr, pos[0], pos[1])
key(fd, sc, sgr, b"y", 0.4)
key(fd, sc, sgr, b"\x18", 0.4)
counts = collections.Counter(m.group(2).decode() for m in TOK.finditer(sgr.buf))
print("CSI finals used: %s" % dict(counts))
for f in "@ABCDEFGHJKLMNPSTXZ`abcdefghlmnpqrsu":
    if f in counts:
        print("   %s x%d  %s" % (f, counts[f], {
            "H":"cursor pos","G":"col","d":"row","A":"up","B":"down","C":"fwd","D":"back",
            "K":"erase line","J":"erase display","X":"ERASE CHARS","P":"DELETE CHARS",
            "@":"INSERT CHARS","L":"insert line","M":"delete line","S":"scroll up",
            "T":"scroll down","m":"SGR","h":"mode set","l":"mode reset","r":"scroll region",
        }.get(f, "OTHER")))
try: os.close(fd)
except OSError: pass
