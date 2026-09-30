#!/usr/bin/env python3
"""kind_schema.py — the one place a kind's declarative keys are declared.

WHY THIS FILE EXISTS

A kind's declarative attributes are declared in four hand-kept places today: the
kind tables and the per-kind rule blocks in tools/gen_ui.py, the C-series rules in
tools/validate_layout.py, the key productions in tools/layout-grammar.ebnf, and
the numbered drift rules in tools/post_generation_verifier.py. The rules are
written twice on purpose -- "C23, enforced here as well as in the validator for
C15's reason" (tools/gen_ui.py:467) -- and the duplication is documented in place.
A forgotten entry is silent: an unknown type falls back to IRCITUI_FRAME
(tools/gen_ui.py:435) rather than being refused.

This module is the single source those four consume. Each key is declared once,
with both spellings, its type, its kind scope, where its default lives, its
constraints and the sentence each refusal is reported in -- so a validator
message and a generator refusal cannot drift apart, because there is only one of
them.

THE BOUNDARY: WHICH KIND OWNS A KEY

A key is here because ONE KIND reads it, and for no other reason. The retarget
test -- "if a host can retarget a value after the tree exists, is it still a
declaration?" -- decides where a value is WRITTEN, not whether it belongs to the
kind (IRCITUILIB_GENERATOR_REFACTOR_V1.md §2.1): a member a setter writes is
still the kind's declaration, and its override lives on the model's
widget_state, which every clone inherits. So `mark_fg` and `orientation` are
here although IrcitUiWidgetDef writes them, and `item_template` and `row_height`
are here although IrcitUiRecyclerCreate writes them.

What is absent is what no single kind owns. `theme_class` is read at paint by
every kind (src/ui_engine.c:2986, written by src/ui_theme.c:131), and the
members of common that every kind reads -- geometry, colours, flags, gravity,
the eight constraint ints, text -- are not declarations at all but the node's
own vocabulary. `bar_fill_glyph` and the banner's ink are kind-scoped and could
be here; they are not, because Item A is bounded by its scope statement (§4.1).

WHERE A DECLARATION IS STORED

Not on common. Each node carries `common.declarative_attributes_aux`, one
borrowed pointer to a per-kind generated const block, or NULL where the layout
declared nothing (IRCITUILIB_GENERATOR_REFACTOR_V1.md §2.1). This file is what
decides the block's members: the type generator emits one struct per kind from
KEYS_FOR(), and the generator emits one block per distinct declared set.

WHAT THIS CARRIES, AND WHAT IT DELIBERATELY DOES NOT

This is the whole of the census's kind-scoped storage: 106 B across eight families,
decided 2026-09-26 (IRCITUILIB_GENERATOR_REFACTOR_V1.md §1.1 and §4.1). Every
member is here except the unread banner's ink, which is the one member with no
YAML key at all -- it is set only through
IrcitUiConversationWindowSetNewMsgBannerStyle -- so it has no declaration to
carry and moves entirely into the model's state (§2.1).

What is absent is the kind-level requirements that range across two keys or
across keys and kinds: C16/C21's "a recycler needs item_template or view_types",
C22's "sticky_headers names a type_id the registry declares", C25's wrap_content
refusal on a conversation window. Those are not per-key constraints, and they are
the next thing this file should carry rather than the last.

THE TABLE TYPE

`view_types` is a list of maps, and the first key here that reaches C as a
pointer with a count -- const IrcitUiViewTypeMapping *view_types, with
n_view_types beside it. §2.2's table type exists for it and for the composite's
indicator table, so the generator emits both through one piece of machinery
rather than two.

Adding a kind here is appending to KIND. The order of that table IS the order of
ircit_ui_kind_enum.h, and post_generation_verifier.py asserts the two agree
rather than trusting them to.
"""
from __future__ import annotations

