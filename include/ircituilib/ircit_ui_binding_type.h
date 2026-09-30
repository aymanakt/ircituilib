/**
 * @file ircit_ui_binding_type.h
 * @brief One application callback, attached to a node by id.
 */
#ifndef IRCITUI_IRCIT_UI_BINDING_TYPE_H
#define IRCITUI_IRCIT_UI_BINDING_TYPE_H

#include "ircituilib/ircit_ui_click_callback_type.h"

/*!
 * @struct IrcitUiBinding
 * @brief One row of the application's callback table.
 *
 * Keyed by node id rather than by node pointer, and that is the point of the
 * struct. A popup is inflated by cloning its template, so a rendered node is a
 * fresh object every time a layer opens; a table keyed on the node address
 * would hold an address that dies with the first pop, and would have to be
 * re-registered after every one. An id outlives all of its clones, so one
 * registration covers every instance the application will ever render.
 *
 * The id must name a node in the reference model — the tree @ref IrcitUiDataBind
 * builds, including its unbound template roots — which is what makes the
 * registration checkable at the time it is made rather than at the time it
 * silently fails to fire.
 */
typedef struct IrcitUiBinding
{
  const char *node_id_ptr; ///< Id of the node to attach to; resolved against the reference tree when registered.
  IrcitUiClickCallback on_click; ///< Called when that node is activated. NULL leaves the row inert.
  void *user_ptr; ///< Passed through to @ref on_click; ownership stays with the application.
} IrcitUiBinding;

#endif /* IRCITUI_IRCIT_UI_BINDING_TYPE_H */
