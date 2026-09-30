import json, subprocess, os
PKG='/home/devops/repos/ircit/packages/ircituilib'
INC=['-Iinclude/ircitui','-Iinclude','-Isrc','-Igenerated']
inv=json.load(open('/tmp/gate5_inv.json'))
tp=tpp=tsh=0
for f,recs in inv.items():
    names={r['name'] for r in recs if r['file']==f and not r['file'].endswith('ui.h')}
    if not names: continue
    src=open(os.path.join(PKG,f)).read()
    r=subprocess.run(['clang','-x','c','-std=c11','-fsyntax-only','-Xclang','-ast-dump=json',
        '-Xclang','-ast-dump-filter=']+INC+['-'],cwd=PKG,input=src,capture_output=True,text=True)
    ast=json.loads(r.stdout)
    def walk(n):
        global tp,tpp,tsh
        if not isinstance(n,dict): return
        if n.get('kind')=='FunctionDecl' and n.get('name') in names:
            params=[p for p in (n.get('inner') or []) if p.get('kind')=='ParmVarDecl']
            ptrs=[p for p in params if '*' in (p.get('type') or {}).get('qualType','')]
            locals_=set()
            for c in (n.get('inner') or []):
                if c.get('kind')!='CompoundStmt': continue
                def collect(x):
                    if not isinstance(x,dict): return
                    if x.get('kind')=='VarDecl' and x.get('name'): locals_.add(x['name'])
                    for y in (x.get('inner') or []):
                        if isinstance(y,dict): collect(y)
                collect(c)
            sh=[p['name'] for p in ptrs if p.get('name') in locals_]
            tp+=len(params); tpp+=len(ptrs); tsh+=len(sh)
            if sh: print(f"  SHADOW {f}:{n['name']} {sh}")
        for c in (n.get('inner') or []):
            if isinstance(c,dict): walk(c)
    walk(ast)
print(f"params={tp} pointer-params={tpp} shadowed={tsh}")