# ---------------------------------------------------------------------------
# --- the keys a value cascades through (IRCITUILIB_GENERATOR_REFACTOR_V1.md §4.1) ---
#
# A layout is verbose because a property must be written again on every child that
# wants what its parent has. These two are the keys where that repetition is worth
# removing, and they are NOT declarations: every kind may name them, they are read
# by the layout pass rather than by a widget, and nothing about them is per-kind.
# What they share with a declaration is that they are written once and meant once.
#
# NOTHING IMPLEMENTS THIS YET, and the reason is measured rather than open: resolving
# it unconditionally changes 13 nodes of the shipped layout, because every child that
# declares no size begins inheriting its parent's -- a text_view inside a match_parent
# parent goes from WRAP to MATCH. The resolution has to run BEFORE the generator's
# dim(), which folds an absent key and `wrap_content` into one value and would leave
# nothing to inherit from, and precedence would be authored > inherited > dim()'s
# default. Whether that is worth 13 nodes of changed behaviour, or wants a per-document
# opt-in -- which would be an addition to the layout language's vocabulary and not a
# tool's private business -- is the owner's call. The set is declared here because it
# is a property of these two keys, not of a tool.
#
# Measured on the shipped layouts: of the 140 declarations these two carry, 9 are a
# child restating an effective value its parent already had, and those 9 are what
# this set removes. Styling does NOT cascade, and that is measured too: carrying it
# into the three colour keys would remove one declaration of 58.
CASCADING_KEYS = ("layout_width", "layout_height")

# The kinds, in the order include/ircituilib/ircit_ui_kind_enum.h declares them.
#
# The spelling on the left is the layout's `type:`; on the right the C enumerator
# the generator emits. The order is load-bearing and append-only: the value is
# serialised into the generated model and compared by the verifier, so a kind
# inserted anywhere but the end renumbers every kind after it.
# ---------------------------------------------------------------------------
KIND = {
    "text_view": "IRCITUI_TEXT_VIEW",
    "button": "IRCITUI_BUTTON",
    "image_view": "IRCITUI_IMAGE_VIEW",
    "edit_text": "IRCITUI_EDIT_TEXT",
    "check_box": "IRCITUI_CHECK_BOX",
    "radio_button": "IRCITUI_RADIO_BUTTON",
    "switch": "IRCITUI_SWITCH",
    "progress_bar": "IRCITUI_PROGRESS_BAR",
    "seek_bar": "IRCITUI_SEEK_BAR",
    "emoji_table": "IRCITUI_EMOJI_TABLE",
    "linear": "IRCITUI_LINEAR",
    "frame": "IRCITUI_FRAME",
    "constraint": "IRCITUI_CONSTRAINT",
    "radio_group": "IRCITUI_RADIO_GROUP",
    "check_group": "IRCITUI_CHECK_GROUP",
    "recycler": "IRCITUI_RECYCLER",
    "conversation_window": "IRCITUI_CONVERSATION_WINDOW",
}

# The kinds with an extent of their own to fold, and children to hide behind it.
# Mirrors IrcitUiIsContainer() in src/ui_engine.c, CONTAINER_KINDS in
# tools/gen_ui.py and the Containers rule in tools/layout-grammar.ebnf.
CONTAINER_KINDS = (
    "linear", "frame", "constraint", "radio_group", "check_group", "recycler",
)

# The kinds that pack along `orientation`, and so the only ones that may declare
# it. Narrower than CONTAINER_KINDS: a frame, a constraint and a recycler place
# their children by other means, and the validator has refused `orientation` on
# them since C-series rule 216. Kept in step with LINEARISH in
# tools/validate_layout.py.
LINEARISH = ("linear", "radio_group", "check_group")

# The kinds that draw a mark, and so the only ones `mark_colour` can colour.
# Every one renders through sPaintToggleMark in the engine, which is what makes
# the key mean the same thing on all five. Kept in step with TOGGLES in
# tools/validate_layout.py.
TOGGLES = ("check_box", "radio_button", "switch", "radio_group", "check_group")

# The palette's eight named colours, as the layout spells them. The value a key
# of type COLOUR takes; unlisted spellings are refused rather than defaulted.
COLOURS = ("black", "red", "green", "yellow", "blue", "magenta", "cyan", "white")

