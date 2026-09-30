#!/usr/bin/env python3
"""post_generation_verifier.py — checks a generated ui_generated.c against the
YAML it was generated from.

WHY THIS EXISTS, AND HOW IT DIFFERS FROM THE OTHER CHECKS

Three things already check this pipeline, and none of them compares the input to
the output:

  * tools/validate_layout.py   reads the YAML and checks it obeys the grammar.
                               It never opens the generated file.
  * tools/gen_ui.py --validate-layout
                               runs that validator before generating. Still
                               input-side only.
  * tests/test_generator.py    thirteen lines: a good document exits 0 and a bad
                               one exits non-zero. It asserts exit codes, not
                               content.

So a generator that silently DROPPED a field, TRANSPOSED two, or wrote the wrong
kind would pass all three, provided the result stayed self-consistent. This
program closes that hole: it parses the YAML, parses the C, and asserts they
agree node by node, field by field.

Two design choices worth stating, because both are load-bearing:

  * The VOCABULARY is imported from tools/gen_ui.py — the kind names, the colour
    map, the flag keys, the listener tables, the id hash. Those are the contract
    between the two halves; re-spelling them here would invent a second contract
    and report false failures whenever the first one legitimately changed.

  * The TRAVERSAL and the C PARSE are written independently, and deliberately do
    not call gen_ui.py's parse(). If this program reused the generator's own
    parser it would agree with the generator about a dropped field, which is
    precisely the failure it exists to catch. The vocabulary is shared; the
    reading of both sides is not.

WHERE IT SITS, AND WHY

It lives in tools/, beside the two programs it audits, rather than under tests/.
It is part of the generation pipeline rather than a test of it: a build step or a
release check can run it directly against whatever was just emitted, and it is
extended as the generator grows — a new attribute gets a rule here in the same
change that teaches the generator to emit it. tests/test_generator.py stays what
it is, a smoke test over exit codes, and does not grow into this.

USAGE

    python3 tools/post_generation_verifier.py <app.yaml> <generated.c>

Run from the repository root, or from anywhere: the side it imports is found
relative to this file, not to the working directory.

Exit status is 0 when every rule passes, 1 otherwise. Every failure names its
rule, the node, and what was expected against what was emitted.

EXTENDING IT

A new rule takes the next free R-number (R20 is free) and a line in THE RULES below. Rules
report through check("<rule>", condition, message) or F.add("<rule>", message),
so the rule name is the only thing that binds a check to its category — keep the
string identical everywhere it appears or the failure will surface under a
category a reader cannot find.

THE RULES

  R1  Node inventory      the pool holds one entry per YAML node
  R2  Kind                `type:` maps to the right IrcitUiKind
  R3  External references layout_template / item_template files are loaded,
                          reachable from templates[], and rewritten to stems
  R4  Listeners           interaction_defaults carries every framework default;
                          named handlers land in interactions_user
  R5  Geometry            layout_width / layout_height / max_height resolve to
                          the emitted dim tuples
  R6  Flags               clickable, border, draggable, scrollable, visibility,
                          enabled map to the right flag bits
  R7  Appearance          text_colour / bg_colour / border_colour map to indices
  R8  Box                 margin and padding reach the emitted arrays
  R9  Constraints         constraint* keys reach their c_* fields
  R10 Text                text arrives escaped and intact
  R11 Structure           child order and arity match, and each container's
                          slice of the child table is the right one
  R12 Identity            the id index reproduces exactly, so every id resolves
                          to the node that declared it
  R13 Soundness           indices in range, slices inside the table, every node
                          reachable exactly once from the root or a template,
                          no cycles
  R14 Row height          a recycler's row_height reaches the node as declared,
                          and is absent on the nodes that cannot read it
  R15 Mark colour         a toggle's mark_colour reaches the node as a colour
                          NUMBER, or as -1 where the key is absent -- and -1 is
                          not 0, so an absent key cannot be mistaken for black
  R16 View types          a recycler's view_types registry is emitted, is pointed
                          at by the node that declared it, resolves its templates
                          to the STEMS the runtime loader searches on, and is
                          absent everywhere else
  R17 Sticky headers      a recycler's sticky_headers type_id reaches the node as
                          that id, and is ABSENT where the feature is off -- the
                          field is an int whose zero value is a LEGAL type_id, so
                          an omitted one decoding as 0 would pin type 0's rows in
                          every list that never asked for pinning

  R18 Palette slots       a node's theme_class reaches the node by the name the
                          layout spelled, and is ABSENT where none was declared.
                          The rule also checks the slot VOCABULARY itself: the C
                          enumerators in ircit_ui_palette_slot_enum.h and the
                          spellings the generator accepts must be the same set, or
                          a layout can name a slot no code resolves -- which the
                          generator would accept and the painter would ignore

  R20 Conversation window a conversation_window's three configuration keys reach it,
                          and a node that is not one reaches the DEFAULTS -- the
                          fields are emitted on every node, so "absent" is not
                          available as the answer. C25 scopes them to the one kind.
  R19 Bar attributes      a bar's fill_glyph, empty_glyph and label_mode reach the
                          node, and a node that is not a bar reaches the defaults
                          rather than whatever the previous bar held. The rule also
                          checks the label-mode VOCABULARY: the enumerators in
                          ircit_ui_bar_label_mode_enum.h and the spellings the
                          grammar declares must be the same set the generator
                          accepts, or a layout can write a mode that generation
                          refuses -- or generation can hold a mode no layout may
                          legally write

  R22 Kind order          the schema's kind table IS ircit_ui_kind_enum.h's order,
                          as a prefix: the enumerator is serialised into the model
                          above, so a kind inserted anywhere but the end renumbers
                          every kind after it. The enum may carry kinds the schema
                          does not -- the message viewport is one the engine
                          synthesizes and no layout writes.
  R23 Decl types          src/include/generated/ui_decl_types.h is what
                          tools/gen_decl_types.py emits now, and every migrated
                          member is declared in the type of every kind that may
                          carry it. This replaces "the schema agrees with a
                          hand-written type": there is no hand-written type left.
  R24 The block pool      every declaration block is pointed at by at least one
                          node, no two blocks carry the same type and the same
                          declared set (pooling is maximal), no block is shared
                          across kinds (a block's type is one kind's, and C's types
                          are distinct even when their members are identical), and
                          the blocks are in first-seen pool order -- without which
                          a regeneration that changes nothing would reorder the file.
  R25 The agreement       a layout that declares a key must have it in the block
                          that node points at, and a node that declared nothing
                          must point at no block. This is the rule of
                          IRCITUILIB_GENERATOR_REFACTOR_V1.md §2.5. Its first half
                          -- that a member still emitted flat carries the same value
                          in the block -- has RETIRED ITSELF, exactly as §2.5 said
                          it would: every family has left IrcitUiCommonState, so
                          there is no flat member left to agree with and the check
                          skips each one. It caught a block reading "rows.yaml"
                          where the node read the stem "rows" while it was live.

  R13 is the only rule that needs no YAML: it is a property of the output alone,
  and it is here because a self-consistent file that nevertheless points a child
  at the wrong pool entry would satisfy R11's arity check while being wrong.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

try:
    import yaml
except ImportError:
    sys.exit("pip install pyyaml")

# The vocabulary is the contract, so it is imported rather than restated. See the
# note at the top: the traversal below is written independently, but the names
# the two halves must agree on are not.
#
# gen_ui is a sibling -- this file lives in tools/ beside it -- and the path is
# taken from __file__ rather than from sys.path[0], so the program runs from any
# working directory and can be imported as a module. dont_write_bytecode is set
# before the import, not after: importing gen_ui is what would leave a
# tools/__pycache__ behind, and that directory is not in .gitignore.
sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
import gen_ui  # noqa: E402
import kind_schema as SCHEMA  # noqa: E402
import gen_decl_types  # noqa: E402
import provenance  # noqa: E402

KIND = gen_ui.KIND
# Derived from the generator's own tables rather than restated, so R15 cannot
# disagree with C20 about which kinds draw a mark or what a colour is worth.
COLOUR_NUM   = gen_ui.COLOR
TOGGLE_KINDS = {gen_ui.KIND[t] for t in gen_ui.YAML_TOGGLES}
COLOR = gen_ui.COLOR
GRAV = gen_ui.GRAV
CFIELDS = gen_ui.CFIELDS
DEFAULT_LISTENERS = dict(gen_ui.DEFAULT_LISTENERS)
USER_LISTENERS = gen_ui.USER_LISTENERS
SLOT_NONE_C = "IRCITUI_SLOT_NONE"
id_hash = gen_ui.id_hash
FNV_SIZE_FACTOR = 2

# The flag bits, and the YAML that sets or clears each. Only the ones the
# generator derives from YAML appear here; the rest are the engine's to manage.
FLAG_KEYS = {
    "IRCITUI_F_CLICKABLE":           lambda d: bool(d.get("clickable")),
    "IRCITUI_F_CHILDREN_SELECTABLE": lambda d: bool(d.get("children_selectable")),
    "IRCITUI_F_DRAGGABLE":           lambda d: bool(d.get("draggable") or d.get("dragable")),
    "IRCITUI_F_BORDER":              lambda d: (d.get("border") is True
                                                or str(d.get("border", "")).lower() == "true"),
    "IRCITUI_F_VISIBLE":             lambda d: d.get("visibility", "visible")
                                                not in ("gone", "invisible", False),
    "IRCITUI_F_ENABLED":             lambda d: d.get("enabled") is not False,
}


class Failures:
    def __init__(self):
        self.by_rule: dict[str, list[str]] = {}

    def add(self, rule: str, msg: str) -> None:
        self.by_rule.setdefault(rule, []).append(msg)

    def report(self) -> int:
        total = sum(len(v) for v in self.by_rule.values())
        if not total:
            print("PASSED — every rule holds")
            return 0
        print(f"FAILED — {total} violation(s)\n")
        for rule in sorted(self.by_rule):
            print(f"  {rule}")
            for m in self.by_rule[rule]:
                print(f"      {m}")
            print()
        return 1


# --------------------------------------------------------------------------
# Reading the YAML side, independently of the generator's parser.
# --------------------------------------------------------------------------

def load_inputs(app_path: Path):
    """Return (app_root, templates) as plain dicts, in the order the pool uses.

    Templates are files reached through layout_template / item_template, plus any
    declared under a top-level `templates:` key. This mirrors what the generator's
    gather() must do; it is written again here rather than imported so that a
    reference the generator fails to follow shows up as a count mismatch in R1
    instead of being silently agreed with.
    """
    data = yaml.safe_load(app_path.read_text())
    root = data.get("app", data.get("ircitui", data))
    base = app_path.parent

    found, seen = [], set()

    def resolve_ref(ref: str):
        if not str(ref).endswith((".yaml", ".yml")):
            return
        for cand in (Path(ref), base / ref, Path.cwd() / ref):
            if cand.is_file():
                key = str(cand.resolve())
                if key in seen:
                    return
                seen.add(key)
                raw = yaml.safe_load(cand.read_text())
                if isinstance(raw, dict) and raw.get("id") is None:
                    raw = dict(raw, id=cand.stem)
                found.append((cand.stem, raw))
                walk(raw)
                return
        raise SystemExit(f"reference not resolvable from {app_path}: {ref}")

    def walk(node):
        if not isinstance(node, dict):
            return
        for key in ("layout_template", "item_template"):
            if node.get(key):
                resolve_ref(str(node[key]))
        # A registry's entries are reached through the same loader as
        # item_template, so they are followed here too. Miss one and R1's count
        # catches it rather than the file being quietly short a template.
        for entry in (node.get("view_types") or []):
            if isinstance(entry, dict) and entry.get("template"):
                resolve_ref(str(entry["template"]))
        for child in effective_children(node):
            walk(child)

    walk(root)
    raw_templates = data.get("templates") or []
    if isinstance(raw_templates, dict):
        raw_templates = [dict(v or {}, id=k) for k, v in raw_templates.items()]
    for item in raw_templates:
        found.append((item.get("id"), item))

    return root, found


def effective_children(node):
    """The children this node really has, declared and synthesised.

    Two generator behaviours mean the child list is not simply what the YAML
    wrote under `children:`:

      * A radio_group or check_group carrying `values:` gets one synthesised
        child per value -- `{id}_{i}`, of kind radio_button or check_box, with
        the value as its text, laid out match_parent x wrap_content and
        inheriting the group's colours. They are appended AFTER any declared
        children.
      * An image_view's `path` is emitted into .text rather than a field of its
        own, so its text is `path` or `text`.

    Both are the generator's contract, not an accident, and a verifier that did
    not model them would report six false failures against the stock app.
    """
    declared = list(node.get("children") or [])
    kind = KIND.get(str(node.get("type", "frame")), "IRCITUI_FRAME")
    if kind in ("IRCITUI_RADIO_GROUP", "IRCITUI_CHECK_GROUP"):
        vals = node.get("values") or []
        if not isinstance(vals, list):
            vals = [vals]
        radio = kind == "IRCITUI_RADIO_GROUP"
        for i, val in enumerate(vals):
            declared.append({
                "id": f"{node.get('id')}_{i}",
                "type": "radio_button" if radio else "check_box",
                "text": str(val),
                "layout_width": "match_parent",
                "layout_height": "wrap_content",
                "clickable": True,
                "text_colour": node.get("text_colour", "white"),
                "bg_colour": node.get("bg_colour", "black"),
                "_synthesised": True,
            })
    return declared


def post_order(node, acc):
    """Pool order: children before their parent, synthesised ones included."""
    if not isinstance(node, dict):
        return
    for child in effective_children(node):
        post_order(child, acc)
    acc.append(node)
    return acc


def build_pool(app_path: Path):
    """The YAML side as an ordered list of dicts, in the generator's pool order."""
    root, templates = load_inputs(app_path)
    tpl_nodes = []
    for _, tpl in templates:
        post_order(tpl, tpl_nodes)
    app_nodes = post_order(root, [])
    return tpl_nodes + app_nodes, [t[0] for t in templates], root


