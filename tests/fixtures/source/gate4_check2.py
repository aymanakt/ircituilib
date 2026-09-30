import re, glob

def read(p): return open(p, encoding='utf-8').read()
def strip_comments(s):
    s = re.sub(r'/\*.*?\*/', ' ', s, flags=re.S)
    return re.sub(r'//[^\n]*', ' ', s)
def nocode(s):
    return re.sub(r'#\s*include[^\n]*', '', strip_comments(s))

def typedefs(text):
    body = nocode(text); out = {}
    pat = r'typedef\s+(enum|struct)\s*([A-Za-z_][A-Za-z0-9_]*)?\s*\{(.*?)\}\s*([A-Za-z_][A-Za-z0-9_]*)\s*;'
    for m in re.finditer(pat, body, flags=re.S):
        tag = m.group(2) or '(anon)'
        out[m.group(4)] = (m.group(1), tag, ' '.join(m.group(3).split()))
    for m in re.finditer(r'typedef\s+struct\s+([A-Za-z_][A-Za-z0-9_]*)\s+([A-Za-z_][A-Za-z0-9_]*)\s*;', body):
        out[m.group(2)] = ('opaque', m.group(1), '')
    for m in re.finditer(r'typedef\s+void\s*\(\s*\*\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)\s*\(([^)]*)\)\s*;', body):
        out[m.group(1)] = ('fnptr', '-', ' '.join(m.group(2).split()))
    return out

old = read('/tmp/ui_h_old.h')
hdrs = sorted(glob.glob('include/ircituilib/ircit_ui_*_type.h') + glob.glob('include/ircituilib/ircit_ui_*_enum.h'))
print("headers:", len(hdrs))
new = '\n'.join(read(h) for h in hdrs)

to, tn = typedefs(old), typedefs(new)
print("old:", len(to), "new:", len(tn))
print("MISSING:", sorted(set(to)-set(tn)), "EXTRA:", sorted(set(tn)-set(to)))
ok = True
for k in sorted(set(to) & set(tn)):
    a, b = to[k], tn[k]
    an = ' '.join(a[2].split()).replace('struct IrcitUiNode *','NODEPTR')
    bn = ' '.join(b[2].split()).replace('struct IrcitUiNode *','NODEPTR')
    if (a[0], a[1]) != (b[0], b[1]) or an != bn:
        ok = False
        print("DIFF", k)
        print("   old:", a[0], a[1], an)
        print("   new:", b[0], b[1], bn)
print("all type bodies+tags identical:", ok)

# anonymous flags enum body
fo = re.search(r'enum\s*\{(.*?)\}\s*;', old, flags=re.S)
fn = re.search(r'enum\s*\{(.*?)\}\s*;', read('include/ircituilib/ircit_ui_flags_enum.h'), flags=re.S)
print("flags enum identical:", ' '.join(fo.group(1).split()) == ' '.join(fn.group(1).split()))
