"""Full picker region, column-indexed, so box geometry is read not inferred.
Also runs the SAME build twice as a probe-noise control."""
import os, sys
sys.path.insert(0, "/tmp/irctui-caret"); sys.path.insert(0, "/tmp/irctui-menu/g3")
from g3_menu import spawn, read


def snap(binp):
    os.chdir("/tmp/irctui-menu/marker")
    pid, fd, sc, sgr = spawn(binp)
    read(fd, sc, sgr, 1.2)
    os.write(fd, b"\x1b[<0;58;3M"); read(fd, sc, sgr, 0.3)
    os.write(fd, b"\x1b[<0;58;3m"); read(fd, sc, sgr, 0.7)
    out = []
    for r in range(8, 14):
        s = "".join(c if c else "·" for c in sc.grid[r])
        out.append((r, s))
    os.close(fd)
    return out


for label, path in [("withmarker", sys.argv[1]), ("withmarker AGAIN", sys.argv[1]),
                    ("nomarker", sys.argv[2])]:
    print("=== %s" % label)
    for r, s in snap(os.path.abspath(path)):
        print("  %2d |%s|" % (r, s[27:52]))
