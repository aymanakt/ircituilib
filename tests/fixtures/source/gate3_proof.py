#!/usr/bin/env python3
"""Gate 3 invariance proof.

Gate 3 claims to rename symbols, plus a small, enumerable list of structural
edits. It claims to change no logic. This proves it.

Method: strip comments, tokenise both revisions, apply the signed-off rename map
to the OLD tokens, then diff the two token streams. A pure rename produces an
EMPTY diff. Every remaining hunk is therefore an intended structural edit, and
the script prints them so they can be read rather than trusted -- a real logic
change hiding among 2,500 renames would surface here as an unexplained hunk.
"""
import difflib
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path("/home/devops/repos/ircit/packages/ircituilib")
FILES = [
    "src/ui_engine.c", "src/ui_image.c", "src/ui_verify.c", "src/ui_theme.c",
    "include/ircituilib/ui.h", "examples/ui_main.c",
    "tests/ircitui_core/api_test.cpp", "tests/ircitui_core/theme_test.cpp",
    "tests/ircitui_core/lifecycle_test.cpp", "tests/ircitui_core/version_test.cpp",
]
# The private header was renamed in this commit, so its old revision lives at a
# different path.
RENAMED = {"src/ui_priv.h": "src/ui_internal.h"}

TOKEN = re.compile(r"""
      "(?:\\.|[^"\\])*"          # string literal, escapes included
    | '(?:\\.|[^'\\])*'          # char literal
    | [A-Za-z_][A-Za-z0-9_]*     # identifier
    | \d+\.?\d*                  # number
    | \.\.\.|->|\+\+|--|<<|>>|<=|>=|==|!=|&&|\|\||[-+*/%&|^~!<>=?:;,.()\[\]{}#]
""", re.X)


def strip_comments(s):
    out, i, n = [], 0, len(s)
    while i < n:
        if s[i:i + 2] == "//":
            j = s.find("\n", i); j = n if j < 0 else j
            out.append(" " * (j - i)); i = j; continue
        if s[i:i + 2] == "/*":
            j = s.find("*/", i + 2); j = n if j < 0 else j + 2
            out.append("".join(ch if ch == "\n" else " " for ch in s[i:j])); i = j; continue
        if s[i] == '"' or s[i] == "'":
            q = s[i]; out.append(s[i]); i += 1
            while i < n and s[i] != q:
                if s[i] == "\\":
                    out.append(s[i:i + 2]); i += 2; continue
                out.append(s[i]); i += 1
            if i < n: out.append(s[i]); i += 1
            continue
        out.append(s[i]); i += 1
    return "".join(out)


def tokens(s):
    return TOKEN.findall(strip_comments(s))


def load_map():
    t = {}
    for line in (ROOT / "dev/rename_map.tsv").read_text().splitlines()[1:]:
        if not line.strip():
            continue
        f = line.split("\t")
        if f[2] != "(DELETE)":
            t[f[1]] = f[2]
    return t


def old_rev(rel):
    path = RENAMED.get(rel, rel)
    r = subprocess.run(["git", "show", "HEAD:./%s" % path], cwd=ROOT,
                       capture_output=True, text=True)
    return r.stdout if r.returncode == 0 else None


def main():
    table = load_map()
    print("rename map applied to the HEAD side: %d names\n" % len(table))
    total_hunks = 0
    for rel in FILES + list(RENAMED):
        old = old_rev(rel)
        new = (ROOT / rel).read_text()
        if old is None:
            print("  SKIP %-38s (no HEAD revision)" % rel); continue
        a = [table.get(t, t) for t in tokens(old)]
        b = tokens(new)
        a = [x for x in a if not (x.startswith('"') and "ui_" in x and x.endswith('.h"'))]
        b = [x for x in b if not (x.startswith('"') and "ui_" in x and x.endswith('.h"'))]
        sm = difflib.SequenceMatcher(None, a, b, autojunk=False)
        hunks = [op for op in sm.get_opcodes() if op[0] != "equal"]
        total_hunks += len(hunks)
        print("%-38s tokens %6d -> %6d   hunks: %d" % (rel, len(a), len(b), len(hunks)))
        for tag, i1, i2, j1, j2 in hunks:
            if tag == "replace" and (i2 - i1) <= 6 and (j2 - j1) <= 6:
                print("     - %s" % " ".join(a[i1:i2]))
                print("     + %s" % " ".join(b[j1:j2]))
            else:
                print("     %s old[%d:%d]=%s" % (tag.upper(), i1, i2, " ".join(a[i1:i2])[:90]))
                print("     %s new[%d:%d]=%s" % (tag.upper(), j1, j2, " ".join(b[j1:j2])[:90]))
    print("\ntotal hunks across all files: %d" % total_hunks)
    return 0


sys.exit(main())
