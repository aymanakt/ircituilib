#!/usr/bin/env python3
"""YAML → the generated model: declarations, then the bind table."""
from __future__ import annotations
import sys
from pathlib import Path

try:
    import yaml
except ImportError:
    raise SystemExit("pip install pyyaml")

# The kind table and the declarative vocabulary are the schema's: it is the one
# place a key or a kind's scope is declared (IRCITUILIB_GENERATOR_REFACTOR_V1.md
# §2.2), and post_generation_verifier.py asserts this file's derivations below
# still agree with it rather than trusting the two to stay in step by hand.
sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
import kind_schema as SCHEMA  # noqa: E402
import provenance  # noqa: E402

KIND = dict(SCHEMA.KIND)


def decl_key_names(kind, type_name):
    """The schema's keys of one type that a kind may carry, in table order.

    The vocabulary below is DERIVED rather than restated: a key added to the
    schema is picked up here, and a key renamed there cannot leave this file
    spelling it the old way."""
    return tuple(k["yaml_key"] for k in SCHEMA.keys_for(kind) if k["type"] == type_name)


GRAV = {
    "start": "IRCITUI_G_START", "end": "IRCITUI_G_END", "top": "IRCITUI_G_TOP",
    "bottom": "IRCITUI_G_BOTTOM", "center": "IRCITUI_G_CENTER", "fill": "IRCITUI_G_FILL",
}

# The kinds that can be folded, and so the only kinds the collapse vocabulary
# applies to. Mirrors IrcitUiIsContainer() in src/ui_engine.c and the
# Containers rule in tools/layout-grammar.ebnf: a container is the only thing
# with an extent of its own to shrink, and the only thing whose children can be
# hidden behind it.
CONTAINER_KINDS = tuple(KIND[k] for k in SCHEMA.CONTAINER_KINDS)

# The three glyph properties a container may declare, in the order they are
# reported back when one carries the wrong number of glyphs. The schema's, via
# the kind it scopes them to.
GLYPH_KEYS = decl_key_names("linear", SCHEMA.GLYPH)

# The collapse vocabulary, in the order it is reported back when one of them
# lands on a kind that cannot carry it.
COLLAPSE_KEYS = ("collapsible", "collapsed") + GLYPH_KEYS

# C24's subject: the two kinds that draw a value as a filled track, the glyph
# keys they alone read, and the label modes the enum declares. The schema names
# the kinds and the keys; the modes come from it too, and R19 still asserts the
# tuple equals ircit_ui_bar_label_mode_enum.h rather than trusting it to.
BARS = tuple(KIND[b] for b in SCHEMA.BARS)
BAR_GLYPH_KEYS = decl_key_names("progress_bar", SCHEMA.GLYPH)
# The enumerator spellings, in declaration order, so this table IS
# ircit_ui_bar_label_mode_enum.h written out -- from the schema, which is where
# the modes and their spellings are declared.
BAR_LABEL_MODE_NAMES = tuple(SCHEMA.enum_values("label_mode").values())
# The YAML spellings, DERIVED from the enumerators above so a rename cannot leave
# the two spellings of one mode disagreeing. `IRCITUI_BAR_LABEL_ADJACENT` becomes
# the key `adjacent`, which is what C24's production accepts.
BAR_LABEL_MODES = {n.replace("IRCITUI_BAR_LABEL_", "").lower(): i
                   for i, n in enumerate(BAR_LABEL_MODE_NAMES)}

COLOR = {
    "black": 0, "red": 1, "green": 2, "yellow": 3,
    "blue": 4, "magenta": 5, "cyan": 6, "white": 7,
}

# The palette slots, one vocabulary with ircit_ui_palette_slot_enum.h and with
# validate_layout.py's copy. Three places hold the spellings and
# post_generation_verifier.py's R18 asserts the C enum and this set agree, so a slot
# renamed in the enum without being renamed here is caught rather than shipped.
SLOTS = {
    "surface", "panel", "text", "text_muted",
    "text_error", "edit_active", "overlay_snackbar", "overlay_action",
    "accent",
}
SIDES = ("top", "right", "bottom", "left")

# C20's subject in YAML spellings. The C enum spellings are in KIND, and the two
# are not interchangeable: parse() has both an id/type string and n.kind, and
# comparing the wrong one silently never matches.
YAML_TOGGLES = set(SCHEMA.TOGGLES)
CFIELDS = (
    ("constraintTop_toTopOf", "c_top_top"),
    ("constraintTop_toBottomOf", "c_top_bot"),
    ("constraintBottom_toTopOf", "c_bot_top"),
    ("constraintBottom_toBottomOf", "c_bot_bot"),
    ("constraintStart_toStartOf", "c_start_start"),
    ("constraintStart_toEndOf", "c_start_end"),
    ("constraintEnd_toStartOf", "c_end_start"),
    ("constraintEnd_toEndOf", "c_end_end"),
)

# The lifecycle listener the framework binds to every node it emits, and the
# default implementation that listener is bound to.
#
# These are framework defaults rather than application choices, so they are
# emitted into IrcitUiCommonState.interaction_defaults and not into the node's
# own interactions_user block, which is where a handler named in YAML lands.
#
# Each name is one shared master router: a single entry point per listener that
# dispatches on the node's kind internally. That is why the table is keyed by
# listener alone and carries no kind, and why it replaces the per-kind
# IrcitUiButtonInput / IrcitUiEditInput / IrcitUiEmojiInput /
# IrcitUiRadioItemInput / IrcitUiCheckItemInput binding that used to sit inline
# in parse().
#
# The implementations live in src/ui_defaults_router.c, which is compiled into
# the library alongside this file's output, so the emitted names always resolve.
DEFAULT_LISTENERS = (
    ("on_key_down", "IrcitUiDefaultOnKeyDown"),
    ("on_click", "IrcitUiDefaultOnClick"),
    ("on_scroll", "IrcitUiDefaultOnScroll"),
    ("on_draw", "IrcitUiDefaultOnDraw"),
    ("on_resize", "IrcitUiDefaultOnResize"),
    ("on_focus_changed", "IrcitUiDefaultOnFocusChanged"),
    ("on_inflate", "IrcitUiDefaultOnInflate"),
    ("on_deflate", "IrcitUiDefaultOnDeflate"),
)


def default_listeners_init():
    """Render one node's interaction_defaults initialiser.

    Emits only the listener block: the other members of
    IrcitUiInteractionState (user, ebuf, caret) are per-node storage, not
    framework defaults, and are left to be zero-initialised."""
    bound = ", ".join(f".{field}={fn}" for field, fn in DEFAULT_LISTENERS)
    return "{ .lifecycle_listeners = { " + bound + " } }"


# The listeners a template may name, in the order they are declared in
# IrcitUiInteractionState. The YAML key is the field name, so a template writes
# `on_inflate: IrcitUiPopupFocusOnOpen` and the generator emits
# `.on_inflate=IrcitUiPopupFocusOnOpen` into the node's own interactions_user
# block.
#
# These are the application's choices, not the framework's, which is why they
# land in a different block from DEFAULT_LISTENERS above. A template that names
# none leaves every slot null, and a null slot is how a node says it has no
# opinion: the dispatcher runs interactions_user first and falls through to
# interaction_defaults when a listener is absent or returns 0.
#
# The list is spelled out rather than derived from the C type because the
# generator does not parse headers: a listener added to IrcitUiInteractionState
# without being added here would be silently unreachable from YAML, so the two
# are kept in step by hand and the compiler catches a name that drifts.
#
# THIS TUPLE IS THE SINGLE SOURCE FOR THE VOCABULARY. tools/validate_layout.py
# imports it rather than keeping its own copy -- and used to keep one, which is why
# `--validate-layout` refused seven of these eight keys while this generator
# accepted them. A key the generator emits and the referee rejects is not a
# theoretical fault: it is the children_selectable defect class
# (IRCITUILIB_LAYOUT_GENERATION.md 18.1), and it had recurred here unnoticed
# because `on_inflate` is the only one of the eight any layout uses.
USER_LISTENERS = (
    "on_key_down",
    "on_click",
    "on_scroll",
    "on_draw",
    "on_resize",
    "on_focus_changed",
    "on_inflate",
    "on_deflate",
)

