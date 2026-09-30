#!/usr/bin/env python3
"""A7: dispatch-then-dismiss must pop exactly one layer, by identity.

menu_activate captures the container's id, calls the item's handler, then
re-resolves the layer with ui_find and pops it only if it is still there. Two
things can go wrong and both are observable when a SECOND layer sits underneath:

  - popping by z-order (ui_close_top_popup) instead of by identity: the item's
    own handler already popped this menu, so the second pop takes the layer
    underneath;
  - not popping at all when the item does not pop itself: the menu stays open
    after Enter, which is the opposite failure.

So the test stacks two layers: the tools panel first (F8's sibling, F7), then
the file menu on top (posted later, so it is the top layer and takes focus). The
panel underneath is the witness - it must survive either Enter path.

Planned V5 drove the context menu instead, but the context menu only becomes
keyboard-navigable with A11, which is G-4. This is the same property tested
where it is reachable today.

Usage: g3_v5.py <binary>
"""
import os
import sys

sys.path.insert(0, "/tmp/irctui-caret")
sys.path.insert(0, "/tmp/irctui-menu/g3")
from g3_menu import spawn, key, DOWN, ENTER, F8, CMDLINE_ROW  # noqa

F7 = b"\x1b[18~"
fails = 0


def check(label, cond, detail):
    global fails
    print(("  PASS  %-58s %s" if cond else "  FAIL  %-58s %s") % (label, detail))
    if not cond:
        fails += 1


def frames(sc):
    """How many popup frames are on screen, by their title bars."""
    return (sum(sc.line(r).count("file_menu") for r in range(24)),
            sum(sc.line(r).count("tools_panel") for r in range(24)))


def case(binp, label, downs, item):
    print("\n=== %s ===" % label)
    pid, fd, sc, sgr = spawn(binp)
    key(fd, sc, sgr, F7, 0.9)            # tools panel, underneath
    key(fd, sc, sgr, F8, 0.9)            # file menu, on top and focused
    fm, tp = frames(sc)
    check("both layers are open, file_menu on top",
          fm == 1 and tp == 1, "file_menu=%d tools_panel=%d" % (fm, tp))
    for _ in range(downs):
        key(fd, sc, sgr, DOWN, 0.45)
    key(fd, sc, sgr, ENTER, 1.0)
    fm, tp = frames(sc)
    # The panel underneath is the witness: a z-order double pop would take it.
    check("exactly one layer went (the panel underneath survived)",
          fm == 0 and tp == 1, "file_menu=%d tools_panel=%d" % (fm, tp))
    # Liveness, staleness-immune: if the panel is really there and focus was
    # re-resolved, the arrow must still drive its radio group.
    before = [sc.line(r) for r in range(24)]
    key(fd, sc, sgr, DOWN, 0.6)
    moved = any("●" in sc.line(r) for r in range(24)) and before != \
        [sc.line(r) for r in range(24)]
    check("the surviving panel still answers keys", moved,
          "its radio group moved on Down")
    try:
        os.write(fd, b"\x03")
        os.close(fd)
    except OSError:
        pass


def main():
    binp = os.path.abspath(sys.argv[1])
    os.chdir(os.environ.get("PROBE_CWD", "/tmp/irctui-menu/cur"))
    os.environ.setdefault("ASAN_OPTIONS", "detect_leaks=0")

    # Down x2 puts the cursor on Close, whose handler pops the layer itself.
    case(binp, "Enter on Close (the item pops its own layer)", 2, "mi_close")
    # Down x1 puts it on Open, which does NOT pop - menu_activate must.
    case(binp, "Enter on Open (the item does not pop; menu_activate must)", 1,
         "mi_open")

    print("\n---- %s ----" % ("all passed" if not fails else "%d FAILED" % fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
