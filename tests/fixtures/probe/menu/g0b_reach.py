#!/usr/bin/env python3
"""G-0b: did the Close click actually reach the branch that frees the node?

g0_uaf.py reported no ASan finding, but it never positively confirmed that its
Close click entered the branch under test. The Help control only proves the click
*encoding* works. This probe closes that hole.

The discriminator is the engine's own status echo. ui_engine.c:1347-1354 calls
hit->on_input() and *then* writes "click <id>" to the status node. So a
"click mi_close" in the byte stream means the click resolved to the mi_close
button, and mi_close's id contains "close", so ui_button_input:1864 took the
close branch. That branch returns normally, so control reaches the echo.

This is paired with a negative control: a click on the menu's border hits the
container instead. The container carries no `clickable` and no on_input, so it
falls through to ui_engine.c:1409 and dismisses nothing. If that control also
closed the menu, then "the menu went away" would prove nothing, and any
conclusion drawn from g0_uaf.py would be void.

Usage: g0b_reach.py <binary>
Env:   PROBE_CWD - see g0_uaf.py.
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


def status_line(sc):
    """The status row's text, or '' - the echo is a screen observable.

    Do NOT grep the raw byte stream for "click mi_close": ncurses diffs cell by
    cell, so when the status changes from "click mi_help" to "click mi_close" it
    emits only the differing tail and the literal string never appears. Screening
    the reconstructed screen is the honest check.
    """
    for r in range(24):
        l = sc.line(r)
        if "click " in l:
            return l.strip("│| ")
    return ""


def click(fd, sc, row, col, sink):
    os.write(fd, b"\x1b[<%d;%d;%dM" % (0, col + 1, row + 1))
    pump(fd, sc, 0.25, sink)
    os.write(fd, b"\x1b[<%d;%d;%dm" % (0, col + 1, row + 1))
    pump(fd, sc, 0.25, sink)


def main():
    binp = os.path.abspath(sys.argv[1])
    os.environ.setdefault("ASAN_OPTIONS", "detect_leaks=0")
    os.chdir(os.environ.get("PROBE_CWD", "/tmp/irctui-menu/cur"))

    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm-256color"
        os.environ["LINES"], os.environ["COLUMNS"] = "24", "80"
        os.execv(binp, [binp])
    sc = Screen(24, 80)
    pump(fd, sc, 1.6)

    print("A. OPEN THE MENU, AND LOOK AT WHERE THE BUTTONS ACTUALLY ARE")
    os.write(fd, F8)
    pump(fd, sc, 0.8)
    h = sc.find("Help")
    c = sc.find("Close")
    check("menu open, items locatable", h and c, "Help=%s Close=%s" % (h, c))
    if not (h and c):
        return 1
    for r in range(max(0, h[0] - 2), min(24, c[0] + 3)):
        print("     row %2d %r" % (r, sc.line(r)))

    print("\nB. NEGATIVE CONTROL - a click that misses the buttons must not dismiss")
    # The popup's own cells, not a button's. The boxes are drawn with Unicode
    # box-drawing glyphs, so scan for those rather than ASCII, and take the left
    # border on the Close row - it belongs to the container, never to a button.
    BOX = "│┌┐└┘├┤┬┴┼─"
    row = c[0]
    line = sc.line(row)
    lb = None
    for x in range(c[1] - 1, -1, -1):
        if x < len(line) and line[x] in BOX:
            lb = x
            break
    check("the menu's left border was located", lb is not None,
          "col %s on row %d: %r" % (lb, row, line.rstrip()))
    if lb is None:
        return 1
    before = bytearray()
    click(fd, sc, row, lb, before)
    check("clicking the container's border did NOT dismiss the menu",
          sc.find("Close") is not None,
          "Close on screen after border click: %s" % (sc.find("Close") is not None,))
    check("the border click resolved to no button (no echo on the status row)",
          "click" not in status_line(sc),
          "status row reads %r after %d bytes" % (status_line(sc), len(before)))

    print("\nC. THE PATH UNDER TEST - open, then click the Close BUTTON")
    os.write(fd, ESC)
    pump(fd, sc, 0.5)
    check("clean state", sc.find("Close") is None, "menu closed")
    os.write(fd, F8)
    pump(fd, sc, 0.8)
    c = sc.find("Close")
    if c is None:
        check("menu reopened for the test", False, "Close not found")
        return 1
    raw = bytearray()
    click(fd, sc, c[0], c[1], raw)
    text = bytes(raw)
    shown = status_line(sc)
    reached = "click mi_close" in shown
    gone = sc.find("Close") is None
    check("the click resolved to the mi_close button (echo on the status row)",
          reached, "status row reads %r (%d bytes)" % (shown, len(text)))
    check("the menu was dismissed by that click", gone,
          "Close on screen: %s" % (not gone,))
    print("     => interpretation: %s" % (
        "the close branch RAN (button hit, echo written after it returned)"
        if (reached and gone) else
        "NOT ESTABLISHED - the click did not demonstrably enter the close branch"))

    print("\nD. THE SAME CLICK, 20 ROUNDS, WITH THE BYTE STREAM KEPT")
    # Each round clicks Help first, then Close. The echo is read off the screen,
    # not grepped from the stream - see status_line(). The Help click also proves
    # the round is live before the Close click is measured.
    raw = bytearray()
    rounds_ok = 0
    dismissed_ok = 0
    echoed_ok = 0
    for i in range(20):
        os.write(fd, F8)
        if not pump(fd, sc, 0.30, raw):
            break
        h = sc.find("Help")
        if h is None:
            break
        os.write(fd, b"\x1b[<%d;%d;%dM" % (0, h[1] + 1, h[0] + 1))
        pump(fd, sc, 0.20, raw)
        os.write(fd, b"\x1b[<%d;%d;%dm" % (0, h[1] + 1, h[0] + 1))
        if not pump(fd, sc, 0.20, raw):
            break
        pos = sc.find("Close")
        if pos is None:
            break
        os.write(fd, b"\x1b[<%d;%d;%dM" % (0, pos[1] + 1, pos[0] + 1))
        if not pump(fd, sc, 0.25, raw):
            break
        os.write(fd, b"\x1b[<%d;%d;%dm" % (0, pos[1] + 1, pos[0] + 1))
        if not pump(fd, sc, 0.15, raw):
            break
        rounds_ok += 1
        if "click mi_close" in status_line(sc):
            echoed_ok += 1
        if sc.find("Close") is None:
            dismissed_ok += 1
    text = bytes(raw)
    print("     rounds driven: %d, bytes: %d" % (rounds_ok, len(text)))
    check("every round's Close click reached the button (echo on screen)",
          echoed_ok == rounds_ok, "%d echoes for %d rounds" % (echoed_ok, rounds_ok))
    check("every round's Close click dismissed the menu", dismissed_ok == rounds_ok,
          "%d dismissals for %d rounds" % (dismissed_ok, rounds_ok))
    hits = text.count(b"AddressSanitizer")
    check("and ASan reported nothing across those rounds", hits == 0,
          "%d AddressSanitizer mention(s)" % hits)
    if hits:
        for l in text.split(b"\n"):
            s = l.decode("utf-8", "replace").rstrip()
            if "AddressSanitizer" in s or "ui_engine.c" in s:
                print("     %s" % s)

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
