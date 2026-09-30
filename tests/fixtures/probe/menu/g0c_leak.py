#!/usr/bin/env python3
"""G-0c: measure, rather than infer, what happens to mi_close on dismissal.

Reading ui_pop_stacked says only free(n->children) and free(n) happen - the child
UiNode structs are never freed. This drives the real close path under LeakSanitizer
and looks for the allocation site in the report, which turns "I read that no free
happens" into "these allocations were still live at exit".

The demo also leaks its heap root children by design, so the report is not
expected to be empty; what matters is whether ui_clone's calloc for the menu's
children shows up in it.

Usage: g0c_leak.py <asan-binary>
"""
import os
import pty
import select
import sys
import time

sys.path.insert(0, "/tmp/irctui-caret")
from pty_screen import Screen

F8 = b"\x1b[19~"

def pump(fd, sc, secs, sink=None):
    end = time.time() + secs
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.05)
        if fd not in r:
            continue
        try:
            c = os.read(fd, 65536)
        except OSError:
            return False
        if not c:
            return False
        sc.feed(c)
        if sink is not None:
            sink += c
    return True

def main():
    binp = os.path.abspath(sys.argv[1])
    os.chdir(os.environ.get("PROBE_CWD", "/tmp/irctui-menu/cur"))
    os.environ["ASAN_OPTIONS"] = "detect_leaks=1"

    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm-256color"
        os.environ["LINES"], os.environ["COLUMNS"] = "24", "80"
        os.execv(binp, [binp])
    sc = Screen(24, 80)
    out = bytearray()
    pump(fd, sc, 1.6, out)

    # open, click Close, twice - the branch under test
    for _ in range(2):
        os.write(fd, F8)
        pump(fd, sc, 0.4, out)
        c = sc.find("Close")
        if c is None:
            print("!! menu not found; aborting")
            return 1
        os.write(fd, b"\x1b[<0;%d;%dM" % (c[1] + 1, c[0] + 1))
        pump(fd, sc, 0.25, out)
        os.write(fd, b"\x1b[<0;%d;%dm" % (c[1] + 1, c[0] + 1))
        pump(fd, sc, 0.25, out)
        print("   clicked Close (menu gone: %s)" % (sc.find("Close") is None,))

    # quit so LSan runs its atexit report
    os.write(fd, b"q")
    end = time.time() + 12
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.2)
        if fd not in r:
            try:
                os.waitpid(pid, os.WNOHANG)
            except ChildProcessError:
                break
            continue
        try:
            c = os.read(fd, 65536)
        except OSError:
            break
        if not c:
            break
        out += c
    try:
        os.waitpid(pid, 0)
    except ChildProcessError:
        pass

    text = bytes(out).decode("utf-8", "replace")
    print("\n=== LeakSanitizer present in output: %s ===" % ("yes" if "LeakSanitizer" in text or "leaked" in text else "no"))
    lines = text.split("\n")
    leak_lines = [l for l in lines if "leaked in" in l or "Direct leak" in l or "Indirect leak" in l]
    for l in leak_lines[:20]:
        print("   %s" % l.rstrip())
    print("\n=== allocations attributed to ui_clone / menu children ===")
    hits = 0
    for i, l in enumerate(lines):
        if "ui_clone" in l:
            hits += 1
            for j in range(max(0, i - 4), min(len(lines), i + 2)):
                print("   %s" % lines[j].rstrip())
            print("   ---")
            if hits >= 6:
                break
    if not hits:
        print("   (no ui_clone frame in the report - the children were NOT reported live)")
    return 0

if __name__ == "__main__":
    sys.exit(main())
