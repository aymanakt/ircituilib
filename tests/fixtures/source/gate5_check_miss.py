import sys, os, json, subprocess, re
sys.path.insert(0,'/tmp')
from gate5_locate import locate
PKG='/home/devops/repos/ircit/packages/ircituilib'
INC=['-Iinclude/ircitui','-Iinclude','-Isrc','-Igenerated']
for path in ['src/ui_engine.c','src/ui_image.c','src/ui_theme.c','src/ui_verify.c','examples/ui_main.c','generated/ui_generated.c']:
    src=open(os.path.join(PKG,path)).read()
    r=subprocess.run(['clang','-x','c','-std=c11','-fsyntax-only','-Xclang','-ast-dump=json',
        '-Xclang','-ast-dump-filter=']+INC+['-'],cwd=PKG,input=src,capture_output=True,text=True)
    ast=json.loads(r.stdout); out={}
    def walk(n):
        if not isinstance(n,dict): return
        if n.get('kind')=='FunctionDecl' and n.get('name') and 'includedFrom' not in (n.get('loc') or {}):
            out.setdefault(n['name'],[]).append(n)
        for c in (n.get('inner') or []):
            if isinstance(c,dict): walk(c)
    walk(ast)
    located={l['name'] for l in locate(src,set(out))}
    print(f"=== {path}  located={len(located)}")
    for nm in sorted(set(out)-located):
        # does the name occur in the file text as a word?
        if re.search(r'\b'+re.escape(nm)+r'\b', src):
            ln=[i+1 for i,l in enumerate(src.splitlines()) if re.search(r'\b'+re.escape(nm)+r'\b',l)]
            print(f"    TEXT-PRESENT {nm}  lines={ln[:5]}")
