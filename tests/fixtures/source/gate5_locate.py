"""Locate top-level function declarations/definitions by tokenising the file.

Only *facts* come from clang (which names are functions, their storage class and
their parameter names/types).  All source geometry -- where a declarator starts,
where the declarator name is, where the declarator ends -- is derived here, from
the file text, and then cross-checked against the AST facts.
"""
import json, re, subprocess, shlex, os, sys

PKG = '/home/devops/repos/ircit/packages/ircituilib'
KW = set('''if else for while do switch return sizeof case default break continue
            goto typedef struct union enum const volatile static extern inline
            register auto signed unsigned long short void char int float double
            _Bool restrict __restrict __restrict__ __inline __inline__ __attribute__
            __asm __asm__ __extension__ asm typeof __typeof__ _Noreturn'''.split())

def tokenize(src):
    """[(kind, start, end)] with kind in ident/number/punct/str/chr/comment/preproc."""
    toks = []
    i, n, at_line_start = 0, len(src), True
    while i < n:
        c = src[i]
        if c == '\\' and i + 1 < n and src[i+1] == '\n':
            i += 2; continue
        if c == '\n':
            at_line_start = True; i += 1; continue
        if c in ' \t\r':
            i += 1; continue
        if c == '/' and i + 1 < n and src[i+1] == '*':
            j = src.find('*/', i + 2); j = n if j < 0 else j + 2
            toks.append(('comment', i, j)); i = j; at_line_start = False; continue
        if c == '/' and i + 1 < n and src[i+1] == '/':
            j = src.find('\n', i); j = n if j < 0 else j
            toks.append(('comment', i, j)); i = j; at_line_start = False; continue
        if at_line_start and c == '#':
            j = i
            while j < n:
                k = src.find('\n', j)
                if k < 0: j = n; break
                if src[k-1] == '\\': j = k + 1; continue
                j = k; break
            toks.append(('preproc', i, j)); i = j; at_line_start = False; continue
        at_line_start = False
        if c == '"' or c == "'":
            j = i + 1
            while j < n:
                if src[j] == '\\': j += 2; continue
                if src[j] == c: j += 1; break
                j += 1
            toks.append(('str' if c == '"' else 'chr', i, j)); i = j; continue
        if c.isalpha() or c == '_':
            j = i + 1
            while j < n and (src[j].isalnum() or src[j] == '_'): j += 1
            toks.append(('ident', i, j)); i = j; continue
        if c.isdigit():
            j = i + 1
            while j < n and (src[j].isalnum() or src[j] in '._'): j += 1
            toks.append(('number', i, j)); i = j; continue
        two = src[i:i+2]
        if two in ('->', '++', '--', '<<', '>>', '<=', '>=', '==', '!=', '&&', '||',
                   '+=', '-=', '*=', '/=', '%=', '&=', '|=', '^=', '##'):
            toks.append(('punct', i, i+2)); i += 2; continue
        toks.append(('punct', i, i+1)); i += 1
    return toks

def locate(src, names):
    """[{name, spec_start, name_start, decl_end, is_def, params_text}] for top-level
    function declarators whose name is in `names`."""
    toks = tokenize(src)
    out = []
    brace = paren = 0
    transparent = 0
    stmt_start = 0            # char offset where the current declaration begins
    for idx, (kind, a, b) in enumerate(toks):
        if kind == 'comment' or kind == 'preproc':
            # A comment or a directive can never be part of a declaration
            # specifier, and prose routinely contains '=' ('<=', 'depth 0 = root').
            # Ending the pending slice here keeps that text out of the check.
            if brace == 0 and paren == 0: stmt_start = b
            continue
        if kind == 'punct':
            if src[a] == '{':
                # `extern "C" {` is a linkage block, not a scope that hides
                # declarations: treat its braces as transparent.
                prev2 = [t for t in toks[max(0,idx-2):idx]]
                if brace == 0 and paren == 0 and len(prev2) == 2 and \
                   src[prev2[0][1]:prev2[0][2]] == 'extern' and prev2[1][0] == 'str':
                    transparent += 1; continue
                if brace == 0 and paren == 0: stmt_start = b
                brace += 1; continue
            if src[a] == '}':
                if transparent and brace == 0:
                    transparent -= 1; continue
                brace -= 1
                if brace == 0: stmt_start = b
                continue
            if src[a] == '(':
                if brace == 0 and paren == 0 and kind == 'punct':
                    # IDENT immediately before '(' ?
                    if idx > 0 and toks[idx-1][0] == 'ident':
                        nm = src[toks[idx-1][1]:toks[idx-1][2]]
                        if nm not in KW and nm in names:
                            # matching ')'
                            d, j = 1, idx + 1
                            while j < len(toks) and d:
                                if toks[j][0] == 'punct' and src[toks[j][1]] == '(': d += 1
                                elif toks[j][0] == 'punct' and src[toks[j][1]] == ')': d -= 1
                                j += 1
                            close = toks[j-1]
                            nxt = toks[j] if j < len(toks) else None
                            is_def = nxt is not None and nxt[0] == 'punct' and src[nxt[1]] == '{'
                            is_decl = nxt is not None and nxt[0] == 'punct' and src[nxt[1]] == ';'
                            if is_def or is_decl:
                                spec = src[stmt_start:toks[idx-1][1]]
                                if '=' not in spec and not re.search(r',\s*$', spec):
                                    end = close[2]
                                    if is_def:
                                        d2, k = 1, j + 1
                                        while k < len(toks) and d2:
                                            if toks[k][0] == 'punct' and src[toks[k][1]] == '{': d2 += 1
                                            elif toks[k][0] == 'punct' and src[toks[k][1]] == '}': d2 -= 1
                                            k += 1
                                        end = toks[k-1][2]
                                    else:
                                        end = nxt[2]
                                    out.append({'name': nm, 'spec_start': stmt_start,
                                                'name_start': toks[idx-1][1], 'decl_end': end,
                                                'is_def': is_def, 'next': nxt})
                paren += 1; continue
            if src[a] == ')':
                paren -= 1; continue
            if src[a] == ';' and brace == 0 and paren == 0:
                stmt_start = b; continue
    return out

def ast_facts(path, cmd, cwd, keep):
    r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    if r.returncode != 0: sys.exit("AST FAIL " + r.stderr[:600])
    ast = json.loads(r.stdout)
    facts = {}
    def walk(n):
        if not isinstance(n, dict): return
        if n.get('kind') == 'FunctionDecl' and (n.get('name') or '') in keep:
            nm = n['name']
            if nm not in facts or any(c.get('kind') == 'CompoundStmt' for c in (n.get('inner') or [])):
                facts[nm] = {
                    'storage': n.get('storageClass') or '',
                    'params': [{'name': p.get('name'), 'type': (p.get('type') or {}).get('qualType')}
                               for p in (n.get('inner') or []) if p.get('kind') == 'ParmVarDecl'],
                    'body': any(c.get('kind') == 'CompoundStmt' for c in (n.get('inner') or [])),
                }
        for c in (n.get('inner') or []):
            if isinstance(c, dict): walk(c)
    walk(ast)
    return facts
