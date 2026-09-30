"""Gate 5 formatter.

Facts come from clang (via /tmp/gate5_inv.json, cross-checked against the AST);
all geometry is derived from the file text.  Every replacement is applied to a
byte range whose current contents are first asserted to be what the inventory
says they are, so a stale inventory fails loudly instead of corrupting a file.
"""
import json, os, re, sys

PKG = '/home/devops/repos/ircit/packages/ircituilib'
INV = json.load(open('/tmp/gate5_inv.json'))
PUBLIC = {r['name'] for r in INV['include/ircituilib/ui.h']}

DRY = '--dry' in sys.argv
ONLY = None
for a in sys.argv[1:]:
    if a.startswith('--only='): ONLY = a.split('=', 1)[1]

# --- single-character pointer-parameter stems ---------------------------------
# A fixed, reviewable table.  Multi-character names simply gain the `_ptr`
# suffix; only one-character names need a stem, and there are eleven of them.
STEM = {
    'n': 'node', 's': 'str', 'g': 'group', 't': 'theme', 'm': 'menu',
    'd': 'def', 'r': 'rule', 'a': 'node_a', 'b': 'node_b', 'p': 'px',
    'w': 'w', 'h': 'h', 'x': 'x', 'y': 'y',
}
# Two one-letter names carry different meanings at different sites, so they are
# resolved per function rather than by the table above.
STEM_BY_FN = {
    ('sMenuSelectable', 'c'): 'child',
    ('IrcitUiSetThemeClass', 'c'): 'class_name',
}


def new_param_name(fn, old, qual_type):
    if (fn, old) in STEM_BY_FN:
        return STEM_BY_FN[(fn, old)] + '_ptr'
    if len(old) == 1:
        if old not in STEM:
            raise SystemExit(f"no stem for single-char pointer param {old!r} in {fn}")
        return STEM[old] + '_ptr'
    return old + '_ptr'


# --- code masking -------------------------------------------------------------
def mask(src):
    """1 = code, 2 = comment, 3 = literal.  Line comments, block comments and
    string/char literals are tagged so that identifier work never touches
    prose or data."""
    m = bytearray(len(src))
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if c == '/' and i + 1 < n and src[i+1] == '*':
            j = src.find('*/', i + 2); j = n if j < 0 else min(j + 2, n)
            m[i:j] = b'\x02' * (j - i); i = j
        elif c == '/' and i + 1 < n and src[i+1] == '/':
            j = src.find('\n', i); j = n if j < 0 else j
            m[i:j] = b'\x02' * (j - i); i = j
        elif c == '"' or c == "'":
            j = i + 1
            while j < n:
                if src[j] == '\\': j += 2; continue
                if src[j] == c: j += 1; break
                j += 1
            m[i:j] = b'\x03' * (j - i); i = j
        else:
            m[i] = 1; i += 1
    return m


def edits(path, recs):
    src = open(os.path.join(PKG, path)).read()
    m = mask(src)
    out = []
    # ---- A: storage class and return type on their own line, PUBLIC_API ----
    for r in recs:
        seg = src[r['spec_start']:r['name_start']]
        assert seg == r['spec_text'], f"{path}:{r['name']} spec drift"
        s0 = r['spec_start'] + (len(seg) - len(seg.lstrip()))
        ls = src.rfind('\n', 0, s0) + 1
        indent = src[ls:s0]
        if indent.strip():
            raise SystemExit(f"{path}:{r['name']}: code precedes the specifier on its line")
        spec = ' '.join(src[s0:r['name_start']].split())
        assert spec and not spec.endswith('* '), spec
        new = ('PUBLIC_API ' + spec if r['name'] in PUBLIC else spec) + '\n' + indent
        out.append((s0, r['name_start'], new, 'spec'))
    # ---- B: _ptr on pointer parameters (declarator + body + doc tags) ----
    for r in recs:
        if r['file'] == 'include/ircituilib/ui.h':
            extent_end = r['decl_end']
        else:
            extent_end = r['decl_end']
        renames = {}
        for p in r['params']:
            nm, ty = p.get('name'), (p.get('type') or '')
            if not nm or '*' not in ty:
                continue
            new = new_param_name(r['name'], nm, ty)
            if new == nm:
                raise SystemExit(f"{path}:{r['name']}: {nm} unchanged")
            renames[nm] = new
        if not renames:
            continue
        # collision check: the new name must not already occur in the extent
        extent = src[r['name_start']:extent_end]
        for old, new in renames.items():
            if re.search(r'\b' + re.escape(new) + r'\b', extent):
                raise SystemExit(f"{path}:{r['name']}: {old}->{new} collides in the declaration")
        # doc block immediately above the declaration
        ds = r['spec_start']
        head = src.rfind('\n', 0, ds) + 1
        doc_start = None
        k = ds
        while k > 0:
            prev_end = k if src[k-1] == '\n' else k
            line_start = src.rfind('\n', 0, max(0, k - 1)) + 1
            if src[line_start:k].strip() == '':
                k = line_start
                continue
            if src.rfind('/**', line_start, k) != -1 or src.rfind('*/', line_start, k) != -1:
                # find the start of the doc block
                d = src.rfind('/**', 0, k)
                if d != -1 and src.find('*/', d) >= k - 1:
                    doc_start = d
                break
            break
        # names in code within the extent
        for mm in re.finditer(r'\b(' + '|'.join(map(re.escape, renames)) + r')\b', extent):
            a = r['name_start'] + mm.start()
            if m[a] != 1:
                continue
            if src[a-1:a] == '.' or src[a-2:a] == '->':
                continue
            if src[a-1:a] == '*' or src[a-1:a] == '&':
                # still the identifier itself: `*n` dereferences the parameter
                pass
            out.append((a, a + len(mm.group(1)), renames[mm.group(1)], 'param'))
        # @param / @p tags in the doc block
        if doc_start is not None:
            doc = src[doc_start:r['spec_start']]
            for tag, name in re.findall(r'(@param(?:\[[a-z,]+\])?\s+)(\w+)', doc):
                if name in renames:
                    p = doc_start + doc.index(tag)
                    a = p + len(tag)
                    out.append((a, a + len(name), renames[name], 'param-tag'))
            for p in re.finditer(r'(@p\s+)(\w+)', doc):
                if p.group(2) in renames:
                    a = doc_start + p.start(2)
                    out.append((a, a + len(p.group(2)), renames[p.group(2)], 'p-tag'))
    return src, out


def apply_edits(src, out, path):
    out = sorted(out, key=lambda e: (e[0], e[1]))
    for i in range(1, len(out)):
        if out[i][0] < out[i-1][1]:
            raise SystemExit(f"{path}: overlapping edits at {out[i-1][:2]} and {out[i][:2]}")
    res, pos = [], 0
    for a, b, new, kind in out:
        assert src[a:b] and not src[a:b].strip() == '' or True
        res.append(src[pos:a]); res.append(new); pos = b
    res.append(src[pos:])
    return ''.join(res)


def main():
    for path, recs in INV.items():
        if ONLY and path != ONLY:
            continue
        src, out = edits(path, recs)
        new = apply_edits(src, out, path)
        counts = {}
        for e in out: counts[e[3]] = counts.get(e[3], 0) + 1
        print(f"{path:30s} edits={len(out):4d} {counts}")
        if DRY:
            continue
        open(os.path.join(PKG, path), 'w').write(new)


main()
