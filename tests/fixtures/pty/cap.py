import os, pty, time, select, fcntl, termios, struct, sys
sys.path.insert(0,'/tmp')
COLS, ROWS = 80, 24
BIN = "/home/devops/repos/ircit/packages/ircituilib/build/dev/ircitui_demo"
def run(keys, settle=0.5):
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"]="xterm-256color"
        os.chdir("/home/devops/repos/ircit/packages/ircituilib")
        os.execv(BIN,[BIN])
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH",ROWS,COLS,0,0))
    out=b""
    def drain(t):
        nonlocal out
        end=time.time()+t
        while time.time()<end:
            r,_,_=select.select([fd],[],[],0.1)
            if r:
                try: out+=os.read(fd,65536)
                except OSError: return
    drain(0.9)
    for k in keys: os.write(fd,k); drain(settle)
    drain(0.6)
    os.write(fd,b"\x1b[21~"); drain(0.3)
    try: os.kill(pid,9)
    except ProcessLookupError: pass
    os.waitpid(pid,0)
    return out
def click(x,y): return f"\x1b[<0;{x};{y}M\x1b[<0;{x};{y}m".encode()
open("/tmp/clk_menu.raw","wb").write(run([click(64,3)]))
open("/tmp/clk_deny.raw","wb").write(run([b"\x1b[20~", click(64,3)]))
print("captured")