# --------------------------------------------------------------------------
# Reading the C side. Deliberately not the generator's emitter: this parses the
# text back, which is what a reader of the file would have to do.
# --------------------------------------------------------------------------

class Generated:
    def __init__(self, path: Path):
        self.src = path.read_text()
        self.pool_size = int(re.search(r"IrcitUiNode\s+pool\[(\d+)\]", self.src).group(1))
        self.nodes = {}
        body = self.src[self.src.index(".pool = {"):]
        for m in re.finditer(r"\[(\d+)\] = \{(.*?)\n        \},", body, re.S):
            self.nodes[int(m.group(1))] = m.group(2)
        cref = self.src[self.src.index(".child_refs = {"):self.src.index(".templates = {")] \
            if ".templates = {" in self.src else \
            self.src[self.src.index(".child_refs = {"):self.src.index(".pool = {")]
        self.child_refs = [int(x) for x in re.findall(r"&s_ui_model\.pool\[(\d+)\]", cref)]
        tm = re.search(r"\.templates = \{(.*?)\n    \},", self.src, re.S)
        self.templates = [int(x) for x in re.findall(r"&s_ui_model\.pool\[(\d+)\]", tm.group(1))] if tm else []
        self.slots = [SLOT_NONE_C if c.strip() == SLOT_NONE_C else int(c)
                      for c in re.findall(r"([\d]+|IRCITUI_SLOT_NONE)",
                                          self.src[self.src.index(".id_slot_table = {"):
                                                       self.src.index("};", self.src.index(".id_slot_table = {"))])]
        self.root = int(re.search(r"pool\[(\d+)\]", re.search(r"\.root\s*=\s*([^,\n]+)", self.src).group(1)).group(1))
        # The file-scope view-type registries, by name. Parsed from their own
        # definitions rather than from the node fields that reference them, so a
        # node pointing at a name defined nowhere reads as an empty registry and
        # fails R16, rather than as whatever the last one happened to hold.
        #
        # The name is NOT hard-coded: a registry array now belongs to the
        # declaration block that points at it (IRCITUILIB_GENERATOR_REFACTOR_V1.md
        # §2.3), so it is named after its block rather than after the node -- and
        # a rule that spelled the old prefix would have gone on passing for the
        # shipped layout, which declares no registry at all.
        # The declaration blocks, by name: their type, their members as written,
        # and the ids of the nodes that point at each. Parsed from the
        # definitions, like the registries above and for the same reason -- a
        # node pointing at a block defined nowhere must not read as one that
        # happens to hold the right thing.
        self.blocks = {}
        for m in re.finditer(r"static const (\w+DeclarativeAttributesAux) (s_decl_\w+) = \{\n"
                             r"    (.*?) \};", self.src, re.S):
            self.blocks[m.group(2)] = {
                "type": m.group(1),
                "members": dict(re.findall(r"\.(\w+)=([^,}]+?)(?:,|$)", m.group(3))),
                "sharers": [],
            }
        self.decl_of = {}
        for idx, blob in self.nodes.items():
            name = re.search(r"\.declarative_attributes_aux=&(\w+)", blob)
            if name:
                self.decl_of[idx] = name.group(1)
                if name.group(1) in self.blocks:
                    self.blocks[name.group(1)]["sharers"].append(idx)

        self.registries = {
            m.group(1): [(int(a), c_unescape(b)) for a, b in
                         re.findall(r'\{\s*(-?\d+),\s*"((?:[^"\\]|\\.)*)"\s*\}', m.group(2))]
            for m in re.finditer(r"static const IrcitUiViewTypeMapping (\w+)\[\] = \{\n(.*?)\n\};",
                                 self.src, re.S)
        }

    def field(self, idx, pattern, cast=str, default=None):
        m = re.search(pattern, self.nodes[idx])
        return cast(m.group(1)) if m else default

    def ident(self, idx):
        return self.field(idx, r'\.id="((?:[^"\\]|\\.)*)"', str, "")

    def kids(self, idx):
        off = self.field(idx, r"\.children=&s_ui_model\.child_refs\[(\d+)\]", int, None) or 0
        n = self.field(idx, r"\.n_children=(\d+)", int, 0)
        return self.child_refs[off:off + n]

    def flags(self, idx):
        raw = self.field(idx, r"\.flags=([^,\n]+)", str, "0")
        return set() if raw.strip() == "0" else {f.strip() for f in raw.split("|")}

    def view_types(self, idx):
        """The registry node @p idx points at, read back through the pointer.

        None when the node carries no `.view_types=` field -- which is the
        homogeneous case, and is checked as such rather than being treated as an
        empty registry. Following the pointer is the point: the array is emitted
        at file scope, so a node naming one that does not exist would satisfy any
        check made against the node alone."""
        name = self.field(idx, r"\.view_types=(\w+)", str, None)
        if name is None:
            # The array belongs to the declaration block now, so the node names it
            # there rather than in its own literal.
            name = self.blocks.get(self.decl_of.get(idx, ""), {}).get("members", {}) \
                       .get("view_types", "").strip() or None
        if name is None:
            return None
        return self.registries.get(name, [])

    @property
    def registry_names(self):
        return sorted(self.registries)


