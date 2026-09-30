#!/usr/bin/env python3
"""The cost that the byte count cannot see.

live_motioncost.py showed 40 buttonless motions cost the same number of bytes
as 40 dropped ones (92 B both builds) - doupdate diffs against the physical
screen, so an unchanged frame writes nothing. But the engine still does the
work per event: ui_hit_test walks the tree, then the demo runs ui_mark_overlap +
ui_draw over every node. With REPORT_MOUSE_POSITION the terminal reports motion
at whatever rate the mouse moves, so that work happens on every move.

So measure CPU, not bytes: read utime+stime from /proc/<pid>/stat before and
after a burst of buttonless motion. now2_demo drops every one of those events;
dragfix_demo delivers them. The difference is the cost of the fix.

Usage: live_motioncpu.py binA binB
"""
import os, pty, select, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import Screen

CMDROW = 21
N = 2500
HZ = os.sysconf("SC_CLK_TCK")


def cpu_ticks(pid):
    with open("/proc/%d/stat" % pid, "rb") as f:
        data = f.read()
    # comm may contain spaces/parens: split on the LAST ')' to be safe
    fields = data[data.rindex(b")") + 2:].split()
    return int(fields[11]) + int(fields[12])      # utime + stime


def drain(fd, secs):
    got = bytearray()
    end = time.time() + secs
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.02)
        if fd not in r:
            continue
        try:
            c = os.read(fd, 65536)
        except OSError:
            break
        if not c:
            break
        got += c
    return bytes(got)


def motion(col, row, btn):
    return b"\x1b[<%d;%d;%dM" % (btn, col + 1, row + 1)


def burst(fd, sc, n, btn, x0):
    """Send n motion events in small chunks, draining between them.

    A tight 3000-event loop (39 KB) overruns the pty input buffer and the events
    are dropped on BOTH builds, which made the first version of this script
    report identical numbers for builds that provably differ. Pacing keeps the
    backlog inside the buffer so the program actually sees every event.
    """
    CHUNK, out = 20, bytearray()
    sent = 0
    while sent < n:
        k = min(CHUNK, n - sent)
        for i in range(k):
            os.write(fd, motion(x0 + ((sent + i) % 40), CMDROW, btn))
        sent += k
        out += drain(fd, 0.01)
    out += drain(fd, 0.6)
    return bytes(out)


def measure(binp):
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm-256color"
        os.environ["LINES"], os.environ["COLUMNS"] = "24", "80"
        os.execv(binp, [binp])
    sc = Screen(24, 80)
    drain(fd, 1.5)
    drain(fd, 0.5)
    # The cmdline starts EMPTY. A drag over an empty field paints nothing on
    # either build, which is indistinguishable from "the events were dropped" -
    # the same confound that made live_textview_drag.py read 0 on both builds
    # until F6 filled the canvas. Put text in it first.
    for ch in "abcdefghijklmnop":
        os.write(fd, ch.encode())
    drain(fd, 0.5)

    w0 = cpu_ticks(pid)
    idle_bytes = len(drain(fd, 1.0))
    idle_cpu = (cpu_ticks(pid) - w0) / HZ

    w1 = cpu_ticks(pid)
    out = burst(fd, sc, N, 35, 3)              # buttonless
    moved_cpu = (cpu_ticks(pid) - w1) / HZ

    # POSITIVE CONTROL. Buttonless motion is delivered but changes nothing on
    # screen. A held-button drag DOES paint (live_drag.py: 0 vs 2 reverse runs),
    # so a working harness must show a difference here. If it does not, the
    # numbers above mean nothing.
    os.write(fd, motion(6, CMDROW, 0))          # press
    drain(fd, 0.15)
    w2 = cpu_ticks(pid)
    out2 = burst(fd, sc, N, 32, 7)              # drag
    drag_cpu = (cpu_ticks(pid) - w2) / HZ
    os.write(fd, b"\x1b[<%d;%dm" % (6 + 1, CMDROW + 1))   # release
    drain(fd, 0.2)

    os.write(fd, b"\x03")
    drain(fd, 0.3)
    try:
        os.close(fd)
    except OSError:
        pass

    print("%-13s idle %5.3f s/1.0 s %3d B | buttonless x%d %5.3f s %5d B"
          " | HELD DRAG x%d %5.3f s %5d B"
          % (os.path.basename(binp), idle_cpu, idle_bytes, N, moved_cpu,
             len(out), N, drag_cpu, len(out2)))
    return drag_cpu


def main():
    for b in sys.argv[1:]:
        measure(b)
    return 0


if __name__ == "__main__":
    sys.exit(main())
