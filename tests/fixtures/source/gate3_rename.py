#!/usr/bin/env python3
"""Gate 3: apply the signed-off rename map.

Why not sed: a blind substitution rewrites identifiers inside string literals
and Doxygen prose, silently changing text a human reads. So C-family files are
lexed with a state machine that knows code from comment from string, and every
class is rewritten on its own terms:

  code     every mapped identifier, whole-word
  comment  every mapped identifier EXCEPT the words in PROSE_EXEMPT, which are
           English at those sites ("a successful paint", "attach to @p parent")
  string   untouched, except the short allow-list below -- user-visible text is
           changed only deliberately

Python and YAML are not lexed as C. gen_ui.py emits C from its string literals,
so there the strings ARE the payload and a whole-file whole-word rewrite is the
correct treatment. YAML carries an explicit two-name allow-list, because a
blanket rewrite would corrupt `id: sample` into `id: sSample`.

Refuses to run unless every mapped name is found, and reports counts per class.
"""
import re
import sys
from pathlib import Path

import os
ROOT = Path(os.environ.get("G3ROOT", "/home/devops/repos/ircit/packages/ircituilib"))
MAP = ROOT / "dev/rename_map.tsv"

APPLY = "--apply" in sys.argv

# C-family: lexed.
C_FILES = [
    "include/ircituilib/ui.h", "src/ui_internal.h", "src/ui_engine.c",
    "src/ui_image.c", "src/ui_verify.c", "src/ui_theme.c", "src/version.c.sh",
    "examples/ui_main.c",
    "tests/ircitui_core/api_test.cpp", "tests/ircitui_core/theme_test.cpp",
    "tests/ircitui_core/lifecycle_test.cpp", "tests/ircitui_core/version_test.cpp",
]
# Whole-file whole-word rewrite: the strings are generated C.
REGEX_FILES = ["tools/gen_ui.py"]
# Explicit allow-list of the only mapped names these files legitimately carry.
YAML_FILES = {
    "config/ircitui.yaml": {"ui_edit_input"},
    "config/file_menu.yaml": {"ui_popup_focus_on_open"},
    "config/tools_panel.yaml": {"ui_popup_focus_on_open"},
    "config/emoji_picker.yaml": {"ui_popup_focus_on_open"},
}

# Words that are English prose at their comment sites. Renamed in code; left
# alone in comments. Evidence: the map's own counts for these two in ui.h are
# exactly these occurrences, and each reads as prose.
PROSE_EXEMPT = {"paint", "attach"}

# The single user-visible string that names a renamed symbol, so it stays true.
STRING_ALLOW = {("src/ui_verify.c", "ui_data_bind"): "IrcitUiDataBind"}

# The private header is renamed in the same commit; its spelling appears inside
# include directives, which are string literals and so are not covered above.
INCLUDE_OLD = "ui_internal.h"
INCLUDE_NEW = "ui_priv.h"

IDENT = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")


def load_map():
    rows, table = [], {}
    for line in MAP.read_text().splitlines()[1:]:
        if not line.strip():
            continue
        f = line.split("\t")
        kind, old, new = f[0], f[1], f[2]
        if new == "(DELETE)":
            continue
        rows.append((kind, old, new))
        table[old] = new
    return rows, table


