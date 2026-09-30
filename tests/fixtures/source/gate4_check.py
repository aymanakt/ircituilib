import re, subprocess, sys, glob, os

def read(p):
    return open(p, encoding='utf-8').read()

def strip_comments(s):
    s = re.sub(r'/\*.*?\*/', ' ', s, flags=re.S)
    s = re.sub(r'//[^\n]*', ' ', s)
    return s

def decls(text):
    """Function declarations: lines ending in ); that start a declaration."""
    body = strip_comments(text)
    body = re.sub(r'#\s*include[^\n]*', '', body)
    out = []
    for stmt in body.split(';'):
        st = ' '.join(stmt.split())
        if not st:
            continue
        if '(' in st and ')' in st and 'typedef' not in st and '#' not in st and 'extern' not in st and not st.startswith('}'):
            out.append(st)
    return sorted(out)

old = read('/tmp/ui_h_old.h')
new = read('include/ircituilib/ui.h')

od, nd = decls(old), decls(new)
print("OLD decls:", len(od), " NEW decls:", len(nd))
only_old = [x for x in od if x not in nd]
only_new = [x for x in nd if x not in od]
print("ONLY IN OLD:")
for x in only_old: print("   ", x)
print("ONLY IN NEW:")
for x in only_new: print("   ", x)

globals_old = sorted(re.findall(r'extern\s+[^;]+;', strip_comments(old)))
globals_new = sorted(re.findall(r'extern\s+[^;]+;', strip_comments(new)))
print("externs equal:", [' '.join(g.split()) for g in globals_old] == [' '.join(g.split()) for g in globals_new])

# --- type bodies: old ui.h vs the new type headers + new ui.h
headers = sorted(glob.glob('include/ircituilib/ircit_ui_*_type.h') + glob.glob('include/ircituilib/ircit_ui_*_enum.h'))
tnew = '\n'.join(read(h) for h in headers)

def typedefs(text):
    body = strip_comments(text)
    body = re.sub(r'#\s*include[^\n]*', '', body)
    out = {}
    for m in re.finditer(r'typedef\s+(enum\s*\{.*?\}|struct\s*\{.*?\})\s*([A-Za-z_][A-Za-z0-9_]*)\s*;', body, flags=re.S):
        out[m.group(2)] = ' '.join(m.group(1).split())
    for m in re.finditer(r'typedef\s+struct\s+([A-Za-z_][A-Za-z0-9_]*)\s+([A-Za-z_][A-Za-z0-9_]*)\s*;', body):
        out[m.group(2)] = 'opaque struct ' + m.group(1)
    for m in re.finditer(r'typedef\s+void\s*\(\s*\*\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)\s*\(([^)]*)\)\s*;', body):
        out[m.group(1)] = 'fnptr(' + ' '.join(m.group(2).split()) + ')'
    return out

to = typedefs(old)
tn = typedefs(tnew)
print("OLD typedefs:", sorted(to))
print("NEW typedefs:", sorted(tn))
missing = set(to) - set(tn)
extra   = set(tn) - set(to)
print("MISSING from new:", sorted(missing))
print("EXTRA in new:   ", sorted(extra))
for k in sorted(set(to) & set(tn)):
    a = ' '.join(to[k].split())
    b = ' '.join(tn[k].split())
    a = a.replace('struct IrcitUiNode *','NODEPTR').replace('struct IrcitUiNode*','NODEPTR')
    b = b.replace('struct IrcitUiNode *','NODEPTR').replace('struct IrcitUiNode*','NODEPTR')
    if a != b:
        print("BODY DIFF", k)
        print("   old:", a)
        print("   new:", b)

# anonymous flags enum
fo = re.search(r'enum\s*\{(.*?)\}\s*;', old, flags=re.S)
fn = re.search(r'enum\s*\{(.*?)\}\s*;', tnew if 'IRCITUI_F_VISIBLE' not in old else tnew, flags=re.S)
print("flags enum present in new headers:", 'IRCITUI_F_VISIBLE' in tnew)

# macros
mo = sorted(re.findall(r'#\s*define\s+(IRCITUI_[A-Z_]+)\s+([^\n]*)', strip_comments(old)))
mn = sorted(re.findall(r'#\s*define\s+(IRCITUI_[A-Z_]+)\s+([^\n]*)', strip_comments(new)))
print("macros old:", mo, " new:", mn)
