#!/usr/bin/env python3
"""Gate 6 pass A: public struct/enum doc markers.

  /**  ->  /*!   for a block comment that documents a typedef struct/enum
  /**< ->  ///<  for trailing member / enumerator comments

Both substitutions are width-preserving (3->3, 4->4), so no realignment is
needed and no other byte in the file moves.
"""
import re
import subprocess
import sys

HDRS = subprocess.run(
    ["git", "ls-files", "include/ircituilib/*.h"],
    capture_output=True, text=True, check=True).stdout.split()


def blocks(text):
    """Yield (start, end_exclusive, body) for every /** or /*! block comment."""
    for m in re.finditer(r"/\*[!*]", text):
        if m.group(0) == "/**" and text[m.start():m.start() + 4] == "/**<":
            continue
        if text[m.start():m.start() + 4] == "/**<":
            continue
        # already a ///< ? not matched by /\*[!*]
        end = text.find("*/", m.start())
        if end < 0:
            continue
        yield m.start(), end + 2, text[m.start():end + 2]


def documents_type(text, end):
    """True if the block at `end` is followed only by blanks then a struct/enum."""
    i = end
    n = len(text)
    while i < n:
        if text[i] in " \t\r\n":
            i += 1
            continue
        break
    return re.match(r"(typedef\s+)?(struct|enum)\b", text[i:]) is not None


def main():
    dry = "--dry" in sys.argv
    total_a = total_b = 0
    for path in HDRS:
        src = open(path, encoding="utf-8").read()
        edits = []
        for s, e, body in blocks(src):
            if documents_type(src, e) and src[s:s + 3] == "/**":
                edits.append((s, s + 3, "/*!"))
        for m in re.finditer(r"/\*\*<", src):
            edits.append((m.start(), m.start() + 4, "///<"))
        if not edits:
            continue
        edits.sort()
        # no overlap
        for i in range(1, len(edits)):
            assert edits[i - 1][1] <= edits[i][0], (path, edits[i - 1], edits[i])
        out = []
        prev = 0
        for a, b, rep in edits:
            out.append(src[prev:a])
            out.append(rep)
            prev = b
        out.append(src[prev:])
        new = "".join(out)
        na = sum(1 for _, _, r in edits if r == "/*!")
        nb = sum(1 for _, _, r in edits if r == "///<")
        total_a += na
        total_b += nb
        print(f"{path}: /*! {na}   ///< {nb}")
        if not dry:
            open(path, "w", encoding="utf-8").write(new)
    print(f"TOTAL: /**->/*! {total_a}   /**<->///< {total_b}")


main()