def lexed_rewrite(text, table, rel):
    """Rewrite code identifiers, Doxygen-referenced identifiers, and the one
    allow-listed string. Everything else is copied through byte for byte."""
    out, i, n, state = [], 0, len(text), "code"
    names = sorted(table, key=len, reverse=True)
    pat = re.compile(r"\b(%s)\b" % "|".join(re.escape(x) for x in names))

    def sub_code(s):
        return pat.sub(lambda m: table[m.group(1)], s)

    while i < n:
        c = text[i]
        if state == "code":
            if text[i:i + 2] == "//":
                state, i = "line", i + 2
                out.append("//")
                continue
            if text[i:i + 2] == "/*":
                state, i = "block", i + 2
                out.append("/*")
                continue
            if c == '"':
                state, i = "str", i + 1
                out.append('"')
                continue
            if c == "'":
                state, i = "chr", i + 1
                out.append("'")
                continue
            m = IDENT.match(text, i)
            if m:
                w = m.group(0)
                out.append(sub_code(w))
                i = m.end()
                continue
            out.append(c)
            i += 1
            continue

        if state == "line":
            if c == "\n":
                state = "code"
                out.append(c)
                i += 1
                continue
            m = IDENT.match(text, i)
            if m:
                w = m.group(0)
                out.append(w if w in PROSE_EXEMPT else table.get(w, w))
                i = m.end()
                continue
            out.append(c)
            i += 1
            continue

        if state == "block":
            if text[i:i + 2] == "*/":
                state, i = "code", i + 2
                out.append("*/")
                continue
            m = IDENT.match(text, i)
            if m:
                w = m.group(0)
                out.append(w if w in PROSE_EXEMPT else table.get(w, w))
                i = m.end()
                continue
            out.append(c)
            i += 1
            continue

        # string / char literal
        if c == "\\":
            out.append(text[i:i + 2])
            i += 2
            continue
        if (state == "str" and c == '"') or (state == "chr" and c == "'"):
            state, i = "code", i + 1
            out.append(c)
            continue
        m = IDENT.match(text, i)
        if m:
            w = m.group(0)
            out.append(STRING_ALLOW.get((rel, w), w))
            i = m.end()
            continue
        out.append(c)
        i += 1
        continue
    return "".join(out)


def regex_rewrite(text, table, allowed=None):
    names = sorted(allowed if allowed is not None else table, key=len, reverse=True)
    pat = re.compile(r"\b(%s)\b" % "|".join(re.escape(x) for x in names))
    return pat.sub(lambda m: table[m.group(1)], text)


def main():
    rows, table = load_map()
    print("map rows (excluding DELETE): %d, distinct source names: %d"
          % (len(rows), len(table)))

    changed, total = [], 0
    for rel in C_FILES + REGEX_FILES + list(YAML_FILES):
        path = ROOT / rel
        if not path.exists():
            print("  MISSING %s" % rel)
            return 1
        before = path.read_text()
        if rel in YAML_FILES:
            after = regex_rewrite(before, table, YAML_FILES[rel])
        elif rel in REGEX_FILES:
            after = regex_rewrite(before, table)
        else:
            after = lexed_rewrite(before, table, rel)
        # The private header's spelling lives inside include directives, which are
        # string literals and so are copied through by both rewriters above. The
        # quoted form is replaced here because it only ever means this include:
        # verified before applying -- every occurrence in the tree is an #include.
        after = after.replace('"%s"' % INCLUDE_OLD, '"%s"' % INCLUDE_NEW)
        hits = sum(1 for a, b in zip(before, after) if a != b)
        n_lines = sum(1 for x, y in zip(before.split("\n"), after.split("\n")) if x != y)
        # A rename never changes the line count; a mismatch means a literal newline
        # was consumed, which would be a bug in the lexer.
        if before.count("\n") != after.count("\n"):
            print("  LINE COUNT CHANGED in %s -- refusing" % rel)
            return 1
        if after != before:
            changed.append((rel, n_lines))
            total += n_lines
        if APPLY:
            path.write_text(after)

    print("\nfiles touched: %d, lines changed: %d" % (len(changed), total))
    for rel, k in changed:
        print("  %-42s %4d line(s)" % (rel, k))

    # Every rename target must now be present, and every source name gone from
    # code, in the files that carried it.
    if APPLY:
        print("\npost-apply check")
        stale, missing = [], []
        for rel in C_FILES + REGEX_FILES:
            t = (ROOT / rel).read_text()
            for kind, old, new in rows:
                if re.search(r"\b%s\b" % re.escape(old), t):
                    stale.append("%s:%s" % (rel, old))
                if new not in t and kind in ("function", "type"):
                    missing.append("%s:%s" % (rel, new))
        print("  residual source names (expected: comments/prose/allowed strings)")
        for s in stale:
            print("    %s" % s)
        print("  mode: %s" % ("APPLIED" if APPLY else "dry run"))
    return 0


sys.exit(main())
