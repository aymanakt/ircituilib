#!/usr/bin/env python3
"""What does ncurses deliver for each motion SHAPE, with and without the bit?

dragbits.py sends a fixed sequence. This one sends the shapes separately so the
answer is attributable:

  1. motion, NO button held   (SGR Cb=35) - what moving the mouse costs
  2. press, no motion         (Cb=0)
  3. motion, button 1 held    (Cb=32)     - the drag we want
  4. release                  (Cb=0, 'm')

Run with and without the "motion" argv flag to see the bit's effect.
"""
import os, pty, select, sys, time

SEQ = [(b"\x1b[<35;11;6M", "motion, no button  (35)"),
       (b"\x1b[<35;12;6M", "motion, no button  (35)"),
       (b"\x1b[<0;12;6M",  "press               (0)"),
       (b"\x1b[<32;13;6M", "motion, button1 held(32)"),
       (b"\x1b[<32;14;6M", "motion, button1 held(32)"),
       (b"\x1b[<0;14;6m",  "release             (m)")]


def pump(fd, secs):
    got = bytearray()
    end = time.time() + secs
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.05)
        if fd in r:
            try:
                c = os.read(fd, 65536)
            except OSError:
                break
            if not c:
                break
            got += c
    return bytes(got)


def main():
    flag = sys.argv[1] if len(sys.argv) > 1 else ""
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm-256color"
        os.environ["LINES"], os.environ["COLUMNS"] = "24", "80"
        os.execv("/tmp/irctui-caret/dragbits",
                 ["/tmp/irctui-caret/dragbits", flag])
    out = pump(fd, 1.0)
    print("=== dragbits mousemask=%s ===" % (flag or "ALL_MOUSE_EVENTS"))
    for data, label in SEQ:
        os.write(fd, data)
        got = pump(fd, 0.35).decode("utf-8", "replace")
        lines = [l.strip() for l in got.split("\n") if "MOUSE" in l]
        print("  %-24s -> %s" % (label, " | ".join(lines) if lines else "(no event)"))
    os.write(fd, b"q")
    pump(fd, 0.2)
    try:
        os.close(fd)
    except OSError:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
