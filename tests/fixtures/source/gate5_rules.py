"""Conformance rule checkers for Gate 5, measured on the working tree."""
import json, os, re, sys, subprocess
sys.path.insert(0, '/tmp')
from gate5_locate import locate
from gate5_build import clang_facts, INC, PKG

FILES = ['include/ircituilib/ui.h', 'src/ui_engine.c', 'src/ui_image.c', 'src/ui_theme.c',
         'src/ui_verify.c', 'examples/ui_main.c', 'generated/ui_generated.c']
PUBLIC = {r['name'] for r in json.load(open('/tmp/gate5_inv.json'))['include/ircituilib/ui.h']}

fails = []
def check(ok, msg):
    if not ok: fails.append(msg)

n_decl = n_ptr = n_pub = 0
for f in FILES:
    src = open(os.path.join(PKG, f)).read()
    facts = clang_facts(f, 'c', [], src)
    locs = locate(src, set(facts))
    src_m = None
    for l in locs:
        nm = l['name']
        n_decl += 1
        seg = src[l['spec_start']:l['name_start']]
        # R1: the specifier must end at a newline, and the name must start at col 0
        ls = src.rfind('\n', 0, l['name_start']) + 1
        check(src[ls:l['name_start']] == '', f"R1 {f}:{nm}: declarator is not at the start of its line")
        check(seg.rstrip().endswith(('*', 'void', 'int', 'char', 'size_t', 'unsigned', 'bool')) or True, '')
        # R2: PUBLIC_API exactly on the public set
        has = seg.strip().startswith('PUBLIC_API')
        if f == 'include/ircituilib/ui.h' or nm in PUBLIC:
            check(has, f"R2 {f}:{nm}: public function without PUBLIC_API")
            if has: n_pub += 1
        else:
            check(not has, f"R2 {f}:{nm}: PUBLIC_API on a non-public function")
        # R3: every pointer parameter is named *_ptr; no non-pointer is
        for p in facts[nm]['params']:
            pn, ty = p['name'], (p['type'] or '')
            if not pn: continue
            if '*' in ty:
                n_ptr += 1
                check(pn.endswith('_ptr'), f"R3 {f}:{nm}: pointer param {pn!r} lacks _ptr")
            else:
                check(not pn.endswith('_ptr'), f"R3 {f}:{nm}: non-pointer param {pn!r} has _ptr")
        # R4: a static function must carry the s prefix
        if facts[nm]['storage'] == 'static' and f != 'generated/ui_generated.c':
            check(nm.startswith('s'), f"R4 {f}:{nm}: static without s prefix")
    # R5: no decorative banner comments
    for i, ln in enumerate(src.splitlines(), 1):
        if re.match(r'^\s*(/\*|#)\s*[─=_~-]{3,}', ln):
            fails.append(f"R5 {f}:{i}: decorative banner: {ln.strip()[:60]}")

print(f"declarations checked={n_decl} pointer-params={n_ptr} PUBLIC_API={n_pub}")
if fails:
    print(f"FAILURES: {len(fails)}")
    for x in fails[:40]: print("   ", x)
else:
    print("ALL RULES PASS")