# The listeners that live in the OTHER block of IrcitUiInteractionState, and are
# therefore emitted somewhere else. Same rule as above -- the YAML key is the field
# name -- but these land in .post_interceptor_listeners, which is the block the edit
# field's fast lane reads at stage 3, after the buffer has already changed.
#
# A list of its own rather than a suffix on USER_LISTENERS, because the two are
# different structs: a name written into the wrong block is a compile error, which
# is what makes the split safe to keep by hand.
#
# on_scrolled IS NOT HERE, deliberately. It is the block's other member and nothing
# in the tree calls it -- no producer, no reader, no test. Making it nameable from
# YAML would ship a key that binds a listener the framework never invokes, which is
# the fault SS4.11 found in on_scroll and on_resize and closed by writing their
# producers, not by declaring them.
POST_INTERCEPTOR_LISTENERS = (
    "on_text_changed",
)


def user_listeners_init(n):
    """Render one node's interactions_user initialiser.

    Only the listeners the template named are listed. The other members of
    IrcitUiInteractionState - user, ebuf, caret - are per-node storage rather
    than bindings, so they are set to zero explicitly and the listener block is
    omitted entirely when the template asked for nothing.

    The two listener blocks are rendered independently, so a template may name a
    lifecycle listener, a post-interceptor one, both, or neither, and the emitted
    literal carries exactly the blocks it named. A block the template did not name
    is omitted rather than emitted empty: a null slot is how a node says it has no
    opinion, and { 0 } says the same thing less legibly."""
    lifecycle = ", ".join(f".{field}={fn}" for field, fn in n.listeners.items() if fn)
    post      = ", ".join(f".{field}={fn}" for field, fn in n.post_listeners.items() if fn)
    blocks    = []
    if lifecycle:
        blocks.append(".lifecycle_listeners = { " + lifecycle + " }")
    if post:
        blocks.append(".post_interceptor_listeners = { " + post + " }")
    if not blocks:
        return "{ .user=0, .ebuf={0}, .caret=0 }"
    return "{ .user=0, .ebuf={0}, .caret=0, " + ", ".join(blocks) + " }"


def dim(v):
    """Parse one axis of a size request into (mode, cells, percent).

    Three values because IrcitUiDim carries cells and percent as separate
    members; the mode decides which one is read, so exactly one of the two is
    ever non-zero.

    A percentage is written "NN%" in the template and is resolved at layout
    time against whatever the parent offers along that axis. It is rejected
    outside 1-100 here rather than clamped, so a typo is a build failure like
    the out-of-range group index below rather than a silently degenerate
    layout. Zero is refused for the same reason: a node given no share of its
    parent is a mistake, and "one cell" is a poor way to spell it."""
    if v is None or v == "wrap_content":
        return "IRCITUI_SIZE_WRAP", 0, 0
    if v == "match_parent":
        return "IRCITUI_SIZE_MATCH", 0, 0
    s = str(v).strip()
    if s.endswith("%"):
        try:
            pct = int(s[:-1].strip())
        except ValueError:
            raise SystemExit(f"layout size is neither a cell count nor a percentage: {v!r}")
        if not 1 <= pct <= 100:
            raise SystemExit(f"layout percentage out of range: {v!r}; valid range is 1%-100%")
        return "IRCITUI_SIZE_PERCENT", 0, pct
    return "IRCITUI_SIZE_EXACT", int(v), 0


def esc(s):
    """Escape a template string for a C string literal.

    Used for every member emitted as a literal, so the escaping rule lives in
    one place: a backslash is escaped before the quote, or the quote would be
    escaped twice."""
    return str(s).replace("\\", "\\\\").replace('"', '\\"')


def ccomment(s):
    """Make a string safe to place inside a C block comment.

    Node ids reach here from the layout file. The grammar constrains them to
    Ident, but the generator does not enforce that, and an id carrying the
    comment terminator would end the comment early and emit the rest of itself
    as code. A newline would likewise break the one-line-per-entry layout."""
    return str(s).replace("*/", "").replace("\n", " ").replace("\r", " ")


# FNV-1a, 32-bit. sIdHash() in src/ui_engine.c computes the same value with the
# same two constants, and the two must be changed together: a mismatch would not
# fail loudly, it would simply stop finding ids that are present.
SLOT_NONE = 0xFFFFFFFF
FNV_INIT = 0x811C9DC5
FNV_PRIME = 0x01000193


def id_hash(s):
    """The hash the id index is built on and looked up with."""
    h = FNV_INIT
    for b in str(s).encode("utf-8"):
        h ^= b
        h = (h * FNV_PRIME) & 0xFFFFFFFF
    return h


# The node attribute each declarative key is parsed into. Five differ from the C
# member's spelling, and naming them here is what lets a block carry the SAME
# parsed value the flat member does rather than a second derivation of it -- the
# property §2.5's agreement rule measures while both are emitted.
NODE_ATTR = {
    "orientation": "orient",
    "bar_fill_glyph": "fill_glyph",
    "bar_empty_glyph": "empty_glyph",
    "bar_label_mode": "label_mode",
    "sticky_header_type": "sticky_headers",
}

# The members parse() escaped when it read them, so a block must not escape them
# a second time. The rest are raw strings and are escaped once, here.
PRE_ESCAPED = {"indicator_open", "indicator_close", "collapse_fill",
               "fill_glyph", "empty_glyph"}


def decl_text(n, key):
    """The C text one declared key contributes to a block.

    The value is the node's own parsed one, so the block and the flat member can
    only agree -- there is one parse and two destinations."""
    member = key["c_member"]
    value = getattr(n, NODE_ATTR.get(member, member))
    if member == "orientation":
        return value                       # already the enumerator's spelling
    if member == "bar_label_mode":
        return BAR_LABEL_MODE_NAMES[value]
    if key["type"] in (SCHEMA.GLYPH, SCHEMA.STRING):
        return f'"{value if member in PRE_ESCAPED else esc(value)}"'
    return str(int(value))


def declared_attrs(n, d):
    """What a node declares: (scalar members, table members).

    A key the layout did not write is ABSENT, which is what a NULL block means:
    the widget's own default applies. Two members are the exception and carry a
    sentinel instead -- mark_fg and sticky_header_type both have a LEGAL zero, so
    a block that omitted one would read zero as a value rather than as "not
    given", which is the trap the emitter's comment about sticky_header_type has
    recorded since view types were added."""
    scalars, tables = {}, {}
    keys = SCHEMA.keys_for(SCHEMA.YAML_KIND.get(n.kind, "frame"))
    for key in keys:
        if key["type"] == SCHEMA.TABLE:
            if key["yaml_key"] in d:
                tables[key["c_member"]] = list(getattr(n, key["c_member"]))
        elif key["yaml_key"] in d:
            scalars[key["c_member"]] = decl_text(n, key)
    # A sentinel is added only to a block that EXISTS. It is there so the accessor can
    # tell "declared the value its zero means" from "declared nothing", and a node that
    # declared nothing has no block for it to be added to -- giving it one would make
    # every untouched toggle carry a block (and the storage this refactor removes).
    if scalars or tables:
        for key in keys:
            if key.get("sentinel") and key["c_member"] not in scalars:
                scalars[key["c_member"]] = key["sentinel"]
    return scalars, tables