# ---------------------------------------------------------------------------
# The key types. These are the shapes the type generator and the emit branch on,
# and the reason a key's C type is not written down here: it is derived from the
# type name, in one place, so a member's spelling and its type cannot disagree.
# ---------------------------------------------------------------------------
GLYPH = "glyph"        # const char * -- exactly one code point, default in the engine
STRING = "string"      # const char * -- free text or a template id, "" / NULL = none
CELLS = "cells"        # int -- a positive cell count
INTEGER = "integer"    # int -- a bounded count, 0 = not given
ENUM = "enum"          # int -- one of a named set of C enumerators
COLOUR = "colour"      # int -- a 0-7 colour index, or the kind's own sentinel
TABLE = "table"        # a list of maps: a const element array plus its count

# The two kinds that draw a value as a filled track, and so the only ones the
# three bar keys are declared on. Mirrors BARS in tools/gen_ui.py and in
# tools/validate_layout.py, and the case labels in the engine's bar painter.
BARS = ("progress_bar", "seek_bar")

# ---------------------------------------------------------------------------
# The declarative keys.
#
# Every field earns its place (IRCITUILIB_GENERATOR_REFACTOR_V1.md §2.2):
#
#   yaml_key     the layout's spelling
#   c_member     the C member's spelling -- the two already differ, and a record
#                with one name for both emits a member the widget does not read
#   type         the shape, above
#   kinds        the kinds that may declare it; a key on any other kind parses,
#                validates and generates, and is then never read
#   default_ref  where the value comes from when the key is absent. A NAMED
#                fallback, never a number: the engine's own default is the only
#                copy, and a number restated here is a second one that a
#                regeneration would pin into the generated file
#   checks       the constraints, each carrying the sentence it is refused in
#   doc          one line, emitted as the member's comment in the generated header
#   family       which family's removal stage the key belongs to (§7's S3), so a
#                family is named in one place and the removal is a flag, not a hunt
#   fallback     the C expression an accessor answers with once the flat member is
#                gone. While it is still there the accessor reads it instead, which
#                is what makes the two paths agree during the window (§2.5)
#   sentinel     present only where the member's zero is a LEGAL value: the block
#                then carries this instead of zero when the layout declared
#                nothing, or the accessor could not tell "declared 0" from "not
#                given" -- the trap sticky_header_type's own comment records
#
# The refusal sentences are templates. {k} is the layout's spelling, {v!r} the
# offending value, {kinds} the kinds that would have accepted it, {typ} the kind
# that did not, {n} a measured count. Each consumer prefixes its own location --
# the validator a file and a node id, the generator a node id -- and prints the
# sentence unchanged.
# ---------------------------------------------------------------------------
DECLARATIVE_KEYS = (
    {
        "yaml_key": "collapsible_indicator_open",
        "family": "collapse",
        "fallback": "0",
        "c_member": "indicator_open",
        "type": GLYPH,
        "kinds": CONTAINER_KINDS,
        "default_ref": "the engine's sIndicatorGlyph fallback, src/ui_engine.c:2269-2281",
        "checks": (
            {"test": "glyph"},
        ),
        "doc": "the glyph a folded container shows while it is open",
    },
    {
        "yaml_key": "collapsible_indicator_close",
        "family": "collapse",
        "fallback": "0",
        "c_member": "indicator_close",
        "type": GLYPH,
        "kinds": CONTAINER_KINDS,
        "default_ref": "the engine's sIndicatorGlyph fallback, src/ui_engine.c:2269-2281",
        "checks": (
            {"test": "glyph"},
        ),
        "doc": "the glyph a folded container shows while it is folded",
    },
    {
        "yaml_key": "collapsible_fill",
        "family": "collapse",
        "fallback": "0",
        "c_member": "collapse_fill",
        "type": GLYPH,
        "kinds": CONTAINER_KINDS,
        "default_ref": "the engine's sFillGlyph fallback, src/ui_engine.c:2284-2288",
        "checks": (
            {"test": "glyph"},
        ),
        "doc": "the glyph painted across a folded strip",
    },
    {
        "yaml_key": "bubble_max_width_percent",
        "family": "conversation",
        "fallback": "0",
        "c_member": "bubble_max_pct",
        "type": INTEGER,
        "kinds": ("conversation_window",),
        "default_ref": "the engine's 65 per cent, src/include/ui_priv.h:232-233",
        "checks": (
            {"test": "range", "min": 1, "max": 100},
        ),
        "doc": "the share of the widget's width one bubble may occupy",
    },
    {
        "yaml_key": "bubble_collapse_lines",
        "family": "conversation",
        "fallback": "0",
        "c_member": "bubble_collapse_lines",
        "type": INTEGER,
        "kinds": ("conversation_window",),
        "default_ref": "the engine's six-line ceiling, src/include/ui_priv.h:232-233",
        "checks": (
            {"test": "min", "min": 1},
        ),
        "doc": "the line count above which a bubble is truncated",
    },
    {
        "yaml_key": "item_template",
        "family": "recycler",
        "fallback": "0",
        "c_member": "item_template",
        "type": STRING,
        "kinds": ("recycler",),
        "default_ref": "NULL -- a recycler naming neither item_template nor view_types is refused (C16/C21)",
        "checks": (
            {"test": "non_empty"},
        ),
        "doc": "the orphan template cloned once for each visible row",
    },
    {
        "yaml_key": "row_height",
        "family": "recycler",
        "fallback": "0",
        "c_member": "row_height",
        "type": CELLS,
        "kinds": ("recycler",),
        "default_ref": "0, which the engine floors to 1 -- it is a divisor in the layout pass",
        "checks": (
            {"test": "cells"},
        ),
        "doc": "the rows each pooled row occupies",
    },
    {
        "yaml_key": "mark_colour",
        "family": "toggle",
        "fallback": "-1",
        "sentinel": "-1",
        "c_member": "mark_fg",
        "type": COLOUR,
        "kinds": TOGGLES,
        "default_ref": "-1: the mark takes the node's own text_colour",
        "checks": (
            {"test": "colour"},
        ),
        "doc": "the ink of a toggle's mark; unset means it follows text_colour",
    },
    {
        "yaml_key": "orientation",
        "family": "orient",
        "fallback": "IRCITUI_VERT",
        "sentinel": "-1",
        "c_member": "orientation",
        "type": ENUM,
        "kinds": LINEARISH,
        "default_ref": "IRCITUI_VERT",
        "checks": (
            {"test": "enum", "values": {"vertical": "IRCITUI_VERT",
                                        "horizontal": "IRCITUI_HORIZ"}},
        ),
        "doc": "the axis a container packs its children along",
    },
    {
        "yaml_key": "layout_template",
        "family": "template",
        "fallback": "0",
        "c_member": "layout_template",
        "type": STRING,
        # Every kind: the engine inflates a named template on a click of ANY node
        # (src/ui_engine.c:4652-4664), so this is the one key here whose scope is
        # not a kind list. The validator's own rule only warns, and only about
        # buttons, which is a convention rather than a scope.
        "kinds": tuple(KIND),
        "default_ref": "NULL -- the node declares no template",
        "checks": (
            {"test": "non_empty"},
        ),
        "doc": "the template inflated when this node is clicked",
    },
    {
        "yaml_key": "fill_glyph",
        "family": "bar",
        "fallback": "0",
        "c_member": "bar_fill_glyph",
        "type": GLYPH,
        "kinds": BARS,
        "default_ref": "the engine's sBarDefaultFill, src/ui_engine.c:2584-2587",
        "checks": (
            {"test": "glyph"},
        ),
        "doc": "the glyph painted across a bar's filled track",
    },
    {
        "yaml_key": "empty_glyph",
        "family": "bar",
        "fallback": "0",
        "c_member": "bar_empty_glyph",
        "type": GLYPH,
        "kinds": BARS,
        "default_ref": "the engine's sBarDefaultEmpty, src/ui_engine.c:2584-2587",
        "checks": (
            {"test": "glyph"},
        ),
        "doc": "the glyph painted across a bar's empty track",
    },
    {
        "yaml_key": "label_mode",
        "family": "bar",
        "fallback": "IRCITUI_BAR_LABEL_OFF",
        "sentinel": "-1",
        "c_member": "bar_label_mode",
        "type": ENUM,
        "kinds": BARS,
        # Three values, and no fourth: "time" is deliberately absent, because
        # nothing implements it and a value that resolves to nothing is a trap
        # rather than a reservation -- the reason C24 gives.
        "default_ref": "IRCITUI_BAR_LABEL_OFF, the enum's zero and what the engine reads as no label",
        "checks": (
            {"test": "enum", "values": {"off": "IRCITUI_BAR_LABEL_OFF",
                                        "adjacent": "IRCITUI_BAR_LABEL_ADJACENT",
                                        "centered": "IRCITUI_BAR_LABEL_CENTERED"}},
        ),
        "doc": "where a bar draws its value's label",
    },
    {
        "yaml_key": "view_types",
        "family": "registry",
        "fallback": "0",
        "c_member": "view_types",
        "type": TABLE,
        "element": "IrcitUiViewTypeMapping",
        "element_header": "ircituilib/ircit_ui_view_type_mapping_type.h",
        "count_member": "n_view_types",
        "kinds": ("recycler",),
        "default_ref": "NULL and 0 -- the homogeneous case, which item_template spells",
        "checks": (
            {"test": "non_empty"},
            {"test": "entries", "fields": {"type_id": "non_negative_int",
                                           "template": "non_empty_str"}},
        ),
        "doc": "the template each application-supplied row type is cloned from",
    },
    {
        "yaml_key": "sticky_headers",
        "family": "registry",
        "fallback": "-1",
        "sentinel": "-1",
        "c_member": "sticky_header_type",
        "type": INTEGER,
        "kinds": ("recycler",),
        # Always emitted, -1 included, and it is the opposite of what
        # item_template does: the engine tells "not given" from a real value by
        # the SIGN, and 0 is a legal type_id, so the member's own zero cannot be
        # the sentinel.
        "default_ref": "-1 -- off, and emitted even then, because 0 is a legal type_id",
        "checks": (
            {"test": "min", "min": 0},
            {"test": "declared_type_id"},
        ),
        "doc": "the type_id whose rows stick to the top of the viewport",
    },
    {
        "yaml_key": "bubble_avatar_gutter_cols",
        "family": "gutter",
        "fallback": "0",
        "c_member": "bubble_avatar_gutter_cols",
        "type": INTEGER,
        "kinds": ("conversation_window",),
        # Six is the floor: the timestamps the gutter is reserved beside are six
        # columns wide, so below it a stamp row and an avatar would share cells.
        "default_ref": "0 -- no gutter reserved",
        "checks": (
            {"test": "range", "min": 6, "max": 12},
        ),
        "doc": "the columns reserved at the viewport's edge for an avatar",
    },
)

