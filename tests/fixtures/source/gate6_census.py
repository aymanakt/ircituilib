"""Doxygen census: what is missing, measured against HEAD's tree state."""
import json, os, re, subprocess, sys
sys.path.insert(0, '/tmp')
from gate5_locate import locate

PKG = '/home/devops/repos/ircit/packages/ircituilib'
INC = ['-Iinclude/ircitui', '-Iinclude', '-Isrc', '-Igenerated']


def clang_facts(path, src):
    r = subprocess.run(['clang', '-x', 'c', '-std=c11', '-fsyntax-only',
                        '-Xclang', '-ast-dump=json', '-Xclang', '-ast-dump-filter='] + INC + ['-'],
                       cwd=PKG, input=src, capture_output=True, text=True)
    if r.returncode:
        sys.exit(f"AST FAIL {path}:\n{r.stderr[:600]}")
    ast = json.loads(r.stdout)
    out = {}
    def walk(n):
        if not isinstance(n, dict): return
        if n.get('kind') == 'FunctionDecl' and n.get('name') and 'includedFrom' not in (n.get('loc') or {}):
            body = any(c.get('kind') == 'CompoundStmt' for c in (n.get('inner') or []))
            rec = {'body': body, 'storage': n.get('storageClass') or '',
                   'params': [p.get('name') for p in (n.get('inner') or []) if p.get('kind') == 'ParmVarDecl']}
            if n['name'] not in out or body: out[n['name']] = rec
        for c in (n.get('inner') or []):
            if isinstance(c, dict): walk(c)
    walk(ast)
    return out


def doc_block_above(src, spec_start):
    """The /** ... */ or /*! ... */ block immediately above, or ''."""
    s0 = spec_start + (len(src[spec_start:]) - len(src[spec_start:].lstrip()))
    ls = src.rfind('\n', 0, s0)
    k = ls
    while k > 0:
        prev = src.rfind('\n', 0, k - 1) + 1
        text = src[prev:k]
        if text.strip() == '':
            k = prev
            continue
        if text.rstrip().endswith('*/'):
            d = max(src.rfind('/**', 0, k), src.rfind('/*!', 0, k))
            if d != -1 and prev <= src.find('*/', d) < k:
                return src[d:src.find('*/', d) + 2]
        return ''
    return ''


# --- 1. @file / @brief -----------------------------------------------------
print("== @file / @brief at the top of each file")
files = ['include/ircituilib/' + h for h in sorted(os.listdir(os.path.join(PKG, 'include/ircitui'))) if h.endswith('.h')]
files += ['src/ui_engine.c', 'src/ui_image.c', 'src/ui_theme.c', 'src/ui_verify.c',
          'src/ui_priv.h', 'examples/ui_main.c', 'generated/ui_generated.c', 'src/version.c.sh']
for f in files:
    p = os.path.join(PKG, f)
    if not os.path.exists(p):
        print(f"   ABSENT {f}"); continue
    s = open(p).read()
    if not (re.search(r'@file\b', s) and re.search(r'@brief\b', s)):
        print(f"   MISSING  {f}")

# --- 2. struct/enum doc marker and member docs ------------------------------
print("\n== struct / enum: /*! marker and ///< on every member")
for f in files:
    p = os.path.join(PKG, f)
    if not os.path.exists(p): continue
    s = open(p).read()
    for mm in re.finditer(r'typedef\s+(struct|union|enum)(\s+\w+)?\s*\{(.*?)\n\}\s*(\w+)\s*;', s, re.S):
        kind, body, name = mm.group(1), mm.group(3), mm.group(4)
        before = s[:mm.start()]
        b = before.rfind('*/')
        block = ''
        if b != -1 and before[b+2:].strip() == '':
            d = max(before.rfind('/**', 0, b), before.rfind('/*!', 0, b))
            if d != -1: block = before[d:b+2]
        marker = '/*!' if block.startswith('/*!') else ('/**' if block.startswith('/**') else 'NONE')
        miss, tot = [], 0
        for line in body.splitlines():
            t = line.strip()
            if not t or t.startswith('/*') or t.startswith('*') or t.startswith('//'): continue
            if t.endswith(';') or ',' in t:
                tot += 1
                if not re.search(r'/\*\*<|///<', line): miss.append(t[:58])
        ln = s.count('\n', 0, mm.start()) + 1
        flag = '' if (marker == '/*!' and not miss) else '  <-- '
        print(f"   {flag}{f}:{ln} {kind} {name} marker={marker} members={tot} undocumented={len(miss)}")
        for x in miss[:3]: print(f"          {x}")

# --- 3. public functions ----------------------------------------------------
print("\n== public functions in ui.h")
src = open(os.path.join(PKG, 'include/ircituilib/ui.h')).read()
facts = clang_facts('include/ircituilib/ui.h', src)
locs = locate(src, set(facts))
nb = np_ = nr = nc = 0
detail = []
for l in locs:
    nm = l['name']
    block = doc_block_above(src, l['spec_start'])
    seg = src[l['spec_start']:l['name_start']]
    ret = seg.replace('PUBLIC_API', '').strip()
    p = []
    if '@brief' not in block: nb += 1; p.append('@brief')
    for pn in facts[nm]['params']:
        if pn and not re.search(r'@param(?:\[[a-z,]+\])?\s+' + re.escape(pn) + r'\b', block):
            np_ += 1; p.append(f"@param {pn}")
    if not re.fullmatch(r'void\s*\*?', ret) and '@return' not in block:
        nr += 1; p.append('@return')
    if '@code' not in block: nc += 1; p.append('@code')
    if p: detail.append((nm, p))
print(f"   functions={len(locs)}  missing @brief={nb}  missing @param={np_}  missing @return={nr}  missing @code={nc}")
for nm, p in detail[:8]:
    print(f"      {nm}: {', '.join(p[:6])}")
