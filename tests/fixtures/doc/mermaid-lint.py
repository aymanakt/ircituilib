#!/usr/bin/env python3
"""Structural Mermaid linter.

Checks the failure modes that actually break rendering, without needing a
browser. Not a substitute for a real render -- it validates structure, not
layout.

Usage: mmlint.py FILE.md
"""
import re
import sys

VALID_HEADERS = (
    "flowchart", "graph", "classDiagram", "sequenceDiagram", "stateDiagram-v2",
    "stateDiagram", "erDiagram", "journey", "gantt", "pie", "mindmap",
    "timeline", "quadrantChart", "gitGraph", "block-beta",
)

# sequence diagram arrow tokens
SEQ_ARROWS = ("->>", "-->>", "->", "-->", "-x", "--x", "-)", "--)", "<<->>", "<<-->>")

errors = []
warnings = []


def blocks(path):
    """Yield (start_line, [lines]) for each ```mermaid fence."""
    with open(path, encoding="utf-8") as fh:
        lines = fh.read().split("\n")
    i = 0
    while i < len(lines):
        if lines[i].strip().startswith("```mermaid"):
            start = i + 1          # 1-based line of the fence
            body = []
            i += 1
            while i < len(lines) and not lines[i].strip().startswith("```"):
                body.append(lines[i])
                i += 1
            if i >= len(lines):
                errors.append("%s:%d unterminated ```mermaid fence" % (path, start))
            yield start, body
        i += 1


def check(start, body, idx):
    tag = "diagram #%d (line %d)" % (idx, start)
    if not body:
        errors.append("%s: empty" % tag)
        return
    head = body[0].strip()
    kind = head.split()[0] if head.split() else ""
    if kind not in VALID_HEADERS:
        errors.append("%s: invalid diagram header %r" % (tag, head))
        return

    # quote balance across the block
    for n, raw in enumerate(body, start=start):
        if raw.count('"') % 2 != 0:
            errors.append("%s line %d: odd number of double quotes" % (tag, n))

    if kind in ("flowchart", "graph"):
        depth = 0
        for n, raw in enumerate(body, start=start):
            s = raw.strip()
            if s.startswith("%%"):
                continue
            opens = len(re.findall(r"\bsubgraph\b", s))
            # a bare `end` closes one subgraph
            closes = 1 if s == "end" else 0
            depth += opens - closes
            if depth < 0:
                errors.append("%s line %d: 'end' without matching 'subgraph'" % (tag, n))
                depth = 0
            # unquoted parentheses inside a node label are a classic parse break
            for lbl in re.findall(r"\[([^\]]*)\]", s):
                if not lbl.startswith('"') and ("(" in lbl or ")" in lbl):
                    errors.append("%s line %d: unquoted () in node label %r" % (tag, n, lbl))
                if not lbl.startswith('"') and (":" in lbl or "," in lbl):
                    warnings.append("%s line %d: bare ':' or ',' in label %r" % (tag, n, lbl))
        if depth != 0:
            errors.append("%s: %d unclosed subgraph(s)" % (tag, depth))

    if kind == "classDiagram":
        # Braces open both `namespace X {` and `class X {` bodies, so track a
        # stack rather than assuming `}` only closes namespaces.
        stack = []
        for n, raw in enumerate(body, start=start):
            s = raw.strip()
            if s.startswith("%%"):
                continue
            if s.endswith("{") or "{" in s:
                if re.match(r"^namespace\s+\S+\s*\{", s):
                    stack.append("namespace")
                elif re.match(r"^(class|classDiagram)\b.*\{\s*$", s) or re.match(r"^class\s+\S+\s*\(", s):
                    stack.append("class")
                else:
                    errors.append("%s line %d: unexpected '{' opener %r" % (tag, n, s))
                    stack.append("?")
            elif s == "}":
                if not stack:
                    errors.append("%s line %d: '}' with nothing open" % (tag, n))
                else:
                    stack.pop()
            # member lines containing these are risky
            if re.search(r"^\s*[+\-#~]\s*\S+.*[\[\]<>]", s):
                warnings.append("%s line %d: brackets in class member %r" % (tag, n, s))
        if stack:
            errors.append("%s: %d unclosed brace block(s) %r" % (tag, len(stack), stack))

    if kind.startswith("stateDiagram"):
        in_note = False
        for n, raw in enumerate(body, start=start):
            s = raw.strip()
            # `note ... end note` is a free-text block: its body may contain
            # anything, including lines that look like bare state names.
            if in_note:
                if s.startswith("end note"):
                    in_note = False
                continue
            if re.match(r"^note\b", s):
                if not re.match(r"^note\s+\S+\s+of\s+\S+\s*:", s) and not re.match(
                        r"^note\s+(left|right)\s+of\s+\S+\s*:", s):
                    in_note = True
                continue
            if s.startswith("state ") and "{" in s and "}" not in s:
                continue
            if "-->" not in s and s and not s.startswith(("state", "}", "%%", "direction", "classDef", "class ")):
                if ":" not in s:
                    warnings.append("%s line %d: state line without transition or label: %r" % (tag, n, s))

    if kind == "sequenceDiagram":
        declared = set()
        for n, raw in enumerate(body, start=start):
            s = raw.strip()
            if s.startswith("participant ") or s.startswith("actor "):
                m = re.match(r"(?:participant|actor)\s+(\S+)(?:\s+as\s+(.+))?$", s)
                if m:
                    declared.add(m.group(1))
                    alias = m.group(2)
                    if alias and re.search(r"[()\[\]]", alias) and not alias.strip().startswith('"'):
                        errors.append("%s line %d: unquoted () in participant alias %r" % (tag, n, alias))
            elif "-->>" in s or "->>" in s or "->" in s:
                if not any(a in s for a in SEQ_ARROWS):
                    errors.append("%s line %d: unrecognised sequence arrow %r" % (tag, n, s))
            # 'rect' / 'alt' / 'loop' / 'opt' / 'par' / 'critical' / 'break' need 'end'
        for kw in ("rect", "alt", "loop", "opt", "par", "critical", "break"):
            o = sum(1 for s in body if re.match(r"^\s*%s\b" % kw, s))
            # 'end' may close any of them; only flag if fewer ends than openers overall
        opens = sum(1 for s in body if re.match(r"^\s*(alt|loop|opt|par|critical|break|rect)\b", s))
        ends = sum(1 for s in body if s.strip() == "end")
        if ends < opens:
            errors.append("%s: %d block opener(s) but only %d 'end'" % (tag, opens, ends))


def main():
    path = sys.argv[1]
    seen = 0
    for i, (start, body) in enumerate(blocks(path), 1):
        seen += 1
        check(start, body, i)
    print("mermaid blocks found: %d" % seen)
    if warnings:
        print("\nWARNINGS (%d):" % len(warnings))
        for w in warnings:
            print("  " + w)
    if errors:
        print("\nERRORS (%d):" % len(errors))
        for e in errors:
            print("  " + e)
        return 1
    print("no structural errors")
    return 0


if __name__ == "__main__":
    sys.exit(main())
