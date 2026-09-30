import os, pty, sys, time, select, fcntl, termios, struct, re

COLS, ROWS = 80, 24
BIN = "/home/devops/repos/ircit/packages/ircituilib/build/dev/ircitui_demo"

def strip(s):
    return re.sub(r'\x1b\[[0-9;?]*[a-zA-Z]|\x1b[()][A-Z0-9]|\x1b[=>]', '', s)

def run(keys, label, settle=0.6):
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm-256color"
        os.chdir("/home/devops/repos/ircit/packages/ircituilib")
        os.execv(BIN, [BIN])
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", ROWS, COLS, 0, 0))
    out = b""
    def drain(t):
        nonlocal out
        end = time.time() + t
        while time.time() < end:
            r,_,_ = select.select([fd], [], [], 0.1)
            if r:
                try: out += os.read(fd, 65536)
                except OSError: return
    drain(1.0)
    for k in keys:
        os.write(fd, k)
        drain(settle)
    drain(1.0)
    os.write(fd, b"\x1b[21~")   # F10
    drain(0.5)
    try: os.kill(pid, 9)
    except ProcessLookupError: pass
    os.waitpid(pid, 0)
    open(f"/tmp/demo_{label}.raw","wb").write(out)
    return out

# F7 = ESC O S? xterm F7 is \x1b[18~
for label, keys in [("i80", []), ("t80", [b"\x1b[18~"])]:
    o = run(keys, label)
    print(f"===== {label}: {len(o)} bytes =====")