# The refusal sentences, one per constraint test, so a rule and its wording are
# read together. The scope sentence is per-key rather than global because the
# kinds it names are the key's own.
#
# A consumer fills these in and prints them unchanged, prefixing its own location
# -- the validator a file and a node id, the generator a node id. Where a
# placeholder names a set, the consumer supplies it ready to read: {values} and
# {colours} are sorted, comma-joined spellings, and {kinds} is the same for a
# key's scope.
SCOPE_REFUSAL = (
    "{k} only allowed on {kinds}; this node is type '{typ}'"
)
REFUSALS = {
    "glyph": "{k} must be exactly one glyph, not {n}; omit the key to take the framework default",
    "non_empty": "{k} must name something; omit the key rather than writing an empty value",
    "cells": "{k} must be a positive int",
    "range": "{k} must be an integer {min}-{max}, got {v!r}",
    "min": "{k} must be a positive integer, got {v!r}",
    "enum": "{k} {v!r} is not one of {values}",
    "colour": "{k} {v!r} is not one of {colours}",
    "entries": "each {k} entry must be a mapping of {fields}",
    "type_id": "{k} type_id must be a non-negative integer, got {v!r}",
    "duplicate_type_id": "{k} type_id {v} is declared twice -- which entry wins would be an implementation detail",
    "declared_type_id": "{k} type_id {v} is not declared by view_types -- declared: {declared}",
}

