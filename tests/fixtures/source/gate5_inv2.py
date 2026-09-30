"""Gate 5 inventory: function facts from clang's AST, geometry derived here.

clang's loc/range offsets for a FunctionDecl are not consistent between a file
compiled as its own TU, a header parsed as its own TU, and a header reached
through an include. So only *facts* are taken from the AST (name, storage,
parameters and their types). Every source offset is re-derived by searching the
file text for the declaration, which is the thing under test anyway.
"""
import json, re, subprocess, shlex, os, sys

PKG = '/home/devops/repos/ircit/packages/ircituilib'
CC = json.load(open(os.path.join(PKG, 'build/conv-gate3-test/compile_commands.json')))

def ast_of(cmd, cwd):
    r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit("AST FAIL %s\n%s" % (' '.join(cmd), r.stderr[:600]))
    return json.loads(r.stdout)

def fdecls(ast):
    out = []
    def walk(n):
        if not isinstance(n, dict): return
        if n.get('kind') == 'FunctionDecl':
            out.append({
                'name': n.get('name') or '<anon>',
                'storage': n.get('storageClass') or '',
                'has_body': any(c.get('kind') == 'CompoundStmt' for c in (n.get('inner') or [])),
                'included_from': (n.get('loc', {}) or {}).get('includedFrom', {}).get('file')
                                 if isinstance(n.get('loc'), dict) else None,
                'params': [{'name': p.get('name'), 'type': (p.get('type') or {}).get('qualType')}
                           for p in (n.get('inner') or []) if p.get('kind') == 'ParmVarDecl'],
            })
        for c in (n.get('inner') or []):
            if isinstance(c, dict): walk(c)
    walk(ast)
    return out

jobs, inv = [], {}
for e in CC:
    src = e['file']
    rel = os.path.relpath(src, PKG)
    if not src.endswith(('.c', '.cpp')) or 'version.c' in src: continue
    if any(j[0] == rel for j in jobs): continue
    a = shlex.split(e['command']); o = []; i = 0
    while i < len(a):
        if a[i] == '-o': i += 2; continue
        if a[i] == '-c': i += 1; continue
        if a[i].endswith('.o'): i += 1; continue
        o.append(a[i]); i += 1
    jobs.append((rel, o + ['-fsyntax-only', '-Xclang', '-ast-dump=json'], e['directory']))

# ui.h has no TU of its own here: its declarations are read from src/ui_theme.c,
# where they appear with includedFrom == "src/ui_priv.h" (nothing else ui_priv.h
# pulls in is a C header, so that predicate selects ui.h's own declarations).
jobs.append(('include/ircituilib/ui.h',
             ['clang', '-std=c11', '-Iinclude', '-Isrc', '-fsyntax-only',
              '-Xclang', '-ast-dump=json', 'src/ui_theme.c'],
             PKG))

for rel, cmd, cwd in sorted(jobs):
    ast = ast_of(cmd, cwd)
    seen = {}
    for f in fdecls(ast):
        if rel == 'include/ircituilib/ui.h':
            if f['included_from'] != 'src/ui_priv.h': continue
            if f['storage']: continue
        else:
            if f['included_from'] is not None: continue
        key = f['name']
        if key in seen and not f['has_body']: continue   # keep the definition
        seen[key] = f
    inv[rel] = list(seen.values())
json.dump(inv, open('/tmp/gate5_inv2.json', 'w'), indent=1)
for rel, fl in sorted(inv.items()):
    print(f"{rel:38} fdecls={len(fl):3} with-body={sum(1 for f in fl if f['has_body']):3} static={sum(1 for f in fl if f['storage']=='static'):3}")
