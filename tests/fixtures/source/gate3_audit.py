#!/usr/bin/env python3
"""Gate 3 pre-flight: reconcile the rename map against the tree.

Three jobs, all read-only:

  1. Every `static` row in the map must correspond to a real file-scope static
     declaration. If a mapped name is really a local variable, renaming it would
     stamp an `s` prefix on a local -- a convention violation, not a fix.
  2. Every mapped name must occur in the tree. A row with zero occurrences is a
     ghost: either the map is stale or the name is spelled differently.
  3. Classify each occurrence as code / comment / string, per file, so the
     decision about what to rewrite is made on counts rather than on memory.
"""
import re
import sys
from pathlib import Path

ROOT = Path("/home/devops/repos/ircit/packages/ircituilib")
MAP = ROOT / "dev/rename_map.tsv"

# Files whose symbol occurrences must be rewritten. generated/ui_generated.c and
# src/ui_data.c are absent by design: the first is regenerated, the second removed.
TARGETS = [
    "include/ircituilib/ui.h", "src/ui_internal.h", "src/ui_engine.c",
    "src/ui_image.c", "src/ui_verify.c", "src/ui_theme.c", "src/version.c.sh",
    "examples/ui_main.c", "tools/gen_ui.py",
    "config/ircitui.yaml", "config/file_menu.yaml", "config/tools_panel.yaml",
    "config/emoji_picker.yaml",
    "tests/ircitui_core/api_test.cpp", "tests/ircitui_core/theme_test.cpp",
    "tests/ircitui_core/lifecycle_test.cpp", "tests/ircitui_core/version_test.cpp",
]


def classify(text):
    """Return (code, comment, string) identifier multisets for C-family text."""
    code, comment, string = [], [], []
    i, n, state = 0, len(text), "code"
    ident = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
    while i < n:
        c = text[i]
        if state == "code":
            if c == "/" and text[i:i + 2] == "//":
                state = "line"; i += 2; continue
            if c == "/" and text[i:i + 2] == "/*":
                state = "block"; i += 2; continue
            if c == '"':
                state = "str"; i += 1; continue
            if c == "'":
                state = "chr"; i += 1; continue
            m = ident.match(text, i)
            if m:
                code.append(m.group(0)); i = m.end(); continue
            i += 1; continue
        if state == "line":
            if c == "\n":
                state = "code"; i += 1; continue
            m = ident.match(text, i)
            if m:
                comment.append(m.group(0)); i = m.end(); continue
            i += 1; continue
        if state == "block":
            if text[i:i + 2] == "*/":
                state = "code"; i += 2; continue
            m = ident.match(text, i)
            if m:
                comment.append(m.group(0)); i = m.end(); continue
            i += 1; continue
        if state in ("str", "chr"):
            tgt = string if state == "str" else string
            if c == "\\":
                i += 2; continue
            if (state == "str" and c == '"') or (state == "chr" and c == "'"):
                state = "code"; i += 1; continue
            m = ident.match(text, i)
            if m:
                tgt.append(m.group(0)); i = m.end(); continue
            i += 1; continue
    return code, comment, string


def main():
    rows = []
    for line in MAP.read_text().splitlines()[1:]:
        if not line.strip():
            continue
        kind, old, new, occ, files, *rest = (line.split("\t") + [""] * 2)[:6]
        rows.append((kind, old, new, int(occ) if occ.isdigit() else 0, files, (rest[0] if rest else "")))

    declarations = {}
    for rel in TARGETS:
        if not rel.endswith((".c", ".h", ".cpp")):
            continue
        for i, l in enumerate((ROOT / rel).read_text().splitlines(), 1):
            m = re.match(r"^static\s+(?:[A-Za-z_][\w \t*]*?)\b([A-Za-z_]\w*)\s*[\(\[=;]", l)
            if m:
                declarations.setdefault(m.group(1), []).append("%s:%d" % (rel, i))

    print("=== 1. map statics vs real file-scope static declarations ===")
    mapped_statics = {old for k, old, _, _, _, _ in rows if k == "static"}
    print("map static rows: %d   declared statics found: %d"
          % (len(mapped_statics), len(declarations)))
    unmatched = sorted(n for n in mapped_statics if n not in declarations)
    extra = sorted(n for n in declarations if n not in mapped_statics)
    print("map statics with NO matching declaration: %s" % (unmatched or "none"))
    print("declarations missing from map: %s" % (extra or "none"))

    print("\n=== 2. occurrence reconciliation (map count vs actual) ===")
    texts = {rel: (ROOT / rel).read_text() for rel in TARGETS}
    analysed = {}
    for rel, t in texts.items():
        analysed[rel] = classify(t) if rel.endswith((".c", ".h", ".cpp")) else (None, None, None)

    ghosts, mismatches = [], []
    for kind, old, new, occ, files, ruling in rows:
        tot_c = tot_m = tot_s = 0
        for rel in TARGETS:
            c, cm, s = analysed[rel]
            if c is None:
                n_all = len(re.findall(r"\b%s\b" % re.escape(old), texts[rel]))
                tot_c += n_all
            else:
                tot_c += c.count(old); tot_m += cm.count(old); tot_s += s.count(old)
        total = tot_c + tot_m + tot_s
        if total == 0:
            ghosts.append((kind, old, new))
        elif total != occ:
            mismatches.append((kind, old, occ, total, tot_c, tot_m, tot_s))

    print("ghosts (mapped, zero occurrences in scoped files): %d" % len(ghosts))
    for k, o, n in ghosts:
        print("   %-16s %-28s -> %s" % (k, o, n))
    print("\ncount mismatches vs map: %d" % len(mismatches))
    print("   %-16s %-24s %6s %6s %6s %6s %6s" % ("kind", "old", "map", "now", "code", "cmnt", "str"))
    for k, o, want, tot, c, cm, s in sorted(mismatches, key=lambda r: -abs(r[2] - r[3]))[:40]:
        print("   %-16s %-24s %6d %6d %6d %6d %6d" % (k, o, want, tot, c, cm, s))
    return 0


sys.exit(main())
