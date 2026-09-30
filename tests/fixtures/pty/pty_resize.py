#!/usr/bin/env python3
"""The scenario D3 exists for: a popup is open when the terminal shrinks.

Opens the Tools panel (F7) at 80x24, then resizes the PTY with TIOCSWINSZ, which
makes ncurses deliver SIGWINCH -> getch() returns KEY_RESIZE -> ui_main.c:63-66
calls ui_layout(root, ...) + ui_realize(root).

Reports, per target size: how many bytes came back, how many full-screen clears
were in them, and whether anything is on the screen afterwards. Also reports
whether a GRow behaves differently from a SHrink, and does the same run with no
popup open so the popup's contribution can be separated from the resize path's.

Usage: pty_resize.py <binary>
"""
import os, fcntl, struct, termios, sys, pty

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vt import Screen, drain  # noqa: E402


def setsize(fd, rows, cols):
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))


def run(binp, to_r, to_c, popup):
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm-256color"
        os.execv(binp, [binp])
    setsize(fd, 24, 80)
    sc = Screen(24, 80)
    sc.feed(drain(fd, 1.2))
    if popup:
        os.write(fd, b"\x1b[18~")               # F7: tools_panel
        sc.feed(drain(fd, 1.2))

    setsize(fd, to_r, to_c)
    raw = drain(fd, 1.5)
    sc2 = Screen(to_r, to_c)
    sc2.feed(raw)
    clears = raw.count(b"\x1b[H\x1b[J") + raw.count(b"\x1b[H\x1b[2J")
    close = sc2.find("Close")
    # Where does the panel's right edge land? A popup clamped into the canvas has
    # its right border on screen; one left hanging off the edge is cut at the
    # boundary instead.
    edge = "-"
    if close:
        row = sc2.g[close[0] - 1]
        last = max((i for i, ch in enumerate(row) if ch != " "), default=-1)
        edge = "col %d char %r" % (last + 1, row[last] if last >= 0 else "")
    print("popup=%-5s 80x24 -> %2dx%-3d : bytes=%4d clears=%d painted=%4d 'IRCIT'=%-9s 'Close'=%-9s rightedge=%s"
          % (popup, to_r, to_c, len(raw), clears, sc2.nonblank(),
             str(sc2.find("IRCIT")), str(close), edge))

    # Does the next keypress restore the view?
    os.write(fd, b"\x1b[17~")                   # F6: forces layout+realize+draw
    sc2.feed(drain(fd, 1.2))
    close2 = sc2.find("Close")
    edge2 = "-"
    if close2:
        row = sc2.g[close2[0] - 1]
        last = max((i for i, ch in enumerate(row) if ch != " "), default=-1)
        edge2 = "col %d char %r" % (last + 1, row[last] if last >= 0 else "")
    print("             after F6   : painted=%4d 'IRCIT'=%-9s 'Close'=%-9s rightedge=%s"
          % (sc2.nonblank(), str(sc2.find("IRCIT")), str(close2), edge2))

    os.write(fd, b"\x1b[21~")
    drain(fd, 0.3)
    try:
        os.close(fd)
    except OSError:
        pass
    os.waitpid(pid, os.WNOHANG)


def main():
    binp = sys.argv[1]
    for (r, c) in ((10, 30), (18, 50), (20, 60), (30, 100)):
        run(binp, r, c, popup=False)
        run(binp, r, c, popup=True)


if __name__ == "__main__":
    main()
