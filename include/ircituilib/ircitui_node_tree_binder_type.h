/**
 * @file ircitui_node_tree_binder_type.h
 * @brief The callback the framework context is initialised with.
 */
#ifndef IRCITUILIB_IRCITUI_NODE_TREE_BINDER_TYPE_H
#define IRCITUILIB_IRCITUI_NODE_TREE_BINDER_TYPE_H

#include "ircituilib/ircitui_data_bind_descriptor_type.h"

/*!
 * @typedef IrcitUiNodeTreeBinder
 * @brief The application's half of the framework contract.
 *
 * This library is a framework: it drives a display model it did not author. The
 * binder is where an application says "here is my display model, and here are the
 * listeners that bind my domain to your data and interaction requirements" — the
 * model it returns carries both, so one callback delivers the whole application
 * side of the arrangement.
 *
 * An application with no model of its own passes @ref IrcitUiDataBind, which is
 * the tree this library generates; that is the ordinary case, and it makes the
 * generated model nothing more than the framework's default answer to the same
 * question an application answers for itself.
 *
 * It returns the descriptor rather than being declared `void (*)(void)`, which
 * is what it replaced. A void binder can only be called, never read from, so a
 * context given one could not receive the model it produced — which is why that
 * parameter sat unused.
 *
 * A NULL binder is a broken contract and is not defended against: it is called
 * unconditionally, so the caller's own call site is where the fault surfaces.
 */
typedef struct IrcitUiDataBindDescriptor *(*IrcitUiNodeTreeBinder)(void);

#endif /* IRCITUILIB_IRCITUI_NODE_TREE_BINDER_TYPE_H */
