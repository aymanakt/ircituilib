#!/usr/bin/env python3
import re, sys
def defs(path):
    src = open(path, encoding='utf-8', errors='replace').read()
    out = {}
    pat = re.compile(r'^([A-Za-z_][A-Za-z0-9_ \t\*]*?)\b([A-Za-z_][A-Za-z0-9_]*)\s*\(', re.M)
    for m in pat.finditer(src):
        name = m.group(2); i = m.end() - 1; depth = 0; j = i; seen = False
        while j < len(src):
            ch = src[j]
            if ch == '(': depth += 1
            elif ch == ')':
                depth -= 1
                if depth == 0:
                    k = j + 1
                    while k < len(src) and src[k] in ' \t\r\n': k += 1
                    if k < len(src) and src[k] == '{': seen = True; break
                    if k < len(src) and src[k] == ';': break
            j += 1
        if not seen: continue
        depth = 0; k = src.index('{', j); s = k
        while k < len(src):
            if src[k] == '{': depth += 1
            elif src[k] == '}':
                depth -= 1
                if depth == 0: break
            k += 1
        out.setdefault(name, src[s:k+1])
    return out
f = defs(sys.argv[1])
print(f.get(sys.argv[2], "/* NOT FOUND */"))