def c_unescape(s: str) -> str:
    return s.replace('\\"', '"').replace("\\\\", "\\")


def main(argv):
    if len(argv) != 3:
        sys.exit(__doc__)
    app_path, gen_path = Path(argv[1]), Path(argv[2])
    pool, tpl_stems, _root = build_pool(app_path)
    g = Generated(gen_path)
    F = Failures()

    def check(rule, cond, msg):
        if not cond:
            F.add(rule, msg)

    # Index the generated side by emitted id, so the two lists can be compared
    # by identity rather than by position -- position is R11's business.
    by_id = {}
    for idx in g.nodes:
        by_id.setdefault(g.ident(idx), idx)

    def emitted_member(idx, member):
        """A declarative member's value, wherever the removal stage has it.

        The block first, because a member whose family has been removed is ONLY
        there; the node's own literal while the family is still emitted. A rule
        written against one of the two would silently stop checking anything the
        day the member moved -- which is the failure this whole refactor is about."""
        blk = g.blocks.get(g.decl_of.get(idx, ""), {}).get("members", {})
        if member in blk:
            text = blk[member].strip()
        else:
            text = g.field(idx, rf"\.{member}=([^,\n]+)", str, "")
        text = text.strip().rstrip("}").strip()
        if text.startswith('"') and text.endswith('"'):
            text = text[1:-1]
        return text

    # ---- R1 node inventory ------------------------------------------------
    check("R1 node inventory", len(pool) == g.pool_size,
          f"YAML yields {len(pool)} nodes, the pool holds {g.pool_size}")
    check("R1 node inventory", len(g.nodes) == g.pool_size,
          f"pool declares {g.pool_size} entries but {len(g.nodes)} were emitted")
    yaml_ids = [n.get("id") for n in pool]
    missing = [i for i in yaml_ids if i not in by_id]
    extra = [i for i in by_id if i not in yaml_ids]
    check("R1 node inventory", not missing, f"declared but not emitted: {missing}")
    check("R1 node inventory", not extra, f"emitted but not declared: {extra}")

    # ---- per-node rules ---------------------------------------------------
    for node in pool:
        nid = node.get("id")
        if nid not in by_id:
            continue                                    # already reported by R1
        idx = by_id[nid]
        blob = g.nodes[idx]
        kind = KIND.get(str(node.get("type", "frame")), "IRCITUI_FRAME")

        # R2 kind
        check("R2 kind", g.field(idx, r"\.kind=([A-Z_]+)", str, "") == kind,
              f"'{nid}': type '{node.get('type')}' emitted as "
              f"{g.field(idx, r'\.kind=([A-Z_]+)', str, '?')}, expected {kind}")

        # R3 external references
        for key, emitted_name in (("layout_template", "layout_template"),
                                  ("item_template", "item_template")):
            declared = str(node.get(key) or "")
            emitted = emitted_member(idx, emitted_name)
            if declared.endswith((".yaml", ".yml")):
                want = Path(declared).stem
                check("R3 external references",
                      emitted == want,
                      f"'{nid}': {key} '{declared}' emitted as '{emitted}', expected the stem '{want}'")
            elif declared:
                check("R3 external references", emitted == declared,
                      f"'{nid}': {key} '{declared}' emitted as '{emitted}'")
            elif emitted:
                check("R3 external references", False,
                      f"'{nid}': no {key} declared, but '{emitted}' was emitted")

        # R4 listeners — the framework defaults must be prefilled on EVERY node
        defaults = re.search(r"\.interaction_defaults=\{ \.lifecycle_listeners = \{ (.*?) \} \}",
                             blob, re.S)
        if not defaults:
            F.add("R4 listeners", f"'{nid}': interaction_defaults carries no listener block")
        else:
            bound = dict(re.findall(r"\.(\w+)=(\w+)", defaults.group(1)))
            for field, fn in DEFAULT_LISTENERS.items():
                check("R4 listeners", bound.get(field) == fn,
                      f"'{nid}': default {field} is {bound.get(field)!r}, expected {fn}")
            for field in bound:
                check("R4 listeners", field in DEFAULT_LISTENERS,
                      f"'{nid}': unexpected default listener '{field}'")
        # named handlers land in the node's own block
        user = re.search(r"\.interactions_user = \{(.*)\},?$", blob, re.S)
        named = {k: str(node[k]) for k in USER_LISTENERS if node.get(k)}
        emitted_user = dict(re.findall(r"\.(\w+)=(\w+)", user.group(1))) if user else {}
        for field in USER_LISTENERS:
            if field in named:
                check("R4 listeners", emitted_user.get(field) == named[field],
                      f"'{nid}': {field} '{named[field]}' emitted as {emitted_user.get(field)!r}")
            elif field in emitted_user:
                F.add("R4 listeners", f"'{nid}': {field} bound to {emitted_user[field]} but YAML names none")

        # R5 geometry
        def dim_of(key):
            v = node.get(key)
            if v is None or v == "wrap_content":
                return "IRCITUI_SIZE_WRAP", 0, 0
            if v == "match_parent":
                return "IRCITUI_SIZE_MATCH", 0, 0
            s = str(v).strip()
            if s.endswith("%"):
                return "IRCITUI_SIZE_PERCENT", 0, int(s[:-1])
            return "IRCITUI_SIZE_EXACT", int(v), 0
        wm, wc, wp = dim_of("layout_width")
        hm, hc, hp = dim_of("layout_height")
        maxh = int(node.get("max_height") or 0)
        if kind == "IRCITUI_RECYCLER" and maxh and hm == "IRCITUI_SIZE_WRAP":
            hm, hc, hp = "IRCITUI_SIZE_EXACT", maxh, 0        # C16, resolved
        for axis, want in (("w", (wm, wc, wp)), ("h", (hm, hc, hp))):
            m = re.search(rf"\.layout_{axis}=\{{ (\w+), (-?\d+), (-?\d+) \}}", blob)
            got = (m.group(1), int(m.group(2)), int(m.group(3))) if m else None
            check("R5 geometry", got == want,
                  f"'{nid}': layout_{axis} is {got}, expected {want}")

        # R6 flags
        emitted_flags = g.flags(idx)
        for flag, pred in FLAG_KEYS.items():
            want = bool(pred(node))
            check("R6 flags", (flag in emitted_flags) == want,
                  f"'{nid}': {flag} is {'set' if flag in emitted_flags else 'clear'}, "
                  f"expected {'set' if want else 'clear'}")
        # scrollable and collapsible are flags too, but the generator takes them
        # from keys this table does not model; check only the ones above.

        # R7 appearance
        for key, field in (("text_colour", "fg"), ("bg_colour", "bg"),
                           ("border_colour", "bdr")):
            want = COLOR.get(str(node.get(key, "white" if key != "bg_colour" else "black")).lower(),
                             7 if key != "bg_colour" else 0)
            check("R7 appearance", g.field(idx, rf"\.{field}=(-?\d+)", int, -1) == want,
                  f"'{nid}': {field} is {g.field(idx, rf'.{field}=(-?\d+)', int, -1)}, expected {want}")

        # R8 box
        for key, field in (("margin", "margin"), ("padding", "padding")):
            v = node.get(key)
            want = [v] * 4 if isinstance(v, int) else ([int(x) for x in v] if isinstance(v, list)
                                                       and len(v) == 4 else [0, 0, 0, 0])
            m = re.search(rf"\.{field}=\{{ ([^}}]*) \}}", blob)
            got = [int(x) for x in m.group(1).split(",")] if m else None
            check("R8 box", got == want, f"'{nid}': {field} is {got}, expected {want}")

        # R9 constraints
        for key, field in CFIELDS:
            emitted = g.field(idx, rf"\.{field}=([A-Za-z_0-9]+)", str, "?")
            if key in node:
                check("R9 constraints", emitted != "IRCITUI_UNSET",
                      f"'{nid}': {key} declared but {field} was emitted as UNSET")
            else:
                check("R9 constraints", emitted == "IRCITUI_UNSET",
                      f"'{nid}': {field} is {emitted} though {key} was not declared")

        # R10 text
        want_text_raw = str(node.get("text", ""))
        if kind == "IRCITUI_IMAGE_VIEW":          # path is emitted into .text
            want_text_raw = str(node.get("path") or node.get("text") or "")
        want_text = want_text_raw.replace("\\", "\\\\").replace('"', '\\"')
        got_text = g.field(idx, r'\.text="((?:[^"\\]|\\.)*)"', str, None)
        check("R10 text", c_unescape(got_text or "") == want_text_raw,
              f"'{nid}': text emitted as {got_text!r}, expected {want_text!r}")

        # R14 row height. An omitted key means 1 at runtime, but nothing is
        # emitted for it: the engine floors the zero. So the emitted field must
        # match the declaration exactly, including its absence.
        want_rh = int(node.get("row_height") or 0)
        raw_rh = emitted_member(idx, "row_height")
        got_rh = int(raw_rh) if raw_rh.lstrip("-").isdigit() else 0
        check("R14 row height", got_rh == want_rh,
              f"'{nid}': row_height is {got_rh}, expected {want_rh}")
        if kind != "IRCITUI_RECYCLER" and got_rh:
            F.add("R14 row height",
                  f"'{nid}': row_height {got_rh} was emitted on a {kind}, which cannot read it")

        # R15 mark colour (C20). The emitted field is ALWAYS present, because the
        # engine tells "not given" from a colour by the sign: -1 means the mark
        # takes text_colour. A missing field would decode as 0, which is
        # COLOR_BLACK, and every unset tick would be painted black. That is the
        # failure this rule exists to catch, and it is invisible in a diff.
        want_mark = COLOUR_NUM.get(str(node.get("mark_colour", "")).lower(), -1)
        raw_mark  = emitted_member(idx, "mark_fg")
        got_mark  = int(raw_mark) if raw_mark.lstrip("-").isdigit() else -1
        check("R15 mark colour", got_mark == want_mark,
              f"'{nid}': mark_fg is {got_mark}, expected {want_mark}")
        if kind not in TOGGLE_KINDS and got_mark != -1:
            F.add("R15 mark colour",
                  f"'{nid}': mark_fg {got_mark} was emitted on a {kind}, which draws no mark")

        # R16 view types (C21). The registry is emitted at FILE SCOPE and the node
        # holds a borrowed pointer to it, so the check follows the pointer -- the
        # same thing the engine does at run time, and the reason a node naming an
        # array that was never emitted has to fail here rather than read as an
        # empty registry.
        #
        # The templates are compared as STEMS, because that is what the runtime
        # loader resolves them to: sLookupRef searches templates[] by the stem, so
        # a registry still carrying "log_row.yaml" would compile and then resolve
        # nothing. R3 proves the files loaded; this proves the names that reached
        # the array are the names the loader will look up.
        want_vt = node.get("view_types")
        if want_vt:
            want_pairs = [(int(e["type_id"]), Path(str(e["template"])).stem
                           if str(e["template"]).endswith((".yaml", ".yml")) else str(e["template"]))
                          for e in want_vt]
            got_pairs = g.view_types(idx)
            check("R16 view types", got_pairs == want_pairs,
                  f"'{nid}': view_types resolves to {got_pairs}, expected {want_pairs}")
            # The COUNT is checked separately and is not redundant. The engine
            # iterates `i < n_view_types`, so a count that is short silently drops
            # the entries past it -- the array can be perfect and the list still
            # render the wrong shapes. Found by mutation: every other R16
            # mutation was caught, and this one passed.
            raw_n = emitted_member(idx, "n_view_types")
            got_n = int(raw_n) if raw_n.lstrip("-").isdigit() else 0
            check("R16 view types", got_n == len(want_pairs),
                  f"'{nid}': n_view_types is {got_n}, expected {len(want_pairs)}")
            if kind != "IRCITUI_RECYCLER":
                F.add("R16 view types",
                      f"'{nid}': a view-type registry was emitted on a {kind}, which cannot read it")
        else:
            check("R16 view types", g.view_types(idx) is None,
                  f"'{nid}': a view-type registry was emitted though the node declared none")
            check("R16 view types", not emitted_member(idx, "n_view_types").lstrip("-").isdigit()
                  or int(emitted_member(idx, "n_view_types")) == 0,
                  f"'{nid}': n_view_types was emitted though the node declared no registry")

        # R17 sticky headers (C22). The field is an int, and the engine reads a
        # NEGATIVE value as "off" -- so the failure this rule exists to catch is
        # an omitted field decoding as 0, which is a legal type_id and would pin
        # type 0's rows in a list that never asked for pinning. That is R15's
        # failure mode in a different field, and it is invisible in a diff.
        # The emitted value is ALWAYS present, because the engine tells "not
        # given" from a real type by the sign: -1 is off. An absent key must
        # therefore reach the node as -1 and NOT as 0, which is a legal type_id --
        # a list whose registry declares type 0 would otherwise pin type 0's rows
        # without anyone asking. This rule said the opposite when it was first
        # written, and required the very encoding its own message warned about.
        want_sticky = node.get("sticky_headers")
        raw_sticky  = emitted_member(idx, "sticky_header_type")
        got_sticky  = int(raw_sticky) if raw_sticky.lstrip("-").isdigit() else -1
        if want_sticky is None:
            check("R17 sticky headers", got_sticky == -1,
                  f"'{nid}': sticky_header_type is {got_sticky} though the node declared none, "
                  f"expected -1 (off). 0 is a legal type_id, so a node defaulting to it pins "
                  f"rows nobody asked to pin")
        else:
            check("R17 sticky headers", got_sticky == int(want_sticky),
                  f"'{nid}': sticky_header_type is {got_sticky}, expected {want_sticky}")
            if kind != "IRCITUI_RECYCLER":
                F.add("R17 sticky headers",
                      f"'{nid}': a sticky header type was emitted on a {kind}, which cannot pin")

        # R18 palette slots (C23). Two halves, and the first is about vocabulary
        # rather than about this node.
        #
        # The vocabulary half checks the pair that actually has to agree. The names
        # are written down in four places: the C enum, the table in ui_palette.c that
        # maps a name to a slot at runtime, the generator's set, and the validator's.
        # The two that matter together are the generator's set and ui_palette.c's
        # table, because THAT is the mapping a layout's name goes through: a name the
        # generator accepts and the table does not hold resolves to -1 at paint time,
        # and the painter's answer to -1 is to leave the node in its own colours with
        # nothing said. So it is compared here rather than assumed.
        #
        # The enum is checked for its COUNT instead of its spellings: its enumerators
        # are upper-cased versions of these names, and asserting that transform is
        # asserting a convention rather than a fact. A slot added to the enum and not
        # to the table is what the count catches.
        palette_src = open("src/ui_palette.c").read()
        table = palette_src[palette_src.index("sSlotNames[IRCITUI_SLOT_COUNT]"):]
        table = table[:table.index("};")]
        runtime_slots = set(re.findall(r'"([a-z_]+)"', table))
        check("R18 palette slots", runtime_slots == set(gen_ui.SLOTS),
              f"the generator and ui_palette.c's slot table disagree: "
              f"only in the table {sorted(runtime_slots - set(gen_ui.SLOTS))}, "
              f"only in the generator {sorted(set(gen_ui.SLOTS) - runtime_slots)}")

        enum_src = open("include/ircituilib/ircit_ui_palette_slot_enum.h").read()
        n_enum = len(re.findall(r"IRCITUI_SLOT_[A-Z_]+\s*[,=]", enum_src))
        check("R18 palette slots", n_enum == len(gen_ui.SLOTS) + 1,
              f"the enum declares {n_enum} enumerators for {len(gen_ui.SLOTS)} slots "
              f"(+1 for the COUNT fence), so a slot was added to one side only")

        want_class = node.get("theme_class")
        got_class  = g.field(idx, r'\.theme_class="((?:[^"\\]|\\.)*)"', str, None)
        if want_class is None:
            # Absent must reach the node as NULL rather than as a name, because every
            # slot is a real role: a node defaulting to one of them would wear a class
            # nobody asked for.
            check("R18 palette slots", got_class is None,
                  f"'{nid}': theme_class is {got_class!r} though the node declared none")
        else:
            want_class = str(want_class)
            if want_class not in gen_ui.SLOTS:
                F.add("R18 palette slots",
                      f"'{nid}': theme_class {want_class!r} is not a palette slot, and the "
                      f"generator accepted it")
            check("R18 palette slots", got_class == want_class,
                  f"'{nid}': theme_class is {got_class!r}, expected {want_class!r}")

        # R19 bar attributes (C24). Two halves, and the first is about vocabulary
        # rather than about this node.
        #
        # The mode names are written down in three places: the C enum, the
        # generator's BAR_LABEL_MODE_NAMES tuple, and the grammar's production.
        # The generator EMITS the tuple's names verbatim, so a name that drifts
        # between those two is a compile error and the compiler is that half's
        # guard -- what no compiler catches is a mode that exists in the enum and
        # that no YAML can ask for, which is the count check below.
        #
        # The other pair compared here is the generator's spellings against the
        # grammar's, because THAT is the mapping a layout's `label_mode:` goes
        # through: a spelling the grammar declares and the dict does not hold
        # would be refused at generation with a message about a mode the grammar
        # says is legal, and a spelling in the dict that the grammar does not
        # declare is a key no layout can legally write. The grammar file is read
        # as text, as ui_palette.c is above, rather than imported.
        bar_enum_src = open("include/ircituilib/ircit_ui_bar_label_mode_enum.h").read()
        # Anchored to the start of a line, so this counts ENUMERATOR DECLARATIONS
        # and not every mention of one. R18 above counts its slots by scanning the
        # whole file for `IRCITUI_SLOT_<X>,` and gets the right answer only because
        # the palette header's prose happens to contain `IRCITUI_SLOT_COUNT,` with
        # the comma; a prose edit that added or removed such a mention would move
        # that count without any enumerator changing. This is the same count done
        # in a way the prose cannot reach.
        #
        # No trailing `[,=]` is required, and that is deliberate: the LAST
        # enumerator before the closing brace is the fence, and it carries no comma
        # -- requiring one counts every mode except COUNT and then reports the enum
        # as short by one, which is what the first version of this check did.
        n_bar_enum = len(re.findall(r"^\s*IRCITUI_BAR_LABEL_[A-Z_]+", bar_enum_src,
                                    re.M))
        check("R19 bar attributes", n_bar_enum == len(gen_ui.BAR_LABEL_MODE_NAMES) + 1,
              f"the enum declares {n_bar_enum} enumerators for "
              f"{len(gen_ui.BAR_LABEL_MODE_NAMES)} label modes (+1 for the COUNT "
              f"fence), so a mode was added to one side only")

        grammar_src = open("tools/layout-grammar.ebnf").read()
        # The production's own line, up to its terminating `)`. Bounded by the key
        # rather than by an offset so a rule renumbered above it cannot shift the
        # window onto some other production's spellings. The slice starts AFTER
        # the key, or `"label_mode"` itself would be counted as one of its own
        # spellings -- which is what the first version of this check did.
        bar_prod = grammar_src[grammar_src.index('"label_mode", ('):]
        bar_prod = bar_prod[bar_prod.index("(") + 1:bar_prod.index(")")]
        grammar_modes = set(re.findall(r'"([a-z_]+)"', bar_prod))
        check("R19 bar attributes", grammar_modes == set(gen_ui.BAR_LABEL_MODES),
              f"the grammar and the generator disagree about the label_mode "
              f"spellings: only in the grammar "
              f"{sorted(grammar_modes - set(gen_ui.BAR_LABEL_MODES))}, only in the "
              f"generator {sorted(set(gen_ui.BAR_LABEL_MODES) - grammar_modes)}")

        # The per-node half: a bar's three attributes reach it, and a node that is
        # not a bar reaches NOTHING. That second half read "the defaults" while the
        # fields were emitted on every node -- the failure it guards against was a
        # non-bar carrying whatever the last bar in the pool held. The trio is in
        # the declaration blocks now, and a node that declared none has no block
        # member and no literal at all, which is a stronger statement than
        # "carries the default".
        is_bar = KIND.get(str(node.get("type", ""))) in ("IRCITUI_PROGRESS_BAR",
                                                         "IRCITUI_SEEK_BAR")
        got_fill  = emitted_member(idx, "bar_fill_glyph") or ""
        got_empty = emitted_member(idx, "bar_empty_glyph") or ""
        got_lm    = emitted_member(idx, "bar_label_mode") or "" 
        if is_bar:
            want_fill  = gen_ui.esc(node.get("fill_glyph") or "")
            want_empty = gen_ui.esc(node.get("empty_glyph") or "")
            want_lm    = str(node.get("label_mode") or "off").lower()
            if want_lm not in gen_ui.BAR_LABEL_MODES:
                F.add("R19 bar attributes",
                      f"'{nid}': label_mode {want_lm!r} is not a label mode, and the "
                      f"generator accepted it")
            check("R19 bar attributes", got_fill == want_fill,
                  f"'{nid}': bar_fill_glyph is {got_fill!r}, expected {want_fill!r}")
            check("R19 bar attributes", got_empty == want_empty,
                  f"'{nid}': bar_empty_glyph is {got_empty!r}, expected {want_empty!r}")
            # A bar that declared no label_mode carries the sentinel (in a block
            # that exists for another key) or nothing at all, and both mean OFF --
            # which is the value the painter resolves. The rule states the three
            # answers rather than demanding the enumerator, because demanding it
            # was the flat-emit shape.
            want_lm_c = gen_ui.BAR_LABEL_MODE_NAMES[gen_ui.BAR_LABEL_MODES[want_lm]]
            check("R19 bar attributes",
                  got_lm in (want_lm_c, "", "-1"),
                  f"'{nid}': bar_label_mode is {got_lm!r}; expected {want_lm_c!r} for "
                  f"label_mode {want_lm!r}, or the unset sentinel, or nothing")
        else:
            # C24 scopes the vocabulary to the two bar kinds, so on anything else
            # the three must reach the node at all. A glyph that survived onto a
            # button would be generated, compiled and never read.
            check("R19 bar attributes", got_fill == "" and got_empty == "",
                  f"'{nid}': a non-bar carries bar glyphs ({got_fill!r}, {got_empty!r}); "
                  f"C24 scopes them to progress_bar and seek_bar")
            check("R19 bar attributes", got_lm == "",
                  f"'{nid}': a non-bar carries bar_label_mode {got_lm!r}; C24 scopes "
                  f"it to progress_bar and seek_bar")

        # R20 conversation window (C25). Two halves, and the second was the one
        # that mattered while the fields were emitted on EVERY node: a non-window
        # carrying a stale value would be generated, compiled and never read. The
        # pair is in the declaration blocks now -- only a window that declared one
        # has it at all -- so the second half checks that no other kind reaches a
        # value it never declared.
        is_conv = KIND.get(str(node.get("type", ""))) == "IRCITUI_CONVERSATION_WINDOW"
        raw_pct = emitted_member(idx, "bubble_max_pct")
        raw_cap = emitted_member(idx, "bubble_collapse_lines")
        raw_gut = emitted_member(idx, "bubble_avatar_gutter_cols")
        got_pct = int(raw_pct) if raw_pct.isdigit() else 0
        got_cap = int(raw_cap) if raw_cap.isdigit() else 0
        got_gut = int(raw_gut) if raw_gut.isdigit() else 0
        if is_conv:
            want_pct = int(node.get("bubble_max_width_percent") or 0)
            want_cap = int(node.get("bubble_collapse_lines") or 0)
            want_gut = int(node.get("bubble_avatar_gutter_cols") or 0)
            check("R20 conversation window", got_pct == want_pct,
                  f"'{nid}': bubble_max_pct is {got_pct}, expected {want_pct}")
            check("R20 conversation window", got_cap == want_cap,
                  f"'{nid}': bubble_collapse_lines is {got_cap}, expected {want_cap}")
            check("R20 conversation window", got_gut == want_gut,
                  f"'{nid}': bubble_avatar_gutter_cols is {got_gut}, expected {want_gut}")
        else:
            check("R20 conversation window", got_pct == 0 and got_cap == 0 and got_gut == 0,
                  f"'{nid}': a node that is not a conversation_window carries its "
                  f"configuration ({got_pct}, {got_cap}, {got_gut}); C25 scopes all "
                  f"three keys to conversation_window")

        # R11 structure — the child slice must be exactly this node's children
        want_kids = [c.get("id") for c in effective_children(node)]
        got_kids = [g.ident(k) for k in g.kids(idx)]
        check("R11 structure", got_kids == want_kids,
              f"'{nid}': children {got_kids}, expected {want_kids}")

    # ---- R3, the template table ------------------------------------------
    got_stems = [g.ident(t) for t in g.templates]
    for stem, tpl in load_inputs(app_path)[1]:
        root_id = tpl.get("id") or stem
        check("R3 external references", root_id in got_stems,
              f"template '{root_id}' was loaded from {stem} but is not in templates[]")
    check("R3 external references", len(g.templates) == len(tpl_stems),
          f"{len(tpl_stems)} template file(s) referenced, templates[] holds {len(g.templates)}")

    # R16, the other direction: an emitted registry no node points at is dead
    # data in the generated file -- a whole array kept alive by nothing, which is
    # what a node renamed out from under its registry would leave behind.
    referenced = set()
    for i in g.nodes:
        blk = g.blocks.get(g.decl_of.get(i, ""), {}).get("members", {})
        if "view_types" in blk:
            referenced.add(blk["view_types"].strip())
        else:
            referenced.add(g.field(i, r"\.view_types=(\w+)", str, None))
    for name in g.registry_names:
        check("R16 view types", name in referenced,
              f"the registry {name} is emitted but no node points at it")

    # ---- R22 kind order ----------------------------------------------------
    # The schema's kind table IS the C enum's order and is append-only: the
    # enumerator is serialised into the model above and compared here, so a kind
    # inserted anywhere but the end renumbers every kind after it. The schema
    # covers the declarable kinds; the enum may carry more, which is what the
    # message viewport is -- a kind the engine synthesizes and no layout writes.
    root = Path(__file__).resolve().parents[1]
    enum_src = (root / "include/ircituilib/ircit_ui_kind_enum.h").read_text()
    enum_body = enum_src[enum_src.index("typedef enum {"):enum_src.index("} IrcitUiKind;")]
    enum_kinds = re.findall(r"^[ \t]+(IRCITUI_[A-Z_]+)(?=[ \t]*(?:=[^,]*)?,|[ \t]*$)",
                            enum_body, re.M)
    schema_kinds = list(SCHEMA.KIND.values())
    check("R22 kind order", enum_kinds[:len(schema_kinds)] == schema_kinds,
          f"the schema declares {schema_kinds}; ircit_ui_kind_enum.h begins {enum_kinds[:len(schema_kinds)]}")

    # ---- R23 types current -------------------------------------------------
    # The generated types are what the type generator emits now, and every
    # migrated member is declared in the type of every kind that may carry it.
    # This replaces "the schema agrees with a hand-written type": there is no
    # longer a hand-written type to agree with.
    types_path = root / "src/include/generated/ui_decl_types.h"
    types_src = types_path.read_text()
    want_types = "\n".join(gen_decl_types.emit()) + "\n"
    # Both sides carry provenance lines -- the file the one its run wrote, the
    # string below the one THIS run just stamped -- so they are stripped before
    # the comparison. What R23 is about is the body: a header that is stale, or
    # emitted by a generator that has since changed, and neither shows up as a
    # difference in the two stamps.
    check("R23 decl types", provenance.strip(types_src) == provenance.strip(want_types),
          f"{types_path} is not what tools/gen_decl_types.py emits")
    access_path = root / "src/include/generated/ui_decl_access.h"
    access_src = access_path.read_text()
    want_access = "\n".join(gen_decl_types.emit_access()) + "\n"
    check("R23 decl types", provenance.strip(access_src) == provenance.strip(want_access),
          f"{access_path} is not what tools/gen_decl_types.py emits")
    # Every accessor a reader may call exists, and it handles every kind the schema
    # scopes its key to: a scope edited in the schema but not regenerated would
    # otherwise leave a call that falls through to a different kind's block.
    bodies = {}
    for m in re.finditer(r"static inline [\w *]+\n(\w+)\(const IrcitUiNode \*node_ptr\)\n\{(.*?)\n\}",
                         access_src, re.S):
        bodies[m.group(1)] = m.group(2)
    for key in SCHEMA.DECLARATIVE_KEYS:
        for member in (key["c_member"], key.get("count_member")):
            if not member:
                continue
            fn = "IrcitUiDecl" + "".join(x[:1].upper() + x[1:] for x in member.split("_"))
            check("R23 decl types", fn in bodies, f"the accessor {fn} is not in ui_decl_access.h")
            for kind in key["kinds"]:
                check("R23 decl types", f"== {SCHEMA.KIND[kind]}" in bodies.get(fn, ""),
                      f"{fn} does not handle {kind}, which the schema scopes it to")
    for key in SCHEMA.DECLARATIVE_KEYS:
        for kind in key["kinds"]:
            tname = SCHEMA.decl_type_name(kind)
            m = re.search(rf"typedef struct {tname} \{{(.*?)\}}", types_src, re.S)
            check("R23 decl types", bool(m and re.search(rf"\b{key['c_member']}\b", m.group(1))),
                  f"{tname} does not declare {key['c_member']}, which {kind} may carry")

    # ---- R24 the block pool ------------------------------------------------
    for name, blk in g.blocks.items():
        check("R24 block pool", blk["sharers"],
              f"{name} is emitted but no node points at it")
        kinds = {g.field(i, r"\.kind=(IRCITUI_\w+)", str, None) for i in blk["sharers"]}
        check("R24 block pool", len(kinds) == 1,
              f"{name} is pointed at by {sorted(kinds)}: a block's type is one kind's")
        for i in blk["sharers"]:
            want = SCHEMA.decl_type_name(SCHEMA.YAML_KIND[g.field(i, r"\.kind=(IRCITUI_\w+)", str, "")])
            check("R24 block pool", blk["type"] == want,
                  f"'{g.ident(i)}' points at {name}, which is a {blk['type']} and not a {want}")
    order = [min(b["sharers"]) for b in g.blocks.values() if b["sharers"]]
    check("R24 block pool", order == sorted(order),
          "the blocks are not in first-seen pool order, so a regeneration would reorder the file")
    seen_sig = {}
    for name, blk in g.blocks.items():
        sig = (blk["type"], tuple(sorted(blk["members"].items())))
        if sig in seen_sig:
            check("R24 block pool", False,
                  f"{name} and {seen_sig[sig]} declare the same thing and should be one block")
        seen_sig[sig] = name

    # ---- R25 the agreement (IRCITUILIB_GENERATOR_REFACTOR_V1.md §2.5) -------
    # While a migrated member is still emitted flat, the block must carry the same
    # value. This is the rule that holds the two paths together until S3 takes the
    # flat member away, and it is the one that would have caught a block reading
    # "rows.yaml" where the node reads the stem "rows".
    for idx, name in g.decl_of.items():
        blk = g.blocks.get(name, {}).get("members", {})
        for member in SCHEMA.MIGRATED_MEMBERS:
            # The value is the last member on a line, so it carries the closing
            # brace of the initialiser with it.
            if SCHEMA.key_of_member(member)["family"] in SCHEMA.REMOVED_FAMILIES:
                continue          # nothing flat left to agree with
            flat = g.field(idx, rf"\.{member}=([^,\n]+)", str, None)
            if flat is None or member not in blk:
                continue
            flat = flat.strip().rstrip("}").strip()
            # A member the block carries as its SENTINEL was not declared -- the
            # sentinel exists precisely so a legal zero is not read as "absent" --
            # so the node's own value is the answer and there is nothing to agree.
            if blk[member].strip() == SCHEMA.key_of_member(member).get("sentinel"):
                continue
            check("R25 agreement", flat == blk[member].strip(),
                  f"'{g.ident(idx)}': {member} is {flat.strip()} on the node and "
                  f"{blk[member].strip()} in {name}")
    for node in pool:
        idx = by_id.get(node.get("id"))
        kind = str(node.get("type", "frame"))
        if kind not in SCHEMA.KIND:
            kind = "frame"
        declared = {k["c_member"] for k in SCHEMA.keys_for(kind)
                    if node.get(k["yaml_key"]) is not None}
        if idx is None:
            continue
        blk = g.blocks.get(g.decl_of.get(idx, ""), {}).get("members", {})
        check("R25 agreement", not (declared - set(blk)),
              f"'{node.get('id')}': the layout declares {sorted(declared - set(blk))}, "
              f"which its block does not carry")
        check("R25 agreement", declared or idx not in g.decl_of,
              f"'{node.get('id')}': points at a block though the layout declares nothing")

    # ---- R12 identity ------------------------------------------------------
    size = max(1, len(pool) * FNV_SIZE_FACTOR)
    # Recompute the table from the YAML with the same hash and the same insertion
    # order the generator uses, then compare slot for slot. A match means every id
    # in the pool resolves to the node that declared it.
    want_table = [SLOT_NONE_C] * size
    for i, node in enumerate(pool):
        nid = node.get("id")
        if nid is None or nid not in by_id:
            continue
        k = id_hash(nid) % size
        while want_table[k] != SLOT_NONE_C:
            k = (k + 1) % size
        want_table[k] = by_id[nid]
    check("R12 identity", len(g.slots) == size,
          f"id table holds {len(g.slots)} slots, expected {size} for {len(pool)} nodes")
    if len(g.slots) == size:
        for k, (w, got) in enumerate(zip(want_table, g.slots)):
            check("R12 identity", w == got,
                  f"slot {k} holds {got}, expected {w} "
                  f"(ids must resolve to the node that declared them)")
    dupes = [i for i in set(yaml_ids) if yaml_ids.count(i) > 1]
    check("R12 identity", not dupes, f"duplicate ids in the pool: {sorted(dupes)}")

    # ---- R13 soundness (no YAML needed) ------------------------------------
    #
    # The range checks come first and the walks are gated on them. A walk that
    # follows an out-of-range index dereferences a node that does not exist, and
    # the honest report is "this index is out of range", not a traceback: a
    # corrupt file is exactly when this rule matters most.
    n = g.pool_size
    bad_refs = sorted({r for r in g.child_refs if not 0 <= r < n})
    check("R13 soundness", not bad_refs,
          f"child_refs name pool entries outside the {n}-entry pool: {bad_refs}")
    root_ok = g.root in g.nodes
    check("R13 soundness", root_ok,
          f"desc.root names pool[{g.root}], which is not a pool entry (pool holds {n})")
    bad_slots = []
    for idx in g.nodes:
        off = g.field(idx, r"\.children=&s_ui_model\.child_refs\[(\d+)\]", int, 0) or 0
        cnt = g.field(idx, r"\.n_children=(\d+)", int, 0)
        if off + cnt > len(g.child_refs):
            bad_slots.append((g.ident(idx), off, cnt))
    check("R13 soundness", not bad_slots,
          f"child slices run past the {len(g.child_refs)}-entry table: {bad_slots}")
    bad_tpl = sorted({t for t in g.templates if t not in g.nodes})
    check("R13 soundness", not bad_tpl, f"templates[] names non-entries: {bad_tpl}")
    if not (bad_refs or bad_slots or bad_tpl or not root_ok):
        seen, stack, reentered = set(), [g.root], []
        while stack:
            i = stack.pop()
            if i in seen:
                reentered.append(i); continue
            seen.add(i)
            stack.extend(g.kids(i))
        tpl_seen = set()
        for t in g.templates:
            st = [t]
            while st:
                i = st.pop()
                if i in tpl_seen:
                    continue
                tpl_seen.add(i)
                st.extend(g.kids(i))
        orphaned = set(g.nodes) - seen - tpl_seen
        check("R13 soundness", not orphaned, f"orphaned nodes: {sorted(orphaned)}")
        check("R13 soundness", not reentered,
              f"cycle re-entered at pool[{reentered[0]}]" if reentered else "")

    # ---- the verified stamp ------------------------------------------------
    # Written for EVERY invocation, passing or failing, because the artefact's
    # job here is to say when it was last looked at and what was found -- a
    # reader with only the file in front of them cannot tell a current artefact
    # from one nothing has checked, and the exit status never travels with it.
    #
    # The verdict is the one the report below prints, read here rather than
    # recomputed, so a FAILED stamp and a failure report cannot disagree. The
    # write happens last, after every rule has read the text: a stamp added
    # before the comparisons would be one more difference for them to explain.
    passed = not F.by_rule
    for stamped in (gen_path, types_path, access_path):
        if stamped.is_file():
            stamped.write_text(provenance.with_verified(stamped.read_text(),
                                                        provenance.VERIFIER, passed))

    return F.report()


if __name__ == "__main__":
    sys.exit(main(sys.argv))
