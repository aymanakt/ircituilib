#!/usr/bin/env python3
"""Gate 5 formatter for ircituilib.

Three transforms, all driven by clang's AST (never by regex guesswork):
  T1  declaration specifiers (storage class / PUBLIC_API / return type) on their
      own line, the declarator on the next line.
  T2  PUBLIC_API on the 62 public declarations in include/ircituilib/ui.h.
  T3  `_ptr` suffix on every pointer parameter, with a fixed stem table for
      single-letter names.

Usage: gate5_fmt.py [--dry]
"""
import json, os, re, sys

PKG = '/home/devops/repos/ircit/packages/ircituilib'
DRY = '--dry' in sys.argv
PUBLIC_HEADER = 'include/ircituilib/ui.h'

# Single-letter pointer parameter -> stem. Reviewed as a table, not inferred.
STEM = {
    'IrcitUiNode': 'node', 'IrcitUiTheme': 'theme', 'IrcitUiWidgetDef': 'def',
    'IrcitUiThemeRule': 'rule', 'IrcitUiImage': 'image', 'WINDOW': 'win',
    'char': 'str', 'unsigned char': 'bytes',
}
KEEP_LETTER = ('int', 'size_t', 'long', 'unsigned int')

def code_mask(text):
    """1 = code, 2 = comment, 3 = string/char literal."""
    n = len(text); st = bytearray(b'\x01') * n; i = 0
    while i < n:
        c = text[i]
        if c == '/' and i + 1 < n and text[i+1] == '*':
            j = text.find('*/', i + 2); j = n if j < 0 else j + 2
            st[i:j] = b'\x02' * (j - i); i = j
        elif c == '/' and i + 1 < n and text[i+1] == '/':
            j = text.find('\n', i); j = n if j < 0 else j
            st[i:j] = b'\x02' * (j - i); i = j
        elif c == '"':
            j = i + 1
            while j < n:
                if text[j] == '\\': j += 2; continue
                if text[j] == '"': j += 1; break
                j += 1
            st[i:j] = b'\x03' * (j - i); i = j
        elif c == "'":
            j = i + 1
            while j < n:
                if text[j] == '\\': j += 2; continue
                if text[j] == "'": j += 1; break
                j += 1
            st[i:j] = b'\x03' * (j - i); i = j
        else:
            i += 1
    return st

def stem_for(pname, ptype):
    if len(pname) >= 2:
        return pname
    base = ptype.replace('const ', '').replace('struct ', '').rstrip(' *')
    if base in KEEP_LETTER:
        return pname
    return STEM.get(base, pname)

def doc_block_before(text, off):
    """Return (start, end) of the doc comment immediately preceding off, or None."""
    k = text.rfind('*/', 0, off)
    if k < 0: return None
    if text[k+2:off].strip(): return None
    j = text.rfind('/*', 0, k)
    if j < 0: return None
    return (j, k + 2)