# ---------------------------------------------------------------------------
# Lookups. Built once from the table above, so a consumer never re-derives the
# kind scopes by hand -- which is how the four hand-kept lists drifted.
# ---------------------------------------------------------------------------
BY_YAML_KEY = {e["yaml_key"]: e for e in DECLARATIVE_KEYS}
BY_C_MEMBER = {e["c_member"]: e for e in DECLARATIVE_KEYS}
# The C enumerator a layout's `type:` resolves to, back to the layout's spelling.
# The generator holds nodes in the C vocabulary and the schema's scopes are
# written in the layout's, so the one place the two are translated is here.
YAML_KIND = {v: k for k, v in KIND.items()}

# What moves off IrcitUiCommonState: the census's 106 B (§1.1), one entry per
# member a table reaches through its count as well as its pointer. The verifier
# asserts none of these is a member of IrcitUiCommonState once the removal stage
# completes, and that the set is exactly the schema's.
# The families whose flat members have been removed. A family is added here in
# the SAME change that deletes its members from IrcitUiCommonState, so the
# compiler finds every writer still reaching for one.
#
# These are the families NO SETTER WRITES, which is why they need nothing from
# §2.1's write path: a member a setter can reach needs somewhere for the write to
# land, and that is the copy-on-write state, which the remaining families land
# with. A family is as small as its writability allows -- the conversation window
# had to be split, because its two display keys have no writer at all and its
# avatar gutter has one.
REMOVED_FAMILIES = ("collapse", "template", "conversation", "bar", "recycler", "registry", "gutter", "toggle", "orient")

