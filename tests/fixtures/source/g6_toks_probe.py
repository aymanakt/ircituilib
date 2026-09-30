#!/usr/bin/env python3
"""Token-level equivalence for comment-only edits.

Tokenise the HEAD revision and the working revision of a file; drop comments on
both sides; the remaining token stream (kind, text) must be identical. This is
the check that catches a comment edit which changes *meaning* rather than text —
e.g. switching a multi-line /**< trailing comment to ///<, which silently
un-comments its continuation lines.
"""
import subprocess
import sys

IDCH = set("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")


def tokenize(src):
    """Yield (kind, text) for ident/number/punct/str/chr. Comments and
    preprocessor directives are yielded as ('comment'|'preproc', text)."""
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if c in " \t\r\n\f\v":
            i += 1
            continue
        if src.startswith("//", i):
            j = src.find("\n", i)
            j = n if j < 0 else j
            yield ("comment", src[i:j])
            i = j
            continue
        if src.startswith("/*", i):
            j = src.find("*/", i)
            j = n if j < 0 else j + 2
            yield ("comment", src[i:j])
            i = j
            continue
        if c == "\n" or (c == "#" and src[:i].rfind("\n") == src[:i].rfind("\n")):
            pass
        if c == "#":                      # only at line start
            k = src.rfind("\n", 0, i)
            if src[k + 1:i].strip() == "":
                j = i
                while j < n:
                    j = src.find("\n", j)
                    if j < 0:
                        j = n
                        break
                    if not src[j - 1].endswith("\\"):
                        j += 1
                        break
                    j += 1
                yield ("preproc", src[i:j])
                i = j
                continue
        if c in IDCH:
            j = i
            while j < n and src[j] in IDCH:
                j += 1
            yield ("ident", src[i:j])
            i = j
            continue
        if c.isdigit():
            j = i
            while j < n and (src[j] in IDCH or src[j] == "."):
                j += 1
            yield ("number", src[i:j])
            i = j
            continue
        if c in "\"'":
            j = i + 1
            while j < n:
                if src[j] == "\\":
                    j += 2
                    continue
                if src[j] == c:
                    j += 1
                    break
                j += 1
            yield ("str", src[i:j])
            i = j
            continue
        yield ("punct", c)
        i += 1


def code_tokens(src):
    return [(k, t) for k, t in tokenize(src) if k not in ("comment",)]


def main():
    paths = sys.argv[1:] or subprocess.run(
        ["git", "diff", "--name-only"], capture_output=True, text=True,
        check=True).stdout.split()
    bad = 0
    for p in paths:
        try:
            old = subprocess.run(["git", "show", "HEAD:./" + p], capture_output=True,
                                 text=True, check=True).stdout
        except subprocess.CalledProcessError:
            print(f"SKIP (untracked) {p}")
            continue
        new = open(p, encoding="utf-8").read()
        a, b = code_tokens(old), code_tokens(new)
        if a == b:
            print(f"OK   {p}  ({len(a)} code tokens identical)")
            continue
        bad += 1
        print(f"FAIL {p}  {len(a)} -> {len(b)} tokens")
        for i in range(max(len(a), len(b))):
            ta = a[i] if i < len(a) else None
            tb = b[i] if i < len(b) else None
            if ta != tb:
                lo = max(0, i - 6)
                print(f"   first divergence at token {i}:")
                print(f"     old ... {a[lo:i+4]}")
                print(f"     new ... {b[lo:i+4]}")
                break
    print("RESULT:", "PASS" if bad == 0 else f"FAIL ({bad} file(s))")
    sys.exit(1 if bad else 0)


if __name__ == "__main__": main()
