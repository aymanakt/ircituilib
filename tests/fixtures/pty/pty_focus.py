#!/usr/bin/env python3
"""End-to-end: does the input field get focus back when a popup closes?

  click File        -> file_menu opens; focus = menu_file (a plain click)
  click mi_open     -> focus = mi_open, i.e. INSIDE the popup. The menu STAYS
                       open: ui_button_input only closes for close-ish and
                       ctx_* ids, everything else falls through to the branch
                       that writes "btn <id>" to status and leaves the layer up.
  Esc               -> ui_close_top_popup() destroys the layer holding focus
  type "hello"      -> only reaches cmdline if focus was re-resolved to it

The last step is the user-visible point. ui_button_input returns immediately for
any key that is not mouse/Enter, so typing into a focused BUTTON is silently
dropped: the characters are echoed only if focus is back on the edit field.

Note on the escape stream: each repaint the demo emits ends with a bare
ESC[H ESC[2J whose repaint follows in the next write. Feeding that trailing
clear into the screen model would blank it, so it is stripped per chunk. The
model is validated on every step by asserting known labels are on screen.

Usage: pty_focus.py <binary> [label]
"""
import os, pty, struct, sys, fcntl, termios

sys.path.insert(0, "/tmp/irctui-wsd")
from vt import Screen, drain  # noqa: E402

TRAILING_CLEAR = b"\x1b[H\x1b[2J"


def spawn(binp, rows=24, cols=80):
    pid, fd = pty.fork()
    if pid == 0:
        fcntl.ioctl(0, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
        os.environ["TERM"] = "xterm-256color"
        os.execv(binp, [binp])
    return pid, fd


def pump(sc, fd, secs):
    """Drain, strip the repaint-boundary clear, feed the model. Returns bytes."""
    data = drain(fd, secs)
    sc.feed(data[:-8] if data.endswith(TRAILING_CLEAR) else data)
    return data


def click(fd, row, col):
    """SGR-1006 mouse press+release at 1-based (col,row)."""
    os.write(fd, b"\x1b[<0;%d;%dM\x1b[<0;%d;%dm" % (col, row, col, row))


def row_text(sc, row):
    return "".join(sc.g[row]).rstrip() if row is not None else ""


def cmdline_row(sc):
    """The row carrying the '>' prompt of the input field."""
    for y, r in enumerate(sc.g):
        if "".join(r).lstrip("│ ").startswith(">"):
            return y
    return None


def run(binp, label):
    pid, fd = spawn(binp)
    sc = Screen(24, 80)
    pump(sc, fd, 1.5)

    f = sc.find("File")
    ir = sc.find("IRCIT")
    if not f or not ir:
        print("  ABORT: the UI did not paint (File=%s IRCIT=%s)" % (f, ir))
        os.write(fd, b"\x1b[21~")
        drain(fd, 0.3)
        os.close(fd)
        os.waitpid(pid, 0)
        return None
    print("%s" % label)
    print("  initial           : IRCIT=%s File=%s nonblank=%d"
          % (ir, f, sc.nonblank()))

    # 1. open the File menu by clicking its header button
    click(fd, f[0], f[1] + 2)
    pump(sc, fd, 1.2)
    item = sc.find("Open")
    if not item:
        print("  ABORT: the menu did not paint, so the rest would be vacuous")
        os.write(fd, b"\x1b[21~")
        drain(fd, 0.3)
        os.close(fd)
        os.waitpid(pid, 0)
        return None
    print("  after click File  : menu open, 'Open' at %s" % (item,))

    # 2. click the ITEM, so focus ends up inside the popup subtree
    click(fd, item[0], item[1] + 1)
    pump(sc, fd, 1.2)
    still = sc.find("Open")
    print("  after click item  : menu still open=%s (button that is not "
          "close/ctx_* leaves the layer up)" % (still is not None,))

    # 3. Esc destroys the layer that held focus
    os.write(fd, b"\x1b")
    pump(sc, fd, 1.5)
    gone = sc.find("Open") is None
    print("  after Esc         : menu gone=%s" % gone)

    # 4. type. Echoed only if focus is back on the edit field.
    row = cmdline_row(sc)
    before = row_text(sc, row)
    os.write(fd, b"hello")
    pump(sc, fd, 1.2)
    after = row_text(sc, row)
    landed = "hello" in after
    print("  cmdline before    : %r" % before)
    print("  cmdline after     : %r" % after)
    print("  RESULT            : %s"
          % ("typed text reached the input field" if landed else
             "typed text went nowhere (focus is not on an editor)"))

    os.write(fd, b"\x1b[21~")
    drain(fd, 0.4)
    try:
        os.close(fd)
    except OSError:
        pass
    os.waitpid(pid, 0)
    return landed


if __name__ == "__main__":
    r = run(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else sys.argv[1])
    sys.exit(0 if r else 1)
