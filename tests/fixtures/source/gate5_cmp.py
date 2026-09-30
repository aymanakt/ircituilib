import sys, os, json, subprocess, re
sys.path.insert(0, '/tmp')
from gate5_locate import locate

PKG = '/home/devops/repos/ircit/packages/ircituilib'
INC = ['-Iinclude/ircitui', '-Iinclude', '-Isrc', '-Igenerated']

def astnames(path, lang, extra):
    src = open(os.path.join(PKG, path)).read()
    cmd = ['clang', '-x', lang, '-std=c11', '-fsyntax-only', '-Xclang', '-ast-dump=json',
           '-Xclang', '-ast-dump-filter='] + INC + extra + ['-']
    r = subprocess.run(cmd, cwd=PKG, input=src, capture_output=True, text=True)
    if r.returncode != 0:
        return None, r.stderr
    ast = json.loads(r.stdout)
    names = set()
    def walk(n):
        if not isinstance(n, dict): return
        if n.get('kind') == 'FunctionDecl' and n.get('name') and n.get('loc', {}).get('file') is None:
            names.add(n['name'])
        for c in (n.get('inner') or []):
            if isinstance(c, dict): walk(c)
    walk(ast)
    return names, None

for path in ['src/ui_engine.c','src/ui_image.c','src/ui_theme.c','src/ui_verify.c','examples/ui_main.c','generated/ui_generated.c','include/ircituilib/ui.h']:
    src = open(os.path.join(PKG, path)).read()
    names, err = astnames(path, 'c', [])
    if names is None:
        print(f"{path}: AST FAIL {err[:300]}"); continue
    locs = locate(src, names)
    got = {l['name'] for l in locs}
    missing = names - got
    print(f"--- {path}: clang={len(names)} located={len(locs)} missing={len(missing)}")
    for m in sorted(missing):
        # position
        idx = src.find(m)
        ln = src.count('\n', 0, idx) + 1 if idx >= 0 else -1
        line = src.splitlines()[ln-1] if ln > 0 else ''
        print(f"      MISS {m}  line~{ln}: {line.strip()[:100]}")
