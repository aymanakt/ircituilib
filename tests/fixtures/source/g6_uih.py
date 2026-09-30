#!/usr/bin/env python3
"""Split include/ircituilib/ui.h into (doc-block, declaration) items so the
declarations can be re-emitted byte-identically while their doc blocks are
rewritten."""
import re
import sys

PATH = "include/ircituilib/ui.h"
OPEN = "#ifdef __cplusplus\nextern \"C\" {\n#endif\n"
CLOSE = "#ifdef __cplusplus\n}\n#endif\n"


def load():
    return open(PATH, encoding="utf-8").read()


def region(src):
    a = src.index(OPEN) + len(OPEN)
    b = src.index(CLOSE)
    return a, b


def split(src):
    """Return (head, items, tail) where items = [(doc_or_None, decl, gap_before)]."""
    a, b = region(src)
    head, body, tail = src[:a], src[a:b], src[b:]
    # find top-level declarations: a doc block (optional) then PUBLIC_API ... ';'
    toks = []
    i = 0
    n = len(body)
    starts = []
    while True:
        m = re.compile(r"^PUBLIC_API\b", re.M).search(body, i)
        if not m:
            break
        # walk back over an immediately-preceding doc block
        j = m.start()
        k = body.rfind("*/", 0, j)
        doc = None
        if k >= 0 and body[k + 2:j].strip() == "":
            d = body.rfind("/**", 0, k)
            if d >= 0:
                between = body[k + 2:d]
                if ";" not in between and "PUBLIC_API" not in between:
                    doc = (d, k + 2)
        end = body.index(";\n", m.start()) + 2
        starts.append((doc, m.start(), end))
        i = end
    items = []
    prev_end = 0
    for doc, s, e in starts:
        ds = doc[0] if doc else s
        gap = body[prev_end:ds]
        items.append((body[ds:s] if doc else None, body[s:e], gap))
        prev_end = e
    return head, body, tail, items, prev_end


def params_of(decl):
    """Parameter (name, type) pairs from the declaration text."""
    m = re.search(r"^\s*(?:PUBLIC_API\s+)?(.+?)\n(\w+)\s*\((.*)\)\s*;\s*$",
                  decl, re.S | re.M)
    if not m:
        return None, None
    ret, name, args = m.group(1).strip(), m.group(2), m.group(3).strip()
    ps = []
    if args and args != "void":
        for a in args.split(","):
            a = a.strip()
            mm = re.match(r"^(.*?[\s*])(\w+)$", a)
            if mm:
                ps.append((mm.group(2), mm.group(1).strip()))
            else:
                ps.append((a, ""))
    return (ret, name), ps


def main():
    src = load()
    head, body, tail, items, prev_end = split(src)
    print(f"items={len(items)}")
    for doc, decl, gap in items:
        rn, ps = params_of(decl)
        ret, name = rn
        d = doc or ""
        tags = sorted(set(re.findall(r"@(brief|details|param|return|code|note|file|struct|enum)",
                                     d)))
        pn = re.findall(r"@param\s+(\w+)", d)
        print(f"--- {name}  ret={ret!r} params={[p for p, t in ps]}")
        print(f"    doc_tags={tags} doc_params={pn} has_code={'@code' in d}")
    print("trailing after last item:", repr(body[prev_end:]))


if __name__ == "__main__":
    main()
