#!/usr/bin/env python3
"""G-1 regression: the OTHER close-named buttons still close exactly one layer.

G-1 rewrote where the pop happens in ui_button_input's close branch. That branch
is shared by every button whose id contains "close" or whose text is "Close":
tools_close, emoji_close, code_close and the file menu's mi_close. Only the ctx
items change behaviour; for these four the pop count is 1 before and 1 after, by
construction. This measures that rather than resting on the trace.

Each case runs in a FRESH process so that only one layer is on screen and the
string "Close" is unambiguous - closing a popup leaves its interior cells stale
(a pre-existing repaint defect), so a second layer's Close would otherwise be
indistinguishable from residue.

"Closed" is not asserted by looking for pixels - closing a popup leaves its
interior cells stale, a pre-existing repaint defect. Instead the status row is
first seeded with a DIFFERENT echo (the file menu's Open item), and then the
layer's Close cell is clicked again: if the layer is really gone that click
resolves to no button and the row keeps reading "click mi_open". Clicking the same
cell twice without seeding proves nothing, because an absent echo and an unchanged
echo look identical.

Usage: g0h_othercloses.py <binary>
"""
import os
import pty
import select
import sys
import time

sys.path.insert(0, "/tmp/irctui-caret")
from pty_screen import Screen

CMDLINE_ROW = 21
F8 = b"\x1b[19~"

fails = 0


def check(label, cond, detail):
    global fails
    if cond:
        print("  PASS  %-56s %s" % (label, detail))
    else:
        fails += 1
        print("  FAIL  %-56s %s" % (label, detail))


def pump(fd, sc, secs):
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
    return True


def click(fd, sc, row, col):
    os.write(fd, b"\x1b[<0;%d;%dM" % (col + 1, row + 1))
    pump(fd, sc, 0.25)
    os.write(fd, b"\x1b[<0;%d;%dm" % (col + 1, row + 1))
    pump(fd, sc, 0.35)


def status_line(sc):
    """The echo the engine last wrote, with the frame and stale border glyphs cut.

    The raw line is '││click tools_close       ' followed by the panel's stale
    bottom border. Strip the frame's own leading glyphs FIRST - otherwise the cut
    below truncates at index 0 - then stop at the next border glyph.
    """
    for r in range(24):
        l = sc.line(r)
        if "click " in l:
            s = l.lstrip("│")
            for i, ch in enumerate(s):
                if ch in "│┌┐└┘─":
                    s = s[:i]
                    break
            return s.strip()
    return ""


def spawn(binp):
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm-256color"
        os.environ["LINES"], os.environ["COLUMNS"] = "24", "80"
        os.execv(binp, [binp])
    sc = Screen(24, 80)
    pump(fd, sc, 1.6)
    return pid, fd, sc


def kill(fd, sc):
    try:
        os.write(fd, b"\x03")
        pump(fd, sc, 0.3)
    except OSError:
        pass
    try:
        os.close(fd)
    except OSError:
        pass


def case(binp, name, opener, want_id, want_echo):
    print("\n=== %s ===" % name)
    pid, fd, sc = spawn(binp)

    occurrences = sum(sc.line(r).count("Close") for r in range(24))
    opener(fd, sc)
    hits = [(r, sc.line(r).find("Close")) for r in range(24) if "Close" in sc.line(r)]
    check("exactly one Close is on screen (so the target is unambiguous)",
          len(hits) == 1, "found at %s" % (hits,))
    if len(hits) != 1:
        kill(fd, sc)
        return
    row, col = hits[0]
    print("     close button at (row %d, col %d), on screen %r" % (row, col, sc.line(row).strip()))

    click(fd, sc, row, col)
    shown = status_line(sc)
    check("the click reached %s" % want_id, shown == want_echo,
          "status row reads %r" % (shown,))

    # Positive discriminator for "the layer is gone". Clicking the same cell
    # twice cannot show it: when the layer is gone nothing writes the status row,
    # so it keeps the old echo and looks exactly like a successful re-click. So
    # first seed the row with a DIFFERENT echo, from an item that certainly
    # exists - the file menu's Open. Then "still reads mi_open" means the click
    # resolved to nothing, i.e. the layer really is gone.
    os.write(fd, F8)
    pump(fd, sc, 0.7)
    mo = None
    for r in range(24):
        x = sc.line(r).find("Open")
        if x >= 0 and 44 <= x < 60:          # the file menu's own band
            mo = (r, x)
            break
    if mo is None:
        check("the file menu opened to seed the status row", False,
              "Open not found; rows: %s" % [sc.line(r).strip() for r in range(3, 8)])
        kill(fd, sc)
        return
    click(fd, sc, mo[0], mo[1])
    seeded = status_line(sc)
    check("the status row was seeded with a different echo", seeded == "click mi_open",
          "status row reads %r" % (seeded,))

    click(fd, sc, row, col)
    after = status_line(sc)
    check("the same cell now resolves to nothing (the layer is gone)",
          after == "click mi_open",
          "status row reads %r (unchanged means the click hit no button)" % (after,))

    # Liveness. Typing directly here proves nothing: the file menu is still open
    # and focus is on one of its buttons, and ui_button_input ignores any key that
    # is not an activation (ui_engine.c:1844), so the character is swallowed -
    # pre-existing behaviour, not a regression. Esc first, which also exercises the
    # post-pop refocus (ui_focus_top) that this branch is responsible for.
    os.write(fd, b"\x1b")
    pump(fd, sc, 0.6)
    os.write(fd, b"z")
    pump(fd, sc, 0.5)
    check("after Esc the engine is alive and types into the field",
          "z" in sc.line(CMDLINE_ROW),
          "row %d: %r" % (CMDLINE_ROW, sc.line(CMDLINE_ROW).strip()))

    kill(fd, sc)


def open_f7(fd, sc):
    os.write(fd, b"\x1b[18~")
    pump(fd, sc, 0.9)


def open_f5(fd, sc):
    os.write(fd, b"\x1b[15~")
    pump(fd, sc, 0.9)


def open_emoji(fd, sc):
    e = sc.find("😀")
    click(fd, sc, e[0], e[1])


def main():
    binp = os.path.abspath(sys.argv[1])
    os.chdir(os.environ.get("PROBE_CWD", "/tmp/irctui-menu/cur"))
    os.environ.setdefault("ASAN_OPTIONS", "detect_leaks=0")

    case(binp, "F7 tools_panel -> tools_close", open_f7,
         "tools_close", "click tools_close")
    case(binp, "F5 code_win -> code_close", open_f5,
         "code_close", "click code_close")
    case(binp, "header emoji button -> emoji_close", open_emoji,
         "emoji_close", "click emoji_close")

    print("\n---- %s ----" % ("all passed" if not fails else "%d FAILED" % fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