def decl_signature(kname, scalars, tables):
    """A node's declared set, as the key its block is pooled on.

    Only what the layout DECLARED, never a default it left out: a node that
    declares nothing gets no block at all, or the storage this refactor removes
    would return in a new spelling (§2.3).

    The KIND is part of the key, not just the declared set. Two kinds that
    declare the same thing -- a linear and a group, say, both `orientation` --
    have blocks of two DIFFERENT types, and C's types are distinct even when
    their members are identical: sharing one between them would be an aliasing
    violation, and the accessor's cast could no longer be checked by the
    compiler, which is the whole reason the type is per kind."""
    return (SCHEMA.decl_type_name(kname),
            tuple(sorted(scalars.items())),
            tuple((m, tuple((e["type_id"], e["template"]) for e in v))
                  for m, v in sorted(tables.items())))


def emit_decl_blocks(nodes):
    """The declaration blocks, one per distinct declared set, and their sharers.

    At FILE SCOPE and const, because a designated initialiser cannot carry an
    array and nothing ever writes through one: the node holds a borrowed pointer
    and never frees it, the same rule item_template and theme_class follow.

    Returns (lines, decl_of). decl_of maps a node's identity to the block it
    points at and to the names its table members were emitted under, so the pool
    emitter names what the block emitter defined rather than deriving it again."""
    order, index, decl_of = [], {}, {}
    for n in nodes:
        scalars, tables = n.declared
        if not scalars and not tables:
            continue
        kname = SCHEMA.YAML_KIND.get(n.kind, "frame")
        sig = decl_signature(kname, scalars, tables)
        if sig in index:
            order[index[sig]][3].append(n.id)
            decl_of[id(n)] = (order[index[sig]][0], order[index[sig]][4])
            continue
        name = f"s_decl_{n.ident}"
        arrays = {m: f"{name}_{m}" for m in tables}
        index[sig] = len(order)
        order.append((name, scalars, tables, [n.id], arrays, kname))
        decl_of[id(n)] = (name, arrays)
    L = []
    for name, scalars, tables, sharers, arrays, kname in order:
        L.append(f'/* The declarations of {ccomment(sharers[0])}'
                 + (f', and of {", ".join(ccomment(s) for s in sharers[1:])}'
                    f' -- {len(sharers)} nodes that declared the same thing' if len(sharers) > 1 else '')
                 + '.')
        L.append('   Const and shared: a block is never written through, because a value a host')
        L.append('   can retarget is written to the model\'s widget_state instead. */')
        for member, entries in tables.items():
            L.append(f'static const {SCHEMA.BY_C_MEMBER[member]["element"]} {arrays[member]}[] = {{')
            for e in entries:
                L.append(f'    {{ {e["type_id"]}, "{e["template"]}" }},'
                         f'  /* type {e["type_id"]} -> {ccomment(e["template"])} */')
            L.append('};')
        L.append(f'static const {SCHEMA.decl_type_name(kname)} {name} = {{')
        body = [f'.{m}={v}' for m, v in scalars.items()]
        body += [f'.{m}={arrays[m]}, .n_{m}={len(tables[m])}' for m in tables]
        L.append('    ' + ', '.join(body) + ' };')
        L.append('')
    return L, decl_of


def build_id_index(nodes, slot_of):
    """Index every node's id, and prove the index's worst case.

    Returns (table, max_probe). The table is open-addressed with linear probing:
    a slot holds a pool index, or SLOT_NONE where it is empty.

    `max_probe` is the longest run of consecutive occupied slots. That single
    number bounds both a hit - an id present in the table - and a miss, because a
    miss stops at the first empty slot, and no empty slot inside a run of that
    length exists. Emitting it means the reader does not have to assume the hash
    behaves well; it has a bound it can loop on.

    It is the longest *run*, not the most collisions any one insert met. Those are
    different numbers, and only the run bounds a lookup: an item placed into an
    existing cluster at distance 0 still sits in a long cluster, and a miss for an
    id hashing to that cluster's head walks the whole thing. Measuring insert
    collisions understates the reader's worst case.

    The table is sized to twice the node count, i.e. load 0.5. That is where
    linear probing stops clustering badly; at 1.5N the same 44 ids give a longest
    run of 14 against 9 here, for 176 bytes.

    Insert order is the pool order, which is fixed, so the table is reproducible.
    """
    size = max(1, len(nodes) * 2)
    table = [SLOT_NONE] * size
    for n in nodes:
        k = id_hash(n.id) % size
        while table[k] != SLOT_NONE:
            k = (k + 1) % size
        table[k] = slot_of[id(n)]
    run = 0
    longest = 0
    for v in table:
        run = run + 1 if v != SLOT_NONE else 0
        if run > longest:
            longest = run
    return table, longest


def box4(v):
    if v is None:
        return [0, 0, 0, 0]
    if isinstance(v, int):
        return [v, v, v, v]
    if isinstance(v, list) and len(v) == 4:
        return [int(x) for x in v]
    return [0, 0, 0, 0]


def cident(s, n):
    raw = "".join(ch if ch.isalnum() else "_" for ch in (s or f"n{n}"))
    return f"n_{raw}_{n}"


class N:
    def __init__(self):
        self.ident = self.id = ""
        self.kind = "IRCITUI_FRAME"
        self.wm, self.wc, self.wp = "IRCITUI_SIZE_WRAP", 0, 0
        self.hm, self.hc, self.hp = "IRCITUI_SIZE_WRAP", 0, 0
        self.margin = [0, 0, 0, 0]
        self.padding = [0, 0, 0, 0]
        self.flags = ["IRCITUI_F_VISIBLE", "IRCITUI_F_ENABLED"]
        self.fg, self.bg, self.bdr = 7, 0, 7
        # C20. -1 is "not given", which the engine reads as "the mark takes
        # text_colour". It is NOT 0: black is a colour a mark may legitimately be,
        # so an unset marker has to be a value outside the 0-7 range rather than
        # the first colour in it. The three above take the opposite approach and
        # pay for it -- `bdr ? def.bdr : COLOR_WHITE` in sNodeFromDef means an
        # explicitly black border cannot be asked for.
        self.mark_fg = -1
        self.grav = "IRCITUI_G_START"
        self.lgrav = "IRCITUI_G_START"
        self.orient = "IRCITUI_VERT"
        self.cons = {f: "IRCITUI_UNSET" for _, f in CFIELDS}
        self.text = ""
        self.layout_template = ""
        self.listeners = {}
        self.post_listeners = {}
        self.columns = 0
        self.children = []
        self.is_collapsed = 0
        self.indicator_open = ""
        self.indicator_close = ""
        self.collapse_fill = ""
        # C24's vocabulary, on the same terms as the collapse glyphs above: left
        # empty when the template named none, and the painter then supplies the
        # default pair. label_mode is an INT because it reaches the node as an
        # IrcitUiBarLabelMode, and 0 (off) is the meaningful default -- a bar with
        # no label -- so a node that never mentions the key needs no sentinel and
        # the emitted field is written unconditionally.
        self.fill_glyph = ""
        self.empty_glyph = ""
        self.label_mode = 0
        # A recycler's row template. Carried as the file reference until gather()
        # resolves it to a template id, exactly as layout_template is.
        self.item_template = ""
        # A recycler's view-type registry: [{"type_id": int, "template": ref}].
        # The template is carried as the file reference until gather() resolves it
        # to a template id, exactly as item_template is. Empty means homogeneous.
        self.view_types = []
        # The type_id whose rows are section dividers and are also pinned to the
        # top of the viewport. -1 is "off", and it is NEGATIVE rather than 0 for
        # C20's reason: 0 is a legitimate type_id, so an unset marker sharing a
        # value with a real one would make pinning type 0 unaskable.
        self.sticky_headers = -1
        self.max_height = 0
        self.row_height = 0
        self.bubble_max_pct = 0
        self.bubble_collapse_lines = 0
        self.bubble_avatar_gutter_cols = 0
        # C23. Empty is "not given", and it is EMPTY rather than a slot name
        # because there is no slot that means "no slot": every enumerator is a
        # real role, so a default sharing a value with one of them would make that
        # role unaskable -- the trap C20 and sticky_headers both document.
        self.theme_class = ""
        # The declarative keys this layout node wrote, as (scalars, tables), and
        # the raw mapping they were read from. Both are filled by
        # declared_attrs(), after template discovery -- see the note at its call
        # site in main().
        self.declared = ({}, {})
        self.raw = {}


