#!/usr/bin/env python3
"""Validate IRCIT layout YAML against the formal vocabulary (layout-grammar.ebnf)."""
from __future__ import annotations

import sys
from pathlib import Path

try:
    import yaml
except ImportError:
    sys.exit("pip install pyyaml")

# The event vocabulary is the generator's, and it is imported rather than restated.
# tools/gen_ui.py is where a listener key is decided -- it is the file that emits
# one into a node -- so a second copy here could only ever drift from it, and did:
# this file's own list refused seven of the generator's eight keys.
#
# The idiom is tools/post_generation_verifier.py's, and for its reason.
# dont_write_bytecode is set BEFORE the import, not after: importing a sibling is
# what would leave a tools/__pycache__ behind, and that directory is not in
# .gitignore, so a flagged run would add an untracked file to a tree that has none.
sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
import gen_ui  # noqa: E402

WIDGETS = {
    "text_view", "button", "image_view", "edit_text",
    "check_box", "radio_button", "switch",
    "progress_bar", "seek_bar", "emoji_table",
    "conversation_window",
}
CONTAINERS = {
    "linear", "frame", "constraint", "radio_group", "check_group", "recycler",
}
TYPES = WIDGETS | CONTAINERS

COLOURS = {
    "black", "red", "green", "yellow", "blue", "magenta", "cyan", "white",
}
GRAVITY = {"start", "end", "top", "bottom", "center", "fill"}
VIS = {"visible", "invisible", "gone", True, False}
# The listener keys a template may name, taken from the generator rather than
# restated -- see the import note at the top. Split by the block each is emitted
# into, because the two are different structs and only one of them is read on every
# kind: LIFECYCLE_EVENTS go to .lifecycle_listeners, POST_INTERCEPTOR_EVENTS to
# .post_interceptor_listeners, which only the edit field's fast lane reads.
#
# `on_input`, `on_inflated`, `on_visible`, `on_overlapped`, `on_hidden`,
# `on_deflated` and `on_dragged` used to be listed here and were accepted for keys
# gen_ui.py silently ignores; they are not part of the vocabulary and are gone.
LIFECYCLE_EVENTS = set(gen_ui.USER_LISTENERS)
POST_INTERCEPTOR_EVENTS = set(gen_ui.POST_INTERCEPTOR_LISTENERS)
EVENTS = LIFECYCLE_EVENTS | POST_INTERCEPTOR_EVENTS
CONSTRAINTS = {
    "constraintTop_toTopOf", "constraintTop_toBottomOf",
    "constraintBottom_toTopOf", "constraintBottom_toBottomOf",
    "constraintStart_toStartOf", "constraintStart_toEndOf",
    "constraintEnd_toStartOf", "constraintEnd_toEndOf",
}
# The three glyph properties, and the two boolean ones. Container-only: a
# container is the only kind with an extent of its own to fold, and the only one
# whose children can be hidden behind it. Kept in step with CONTAINER_KINDS in
# tools/gen_ui.py and IrcitUiIsContainer() in src/ui_engine.c.
GLYPH = {
    "collapsible_indicator_open",
    "collapsible_indicator_close",
    "collapsible_fill",
}
COLLAPSE = {"collapsible", "collapsed"} | GLYPH

# C24's subject: the two kinds that draw a value as a filled track, and the
# attributes they alone read. Kept in step with BARS in tools/gen_ui.py and with
# the case labels in src/ui_engine.c's IrcitUiDraw.
BARS = {"progress_bar", "seek_bar"}
BAR_GLYPH = {"fill_glyph", "empty_glyph"}
# The three the enum declares, and the only spellings accepted. "time" is
# deliberately absent: nothing implements it yet, and a value that resolves to
# nothing is a trap rather than a reservation -- the reason C23 gives.
BAR_LABEL_MODES = {"off", "adjacent", "centered"}
CONVERSATION_KEYS = {"bubble_max_width_percent", "bubble_collapse_lines",
                     "bubble_avatar_gutter_cols"}
