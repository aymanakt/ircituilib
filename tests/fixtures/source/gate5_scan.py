import json, subprocess, shlex, os, sys

PKG = '/home/devops/repos/ircit/packages/ircituilib'
CC  = json.load(open(os.path.join(PKG, 'build/conv-gate3-test/compile_commands.json')))

def dump(cmd, cwd):
    r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit("AST FAIL: %s\n%s" % (' '.join(cmd), r.stderr[:800]))
    return json.loads(r.stdout)

def off(l):
    return l if isinstance(l, dict) and 'offset' in l else None

def collect(ast, main_only=True):
    out = []
    def walk(n):
        if not isinstance(n, dict): return
        if n.get('kind') == 'FunctionDecl':
            b = off(n.get('range', {}).get('begin'))
            e = off(n.get('range', {}).get('end'))
            l = off(n.get('loc'))
            if b and l and e:
                out.append({
                    'name': n.get('name') or '<anon>',
                    'storage': n.get('storageClass') or '',
                    'has_body': any(c.get('kind') == 'CompoundStmt' for c in (n.get('inner') or [])),
                    'b': b, 'e': e, 'l': l,
                    'params': [{'name': p.get('name'),
                                'type': p.get('type', {}).get('qualType'),
                                'loc': off(p.get('loc'))}
                               for p in (n.get('inner') or []) if p.get('kind') == 'ParmVarDecl'],
                })
        for c in (n.get('inner') or []):
            if isinstance(c, dict): walk(c)
    walk(ast)
    return out

jobs = []
for e in CC:
    src = e['file']
    rel = os.path.relpath(src, PKG)
    if not src.endswith(('.c', '.cpp')): continue
    if 'version.c' in src: continue
    if any(j[0] == rel for j in jobs): continue
    args = shlex.split(e['command'])
    out, i = [], 0
    while i < len(args):
        a = args[i]
        if a == '-o': i += 2; continue
        if a == '-c': i += 1; continue
        if a.endswith('.o'): i += 1; continue
        out.append(a); i += 1
    jobs.append((rel, out + ['-fsyntax-only', '-Xclang', '-ast-dump=json'], e['directory']))

# ui.h as its own main file
jobs.append(('include/ircituilib/ui.h',
             ['clang', '-x', 'c-header', '-std=c11', '-Iinclude', '-fsyntax-only',
              '-Xclang', '-ast-dump=json', 'include/ircituilib/ui.h'],
             PKG))

result = {}
for rel, cmd, cwd in sorted(jobs):
    ast = dump(cmd, cwd)
    fns = collect(ast)
    # keep only those whose begin is in the main file (no includedFrom, no file)
    keep = []
    for f in fns:
        b = f['b']
        if 'includedFrom' not in b and 'file' not in b:
            keep.append(f)
    result[rel] = keep
json.dump(result, open('/tmp/gate5_inv.json', 'w'), indent=1)
for rel, fl in sorted(result.items()):
    src = open(os.path.join(PKG, rel), encoding='utf-8').read()
    def line_of(o):
        return src.count('\n', 0, o) + 1
    for f in fl:
        f['bline'] = line_of(f['b']['offset']); f['bcol'] = f['b']['offset'] - src.rfind('\n', 0, f['b']['offset'])
        f['lline'] = line_of(f['l']['offset']); f['lcol'] = f['l']['offset'] - src.rfind('\n', 0, f['l']['offset'])
    json.dump(result, open('/tmp/gate5_inv.json', 'w'), indent=1)
    split = sum(1 for f in fl if f['bline'] == f['lline'])
    ptr = sum(1 for f in fl for p in f['params']
              if p['name'] and p['type'] and '*' in p['type'] and not p['name'].endswith('_ptr'))
    print(f"{rel:38} fdecls={len(fl):3} need-split={split:3} ptr-params-to-rename={ptr:3} bodies={sum(1 for f in fl if f['has_body']):3}")
