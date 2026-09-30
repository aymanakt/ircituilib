#!/usr/bin/env python3
"""A group that also carries UI_F_CHILDREN_SELECTABLE must keep group semantics.

The flag's documentation claims this, on the grounds that the group key block
runs before the new cursor block. This measures it: with the flag ON a radio
group, the arrows must still APPLY the selection (the bullet moves and the row
is checked), not merely move a cursor. If the new block had been placed above
the group block, Down would return 1 before group_apply_row ever ran.

The discriminator is the radio bullet itself: group_apply_row (:1194) moves
UI_F_CHECKED between children, which is what changes the glyph.
"""
import os, sys
sys.path.insert(0, "/tmp/irctui-menu/g3")
from g3_menu import spawn, key, DOWN, UP  # noqa

F7 = b"\x1b[18~"
fails = 0


def check(label, cond, detail):
    global fails
    print(("  PASS  %-58s %s" if cond else "  FAIL  %-58s %s") % (label, detail))
    if not cond:
        fails += 1


def bullets(sc):
    out = []
    for r in range(24):
        l = sc.line(r)
        for i, ch in enumerate(l):
            if ch in ("●", "○"):
                out.append((r, ch))
    return out


def main():
    binp = os.path.abspath(sys.argv[1])
    os.chdir(os.path.dirname(binp))
    os.environ.setdefault("ASAN_OPTIONS", "detect_leaks=0")
    pid, fd, sc, sgr = spawn(binp)

    print("a flagged radio group still applies its selection")
    key(fd, sc, sgr, F7, 1.0)
    b0 = bullets(sc)
    print("    bullets on open: %s" % (b0,))
    check("the radio starts on its first row", b0 and b0[0][1] == "●",
          "first bullet = %r" % (b0[0][1] if b0 else None,))

    key(fd, sc, sgr, DOWN, 0.6)
    key(fd, sc, sgr, DOWN, 0.6)
    b2 = bullets(sc)
    print("    bullets after 2x Down: %s" % (b2,))
    check("Down still APPLIES the selection, not just a cursor",
          b2 and b2[0][1] == "○" and b2[-1][1] == "●",
          "first=%r last=%r" % (b2[0][1] if b2 else None, b2[-1][1] if b2 else None))

    key(fd, sc, sgr, UP, 0.6)
    b1 = bullets(sc)
    check("Up moves the selection back one row",
          b1 and b1[1][1] == "●" and b1[0][1] == "○",
          "bullets = %s" % ([c for _, c in b1],))

    key(fd, sc, sgr, b"\x03", 0.3)
    print("\n---- %s ----" % ("all passed" if not fails else "%d FAILED" % fails))
    try: os.close(fd)
    except OSError: pass
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