BAR_KEYS = BAR_GLYPH | {"label_mode"}
KNOWN = (
    {
        "id", "type", "children", "text",
        "layout_width", "layout_height",
        "margin", "padding",
        "border", "clickable", "enabled", "draggable", "dragable",
        "scrollable", "children_selectable", "visibility",
        "text_colour", "bg_colour", "border_colour", "mark_colour",
        "orientation", "gravity", "layout_gravity",
        "values", "selected", "default_selection",
        "path", "render_mode", "columns",
        "layout_template", "item_template", "max_height", "row_height",
        "view_types", "sticky_headers", "theme_class",
    }
    | EVENTS
    | CONSTRAINTS
    | COLLAPSE
    | BAR_KEYS
    | CONVERSATION_KEYS
)

LINEARISH = {"linear", "radio_group", "check_group"}

# C20's subject: the kinds that draw a mark. Every one of them renders through
# sPaintToggleMark in the engine, which is what makes mark_colour mean the same
# thing on all five.
# The palette slots, one vocabulary with ircit_ui_palette_slot_enum.h. R18 in
# post_generation_verifier.py asserts they agree; this is the only place the
# spellings are written down on the input side.
SLOTS = {
    "surface", "panel", "text", "text_muted",
    "text_error", "edit_active", "overlay_snackbar", "overlay_action",
    "accent",
}

TOGGLES = {"check_box", "radio_button", "switch", "radio_group", "check_group"}


class Report:
    def __init__(self, path: str):
        self.path = path
        self.errors: list[str] = []
        self.warnings: list[str] = []

    def err(self, loc: str, msg: str) -> None:
        self.errors.append(f"{self.path}:{loc}: error: {msg}")

    def warn(self, loc: str, msg: str) -> None:
        self.warnings.append(f"{self.path}:{loc}: warning: {msg}")


def loc_of(node: dict, fallback: str) -> str:
    return str(node.get("id") or fallback)


def check_size(rep: Report, loc: str, key: str, val) -> None:
    if val in ("match_parent", "wrap_content"):
        return
    # A percentage is "NN%", resolved at layout time against whatever the parent
    # offers along that axis. Rejected outside 1-100 rather than clamped, so a
    # typo fails here and in the generator rather than becoming a silently
    # degenerate layout. Zero is refused rather than allowed to floor: a node
    # given no share of its parent is a mistake, and "one cell" is a poor way to
    # spell it. The C API is a separate question -- IrcitUiWidgetDef carries no
    # validator, so a percent of 0 can still arrive that way and the engine
    # floors it in sPlace.
    if isinstance(val, str) and val.strip().endswith("%"):
        body = val.strip()[:-1].strip()
        if body.isdigit() and 1 <= int(body) <= 100:
            return
        rep.err(loc, f"{key} percentage '{val}' must be 1%-100%")
        return
    if isinstance(val, bool) or not isinstance(val, int) or val <= 0:
        rep.err(loc, f"{key} must be match_parent, wrap_content, a positive int, or NN%")


def check_cells(rep: Report, loc: str, key: str, val) -> None:
    # A plain count, unlike check_size: max_height bounds an extent that has
    # already been resolved, so match_parent and wrap_content have nothing to
    # bound, and a percentage would be relative to the very offer it is meant to
    # cap. The grammar says Cells, and Cells is "integer > 0".
    if isinstance(val, bool) or not isinstance(val, int) or val <= 0:
        rep.err(loc, f"{key} must be a positive int")


def check_box(rep: Report, loc: str, key: str, val) -> None:
    if isinstance(val, int) and not isinstance(val, bool) and val >= 0:
        return
    if isinstance(val, list) and len(val) == 4 and all(
        isinstance(x, int) and not isinstance(x, bool) and x >= 0 for x in val
    ):
        return
    rep.err(loc, f"{key} must be an int or [T,R,B,L]")


