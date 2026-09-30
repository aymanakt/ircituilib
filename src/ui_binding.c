/**
 * @file ui_binding.c
 * @brief The application's callback table, and the dispatch that consults it.
 *
 * A second, application-owned table of callbacks sits alongside the engine's own
 * per-node hooks. The engine's hooks (on_input, on_inflated, …) are mechanics:
 * they are filled in by the generator and they make the widget set work. This
 * table is policy: it holds what the application decided about a node, and it is
 * the application that fills it in, at run time, after IrcitUiDataBind has
 * built the tree it resolves against.
 *
 * The two do not overlap. The engine calls its own hook and this table
 * independently, and neither is expressed in terms of the other.
 */
#include "ui_priv.h"
#include "ircituilib/config_defs.h"
#include "ircituilib/ircitui_context.h"

/* The table, and why it is a file-scope singleton for now.
 *
 * The target is one table per instance of the UI: an application that renders
 * two screens gets two tables, and a registration on one is invisible to the
 * other. There is one instance today — IrcitUiScreen is a global (ui.h), so a
 * singleton table is not a shortcut around the ownership question, it is the
 * honest shape while the question is open. When the UI becomes an object this
 * array moves into it and these three functions take that object as a first
 * parameter; nothing else about them changes. */
static IrcitUiBinding s_bindings[PRIV_CONFIG_DEFAULT_BINDING_CAP];
static int           s_binding_count;

/* The id a registration should store, or NULL when no such node exists.
 *
 * The reference tree is the right thing to ask, and the screen tree is the
 * wrong one. The reference tree is built once by IrcitUiDataBind and is never
 * cloned or reset, so the id pointer found in it is stable for the life of the
 * process — that is what lets a row keep the pointer it resolves rather than
 * copying the string, and it is why the copy a caller passes in need not
 * outlive the call. The screen tree is rebuilt by IrcitUiScreenReset, so an id
 * taken from it would be an id taken from a node that comes and goes.
 *
 * The model's unbound template roots are searched as well as its root tree,
 * because the interesting buttons live inside popups and those templates are not
 * part of the root tree's walk. Each template is searched with IrcitUiFind rather than compared
 * against its own id: a popup's contents are its descendants, so a comparison
 * against the template root alone would resolve the popup and refuse every
 * button in it - which is the opposite of the reason for searching here. */
static const char *
sResolveId(const char *id_ptr)
{
  /* One model lookup by id, which is the index when the model has one. This used
     to walk the root tree and then every template root here, in parallel with the
     engine's own copy of the same walk - two implementations of one rule, kept in
     step by hand. There is one now. */
  const IrcitUiNode *ref = IrcitUiFindModel(id_ptr);
  return ref ? ref->common.id : NULL;
}

PUBLIC_API void
IrcitUiBindingReset(void)
{
  memset(s_bindings, 0, sizeof s_bindings);
  s_binding_count = 0;
}

PUBLIC_API int
IrcitUiBindClick(const char *node_id_ptr, IrcitUiClickCallback on_click_ptr, void *user_ptr)
{
  const char *resolved;
  int         i;

  if (!node_id_ptr || !on_click_ptr) return 0;
  /* Nothing to resolve against yet. Refused rather than deferred: a
     registration that silently takes effect later would fire at a moment the
     application did not choose and cannot see. */
  if (!IrcitUiModel()->root) return 0;
  for (i = 0; i < s_binding_count; i++)
    if (strcmp(s_bindings[i].node_id_ptr, node_id_ptr) == 0) return 0;
  if (s_binding_count >= PRIV_CONFIG_DEFAULT_BINDING_CAP) return 0;
  resolved = sResolveId(node_id_ptr);
  if (!resolved) return 0;
  s_bindings[s_binding_count].node_id_ptr = resolved;
  s_bindings[s_binding_count].on_click   = on_click_ptr;
  s_bindings[s_binding_count].user_ptr   = user_ptr;
  s_binding_count++;
  return 1;
}

PUBLIC_API IrcitUiClickDisposition
IrcitUiClickDispatch(IrcitUiNode *node_ptr)
{
  int i;

  if (!node_ptr || !node_ptr->common.id) return IRCITUI_CLICK_CARRY;
  for (i = 0; i < s_binding_count; i++)
    /* The row's id pointer is the one sResolveId found in the reference tree,
       so this compares against the same literal the node carries rather than
       against a copy of it. IrcitUiBindClick refuses a NULL callback, which is
       the only way a row is ever filled, so the call below cannot be a NULL
       one. */
    if (strcmp(s_bindings[i].node_id_ptr, node_ptr->common.id) == 0) return s_bindings[i].on_click(node_ptr, s_bindings[i].user_ptr);
  return IRCITUI_CLICK_CARRY;
}
