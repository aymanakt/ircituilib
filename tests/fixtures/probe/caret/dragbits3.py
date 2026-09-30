#!/usr/bin/env python3
"""Drive dragbits3 and report (a) which DECSET ncurses actually put in force
and (b) whether buttonless / held-button motion is delivered.

The DECSET is read off the wire, not assumed: the startup bytes are printed.
?1000h = basic press/release, ?1002h = motion while a button is held,
?1003h = ALL motion (idle mouse movement included).
"""
import os, pty, re, select, sys, time

DECSET = re.compile(rb"\x1b\[\?(\d+)([hl])")

SEQ = [(b"\x1b[<35;11;6M", "motion, no button"),
       (b"\x1b[<0;11;6M",  "press"),
       (b"\x1b[<32;12;6M", "motion, button1 held"),
       (b"\x1b[<0;12;6m",  "release")]


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


def run(mask, fputs):
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm-256color"
        os.environ["LINES"], os.environ["COLUMNS"] = "24", "80"
        os.execv("/tmp/irctui-caret/dragbits3",
                 ["/tmp/irctui-caret/dragbits3", mask, fputs])
    init = pump(fd, 1.0)
    modes = []
    for n, hl in DECSET.findall(init):
        if hl == b"h" and n in (b"1000", b"1002", b"1003", b"1006"):
            modes.append(n.decode())
    print("mask=%-6s fputs=%-3s -> in force: %s"
          % (mask, fputs, ",".join(modes) if modes else "(none)"))
    for data, label in SEQ:
        os.write(fd, data)
        got = pump(fd, 0.3).decode("utf-8", "replace")
        lines = [l.strip() for l in got.split("\n") if "MOUSE" in l]
        print("      %-22s -> %s" % (label, " | ".join(lines) if lines else "(no event)"))
    os.write(fd, b"q")
    pump(fd, 0.2)
    try:
        os.close(fd)
    except OSError:
        pass


def main():
    for mask, fputs in [("all", "yes"), ("motion", "yes"),
                        ("motion", "no"), ("all", "no")]:
        run(mask, fputs)
        print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
