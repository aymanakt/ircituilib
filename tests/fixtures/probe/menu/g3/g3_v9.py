#!/usr/bin/env python3
"""V9: what G-3 must NOT have changed.

The cursor block sits in the middle of the shared key path, so the risk is not
that it fails to work but that it swallows a key something else needed, or that
focus is left somewhere the next keystroke dies. These are the surrounding
behaviours, measured rather than reasoned about:

  - the tools panel still closes on Esc and typing resumes afterwards;
  - 'q' on an empty command line still quits, from a clean start;
  - F8 is still a toggle and does not double-toggle, given the demo handles
    KEY_F(8) itself (ui_main.c:98) and then still calls ui_dispatch_key with the
    same key - a new key block that consumed it could open and close in one press;
  - with a menu open the demo's own 'q' binding still fires, because ui_main
    checks it before dispatch and without consulting focus. Recorded because it
    is surprising, not because it is wrong: it is unchanged from before.

Usage: g3_v9.py <binary>
"""
import os
import pty
import select
import sys
import time

sys.path.insert(0, "/tmp/irctui-caret")
sys.path.insert(0, "/tmp/irctui-menu/g3")
from pty_screen import Screen
from g3_menu import spawn, read, key, F8, CMDLINE_ROW  # noqa

F7 = b"\x1b[18~"
fails = 0


def check(label, cond, detail):
    global fails
    print(("  PASS  %-58s %s" if cond else "  FAIL  %-58s %s") % (label, detail))
    if not cond:
        fails += 1


def panels(sc):
    return sum(sc.line(r).count("tools_panel") + sc.line(r).count("file_menu")
               for r in range(24))


def wait_exit(fd, secs):
    """Did the child close the pty (i.e. exit) within secs?"""
    end = time.time() + secs
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.1)
        if fd in r:
            try:
                c = os.read(fd, 65536)
            except OSError:
                return True
            if not c:
                return True
    return False


def main():
    binp = os.path.abspath(sys.argv[1])
    os.chdir(os.environ.get("PROBE_CWD", "/tmp/irctui-menu/cur"))
    os.environ.setdefault("ASAN_OPTIONS", "detect_leaks=0")

    print("V9a. the tools panel still closes on Esc, then typing resumes")
    pid, fd, sc, sgr = spawn(binp)
    key(fd, sc, sgr, F7, 0.9)
    opened = panels(sc)
    check("F7 opened the panel", opened >= 1, "labels on screen = %d" % opened)
    key(fd, sc, sgr, b"\x1b", 0.8)
    after = panels(sc)
    check("Esc closed it", after == 0, "labels on screen = %d" % after)
    key(fd, sc, sgr, b"y", 0.6)
    check("typing lands in the field again", "y" in sc.line(CMDLINE_ROW),
          "row %d: %r" % (CMDLINE_ROW, sc.line(CMDLINE_ROW).strip()))
    try:
        os.write(fd, b"\x03")
        os.close(fd)
    except OSError:
        pass

    print("\nV9b. 'q' on an empty command line still quits")
    pid, fd, sc, sgr = spawn(binp)
    os.write(fd, b"q")
    check("the demo exited", wait_exit(fd, 2.0), "pty closed by the child")
    try:
        os.close(fd)
    except OSError:
        pass

    print("\nV9c. a character in the field still suppresses 'q'")
    pid, fd, sc, sgr = spawn(binp)
    key(fd, sc, sgr, b"x", 0.5)
    os.write(fd, b"q")
    check("it did NOT exit (the guard still works)", not wait_exit(fd, 1.5),
          "still running")
    key(fd, sc, sgr, b"\x03", 0.3)
    try:
        os.close(fd)
    except OSError:
        pass

    print("\nV9d. F8 is still a toggle and does not double-toggle")
    pid, fd, sc, sgr = spawn(binp)
    key(fd, sc, sgr, F8, 0.9)
    first = panels(sc)
    check("one F8 opens exactly one menu", first == 1,
          "labels on screen = %d" % first)
    key(fd, sc, sgr, F8, 0.9)
    second = panels(sc)
    check("a second F8 closes it (one press, one toggle)", second == 0,
          "labels on screen = %d" % second)
    key(fd, sc, sgr, F8, 0.9)
    third = panels(sc)
    check("a third F8 reopens it, not a double toggle", third == 1,
          "labels on screen = %d" % third)

    print("\nV9e. 'q' with a menu open: the demo's own binding still fires")
    key(fd, sc, sgr, b"q", 0.5)
    quit_worked = wait_exit(fd, 1.5)
    check("the demo exited, as it did before this change", quit_worked,
          "ui_main checks 'q' before dispatch and without consulting focus")
    try:
        os.close(fd)
    except OSError:
        pass

    print("\n---- %s ----" % ("all passed" if not fails else "%d FAILED" % fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
