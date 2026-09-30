#!/usr/bin/env python3
"""G-4: the right-click context menu joins the keyboard contract, and a mouse
activation dismisses.

A11 gives the C-built context menu UI_F_CHILDREN_SELECTABLE and focuses it on
open. It is hand-built and never goes through inflate_at, so there is no template
to hang on_inflated on - the focus call is explicit, and this checks it.

A10 makes a click on a menu row dismiss the menu. Its second half - the cursor
moving onto the clicked row and focus going to the CONTAINER rather than the row
- has no independent observable here, because "any activation dismisses" pops the
container in the same event: ui_pop_stacked clears the focus and ui_focus_top
re-resolves it, so the end state is the same with or without that half. It is
implemented as specified (it keeps the invariant true for a menu that does not
dismiss itself) but this probe cannot demonstrate it separately, and does not
claim to.

THE FOCUS PROBE. A focused menu swallows plain characters: its children are
buttons whose on_input ignores anything but Enter, and the container has no
on_input of its own, so the key falls through and dies. The command line is
therefore a precise focus indicator - 'z' lands in the field when focus is on
cmdline and vanishes when focus is on a menu. Both polarities are measured in
case 3 rather than assumed, which is what gives the assertions their power.

Usage: g4_probe.py <binary>
"""
import os
import sys

sys.path.insert(0, "/tmp/irctui-caret")
sys.path.insert(0, "/tmp/irctui-menu/g3")
from g3_menu import spawn, key, read, DOWN, ENTER, F8, CMDLINE_ROW, ITEMS  # noqa

fails = 0
CTX = [b"Copy", b"Select all", b"Paste", b"Clear"]
CARET = "┃"


def check(label, cond, detail):
    global fails
    print(("  PASS  %-56s %s" if cond else "  FAIL  %-56s %s") % (label, detail))
    if not cond:
        fails += 1


def click(fd, sc, sgr, row, col, button=0, wait=0.4):
    os.write(fd, b"\x1b[<%d;%d;%dM" % (button, col + 1, row + 1))
    read(fd, sc, sgr, 0.25)
    os.write(fd, b"\x1b[<%d;%d;%dm" % (button, col + 1, row + 1))
    return read(fd, sc, sgr, wait)


def show(sc, label):
    print("     --- %s ---" % label)
    for r in range(24):
        l = sc.line(r).rstrip()
        if l.strip():
            print("     %2d %r" % (r, l))


def field(sc):
    """The command line's text, as a user reads it.

    Columns 4..75 of row 21: past the '> ' prompt, before the focus marker that
    prints at col 77. The caret glyph is dropped so an assertion is about the
    buffer, not about where the cursor happens to be.
    """
    return sc.line(CMDLINE_ROW)[4:76].rstrip().replace(CARET, "")


def menu_hi(sgr):
    """ITEMS[0] and ITEMS[1] only.

    The context menu's single item is 'Close', whose label [ Close ] is the same
    string as the file menu's third item, so a stale context row could
    contaminate ITEMS[2]. The context menu never renders Help or Open, so those
    two are clean witnesses for the file menu's cursor.
    """
    return [sgr.last_is_reversed(t) for t in (ITEMS[0], ITEMS[1])]


def case_a11_keyboard(binp):
    print("\n=== case 1: the context menu takes the keyboard on open (A11) ===")
    pid, fd, sc, sgr = spawn(binp)
    click(fd, sc, sgr, CMDLINE_ROW, 4, button=2)   # on the cmdline -> 4 items
    show(sc, "the context menu, opened by right-click")
    present = [n for n in CTX
               if any(sc.line(r).find(n.decode()) >= 0 for r in range(24))]
    check("all four text items are on screen", len(present) == 4,
          "found: %s" % ([p.decode() for p in present],))
    st = [sgr.last_is_reversed(t) for t in CTX]
    check("its FIRST item is highlighted on open",
          st == [True, False, False, False],
          "reverse(Copy,Select all,Paste,Clear) = %s" % (st,))
    key(fd, sc, sgr, b"\x1bOA", 0.5)               # UP at the top
    st = [sgr.last_is_reversed(t) for t in CTX]
    check("UP at the top clamps on item 1 (no wrap to the last)",
          st == [True, False, False, False], "states = %s" % (st,))
    key(fd, sc, sgr, DOWN, 0.5)
    st = [sgr.last_is_reversed(t) for t in CTX]
    check("DOWN moves the highlight to item 2",
          st == [False, True, False, False], "states = %s" % (st,))
    for _ in range(3):
        key(fd, sc, sgr, DOWN, 0.45)
    st = [sgr.last_is_reversed(t) for t in CTX]
    check("DOWN past the end clamps on item 4 (no wrap, no 5th row)",
          st == [False, False, False, True], "states = %s" % (st,))
    key(fd, sc, sgr, b"\x1b", 0.7)
    key(fd, sc, sgr, b"k", 0.6)
    check("Esc dismissed it and the keyboard is back in the field",
          field(sc) == "k", "field = %r" % (field(sc),))
    os.close(fd)


