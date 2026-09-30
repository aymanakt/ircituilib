"""Token-delta classification.

Every token difference between HEAD (Gate 4) and the working tree must be one of:
  - PUBLIC_API inserted
  - a declared parameter rename (old name -> new name), 1:1 and in place
Anything else is an unintended edit and is reported as a failure.
"""
import subprocess, sys, difflib, json, os, re
sys.path.insert(0, '/tmp')
from gate5_locate import tokenize

PKG = '/home/devops/repos/ircit/packages/ircituilib'
FILES = ['include/ircituilib/ui.h', 'src/ui_engine.c', 'src/ui_image.c', 'src/ui_theme.c',
         'src/ui_verify.c', 'examples/ui_main.c', 'generated/ui_generated.c']

# the verified rename set: recovered from HEAD and type-checked positionally
PAIRS = json.load(open('/tmp/gate5_pairs.json'))
renames = {}
for k, v in PAIRS.items():
    _, _, old = k.split('|')
    renames.setdefault(old, set()).update(v)

ok = True
for f in FILES:
    old = subprocess.run(['git', 'show', f'HEAD:./{f}'], cwd=PKG,
                         capture_output=True, text=True).stdout
    new = open(os.path.join(PKG, f)).read()
    def toks(s):
        return [(k, s[a:b]) for (k, a, b) in tokenize(s)
                if k in ('ident', 'number', 'str', 'chr', 'punct', 'preproc')]
    O, N = toks(old), toks(new)
    sm = difflib.SequenceMatcher(a=[t[1] for t in O], b=[t[1] for t in N], autojunk=False)
    bad, stats = [], {}
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag == 'equal':
            continue
        if tag == 'insert':
            for t in N[j1:j2]:
                if t[1] == 'PUBLIC_API':
                    stats['PUBLIC_API'] = stats.get('PUBLIC_API', 0) + 1
                else:
                    bad.append(('insert', t))
        elif tag == 'delete':
            for t in O[i1:i2]:
                bad.append(('delete', t))
        else:
            if (i2 - i1) != (j2 - j1):
                bad.append(('replace-len', O[i1:i2], N[j1:j2])); continue
            for a, b in zip(O[i1:i2], N[j1:j2]):
                if a[1] == b[1]:
                    continue
                if b[1] in renames.get(a[1], ()):
                    stats['rename'] = stats.get('rename', 0) + 1
                else:
                    bad.append(('rename?', a, b))
    status = 'OK' if not bad else f'FAIL ({len(bad)})'
    print(f"{f:30s} {status:14s} {stats}")
    for x in bad[:10]:
        print("      ", x)
    ok = ok and not bad
print("VERDICT:", "PASS" if ok else "FAIL")
