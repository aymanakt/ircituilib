#!/usr/bin/env python3
"""Drive ui_demo under a PTY and check screen content."""
import os, pty, select, time, sys, re

def read_more(fd, secs=0.8):
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

def visible(buf: bytes) -> str:
    s = buf.decode("utf-8", "replace")
    s = re.sub(r"\x1b\[[0-9;?]*[A-Za-z]", "", s)
    s = re.sub(r"\x1b.", "", s)
    return s

def main():
    binp = sys.argv[1] if len(sys.argv) > 1 else "/tmp/ui_demo"
    os.chdir(os.path.dirname(os.path.abspath(__file__)))
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm-256color"
        os.execv(binp, [binp])
    buf = read_more(fd, 1.0)
    vis = visible(buf)
    checks = []
    def ok(name, cond):
        checks.append((name, cond))
        print(("PASS" if cond else "FAIL"), name)

    ok("home_title", "IRCIT" in vis)
    ok("home_file", "File" in vis)
    # F7 = CSI 18~
    os.write(fd, b"\x1b[18~")
    buf += read_more(fd, 1.0)
    vis = visible(buf)
    ok("tools_title", "tools" in vis.lower() or "drag" in vis.lower())
    ok("tools_close", "Close" in vis)
    ok("tools_radio", ("(*)" in vis or "(*)" in vis or "( " in vis or "1" in vis) and ("Close" in vis))
    # Esc close
    os.write(fd, b"\x1b")
    buf += read_more(fd, 0.5)
    os.write(fd, b"\x1b[19~")  # F8 file menu
    buf += read_more(fd, 0.8)
    vis = visible(buf)
    ok("file_menu", "Help" in vis or "Open" in vis or "Close" in vis)
    os.write(fd, b"q")
    read_more(fd, 0.3)
    try:
        os.close(fd)
    except OSError:
        pass
    failed = [n for n,c in checks if not c]
    print("summary", f"{len(checks)-len(failed)}/{len(checks)}")
    sys.exit(1 if failed else 0)

if __name__ == "__main__":
    main()
