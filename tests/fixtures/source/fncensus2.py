#!/usr/bin/env python3
"""Rigorous census: collapse v3's composed-state prefixes (->common. / ->container.
/ ->interaction.) down to the flat baseline field names, then compare. Any function
that becomes IDENTICAL is a pure rename (no behaviour change). Whatever is left is
the true behavioural worklist."""
import re, sys
def defs(path, collapse):
    src = open(path, encoding='utf-8', errors='replace').read()
    if collapse:
        src = re.sub(r'->(?:common|container|interaction)\.', '->', src)
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
n = lambda b: re.sub(r'\s+', ' ', b).strip()
base = defs(sys.argv[1], False); new = defs(sys.argv[2], True)
ident, mod = [], []
for k, b in sorted(base.items()):
    if k not in new: continue
    (ident if n(b) == n(new[k]) else mod).append(k)
print("PURE RENAME (identical after collapsing the state prefixes): %d" % len(ident))
print("STILL DIFFERENT (true behavioural delta)                  : %d" % len(mod))
print()
for k in mod: print("  " + k)
