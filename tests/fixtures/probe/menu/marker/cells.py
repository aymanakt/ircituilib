"""Column-exact dump of one screen row, so wide chars cannot fool the reading."""
import os, sys
sys.path.insert(0, "/tmp/irctui-caret"); sys.path.insert(0, "/tmp/irctui-menu/g3")
from g3_menu import spawn, read
binp = os.path.abspath(sys.argv[1]); rows = [int(x) for x in sys.argv[2].split(",")]; lo, hi = int(sys.argv[3]), int(sys.argv[4])
os.chdir("/tmp/irctui-menu/marker")
pid, fd, sc, sgr = spawn(binp)
read(fd, sc, sgr, 1.2)
os.write(fd, b"\x1b[<0;58;3M"); read(fd, sc, sgr, 0.3)      # press [ 😀 ]
os.write(fd, b"\x1b[<0;58;3m"); read(fd, sc, sgr, 0.6)      # release
for row in rows:
    print("%s  row %d, columns %d..%d" % (os.path.basename(binp), row, lo, hi))
    for c in range(lo, hi):
        cell = sc.grid[row][c]
        print("   col %2d  %r%s" % (c, cell, "   <-- second half of a wide char" if cell == "" else ""))
os.close(fd)