def parse(d, seq, siblings_out):
    n = N()
    n.id = str(d.get("id", d.get("type", "w")))
    n.ident = cident(n.id, seq[0]); seq[0] += 1
    n.kind = KIND.get(str(d.get("type", "frame")), "IRCITUI_FRAME")
    n.wm, n.wc, n.wp = dim(d.get("layout_width"))
    n.hm, n.hc, n.hp = dim(d.get("layout_height"))
    n.margin = box4(d.get("margin"))
    n.padding = box4(d.get("padding"))
    if d.get("clickable"):
        n.flags.append("IRCITUI_F_CLICKABLE")
    if d.get("children_selectable"):
        n.flags.append("IRCITUI_F_CHILDREN_SELECTABLE")
    vis = d.get("visibility", "visible")
    if vis in ("gone", "invisible", False):
        n.flags = [f for f in n.flags if f != "IRCITUI_F_VISIBLE"]
    if d.get("enabled") is False:
        n.flags = [f for f in n.flags if f != "IRCITUI_F_ENABLED"]
    n.fg = COLOR.get(str(d.get("text_colour", "white")).lower(), 7)
    n.bg = COLOR.get(str(d.get("bg_colour", "black")).lower(), 0)
    n.bdr = COLOR.get(str(d.get("border_colour", "white")).lower(), 7)
    # C20, and enforced here as well as in the validator for the reason C15 and
    # C16 are: the generator is the path every build takes, so it does not rely on
    # the validator having been run. A mark_colour on a kind with no mark is
    # refused rather than dropped, because silently dropping it is how a template
    # ends up asking for something it never gets.
    if "mark_colour" in d:
        if str(d.get("type", "frame")) not in YAML_TOGGLES:
            raise SystemExit(
                f"'{n.id}': mark_colour only allowed on a toggle (C20): "
                f"{', '.join(sorted(YAML_TOGGLES))} -- got {d.get('type')}")
        n.mark_fg = COLOR.get(str(d["mark_colour"]).lower(), -1)
        if n.mark_fg < 0:
            raise SystemExit(
                f"'{n.id}': unknown mark_colour {d['mark_colour']!r} (C20) -- "
                f"one of {', '.join(sorted(COLOR))}")
    # C23, enforced here as well as in the validator for C15's reason: the generator
    # is the path every build takes. Refused rather than dropped, and refused rather
    # than passed through as free text, because a name outside the slot vocabulary
    # would set a class no slot resolves -- a node that silently keeps its own
    # colours while the layout says otherwise.
    if "theme_class" in d:
        n.theme_class = str(d["theme_class"])
        if n.theme_class not in SLOTS:
            raise SystemExit(
                f"'{n.id}': theme_class {n.theme_class!r} is not a palette slot (C23) -- "
                f"one of {', '.join(sorted(SLOTS))}")
        clash = [k for k in ("text_colour", "bg_colour") if k in d]
        if clash:
            raise SystemExit(
                f"'{n.id}': theme_class supplies the surface, so "
                f"{', '.join(clash)} would be ignored (C23)")
    if d.get("border") is True or str(d.get("border", "")).lower() == "true":
        n.flags.append("IRCITUI_F_BORDER")
    n.grav = GRAV.get(str(d.get("gravity", "start")), "IRCITUI_G_START")
    n.lgrav = GRAV.get(str(d.get("layout_gravity", "start")), "IRCITUI_G_START")
    n.orient = "IRCITUI_HORIZ" if str(d.get("orientation", "vertical")) == "horizontal" else "IRCITUI_VERT"
    n.text = esc(d.get("text", ""))
    n.layout_template = str(d.get("layout_template") or "")
    n.item_template = str(d.get("item_template") or "")
    # C21, enforced here as well as in the validator for the reason C15, C16 and
    # C20 are: the generator is the path every build takes, so it does not rely on
    # the validator having been run. Each refusal below is the validator's own
    # message, so a template that fails one fails the other identically and a
    # reader never has to work out which of the two is stricter.
    raw_view_types = d.get("view_types")
    if raw_view_types is not None:
        if str(d.get("type", "frame")) != "recycler":
            raise SystemExit(
                f"'{n.id}': view_types only allowed on recycler (C21) -- got {d.get('type')}")
        if n.item_template:
            raise SystemExit(
                f"recycler '{n.id}': item_template and view_types are mutually "
                f"exclusive (C21) -- they both answer what shape a row is, use one")
        if not isinstance(raw_view_types, list) or not raw_view_types:
            raise SystemExit(
                f"recycler '{n.id}': view_types must be a non-empty list (C21) -- an "
                f"empty registry is the homogeneous case, which item_template spells")
        seen_types = set()
        for entry in raw_view_types:
            if not isinstance(entry, dict):
                raise SystemExit(
                    f"recycler '{n.id}': each view_types entry must be a mapping of "
                    f"type_id and template (C21)")
            extra = sorted(k for k in entry if k not in ("type_id", "template"))
            if extra:
                raise SystemExit(
                    f"recycler '{n.id}': view_types entry has unknown key(s) {extra}")
            tid = entry.get("type_id")
            # bool is an int in Python and is not a type tag here.
            if isinstance(tid, bool) or not isinstance(tid, int) or tid < 0:
                raise SystemExit(
                    f"recycler '{n.id}': view_types type_id must be a non-negative "
                    f"integer, got {tid!r}")
            if tid in seen_types:
                raise SystemExit(
                    f"recycler '{n.id}': view_types type_id {tid} is declared twice "
                    f"(C21) -- which entry wins would be an implementation detail")
            seen_types.add(tid)
            tpl = entry.get("template")
            if not tpl or not isinstance(tpl, str):
                raise SystemExit(
                    f"recycler '{n.id}': view_types entry {tid!r} needs a template")
            n.view_types.append({"type_id": tid, "template": tpl})
    # C22, enforced here as well as in the validator for the reason C15/C16/C21
    # are: the generator is the path every build takes.
    if "sticky_headers" in d:
        sid = d.get("sticky_headers")
        if isinstance(sid, bool) or not isinstance(sid, int) or sid < 0:
            raise SystemExit(
                f"'{n.id}': sticky_headers must be a non-negative type_id, got {sid!r} "
                f"(C22) -- it names a row type, it is not a flag")
        if str(d.get("type", "frame")) != "recycler":
            raise SystemExit(
                f"'{n.id}': sticky_headers only allowed on recycler (C22) -- got {d.get('type')}")
        declared = {e["type_id"] for e in n.view_types}
        if not n.view_types:
            raise SystemExit(
                f"recycler '{n.id}': sticky_headers needs view_types (C22) -- it names "
                f"a type_id, and without a registry there is no id space to name one in")
        if sid not in declared:
            raise SystemExit(
                f"recycler '{n.id}': sticky_headers type_id {sid} is not declared by "
                f"view_types (C22) -- declared: {sorted(declared)}")
        n.sticky_headers = sid
    n.max_height = int(d.get("max_height") or 0)
    # Omitted means 1, the answer for a list of one-line entries, so nothing is
    # emitted and the engine floors the zero. The key exists for the case that
    # is not one line.
    n.row_height = int(d.get("row_height") or 0)
    # C25, enforced here as well as in the validator for C15's reason: the
    # generator is the path every build takes, so it does not rely on the
    # validator having been run. Two keys, both scoped to one kind, both refused
    # on anything else -- a width or a ceiling the engine will not read is worse
    # than no key at all, because it looks like it did something.
    conv_keys = [k for k in ("bubble_max_width_percent", "bubble_collapse_lines",
                             "bubble_avatar_gutter_cols") if k in d]
    if conv_keys and n.kind != KIND["conversation_window"]:
        raise SystemExit(
            f"{n.id}: {', '.join(conv_keys)} only allowed on conversation_window "
            f"(C25); this node is type '{d.get('type', 'frame')}'")
    if "children" in d and n.kind == KIND["conversation_window"]:
        raise SystemExit(
            f"{n.id}: conversation_window cannot declare children "
            f"(C25) -- its rows are not nodes. The one child it owns is the message "
            f"viewport, which the widget synthesizes itself and which is not a "
            f"declarable type")
    if n.kind == KIND["conversation_window"]:
        for axis in ("layout_width", "layout_height"):
            if str(d.get(axis, "")).lower() == "wrap_content":
                raise SystemExit(
                    f"{n.id}: {axis} wrap_content has no meaning on conversation_window "
                    f"(C25) -- its width drives its own reflow, so use match_parent or "
                    f"an explicit cell count")
        if str(d.get("scrollable", "")).lower() == "false":
            raise SystemExit(
                f"{n.id}: scrollable: false is refused on conversation_window (C25) -- the "
                f"kind sets the flag itself, because a conversation that cannot be scrolled "
                f"is a defect rather than a choice")
    if "bubble_max_width_percent" in d:
        pct = d["bubble_max_width_percent"]
        if isinstance(pct, bool) or not isinstance(pct, int) or not 1 <= pct <= 100:
            raise SystemExit(
                f"{n.id}: bubble_max_width_percent must be an integer 1-100, got "
                f"{pct!r} (C25)")
        n.bubble_max_pct = pct
    if "bubble_collapse_lines" in d:
        cap = d["bubble_collapse_lines"]
        if isinstance(cap, bool) or not isinstance(cap, int) or cap < 1:
            raise SystemExit(
                f"{n.id}: bubble_collapse_lines must be a positive integer, got "
                f"{cap!r} (C25)")
        n.bubble_collapse_lines = cap
    if "bubble_avatar_gutter_cols" in d:
        gut = d["bubble_avatar_gutter_cols"]
        # 6 is the floor because the timestamps the gutter is reserved beside are
        # six columns wide: below it a stamp row and an avatar would share cells.
        if isinstance(gut, bool) or not isinstance(gut, int) or not 6 <= gut <= 12:
            raise SystemExit(
                f"{n.id}: bubble_avatar_gutter_cols must be an integer 6-12, got "
                f"{gut!r} (C25)")
        n.bubble_avatar_gutter_cols = gut
    # C16, both halves, enforced here as well as in the validator: like C15, the
    # generator does not rely on the validator having been run, because it is the
    # path every build takes.
    if n.kind == "IRCITUI_RECYCLER":
        if n.hm == "IRCITUI_SIZE_WRAP" and not n.max_height:
            raise SystemExit(
                f"recycler '{n.id}': layout_height: wrap_content needs max_height "
                f"(C16) -- a virtual viewport has no content height to wrap")
        # C21 spelled the other way round: a registry satisfies this as well as a
        # single template does, because both answer "what shape is a row". What is
        # still refused is a recycler naming neither, which has no rows to clone.
        if not n.item_template and not n.view_types:
            raise SystemExit(
                f"recycler '{n.id}': item_template (C16) or view_types (C21) is "
                f"required -- they are the template(s) every visible row is cloned from")
        # Resolved here rather than carried onto the node. A recycler's extent is
        # not derived from its children -- the children are allocated to fit it --
        # so wrap_content plus max_height means exactly max_height, and the
        # attribute has nothing left to say once it has settled the extent. That
        # keeps it off IrcitUiCommonState, where a field one kind reads would be a
        # standing invitation to read it from another.
        if n.max_height and n.hm == "IRCITUI_SIZE_WRAP":
            n.hm, n.hc, n.hp = "IRCITUI_SIZE_EXACT", n.max_height, 0
    for field in USER_LISTENERS:
        handler = d.get(field)
        if handler:
            n.listeners[field] = str(handler)
    for field in POST_INTERCEPTOR_LISTENERS:
        handler = d.get(field)
        if handler:
            n.post_listeners[field] = str(handler)
    if d.get("draggable") or d.get("dragable"):
        n.flags.append("IRCITUI_F_DRAGGABLE")
    if d.get("scrollable"):
        n.flags.append("IRCITUI_F_SCROLLABLE")
    # The collapse vocabulary. Refused on a non-container rather than ignored:
    # the validator reports the same thing when it is run, but the generator is
    # the path every build takes, so this is where the mistake has to stop.
    named = [k for k in COLLAPSE_KEYS if d.get(k) is not None]
    if named and n.kind not in CONTAINER_KINDS:
        raise SystemExit(
            f"{n.id}: {', '.join(named)} only allowed on containers "
            f"(linear, frame, constraint, radio_group, check_group); this node "
            f"is type '{d.get('type', 'frame')}'")
    # One glyph, and exactly one. len() on a str counts code points, so 'e' plus
    # a combining acute is two and is refused while a double-width emoji is one
    # and is let through. An explicitly empty value fails the same test, which is
    # the intent: omitting the key is how a template asks for the framework
    # default, so a stray pair of quotes should not quietly resolve to one.
    # C17, the same rule the validator applies. The generator is the path every
    # build takes, so like C15 and C16 it does not rely on the validator having
    # been run. This key lived in this file and in layout/file_menu.yaml for some
    # time without being in the grammar, which is what made --validate-layout
    # refuse a document this generator happily accepted.
    if d.get("children_selectable"):
        if n.kind not in CONTAINER_KINDS:
            raise SystemExit(
                f"{n.id}: children_selectable only allowed on containers (C17); "
                f"this node is type '{d.get('type', 'frame')}'")
        if d.get("scrollable"):
            raise SystemExit(
                f"{n.id}: children_selectable and scrollable are not supported "
                f"together (C17) -- the child offset scrollable applies breaks the "
                f"child-index-to-row mapping the cursor depends on")
    # One glyph, and exactly one -- len() on a str counts code points, so 'e' plus a
    # combining acute is two and is refused, a double-width emoji is one and is let
    # through. The bar's two glyphs are held to the same rule as the collapse three and
    # by the same loop: C24 scopes them to the bar kinds, which the kind check above has
    # already established, so what is left is the length.
    for k in GLYPH_KEYS + BAR_GLYPH_KEYS:
        if d.get(k) is not None:
            n_pts = len(str(d[k]))
            if n_pts != 1:
                raise SystemExit(
                    f"'{n.id}': {k} must be exactly one glyph, not {n_pts}; "
                    f"omit the key to take the framework default")
    # 'collapsed' implies 'collapsible': naming a starting state that the node
    # has no way to leave would be a dead key. Stated in the grammar as C13.
    if d.get("collapsible") or d.get("collapsed"):
        n.flags.append("IRCITUI_F_COLLAPSIBLE")
    n.is_collapsed = 1 if d.get("collapsed") else 0
    # Left empty when the template named none. The engine then picks the
    # default pair for this node's collapse direction, so the defaults live in
    # one place - a node built through the C API has no template to inherit
    # from and needs the engine's fallback regardless.
    n.indicator_open = esc(d.get("collapsible_indicator_open") or "")
    n.indicator_close = esc(d.get("collapsible_indicator_close") or "")
    n.collapse_fill = esc(d.get("collapsible_fill") or "")
    # C24. Validated by tools/validate_layout.py as well; refused here too rather
    # than falling through, because a typo'd label_mode that silently rendered no
    # label is the trap C24 names in a smaller place. A glyph key on a non-bar is
    # refused for the reason the emitter's comment gives about kind-scoped fields:
    # it would be generated, and never read.
    if n.kind not in BARS:
        bad = [k for k in BAR_GLYPH_KEYS + ("label_mode",) if k in d]
        if bad:
            raise SystemExit(f"'{n.id}': {', '.join(sorted(bad))} only allowed on "
                             f"progress_bar or seek_bar (C24)")
    n.fill_glyph = esc(d.get("fill_glyph") or "")
    n.empty_glyph = esc(d.get("empty_glyph") or "")
    lm = str(d.get("label_mode") or "off").lower()
    if lm not in BAR_LABEL_MODES:
        raise SystemExit(f"'{n.id}': label_mode {lm!r} is not a label mode (C24) -- "
                         f"{', '.join(sorted(BAR_LABEL_MODES))}")
    n.label_mode = BAR_LABEL_MODES[lm]
    n.columns = int(d["columns"]) if d.get("columns") is not None else (8 if n.kind=="IRCITUI_EMOJI_TABLE" else 0)
    rm = str(d.get("render_mode") or "").lower()
    if n.kind == "IRCITUI_IMAGE_VIEW":
        n.columns = 1 if "braille" in rm else 0
        pth = d.get("path") or d.get("text") or ""
        n.text = str(pth)
    kids = d.get("children") or []
    child_nodes = []
    for k in kids:
        child_nodes.append(parse(k, seq, child_nodes))
    if n.kind in ("IRCITUI_RADIO_GROUP", "IRCITUI_CHECK_GROUP"):
        vals = d.get("values") or []
        if not isinstance(vals, list):
            vals = [vals]
        default = d.get("default_selection", 0)
        selected = d.get("selected", None)
        radio = n.kind == "IRCITUI_RADIO_GROUP"
        chosen = set()
        if selected is None:
            if radio:
                try:
                    chosen.add(int(default))
                except Exception:
                    chosen.add(0)
            else:
                if isinstance(default, list):
                    for x in default:
                        try: chosen.add(int(x))
                        except Exception: pass
                else:
                    try: chosen.add(int(default))
                    except Exception: chosen.add(0)
        else:
            sl = selected if isinstance(selected, list) else [selected]
            for item in sl:
                if isinstance(item, int):
                    chosen.add(item)
                else:
                    try:
                        chosen.add(vals.index(str(item)))
                    except ValueError:
                        pass
            if radio and len(chosen) > 1:
                chosen = {min(chosen)}
            if radio and not chosen and vals:
                chosen.add(0)
        bad = sorted(i for i in chosen if i < 0 or i >= len(vals))
        if bad:
            raise SystemExit(f"{n.id}: selected/default_selection index out of range: {bad}; valid range is 0..{len(vals)-1}")
        for i, val in enumerate(vals):
            ch = {
                "id": f"{n.id}_{i}",
                "type": "radio_button" if radio else "check_box",
                "text": str(val),
                "layout_width": "match_parent",
                "layout_height": "wrap_content",
                "clickable": True,
                "text_colour": d.get("text_colour", "white"),
                "bg_colour": d.get("bg_colour", "black"),
            }
            node = parse(ch, seq, child_nodes)
            if i in chosen:
                if "IRCITUI_F_CHECKED" not in node.flags:
                    node.flags.append("IRCITUI_F_CHECKED")
            child_nodes.append(node)
    n.children = child_nodes
    # resolve constraint names against sibling list — filled in emit via id map later
    n._raw_cons = {yamlk: d.get(yamlk) for yamlk, _ in CFIELDS}
    n.raw = d
    return n


