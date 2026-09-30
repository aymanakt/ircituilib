"""Locate wrapped *argument lists* and check the continuation indent.

Signature of a wrapped argument list: a line that ends with a top-level ',' and
whose enclosing '(' was opened after a function name.  The continuation lines of
such a list are conventionally aligned to the column just after that '('.
"""
import sys

def scan(text):
    lines = text.splitlines()
    stack = []          # (col_after_paren, is_arglist)
    bad = []
    for i, ln in enumerate(lines):
        cont = any(s[1] for s in stack) and i > 0
        if cont:
            want = stack[-1][0]
            got = len(ln) - len(ln.lstrip())
            if ln.strip() and got != want:
                bad.append((i + 1, want, got, ln.strip()[:64]))
        j, n, prev_sig = 0, len(ln), None
        while j < n:
            c = ln[j]
            if ln[j:j+2] == '/*':
                j = ln.find('*/', j + 2); j = n if j < 0 else j + 2; continue
            if ln[j:j+2] == '//': break
            if c in '"\'':
                q = c; j += 1
                while j < n:
                    if ln[j] == '\\': j += 2; continue
                    if ln[j] == q: j += 1; break
                    j += 1
                prev_sig = 'lit'; continue
            if c == '(':
                # an argument list iff an identifier (not a keyword) precedes it
                k = j - 1
                while k >= 0 and ln[k] == ' ': k -= 1
                arglist = k >= 0 and (ln[k].isalnum() or ln[k] == '_')
                if arglist:
                    m = k
                    while m >= 0 and (ln[m].isalnum() or ln[m] == '_'): m -= 1
                    if ln[m+1:k+1] in ('if', 'while', 'for', 'switch', 'return', 'sizeof'):
                        arglist = False
                stack.append((j + 1, arglist))
            elif c == ')':
                if stack: stack.pop()
            j += 1
    return bad

for p in sys.argv[1:]:
    bad = scan(open(p).read())
    print(f"{p}: {len(bad)}")
    for x in bad[:15]:
        print(f"    line {x[0]:5d} want {x[1]:3d} got {x[2]:3d}  {x[3]}")
