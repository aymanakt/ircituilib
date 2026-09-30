"""Build the Gate 5 inventory: clang supplies facts, the file text supplies geometry."""
import sys, os, json, subprocess, re
sys.path.insert(0, '/tmp')
from gate5_locate import locate

PKG = '/home/devops/repos/ircit/packages/ircituilib'
INC = ['-Iinclude/ircitui', '-Iinclude', '-Isrc', '-Igenerated', '-Ithird_party']

TARGETS = [
    ('include/ircituilib/ui.h',     'c',   []),
    ('src/ui_engine.c',          'c',   []),
    ('src/ui_image.c',           'c',   []),
    ('src/ui_theme.c',           'c',   []),
    ('src/ui_verify.c',          'c',   []),
    ('examples/ui_main.c',       'c',   []),
    ('generated/ui_generated.c', 'c',   []),
]

def clang_facts(path, lang, extra, src):
    cmd = ['clang', '-x', lang, '-std=' + ('c11' if lang == 'c' else 'c++17'),
           '-fsyntax-only', '-Xclang', '-ast-dump=json', '-Xclang', '-ast-dump-filter='] \
          + INC + extra + ['-']
    r = subprocess.run(cmd, cwd=PKG, input=src, capture_output=True, text=True)
    if r.returncode:
        sys.exit(f"AST FAIL {path}:\n{r.stderr[:1200]}")
    ast = json.loads(r.stdout)
    out = {}
    def walk(n):
        if not isinstance(n, dict): return
        if n.get('kind') == 'FunctionDecl' and n.get('name') and 'includedFrom' not in (n.get('loc') or {}):
            nm = n['name']
            body = any(c.get('kind') == 'CompoundStmt' for c in (n.get('inner') or []))
            is_static = n.get('storageClass') == 'static'
            rec = {'storage': n.get('storageClass') or '', 'body': body, 'is_static': is_static,
                   'variadic': any(c.get('kind') == 'Variadic' for c in (n.get('inner') or [])),
                   'params': [{'name': p.get('name'), 'type': (p.get('type') or {}).get('qualType')}
                              for p in (n.get('inner') or []) if p.get('kind') == 'ParmVarDecl']}
            if nm not in out or body: out[nm] = rec
        for c in (n.get('inner') or []):
            if isinstance(c, dict): walk(c)
    walk(ast)
    return out

inv = {}
summary = []
for path, lang, extra in TARGETS:
    src = open(os.path.join(PKG, path)).read()
    facts = clang_facts(path, lang, extra, src)
    locs = locate(src, set(facts))
    seen, recs = {}, []
    for l in locs:
        nm = l['name']
        f = facts[nm]
        line = src.count('\n', 0, l['spec_start']) + 1
        # column of the declaration-specifier start, on that line
        ls = src.rfind('\n', 0, l['spec_start']) + 1
        indent_col = l['spec_start'] - ls
        rec = {
            'file': path, 'name': nm, 'line': line,
            'spec_start': l['spec_start'], 'name_start': l['name_start'],
            'decl_end': l['decl_end'], 'is_def': l['is_def'],
            'indent_col': indent_col,
            'spec_text': src[l['spec_start']:l['name_start']],
            'is_static': f['is_static'], 'variadic': f['variadic'],
            'params': f['params'],
        }
        if nm in seen:
            seen[nm] += 1
            rec['dup'] = seen[nm]
        else:
            seen[nm] = 1
        recs.append(rec)
    inv[path] = recs
    summary.append((path, len(recs), len(seen),
                    sum(1 for r in recs if r['is_def']), sum(1 for r in recs if r['is_static'])))

json.dump(inv, open('/tmp/gate5_inv.json', 'w'), indent=1)
print(f"{'file':30s} {'decls':>6s} {'names':>6s} {'bodies':>7s} {'static':>7s}")
for p, n, u, b, s in summary:
    print(f"{p:30s} {n:6d} {u:6d} {b:7d} {s:7d}")
print(f"{'TOTAL':30s} {sum(x[1] for x in summary):6d} {sum(x[2] for x in summary):6d} "
      f"{sum(x[3] for x in summary):7d} {sum(x[4] for x in summary):7d}")
