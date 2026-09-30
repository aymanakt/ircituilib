#!/usr/bin/env python3
"""Mouse + resize differential. Usage: diffdrv3.py <binary> <seq> <cwd>"""
import os, pty, sys, time, select, signal, fcntl, termios, struct
sys.path.insert(0, "/tmp/irctui-caret")
from pty_screen import Screen  # noqa
ESC = b"\x1b"
def click(btn, col, row, release=True):
    return [(b"\x1b[<%d;%d;%dM" % (btn, col, row))] + ([b"\x1b[<%d;%d;%dm" % (btn, col, row)] if release else [])
# header row 2: "[ File ]" around col 46; "[ Tools ]" around col 62
SEQ = {
    "click_file":    [ESC] ,  # placeholder replaced below
}
SEQ = {
    "click_file":    (click(0, 46, 3) + click(0, 46, 3)),
    "rightclick":    (click(2, 46, 3)),
    "click_tools":   (click(0, 62, 3)),
    "click_canvas":  (click(0, 30, 10)),
    "click_quit":    (click(0, 70, 3)),
    "click_menu_item": (click(0, 62, 3) + click(0, 40, 5)),
}
def setwinsz(fd, r, c):
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", r, c, 0, 0))
def drain(fd, sc, secs):
    end = time.time() + secs
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.05)
        if fd in r:
            try: d = os.read(fd, 65536)
            except OSError: return
            if not d: return
            sc.feed(d)
def main():
    binp, name, cwd = os.path.abspath(sys.argv[1]), sys.argv[2], sys.argv[3]
    os.chdir(cwd)
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm-256color"
        os.environ["LINES"] = "24"; os.environ["COLUMNS"] = "80"
        os.execv(binp, [binp])
    setwinsz(fd, 24, 80)
    sc = Screen(rows=30, cols=100)
    drain(fd, sc, 1.0)
    if name == "resize_bigger":
        setwinsz(fd, 30, 100); os.kill(pid, signal.SIGWINCH); drain(fd, sc, 1.0)
    elif name == "resize_smaller":
        setwinsz(fd, 16, 60); os.kill(pid, signal.SIGWINCH); drain(fd, sc, 1.0)
    else:
        for k in SEQ[name]:
            os.write(fd, k); drain(fd, sc, 0.5)
    for r in range(24):
        print("%2d|%s" % (r, sc.line(r).rstrip()))
    try: os.close(fd)
    except OSError: pass
    try: os.kill(pid, signal.SIGKILL)
    except OSError: pass
main()
