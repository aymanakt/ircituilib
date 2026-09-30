#!/usr/bin/env python3
"""Drive ui_demo through the two in-tree paths WS-D1 changes the window type of:
the F5 floating window (IrcitUICreateFloatingWindow + IrcitUIAddChild/AddButton)
and the right-click context menu (IrcitUIShowContextMenu). Prints the visible
screen after each step so head and patched can be diffed.

Usage: pty_wsd.py <binary> <outfile>
"""
import os, pty, select, time, sys, re


def read_more(fd, secs=1.0):
    out = b""
    end = time.time() + secs
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.1)
        if fd in r:
            try:
                c = os.read(fd, 8192)
            except OSError:
                break
            if not c:
                break
            out += c
    return out


def visible(buf):
    s = buf.decode("utf-8", "replace")
    s = re.sub(r"\x1b\[[0-9;?]*[A-Za-z]", "", s)
    s = re.sub(r"\x1b.", "", s)
    return s


def main():
    binp, outp = sys.argv[1], sys.argv[2]
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm-256color"
        os.execv(binp, [binp])

    log = []
    buf = read_more(fd, 1.2)
    log.append(("initial", visible(buf)))

    # F5 -> code_win floating window with two IrcitUIAddChild children
    os.write(fd, b"\x1b[15~")
    buf = read_more(fd, 1.2)
    log.append(("after_f5", visible(buf)))

    # right-click on the body area -> ctx_menu with IrcitUIAddButton children.
    # SGR 1006: button 2 == right, coords are 1-based.
    for (col, row) in ((10, 8), (20, 6), (5, 12)):
        os.write(fd, b"\x1b[<%d;%d;%dM\x1b[<%d;%d;%dm" % (2, col, row, 2, col, row))
        buf += read_more(fd, 0.7)
    log.append(("after_rclick", visible(buf)))

    # drag the floating window with a left press+move+release on its border
    os.write(fd, b"\x1b[<0;30;5M\x1b[<32;34;7M\x1b[<0;34;7m")
    buf += read_more(fd, 0.8)
    log.append(("after_drag", visible(buf)))

    os.write(fd, b"\x1b[21~")  # F10 quit
    read_more(fd, 0.5)
    try:
        os.close(fd)
    except OSError:
        pass

    with open(outp, "w") as f:
        for name, vis in log:
            f.write("===== %s =====\n%s\n" % (name, vis))
    print("wrote", outp, "(%d steps)" % len(log))


if __name__ == "__main__":
    main()
