"""Measure the parameter-shadowing risk before renaming any parameter."""
import json, subprocess, os, sys
PKG='/home/devops/repos/ircit/packages/ircituilib'
INC=['-Iinclude/ircitui','-Iinclude','-Isrc','-Igenerated']
FILES=['src/ui_engine.c','src/ui_image.c','src/ui_theme.c','src/ui_verify.c',
       'examples/ui_main.c','generated/ui_generated.c']
tot_params=tot_ptr=tot_shadow=0
for f in FILES:
    src=open(os.path.join(PKG,f)).read()
    r=subprocess.run(['clang','-x','c','-std=c11','-fsyntax-only','-Xclang','-ast-dump=json',
        '-Xclang','-ast-dump-filter=']+INC+['-'],cwd=PKG,input=src,capture_output=True,text=True)
    ast=json.loads(r.stdout)
    print(f"--- {f}")
    def walk(n, in_fn):
        global tot_params,tot_ptr,tot_shadow
        if not isinstance(n,dict): return
        if n.get('kind')=='FunctionDecl' and 'includedFrom' not in (n.get('loc') or {}):
            params=[p for p in (n.get('inner') or []) if p.get('kind')=='ParmVarDecl']
            ptrs=[p for p in params if '*' in (p.get('type') or {}).get('qualType','')]
            locals_=set()
            for c in (n.get('inner') or []):
                if c.get('kind')!='CompoundStmt': continue
                def collect(x, depth=0):
                    if not isinstance(x,dict): return
                    if x.get('kind')=='VarDecl' and x.get('name'): locals_.add(x['name'])
                    for y in (x.get('inner') or []):
                        if isinstance(y,dict): collect(y, depth+1)
                collect(c)
            sh=[p['name'] for p in ptrs if p.get('name') in locals_]
            tot_params+=len(params); tot_ptr+=len(ptrs); tot_shadow+=len(sh)
            if sh: print(f"    SHADOW {n['name']}: {sh}  (params={[p.get('name') for p in params]})")
        for c in (n.get('inner') or []):
            if isinstance(c,dict): walk(c, in_fn)
    walk(ast, False)
print(f"TOTAL params={tot_params} pointer-params={tot_ptr} shadowed={tot_shadow}")