def collect(n, acc):
    for c in n.children:
        collect(c, acc)
    acc.append(n)


def resolve_cons(n, siblings):
    idmap = {s.id: s.ident for s in siblings}
    for yamlk, field in CFIELDS:
        v = n._raw_cons.get(yamlk)
        if not v:
            n.cons[field] = "IRCITUI_UNSET"
        elif v == "parent":
            n.cons[field] = "IRCITUI_PARENT"
        elif v in idmap:
            n.cons[field] = "&" + idmap[v]  # pointer — we'll store index instead
        else:
            n.cons[field] = "IRCITUI_UNSET"
    # use sibling index for engine simplicity
    idxmap = {s.id: i for i, s in enumerate(siblings)}
    for yamlk, field in CFIELDS:
        v = n._raw_cons.get(yamlk)
        if not v:
            n.cons[field] = "IRCITUI_UNSET"
        elif v == "parent":
            n.cons[field] = "IRCITUI_PARENT"
        elif v in idxmap:
            n.cons[field] = str(idxmap[v])
        else:
            n.cons[field] = "IRCITUI_UNSET"


def walk_resolve(n):
    for c in n.children:
        resolve_cons(c, n.children)
        walk_resolve(c)


def parse_argv(argv):
    """Split --validate-layout from the two positionals.

    Hand-rolled rather than argparse: this script has taken two positionals
    since it was written, and moving it to argparse would change the usage and
    the error text of an invocation that the design doc and the generator test
    both rely on. An unrecognised option is refused rather than taken as a path,
    which is what a typo would otherwise quietly become."""
    validate = False
    positional = []
    for a in argv[1:]:
        if a == "--validate-layout":
            validate = True
        elif a.startswith("-") and a != "-":
            raise SystemExit(f"gen_ui.py: unknown option '{a}'")
        else:
            positional.append(a)
    return validate, positional


