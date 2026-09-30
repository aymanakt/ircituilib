#!/usr/bin/env python3
"""A VT100 screen model good enough for ncurses output (CUP/CUU/CUD/CUF/CUB/
VPA/CHA/EL/ED/SGR/ACS)."""
import os, re, select, time


class Screen:
    def __init__(self, rows=24, cols=80):
        self.rows, self.cols = rows, cols
        self.g = [[" "] * cols for _ in range(rows)]
        self.cy = self.cx = 0

    def put(self, ch):
        if 0 <= self.cy < self.rows and 0 <= self.cx < self.cols:
            self.g[self.cy][self.cx] = ch
        self.cx += 1

    def feed(self, data):
        s = data.decode("utf-8", "replace")
        i = 0
        while i < len(s):
            c = s[i]
            if c == "\x1b":
                m = re.match(r"\x1b\[([0-9;?]*)([A-Za-z@`])", s[i:])
                if m:
                    params, cmd = m.group(1), m.group(2)
                    nums = [int(p) for p in params.split(";") if p.isdigit()] or [0]
                    if cmd in ("H", "f"):
                        self.cy = max(0, (nums[0] or 1) - 1)
                        self.cx = max(0, (nums[1] if len(nums) > 1 else 1) - 1)
                    elif cmd == "d":                      # VPA
                        self.cy = max(0, (nums[0] or 1) - 1)
                    elif cmd == "G":                      # CHA
                        self.cx = max(0, (nums[0] or 1) - 1)
                    elif cmd == "A":
                        self.cy = max(0, self.cy - (nums[0] or 1))
                    elif cmd == "B":
                        self.cy = min(self.rows - 1, self.cy + (nums[0] or 1))
                    elif cmd == "C":
                        self.cx = min(self.cols - 1, self.cx + (nums[0] or 1))
                    elif cmd == "D":
                        self.cx = max(0, self.cx - (nums[0] or 1))
                    elif cmd == "K":                      # EL
                        if nums[0] == 0:
                            for x in range(self.cx, self.cols):
                                self.g[self.cy][x] = " "
                        elif nums[0] == 1:
                            for x in range(0, min(self.cx + 1, self.cols)):
                                self.g[self.cy][x] = " "
                        else:
                            self.g[self.cy] = [" "] * self.cols
                    elif cmd == "J":                      # ED
                        if nums[0] == 0:
                            for x in range(self.cx, self.cols):
                                self.g[self.cy][x] = " "
                            for y in range(self.cy + 1, self.rows):
                                self.g[y] = [" "] * self.cols
                        elif nums[0] == 1:
                            for y in range(0, self.cy):
                                self.g[y] = [" "] * self.cols
                            for x in range(0, min(self.cx + 1, self.cols)):
                                self.g[self.cy][x] = " "
                        else:
                            self.g = [[" "] * self.cols for _ in range(self.rows)]
                    i += m.end()
                    continue
                m = (re.match(r"\x1b[()][0-9A-Za-z]", s[i:])
                     or re.match(r"\x1b\[[0-9;]*[hl]", s[i:])
                     or re.match(r"\x1b.", s[i:]))
                i += m.end() if m else 1
                continue
            if c == "\r":
                self.cx = 0
            elif c == "\n":
                self.cy = min(self.rows - 1, self.cy + 1)
            elif c == "\b":
                self.cx = max(0, self.cx - 1)
            elif c == "\t":
                self.cx = min(self.cols - 1, (self.cx // 8 + 1) * 8)
            elif c >= " ":
                self.put(c)
            i += 1

    def text(self):
        return "\n".join("".join(r).rstrip() for r in self.g)

    def find(self, needle):
        for y, row in enumerate(self.g):
            x = "".join(row).find(needle)
            if x >= 0:
                return (y + 1, x + 1)
        return None

    def nonblank(self):
        return sum(1 for r in self.g for ch in r if ch != " ")


def drain(fd, secs=1.0):
    out = b""
    end = time.time() + secs
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.1)
        if fd in r:
            try:
                c = os.read(fd, 65536)
            except OSError:
                break
            if not c:
                break
            out += c
    return out
