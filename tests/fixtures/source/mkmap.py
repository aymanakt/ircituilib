#!/usr/bin/env python3
"""Gate 1: build the rename map for ircituilib convention conformance.

Produces a TSV of old -> new for every symbol class. Read-only: it inspects the
tree and writes only the map.
"""
import re
import sys
from collections import OrderedDict
from pathlib import Path

ROOT = Path("/home/devops/repos/ircit/packages/ircituilib")
HEADER = ROOT / "include/ircituilib/ui.h"
IMPL = [ROOT / "src/ui_engine.c", ROOT / "src/ui_image.c",
        ROOT / "src/ui_verify.c", ROOT / "src/ui_theme.c"]
ALL_C = IMPL + [ROOT / "src/ui_internal.h", HEADER,
                ROOT / "generated/ui_generated.c", ROOT / "src/ui_data.c",
                ROOT / "examples/ui_main.c",
                ROOT / "tests/ircitui_core/api_test.cpp",
                ROOT / "tests/ircitui_core/theme_test.cpp",
                ROOT / "tests/ircitui_core/lifecycle_test.cpp",
                ROOT / "tests/ircitui_core/version_test.cpp",
                ROOT / "tools/gen_ui.py", ROOT / "src/version.c.sh",
                ROOT / "src/include/version.h"]


def pascal(words):
    return "".join(w[:1].upper() + w[1:] for w in words if w)


def func_new(old):
    if old.startswith("IrcitUI"):
        return "IrcitUi" + old[len("IrcitUI"):]
    if old.startswith("ui_"):
        return "IrcitUi" + pascal(old[3:].split("_"))
    raise ValueError(old)


def read(p):
    return p.read_text(encoding="utf-8", errors="replace")


def occurrences(name):
    """Count whole-word occurrences and list files."""
    pat = re.compile(r"\b" + re.escape(name) + r"\b")
    total, where = 0, []
    for p in ALL_C:
        if not p.exists():
            continue
        n = len(pat.findall(read(p)))
        if n:
            total += n
            where.append("%s:%d" % (p.relative_to(ROOT), n))
    return total, where


hdr = read(HEADER)

# --- public functions -------------------------------------------------------
funcs = []
for m in re.finditer(r"^[A-Za-z_][A-Za-z0-9_ \*]*?\b(ui_[a-z0-9_]+|IrcitUI[A-Za-z0-9_]+)\s*\(",
                     hdr, re.M):
    if m.group(1) not in funcs:
        funcs.append(m.group(1))
# also pick up struct-pointer members declared as `UiNode *ui_x(` forms already covered

# --- public types -----------------------------------------------------------
# enum/struct/typedef names introduced in the public header
types = []
for m in re.finditer(r"}\s*([A-Za-z_][A-Za-z0-9_]*)\s*;", hdr):
    types.append(m.group(1))
for m in re.finditer(r"typedef\s+struct\s+([A-Za-z_][A-Za-z0-9_]*)\s+([A-Za-z_][A-Za-z0-9_]*)\s*;", hdr):
    types.append(m.group(2))
for m in re.finditer(r"typedef\s+void\s*\(\s*\*\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)", hdr):
    types.append(m.group(1))
types = list(dict.fromkeys(types))

CALLBACK = {"UiEvent", "UiKeyEvent", "UiDragEvent"}

# --- enum constants and flag macros ----------------------------------------
# Every UI_* token in the public header is a public constant (enum value or
# #define). Line-anchored matching is NOT safe here: values whose line ends in a
# `/**< ... */` trailing comment have no comma/EOL after the identifier, and the
# one-line UiThemeSelector enum has four values on one line. Collect tokens.
consts = list(dict.fromkeys(re.findall(r"\bUI_[A-Z0-9_]+\b", hdr)))

TUNABLES = {"UI_EDIT_CAP", "UI_CARET_BLINK_MS"}

# --- globals ---------------------------------------------------------------
globals_ = []
for m in re.finditer(r"^extern\s+[A-Za-z_][A-Za-z0-9_ \*]*?\b([A-Za-z_][A-Za-z0-9_]*)\s*;", hdr, re.M):
    globals_.append(m.group(1))

GLOBAL_MAP = {
    "ircit_ui": "IrcitUiRoot",
    "ircit_ui_screen": "IrcitUiScreen",
    "ui_focus": "IrcitUiFocus",
    "ui_templates": "IrcitUiTemplates",
    "ui_n_templates": "IrcitUiTemplateCount",
    "ircitui_version": "IrcitUiVersion",
    "ircitui_compiled_by": "IrcitUiCompiledBy",
    "ircitui_compiled_at": "IrcitUiCompiledAt",
}

# --- file-scope statics ----------------------------------------------------
statics = []
for p in IMPL:
    src = read(p)
    for m in re.finditer(
            r"^static\s+[A-Za-z_][A-Za-z0-9_]*[A-Za-z0-9_ \*]*?\b([a-z_][A-Za-z0-9_]*)\s*\(",
            src, re.M):
        statics.append((m.group(1), p.name))
statics = list(dict.fromkeys(statics))

rows = []


def add(kind, old, new, ruling=""):
    total, where = occurrences(old)
    rows.append((kind, old, new, str(total), ",".join(where), ruling))