def validate_or_exit(paths, what):
    """Run the layout grammar validator over paths; abort if any of them fails.

    Called with the same file set the generator is about to consume, not just
    the root document. The validator does not follow layout_template references
    and the generator does, so validating the root alone would print OK for a
    run that never checked the templates about to be spliced in - which is
    precisely where a template's own errors live.

    Imported here rather than at module scope, so that a missing or broken
    tools/validate_layout.py cannot break plain generation, which does not need
    it. Diagnostics go to stderr, the stream this script already reports on.

    'what' names the set in the summary, because the root and the templates are
    checked in two passes and a bare count would not say which one it counts.

    Warnings do not abort. Only errors do, matching the validator's own exit
    semantics."""
    if not paths:
        return
    tools_dir = Path(__file__).resolve().parent
    if str(tools_dir) not in sys.path:
        sys.path.insert(0, str(tools_dir))
    # Bytecode caching is suppressed across the import: importing the validator
    # would otherwise leave tools/__pycache__/validate_layout.cpython-*.pyc in
    # the tree, and __pycache__ is not in .gitignore, so every flagged run would
    # add an untracked file to a repository that currently has none. Restored
    # afterwards so the setting does not outlive what needs it.
    saved_bytecode = sys.dont_write_bytecode
    sys.dont_write_bytecode = True
    # Run as a script this file is `__main__`, so the validator's `import gen_ui`
    # would load it a second time; alias it so that import resolves to this module.
    sys.modules.setdefault("gen_ui", sys.modules[__name__])
    try:
        import validate_layout
    except ImportError as e:
        raise SystemExit(f"--validate-layout: cannot import tools/validate_layout.py: {e}")
    finally:
        sys.dont_write_bytecode = saved_bytecode
    warnings_total = 0
    failed = 0
    for p in paths:
        rep = validate_layout.validate_doc(Path(p))
        for line in rep.errors + rep.warnings:
            print(line, file=sys.stderr)
        warnings_total += len(rep.warnings)
        if rep.errors:
            failed += 1
            print(f"{p}: FAIL ({len(rep.errors)} error(s), {len(rep.warnings)} warning(s))",
                  file=sys.stderr)
        else:
            print(f"{p}: OK ({len(rep.warnings)} warning(s))", file=sys.stderr)
    if failed:
        raise SystemExit(f"--validate-layout: {failed} of {len(paths)} {what} failed validation "
                         f"({warnings_total} warning(s)); nothing written")