def main():
    inv = json.load(open('/tmp/gate5_inv.json'))
    report = []
    warnings = []
    for rel in sorted(inv):
        if rel.startswith('generated/'):
            report.append((rel + '  [SKIPPED - generator owns it]', len(inv[rel]), 0))
            continue
        path = os.path.join(PKG, rel)
        text = open(path, encoding='utf-8').read()
        mask = code_mask(text)
        fns = inv[rel]

        # canonical parameter names, positionally from the declaration when the
        # declaration is in the public header, else from the function itself.
        canon = {}
        for f in fns:
            names, used = [], set()
            for p in f['params']:
                if not p['name']:
                    names.append(None); continue
                pol = p['type'] and '*' in p['type']
                nm = p['name']
                if pol and not nm.endswith('_ptr'):
                    st = stem_for(nm, p['type'])
                    cand = st + '_ptr'
                    if cand in used or (len(nm) >= 2 and cand == nm):
                        cand = nm + '_ptr'
                    nm = cand
                used.add(nm); names.append(nm)
            canon[(f['name'], f['b']['offset'])] = names

        # public-header canonical names, keyed by name, to align definitions
        header_names = {}
        if rel == PUBLIC_HEADER:
            for f in fns:
                header_names[f['name']] = canon[(f['name'], f['b']['offset'])]

        edits = []
        for f in fns:
            names = canon[(f['name'], f['b']['offset'])]
            if rel != PUBLIC_HEADER and f['name'] in header_names and not f['storage']:
                hn = header_names[f['name']]
                if len(hn) == len(names):
                    types_ok = all(
                        (p['type'] and '*' in p['type']) == (q['type'] and '*' in q['type'])
                        for p, q in zip(f['params'],
                                        [x for x in inv[PUBLIC_HEADER] if x['name'] == f['name']][0]['params']))
                    if types_ok:
                        names = hn
            # T1: declaration specifiers on their own line, declarator on the
            # next. Anchored on the declarator name (reliable) and derived by
            # scanning back to the start of the line, because clang reports
            # range.begin inconsistently for a header parsed as its own TU.
            b, l = f['b']['offset'], f['l']['offset']
            ls = text.rfind('\n', 0, l) + 1
            ss = ls
            while ss < l and text[ss] in ' \t':
                ss += 1
            if ss < l:
                spec = text[ss:l].rstrip()
                if rel == PUBLIC_HEADER:
                    spec = 'PUBLIC_API ' + spec
                edits.append((ss, l, spec + '\n' + ' ' * (l - ls), 'T1', f['name']))
            elif rel == PUBLIC_HEADER:
                prev = text.rfind('\n', 0, max(0, ls - 1)) + 1
                while prev < ls and text[prev] in ' \t':
                    prev += 1
                edits.append((prev, prev, 'PUBLIC_API ', 'T2', f['name']))
            # T3: parameter renames within the function's own extent
            for idx, p in enumerate(f['params']):
                if not p['name'] or names[idx] is None or p['name'] == names[idx]:
                    continue
                old, new = p['name'], names[idx]
                lo, hi = f['b']['offset'], f['e']['offset']
                pat = re.compile(r'\b' + re.escape(old) + r'\b')
                for m in pat.finditer(text, lo, hi):
                    if mask[m.start()] != 1:
                        continue
                    head = text[:m.start()].rstrip()
                    if head.endswith('.') or head.endswith('->'):
                        continue   # a struct member that happens to share the name
                    edits.append((m.start(), m.end(), new, 'T3', f['name']))
                # @param in the preceding doc block
                db = doc_block_before(text, ss)
                if db:
                    seg = text[db[0]:db[1]]
                    tag = re.compile(r'(@param(?:\[[a-z,]+\])?\s+|@p\s+)(' + re.escape(old) + r')\b')
                    for m in tag.finditer(seg):
                        edits.append((db[0] + m.start(2), db[0] + m.end(2), new, 'T4', f['name']))
                # a comment inside the body that names the parameter follows the
                # parameter. Only the `x->` / `x.` form is treated as a reference;
                # anything else is listed for a human instead of guessed at.
                for m in pat.finditer(text, lo, hi):
                    if mask[m.start()] != 2:
                        continue
                    tail = text[m.end():m.end()+2]
                    if tail.startswith(('->', '.')):
                        edits.append((m.start(), m.end(), new, 'T5', f['name']))
                    else:
                        ctx = text[max(0, m.start()-45):m.start()+len(old)+25].replace('\n', ' / ')
                        warnings.append(f"{rel}: comment in {f['name']} mentions '{old}' :: ...{ctx}...")

        # apply, descending
        edits.sort(key=lambda e: -e[0])
        seen_spans = set()
        for a, z, rep, tag, fn in edits:
            if any(a >= s and z <= t for s, t in seen_spans):
                continue
            seen_spans.add((a, z))
            text = text[:a] + rep + text[z:]
        if not DRY:
            open(path, 'w', encoding='utf-8').write(text)
        by = {}
        for e in edits: by[e[3]] = by.get(e[3], 0) + 1
        report.append((rel, len(fns), len(edits), ' '.join(f'{k}={v}' for k, v in sorted(by.items()))))

    for row in report:
        rel, nf, ne, by = (row + ('',))[:4]
        print(f"{rel:38} funcs={nf:3} edits={ne:4}  {by}")
    print("\n--- warnings (%d) ---" % len(warnings))
    for w in warnings:
        print("  ", w)

main()
