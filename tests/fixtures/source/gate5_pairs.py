"""Recover the parameter rename pairs, verified positionally against HEAD.

For each declaration present in both HEAD and the working tree, the parameter
lists must have the same length and the same types in the same order; only the
names may differ.  Any other difference is reported as a failure.
"""
import json, os, subprocess, sys
sys.path.insert(0, '/tmp')
from gate5_locate import locate
from gate5_build import clang_facts, INC, PKG

FILES = ['include/ircituilib/ui.h', 'src/ui_engine.c', 'src/ui_image.c', 'src/ui_theme.c',
         'src/ui_verify.c', 'examples/ui_main.c', 'generated/ui_generated.c']

pairs = {}
report = []
for f in FILES:
    old = subprocess.run(['git', 'show', f'HEAD:./{f}'], cwd=PKG,
                         capture_output=True, text=True).stdout
    facts = clang_facts(f, 'c', [], old)
    locs = locate(old, set(facts))
    seen = {}
    for l in locs:
        nm = l['name']
        seen.setdefault(nm, l)
    cur = json.load(open('/tmp/gate5_inv.json'))[f]
    for r in cur:
        if r['name'] not in seen:
            report.append(f"{f}:{r['name']}: not found in HEAD")
            continue
        op = facts[r['name']]['params']
        np_ = r['params']
        if len(op) != len(np_):
            report.append(f"{f}:{r['name']}: arity {len(op)} -> {len(np_)}")
            continue
        for a, b in zip(op, np_):
            if (a['type'] or '').replace(' ', '') != (b['type'] or '').replace(' ', ''):
                report.append(f"{f}:{r['name']}: {a} -> {b} TYPE DRIFT")
                continue
            if a['name'] != b['name']:
                pairs.setdefault((f, r['name'], a['name']), set()).add(b['name'])
json.dump({f"{k[0]}|{k[1]}|{k[2]}": sorted(v) for k, v in pairs.items()},
          open('/tmp/gate5_pairs.json', 'w'), indent=1)
print(f"pairs recovered: {len(pairs)}   distinct old names: {len({k[2] for k in pairs})}")
amb = {k: v for k, v in pairs.items() if len(v) > 1}
print("ambiguous (one old name -> several new names):", len(amb))
for k, v in list(amb.items())[:10]:
    print("   ", k, "->", sorted(v))
print("problems:", len(report))
for x in report[:20]:
    print("   ", x)