def main():
    validate, positional = parse_argv(sys.argv)
    src = Path(positional[0] if positional else "layout/ircitui.yaml")
    dst = Path(positional[1] if len(positional) > 1 else "src/generated/ui_generated.c")
    # Before the document is read, so a broken layout is reported by the
    # validator's own message rather than as a yaml.YAMLError traceback.
    if validate:
        validate_or_exit([src], "root layout")
    data = yaml.safe_load(src.read_text())
    seq = [0]
    root = parse(data.get("app", data.get("ircitui", data)), seq, [])
    base = src.parent
    tpls = []
    seen = set()
    loaded = []  # template files actually read, for --validate-layout

    def load_tpl_file(pathstr):
        path = Path(pathstr)
        if not path.is_file():
            path = base / pathstr
        if not path.is_file():
            path = Path.cwd() / pathstr
        if not path.is_file():
            raise SystemExit(f"layout_template not found: {pathstr}")
        key = str(path.resolve())
        if key in seen:
            return
        seen.add(key)
        loaded.append(path)
        raw = yaml.safe_load(path.read_text())
        if isinstance(raw, dict) and raw.get("id") is None:
            raw = dict(raw)
            raw["id"] = path.stem
        node = parse(raw, seq, [])
        tpls.append(node)
        # recurse if the template itself references more files
        gather(node)

    def gather(n):
        # layout_template is "inflate me when clicked"; item_template is "clone me
        # once per visible row". Two different questions, one loader: a recycler's
        # rows come from an orphan template reached the same way a popup is.
        for attr in ("layout_template", "item_template"):
            ref = getattr(n, attr, "") or ""
            if ref.endswith(".yaml") or ref.endswith(".yml"):
                load_tpl_file(ref)
                # store the template id (file stem) for inflate at runtime
                setattr(n, attr, Path(ref).stem)
        # A registry's entries are reached by the same loader and resolved to the
        # same stem. Each one is a file the build reads, so each one is loaded --
        # and therefore validated and emitted into templates[] -- exactly as
        # item_template would have been.
        for vt in n.view_types:
            ref = vt["template"]
            if ref.endswith(".yaml") or ref.endswith(".yml"):
                load_tpl_file(ref)
                vt["template"] = Path(ref).stem
        for c in n.children:
            gather(c)

    gather(root)
    # After discovery, so the set validated is the set about to be consumed -
    # including templates reached through another template, which gather()
    # recurses into.
    if validate:
        validate_or_exit(loaded, "template")
    rawt = data.get("templates") or []
    if isinstance(rawt, dict):
        rawt = [dict(v or {}, id=k) for k, v in rawt.items()]
    for item in rawt:
        tpls.append(parse(item, seq, []))

    walk_resolve(root)
    for tp in tpls:
        walk_resolve(tp)

    order = []
    collect(root, order)
    torder = []
    for tp in tpls:
        collect(tp, torder)

    alln = torder + order
    # A node's index in the pool is how it is addressed from here on: the child
    # table and the template index both name pool entries, and the id index (a
    # later step) will resolve to one. The order is therefore load-bearing now,
    # where before it only decided the order of the log lines.
    #
    # It is unchanged from what the previous emitter produced - templates first
    # in post-order, then the app tree in post-order - which is the order the
    # per-node log lines have always arrived in.
    slot = {id(n): i for i, n in enumerate(alln)}
    npool = len(alln)
    ntpl = len(tpls)

    # One flat child table, sliced by offset, rather than one array per
    # container. The model is one object, so it holds one table; a container's
    # children are the run that begins at its own offset.
    offsets = {}
    nrefs = 0
    for n in alln:
        if n.children:
            offsets[id(n)] = nrefs
            nrefs += len(n.children)

    # An array member of length zero is not valid C, so a table a layout leaves
    # empty still gets one slot. Nothing reads it: the matching count is zero
    # and the descriptor's pointer into it is emitted as NULL.
    refs_sz = max(nrefs, 1)
    tpl_sz = max(ntpl, 1)

    # The id index needs one node per id. A duplicate cannot be indexed - one
    # slot cannot hold two nodes - and the traversal order that would otherwise
    # shadow one is exactly what a table cannot express. Refused rather than
    # resolved: ui_verify.c's sCheckIds already reports a duplicate id as an
    # error at run time, so failing the build is the same verdict, one step
    # earlier and against the layout rather than against a rendered tree.
    first_seen = {}
    for n in alln:
        if n.id in first_seen:
            raise SystemExit(
                f"duplicate id '{n.id}': the id index holds one node per id, and both "
                f"pool[{first_seen[n.id]}] and pool[{slot[id(n)]}] claim it. Node ids must "
                f"be unique across the app tree and every template it reaches.")
        first_seen[n.id] = slot[id(n)]

    id_slot_table, id_slot_max_probe = build_id_index(alln, slot)
    id_slot_count = len(id_slot_table)

    # The declared sets are read HERE, not during parse, because template
    # discovery is what rewrites a reference to its stem: a block built while
    # parsing would carry "rows.yaml" where the node's own member carries "rows",
    # and the two would disagree -- the drift §2.5's rule exists to catch.
    for n in alln:
        n.declared = declared_attrs(n, n.raw)

    decl_lines, decl_of = emit_decl_blocks(alln)

    L = ['/* generated — do not edit */',
         provenance.generated_line(provenance.GEN_UI),
         '/**',
         f' * @file {dst.name}',
         f' * @brief Node data for the reference model, emitted from {src}',
         ' *        by tools/gen_ui.py. Do not edit: edit the generator.',
         ' */',
         '#include "ui_priv.h"',
         '#include "ui_log.h"',
         '/* The per-kind declaration types: the blocks below are laid out in them, and',
         '   the widgets that read a block include this header for the cast. */',
         '#include "generated/ui_decl_types.h"', '']
    L += decl_lines
    L += ['/* The reference model, whole, in one object: the descriptor head, the node',
         '   pool, the flattened child table and the template index. Every pointer',
         '   below is an address constant, so the loader resolves the graph and no',
         '   code runs to build it. Nothing here is ever freed: the model is static',
         '   storage for the life of the process, which is what lets a binding keep',
         '   the pointer to an id it resolved.',
         '',
         '   Pool order is templates first in post-order, then the app tree in',
         '   post-order. That is the order the per-node log lines have always been',
         '   emitted in, and it is now also how a node is addressed. */',
         'static struct {',
         '    struct IrcitUiDataBindDescriptor desc;',
         f'    IrcitUiNode   pool[{npool}];',
         f'    IrcitUiNode  *child_refs[{refs_sz}];',
         f'    IrcitUiNode  *templates[{tpl_sz}];',
         f'    uint32_t      id_slot_table[{id_slot_count}];',
         '} s_ui_model = {',
         '    .desc = {',
         f'        .root           = &s_ui_model.pool[{slot[id(root)]}],',
         '        .nodes          = s_ui_model.pool,',
         f'        .node_count     = {npool},',
         '        .templates      = ' + ('s_ui_model.templates,' if ntpl else '0,'),
         f'        .template_count = {ntpl},',
         '        .id_slots          = s_ui_model.id_slot_table,',
         f'        .id_slot_count     = {id_slot_count},',
         f'        .id_slot_max_probe = {id_slot_max_probe},',
         '    },']
    if nrefs:
        L.append('    /* One entry per child edge, in pool order, so each container holds a')
        L.append('       slice of this table: &child_refs[offset]. */')
        L.append('    .child_refs = {')
        for n in alln:
            if n.children:
                refs = ", ".join(f"&s_ui_model.pool[{slot[id(c)]}]" for c in n.children)
                L.append(f'        {refs},  /* {ccomment(n.id)}, offset {offsets[id(n)]} */')
        L.append('    },')
    if ntpl:
        L.append('    /* The unbound roots: parsed and emitted alongside the app tree, but')
        L.append('       belonging to no parent within it. Instantiated on demand. */')
        L.append('    .templates = {')
        for tp in tpls:
            L.append(f'        &s_ui_model.pool[{slot[id(tp)]}],  /* {ccomment(tp.id)} */')
        L.append('    },')
    L.append('    .pool = {')
    for i, n in enumerate(alln):
        ch = f"&s_ui_model.child_refs[{offsets[id(n)]}]" if n.children else "0"
        nc = len(n.children)
        fl = " | ".join(n.flags) if n.flags else "0"
        mt = ", ".join(str(x) for x in n.margin)
        pd = ", ".join(str(x) for x in n.padding)
        L.append(f'        [{i}] = {{')
        # The node points at its declaration block, or at nothing: a node that
        # declared no key of its kind emits no block (§2.3), and a clone copies
        # this pointer with the rest of common.
        dname = decl_of.get(id(n), (None, {}))[0]
        decl_ref = f'.declarative_attributes_aux=&{dname}, ' if dname else ''
        L.append(f'            .common = {{ .id="{n.id}", .kind={n.kind}, .layout_w={{ {n.wm}, {n.wc}, {n.wp} }}, .layout_h={{ {n.hm}, {n.hc}, {n.hp} }}, {decl_ref}')
        # -1 means "not given", and it is emitted rather than omitted so the value
        # is explicit in the generated file: the engine reads a negative mark_fg as
        # "the mark takes text_colour", where a zeroed node -- a clone, a C-built
        # def -- would mean COLOR_BLACK and paint every tick black.
        L.append(f'                .margin={{ {mt} }}, .padding={{ {pd} }}, .flags={fl}, .fg={n.fg}, .bg={n.bg}, .bdr={n.bdr}, .gravity={n.grav}, .layout_gravity={n.lgrav},')
        L.append(f'                .c_top_top={n.cons["c_top_top"]}, .c_top_bot={n.cons["c_top_bot"]}, .c_bot_top={n.cons["c_bot_top"]}, .c_bot_bot={n.cons["c_bot_bot"]},')
        L.append(f'                .c_start_start={n.cons["c_start_start"]}, .c_start_end={n.cons["c_start_end"]}, .c_end_start={n.cons["c_end_start"]}, .c_end_end={n.cons["c_end_end"]},')
        # item_template is written only when the node has one, unlike the string
        # fields beside it. It is the one field here that a single kind reads, and
        # writing it empty on every other node said otherwise -- an attribute the
        # validator refuses on 43 of the 44 nodes should not appear on all 44. An
        # omitted member of a designated initialiser is zero-initialised, and this
        # struct already reads NULL as "named none", the representation
        # indicator_open's own comment describes.
        #
        # THE OTHER KIND-SCOPED STRINGS STILL BLEED, and are left alone rather
        # than folded in here: layout_template is written on every node though
        # only a button reads it, and indicator_open / indicator_close /
        # collapse_fill on every node though C12 scopes them to containers. This
        # change is vocabulary for one capability, not a pass over the emitter.
        # A future refactor should give all four the treatment item_template and
        # view_types now get, and check whether IrcitUiCommonState should carry
        # them at all.

        # view_types joins item_template in the treatment above, and for the same
        # reason: it is the other field only a recycler reads, so a node that is
        # not a recycler emits neither. The two are mutually exclusive (C21), so
        # at most one of these is ever non-empty.
        # The array itself is the block's, not the node's: two nodes that
        # declared the same registry share one block and therefore one array.

        # C22's type_id, and it is ALWAYS emitted -- including the -1 that means
        # off. That is the opposite of what item_template and view_types do above,
        # and it is deliberate, for exactly the reason R15 gives about mark_fg:
        # the engine tells "not given" from a real value by the SIGN, so a missing
        # field would decode as 0 -- and 0 is a LEGAL type_id. Omitting it made
        # every recycler whose registry declares type 0 pin type 0's rows without
        # anyone asking. A field whose zero value is a valid value must never be
        # left to zero-initialisation.

        # The class is emitted as a STRING LITERAL, which is what makes it safe to
        # store as a borrowed pointer: ui_priv.h documents common.theme_class as
        # never copied, and a literal in the generated file lives as long as the
        # program. NULL when unset, matching every other "not given" here.
        cls = f'"{n.theme_class}"' if n.theme_class else '0'

        # Emitted only when non-zero, and zero is a genuine sentinel for all three
        # (C25 refuses 0 as a value), so a node that never mentions them carries
        # the defaults rather than whatever the last conversation window held.

        L.append(f'                .text="{n.text}", .interaction_defaults={default_listeners_init()}, .parent=0, .win=0, .x=0, .y=0, .w=0, .h=0, .ref=0, .theme_class={cls},')
        L.append('                },')
        L.append(f'            .container = {{ .children={ch}, .n_children={nc}, .columns={n.columns}, .scroll_y=0, .sel0=0, .sel1=0, .is_collapsed={n.is_collapsed} }},')
        L.append(f'            .interactions_user = {user_listeners_init(n)}')
        L.append('        },')
    L.append('    },')
    L.append('    /* The id index: a slot holds the pool index of the node whose id hashes')
    L.append('       there, or IRCITUI_SLOT_NONE where it is empty. The longest run of')
    L.append(f'       occupied slots below is {id_slot_max_probe}, emitted as id_slot_max_probe:')
    L.append('       the bound a reader loops on, so it never has to assume the hash does')
    L.append('       not cluster. Built and measured by build_id_index() in the generator. */')
    L.append('    .id_slot_table = {')
    for off in range(0, id_slot_count, 12):
        cells = ", ".join("IRCITUI_SLOT_NONE" if v == SLOT_NONE else str(v)
                          for v in id_slot_table[off:off + 12])
        L.append(f'        {cells},')
    L.append('    },')
    L.append('};')
    L.append('')
    L.append('/* IrcitUiScreen is the one model-wide global left. It is the mutable handle')
    L.append('   for the on-glass tree, not part of the model, so it is not a member of the')
    L.append('   descriptor and is not reached through it. The three aliases that used to sit')
    L.append('   beside it - IrcitUiRoot, IrcitUiTemplates and IrcitUiTemplateCount - are')
    L.append('   gone: they were views of the object above, and a second way to name one')
    L.append('   thing is a way for the two to disagree. */')
    L.append('IrcitUiTree  IrcitUiScreen;')
    L.append('')
    L.append('PUBLIC_API struct IrcitUiDataBindDescriptor *')
    L.append('IrcitUiDataBind(void)')
    L.append('{')
    L.append('    size_t i;')
    L.append('')
    L.append('    /* One pass, so a run records every node it is working from, in the order')
    L.append('       this file emits them. The model is not touched: it was complete before')
    L.append('       main() ran, so there is nothing here to construct. */')
    L.append('    for (i = 0; i < s_ui_model.desc.node_count; i++) {')
    L.append('        const IrcitUiNode *node_ptr = &s_ui_model.pool[i];')
    L.append('        IRCITUI_LOG_INFO("reference node: id=%s kind=%d children=%d",')
    L.append('                       node_ptr->common.id, (int)node_ptr->common.kind, node_ptr->container.n_children);')
    L.append('    }')
    L.append('    return &s_ui_model.desc;')
    L.append('}')
    L.append('')
    dst.write_text("\n".join(L))
    print(f"wrote {dst}", file=sys.stderr)

if __name__ == "__main__":
    main()
