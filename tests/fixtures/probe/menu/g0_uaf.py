#!/usr/bin/env python3
"""G-0: is the free-then-read in ui_button_input a crash, or only a latent UB?

ui_engine.c:1884 calls ui_close_top_popup(), which frees the popup the button
lives in. ui_engine.c:1885 then reads n->id - and `id` is the FIRST member of
UiNode, i.e. offset 0, the offset where the allocator keeps its free-list link.
So that read is of freed memory.

This does not assert what the value is; it measures what happens. Built with
-fsanitize=address, the read is either reported or it is not, and that decides
the severity before anything is changed.

Positive control first: click "Help", which frees nothing, and require the
engine's own "click mi_help" echo. Without that, a silent click and a crash are
indistinguishable. Only then click "Close", which is the path under test.

Usage: g0_uaf.py <binary> [rounds]
Env:   ASAN_OPTIONS is passed through (default: detect_leaks=0 - the demo leaks
       its heap root children at exit by design, and leak noise would bury the
       thing being measured).
       PROBE_CWD overrides the working directory the demo runs in. The demo
       loads sample.png by relative path, so the child is run from the tree that
       owns the images; otherwise the image loader's complaint would land in the
       same byte stream this probe is searching for ASan reports.
"""
import os
import pty
import select
import sys
import time

sys.path.insert(0, "/tmp/irctui-caret")
from pty_screen import Screen

F8 = b"\x1b[19~"
ESC = b"\x1b"

fails = 0


def check(label, cond, detail):
    global fails
    if cond:
        print("  PASS  %-58s %s" % (label, detail))
    else:
        fails += 1
        print("  FAIL  %-58s %s" % (label, detail))


def pump(fd, sc, secs, sink=None):
    """Drain for secs. Every byte goes to the screen and, if asked, to sink."""
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


def click(fd, sc, row, col, sink):
    """A full click: press then release, SGR encoding, 1-based coordinates."""
    os.write(fd, b"\x1b[<%d;%d;%dM" % (0, col + 1, row + 1))
    pump(fd, sc, 0.20, sink)
    os.write(fd, b"\x1b[<%d;%d;%dm" % (0, col + 1, row + 1))
    pump(fd, sc, 0.20, sink)


def alive(fd, sc):
    """Still responsive? Send a key that always causes output."""
    before = bytearray()
    if not send(fd, b"\x1b[19~"):       # F8: toggles the menu, always writes
        return False
    pump(fd, sc, 0.40, before)
    return len(before) > 0


def send(fd, data):
    """A write to a dead child's pty raises EIO; that is a result, not a crash."""
    try:
        os.write(fd, data)
        return True
    except OSError:
        return False


def main():
    binp = sys.argv[1]
    rounds = int(sys.argv[2]) if len(sys.argv) > 2 else 20
    os.environ.setdefault("ASAN_OPTIONS", "detect_leaks=0")
    os.chdir(os.environ.get("PROBE_CWD", "/tmp/irctui-menu/cur"))
    binp = os.path.abspath(binp)

    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm-256color"
        os.environ["LINES"], os.environ["COLUMNS"] = "24", "80"
        os.execv(binp, [binp])
    sc = Screen(24, 80)
    pump(fd, sc, 1.6)

    print("1. OPEN THE MENU AND LOCATE THE ITEMS")
    os.write(fd, F8)
    pump(fd, sc, 0.8)
    h = sc.find("Help")
    c = sc.find("Close")
    check("the file menu is open and its items are locatable",
          h is not None and c is not None,
          "Help=%s Close=%s" % (h, c))
    if h is None or c is None:
        print("\n  cannot proceed without the menu on screen")
        return 1

    print("\n2. POSITIVE CONTROL - click Help, which frees nothing")
    raw = bytearray()
    click(fd, sc, h[0], h[1], raw)
    got = b"click mi_help" in bytes(raw)
    check("the click reached the item (engine echoed 'click mi_help')", got,
          "%d bytes, echo %s" % (len(raw), "present" if got else "ABSENT"))
    if not got:
        print("\n  the click path itself is not working; the test below would "
              "prove nothing")
        return 1
    # This used to assert the opposite - "Help does not close the menu". That was
    # behaviour-locking: it pinned whatever the build happened to do. Clicking a
    # menu row now dismisses the menu by decision ("any activation dismisses"),
    # so the assertion follows the contract instead of the old output.
    check("Help closes the menu (any activation dismisses)",
          sc.find("Close") is None,
          "Close still on screen: %s" % (sc.find("Close") is not None,))

    print("\n3. ESC TO A CLEAN STATE")
    os.write(fd, ESC)
    pump(fd, sc, 0.6)
    check("menu closed", sc.find("Close") is None,
          "Close on screen: %s" % (sc.find("Close") is not None,))

    print("\n4. THE PATH UNDER TEST - click Close, %d rounds" % rounds)
    raw = bytearray()
    died_at = None
    for i in range(rounds):
        if not send(fd, F8):
            died_at = i
            break
        pump(fd, sc, 0.30, raw)
        pos = sc.find("Close")
        if pos is None:
            died_at = i
            break
        os.write(fd, b"\x1b[<%d;%d;%dM" % (0, pos[1] + 1, pos[0] + 1))
        ok = pump(fd, sc, 0.25, raw)
        os.write(fd, b"\x1b[<%d;%d;%dm" % (0, pos[1] + 1, pos[0] + 1))
        if not pump(fd, sc, 0.15, raw):
            died_at = i
            break

    text = bytes(raw)
    hits = text.count(b"AddressSanitizer")
    print("     captured %d bytes over the rounds" % len(text))
    if hits:
        line = [l for l in text.split(b"\n") if b"ERROR: AddressSanitizer" in l]
        first = line[0].decode("utf-8", "replace") if line else "(no headline)"
        print("     %s" % first)
        for l in text.split(b"\n"):
            s = l.decode("utf-8", "replace").strip()
            if "#" in s and "ui_engine.c" in s:
                print("     %s" % s)
                break
    check("ASan reported nothing on the Close path", hits == 0,
          "%d AddressSanitizer mention(s)" % hits)
    if died_at is not None:
        print("     the menu stopped reappearing after round %d of %d" %
              (died_at, rounds))

    print("\n5. LIVENESS AFTER THE ROUNDS")
    ok = alive(fd, sc)
    check("the process still responds", ok,
          "output after a further key: %s" % ok)

    print("\n---- %s ----" % ("all passed" if not fails else "%d FAILED" % fails))
    try:
        os.write(fd, b"\x03")
        pump(fd, sc, 0.3)
    except OSError:
        pass
    try:
        os.close(fd)
    except OSError:
        pass
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