def case_a11_target(binp):
    print("\n=== case 2: ctx_target survives the focus move (A11) ===")
    print("    The whole round-trip is driven by keyboard: the menu is opened")
    print("    by right-click, then each item is reached with DOWN and Enter.")
    pid, fd, sc, sgr = spawn(binp)
    for ch in "hello":
        key(fd, sc, sgr, ch.encode(), 0.15)
    check("the field holds 'hello' before we start", field(sc) == "hello",
          "field = %r" % (field(sc),))

    def run(downs):
        click(fd, sc, sgr, CMDLINE_ROW, 4, button=2)
        for _ in range(downs):
            key(fd, sc, sgr, DOWN, 0.4)
        key(fd, sc, sgr, ENTER, 0.9)

    run(1)                                  # item 2: Select all
    run(0)                                  # item 1: Copy
    run(3)                                  # item 4: Clear
    # ui_clip_clear on a non-edit-text target only blanks n->user, which the menu
    # does not have - it is a no-op. So an empty field here is direct proof that
    # the clear reached the command line and not the focused menu.
    check("Clear emptied the field, so it acted on the field and not the menu",
          field(sc) == "", "field = %r" % (field(sc),))
    run(2)                                  # item 3: Paste
    check("Paste put the text back (the clip round-trip survived)",
          field(sc) == "hello", "field = %r" % (field(sc),))
    os.close(fd)


def case_v5(binp):
    print("\n=== case 3: Enter on ctx_close takes ONE layer, not two (V5) ===")
    pid, fd, sc, sgr = spawn(binp)
    key(fd, sc, sgr, b"z", 0.6)
    check("control: with no menu focused, 'z' reaches the field",
          field(sc) == "z", "field = %r" % (field(sc),))
    key(fd, sc, sgr, b"\x18", 0.6)          # ^X clears the field
    check("control: ^X cleared it, so the field starts empty",
          field(sc) == "", "field = %r" % (field(sc),))

    key(fd, sc, sgr, F8, 0.9)
    check("the file menu opened with Help as its cursor",
          menu_hi(sgr) == [True, False], "Help,Open reversed = %s" % (menu_hi(sgr),))
    key(fd, sc, sgr, b"z", 0.6)
    check("control: with the file menu focused, 'z' is swallowed",
          field(sc) == "", "field = %r" % (field(sc),))

    q = sc.find("quit")
    check("the header's quit button is locatable", q is not None, "quit at %s" % (q,))
    if q is None:
        return
    click(fd, sc, sgr, q[0], q[1], button=2)
    show(sc, "the context menu (Close) stacked over the file menu")
    check("the context menu opened over the file menu",
          sc.find("ctx_menu") is not None, "ctx_menu title on screen")
    check("its single Close item is highlighted, like any other menu",
          sgr.last_is_reversed(b"Close") is True, "Close reversed")
    check("the file menu underneath is still open",
          sc.find("file_menu") is not None, "file_menu title on screen")

    key(fd, sc, sgr, ENTER, 1.0)
    show(sc, "after Enter on ctx_close")

    # If both layers had gone, focus would have fallen to the command line and
    # 'z' would land there. It must not.
    key(fd, sc, sgr, b"z", 0.7)
    check("'z' is still swallowed, so a menu still holds the keyboard",
          field(sc) == "", "field = %r" % (field(sc),))
    # And it must be the FILE menu holding it, not a surviving context menu: the
    # context menu has one item, so UP/DOWN would clamp and leave Help reversed.
    # Help going plain while Open goes reversed can only be the file menu moving
    # its cursor - and the file menu's rows are never drawn by the context menu.
    key(fd, sc, sgr, DOWN, 0.7)
    check("DOWN still drives the file menu - the context menu lost the keyboard",
          menu_hi(sgr) == [False, True], "Help,Open reversed = %s" % (menu_hi(sgr),))
    check("the context menu is gone from the screen",
          sc.find("ctx_menu") is None, "no ctx_menu title")
    key(fd, sc, sgr, b"\x1b", 0.7)
    key(fd, sc, sgr, b"w", 0.7)
    check("Esc closed the surviving file menu and the field works again",
          field(sc) == "w", "field = %r" % (field(sc),))
    os.close(fd)


def case_a10(binp):
    print("\n=== case 4: a click on a menu row dismisses it (A10) ===")
    pid, fd, sc, sgr = spawn(binp)
    key(fd, sc, sgr, F8, 0.9)
    pos = None
    for r in range(24):
        x = sc.line(r).find("[ Open ]")
        if x >= 0:
            pos = (r, x + 2)
            break
    check("the Open row is locatable", pos is not None, "at %s" % (pos,))
    if pos is None:
        return
    click(fd, sc, sgr, pos[0], pos[1])
    check("the click echo names the row that was clicked",
          b"click mi_open" in sgr.buf, "status echo 'click mi_open' written")
    key(fd, sc, sgr, b"m", 0.7)
    check("the menu is gone and the keyboard is back in the field",
          field(sc) == "m", "field = %r" % (field(sc),))
    key(fd, sc, sgr, F8, 0.9)
    check("it reopens with the cursor back on item 1",
          menu_hi(sgr) == [True, False], "Help,Open reversed = %s" % (menu_hi(sgr),))
    key(fd, sc, sgr, DOWN, 0.6)
    check("and the arrows still work on it",
          menu_hi(sgr) == [False, True], "Help,Open reversed = %s" % (menu_hi(sgr),))
    os.close(fd)


def main():
    binp = os.path.abspath(sys.argv[1])
    os.chdir(os.environ.get("PROBE_CWD", "/tmp/irctui-menu/cur"))
    os.environ.setdefault("ASAN_OPTIONS", "detect_leaks=0")
    case_a11_keyboard(binp)
    case_a11_target(binp)
    case_v5(binp)
    case_a10(binp)
    print("\n---- %s ----" % ("all passed" if not fails else "%d FAILED" % fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
