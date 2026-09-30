#!/usr/bin/env python3
"""Drive dragbits with a real press / motion / release and print what arrives."""
import os, pty, select, sys, time


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
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm-256color"
        os.environ["LINES"], os.environ["COLUMNS"] = "24", "80"
        os.execv("/tmp/irctui-caret/dragbits", ["/tmp/irctui-caret/dragbits"] + sys.argv[2:])

    out = pump(fd, 1.0)
    print("--- startup ---")
    print(out.decode("utf-8", "replace").strip()[:200])

    # press at (10,5), then motion across, then release
    seqs = [(b"\x1b[<0;11;6M", "press  (0)"),
            (b"\x1b[<32;12;6M", "motion (32)"),
            (b"\x1b[<32;13;6M", "motion (32)"),
            (b"\x1b[<32;14;6M", "motion (32)"),
            (b"\x1b[<0;14;6m", "release(0, m)")]
    for data, label in seqs:
        os.write(fd, data)
        got = pump(fd, 0.35)
        txt = got.decode("utf-8", "replace")
        lines = [l for l in txt.split("\n") if "MOUSE" in l or "KEY" in l]
        print("%-14s -> %s" % (label, " | ".join(lines) if lines else "(no event)"))

    os.write(fd, b"q")
    pump(fd, 0.2)
    try:
        os.close(fd)
    except OSError:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
