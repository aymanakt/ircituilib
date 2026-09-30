"""Continuation lines of *wrapped argument lists* only.

Qualifies when the enclosing '(' follows an identifier (an argument list) and
the previous non-blank line ends with a top-level ',' belonging to that list.
"""
import sys, re

def scan(text):
    lines = text.splitlines()
    stack = []          # (col_after_paren, is_arglist, prev_line_ended_with_comma)
    bad = []
    prev_ends_comma = False
    for i, ln in enumerate(lines):
        if stack and stack[-1][1] and prev_ends_comma:
            want, got = stack[-1][0], len(ln) - len(ln.lstrip())
            if ln.strip() and got != want:
                bad.append((i + 1, want, got, ln.strip()[:64]))
        ends_comma = False
        j, n = 0, len(ln)
        while j < n:
            if ln[j:j+2] == '/*':
                j = ln.find('*/', j + 2); j = n if j < 0 else j + 2; continue
            if ln[j:j+2] == '//': break
            c = ln[j]
            if c in '"\'':
                q = c; j += 1
                while j < n:
                    if ln[j] == '\\': j += 2; continue
                    if ln[j] == q: j += 1; break
                    j += 1
                continue
            if c == '(':
                k = j - 1
                while k >= 0 and ln[k] == ' ': k -= 1
                arg = k >= 0 and (ln[k].isalnum() or ln[k] == '_')
                if arg:
                    m = k
                    while m >= 0 and (ln[m].isalnum() or ln[m] == '_'): m -= 1
                    if ln[m+1:k+1] in ('if','while','for','switch','return','sizeof'): arg = False
                stack.append([j + 1, arg])
            elif c == ')':
                if stack: stack.pop()
            elif c == ',' and stack:
                ends_comma = True
            j += 1
        prev_ends_comma = ends_comma
    return bad

if __name__ == '__main__':
    import sys
    for p in sys.argv[1:]:
        b = scan(open(p).read())
        print(f"{p}: {len(b)}")
        for x in b[:20]:
            print(f"    line {x[0]:5d} want {x[1]:3d} got {x[2]:3d}  {x[3]}")
