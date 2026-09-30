#!/usr/bin/env python3
"""gen_decl_types.py — emit src/include/generated/ui_decl_types.h.

The declaration types, one per kind, generated from tools/kind_schema.py. A key's
C member and its type therefore have exactly one source: the schema's `c_member`
and `type` fields. Hand-writing these structs beside the schema, and policing the
two against each other, is the arrangement §2.2 replaces.

WHY A TYPE PER KIND, WHEN SEVERAL ARE THE SAME SHAPE

Because the accessor is per kind and casts to its kind's type behind a kind check
(§2.1). Two kinds that happen to carry the same keys get two structurally
identical types, and that is the point: a cast that names the wrong kind does not
compile, rather than quietly reading a member that means something else. The
duplication is generated, so it cannot drift the way a hand-kept copy would.

WHAT IT EMITS, AND WHAT IT DOES NOT

Types only. The accessors live in the unit that owns the widget, the blocks live
in the generated model, and the enum-order rule binding schema to
ircit_ui_kind_enum.h is a verifier rule rather than a declaration.

The output is tracked and committed, like the model file beside it, because the
generator is hand-run rather than a CMake target: regenerate with

    python3 tools/gen_decl_types.py

and the check is that a second run changes nothing (IRD §2.4).
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
import kind_schema as S  # noqa: E402
import provenance  # noqa: E402

DEFAULT_OUT = Path("src/include/generated/ui_decl_types.h")

# The C type a key's member is declared with, per kind_schema's type set. A table
# is two members, so it is handled apart from this map. The star is written with
# the NAME rather than the type, so the names line up in a column the way
# include/ircituilib/ircit_ui_widget_def_type.h writes its own.
CTYPE = {
    S.GLYPH: "const char",
    S.STRING: "const char",
    S.CELLS: "int",
    S.INTEGER: "int",
    S.ENUM: "int",
    S.COLOUR: "int",
}


# The type's name is the schema's rule, not this generator's: the model emitter
# points at these types and must spell them identically.
type_name = S.decl_type_name


def members_of(key):
    """The (type, declarator, doc) triples a key contributes to a struct.

    A table contributes its pointer and its count, in that order, so the count
    always follows what it counts."""
    if key["type"] == S.TABLE:
        return [
            (f'const {key["element"]}', f'*{key["c_member"]}', key["doc"]),
            ("int", key["count_member"], f"how many {key['c_member']} entries there are"),
        ]
    ctype = CTYPE[key["type"]]
    declarator = f'*{key["c_member"]}' if ctype == "const char" else key["c_member"]
    return [(ctype, declarator, key["doc"])]


def emit_struct(kind, out):
    """Append one kind's declaration type, or nothing where it carries no key."""
    keys = S.keys_for(kind)
    if not keys:
        return
    rows = [row for key in keys for row in members_of(key)]
    wtype = max(len(t) for t, _, _ in rows)
    decls = [f"  {ctype:<{wtype}}  {declarator};" for ctype, declarator, _ in rows]
    wdecl = max(len(d) for d in decls)
    out.append(f"/* What {S.KIND[kind]} may declare in a layout. A node of this kind")
    out.append("   carries no block at all until it declares one of these, and its own")
    out.append("   defaults apply where it declares none. */")
    out.append(f"typedef struct {type_name(kind)} {{")
    for decl, (_, _, doc) in zip(decls, rows):
        out.append(f"{decl:<{wdecl}}  // {doc}")
    out.append(f"}} {type_name(kind)};")
    out.append("")


def emit():
    """The whole header, as a list of lines."""
    lines = [
        "/* generated — do not edit */",
        provenance.generated_line(provenance.GEN_DECL_TYPES),
        "/**",
        " * @file ui_decl_types.h",
        " * @brief The per-kind declaration types, emitted from tools/kind_schema.py",
        " *        by tools/gen_decl_types.py. Do not edit: edit the schema, then the",
        " *        generator, and regenerate.",
        " *",
        " * A node's declarations are one generated const block, pointed at from",
        " * IrcitUiCommonState.declarative_attributes_aux and read at the point of use",
        " * by the kind's own accessor. These are the types those blocks are laid out",
        " * in: one per kind, so an accessor that casts to the wrong kind fails to",
        " * compile rather than reading a member that means something else.",
        " *",
        " * Private by convention: this sits under src/include/ with the rest of the",
        " * framework's private headers, not under include/ircituilib/, which is the",
        " * public root and the boundary an application builds against. A key reaches",
        " * an application through the public API of the widget that owns it.",
        " */",
    ]
    # A table member is a pointer to the element type, and the initialiser that
    # fills it lives in the model, so the header only needs the declaration. It
    # includes it anyway rather than forward-declaring: a forward declaration
    # would compile here and fail at the one place that builds the array.
    headers = sorted({key["element_header"] for key in S.DECLARATIVE_KEYS
                      if key["type"] == S.TABLE})
    if headers:
        lines += [""] + [f'#include "{h}"' for h in headers]
    lines += ["", "#ifndef IRCITUI_GENERATED_UI_DECL_TYPES_H", "#define IRCITUI_GENERATED_UI_DECL_TYPES_H", ""]
    for kind in S.KIND:
        emit_struct(kind, lines)
    lines += ["#endif /* IRCITUI_GENERATED_UI_DECL_TYPES_H */"]
    return lines


