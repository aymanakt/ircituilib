import re, sys

ACS = {'l':'┌','m':'┐','k':'┐','x':'│','q':'─','t':'├','u':'┤','v':'┴','w':'┬','n':'┼',
       'j':'┘','0':'█','`':'◆','a':'▒','f':'°','g':'#','~':'·',',':'<','+':'→','.':'↓',
       '-':'↑',"'":'¯','p':'─','o':'⎺','s':'⎽','_':' '}

class Screen:
    def __init__(self, rows=35, cols=100):
        self.rows, self.cols = rows, cols
        self.buf = [[' ']*cols for _ in range(rows)]
        self.r = self.c = 0
        self.acs = False
    def put(self, ch):
        if self.c >= self.cols:
            self.c = 0; self.r += 1
            if self.r >= self.rows: self.scroll()
        if 0 <= self.r < self.rows and 0 <= self.c < self.cols:
            self.buf[self.r][self.c] = ch
        self.c += 1
    def scroll(self):
        self.buf.pop(0); self.buf.append([' ']*self.cols); self.r = self.rows-1
    def ri(self):
        if self.r > 0: self.r -= 1
        else: self.buf.insert(0, [' ']*self.cols); self.buf.pop()
    def feed(self, data):
        i = 0; n = len(data)
        while i < n:
            ch = data[i]
            if ch == '\x1b':
                m = re.match(r'\x1b\[([0-9;?]*)([a-zA-Z@`])', data[i:])
                if m:
                    self.csi(m.group(1), m.group(2)); i += m.end(); continue
                m = re.match(r'\x1b[()]([A-Z0-9])', data[i:])
                if m:
                    self.acs = (m.group(1) == '0'); i += m.end(); continue
                m = re.match(r'\x1b([A-Za-z=><78])', data[i:])
                if m:
                    g = m.group(1)
                    if g == 'D': self.put('\n')
                    elif g == 'M': self.ri()
                    elif g in '78': pass
                    elif g == 'c': self.__init__(self.rows, self.cols)
                    i += m.end(); continue
                i += 1; continue
            if ch == '\r': self.c = 0
            elif ch == '\n': self.r += 1
            elif ch == '\b': self.c = max(0, self.c-1)
            elif ch == '\t': self.c = (self.c//8+1)*8
            elif ch == '\x0f' or ch == '\x0e': pass
            else:
                self.put(ACS.get(ch, ch) if self.acs and ch in ACS else ch)
            i += 1
    def csi(self, params, final):
        p = [int(x) for x in params.split(';') if x.isdigit()] if params and params.strip('?') else []
        if final == 'H' or final == 'f':
            self.r = (p[0]-1) if len(p) > 0 else 0
            self.c = (p[1]-1) if len(p) > 1 else 0
        elif final == 'J':
            n = p[0] if p else 0
            if n >= 2:
                self.buf = [[' ']*self.cols for _ in range(self.rows)]
            elif n == 0:
                for cc in range(self.c, self.cols): self.buf[self.r][cc] = ' '
                for rr in range(self.r+1, self.rows): self.buf[rr] = [' ']*self.cols
        elif final == 'K':
            n = p[0] if p else 0
            if n == 0:
                for cc in range(self.c, self.cols): self.buf[self.r][cc] = ' '
            elif n == 1:
                for cc in range(0, self.c+1): self.buf[self.r][cc] = ' '
            else:
                self.buf[self.r] = [' ']*self.cols
        elif final == 'A': self.r = max(0, self.r-(p[0] if p else 1))
        elif final == 'B': self.r = min(self.rows-1, self.r+(p[0] if p else 1))
        elif final == 'C': self.c = min(self.cols-1, self.c+(p[0] if p else 1))
        elif final == 'D': self.c = max(0, self.c-(p[0] if p else 1))
        elif final == 'L':
            k = p[0] if p else 1
            for _ in range(k):
                self.buf.insert(self.r, [' ']*self.cols); self.buf.pop()
        elif final == 'M':
            k = p[0] if p else 1
            for _ in range(k):
                self.buf.pop(self.r); self.buf.append([' ']*self.cols)
    def dump(self):
        out = []
        for i, row in enumerate(self.buf):
            out.append(f"{i:02d}|" + ''.join(row).rstrip())
        return '\n'.join(out)

if __name__ == '__main__':
    s = Screen()
    s.feed(open(sys.argv[1], 'rb').read().decode('utf-8', 'replace'))
    print(s.dump())
