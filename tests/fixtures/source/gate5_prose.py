"""Report code comments that still name a parameter by its pre-rename name.

A comment inside a function whose parameter was renamed from X to Y is a
candidate: the prose may have meant the parameter.  Each hit is printed with
context so it can be judged as prose or as a stale reference."""
import json, os, re, sys
sys.path.insert(0, '/tmp')
import importlib.util as _iu
_s=_iu.spec_from_file_location('_g','/tmp/gate5_fmt2.py'); _m=_iu.module_from_spec(_s)
import sys as _sys; _sys.argv=['x','--dry']; _s.loader.exec_module(_m); mask=_m.mask
PKG = '/home/devops/repos/ircit/packages/ircituilib'
INV = json.load(open('/tmp/gate5_inv.json'))
for path, recs in INV.items():
    src = open(os.path.join(PKG, path)).read()
    m = mask(src)
    for r in recs:
        old = [p['name'] for p in r['params'] if p.get('name') and '*' in (p.get('type') or '')]
        if not old:
            continue
        for mm in re.finditer(r'\b(' + '|'.join(map(re.escape, old)) + r')\b', src[r['name_start']:r['decl_end']]):
            a = r['name_start'] + mm.start()
            if m[a] != 2:
                continue
            ls = src.rfind('\n', 0, a) + 1
            le = src.find('\n', a); le = len(src) if le < 0 else le
            print(f"{path}:{src.count(chr(10),0,a)+1} [{r['name']}] {src[ls:le].strip()[:110]}")
