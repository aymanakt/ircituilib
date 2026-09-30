import sys, os, json, subprocess, re
sys.path.insert(0, '/tmp')
from gate5_locate import locate

PKG = '/home/devops/repos/ircit/packages/ircituilib'
INC = ['-Iinclude/ircitui', '-Iinclude', '-Isrc', '-Igenerated']

TARGETS = [
    ('include/ircituilib/ui.h',       'c',  []),
    ('src/ui_engine.c',            'c',  []),
    ('src/ui_image.c',             'c',  []),
    ('src/ui_theme.c',             'c',  []),
    ('src/ui_verify.c',            'c',  []),
    ('examples/ui_main.c',         'c',  []),
    ('generated/ui_generated.c',   'c',  []),
    ('tests/main.cpp',             'c++',['-Itests','-Iinclude/ircitui']),
]

def facts(path, lang, extra):
    src = open(os.path.join(PKG, path)).read()
    cmd = ['clang','-x',lang,'-std='+('c11' if lang=='c' else 'c++17'),'-fsyntax-only',
           '-Xclang','-ast-dump=json','-Xclang','-ast-dump-filter=']+INC+extra+['-']
    r = subprocess.run(cmd, cwd=PKG, input=src, capture_output=True, text=True)
    if r.returncode: return src, None, r.stderr
    ast = json.loads(r.stdout)
    out = {}
    def walk(n):
        if not isinstance(n, dict): return
        if n.get('kind')=='FunctionDecl' and n.get('name'):
            loc = n.get('loc') or {}
            if 'includedFrom' not in loc:
                nm = n['name']
                body = any(c.get('kind')=='CompoundStmt' for c in (n.get('inner') or []))
                if nm not in out or body:
                    out[nm] = {
                        'storage': n.get('storageClass') or '',
                        'body': body,
                        'params': [{'name': p.get('name'),'type':(p.get('type') or {}).get('qualType')}
                                   for p in (n.get('inner') or []) if p.get('kind')=='ParmVarDecl'],
                    }
        for c in (n.get('inner') or []):
            if isinstance(c, dict): walk(c)
    walk(ast)
    return src, out, None

total = 0
for path, lang, extra in TARGETS:
    src, f, err = facts(path, lang, extra)
    if f is None:
        print(f"{path}: AST FAIL\n{err[:400]}"); continue
    locs = locate(src, set(f))
    got = {l['name'] for l in locs}
    miss = sorted(set(f) - got)
    extra_l = sorted(got - set(f))
    print(f"{path:30s} clang={len(f):4d} located={len(locs):4d} bodies={sum(1 for l in locs if l['is_def']):4d} "
          f"miss={len(miss)} extra={len(extra_l)}")
    for m in miss:  print(f"    MISS {m}")
    for m in extra_l: print(f"    EXTRA {m}")
    total += len(locs)
print("TOTAL located:", total)