def walk(rep: Report, node, path: str, parent_type: str | None, siblings: list) -> None:
    if not isinstance(node, dict):
        rep.err(path, "widget must be a mapping")
        return

    loc = loc_of(node, path)
    unknown = [k for k in node if k not in KNOWN]
    for k in unknown:
        rep.err(loc, f"unknown key '{k}'")

    wid = node.get("id")
    if not wid or not isinstance(wid, str):
        rep.err(loc, "missing id")
    typ = node.get("type")
    if typ not in TYPES:
        rep.err(loc, f"invalid type '{typ}'")
        typ = None

    ids = [s.get("id") for s in siblings if isinstance(s, dict) and s.get("id")]
    if wid and ids.count(wid) > 1:
        rep.err(loc, f"duplicate sibling id '{wid}'")

    if "layout_width" in node:
        check_size(rep, loc, "layout_width", node["layout_width"])
    if "layout_height" in node:
        check_size(rep, loc, "layout_height", node["layout_height"])
    for box in ("margin", "padding"):
        if box in node:
            check_box(rep, loc, box, node[box])

    for ck in ("text_colour", "bg_colour", "border_colour", "mark_colour"):
        if ck in node and str(node[ck]).lower() not in COLOURS:
            rep.err(loc, f"{ck} '{node[ck]}' not in {sorted(COLOURS)}")

    for gk in ("gravity", "layout_gravity"):
        if gk in node and node[gk] not in GRAVITY:
            rep.err(loc, f"{gk} '{node[gk]}' invalid")

    if "orientation" in node:
        if typ not in LINEARISH:
            rep.err(loc, "orientation only allowed on linear/radio_group/check_group")
        elif node["orientation"] not in ("vertical", "horizontal"):
            rep.err(loc, "orientation must be vertical or horizontal")

    for ck in sorted(CONVERSATION_KEYS):
        if ck in node and typ != "conversation_window":
            rep.err(loc, f"{ck} only allowed on conversation_window")
    if typ == "conversation_window":
        # No `children` check here: the validator's existing leaf rule already
        # refuses children on any kind that is not a ContainerType, and this kind
        # is in WIDGETS. A second message for the same mistake is noise.
        if "bubble_max_width_percent" in node:
            pct = node["bubble_max_width_percent"]
            if isinstance(pct, bool) or not isinstance(pct, int) or not 1 <= pct <= 100:
                rep.err(loc, f"bubble_max_width_percent must be an integer 1-100, got {pct!r}")
        if "bubble_collapse_lines" in node:
            cap = node["bubble_collapse_lines"]
            if isinstance(cap, bool) or not isinstance(cap, int) or cap < 1:
                rep.err(loc, f"bubble_collapse_lines must be a positive integer, got {cap!r}")
        if "bubble_avatar_gutter_cols" in node:
            gut = node["bubble_avatar_gutter_cols"]
            # Six is the floor: the timestamps sit in the same columns, so a narrower
            # gutter would have a stamp row and an avatar claiming the same cells.
            if isinstance(gut, bool) or not isinstance(gut, int) or not 6 <= gut <= 12:
                rep.err(loc, f"bubble_avatar_gutter_cols must be an integer 6-12, got {gut!r}")
        # C25. wrap_content is refused rather than answered. The widget's width is
        # what drives its own reflow, so a measured width would be derived from a
        # measurement of itself; and the engine's fallback for a kind with no wrap
        # branch answers ONE column, which is a wrong answer rather than a
        # conservative one. A floor invented here would be a second copy of a
        # number the widget already owns.
        for axis in ("layout_width", "layout_height"):
            if str(node.get(axis, "")).lower() == "wrap_content":
                rep.err(loc, f"{axis}: wrap_content has no meaning on conversation_window "
                             f"(C25) -- its width drives its own reflow, so use match_parent "
                             f"or an explicit cell count")
        # C25. A conversation is scrollable by its nature, so the kind sets the flag
        # itself in the layout pass; `scrollable: false` would then be IGNORED, and
        # a key that resolves to nothing is a trap rather than a reservation.
        # Refused here and in the generator so the two cannot disagree.
        if str(node.get("scrollable", "")).lower() == "false":
            rep.err(loc, "scrollable: false is refused on conversation_window (C25) -- the kind "
                         "sets the flag itself, because a conversation that cannot be scrolled "
                         "is a defect rather than a choice. The bar is drawn only when there is "
                         "overflow, so a conversation that fits its box carries none")

    cons = [k for k in node if k in CONSTRAINTS]
    if cons and parent_type and parent_type != "constraint":
        rep.err(loc, "constraint* only valid when parent type is constraint")
    sib_ids = {s.get("id") for s in siblings if isinstance(s, dict)}
    for k in cons:
        tgt = node[k]
        if tgt != "parent" and tgt not in sib_ids:
            rep.err(loc, f"{k} target '{tgt}' is not parent or a sibling id")

    if typ not in CONTAINERS and "children" in node:
        rep.err(loc, f"type {typ} cannot have children")
    if typ in CONTAINERS:
        kids = node.get("children") or []
        if not isinstance(kids, list):
            rep.err(loc, "children must be a list")
            kids = []
        if node.get("layout_width") in (None, "wrap_content") and not kids:
            # A recycler is exempt for a different reason from the group kinds: its
            # children are not declared, they are cloned from item_template to fill
            # whatever extent it was given, so an empty children list is the normal
            # case rather than a container about to collapse to nothing.
            if typ not in ("radio_group", "check_group", "recycler"):
                rep.warn(loc, "empty wrap_content container")
        for i, ch in enumerate(kids):
            walk(rep, ch, f"{loc}/[{i}]", typ, kids)

    if typ in ("radio_group", "check_group"):
        if "values" in node and not isinstance(node["values"], list):
            rep.err(loc, "values must be a list")
    else:
        for k in ("values", "selected", "default_selection"):
            if k in node:
                rep.err(loc, f"{k} only allowed on radio_group or check_group")

    if typ != "image_view":
        for k in ("path", "render_mode"):
            if k in node:
                rep.err(loc, f"{k} only allowed on image_view")
    elif "render_mode" in node:
        rm = str(node["render_mode"]).lower()
        if rm not in ("half-blocks", "half-block", "braille"):
            rep.err(loc, f"render_mode '{node['render_mode']}' invalid")

    if "columns" in node and typ not in ("emoji_table", "image_view"):
        rep.err(loc, "columns only allowed on emoji_table or image_view")

    # C24. The bar vocabulary is read by two kinds and no others, and a glyph the
    # painter never consults is worse than an absent key: it parses, validates and
    # generates, and then does nothing, with nothing anywhere saying so.
    if typ not in BARS:
        for k in sorted(BAR_KEYS):
            if k in node:
                rep.err(loc, f"{k} only allowed on progress_bar or seek_bar (C24)")
    elif "label_mode" in node:
        lm = str(node["label_mode"]).lower()
        if lm not in BAR_LABEL_MODES:
            rep.err(loc, f"label_mode '{node['label_mode']}' is not a label mode (C24): "
                         f"{', '.join(sorted(BAR_LABEL_MODES))}")

    for k in sorted(COLLAPSE):
        if k in node and typ not in CONTAINERS:
            rep.err(loc, f"{k} only allowed on containers")
    # The post-interceptor block is read by the edit field's fast lane and by
    # nothing else (ircitui_edit_text.c, stage 3), so a key naming one on any other
    # kind would parse, validate and generate, and then never fire -- the fault C24
    # refuses for the bar vocabulary, and the reason on_scrolled is not a key at
    # all: it is the block's other member and nothing in the tree calls it.
    if typ != "edit_text":
        for k in sorted(POST_INTERCEPTOR_EVENTS):
            if k in node:
                rep.err(loc, f"{k} only allowed on edit_text")
    # C17. The flag makes a container's children a keyboard-navigable list with
    # sel0 as the cursor, so there has to be a container to hold the cursor; and
    # scrollable is excluded because the child offset it applies breaks the
    # mapping from child index to row that the cursor walks.
    if node.get("children_selectable"):
        if typ not in CONTAINERS:
            rep.err(loc, "children_selectable only allowed on containers")
        if node.get("scrollable"):
            rep.err(loc, "children_selectable and scrollable are not supported together (C17): "
                         "the child offset scrollable applies breaks the child-index-to-row "
                         "mapping the cursor depends on")
    # str() so a YAML `true` and a quoted "true" both read as the same answer,
    # the tolerance the generator's border handling already shows.
    for k in ("collapsible", "collapsed"):
        if k in node and str(node[k]).lower() not in ("true", "false"):
            rep.err(loc, f"{k} must be true or false")
    # One glyph, and exactly one. len() on a str counts code points, so this is
    # the rule stated plainly: 'e' plus a combining acute is two and is refused,
    # a double-width emoji is one and is let through. Guarded on the kind test,
    # because a glyph on a kind that cannot carry one is already an error and the
    # length of a value that cannot be used is not worth a second line.
    #
    # The bar's two glyphs are held to the same rule and by the same loop: C24
    # scopes them to the two bar kinds, so which set applies is decided by the
    # kind, exactly as it is for the container glyphs.
    glyph_keys = GLYPH if typ in CONTAINERS else (BAR_GLYPH if typ in BARS else ())
    for k in sorted(glyph_keys):
        if k in node:
            n_pts = len(str(node[k]))
            if n_pts != 1:
                rep.err(loc, f"{k} must be exactly one glyph, not {n_pts}; "
                             f"omit the key to take the framework default")

    # C16. A recycler's extent is not derived from its children -- the children
    # are allocated to fit the extent -- so wrap_content alone is circular and
    # there is nothing to compute the row allocation from. max_height breaks the
    # circle by supplying the extent outright.
    if typ == "recycler":
        if node.get("layout_height") == "wrap_content" and "max_height" not in node:
            rep.err(loc, "recycler with layout_height: wrap_content needs max_height "
                         "(C16): a virtual viewport has no content height to wrap")
        # Either spelling satisfies C16: one template for every row, or a registry
        # naming one template per row shape. A recycler with neither still has no
        # rows to clone and is still refused.
        if not node.get("item_template") and not node.get("view_types"):
            rep.err(loc, "recycler needs item_template (C16) or view_types (C21): "
                         "they are the template(s) its visible rows are cloned from")
    else:
        for k in ("item_template", "max_height", "row_height", "view_types", "sticky_headers"):
            if k in node:
                rep.err(loc, f"{k} only allowed on recycler")

    # C22. Three halves of one rule: the value NAMES a row type, so there has to
    # be an id space (view_types), the value has to be a type id at all, and the
    # registry has to hold it -- an id the registry does not hold would pin a slot
    # no row can be built for, so the header would be blank every pass with
    # nothing said about why. Checked here as well as in the generator, for C15's
    # reason, and in the same words.
    if "sticky_headers" in node:
        declared = {e.get("type_id") for e in (node.get("view_types") or [])
                    if isinstance(e, dict)}
        sid = node["sticky_headers"]
        if isinstance(sid, bool) or not isinstance(sid, int) or sid < 0:
            rep.err(loc, f"sticky_headers must be a non-negative type_id, got {sid!r} "
                         f"(C22): it names a row type, it is not a flag")
        elif not node.get("view_types"):
            rep.err(loc, "sticky_headers needs view_types (C22): it names a type_id, "
                         "and without a registry there is no id space to name one in")
        elif sid not in declared:
            rep.err(loc, f"sticky_headers type_id {sid} is not declared by view_types (C22) "
                         f"-- declared: {sorted(x for x in declared if x is not None)}")

    # C21. The registry is the heterogeneous half of C16's question, so the two
    # are exclusive: a node asking both would leave the answer to whichever the
    # engine read first. Checked here as well as in the generator, for C15's
    # reason -- the generator is the path every build takes and does not rely on
    # the validator having been run -- and in the same words so a template that
    # fails one fails the other identically.
    if "view_types" in node:
        if typ == "recycler" and "item_template" in node:
            rep.err(loc, "item_template and view_types are mutually exclusive (C21): "
                         "they both answer what shape a row is -- use one")
        entries = node["view_types"]
        if not isinstance(entries, list) or not entries:
            rep.err(loc, "view_types must be a non-empty list (C21): an empty registry "
                         "is the homogeneous case, which item_template already spells")
            entries = []
        seen: set = set()
        for e in entries:
            if not isinstance(e, dict):
                rep.err(loc, "each view_types entry must be a mapping of type_id and template")
                continue
            extra = sorted(k for k in e if k not in ("type_id", "template"))
            if extra:
                rep.err(loc, f"view_types entry has unknown key(s) {extra}")
            tid = e.get("type_id")
            # bool is an int in Python and is not a type tag here, so it is
            # excluded rather than silently read as 0 or 1.
            if isinstance(tid, bool) or not isinstance(tid, int) or tid < 0:
                rep.err(loc, f"view_types type_id must be a non-negative integer, got {tid!r}")
            elif tid in seen:
                rep.err(loc, f"view_types type_id {tid} is declared twice (C21): "
                             f"which entry wins would be an implementation detail")
            else:
                seen.add(tid)
            tpl = e.get("template")
            if not tpl or not isinstance(tpl, str):
                rep.err(loc, f"view_types entry {tid!r} needs a template")
    for k in ("max_height", "row_height"):
        if k in node:
            check_cells(rep, loc, k, node[k])

    # C20: mark_colour colours a toggle's mark, so a node with no mark has
    # nothing for it to colour. Checked here rather than left to the painter,
    # which would silently ignore it -- a template asking for a coloured tick on
    # a text_view has made a mistake, and a mistake that produces no output is
    # the kind that survives a review.
    if "mark_colour" in node and typ not in TOGGLES:
        rep.err(loc, f"mark_colour only allowed on a toggle (C20): "
                     f"{', '.join(sorted(TOGGLES))} -- got {typ}")

    # C23: theme_class names a palette slot, and the vocabulary is closed -- see
    # SLOTS. A free-form value would defeat the point of a semantic name: two
    # spellings of one intent that no rule and no slot table can both match.
    if "theme_class" in node:
        if str(node["theme_class"]) not in SLOTS:
            rep.err(loc, f"theme_class '{node['theme_class']}' is not a palette slot (C23): "
                         f"{', '.join(sorted(SLOTS))}")
        # C23: a slot supplies the surface, so a node declaring both has one key
        # that is silently ignored -- the slot is resolved at paint time, after the
        # node's own colours were set. A key that resolves to nothing is a trap
        # rather than a reservation, so the combination is refused. border_colour
        # stays allowed: the nine roles name a surface, a text line, an overlay or
        # a value's leading edge, and a border is none of those.
        clash = [k for k in ("text_colour", "bg_colour") if k in node]
        if clash:
            rep.err(loc, f"theme_class supplies the surface, so {', '.join(clash)} "
                         f"would be ignored (C23)")

    if "layout_template" in node and typ != "button":
        rep.warn(loc, "layout_template is intended on button")

    if "visibility" in node and node["visibility"] not in VIS:
        rep.err(loc, f"visibility '{node['visibility']}' invalid")


def validate_doc(path: Path) -> Report:
    rep = Report(str(path))
    try:
        data = yaml.safe_load(path.read_text())
    except yaml.YAMLError as e:
        rep.err("-", f"YAML syntax: {e}")
        return rep
    if not isinstance(data, dict):
        rep.err("-", "document root must be a mapping")
        return rep
    if "app" in data:
        extra = [k for k in data if k != "app"]
        for k in extra:
            rep.err("-", f"AppFile extra key '{k}'")
        walk(rep, data["app"], "app", None, [data["app"]])
        app = data["app"]
        if isinstance(app, dict) and app.get("type") not in CONTAINERS:
            rep.err(app.get("id", "app"), "AppFile root type must be a container")
    else:
        walk(rep, data, path.stem, None, [data])
    return rep


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print("usage: validate_layout.py <file.yaml>...", file=sys.stderr)
        return 2
    rc = 0
    for a in argv[1:]:
        r = validate_doc(Path(a))
        for line in r.errors + r.warnings:
            print(line)
        if r.errors:
            rc = 1
            print(f"{a}: FAIL ({len(r.errors)} error(s), {len(r.warnings)} warning(s))")
        else:
            print(f"{a}: OK ({len(r.warnings)} warning(s))")
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv))
