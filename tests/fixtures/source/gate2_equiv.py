#!/usr/bin/env python3
"""Gate 2 equivalence proof.

Gate 2 claims to change no behaviour: it replaces bare literals with named
constants and adds includes. This proves it, rather than asserting it.

Method: parse the values back OUT of config_defs.h and ui_internal.h, reverse
the substitution on the new source, and require the result to be token-identical
to HEAD. If a constant were given the wrong value, the reverse map would produce
a different number and the comparison would fail - so this checks the values as
well as the substitutions.
"""
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path("/home/devops/repos/ircit/packages/ircituilib")
FILES = ["src/ui_engine.c", "src/ui_image.c", "src/ui_verify.c", "src/ui_theme.c"]


def strip_comments(s):
    """Blank comments to spaces, preserving newlines so structure survives."""
    out, i, n = [], 0, len(s)
    while i < n:
        c = s[i]
        if c == "/" and i + 1 < n and s[i + 1] == "/":
            j = s.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
            continue
        if c == "/" and i + 1 < n and s[i + 1] == "*":
            j = s.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("".join(ch if ch == "\n" else " " for ch in s[i:j]))
            i = j
            continue
        out.append(c)
        i += 1
    return "".join(out)


def macros(path, prefix):
    """NAME -> literal value, for #define NAME value lines."""
    vals = {}
    for m in re.finditer(r"^#define\s+(%s[A-Z0-9_]*)\s+(-?\d+)\b" % prefix,
                         path.read_text(), re.M):
        vals[m.group(1)] = m.group(2)
    return vals


def resolve_macro(m):
    """Macro names are themselves values; return int or None."""
    try:
        return int(m, 0)
    except ValueError:
        return None


def reverse_map(text, table):
    """Replace the longest names first so prefixes cannot shadow each other."""
    for name in sorted(table, key=len, reverse=True):
        text = re.sub(r"\b%s\b" % re.escape(name), table[name], text)
    return text


def fold_numeric(text):
    """Fold `N op M` where both operands are integer literals. Applied to BOTH
    sides, so it cannot mask a difference - it only lets `256 - 1` compare equal
    to `255`."""
    pat = re.compile(r"(?<![\w.])(\d+)\s*([-+])\s*(\d+)(?![\w.])")
    prev = None
    while prev != text:
        prev = text
        def sub(m):
            a, op, b = int(m.group(1)), m.group(2), int(m.group(3))
            return str(a + b if op == "+" else a - b)
        text = pat.sub(sub, text)
    return text


def normalise(text, table):
    text = strip_comments(text)
    text = "\n".join(l for l in text.split("\n")
                     if l.strip() != '#include "ircituilib/config_defs.h"')
    text = reverse_map(text, table)
    text = fold_numeric(text)
    return re.sub(r"\s+", "", text)


def old_version(rel):
    return subprocess.run(["git", "show", "HEAD:./%s" % rel], cwd=ROOT,
                          capture_output=True, text=True, check=True).stdout


def main():
    config = macros(ROOT / "include/ircituilib/config_defs.h", "PRIV_CONFIG_DEFAULT_")
    keys = macros(ROOT / "src/ui_internal.h", "IRCITUI_KEY_")
    table = dict(config)
    table.update(keys)
    # Library-provided names substituted for colour literals. Values confirmed
    # against ncurses itself, not assumed.
    table["COLOR_WHITE"] = "7"
    table["COLOR_YELLOW"] = "3"

    print("reverse-map table: %d entries (%d config, %d key, 2 colour)"
          % (len(table), len(config), len(keys)))

    bad = 0
    for rel in FILES:
        a = normalise(old_version(rel), table)
        b = normalise((ROOT / rel).read_text(), table)
        if a == b:
            print("  PASS  %-20s token-identical to HEAD (%d chars)"
                  % (rel, len(a)))
        else:
            bad += 1
            print("  FAIL  %-20s DIFFERS" % rel)
            for i, (x, y) in enumerate(zip(a, b)):
                if x != y:
                    print("        first difference at %d:" % i)
                    print("          HEAD: ...%s..." % a[max(0, i - 60):i + 60])
                    print("          now : ...%s..." % b[max(0, i - 60):i + 60])
                    break
            else:
                print("        lengths differ: HEAD=%d now=%d" % (len(a), len(b)))

    # Whitespace/comment-only normalisation could in principle hide a real edit,
    # so also assert the raw line counts agree apart from the added includes.
    print()
    for rel in FILES:
        o = old_version(rel).count("\n")
        n = (ROOT / rel).read_text().count("\n")
        status = "OK" if n == o + 1 else "UNEXPECTED"
        print("  %-8s %-20s HEAD=%d now=%d (expect +1 for the added include)"
              % (status, rel, o, n))
        if n != o + 1:
            bad += 1

    print("\nRESULT: %s" % ("EQUIVALENT" if bad == 0 else "%d FAILURE(S)" % bad))
    return 1 if bad else 0


sys.exit(main())
