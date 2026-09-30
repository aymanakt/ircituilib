import os, pty, time, select, fcntl, termios, struct, sys
sys.path.insert(0, '/tmp')
COLS, ROWS = 100, 35
BIN = "/home/devops/repos/ircit/packages/ircituilib/build/dev/ircitui_demo"

def run(keys, label, settle=0.7):
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm-256color"
        os.chdir("/home/devops/repos/ircit/packages/ircituilib")
        os.execv(BIN, [BIN])
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", ROWS, COLS, 0, 0))
    out = b""
    def drain(t):
        nonlocal out
        end = time.time()+t
        while time.time() < end:
            r,_,_ = select.select([fd],[],[],0.1)
            if r:
                try: out += os.read(fd, 65536)
                except OSError: return
    drain(1.0)
    for k in keys:
        os.write(fd, k); drain(settle)
    drain(0.8)
    os.write(fd, b"\x1b[21~"); drain(0.4)
    try: os.kill(pid, 9)
    except ProcessLookupError: pass
    os.waitpid(pid, 0)
    open(f"/tmp/clk_{label}.raw","wb").write(out); return out

def click(x, y):   # 1-based SGR
    return f"\x1b[<0;{x};{y}M\x1b[<0;{x};{y}m".encode()

# "Tools" spans cols 65..71 on row 3 (1-based) at 100 cols
TX, TY = 67, 3
run([b"\x1b[20~"], "f9only")
run([b"\x1b[20~", click(TX,TY)], "allow")   # F9 then click
print("done")
