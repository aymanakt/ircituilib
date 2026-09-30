"""Realign wrapped-argument-list continuation lines to the enclosing '(' column.

Restricted to the files Gate 5 edits, and only to lines that qualify as
continuations of an argument list (enclosing '(' follows an identifier, and the
previous line ends with a ',' belonging to that list).  Whitespace only.
"""
import sys, os
sys.path.insert(0, '/tmp')
from gate5_align2 import scan

PKG = '/home/devops/repos/ircit/packages/ircituilib'
FILES = ['src/ui_engine.c', 'src/ui_verify.c', 'src/ui_image.c', 'src/ui_theme.c',
         'examples/ui_main.c', 'include/ircituilib/ui.h']
DRY = '--dry' in sys.argv

for f in FILES:
    p = os.path.join(PKG, f)
    src = open(p).read()
    bad = scan(src)
    if not bad:
        print(f"{f:30s} nothing to realign")
        continue
    lines = src.splitlines(keepends=True)
    for lineno, want, got, _ in bad:
        ln = lines[lineno - 1]
        assert len(ln) - len(ln.lstrip()) == got, (f, lineno)
        lines[lineno - 1] = ' ' * want + ln.lstrip(' ')
    new = ''.join(lines)
    print(f"{f:30s} realigned {len(bad)} line(s)")
    if not DRY:
        open(p, 'w').write(new)