ACCESS_DEFAULT = Path("src/include/generated/ui_decl_access.h")

ACCESS_RTYPE = {
    S.GLYPH: "const char *",
    S.STRING: "const char *",
    S.CELLS: "int",
    S.INTEGER: "int",
    S.ENUM: "int",
    S.COLOUR: "int",
}


def accessor_name(key, member=None, suffix=""):
    """The accessor for one member: bar_label_mode -> IrcitUiDeclBarLabelMode."""
    m = member or key["c_member"]
    return "IrcitUiDecl" + "".join(p[:1].upper() + p[1:] for p in m.split("_")) + suffix


def accessor_fallback(key, member):
    """What the accessor answers when neither the block nor the flat member has it.

    While the family is still on the node the flat member IS the answer, and after
    its removal the schema's own fallback expression is -- one line, and the flag
    that swaps them is the same one S3 flips when it deletes the members."""
    if key["family"] in S.REMOVED_FAMILIES:
        return key["fallback"]
    return f"node_ptr->common.{member}"


def emit_accessor(key, member, rtype, out):
    """One `static inline` accessor: the cast, the kind check, and the fallback."""
    sentinel = key.get("sentinel")
    name = accessor_name(key, member)
    out.append(f"/* {key['doc'].capitalize()}, as the layout declared it. {S.KIND[key['kinds'][0]]}"
               + (f" and the other kinds it is scoped to" if len(key["kinds"]) > 1 else "")
               + ",")
    out.append("   and the node's own default on any other kind or where nothing was declared. */")
    out.append(f"static inline {rtype}")
    out.append(f"{name}(const IrcitUiNode *node_ptr)")
    out.append("{")
    # An if/else chain rather than a switch: every kind OUTSIDE the scope is an
    # intended fall-through, and a switch would have -Wswitch warn on each of
    # them -- a dozen warnings per accessor saying what this chain states once.
    out.append("  const void *decl_ptr = node_ptr->common.declarative_attributes_aux;")
    out.append("")
    out.append("  if (decl_ptr) {")
    for i, kind in enumerate(key["kinds"]):
        tname = S.decl_type_name(kind)
        kw = "if" if i == 0 else "} else if"
        out.append(f"    {kw} (node_ptr->common.kind == {S.KIND[kind]}) {{")
        out.append(f"      const {tname} *d_ptr = (const {tname} *)decl_ptr;")
        test = f"d_ptr->{member} != {sentinel}" if sentinel else f"d_ptr->{member}"
        out.append(f"      if ({test}) return d_ptr->{member};")
    out.append("    }")
    out.append("  }")
    # The flat member while it exists, its own default once it does not.
    out.append(f"  return {accessor_fallback(key, member)};")
    out.append("}")
    out.append("")


def emit_access():
    """The whole accessor header, as a list of lines."""
    lines = [
        "/* generated — do not edit */",
        provenance.generated_line(provenance.GEN_DECL_TYPES),
        "/**",
        " * @file ui_decl_access.h",
        " * @brief The declaration accessors, emitted from tools/kind_schema.py by",
        " *        tools/gen_decl_types.py. Do not edit: edit the schema and regenerate.",
        " *",
        " * ONE ACCESSOR PER MEMBER, and it is generated for a reason the hand-written",
        " * version could not answer: a key shared by six kinds -- the collapse glyphs,",
        " * say -- would need six casts written out by hand, one per kind's type, and a",
        " * seventh kind added later would be a case nobody remembered to write. Here",
        " * the scope is the schema's, and R23 asserts every accessor is current.",
        " *",
        " * Each one does three things: checks the node's kind, casts the block to that",
        " * kind's type, and answers with the block's value -- or with the node's own,",
        " * which is what every node still carries until the removal stage deletes it.",
        " *",
        " * Private by convention, like the types it casts to.",
        " */",
        "#ifndef IRCITUI_GENERATED_UI_DECL_ACCESS_H",
        "#define IRCITUI_GENERATED_UI_DECL_ACCESS_H",
        "",
        '#include "generated/ui_decl_types.h"',
        '#include "ui_priv.h"',
        "",
    ]
    for key in S.DECLARATIVE_KEYS:
        if key["type"] == S.TABLE:
            emit_accessor(key, key["c_member"],
                          f'const {key["element"]} *', lines)
            emit_accessor(key, key["count_member"], "int", lines)
        else:
            emit_accessor(key, key["c_member"], ACCESS_RTYPE[key["type"]], lines)
    lines += ["#endif /* IRCITUI_GENERATED_UI_DECL_ACCESS_H */"]
    return lines


def main(argv):
    dst = Path(argv[1]) if len(argv) > 1 else DEFAULT_OUT
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_text("\n".join(emit()) + "\n")
    print(f"wrote {dst}", file=sys.stderr)
    access = Path(argv[2]) if len(argv) > 2 else ACCESS_DEFAULT
    access.write_text("\n".join(emit_access()) + "\n")
    print(f"wrote {access}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
