#!/usr/bin/env python3
"""Dump the delta of each Down, to see whether the old row loses its reverse."""
import os, pty, select, sys, time
def pump(fd, secs):
    out=b""; end=time.time()+secs
    while time.time()<end:
        r,_,_=select.select([fd],[],[],0.05)
        if fd in r:
            try: c=os.read(fd,65536)
            except OSError: break
            if not c: break
            out+=c
    return out
binp=os.path.abspath(sys.argv[1]); os.chdir("/tmp/irctui-menu/cur")
pid,fd=pty.fork()
if pid==0:
    os.environ["TERM"]="xterm-256color"; os.environ["LINES"],os.environ["COLUMNS"]="24","80"
    os.execv(binp,[binp])
pump(fd,1.6)
os.write(fd,b"\x1b[19~"); d1=pump(fd,0.9)
print("=== after F8: %d bytes ===" % len(d1)); print(repr(d1)); print()
os.write(fd,b"\x1bOB"); d2=pump(fd,0.6)
print("=== after DOWN #1: %d bytes ===" % len(d2)); print(repr(d2)); print()
os.write(fd,b"\x1bOB"); d3=pump(fd,0.6)
print("=== after DOWN #2: %d bytes ===" % len(d3)); print(repr(d3)); print()
os.write(fd,b"\x03"); pump(fd,0.3)
try: os.close(fd)
except OSError: pass
