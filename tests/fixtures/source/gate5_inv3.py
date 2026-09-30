import sys, os, json, subprocess, re
sys.path.insert(0, '/tmp')
from gate5_locate import locate, ast_facts

PKG = '/home/devops/repos/ircit/packages/ircituilib'
INC = ['-Iinclude/ircitui', '-Iinclude', '-Isrc', '-Igenerated']
WARN = ['-Wall', '-Wextra']

TARGETS = [
    ('include/ircituilib/ui.h',              'c',  []),
    ('src/ui_engine.c',                   'c',  []),
    ('src/ui_image.c',                    'c',  []),
    ('src/ui_theme.c',                    'c',  []),
    ('src/ui_verify.c',                   'c',  []),
    ('examples/ui_main.c',                'c',  []),
    ('generated/ui_generated.c',          'c',  []),
    ('tests/main.cpp',                    'c++',['-Itests', '-isystem', '/usr/include']),
]

def clang_ast(path, lang, extra):
    src = open(os.path.join(PKG, path)).read()
    cmd = ['clang', '-x', lang, '-std=' + ('c11' if lang=='c' else 'c++17'),
           '-fsyntax-only', '-Xclang', '-ast-dump=json', '-Xclang', '-ast-dump-filter=',
           ] + INC + WARN + extra + ['-']
    r = subprocess.run(cmd, cwd=PKG, input=src, capture_output=True, text=True)
    return r

def main():
    for path, lang, extra in TARGETS:
        src = open(os.path.join(PKG, path)).read()
        r = clang_ast(path, lang, extra)
        if r.returncode != 0:
            print(f"{path}: AST FAIL rc={r.returncode}\n{r.stderr[:800]}"); continue
        try:
            ast = json.loads(r.stdout)
        except Exception as e:
            print(f"{path}: JSON FAIL {e}"); continue
        names = set()
        def walk(n):
            if not isinstance(n, dict): return
            if n.get('kind') == 'FunctionDecl' and n.get('name'): names.add(n['name'])
            for c in (n.get('inner') or []):
                if isinstance(c, dict): walk(c)
        walk(ast)
        locs = locate(src, names)
        defs = [l for l in locs if l['is_def']]
        print(f"{path:38s} fdecls(located)={len(locs):4d} with-body={len(defs):4d} "
              f"static={sum(1 for l in locs if re.search(r'\bstatic\b', src[l['spec_start']:l['name_start']])):4d}")
        for l in locs:
            if l['is_def']:
                print(f"    BODY {path}:{src.count(chr(10),0,l['spec_start'])+1}  {l['name']}")
main()