for f in funcs:
    add("function", f, func_new(f))

for t in types:
    if t in CALLBACK:
        add("callback-typedef", t, "IrcitUi" + t[2:] + "Callback")
    elif t.startswith("Ui"):
        add("type", t, "IrcitUi" + t[2:])
    elif t.startswith("IrcitUI"):
        add("type", t, "IrcitUi" + t[len("IrcitUI"):])

for c in consts:
    if c in TUNABLES:
        add("tunable", c, "PRIV_CONFIG_DEFAULT_" + c[3:], "MOVE-TO-config_defs.h")
    else:
        add("constant", c, "IRCITUI_" + c[3:], "")

for g in globals_:
    add("global", g, GLOBAL_MAP.get(g, "IrcitUi" + pascal(g.split("_"))))

# Version globals are emitted by src/version.c.sh and extern-declared in the
# tests, not in ui.h -- so header-driven extraction misses them.
for g in ("ircitui_version", "ircitui_compiled_by", "ircitui_compiled_at"):
    add("version-global", g, GLOBAL_MAP[g], "")

for s, f in statics:
    add("static", s, "s" + pascal(s.split("_")), "")

# --- accidental exports: external linkage, declared in no header -------------
# Found by the completeness audit. Each is used only inside its own translation
# unit, so the convention answer is static + s prefix (the plan's Gate 3 goal).
ACCIDENTAL_EXPORTS = {
    "ui_text_index_at": ("sTextIndexAt", "src/ui_engine.c", "BECOME-STATIC"),
    "ui_image_load": ("sImageLoad", "src/ui_image.c", "BECOME-STATIC"),
    "ui_image_cell_size": ("sImageCellSize", "src/ui_image.c", "BECOME-STATIC"),
    "ui_internal_free_tree": ("(DELETE)", "src/ui_engine.c",
                              "DELETE: dead wrapper, 0 callers; static would fail -Werror"),
}
for name, (new, where, ruling) in ACCIDENTAL_EXPORTS.items():
    add("internal-export", name, new, "%s (%s)" % (ruling, where))

# --- private types (not public: no module-prefix requirement) ---------------
PRIVATE_TYPES = ["UiCommonState", "UiContainerState", "UiInteractionState", "UiImage"]
for t in PRIVATE_TYPES:
    add("private-type", t, "IrcitUi" + t[2:], "")

hdr_line = "kind\told\tnew\toccurrences\tfiles\truling"
out = [hdr_line]
for r in sorted(rows, key=lambda x: (x[0], x[1])):
    out.append("\t".join(r))

dest = ROOT / "dev/rename_map.tsv"
dest.write_text("\n".join(out) + "\n", encoding="utf-8")

from collections import Counter
c = Counter(r[0] for r in rows)
print("wrote %s" % dest)
print("rows: %d" % len(rows))
for k, v in sorted(c.items()):
    print("  %-16s %d" % (k, v))
targets = [r[2] for r in rows]
dupes = [t for t, n in Counter(targets).items() if n > 1]
print("duplicate targets: %s" % (dupes if dupes else "none"))
selfmapped = [r[1] for r in rows if r[1] == r[2]]
print("self-mapped (no-op): %s" % (selfmapped if selfmapped else "none"))

# --- COMPLETENESS AUDIT ----------------------------------------------------
# Every identifier in the tree that belongs to a mapped class must appear in the
# map. Anything found in the tree but absent from the map is a symbol the rename
# would silently leave behind.
mapped = {r[1] for r in rows}
pat_all = re.compile(r"\b(ui_[a-z0-9_]+|ircitui_[a-z0-9_]+|IrcitUI[A-Za-z0-9_]+|Ui[A-Z][A-Za-z0-9_]*|UI_[A-Z0-9_]+)\b")
# Filename stems are not symbols. They are handled as a *file* rename (see
# FILE_RENAMES) and are excluded from the symbol audit so they cannot mask a
# genuinely unmapped symbol.
FILENAME_STEMS = {"ui_data", "ui_decl", "ui_demo", "ui_engine", "ui_generated",
                  "ui_image", "ui_internal", "ui_main", "ui_verify",
                  # CMake target/function names, not C symbols:
                  "ircitui_apply_build_options", "ircitui_core",
                  "ircitui_core_api_tests", "ircitui_core_generator_tests",
                  "ircitui_core_lifecycle_tests", "ircitui_core_theme_tests",
                  "ircitui_core_version_tests"}
found = Counter()
for p in ALL_C + [ROOT / "tools/gen_ui.py", ROOT / "src/version.c.sh"]:
    if not p.exists():
        continue
    for m in pat_all.finditer(read(p)):
        found[m.group(1)] += 1
missing = sorted(set(found) - mapped - FILENAME_STEMS)
print()
print("AUDIT: identifier-like tokens in tree = %d, unmapped symbols = %d"
      % (len(found), len(missing)))
for m in missing:
    print("  UNMAPPED  %-32s %d occurrence(s)" % (m, found[m]))
stems = sorted(set(found) & FILENAME_STEMS)
print()
print("FILE RENAME worklist (%d stems, %d references) -- handled as filenames, not symbols:"
      % (len(stems), sum(found[s] for s in stems)))
for s in stems:
    print("  %-16s %d reference(s)" % (s, found[s]))
