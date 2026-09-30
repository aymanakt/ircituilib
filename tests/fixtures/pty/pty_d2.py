#!/usr/bin/env python3
"""Show WS-D2 as a user-visible effect.

F5 builds the code_win floating window; F6 runs ui_demo_fill(), which calls
ui_layout(root, ...) + ui_realize(root) - the same pair KEY_RESIZE uses.

Pre-D2 the pack passes re-placed the popup at the root's content-box origin
(1,1), on top of the header. Post-D2 it stays where it was put. To tell the
difference we need the FINAL screen, not an accumulated escape stream, so this
uses the VT100 model in vt.py.

Usage: pty_d2.py <binary>
"""
import os, pty, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vt import Screen, drain as read_more  # noqa: E402


def main():
    binp = sys.argv[1]
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm-256color"
        os.execv(binp, [binp])

    sc = Screen(24, 80)
    sc.feed(read_more(fd, 1.2))
    checks = []

    def ok(name, cond, detail=""):
        checks.append((name, cond))
        print(("PASS" if cond else "FAIL"), name, detail)

    os.write(fd, b"\x1b[15~")  # F5: build code_win
    sc.feed(read_more(fd, 1.2))
    pos5 = sc.find("from C API")
    ok("F5 popup is on screen", pos5 is not None, str(pos5))

    os.write(fd, b"\x1b[17~")  # F6: ui_demo_fill() -> ui_layout(root) + ui_realize(root)
    sc.feed(read_more(fd, 1.5))

    pos6 = sc.find("from C API")
    hdr = sc.find("IRCIT")
    ok("popup survives the relayout", pos6 is not None, str(pos6))
    ok("popup did not move", pos5 is not None and pos5 == pos6,
       "was %s, now %s" % (pos5, pos6))
    ok("header is not covered by the popup", hdr is not None, str(hdr))

    os.write(fd, b"\x1b[21~")  # F10
    read_more(fd, 0.4)
    try:
        os.close(fd)
    except OSError:
        pass
    failed = [n for n, c in checks if not c]
    print("summary", "%d/%d" % (len(checks) - len(failed), len(checks)))
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
