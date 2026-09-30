#!/usr/bin/env python3
"""Function-level census between two C sources. Read-only analysis.
Extracts brace-matched bodies of top-level function definitions and reports
IDENTICAL / MODIFIED / MISSING. No claim without the diff behind it."""
import re, sys

def defs(path):
    src = open(path, encoding='utf-8', errors='replace').read()
    out = {}
    # a definition = start of line, has a name(, and a { before any ;
    pat = re.compile(r'^([A-Za-z_][A-Za-z0-9_ \t\*]*?)\b([A-Za-z_][A-Za-z0-9_]*)\s*\(', re.M)
    for m in pat.finditer(src):
        name = m.group(2)
        i = m.end() - 1
        depth = 0; j = i; seen = False
        while j < len(src):
            ch = src[j]
            if ch == '(': depth += 1
            elif ch == ')':
                depth -= 1
                if depth == 0:
                    k = j + 1
                    while k < len(src) and src[k] in ' \t\r\n': k += 1
                    if k < len(src) and src[k] == '{':
                        seen = True; break
                    if k < len(src) and src[k] == ';':
                        break
            j += 1
        if not seen: continue
        depth = 0; k = src.index('{', j)
        s = k
        while k < len(src):
            if src[k] == '{': depth += 1
            elif src[k] == '}':
                depth -= 1
                if depth == 0: break
            k += 1
        body = src[s:k+1]
        out.setdefault(name, body)
    return out

def norm(b):
    # collapse whitespace so formatting-only churn is not reported as behaviour
    return re.sub(r'\s+', ' ', b).strip()

base = defs(sys.argv[1]); new = defs(sys.argv[2])
ident, mod, miss, added = [], [], [], []
for n, b in sorted(base.items()):
    if n not in new: miss.append(n)
    elif norm(b) == norm(new[n]): ident.append(n)
    else: mod.append(n)
for n in sorted(new):
    if n not in base: added.append(n)

print("baseline functions found : %d" % len(base))
print("v3 functions found       : %d" % len(new))
print("IDENTICAL (whitespace-normalised) : %d" % len(ident))
print("MODIFIED                          : %d" % len(mod))
print("MISSING from v3                   : %d" % len(miss))
print("NEW in v3                         : %d" % len(added))
print()
print("=== MISSING (%d) ===" % len(miss))
for n in miss: print("  " + n)
print()
print("=== NEW in v3 (%d) ===" % len(added))
for n in added: print("  " + n)
print()
print("=== MODIFIED (%d) ===" % len(mod))
for n in mod:
    bl = len(base[n].splitlines()); nl = len(new[n].splitlines())
    print("  %-28s baseline %4d lines -> v3 %4d lines  (%+d)" % (n, bl, nl, nl-bl))
