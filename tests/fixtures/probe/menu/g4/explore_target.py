#!/usr/bin/env python3
"""What does the field actually contain after each ctx_ item, by keyboard?"""
import os
import sys

sys.path.insert(0, "/tmp/irctui-caret")
sys.path.insert(0, "/tmp/irctui-menu/g3")
from g3_menu import spawn, key, read, DOWN, ENTER, CMDLINE_ROW  # noqa


def click(fd, sc, sgr, row, col, button=2):
    os.write(fd, b"\x1b[<%d;%d;%dM" % (button, col + 1, row + 1))
    read(fd, sc, sgr, 0.25)
    os.write(fd, b"\x1b[<%d;%d;%dm" % (button, col + 1, row + 1))
    read(fd, sc, sgr, 0.35)


binp = os.path.abspath(sys.argv[1])
os.chdir("/tmp/irctui-menu/cur")
pid, fd, sc, sgr = spawn(binp)
for ch in "hello":
    key(fd, sc, sgr, ch.encode(), 0.15)
print("typed       : %r" % sc.line(CMDLINE_ROW))
for r in (20, 21, 22):
    print("  row %d raw: %r" % (r, sc.line(r)))


def run(label, downs):
    click(fd, sc, sgr, CMDLINE_ROW, 4)
    for _ in range(downs):
        key(fd, sc, sgr, DOWN, 0.4)
    key(fd, sc, sgr, ENTER, 0.9)
    print("%-12s: %r" % (label, sc.line(CMDLINE_ROW)))
    for r in (20, 21, 22):
        print("  row %d raw: %r" % (r, sc.line(r)))


run("Select all", 1)
run("Copy", 0)
run("Clear", 3)
run("Paste", 2)

try:
    os.write(fd, b"\x03")
    os.close(fd)
except OSError:
    pass
