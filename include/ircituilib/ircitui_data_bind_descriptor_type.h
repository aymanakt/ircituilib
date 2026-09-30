/**
 * @file ircitui_data_bind_descriptor_type.h
 * @brief The whole reference model, as one object.
 */
#ifndef IRCITUILIB_IRCITUI_DATA_BIND_DESCRIPTOR_TYPE_H
#define IRCITUILIB_IRCITUI_DATA_BIND_DESCRIPTOR_TYPE_H

#include <stddef.h>
#include <stdint.h>

#include "ircituilib/ircit_ui_node_type.h"

/** @brief The id index's empty marker: a slot holding no node.
 *
 * A sentinel rather than a zero, because slot 0 is a real node. It is what ends
 * a probe: a lookup walks occupied slots until it finds the id it wants or this. */
#define IRCITUI_SLOT_NONE 0xFFFFFFFFu

/*!
 * @struct IrcitUiDataBindDescriptor
 * @brief The whole reference model, as one object.
 *
 * @ref IrcitUiDataBind returns a pointer to this. Everything the reference model
 * is made of is reachable from it, so a consumer holds one handle rather than
 * the loose globals it replaces — the globals @c IrcitUiRoot, @c IrcitUiTemplates
 * and @c IrcitUiTemplateCount are gone, and these members are what a caller reads
 * instead.
 *
 * Every member is a pointer or a count, never an embedded array, so the type has
 * the same size and layout whatever the layout file declares. The storage they
 * point at is one object in `generated/ui_generated.c`, initialised at link
 * time: nothing behind this descriptor is allocated, and no code runs to
 * construct it. The model is therefore valid from process start, and
 * @ref IrcitUiDataBind constructs nothing — it hands back the handle.
 *
 * @ref templates and @ref template_count name the **unbound** nodes: the
 * template roots. They are parsed and emitted alongside the app tree but belong
 * to no parent within it, and are instantiated on demand by
 * @ref IrcitUiInflate. They are unbound, not detached — a template root has no
 * parent until a clone of it is attached to the screen tree.
 *
 * @ref nodes is the pool every node in the model lives in, contiguous, in
 * emission order. It is exposed for identity and for the id index; a consumer
 * cannot index it directly, because @ref IrcitUiNode is opaque outside the
 * library.
 *
 * @ref id_slots is the id index: an open-addressed table mapping an id to the
 * pool slot holding it, so a lookup is a hash and a bounded probe rather than a
 * search. It is **optional** — NULL means this model carries none, and a reader
 * must fall back to walking the tree. The generated model always carries one; a
 * model an application assembles itself is free not to, and pays for it in
 * lookup time rather than in correctness.
 */
typedef struct IrcitUiDataBindDescriptor {
    IrcitUiNode  *root;           ///< Root of the reference tree, or NULL when
                                  ///< the model is empty.
    IrcitUiNode  *nodes;          ///< The pool: every node in the model, in
                                  ///< emission order.
    size_t        node_count;     ///< Entries in @ref nodes.
    IrcitUiNode **templates;      ///< The unbound template roots, or NULL when
                                  ///< the layout declares none.
    size_t        template_count; ///< Entries in @ref templates.
    const uint32_t *id_slots;     ///< id -> pool slot, @ref IRCITUI_SLOT_NONE
                                  ///< where empty; NULL when the model carries
                                  ///< no index.
    size_t        id_slot_count;  ///< Entries in @ref id_slots.
    size_t        id_slot_max_probe; ///< The index's proven worst case: the
                                  ///< longest run of occupied slots, which
                                  ///< bounds a hit and a miss alike. A reader
                                  ///< may rely on it rather than assuming the
                                  ///< hash never clusters.
} IrcitUiDataBindDescriptor;

#endif /* IRCITUILIB_IRCITUI_DATA_BIND_DESCRIPTOR_TYPE_H */
