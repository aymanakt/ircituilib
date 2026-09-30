import os, pty, select, sys, time
sys.path.insert(0, "/tmp/irctui-caret")
from pty_screen import Screen
def pump(fd, sc, s):
    e = time.time() + s
    while time.time() < e:
        r,_,_ = select.select([fd],[],[],0.05)
        if fd in r:
            try: c = os.read(fd, 65536)
            except OSError: return
            if not c: return
            sc.feed(c)
b = sys.argv[1]
os.chdir("/tmp/irctui-menu/cur")
pid, fd = pty.fork()
if pid == 0:
    os.environ["TERM"]="xterm-256color"; os.environ["LINES"]="24"; os.environ["COLUMNS"]="80"
    os.execv(b,[b])
sc = Screen(24,80); pump(fd,sc,1.6)
def dump(lbl):
    print("---- %s ----" % lbl)
    for r in range(24):
        l = sc.line(r).rstrip()
        if l.strip("│| "): print("  %2d %r" % (r,l))
os.write(fd, b"\x1b[18~"); pump(fd,sc,0.9); dump("F7 tools_panel")
os.write(fd, b"\x1b"); pump(fd,sc,0.4)
os.write(fd, b"\x1b[15~"); pump(fd,sc,0.9); dump("F5 code_win")
os.write(fd, b"\x1b"); pump(fd,sc,0.4)
e = sc.find("😀")
print("emoji header button at", e)
if e:
    os.write(fd, b"\x1b[<0;%d;%dM" % (e[1]+1, e[0]+1)); pump(fd,sc,0.25)
    os.write(fd, b"\x1b[<0;%d;%dm" % (e[1]+1, e[0]+1)); pump(fd,sc,0.6)
    dump("emoji_picker")
os.write(fd, b"\x03"); pump(fd,sc,0.3)
