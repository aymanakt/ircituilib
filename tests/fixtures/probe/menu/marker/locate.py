import os, sys
sys.path.insert(0, "/tmp/irctui-caret"); sys.path.insert(0, "/tmp/irctui-menu/g3")
from g3_menu import spawn, read
binp = os.path.abspath(sys.argv[1])
os.chdir("/tmp/irctui-menu/marker")
pid, fd, sc, sgr = spawn(binp)
read(fd, sc, sgr, 1.2)
for r in range(24):
    if sc.line(r).strip():
        print("%2d %r" % (r, sc.line(r).rstrip()))
print("--- locator ---")
for needle in ("File", "\U0001F600"):
    for r in range(24):
        i = sc.line(r).find(needle)
        if i >= 0:
            print("  %r at row %d col %d" % (needle, r, i))
os.close(fd)
