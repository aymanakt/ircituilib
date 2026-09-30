"""Cell-level differential of the reconstructed screen between two builds.

Reports ONLY cells that differ, so "the marker went away and nothing else moved"
is a measured claim rather than an eyeballed one.
"""
import os, sys
sys.path.insert(0, "/tmp/irctui-caret"); sys.path.insert(0, "/tmp/irctui-menu/g3")
from g3_menu import spawn, read

ROWS = 24


def click(fd, sc, sgr, row, col, button=0):
    os.write(fd, b"\x1b[<%d;%d;%dM" % (button, col + 1, row + 1)); read(fd, sc, sgr, 0.3)
    os.write(fd, b"\x1b[<%d;%d;%dm" % (button, col + 1, row + 1)); read(fd, sc, sgr, 0.5)


SCEN = [
    ("home (cmdline focused)",        []),
    ("F8 file_menu",                  [("k", b"\x1b[19~")]),
    ("F7 tools_panel",                [("k", b"\x1b[18~")]),
    ("F7 then Esc",                   [("k", b"\x1b[18~"), ("k", b"\x1b")]),
    ("F8, Down, Down",                [("k", b"\x1b[19~"), ("k", b"\x1bOB"), ("k", b"\x1bOB")]),
    ("F5 code window",                [("k", b"\x1b[15~")]),
    ("click [ 😀 ] -> picker",        [("c", 2, 57)]),
    ("F8 then right-click File",      [("k", b"\x1b[19~"), ("c", 2, 49, 2)]),
    ("type 'hi' into the field",      [("k", b"hi")]),
]


def capture(binp, actions):
    pid, fd, sc, sgr = spawn(binp)
    read(fd, sc, sgr, 1.2)
    for a in actions:
        if a[0] == "k":
            os.write(fd, a[1]); read(fd, sc, sgr, 0.6)
        else:
            click(fd, sc, sgr, a[1], a[2], *(a[3:]))
    rows = [sc.line(r).rstrip() for r in range(ROWS)]
    try:
        os.close(fd)
    except OSError:
        pass
    return rows


a_bin, b_bin = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
os.chdir("/tmp/irctui-menu/marker")
for name, actions in SCEN:
    before = capture(a_bin, actions)
    after = capture(b_bin, actions)
    diffs = [(r, before[r], after[r]) for r in range(ROWS) if before[r] != after[r]]
    print("=== %s" % name)
    if not diffs:
        print("    identical")
    for r, x, y in diffs:
        print("    row %2d" % r)
        print("       with marker: %r" % x)
        print("       without    : %r" % y)
