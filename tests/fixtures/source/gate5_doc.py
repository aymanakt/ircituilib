"""Update doc tags and comment code-references to the renamed parameters.

The rename pairs are the ones recovered and type-checked against HEAD
(/tmp/gate5_pairs.json).  Only two shapes are rewritten, both unambiguous:
  - `@param[..] name` / `@p name` in the doc block attached to the declaration;
  - `name->` / `name.` occurring inside a comment within the declaration.
Bare prose that merely spells a parameter name is left alone.
"""
import json, os, re, sys

PKG = '/home/devops/repos/ircit/packages/ircituilib'
INV = json.load(open('/tmp/gate5_inv.json'))
PAIRS = json.load(open('/tmp/gate5_pairs.json'))
DRY = '--dry' in sys.argv


def mask(src):
    m = bytearray(len(src))
    i, n = 0, len(src)
    while i < n:
        if src[i:i+2] == '/*':
            j = src.find('*/', i + 2); j = n if j < 0 else min(j + 2, n)
            m[i:j] = b'\x02' * (j - i); i = j
        elif src[i:i+2] == '//':
            j = src.find('\n', i); j = n if j < 0 else j
            m[i:j] = b'\x02' * (j - i); i = j
        elif src[i] in '"\'':
            q = src[i]; j = i + 1
            while j < n:
                if src[j] == '\\': j += 2; continue
                if src[j] == q: j += 1; break
                j += 1
            m[i:j] = b'\x03' * (j - i); i = j
        else:
            m[i] = 1; i += 1
    return m


def attached_doc(src, spec_start):
    seg = src[spec_start:]
    s0 = spec_start + (len(seg) - len(seg.lstrip()))
    k = src.rfind('\n', 0, s0) + 1
    while k > 0:
        prev = src.rfind('\n', 0, k - 1) + 1
        text = src[prev:k]
        if text.strip() == '':
            k = prev
            continue
        if text.rstrip().endswith('*/'):
            d = src.rfind('/**', 0, k)
            if d != -1 and prev <= src.find('*/', d) < k:
                return d
        return None
    return None


def renames_for(path, fn):
    out = {}
    for k, v in PAIRS.items():
        f, n, old = k.split('|')
        if f == path and n == fn:
            out[old] = v[0]
    return out


tag_total = ref_total = 0
for path, recs in INV.items():
    src = open(os.path.join(PKG, path)).read()
    m = mask(src)
    edits = []
    for r in recs:
        rn = renames_for(path, r['name'])
        if not rn:
            continue
        alt = '|'.join(sorted(map(re.escape, rn), key=len, reverse=True))
        doc = attached_doc(src, r['spec_start'])
        if doc is not None:
            block = src[doc:r['spec_start']]
            for mm in re.finditer(r'(@param(?:\[[a-z,]+\])?\s+)(' + alt + r')\b', block):
                a = doc + mm.start(2)
                edits.append((a, a + len(mm.group(2)), rn[mm.group(2)], 'tag'))
            for mm in re.finditer(r'(@p\s+)(' + alt + r')\b', block):
                a = doc + mm.start(2)
                edits.append((a, a + len(mm.group(2)), rn[mm.group(2)], 'tag'))
        for mm in re.finditer(r'\b(' + alt + r')\s*(->|\.)', src[r['name_start']:r['decl_end']]):
            a = r['name_start'] + mm.start(1)
            if m[a] != 2:
                continue
            edits.append((a, a + len(mm.group(1)), rn[mm.group(1)], 'ref'))
    edits.sort()
    for i in range(1, len(edits)):
        if edits[i][0] < edits[i-1][1]:
            raise SystemExit(f"{path}: overlapping edits {edits[i-1]} / {edits[i]}")
    out, pos = [], 0
    for a, b, new, kind in edits:
        out.append(src[pos:a]); out.append(new); pos = b
    out.append(src[pos:])
    c = {}
    for e in edits:
        c[e[3]] = c.get(e[3], 0) + 1
    print(f"{path:30s} tags={c.get('tag',0):3d} comment-refs={c.get('ref',0):3d}")
    tag_total += c.get('tag', 0); ref_total += c.get('ref', 0)
    if not DRY and edits:
        open(os.path.join(PKG, path), 'w').write(''.join(out))
print(f"TOTAL tags={tag_total} comment-refs={ref_total}")