MIGRATED_MEMBERS = tuple(
    m for e in DECLARATIVE_KEYS for m in (e["c_member"], e.get("count_member")) if m
)


def keys_for(kind):
    """The declarative keys a kind of this name may carry, in table order.

    `kind` is the layout's spelling, not the enumerator: the scope tables above
    are written in the layout's vocabulary, and the one place the two are
    translated is KIND."""
    return tuple(e for e in DECLARATIVE_KEYS if kind in e["kinds"])


def kinds_refusing(key):
    """The kinds a key is refused on, for the sentence that says so."""
    return tuple(k for k in KIND if k not in key["kinds"])


def key_of_member(member):
    """The key a C member belongs to, a table's count member included.

    A count member is not a key of its own -- nobody declares `n_view_types` --
    so a lookup by member would miss it and the caller would have to special-case
    the one table the schema happens to have today."""
    if member in BY_C_MEMBER:
        return BY_C_MEMBER[member]
    for entry in DECLARATIVE_KEYS:
        if entry.get("count_member") == member:
            return entry
    raise KeyError(member)


def decl_type_name(kind):
    """The declaration type for one kind: conversation_window ->
    IrcitUiConversationWindowDeclarativeAttributesAux.

    One naming rule, read by the type generator that declares these and by the
    model generator that points at them, so the two cannot spell it differently."""
    return "IrcitUi" + "".join(p[:1].upper() + p[1:] for p in kind.split("_")) \
        + "DeclarativeAttributesAux"


def enum_values(yaml_key):
    """The yaml-spelling -> C enumerator map of an enum key, in enum order.

    The generator emits the enumerator; the layout writes the spelling; this is
    the map between them, and it is also the order the modes are numbered in."""
    for check in BY_YAML_KEY[yaml_key]["checks"]:
        if check["test"] == "enum":
            return dict(check["values"])
    raise KeyError(f"{yaml_key} is not an enum key")
