/**
 * @file ui_engine.c
 * @brief Layout, realize, draw, and event dispatch.
 */
#include "ui_priv.h"
// This unit's own declarations, so the two fast-path definitions below are checked against them.
#include "ui_engine_priv.h"
#include "generated/ui_decl_access.h"   /* the generated accessors: one per declared member */
#include "ui_log.h"
/* The per-widget extension structs IrcitUiCommonState.widget_state points at,
   for the kinds that carry one. Co-located with ui_priv.h, hence the bare name.
   A recycler's state owns a pool of row nodes, which is why the teardown below
   has to know about it and cannot simply free the pointer. */
#include "ircitui_text_edit_state_type_priv.h"
#include "ircitui_edit_text_priv.h"
#include "ircitui_recycler_type_priv.h"
#include "ircitui_bar_state_type_priv.h"
#include "ircitui_conversation_state_type_priv.h"
#include "ircituilib/ircitui_conversation.h"
#include "ircituilib/ircitui_edit_text.h"
#include "ircituilib/config_defs.h"
#include "ircituilib/ircitui_context.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <time.h>
#include <curses.h>
#include <version.h>

IrcitUiNode *       IrcitUiFocus;
static IrcitUiNode *sDragNode;
static int          drag_off_x, drag_off_y;
static void
sOffsetTree(IrcitUiNode *node_ptr, int dx, int dy);
static void
sFreeTree(IrcitUiNode *node_ptr);
/* The two are mutually recursive: a recycler's state owns a pool of nodes that
   are torn down with sFreeTree, and sFreeTree reaches the state through here.
   Declared rather than ordered, because ordering them would mean putting the
   kind-specific teardown above the generic sweep it belongs with. */
static void
sFreeWidgetState(IrcitUiNode *node_ptr);
/* Declared here because the public lookups below are defined beside IrcitUiFind,
   well before the model-resolution helpers they dispatch to. */
static const IrcitUiNode *
sLookupRef(const char *id_ptr);
/* Same reason: the mouse matrix sits with the click it serves, which is far above
   the selection helpers it drives. Every recycler call it makes is public and so
   already visible; this is the one that is not. */
static void
sCursorFollow(IrcitUiNode *node_ptr, IrcitUiRecyclerState *state_ptr);
/* Same reason again: SS26's key intercept runs at the top of IrcitUiDispatchKey,
   which is defined long before the recycler's own entry points. */
static int
sRecyclerEditKey(int key);
static void
sRecyclerEndEdit(IrcitUiNode *node_ptr, IrcitUiRecyclerState *state_ptr, int commit);

static MEVENT       sLastMouse;
static IrcitUiNode *sSelNode;
static IrcitUiNode *sScrollDrag;
static IrcitUiNode *sCtxTarget;

/* The single writer for the mouse record above.
 *
 * It is a static here rather than a member of the event because the emoji grid
 * reads its click column out of it, and it has done so since before events
 * carried coordinates. Callers outside this translation unit - which is the
 * dispatcher, posting a click - go through this rather than reaching for the
 * variable, so the store has one writer and a reader can rely on it describing
 * the event that just arrived. */
void
IrcitUiRecordMouse(int mouse_x, int mouse_y)
{
  sLastMouse.x = mouse_x;
  sLastMouse.y = mouse_y;
}


/* IrcitUiFocus was written inline in six places and read as the delivery gate in
   IrcitUiDispatchKey. These are the single writer, the predicate behind it, and the
   resolver that decides where focus goes when it has been lost. */

/* Is n inside the subtree rooted at root? Walks parents rather than descending,
   so cost is depth, not subtree size. */
static int
sInSubtree(const IrcitUiNode *root_ptr, const IrcitUiNode *node_ptr)
{
  const IrcitUiNode *p;
  for (p = node_ptr; p; p = p->common.parent) if (p == root_ptr) return 1;
  return 0;
}

static const char sBarRampOwner[] = "bar-gradient";

/* The bar's own state, or NULL when the node is not a bar or was given none.

   Two kinds, one painter: IRCITUI_PROGRESS_BAR and IRCITUI_SEEK_BAR have always
   shared their wrap width (sWrapW below) and their meaning -- a horizontal track
   whose fill says how far along something is. They differ in the name a layout
   writes, and nothing else, so they share this state and sBarPaint too. The
   alternative, a case each, is two painters to keep in step over a difference
   nobody asked for. */
static IrcitUiBarState *
sBarState(const IrcitUiNode *node_ptr)
{
  if (!node_ptr) return NULL;
  if (node_ptr->common.kind != IRCITUI_PROGRESS_BAR
      && node_ptr->common.kind != IRCITUI_SEEK_BAR) return NULL;
  /* The cast drops const because the state is not const: a node reached through a
     const pointer may still be a clone whose state was calloc'd mutably, and the
     readers that take a const node only read it. */
  return (IrcitUiBarState *)node_ptr->common.widget_state;
}

/* The bar's own glyph or label mode, the override first.
 *
 * A setter writes the state, the layout writes the block, and the framework
 * supplies the default -- three sources, read in that order and in one place, so
 * no two of them can disagree about which wins. */
static const char *
sBarGlyphOf(const char *override_ptr, const char *declared)
{
  if (override_ptr && override_ptr[0]) return override_ptr;
  if (declared && declared[0]) return declared;
  return NULL;
}

/* The bar's label mode on the same three-source terms: the setter's override,
   then the block, which is already OFF where the layout declared nothing. */
static int
sBarLabelModeOf(const IrcitUiNode *node_ptr)
{
  const IrcitUiBarState *state = sBarState(node_ptr);
  if (state && state->label_mode >= 0) return state->label_mode;
  return IrcitUiDeclBarLabelMode(node_ptr);
}

/* Whether a kind keeps the mark_fg / orientation override in a state of THIS
   struct's shape. The five toggles and five of the six containers, and nothing
   else -- and NOT the recycler, which is a container but already owns a state of
   its own: giving it one of these would take the place of IrcitUiRecyclerState and
   leave every recycler without its rows. Its orientation is not read either: a
   recycler's rows are allocated to fit an extent, so it packs along no axis. */
static int
sCarriesOverrides(IrcitUiKind kind)
{
  return kind == IRCITUI_CHECK_BOX || kind == IRCITUI_RADIO_BUTTON || kind == IRCITUI_SWITCH
      || kind == IRCITUI_RADIO_GROUP || kind == IRCITUI_CHECK_GROUP
      || kind == IRCITUI_LINEAR || kind == IRCITUI_FRAME || kind == IRCITUI_CONSTRAINT;
}

/* The override state for a kind that keeps one, allocated if the node has none.
   sApplyDef runs BEFORE sAllocWidgetState on the def route, so a def that asks for
   a non-default value has to be able to create the state itself -- without this, a
   def-built container asking for HORIZ was written into nothing and the node came
   out VERT. */
static IrcitUiDeclOverrides *
sEnsureOverrides(IrcitUiNode *node_ptr)
{
  IrcitUiDeclOverrides *over;

  if (!sCarriesOverrides(node_ptr->common.kind)) return NULL;
  if (node_ptr->common.widget_state) return (IrcitUiDeclOverrides *)node_ptr->common.widget_state;
  over = calloc(1, sizeof(IrcitUiDeclOverrides));
  if (!over) return NULL;
  over->mark_fg     = -1;
  over->orientation = -1;
  node_ptr->common.widget_state = over;
  return over;
}

/* The override a setter wrote, or NULL where the kind keeps none. */
static const IrcitUiDeclOverrides *
sDeclOverrides(const IrcitUiNode *node_ptr)
{
  if (!node_ptr || !sCarriesOverrides(node_ptr->common.kind)) return NULL;
  return (const IrcitUiDeclOverrides *)node_ptr->common.widget_state;
}

/* A toggle's mark ink: the setter's override, then the block, then the node's own
   text colour -- the same three sources sBarGlyphOf reads, in the same order. */
static int
sMarkFgOf(const IrcitUiNode *node_ptr)
{
  const IrcitUiDeclOverrides *over = sDeclOverrides(node_ptr);
  if (over && over->mark_fg >= 0) return over->mark_fg;
  return IrcitUiDeclMarkFg(node_ptr);
}

/* A container's packing axis, on the same three-source terms. */
static int
sOrientationOf(const IrcitUiNode *node_ptr)
{
  const IrcitUiDeclOverrides *over = sDeclOverrides(node_ptr);
  if (over && over->orientation >= 0) return over->orientation;
  return IrcitUiDeclOrientation(node_ptr);
}

// The one place a field's extension struct is created, so every node-creation path gives it the same state.
// A calloc failure leaves the field without state rather than failing the node.
static void
sAllocWidgetState(IrcitUiNode *node_ptr)
{
  if (node_ptr->common.kind == IRCITUI_CONVERSATION_WINDOW) {
    IrcitUiConversationState *state_ptr = IrcitUiConversationStateAlloc();
    node_ptr->common.widget_state = state_ptr;
    if (state_ptr && node_ptr->common.ref) {
      // IrcitUiClone sets common.ref before it calls this, so the node being built can see the model node it was cloned from.
      const IrcitUiConversationState *model_state =
        (const IrcitUiConversationState *)node_ptr->common.ref->common.widget_state;
      if (model_state) {
        // The ADAPTER is inherited so a model-node binding reaches every clone; the
        // DATASET is not, so no string is copied per clone. The clone reconciles.
        state_ptr->adapter          = model_state->adapter;
        state_ptr->host_dataset_ctx = model_state->host_dataset_ctx;
        state_ptr->avatar_provider  = model_state->avatar_provider;
        /* The two a setter can write, where the model carries one: a model node with no
           state at all leaves the clone to its block (the generator's), and anything the
           model did set wins, because a host asked for it after the layout was written. */
        state_ptr->avatar_gutter_override = model_state->avatar_gutter_override;
        state_ptr->banner_slot            = model_state->banner_slot;
        state_ptr->banner_attrs           = model_state->banner_attrs;
        if (state_ptr->adapter.get_item_count && state_ptr->adapter.get_item_info)
          state_ptr->needs_reconcile = true;
      }
    }
    return;
  }
  /* The eleven kinds that carry mark_fg or orientation and have no state of their
     own: the five toggles and the six containers. Both values are -1 (not given)
     so a zeroed state cannot shadow a declared value -- IRCITUI_VERT is 0 and
     COLOR_BLACK is a real ink. See ircitui_decl_override_type_priv.h. */
  if (sCarriesOverrides(node_ptr->common.kind)) {
    IrcitUiDeclOverrides *      over       = sEnsureOverrides(node_ptr);
    const IrcitUiDeclOverrides *model_over = NULL;
    if (over && node_ptr->common.ref && sCarriesOverrides(node_ptr->common.ref->common.kind)) {
      model_over = (const IrcitUiDeclOverrides *)node_ptr->common.ref->common.widget_state;
    }
    if (over && model_over) {
      if (model_over->mark_fg >= 0) over->mark_fg = model_over->mark_fg;
      if (model_over->orientation >= 0) over->orientation = model_over->orientation;
    }
    return;
  }
  if (node_ptr->common.kind == IRCITUI_EDIT_TEXT) {
    IrcitUiEditTextStateAttach(node_ptr);
  } else if (node_ptr->common.kind == IRCITUI_PROGRESS_BAR
             || node_ptr->common.kind == IRCITUI_SEEK_BAR) {
    IrcitUiBarState *bar = calloc(1, sizeof(IrcitUiBarState));
    node_ptr->common.widget_state = bar;
    if (bar) {
      bar->ramp_base = -1;
      bar->label_mode = -1;   /* 0 is a real mode; see the state's own comment. */
    }
  } else if (node_ptr->common.kind == IRCITUI_RECYCLER) {
    IrcitUiRecyclerState *recycler = calloc(1, sizeof(IrcitUiRecyclerState));
    if (recycler) {
      // IrcitUiClone sets common.ref before it calls this, so the node being built can see the model node it was cloned from.
      const IrcitUiNode *         model_ref = node_ptr->common.ref;
      const IrcitUiRecyclerState *model_state =
        model_ref ? (const IrcitUiRecyclerState *)model_ref->common.widget_state : NULL;

      recycler->template_id = IrcitUiDeclItemTemplate(node_ptr);
      recycler->view_types   = IrcitUiDeclViewTypes(node_ptr);
      recycler->n_view_types = IrcitUiDeclNViewTypes(node_ptr);
      recycler->sticky_type    = IrcitUiDeclStickyHeaderType(node_ptr);
      recycler->header_node    = NULL;
      recycler->pinned_section = IRCITUI_NO_SECTION;
      recycler->editing_index   = IRCITUI_NO_SECTION;
      recycler->editing_type_id = -1;
      recycler->edit_focus_pending = 0;
      recycler->last_press_ns      = 0;
      recycler->last_press_index   = IRCITUI_NO_SECTION;
      recycler->item_fixed_height = IrcitUiDeclRowHeight(node_ptr) > 0 ? IrcitUiDeclRowHeight(node_ptr) : 1;
      // The framework's default highlight
      recycler->highlight_attr_mask = A_REVERSE;

      if (model_state) {
        recycler->adapter = model_state->adapter;
        /* The five the layout declares, where the MODEL carries them: a recycler
           built through the C API has no block for them to come from
           (IrcitUiRecyclerCreate), and IrcitUiRecyclerSetViewTypes can retarget
           one after the tree exists. Anything the model did not set is left as
           the block and the defaults gave it. */
        if (model_state->template_id) recycler->template_id = model_state->template_id;
        if (model_state->view_types) {
          recycler->view_types   = model_state->view_types;
          recycler->n_view_types = model_state->n_view_types;
        }
        if (model_state->item_fixed_height > 1) recycler->item_fixed_height = model_state->item_fixed_height;
        if (model_state->sticky_type >= 0) recycler->sticky_type = model_state->sticky_type;
        if (model_state->highlight_attr_mask) recycler->highlight_attr_mask = model_state->highlight_attr_mask;
        recycler->selection_mode = model_state->selection_mode;
        if (model_state->editing_type_id >= 0) recycler->editing_type_id = model_state->editing_type_id;
      }
    }
    node_ptr->common.widget_state = recycler;
  }
}

/* --- Invalidation -----------------------------------------------------------
 *
 * Two flags answering two different questions, and one list recording what the
 * frame must revisit.
 *
 *   is_dirty          "is the geometry stale?"  Answered by a top-down sweep
 *                     from the root, which is the only node that can start one.
 *   IRCITUI_F_DRAWN   "are this node's cells current?"  Cleared by anything
 *                     that rewrites them.
 *
 * A paint clears IRCITUI_F_DRAWN on the subtree beneath the node it paints,
 * because a derwin child shares its parent's window storage: erasing the parent
 * erases the child's cells with it, so a child skipped as "already drawn" would
 * be left blank. That cascade is what makes the flag honest again after a
 * repaint, and it is why the two flags cannot be collapsed into one.
 *
 * EVERY writer that clears IRCITUI_F_DRAWN registers through sPaintDirty, not
 * only the invalidators below. A node repainted this frame had its cells
 * rewritten, and the draw pass needs to know which clean nodes overlap it in
 * order to push them again afterwards -- ncurses resolves stacking by refresh
 * order, so a repaint below a clean node otherwise shows through it.
 */

/* Clear paint, and register the node for this frame's draw pass.
 *
 * A node with no window is not registered. It has nothing to push into newscr,
 * and leaving it out also covers the lifetime case: every node is deflated
 * before it is freed, so a node on its way out never lands in a list that
 * outlives it. */
static void
sPaintDirty(IrcitUiNode *node_ptr)
{
  IrcitUiDirtyList *list = IrcitUiFrameDirtyList();
  size_t            i;

  node_ptr->common.flags &= ~IRCITUI_F_DRAWN;
  if (!node_ptr->common.win) return;

  for (i = 0; i < list->count; i++) if (list->entries[i] == node_ptr) return;

  if (list->count >= IRCITUI_DIRTY_LIST_CAPACITY) {
    // Full. Degrade the frame to a full repaint rather than drop a node that needed revisiting: correctness is not traded for the cap.
    list->overflow = 1;
    return;
  }

  list->entries[list->count++] = node_ptr;
}

/* Raise the layout request to the root. Iterative rather than recursive: a flag
   is not worth consuming stack for the tree's depth. */
static void
sLayoutDirtyUp(IrcitUiNode *node_ptr)
{
  IrcitUiNode *p;
  for (p = node_ptr; p; p = p->common.parent) p->common.is_dirty = 1;
}

/* Drop every entry inside a subtree about to be freed.
 *
 * The list holds borrowed pointers, and this tree frees nodes between an
 * invalidation and the frame that would consume it. Whoever frees a subtree owes
 * it this call. */
static void
sDirtyListForget(const IrcitUiNode *subtree_ptr)
{
  IrcitUiDirtyList *list = IrcitUiFrameDirtyList();
  size_t            i    = 0;

  while (i < list->count) {
    if (sInSubtree(subtree_ptr, list->entries[i])) {
      list->entries[i] = list->entries[--list->count];
      continue;
    }
    i++;
  }
}

/* Non-zero while IrcitUiLayout is running.
 *
 * The layout pass is not a pure reader: the linear packer clamps scroll_y, and a
 * stacked popup is re-clamped through IrcitUiDragTo. Both write fields that the
 * invalidators also write, so a request raised from inside the sweep would
 * re-dirty the root the sweep is in the middle of clearing -- and the relayout
 * would then run every frame, leaving the layout gate gating nothing. Guarding
 * on the depth makes that impossible rather than merely unlikely. */
static int sLayoutDepth;

/* --- The invalidators ------------------------------------------------------ */

PUBLIC_API void
IrcitUiInvalidate(IrcitUiNode *node_ptr)
{
  if (!node_ptr) return;
  // Paint only. The node's box is unchanged, so no ancestor needs re-measuring and nothing bubbles.
  sPaintDirty(node_ptr);
}

PUBLIC_API void
IrcitUiRequestLayout(IrcitUiNode *node_ptr)
{
  if (!node_ptr) return;
  sPaintDirty(node_ptr);
  if (sLayoutDepth > 0) return;
  sLayoutDirtyUp(node_ptr);
}

PUBLIC_API void
IrcitUiNodeSetText(IrcitUiNode *node_ptr, const char *text_ptr)
{
  if (!node_ptr) return;

  node_ptr->common.text = text_ptr;
  IrcitUiRequestLayout(node_ptr);
}

PUBLIC_API void
IrcitUiNodeSetTextLive(IrcitUiNode *node_ptr, const char *text_ptr)
{
  if (!node_ptr) return;

  node_ptr->common.text = text_ptr;
  IrcitUiInvalidate(node_ptr);
}


PUBLIC_API void
IrcitUiBarSetValue(IrcitUiNode *node_ptr, int pct)
{
  IrcitUiBarState *state = sBarState(node_ptr);

  // Not a bar, or a bar whose calloc failed
  if (!state) return;

  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  // Clamped BEFORE the comparison, so asking for 150 while showing 100 is a no-op rather than an invalidate
  if (state->value == pct) return;

  state->value = pct;
  // The box did not change; the cells did.
  IrcitUiInvalidate(node_ptr);
}

PUBLIC_API int
IrcitUiBarValue(const IrcitUiNode *node_ptr)
{
  // sBarState takes a non-const node because its other caller mutates through it
  const IrcitUiBarState *state = sBarState((IrcitUiNode *)node_ptr);
  return state ? state->value : 0;
}

PUBLIC_API void
IrcitUiBarSetBuffered(IrcitUiNode *node_ptr, int pct)
{
  IrcitUiBarState *state = sBarState(node_ptr);

  if (!state) return;

  // The same clamp-then-compare discipline as IrcitUiBarSetValue
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  if (state->buffered == pct) return;

  state->buffered = pct;

   IrcitUiInvalidate(node_ptr);
}

PUBLIC_API int
IrcitUiBarBuffered(const IrcitUiNode *node_ptr)
{
  const IrcitUiBarState *state = sBarState((IrcitUiNode *)node_ptr);
  return state ? state->buffered : 0;
}

/* Exactly one code point, or "no opinion" for NULL and the empty string.
 *
 * mbstowcs does the counting, so the rule is the same one the layout path enforces (C24
 * in tools/layout-grammar.ebnf, "counted in code points, not bytes and not display
 * columns"): a base letter plus a combining mark is two and is refused, a double-width
 * emoji is one and is let through. (size_t)-1 -- an invalid multibyte sequence -- is not
 * one either, so it is refused by the same comparison rather than by a second test.
 *
 * The buffer is four wide chars, which is more than the one being asked for and is what
 * makes "too long" detectable rather than truncated: mbstowcs stops at the limit and
 * reports the limit, so a five-code-point string returns 4 and is refused for the right
 * reason instead of looking like a one-character one. */
static int
sBarGlyphOk(const char *glyph_ptr)
{
  wchar_t w[4];
  size_t  n;

  if (!glyph_ptr || !glyph_ptr[0]) return 1; /* "no opinion", not a malformed glyph */
  n = mbstowcs(w, glyph_ptr, 4);
  return n == 1;
}

PUBLIC_API int
IrcitUiBarSetGlyphs(IrcitUiNode *node_ptr, const char *fill_ptr, const char *empty_ptr)
{
  IrcitUiBarState *state = sBarState(node_ptr);

  if (!state) return 0;

  if (!sBarGlyphOk(fill_ptr) || !sBarGlyphOk(empty_ptr)) {
    IRCITUI_LOG_WARN("bar glyph refused: each must be exactly one code point (C24)");
    return 0;
  }

  /* Borrowed pointers, exactly as a template's are, and held in the STATE: the
     node's declaration block is const and shared, and the flat member is gone
     (IRCITUILIB_GENERATOR_REFACTOR_V1.md §7, the bar family). */
  state->fill_glyph  = fill_ptr;
  state->empty_glyph = empty_ptr;
  IrcitUiInvalidate(node_ptr);
  return 1;
}

PUBLIC_API void
IrcitUiBarSetLabelMode(IrcitUiNode *node_ptr, IrcitUiBarLabelMode mode)
{
  IrcitUiBarState *state = sBarState(node_ptr);

  if (!state) return;

  if (mode < 0 || mode >= IRCITUI_BAR_LABEL_COUNT) return;

  if (state->label_mode == (int)mode) return;
  state->label_mode = (int)mode;
  // Invalidate and not RequestLayout: The node's own geometry is untouched, so nothing above it moves.
  IrcitUiInvalidate(node_ptr);
}

PUBLIC_API void
IrcitUiBarSetGradient(IrcitUiNode *node_ptr, int r1, int g1, int b1, int r2, int g2, int b2)
{
  IrcitUiBarState *state = sBarState(node_ptr);

  if (!state) return;

  state->from_rgb[0] = (short)r1; state->from_rgb[1] = (short)g1; state->from_rgb[2] = (short)b1;
  state->to_rgb[0]   = (short)r2; state->to_rgb[1]   = (short)g2; state->to_rgb[2]   = (short)b2;
  state->gradient    = 1;
  // The registers are re-written on the next paint.
  state->ramp_written = 0;
  IrcitUiInvalidate(node_ptr);
}

PUBLIC_API void
IrcitUiBarClearGradient(IrcitUiNode *node_ptr)
{
  IrcitUiBarState *state = sBarState(node_ptr);

  if (!state || !state->gradient) return;

  // Hand the registers back now rather than at free time, because the point of clearing is to stop holding one of the four.
  if (state->ramp_base >= 0) {
    IrcitUiPaletteRampRelease(state->ramp_base, state->ramp_steps, sBarRampOwner);
    state->ramp_base = -1;
  }
  state->gradient     = 0;
  state->ramp_written = 0;
  IrcitUiInvalidate(node_ptr);
}

PUBLIC_API int
IrcitUiBarGradientLive(const IrcitUiNode *node_ptr)
{
  const IrcitUiBarState *state = sBarState((IrcitUiNode *)node_ptr);
  return state && state->gradient && state->ramp_base >= 0;
}

PUBLIC_API int
IrcitUiBarGradientRequested(const IrcitUiNode *node_ptr)
{
  const IrcitUiBarState *state = sBarState((IrcitUiNode *)node_ptr);
  return state && state->gradient;
}

PUBLIC_API int
IrcitUiBindSeek(const char *node_id_ptr, IrcitUiSeekCallback callback_ptr, void *user_ptr)
{
  IrcitUiNode *    node_ptr;
  IrcitUiBarState *state;

  if (!node_id_ptr || !node_id_ptr[0]) return 0;
  if (!IrcitUiScreen.root) return 0;

  node_ptr = IrcitUiFind(IrcitUiScreen.root, node_id_ptr);
  state    = sBarState(node_ptr);
  if (!state) return 0;

  state->on_seek   = callback_ptr;
  state->seek_user = user_ptr;
  return 1;
}

PUBLIC_API int
IrcitUiEventKeyCode(const IrcitUiEvent *ev_ptr)
{
  return ev_ptr ? ev_ptr->key_code : 0;
}

PUBLIC_API int
IrcitUiEventScrollDelta(const IrcitUiEvent *ev_ptr)
{
  return ev_ptr ? ev_ptr->scroll_delta : 0;
}

/* The model-seam binders, and why they write the MODEL and not the screen.
 *
 * IrcitUiClone copies a node wholesale (*n = *ref_ptr) and then clears only the
 * runtime fields - the window, the parent, the widget_state, the child table. It
 * does NOT clear interactions_user, so a listener written onto a model node is
 * inherited by every clone of it, at the instant of the clone, for the life of the
 * process. That is the whole mechanism, and it buys two things at once: a binding
 * made once at bootstrap survives IrcitUiScreenReset, and a binding can name a
 * template that has no live node at all until something inflates it.
 *
 * IrcitUiBindSeek below cannot work this way and does not try: the bar's callback
 * lives in widget_state, which IrcitUiClone nulls and sAllocWidgetState rebuilds,
 * so there is no model slot for the clone to inherit from.
 *
 * The id resolves through IrcitUiFindModel rather than IrcitUiFindScreen: the
 * latter returns NULL until the framework has a screen tree, which is exactly the
 * bootstrap window these binders exist to be usable in. The model-root guard is
 * IrcitUiBindClick's, and for its reason -- nothing to resolve against yet is
 * refused rather than deferred, because a registration that takes effect later
 * fires at a moment the application did not choose and cannot see. */
static IrcitUiNode *
sBindableModelNode(const char *node_id_ptr)
{
  const IrcitUiDataBindDescriptor *model_ptr;

  if (!node_id_ptr || !node_id_ptr[0]) return NULL;
  model_ptr = IrcitUiModel();
  if (!model_ptr || !model_ptr->root) return NULL;

  return IrcitUiFindModel(node_id_ptr);
}

PUBLIC_API int
IrcitUiBindKey(const char *node_id_ptr, IrcitUiKeyDownCallback callback_ptr, void *user_ptr)
{
  IrcitUiNode *node_ptr = sBindableModelNode(node_id_ptr);

  if (!node_ptr) return 0;

  node_ptr->interactions_user.lifecycle_listeners.on_key_down = callback_ptr;

  node_ptr->interactions_user.user = user_ptr;
  return 1;
}

PUBLIC_API int
IrcitUiBindTextChanged(const char *node_id_ptr, IrcitUiTextChangedCallback callback_ptr, void *user_ptr)
{
  IrcitUiNode *node_ptr = sBindableModelNode(node_id_ptr);

  if (!node_ptr || !callback_ptr) return 0;
  if (node_ptr->common.kind != IRCITUI_EDIT_TEXT) return 0;

  /* The collection the fast lane's notification stage reads -- NOT
     lifecycle_listeners, where on_key_down above lives. The two are separate
     structs on purpose: one is the veto pass, this is the sync pass. */
  node_ptr->interactions_user.post_interceptor_listeners.on_text_changed = callback_ptr;

  node_ptr->interactions_user.user = user_ptr;
  return 1;
}

PUBLIC_API int
IrcitUiBindInflate(const char *node_id_ptr, IrcitUiLifecycleCallback callback_ptr, void *user_ptr)
{
  IrcitUiNode *node_ptr = sBindableModelNode(node_id_ptr);

  if (!node_ptr) return 0;

  node_ptr->interactions_user.lifecycle_listeners.on_inflate = callback_ptr;

  node_ptr->interactions_user.user = user_ptr;
  return 1;
}

PUBLIC_API int
IrcitUiBindDeflate(const char *node_id_ptr, IrcitUiLifecycleCallback callback_ptr, void *user_ptr)
{
  IrcitUiNode *node_ptr = sBindableModelNode(node_id_ptr);

  if (!node_ptr) return 0;

  node_ptr->interactions_user.lifecycle_listeners.on_deflate = callback_ptr;

  node_ptr->interactions_user.user = user_ptr;
  return 1;
}

PUBLIC_API int
IrcitUiBindScroll(const char *node_id_ptr, IrcitUiLifecycleCallback callback_ptr, void *user_ptr)
{
  IrcitUiNode *node_ptr = sBindableModelNode(node_id_ptr);

  if (!node_ptr) return 0;

  node_ptr->interactions_user.lifecycle_listeners.on_scroll = callback_ptr;

  node_ptr->interactions_user.user = user_ptr;
  return 1;
}

PUBLIC_API int
IrcitUiBindResize(const char *node_id_ptr, IrcitUiLifecycleCallback callback_ptr, void *user_ptr)
{
  IrcitUiNode *node_ptr = sBindableModelNode(node_id_ptr);

  /* The root and nothing else. The event is posted on the root alone -- the
     framework relayouts centrally and once -- so a binding on an inner node would
     return 1 and never fire. Refused rather than accepted-and-silent.
     Compared by NODE IDENTITY, not against a literal id: the root's id belongs to
     the layout, and a literal here would refuse every id the day it is renamed. */
  if (!node_ptr || node_ptr != IrcitUiModel()->root) return 0;

  node_ptr->interactions_user.lifecycle_listeners.on_resize = callback_ptr;

  node_ptr->interactions_user.user = user_ptr;
  return 1;
}

PUBLIC_API void
IrcitUiSetFocus(IrcitUiNode *node_ptr)
{
  if (IrcitUiFocus) IrcitUiInvalidate(IrcitUiFocus);
  if (IrcitUiFocus) IrcitUiFocus->common.flags &= ~IRCITUI_F_FOCUSED;
  IrcitUiFocus = node_ptr;
  if (node_ptr) {
    node_ptr->common.flags |= IRCITUI_F_FOCUSED;
    IrcitUiInvalidate(node_ptr);
  }
  // Show the caret lit the moment focus arrives
  IrcitUiBlinkReset();
}

PUBLIC_API IrcitUiNode *
IrcitUiFocusedNode(void)
{
  // The global is the authority; there is nothing to derive. The dispatcher carries a focused_node mirror of it, See IrcitUiDispatcherSetFocus.
  return IrcitUiFocus;
}

/* Deliberately tests flags, never n->common.win: this is consulted from on_inflate,
   which fires after IrcitUiLayout but before IrcitUiRealize, so a node that is about to
   be focusable has no window yet. */
PUBLIC_API int
IrcitUiIsFocusable(const IrcitUiNode *node_ptr)
{
  if (!node_ptr) return 0;
  if ((node_ptr->common.flags & (IRCITUI_F_VISIBLE | IRCITUI_F_ENABLED | IRCITUI_F_CLIPPED)) != (IRCITUI_F_VISIBLE |
    IRCITUI_F_ENABLED)) return 0;
  /* There is no test here for whether the node carries an input handler, and
     there cannot be one: every node is bound to the framework's shared router,
     so that test would answer yes for a header and a footer as readily as for a
     button. The kind switch below is the whole of the answer, and it already
     names every kind the generator ever bound a handler to - which is why
     removing the test changes nothing about which nodes take focus. */
  /* A container that declares its children selectable is itself focusable: the
     keyboard contract belongs to the container, because the container is what
     holds the cursor. The switch below is deliberately left alone - adding
     IRCITUI_LINEAR to it would make every header, body and footer focusable and
     would break IrcitUiFocusTop's preference for the command line. */
  if ((node_ptr->common.flags & IRCITUI_F_CHILDREN_SELECTABLE) && IrcitUiIsContainer(node_ptr->common.kind)) return 1;
  switch (node_ptr->common.kind) {
  case IRCITUI_BUTTON:
  case IRCITUI_EDIT_TEXT:
  case IRCITUI_EMOJI_TABLE:
  case IRCITUI_RADIO_GROUP:
  case IRCITUI_CHECK_GROUP:
  case IRCITUI_CHECK_BOX:
  case IRCITUI_RADIO_BUTTON:
  case IRCITUI_SWITCH:
  /* The one container in this list, and it is here rather than with the
     CHILDREN_SELECTABLE test above because its cursor is not over
     container.children: that array is the viewport, whose entries are rebound as
     it scrolls. A list is a place the keyboard goes, and this is what lets it
     be one. */
  case IRCITUI_RECYCLER:
    return 1;
  default:
    return 0;
  }
}

/* Pre-order search. edit_only picks the pass that prefers an editor, so the
   input field wins over a header button when both are in the same layer. */
static IrcitUiNode *
sFindFocusable(IrcitUiNode *node_ptr, int edit_only)
{
  IrcitUiNode *r;
  int          i;
  if (IrcitUiIsFocusable(node_ptr) && (!edit_only || node_ptr->common.kind == IRCITUI_EDIT_TEXT)) return node_ptr;
  for (i = 0; i < node_ptr->container.n_children; i++) {
    r = sFindFocusable(node_ptr->container.children[i], edit_only);
    if (r) return r;
  }
  return NULL;
}

PUBLIC_API IrcitUiNode *
IrcitUiTopLayer(void)
{
  IrcitUiNode *root = IrcitUiScreen.root, *top = NULL;
  int          i;
  if (!root) return NULL;
  for (i = 0; i < root->container.n_children; i++) if (root->container.children[i]->common.flags & IRCITUI_F_STACKED)
    top = root->container.children[i];
  return top;
}

PUBLIC_API void
IrcitUiFocusInLayer(IrcitUiNode *layer_ptr)
{
  IrcitUiNode *n;

  if (!layer_ptr) return;

  if (IrcitUiFocus && sInSubtree(layer_ptr, IrcitUiFocus) && IrcitUiIsFocusable(IrcitUiFocus)) return;
  n = sFindFocusable(layer_ptr, 1);
  if (!n) n = sFindFocusable(layer_ptr, 0);
  if (n) IrcitUiSetFocus(n);
  // Nothing focusable in the layer: leave focus alone rather than clearing it, since NULLing it would make the next keystroke vanish instead of going somewhere predictable.
}

PUBLIC_API void
IrcitUiFocusTop(void)
{
  IrcitUiNode *layer = IrcitUiTopLayer();
  IrcitUiFocusInLayer(layer ? layer : IrcitUiScreen.root);
}

PUBLIC_API int
IrcitUiPopupFocusOnOpen(IrcitUiEventDispatcher *disp_ptr, IrcitUiEvent *ev_ptr, void *user_ptr)
{
  (void)disp_ptr;
  (void)user_ptr;
  if (!ev_ptr || !ev_ptr->target) return 0;
  IrcitUiFocusInLayer(ev_ptr->target);
  return 1;
}


static int
sLineCount(const char *str_ptr)
{
  int n = 1;
  if (!str_ptr || !*str_ptr) return 0;
  while (*str_ptr) {
    if (*str_ptr == '\n') n++;
    str_ptr++;
  }
  return n;
}

/* byte index in s at display column `col` on line `row` (0-based) */
static int
sTextIndexAt(const char *str_ptr, int col, int row)
{
  int         r = 0, c = 0;
  const char *p = str_ptr ? str_ptr : "";
  if (row < 0) row = 0;
  if (col < 0) col = 0;
  while (*p) {
    if (r == row && c >= col) return (int)(p - str_ptr);
    if (*p == '\n') {
      if (r == row) return (int)(p - str_ptr);
      r++;
      c = 0;
      p++;
      continue;
    }
    c++;
    p++;
  }
  return (int)(p - str_ptr);
}

/* The caret is a byte index that must always land on a character boundary, so
   every move is a walk between boundary to boundary. Both walks treat a stray
   continuation byte as a boundary of its own: that way a buffer that somehow
   acquired one still advances monotonically instead of stalling. */

/* Byte index one character left of @p i; 0 at the start. */
size_t
IrcitUiUtf8Prev(const char *str_ptr, size_t i)
{
  if (i == 0) return 0;
  i--;
  while (i > 0 && ((unsigned char)str_ptr[i] & 0xC0) == 0x80) i--;
  return i;
}

/* Byte index one character right of @p i; strlen(s) at the end. */
size_t
IrcitUiUtf8Next(const char *str_ptr, size_t i)
{
  size_t len = strlen(str_ptr);
  if (i >= len) return len;
  i++;
  while (i < len && ((unsigned char)str_ptr[i] & 0xC0) == 0x80) i++;
  return i;
}

/* One character at @p p, as the cell width it charges and its byte length.
   A malformed sequence is charged one cell and one byte so every walk using it
   stays monotonic; wcwidth is never called on a value mbrtowc did not set. */
void
IrcitUiUtf8Step(const char *str_ptr, size_t p, size_t *k_out_ptr, int *ww_out_ptr)
{
  wchar_t   wc = 0;
  mbstate_t st;
  size_t    k;
  memset(&st, 0, sizeof st);
  k = mbrtowc(&wc, str_ptr + p, PRIV_CONFIG_DEFAULT_UTF8_MAX_BYTES, &st);
  if (k == (size_t)-1 || k == (size_t)-2 || k == 0) {
    *k_out_ptr  = 1;
    *ww_out_ptr = 1;
    return;
  }
  *k_out_ptr  = k;
  *ww_out_ptr = wcwidth(wc);
}

/* Display cells from the start of @p s to byte index @p i. An index that falls
   inside a character stops at that character's start, so the answer is always a
   boundary column. */
int
IrcitUiColForIndex(const char *str_ptr, size_t i)
{
  size_t p   = 0;
  int    col = 0;
  if (!str_ptr) return 0;
  while (p < i && str_ptr[p]) {
    size_t k;
    int    ww;
    IrcitUiUtf8Step(str_ptr, p, &k, &ww);
    if (p + k > i) break;
    if (ww > 0) col += ww;
    p += k;
  }
  return col;
}

/* Inverse of IrcitUiColForIndex: the largest byte index whose column is <= @p col.
   Snaps left so a click can never land inside a character, returns 0 at or
   before the start and strlen(s) once @p col is past the end. */
size_t
IrcitUiIndexForCol(const char *str_ptr, int col)
{
  size_t p = 0;
  int    c = 0;
  if (!str_ptr) return 0;
  if (col <= 0) return 0;
  while (str_ptr[p]) {
    size_t k;
    int    ww;
    IrcitUiUtf8Step(str_ptr, p, &k, &ww);
    if (ww <= 0) { /* zero-width charges no cell */
      p += k;
      continue;
    }
    if (c + ww > col) return p;
    c += ww;
    p += k;
  }
  return p;
}

static void
sPaintSelText(WINDOW *win_ptr, int ox, int oy, int maxw, int maxh, const char *str_ptr, int sel0, int sel1, int scrolly,
             int canopy_y)
{
  int row = 0,    col = 0, idx = 0;
  int a   = sel0, b   = sel1;
  if (!str_ptr) return;
  if (a > b) {
    int t = a;
    a     = b;
    b     = t;
  }
  while (str_ptr[idx] && row - scrolly < maxh) {
    unsigned char ch = (unsigned char)str_ptr[idx];
    if (ch == '\n') {
      row++;
      col = 0;
      idx++;
      continue;
    }
    if (row >= scrolly && col < maxw) {
      if (idx >= a && idx < b)
        wattron(win_ptr, A_REVERSE);
      else
        wattroff(win_ptr, A_REVERSE);
      /* The canopy's style is OR'd into the cell rather than set on the window, because
         the window has one attribute and the backdrop has one per row. A_REVERSE for a
         selection still applies: it is an attribute, and the style carries a pair. */
      /* canopy_y is the node's row on the SCREEN, not in its window, and the difference
         is the whole of this expression: the backdrop was filled with the canopy's row
         for the node's screen position, so text blended against a window-local row would
         carry a colour belonging to a different band. Measured: background register 111
         (screen row 5) against foreground 136 (window row 0) before this was fixed. */
      mvwaddch(win_ptr, oy + row - scrolly, ox + col,
               (chtype)ch | (canopy_y >= 0 ? IrcitUiPaletteCanopyTextStyle(canopy_y + row) : 0));
      wattroff(win_ptr, A_REVERSE);
      col++;
    }
    idx++;
  }
  wattroff(win_ptr, A_REVERSE);
}

PUBLIC_API int
IrcitUiIsContainer(IrcitUiKind k)
{
  return k == IRCITUI_LINEAR || k == IRCITUI_FRAME || k == IRCITUI_CONSTRAINT || k == IRCITUI_RADIO_GROUP || k ==
    IRCITUI_CHECK_GROUP || k == IRCITUI_RECYCLER || k == IRCITUI_CONVERSATION_WINDOW;
}

static int
sDispCols(const char *str_ptr)
{
  wchar_t w[PRIV_CONFIG_DEFAULT_WCS_BUF_SZ];
  size_t  n,        i;
  int     cols = 0, wc;
  if (!str_ptr || !*str_ptr) return 0;
  n = mbstowcs(w, str_ptr, PRIV_CONFIG_DEFAULT_WCS_BUF_SZ - 1);
  if (n == (size_t)-1) return (int)strlen(str_ptr);
  w[n] = 0;
  for (i = 0; i < n; i++) {
    wc   = wcwidth(w[i]);
    cols += wc > 0 ? wc : 1;
  }
  return cols;
}

void
IrcitUiAddUtf8(WINDOW *win_ptr, int y, int x, const char *str_ptr, int maxc)
{
  wchar_t w[PRIV_CONFIG_DEFAULT_WCS_BUF_SZ];
  size_t  n;
  if (!str_ptr) return;
  n = mbstowcs(w, str_ptr, PRIV_CONFIG_DEFAULT_WCS_BUF_SZ - 1);
  if (n == (size_t)-1) {
    mvwaddnstr(win_ptr, y, x, str_ptr, maxc);
    return;
  }
  w[n] = 0;
  if (maxc >= 0 && (int)n > maxc) w[maxc] = 0;
  mvwaddwstr(win_ptr, y, x, w);
}


static int
sWrapW(const IrcitUiNode *node_ptr)
{
  int t;
  // Box chrome is content-independent, so every kind adds it on every path.
  int extra = (node_ptr->common.flags & IRCITUI_F_BORDER) ? 2 : 0;
  extra     += node_ptr->common.padding[1] + node_ptr->common.padding[3];
  if (node_ptr->common.kind == IRCITUI_CHECK_BOX || node_ptr->common.kind == IRCITUI_RADIO_BUTTON || node_ptr->common.
    kind == IRCITUI_SWITCH)
    return PRIV_CONFIG_DEFAULT_GLYPH_W + sDispCols(node_ptr->common.text) + extra;
  if (node_ptr->common.kind == IRCITUI_IMAGE_VIEW) return IrcitUiImageWrapW((IrcitUiNode*)node_ptr) + extra;
  if (node_ptr->common.kind == IRCITUI_PROGRESS_BAR || node_ptr->common.kind == IRCITUI_SEEK_BAR) return
    PRIV_CONFIG_DEFAULT_BAR_MIN_W + extra;
  if (node_ptr->common.kind == IRCITUI_EMOJI_TABLE) {
    int cols = node_ptr->container.columns > 0 ? node_ptr->container.columns : PRIV_CONFIG_DEFAULT_GRID_COLS;
    return cols * 2 + extra;
  }
  t = sDispCols(node_ptr->common.text);
  if (node_ptr->common.kind == IRCITUI_BUTTON) return t + PRIV_CONFIG_DEFAULT_BUTTON_PAD + extra;
  if (node_ptr->common.kind == IRCITUI_EDIT_TEXT) return (t + 2 < PRIV_CONFIG_DEFAULT_EDIT_MIN_W ?
                                                            PRIV_CONFIG_DEFAULT_EDIT_MIN_W :
                                                            t + 2) + extra;
  // A conversation window is a container, so without this branch it would measure
  // its one MATCH-sized viewport child and answer about a cell. It asks instead.
  if (node_ptr->common.kind == IRCITUI_CONVERSATION_WINDOW) {
    int mw = 0, mh = 0;
    IrcitUiConversationMinSize(node_ptr, &mw, &mh);
    return mw + extra;
  }
  if (IrcitUiIsContainer(node_ptr->common.kind)) {
    int i, acc = 0;
    if (sOrientationOf(node_ptr) == IRCITUI_HORIZ) {
      for (i = 0; i < node_ptr->container.n_children; i++) acc += sWrapW(node_ptr->container.children[i]);
    }
    else {
      for (i = 0; i < node_ptr->container.n_children; i++) {
        int cw = sWrapW(node_ptr->container.children[i]);
        if (cw > acc) acc = cw;
      }
    }
    if (node_ptr->container.n_children == 0) acc = 1;
    acc += extra;
    return acc > 0 ? acc : 1;
  }
  return (t < 1 ? 1 : t) + extra;
}

static int
sWrapH(const IrcitUiNode *node_ptr)
{
  int extra = (node_ptr->common.flags & IRCITUI_F_BORDER) ? 2 : 0;
  extra     += node_ptr->common.padding[0] + node_ptr->common.padding[2];
  if (node_ptr->common.kind == IRCITUI_IMAGE_VIEW) return IrcitUiImageWrapH((IrcitUiNode*)node_ptr) + extra;
  if (node_ptr->common.kind == IRCITUI_EMOJI_TABLE) {
    int         cols = node_ptr->container.columns > 0 ? node_ptr->container.columns : PRIV_CONFIG_DEFAULT_GRID_COLS;
    int         nch  = 0;
    const char *s    = node_ptr->common.text;
    wchar_t     wc;
    mbstate_t   st;
    memset(&st, 0, sizeof st);
    while (s && *s) {
      size_t k = mbrtowc(&wc, s, PRIV_CONFIG_DEFAULT_UTF8_MAX_BYTES, &st);
      if (k == (size_t)-1 || k == 0) break;
      if (wcwidth(wc) > 0) nch++;
      s += k;
    }
    return extra + (nch + cols - 1) / cols;
  }
  // As sWrapW: a conversation window would otherwise sum a MATCH-sized child and
  // report one row. The widget's own minimum is the only answer that means anything.
  if (node_ptr->common.kind == IRCITUI_CONVERSATION_WINDOW) {
    int mw = 0, mh = 0;
    IrcitUiConversationMinSize(node_ptr, &mw, &mh);
    return mh + extra;
  }
  if (IrcitUiIsContainer(node_ptr->common.kind) && node_ptr->container.n_children) {
    int i, acc = 0;
    if (sOrientationOf(node_ptr) == IRCITUI_HORIZ) {
      for (i = 0; i < node_ptr->container.n_children; i++) {
        int ch = sWrapH(node_ptr->container.children[i]);
        if (ch > acc) acc = ch;
      }
      return acc + extra;
    }
    for (i = 0; i < node_ptr->container.n_children; i++) acc += sWrapH(node_ptr->container.children[i]);
    return acc + extra;
  }
  if (IrcitUiIsContainer(node_ptr->common.kind)) return 1 + extra;
  return 1 + extra;
}

/* ---- Collapsible containers -------------------------------------------------
 *
 * Two axes are in play and they are not the same one, which is the whole of the
 * subtlety here:
 *
 *   the indicator cell is reserved on the container's MAIN axis, so a horizontal
 *   container loses its first column and a vertical one its first row;
 *
 *   the collapse acts on the CROSS axis, so a horizontal container folds to one
 *   row and a vertical one to one column.
 *
 * The reservation is driven from sContentBox, which is what makes constraint
 * children obey it as well: sEdgeX/sEdgeY resolve a "parent" edge through that
 * same box, so a child pinned to the parent's start lands after the indicator
 * rather than underneath it. */

/* Whether a node is folded. Both halves matter - is_collapsed is meaningful only
   where the framework was told the node may fold, and the generator zeroes it
   everywhere else. */
static int
sIsCollapsed(const IrcitUiNode *node_ptr)
{
  return (node_ptr->common.flags & IRCITUI_F_COLLAPSIBLE) && node_ptr->container.is_collapsed;
}

/* Whether this container folds along its width, which is the vertical case. */
static int
sCollapsesWidth(const IrcitUiNode *node_ptr)
{
  return sOrientationOf(node_ptr) != IRCITUI_HORIZ;
}

static void
sContentBox(const IrcitUiNode *node_ptr, int *x_ptr, int *y_ptr, int *w_ptr, int *h_ptr)
{
  int b  = (node_ptr->common.flags & IRCITUI_F_BORDER) ? 1 : 0;
  *x_ptr = node_ptr->common.x + node_ptr->common.padding[3] + b;
  *y_ptr = node_ptr->common.y + node_ptr->common.padding[0] + b;
  *w_ptr = node_ptr->common.w - node_ptr->common.padding[1] - node_ptr->common.padding[3] - 2 * b;
  *h_ptr = node_ptr->common.h - node_ptr->common.padding[0] - node_ptr->common.padding[2] - 2 * b;

  if ((node_ptr->common.flags & IRCITUI_F_COLLAPSIBLE) && !node_ptr->container.is_collapsed && !(node_ptr->common.flags
    & IRCITUI_F_BORDER)) {
    *y_ptr += 1;
    *h_ptr -= 1;
  }
  if (*w_ptr < 1) *w_ptr = 1;
  if (*h_ptr < 1) *h_ptr = 1;
}

static int
sMeasW(const IrcitUiNode *node_ptr, int avail)
{
  int inner = avail - node_ptr->common.margin[1] - node_ptr->common.margin[3];

  if (sIsCollapsed(node_ptr) && sCollapsesWidth(node_ptr)) return 1 + node_ptr->common.margin[1] + node_ptr->common.
    margin[3];
  if (inner < 1) inner = 1;
  switch (node_ptr->common.layout_w.mode) {
  case IRCITUI_SIZE_EXACT: return node_ptr->common.layout_w.cells + node_ptr->common.margin[1] + node_ptr->common.margin
      [3];
  case IRCITUI_SIZE_MATCH: return avail;

  case IRCITUI_SIZE_PERCENT: return avail * node_ptr->common.layout_w.percent / 100;
  default: return sWrapW(node_ptr) + node_ptr->common.margin[1] + node_ptr->common.margin[3];
  }
}

static int
sMeasH(const IrcitUiNode *node_ptr, int avail)
{
  int inner = avail - node_ptr->common.margin[0] - node_ptr->common.margin[2];
  // The vertical twin of the fold in sMeasW above.
  if (sIsCollapsed(node_ptr) && !sCollapsesWidth(node_ptr)) return 1 + node_ptr->common.margin[0] + node_ptr->common.
    margin[2];
  if (inner < 1) inner = 1;
  switch (node_ptr->common.layout_h.mode) {
  case IRCITUI_SIZE_EXACT: return node_ptr->common.layout_h.cells + node_ptr->common.margin[0] + node_ptr->common.margin
      [2];
  case IRCITUI_SIZE_MATCH: return avail;
  // The vertical twin of the PERCENT case in sMeasW above, including why it is written out rather than left to the default.
  case IRCITUI_SIZE_PERCENT: return avail * node_ptr->common.layout_h.percent / 100;
  default: return sWrapH(node_ptr) + node_ptr->common.margin[0] + node_ptr->common.margin[2];
  }
}

static void
sPlace(IrcitUiNode *node_ptr, int x, int y, int w, int h)
{
  node_ptr->common.x = x + node_ptr->common.margin[3];
  node_ptr->common.y = y + node_ptr->common.margin[0];
  node_ptr->common.w = w - node_ptr->common.margin[1] - node_ptr->common.margin[3];
  node_ptr->common.h = h - node_ptr->common.margin[0] - node_ptr->common.margin[2];
  if (node_ptr->common.w < 1) {
    node_ptr->common.w     = 1;
    node_ptr->common.flags |= IRCITUI_F_CLIPPED;
  }
  if (node_ptr->common.h < 1) {
    node_ptr->common.h     = 1;
    node_ptr->common.flags |= IRCITUI_F_CLIPPED;
  }
}

/* Stacked popups are positioned absolutely and moved by IrcitUiDragTo, so the pack
   passes must leave them alone. Re-placing them teleported every popup to its
   parent's content-box origin on each IrcitUiLayout(root, ...), which KEY_RESIZE,
   the wheel and thumb-drag all call. */
static int
sIsStacked(const IrcitUiNode *node_ptr)
{
  return (node_ptr->common.flags & IRCITUI_F_STACKED) != 0;
}

static void
sLayoutLinear(IrcitUiNode *node_ptr)
{
  int i, sPx, py, pw, ph, used, avail, mw, mh;
  sContentBox(node_ptr, &sPx, &py, &pw, &ph);

  if (sOrientationOf(node_ptr) == IRCITUI_HORIZ) {
    used = 0;
    for (i = 0; i < node_ptr->container.n_children; i++)
      if (!sIsStacked(node_ptr->container.children[i]) && node_ptr->container.children[i]->common.layout_w.mode !=
        IRCITUI_SIZE_MATCH)
        used += sMeasW(node_ptr->container.children[i], pw);
    avail = pw - used;
    if (avail < 0) avail = 0;
    for (i = 0; i < node_ptr->container.n_children; i++) {
      IrcitUiNode *c = node_ptr->container.children[i];
      if (sIsStacked(c)) continue;
      mw = (c->common.layout_w.mode == IRCITUI_SIZE_MATCH) ? avail : sMeasW(c, pw);
      mh = sMeasH(c, ph);
      if (mh > ph) mh = ph;
      sPlace(c, sPx, py, mw, mh);
      IrcitUiLayout(c, c->common.x, c->common.y, c->common.w, c->common.h);
      // Advance by what the child actually occupies, not by the width it asked for.
      sPx = c->common.x + c->common.w + c->common.margin[1];
    }
  }
  else {
    used = 0;
    for (i = 0; i < node_ptr->container.n_children; i++)
      if (!sIsStacked(node_ptr->container.children[i]) && node_ptr->container.children[i]->common.layout_h.mode !=
        IRCITUI_SIZE_MATCH)
        used += sMeasH(node_ptr->container.children[i], ph);
    avail = ph - used;
    if (avail < 0) avail = 0;
    for (i = 0; i < node_ptr->container.n_children; i++) {
      IrcitUiNode *c = node_ptr->container.children[i];
      if (sIsStacked(c)) continue;
      mh = (c->common.layout_h.mode == IRCITUI_SIZE_MATCH) ? avail : sMeasH(c, ph);
      mw = sMeasW(c, pw);
      if (mw > pw) mw = pw;
      sPlace(c, sPx, py, mw, mh);
      IrcitUiLayout(c, c->common.x, c->common.y, c->common.w, c->common.h);
      // See the horizontal case: advance by the realised extent.
      py = c->common.y + c->common.h + c->common.margin[0];
    }
  }
  if (node_ptr->common.flags & IRCITUI_F_SCROLLABLE) {
    int i, maxy = 0, ph;
    ph          = node_ptr->common.h - ((node_ptr->common.flags & IRCITUI_F_BORDER) ? 2 : 0);
    for (i = 0; i < node_ptr->container.n_children; i++) {
      int b = node_ptr->container.children[i]->common.y + node_ptr->container.children[i]->common.h - (node_ptr->common.
        y);
      if (b > maxy) maxy = b;
    }
    node_ptr->container.columns = maxy > ph ? maxy - ph : 0;
    if (node_ptr->container.scroll_y > node_ptr->container.columns) node_ptr->container.scroll_y = node_ptr->container.
      columns;
    if (node_ptr->container.scroll_y) {
      for (i = 0; i < node_ptr->container.n_children; i++) sOffsetTree(node_ptr->container.children[i], 0,
                                                                       -node_ptr->container.scroll_y);
    }
  }
}

/* sLookupRef for an id that may not be there.
 *
 * sLookupRef dereferences its argument unconditionally -- it hashes it -- so a
 * NULL is not "not found", it is a crash inside sIdHash. Every id this file
 * resolves for a recycler comes from a declaration or from a caller, and both can
 * be absent: a recycler that named view_types has no item_template at all, and
 * the public setter below takes whatever registry it is handed.
 *
 * The guard is here rather than inside sLookupRef because sLookupRef is shared
 * with every other lookup in this file and its callers all pass a real id; that
 * one would crash on a NULL is a latent hazard worth reporting, not a licence to
 * change a function five other call sites depend on. This is the boundary the
 * new input crosses.
 *
 * An empty string is treated as absent too: it is how "named none" is spelled by
 * a generated node, and hashing it would find the node whose id is empty rather
 * than answering that there is none. */
static const IrcitUiNode *
sRefOrNull(const char *id_ptr)
{
  if (!id_ptr || !id_ptr[0]) return NULL;
  return sLookupRef(id_ptr);
}

/* The template pool slot @p slot must hold a clone of, or NULL if no row can be
 * built there.
 *
 * The slot's dataset position is `scroll_offset_y + slot` -- the same arithmetic
 * sRowBoundIndex derives, and the reason the layout pass clamps the offset
 * BEFORE it sizes the pool: this reads it. NULL means one of two things, and the
 * layout pass treats them alike because they have the same answer:
 *
 *   - the position is past the end of the dataset, so there is no entry for the
 *     slot to show. This is what makes the last screenful shorter than the pool.
 *   - the application's type for the position has no entry in the registry, so
 *     there is no template to clone. Refusing the slot is deliberately not the
 *     same as falling back to some other type's template: a row of the wrong
 *     shape is a wrong picture, and a missing row is not.
 *
 * HOMOGENEOUS -- no registry -- answers with the one template the state names,
 * for every slot, which is what this widget did before view types existed. The
 * three cases are ordered so that the ordinary one is a single pointer read.
 *
 * With a registry but NO discriminator installed, the first declared mapping
 * answers for every slot. That is a deliberate default rather than a refusal: a
 * declaration that has been generated but not yet bound to an application
 * renders its first row type, which is diagnosable, where an empty list is not.
 *
 * SS26's EDITING ROW IS AN INTERCEPT, before all three of those. A row being
 * edited takes the registry entry editing_type_id names rather than the one the
 * application's discriminator returns for it, and the intercept is here rather
 * than in the discriminator because the discriminator answers a question about
 * the DATASET -- "what shape is this entry" -- while "this one is open for
 * editing" is a fact about the widget. An application asked to answer both would
 * have to be told about a transient state it does not own, and its answer would
 * have to change under it as the user starts and stops typing.
 *
 * A miss falls through to the row's ordinary shape rather than answering NULL.
 * NULL is what hides a row, and hiding the row a user is typing into is the worst
 * of the available failures; IrcitUiRecyclerBeginEdit refuses an editing type
 * that does not resolve, so this is the second gate and not the first. */
static const IrcitUiNode *
sSlotTemplate(const IrcitUiRecyclerState *state_ptr, size_t slot)
{
  size_t position = state_ptr->scroll_offset_y + slot;
  int    type, i;

  if (position >= state_ptr->adapter.item_count) return NULL;

  if (position == state_ptr->editing_index) {
    for (i = 0; i < state_ptr->n_view_types; i++) {
      const IrcitUiNode *ref;
      if (state_ptr->view_types[i].type_id != state_ptr->editing_type_id) continue;
      ref = sRefOrNull(state_ptr->view_types[i].template_id);
      if (ref) return ref;
      break;
    }
  }

  if (state_ptr->n_view_types <= 0) return sRefOrNull(state_ptr->template_id);
  if (!state_ptr->adapter.get_item_view_type) return sRefOrNull(state_ptr->view_types[0].template_id);

  type = state_ptr->adapter.get_item_view_type(position, state_ptr->adapter.user_dataset);
  for (i = 0; i < state_ptr->n_view_types; i++) {
    if (state_ptr->view_types[i].type_id == type) return sRefOrNull(state_ptr->view_types[i].template_id);
  }
  return NULL;
}

/* The template a slot is filled with when its own cannot be resolved.
 *
 * Only the POOL SIZING uses this. A slot has to hold a node whatever the
 * application's type says, because n_children is the viewport: the row-index to
 * dataset-index arithmetic that sRowBoundIndex, IrcitUiRecyclerRowAt, the scroll
 * clamp and the cursor-follow rule all depend on is positional, so a hole in the
 * array would shift every slot after it onto the wrong dataset entry. A slot
 * that cannot be bound keeps its node and is hidden by the layout pass instead.
 *
 * The homogeneous template answers when there is no registry, and the first
 * declared mapping when there is -- the same entry sSlotTemplate falls back to,
 * so a list that has never been bound renders one consistent shape. */
static const IrcitUiNode *
sFallbackTemplate(const IrcitUiRecyclerState *state_ptr)
{
  if (state_ptr->n_view_types <= 0) return sRefOrNull(state_ptr->template_id);
  return sRefOrNull(state_ptr->view_types[0].template_id);
}

/* The template the pinned header is a clone of: the registry entry the sticky
 * type names. NULL when the feature is off or the type has no entry -- C22 keeps
 * the second from being expressible in a declaration, but a C-API caller sets the
 * registry itself and can produce it. */
static const IrcitUiNode *
sStickyTemplate(const IrcitUiRecyclerState *state_ptr)
{
  int i;
  if (state_ptr->sticky_type < 0) return NULL;
  for (i = 0; i < state_ptr->n_view_types; i++) {
    if (state_ptr->view_types[i].type_id == state_ptr->sticky_type) {
      return sRefOrNull(state_ptr->view_types[i].template_id);
    }
  }
  return NULL;
}

/* A monotonic reading in nanoseconds, for measuring a DURATION.
 *
 * CLOCK_MONOTONIC and not CLOCK_REALTIME, and that is a correctness point rather
 * than a preference: the interval between two presses is a duration, and
 * CLOCK_REALTIME is settable. An NTP step or a manual clock change landing between
 * the two clicks would make the subtraction meaningless -- reading as a
 * double-click that never expires if the clock went backwards far enough, or as
 * one that never fires if it went forwards.
 *
 * The multiplication is int64_t for the reason IRCITUI_DOUBLE_CLICK_NS gives: on a
 * 32-bit target `ts.tv_sec * 1000000000` in a `long` overflows after two seconds
 * of uptime. The casts are on the operands, not the product, so the arithmetic is
 * 64-bit throughout rather than being done in 32 bits and widened afterwards.
 *
 * A clock that cannot be read answers 0, which makes every delta enormous and so
 * disables the gesture rather than inventing one. */
static int64_t
sMonotonicNs(void)
{
  struct timespec ts;

  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
  return (int64_t)ts.tv_sec * IRCITUI_NS_PER_SEC + (int64_t)ts.tv_nsec;
}

/* How many of the container's children are ROWS.
 *
 * The pinned header is a child too, and it is the last one, so this is
 * n_children or n_children - 1. Every place that bound-checks a row index must
 * use it rather than n_children, because slot n_children - 1 is the header and
 * binding a dataset position to it would show an entry twice -- once in the list
 * and once pinned -- with the pinned copy describing a different row.
 *
 * Keeping the header LAST is what makes this a subtraction rather than an
 * offset: rows stay at indices 0..n-1, so the binding arithmetic is untouched. */
static size_t
sPoolRowCount(const IrcitUiNode *node_ptr, const IrcitUiRecyclerState *state_ptr)
{
  size_t n = (size_t)node_ptr->container.n_children;
  return state_ptr->header_node ? n - 1 : n;
}

/* Build the pinned header if the feature is on and it is not there yet.
 *
 * One node, appended as the LAST child, cloned from the sticky type's template.
 * Built here rather than in sAllocWidgetState because it is a child, and children
 * are the layout pass's business -- the same reason the pool is.
 *
 * It is NOT one of the pool's rows and never becomes one: the pool holds
 * viewport-height rows bound to consecutive dataset positions, and the header is
 * a second view of one of them. That is why sPoolRowCount exists.
 *
 * Returns 0 only if the template cannot be resolved or the clone fails, in which
 * case there is no header and the list simply renders without one. */
/* Release the pinned header and its slot in the child array.
 *
 * Only reached when the feature is turned OFF after having been on -- there is no
 * setter for it in a declaration, but IrcitUiRecyclerSetStickyHeader can turn it
 * off at run time. A header left in the array would be a node nothing ever shows,
 * and worse: sPoolRowCount subtracts it, so the pool would come up one row short
 * for the rest of the tree's life.
 *
 * It is the LAST child by construction -- see the field's own comment -- so this
 * is a pop and not a splice. */
static void
sRecyclerHeaderDrop(IrcitUiNode *node_ptr, IrcitUiRecyclerState *state_ptr)
{
  IrcitUiNode *header = state_ptr->header_node;

  if (!header) return;
  if (node_ptr->container.n_children > 0 &&
      node_ptr->container.children[node_ptr->container.n_children - 1] == header) {
    node_ptr->container.n_children--;
  }
  sFreeTree(header);
  state_ptr->header_node    = NULL;
  state_ptr->pinned_section = IRCITUI_NO_SECTION;
  if (node_ptr->container.n_children == 0) {
    free(node_ptr->container.children);
    node_ptr->container.children = NULL;
  }
}

static int
sRecyclerHeaderEnsure(IrcitUiNode *node_ptr, IrcitUiRecyclerState *state_ptr)
{
  const IrcitUiNode *ref;
  IrcitUiNode       *row;
  IrcitUiNode      **grown;

  if (state_ptr->sticky_type < 0) {
    sRecyclerHeaderDrop(node_ptr, state_ptr);
    return 1;
  }
  if (state_ptr->header_node) return 1;

  ref = sStickyTemplate(state_ptr);
  if (!ref) return 0;
  row = IrcitUiClone(ref);
  if (!row) return 0;
  grown = realloc(node_ptr->container.children, ((size_t)node_ptr->container.n_children + 1) * sizeof *grown);
  if (!grown) {
    sFreeTree(row);
    return 0;
  }
  node_ptr->container.children                                   = grown;
  node_ptr->container.children[node_ptr->container.n_children++] = row;
  row->common.parent                                             = node_ptr;
  state_ptr->header_node                                         = row;
  state_ptr->pinned_section = IRCITUI_NO_SECTION;

  return 1;
}

/* The section the viewport is inside: the dataset position of the last divider at
 * or before scroll_offset_y, or IRCITUI_NO_SECTION when there is none.
 *
 * SS15.3's backwards scan, and it is O(distance to the section start) --- see the
 * note at the call site, which is where the cost is worth stating rather than
 * here. Bounded by position 0, so it terminates on a list whose sections have not
 * started, which is the case every list begins in.
 *
 * The predicate is the application's own discriminator, which is the whole reason
 * a sticky header needs §14: the framework has no view of the dataset, so the
 * only thing it can ask is "what type is the row at this position", and the
 * answer arrives from the same callback that chooses the row's shape. */
static size_t
sStickySection(const IrcitUiRecyclerState *state_ptr)
{
  size_t position;

  if (state_ptr->sticky_type < 0 || !state_ptr->adapter.get_item_view_type) return IRCITUI_NO_SECTION;
  if (!state_ptr->adapter.item_count) return IRCITUI_NO_SECTION;

  for (position = state_ptr->scroll_offset_y; position != IRCITUI_NO_SECTION; position--) {
    if (position >= state_ptr->adapter.item_count) continue;
    if (state_ptr->adapter.get_item_view_type(position, state_ptr->adapter.user_dataset) ==
        state_ptr->sticky_type) {
      return position;
    }
    if (position == 0) break;
  }
  return IRCITUI_NO_SECTION;
}

/* Give pool slot @p slot a row cloned from @p ref, freeing whatever held it.
 *
 * The replacement half of heterogeneity. A slot is rebuilt in place rather than
 * moved to the end of the array: the array is the viewport, so its ORDER is the
 * dataset's order and moving a row would renumber every slot behind it. */
static int
sPoolSlotRebind(IrcitUiNode *node_ptr, size_t slot, const IrcitUiNode *ref)
{
  IrcitUiNode *row = IrcitUiClone(ref);

  if (!row) return 0;
  sFreeTree(node_ptr->container.children[slot]);
  node_ptr->container.children[slot] = row;
  row->common.parent                 = node_ptr;
  return 1;
}

/* Grow or shrink a recycler's pool to exactly @p want rows.
 *
 * Called on every layout pass, and it is a no-op when the count is already
 * right, which is the common case: the pool only moves when the viewport
 * resizes. SS13 left this out entirely -- the pool was allocated once under a
 * `view_pool == NULL` guard and never revisited, so a resize either laid rows
 * out beyond the container or left the pool too small to fill it.
 *
 * The pool IS container.children, which is what makes the rows reach the
 * tree-wide passes: realize builds their windows, draw paints them, deflate and
 * sFreeTree release them, and the clip and hit-test walks see them. The array is
 * grown and trimmed here rather than through sAttach, because sAttach raises a
 * layout request and this runs inside the layout pass -- raising one would
 * re-dirty the root that pass is in the middle of clearing.
 *
 * Sizing only. Which SHAPE each slot holds is not settled here -- the layout
 * pass rebinds any slot whose template does not match the position it is about
 * to show, so that the two questions stay apart: this one is "how many rows",
 * the pass's is "which rows". Growth still asks sSlotTemplate for each new slot
 * and clones that template rather than the fallback, because the alternative is
 * to build the whole new tail twice on the pass that resizes.
 *
 * Returns 0 only if no template can be resolved at all or an allocation fails,
 * in which case the pool is left at whatever size it had reached. */
static int
sRecyclerPoolResize(IrcitUiNode *node_ptr, IrcitUiRecyclerState *state_ptr, size_t want)
{
  IrcitUiNode *header      = state_ptr->header_node;
  int          restitching = 0;
  int          ok          = 1;
  size_t       have;

    if (header) {
    node_ptr->container.n_children--;
    restitching = 1;
  }
  have = (size_t)node_ptr->container.n_children;

  if (want == have) goto restitch;

  if (want < have) {
    size_t slot;
    for (slot = want; slot < have; slot++) sFreeTree(node_ptr->container.children[slot]);
    node_ptr->container.n_children = (int)want;
    if (want == 0) {
      // The array goes with the last row, so an empty pool is NULL rather than a zero-length allocation.
      free(node_ptr->container.children);
      node_ptr->container.children = NULL;
    }
    goto restitch;
  }

  while ((size_t)node_ptr->container.n_children < want) {
    size_t             slot = (size_t)node_ptr->container.n_children;
    const IrcitUiNode *ref  = sSlotTemplate(state_ptr, slot);
    IrcitUiNode       *row;
    IrcitUiNode      **grown;

    if (!ref) ref = sFallbackTemplate(state_ptr);
    if (!ref) { ok = 0; break; }
    row = IrcitUiClone(ref);
    if (!row) { ok = 0; break; }
    grown = realloc(node_ptr->container.children, ((size_t)node_ptr->container.n_children + 1) * sizeof *grown);
    if (!grown) {
      sFreeTree(row);
      ok = 0;
      break;
    }
    node_ptr->container.children                                   = grown;
    node_ptr->container.children[node_ptr->container.n_children++] = row;
    row->common.parent = node_ptr;
  }

restitch:
  if (restitching) {
    IrcitUiNode **grown = realloc(node_ptr->container.children,
                                  ((size_t)node_ptr->container.n_children + 1) * sizeof *grown);
    if (!grown) {
      // Nowhere to put it back, so it is released rather than leaked.
      sFreeTree(header);
      state_ptr->header_node    = NULL;
      state_ptr->pinned_section = IRCITUI_NO_SECTION;
      return 0;
    }
    node_ptr->container.children                                   = grown;
    node_ptr->container.children[node_ptr->container.n_children++] = header;
    header->common.parent                                          = node_ptr;
  }
  return ok;
}

/* Clear what a previous dataset position left on a row's subtree.
 *
 * A row node is REUSED: when the pool rebinds it to a different entry, everything
 * its children were carrying for the old one comes with it. SS24.1.2 names the
 * cases -- a text selection range, a nested checkable switch left toggled -- and
 * the binder cannot be asked to undo them, because a binder is told a POSITION and
 * not the history of the node it is handed.
 *
 * Only what a rebind invalidates. Text is not touched: the binder overwrites it,
 * and a row template's static labels are the same for every position. Focus is not
 * touched: it is the engine's, and clearing it here would leave IrcitUiFocus
 * naming a node the engine believes is unfocused.
 *
 * A kind that carries state onto a row and is not listed here is a kind whose state
 * survives a rebind; this function is where that list is kept. */
static void
sRecycleClear(IrcitUiNode *node_ptr)
{
  int i;

  node_ptr->common.flags &= ~IRCITUI_F_CHECKED;
  node_ptr->container.sel0 = node_ptr->container.sel1 = -1;
  for (i = 0; i < node_ptr->container.n_children; i++) sRecycleClear(node_ptr->container.children[i]);
}

/* The scavenging pass. Rows are positioned and rebound; nothing is allocated
 * except on the pass that changes the pool's size.
 *
 * This is what makes the recycler virtual, and the claim it has to keep is a
 * counting one: a dataset of fifty thousand rows costs a pool of
 * viewport height + 2, and scrolling moves bindings rather than nodes. */
static void
sLayoutRecycler(IrcitUiNode *node_ptr)
{
  IrcitUiRecyclerState *state_ptr = node_ptr->common.widget_state;
  int                   cx, cy, cw, ch;
  size_t                want_rows, i;

  if (!state_ptr) return;
  // Defensive rather than load-bearing.
  if (state_ptr->item_fixed_height <= 0) state_ptr->item_fixed_height = 1;

  sContentBox(node_ptr, &cx, &cy, &cw, &ch);

  state_ptr->visible_rows = (size_t)(ch / state_ptr->item_fixed_height);

  {
    size_t limit = state_ptr->adapter.item_count > state_ptr->visible_rows
                     ? state_ptr->adapter.item_count - state_ptr->visible_rows : 0;
    if (state_ptr->scroll_offset_y > limit) state_ptr->scroll_offset_y = limit;
  }

  want_rows = state_ptr->visible_rows + 2;
  // Never more rows than there are items.
  if (want_rows > state_ptr->adapter.item_count) want_rows = state_ptr->adapter.item_count;
  (void)sRecyclerPoolResize(node_ptr, state_ptr, want_rows);
  // After the resize, because the resize is what maintains "the header is last" .
  (void)sRecyclerHeaderEnsure(node_ptr, state_ptr);

  if (state_ptr->prev_offset != state_ptr->scroll_offset_y) {
    for (i = 0; i < (size_t)node_ptr->container.n_children; i++) sRecycleClear(node_ptr->container.children[i]);
  }

  // Rows only. The pinned header, when there is one, is the last child and is the layout's business further down.
  for (i = 0; i < sPoolRowCount(node_ptr, state_ptr); i++) {
    IrcitUiNode *      row   = node_ptr->container.children[i];
    size_t             index = state_ptr->scroll_offset_y + i;
    const IrcitUiNode *ref   = sSlotTemplate(state_ptr, i);
    int                rebuilt;

    if (!ref) {
      row->common.flags &= ~IRCITUI_F_VISIBLE;
      continue;
    }

    rebuilt = (row->common.ref != ref);
    if (rebuilt && !sPoolSlotRebind(node_ptr, i, ref)) {
      // The clone failed. The old row is still in the slot and is still the wrong shape, so it is hidden rather than shown.
      row->common.flags &= ~IRCITUI_F_VISIBLE;
      continue;
    }
    row                = node_ptr->container.children[i];
    row->common.flags |= IRCITUI_F_VISIBLE;
    sPlace(row, cx, cy + (int)i * state_ptr->item_fixed_height, cw, state_ptr->item_fixed_height);
    IrcitUiLayout(row, row->common.x, row->common.y, row->common.w, row->common.h);

    if (state_ptr->adapter.on_bind_view &&
        (rebuilt || index != state_ptr->editing_index || state_ptr->prev_offset != state_ptr->scroll_offset_y)) {
      state_ptr->adapter.on_bind_view(row, index, state_ptr->adapter.user_dataset);
    }
  }

  if (state_ptr->edit_focus_pending) {
    IrcitUiNode *field;

    state_ptr->edit_focus_pending = 0;
    field                         = IrcitUiRecyclerRowAt(node_ptr, state_ptr->editing_index);
    if (field && (field->common.flags & IRCITUI_F_VISIBLE)) IrcitUiSetFocus(field);
  }
  // --- the pinned header
  if (state_ptr->header_node) {
    IrcitUiNode *header  = state_ptr->header_node;
    size_t       section = sStickySection(state_ptr);

    if (section != state_ptr->pinned_section) {
      state_ptr->pinned_section = section;
      header->common.flags &= ~IRCITUI_F_DRAWN;
    }

    if (section == IRCITUI_NO_SECTION) {
      header->common.flags &= ~IRCITUI_F_VISIBLE;
    } else {
      header->common.flags |= IRCITUI_F_VISIBLE;
      sPlace(header, cx, cy, cw, state_ptr->item_fixed_height);
      IrcitUiLayout(header, header->common.x, header->common.y, header->common.w, header->common.h);
      if (state_ptr->adapter.on_bind_view) {
        state_ptr->adapter.on_bind_view(header, section, state_ptr->adapter.user_dataset);
      }
    }
  }

  // Recorded after the bind loop, so the next pass compares against what this one actually showed rather than against what it intended to.
  state_ptr->prev_offset = state_ptr->scroll_offset_y;
}

/* The dataset index the row containing @p node_ptr is bound to.
 *
 * Derived, never stored. The pass binds row i to `scroll_offset_y + i`, so the
 * answer is that arithmetic over the row's place in container.children -- which
 * IS the viewport. Storing it on the row instead is SS16's named anti-pattern:
 * a row is rebound as the list scrolls, so a mark or an index kept on the node
 * follows the pool slot and ends up describing a different dataset entry.
 *
 * Returns 0 when the node is not inside a recycler row, or when the row is not
 * in its parent's child array (which a pass in progress can briefly produce).
 * Both callers -- the painter and the click -- treat 0 as "no row here". */
static int
sRowBoundIndex(const IrcitUiNode *node_ptr, IrcitUiNode **list_out_ptr, size_t *index_out_ptr)
{
  const IrcitUiNode *        row         = node_ptr;
  const IrcitUiNode *        list_ptr;
  const IrcitUiRecyclerState *state_ptr;
  int                        i;

  while (row && !(row->common.parent && row->common.parent->common.kind == IRCITUI_RECYCLER)) {
    row = row->common.parent;
  }
  if (!row) return 0;
  list_ptr  = row->common.parent;
  state_ptr = list_ptr->common.widget_state;
  if (!state_ptr) return 0;
  // The pinned header is a child of the same container and is NOT a row.
  if (row == state_ptr->header_node) return 0;
  for (i = 0; i < list_ptr->container.n_children; i++) {
    if (list_ptr->container.children[i] == row) break;
  }
  if (i >= list_ptr->container.n_children) return 0;
  if (list_out_ptr) *list_out_ptr = (IrcitUiNode *)list_ptr;
  if (index_out_ptr) *index_out_ptr = state_ptr->scroll_offset_y + (size_t)i;
  return 1;
}

/* Tell the application that a row was chosen.
 *
 * Returns whether anyone was told. With no on_item_clicked registered this is 0,
 * and the caller leaves the key to bubble: Enter aimed at a list nobody is
 * listening to is still Enter for whatever is behind it. That is the same veto
 * rule the scroll keys follow, and it is why this returns rather than firing
 * blindly.
 *
 * The index handed over is the DATASET position, never the row's place in the
 * viewport: a pool slot means something different after every scroll, so an
 * application given one would act on the wrong entry. The row node goes with it
 * because that is where the application's own binder put the value, but it is
 * explicitly not something to retain -- see the field's comment. */
static int
sRowActivated(IrcitUiNode *list_ptr, size_t index)
{
  IrcitUiRecyclerState *state_ptr = list_ptr->common.widget_state;

  if (!state_ptr || !state_ptr->adapter.on_item_clicked) return 0;
  if (index >= state_ptr->adapter.item_count) return 0;
  state_ptr->adapter.on_item_clicked(IrcitUiRecyclerRowAt(list_ptr, index), index, state_ptr->adapter.user_dataset);
  return 1;
}

/* The attribute a node must carry because of the row it belongs to.
 *
 * Asked of every painted node, so the row's own labels carry the highlight as
 * well as the row: a row is a linear whose children paint their own cells, so
 * marking the row's window alone would be painted over. Deriving it here rather
 * than storing a flag on the node is what keeps a highlight from following the
 * pool slot -- see sRowBoundIndex. */
static uint32_t
sRowForcedAttr(const IrcitUiNode *node_ptr)
{
  const IrcitUiRecyclerState *state_ptr;
  IrcitUiNode *               list_ptr;
  size_t                      index;

  if (!sRowBoundIndex(node_ptr, &list_ptr, &index)) return 0;
  state_ptr = list_ptr->common.widget_state;
  if (!state_ptr->selected_bits || !state_ptr->highlight_attr_mask) return 0;
  if (index >= state_ptr->adapter.item_count) return 0;
  if (!(state_ptr->selected_bits[index >> 5] & (1u << (index & 31u)))) return 0;
  return state_ptr->highlight_attr_mask;
}

static void
sLayoutFrame(IrcitUiNode *node_ptr)
{
  int i, sPx, py, pw, ph, mw, mh, x, y;

  sContentBox(node_ptr, &sPx, &py, &pw, &ph);

  for (i = 0; i < node_ptr->container.n_children; i++) {
    IrcitUiNode *c = node_ptr->container.children[i];
    if (sIsStacked(c)) continue;
    mw = sMeasW(c, pw);
    mh = sMeasH(c, ph);
    if (mw > pw) mw = pw;
    if (mh > ph) mh = ph;
    x = sPx;
    y = py;
    switch (c->common.layout_gravity) {
    case IRCITUI_G_END: x = sPx + pw - mw;
      break;
    case IRCITUI_G_BOTTOM: y = py + ph - mh;
      break;
    case IRCITUI_G_CENTER:
      x = sPx + (pw - mw) / 2;
      y = py + (ph - mh) / 2;
      break;
    case IRCITUI_G_FILL:
      mw = pw;
      mh = ph;
      break;
    default: break;
    }
    sPlace(c, x, y, mw, mh);
    IrcitUiLayout(c, c->common.x, c->common.y, c->common.w, c->common.h);
  }
}

static int
sEdgeY(IrcitUiNode *parent_ptr, IrcitUiNode *sib_ptr, int sSpec, int which)
{
  (void)sib_ptr;
  // which: 0=top of target, 1=bottom of target
  if (sSpec == IRCITUI_PARENT) {
    int b = (parent_ptr->common.flags & IRCITUI_F_BORDER) ? 1 : 0;
    return which ?
             parent_ptr->common.y + parent_ptr->common.h - parent_ptr->common.padding[2] - b :
             parent_ptr->common.y + parent_ptr->common.padding[0] + b;
  }
  if (sSpec >= 0 && sSpec < parent_ptr->container.n_children) {
    IrcitUiNode *t = parent_ptr->container.children[sSpec];
    return which ? t->common.y + t->common.h : t->common.y;
  }
  return IRCITUI_UNSET;
}

static int
sEdgeX(IrcitUiNode *parent_ptr, IrcitUiNode *sib_ptr, int sSpec, int which)
{
  (void)sib_ptr;
  if (sSpec == IRCITUI_PARENT) {
    int b = (parent_ptr->common.flags & IRCITUI_F_BORDER) ? 1 : 0;
    return which ?
             parent_ptr->common.x + parent_ptr->common.w - parent_ptr->common.padding[1] - b :
             parent_ptr->common.x + parent_ptr->common.padding[3] + b;
  }
  if (sSpec >= 0 && sSpec < parent_ptr->container.n_children) {
    IrcitUiNode *t = parent_ptr->container.children[sSpec];
    return which ? t->common.x + t->common.w : t->common.x;
  }
  return IRCITUI_UNSET;
}

static void
sLayoutConstraint(IrcitUiNode *node_ptr)
{
  int i,   pass, top, bot, st, en, mw, mh;
  int sPx, py,   pw,  ph;
  sContentBox(node_ptr, &sPx, &py, &pw, &ph);

  // seed wrap sizes at origin so sibling refs have geometry
  for (i = 0; i < node_ptr->container.n_children; i++) {
    IrcitUiNode *c = node_ptr->container.children[i];
    if (sIsStacked(c)) continue;
    mw = sMeasW(c, pw);
    mh = sMeasH(c, ph);
    sPlace(c, sPx, py, mw, mh);
  }

  for (pass = 0; pass < PRIV_CONFIG_DEFAULT_LAYOUT_PASSES; pass++) {
    for (i = 0; i < node_ptr->container.n_children; i++) {
      IrcitUiNode *c = node_ptr->container.children[i];
      if (sIsStacked(c)) continue;
      top = bot = st = en = IRCITUI_UNSET;

      if (c->common.c_top_top != IRCITUI_UNSET) top = sEdgeY(node_ptr, c, c->common.c_top_top, 0);
      else if (c->common.c_top_bot != IRCITUI_UNSET) top = sEdgeY(node_ptr, c, c->common.c_top_bot, 1);

      if (c->common.c_bot_bot != IRCITUI_UNSET) bot = sEdgeY(node_ptr, c, c->common.c_bot_bot, 1);
      else if (c->common.c_bot_top != IRCITUI_UNSET) bot = sEdgeY(node_ptr, c, c->common.c_bot_top, 0);

      if (c->common.c_start_start != IRCITUI_UNSET) st = sEdgeX(node_ptr, c, c->common.c_start_start, 0);
      else if (c->common.c_start_end != IRCITUI_UNSET) st = sEdgeX(node_ptr, c, c->common.c_start_end, 1);

      if (c->common.c_end_end != IRCITUI_UNSET) en = sEdgeX(node_ptr, c, c->common.c_end_end, 1);
      else if (c->common.c_end_start != IRCITUI_UNSET) en = sEdgeX(node_ptr, c, c->common.c_end_start, 0);

      mw = sMeasW(c, pw);
      mh = sMeasH(c, ph);

      if (st != IRCITUI_UNSET && en != IRCITUI_UNSET && (c->common.layout_w.mode == IRCITUI_SIZE_MATCH || c->common.
        layout_w.mode == IRCITUI_SIZE_EXACT || (c->common.layout_w.mode == IRCITUI_SIZE_WRAP && en - st > mw))) mw = en
        - st;
      else if (st == IRCITUI_UNSET && en != IRCITUI_UNSET) st = en - mw;
      else if (st == IRCITUI_UNSET) st = node_ptr->common.x + node_ptr->common.padding[3];

      if (top != IRCITUI_UNSET && bot != IRCITUI_UNSET && (c->common.layout_h.mode == IRCITUI_SIZE_MATCH || (bot - top >
        mh)))
        mh = bot - top;
      else if (top == IRCITUI_UNSET && bot != IRCITUI_UNSET) top = bot - mh;
      else if (top == IRCITUI_UNSET) top = node_ptr->common.y + node_ptr->common.padding[0];

      if (mw < 1) {
        mw              = 1;
        c->common.flags |= IRCITUI_F_CLIPPED;
      }
      if (mh < 1) {
        mh              = 1;
        c->common.flags |= IRCITUI_F_CLIPPED;
      }
      c->common.x = st + c->common.margin[3];
      c->common.y = top + c->common.margin[0];
      c->common.w = mw - c->common.margin[1] - c->common.margin[3];
      c->common.h = mh - c->common.margin[0] - c->common.margin[2];
      if (c->common.w < 1) {
        c->common.w     = 1;
        c->common.flags |= IRCITUI_F_CLIPPED;
      }
      if (c->common.h < 1) {
        c->common.h     = 1;
        c->common.flags |= IRCITUI_F_CLIPPED;
      }
    }
  }
  for (i = 0; i < node_ptr->container.n_children; i++) {
    IrcitUiNode *c = node_ptr->container.children[i];
    if (sIsStacked(c)) continue;
    IrcitUiLayout(c, c->common.x, c->common.y, c->common.w, c->common.h);
  }
}

/**
 * @brief Pack the conversation window's one child, and publish its scroll state.
 *
 * THE ONE PACKER THAT DOES NOT SIZE ITS CHILDREN FROM THEIR OWN MEASUREMENTS.
 * There is exactly one child and it fills the content box; the arithmetic worth
 * reading is what the engine's scrollable chrome will consume afterwards.
 *
 * `container.columns` -- the overflow -- and `container.scroll_y` both come from
 * the widget: IrcitUiConversationViewportPrime computes the geometry at the box
 * this pass is about to hand the viewport and writes them as it goes. The engine
 * does not re-derive either, and that is the point of doing it this way. The
 * engine's own LINEAR packer computes its overflow ONE ROW HIGH, because it
 * measures the content against the container's ORIGIN while a bordered container's
 * first content row sits one row lower -- measured by dev/probes/conversation's
 * p_conv_scrollable, which needed three independent observables to pin the number
 * down. Reproducing that here would put the scrollbar's ceiling one row away from
 * the model's, so the model publishes its own instead.
 *
 * THE BAR'S COLUMN IS RESERVED ONLY WHEN THERE IS NO BORDER TO PUT IT ON. The
 * engine paints the bar in the node's LAST column; for a bordered node that column
 * IS the border, which the content box already excludes -- measured: box 2..25,
 * bar column 25, content 3..24. A borderless node has no such column, so the bar
 * would land on the bubbles' own last cell unless the content gives it up.
 */
static void
sLayoutConversation(IrcitUiNode *node_ptr)
{
  IrcitUiNode *vp_ptr;
  int          cx, cy, cw, ch;

  // A conversation is scrollable by its nature, so the kind sets the flag itself:
  // the bar painter, the wheel and the thumb-drag all key on it.
  node_ptr->common.flags |= IRCITUI_F_SCROLLABLE;

  sContentBox(node_ptr, &cx, &cy, &cw, &ch);
  if (!(node_ptr->common.flags & IRCITUI_F_BORDER) && cw > 1) {
    cw -= 1;
  }
  if (cw < 1) cw = 1;
  if (ch < 1) ch = 1;

  vp_ptr = IrcitUiConversationViewportEnsure(node_ptr);
  if (!vp_ptr) return;
  // Prime at exactly the box the child is about to get, so the geometry the * painter reads on the next pass is the geometry this pass measured.
  IrcitUiConversationViewportPrime(node_ptr, cw, ch);
  sPlace(vp_ptr, cx, cy, cw, ch);
  IrcitUiLayout(vp_ptr, vp_ptr->common.x, vp_ptr->common.y, vp_ptr->common.w, vp_ptr->common.h);
}

static void
sClipToParent(IrcitUiNode *node_ptr)
{
  IrcitUiNode *p = node_ptr->common.parent;
  int          x, y, w, h;
  if (!p || (p->common.flags & IRCITUI_F_SCROLLABLE)) return;
  sContentBox(p, &x, &y, &w, &h);
  if (node_ptr->common.x < x) {
    node_ptr->common.w -= x - node_ptr->common.x;
    node_ptr->common.x = x;
  }
  if (node_ptr->common.y < y) {
    node_ptr->common.h -= y - node_ptr->common.y;
    node_ptr->common.y = y;
  }
  if (node_ptr->common.x + node_ptr->common.w > x + w) node_ptr->common.w = x + w - node_ptr->common.x;
  if (node_ptr->common.y + node_ptr->common.h > y + h) node_ptr->common.h = y + h - node_ptr->common.y;
  if (node_ptr->common.w < 1) {
    node_ptr->common.w     = 1;
    node_ptr->common.flags |= IRCITUI_F_CLIPPED;
  }
  if (node_ptr->common.h < 1) {
    node_ptr->common.h     = 1;
    node_ptr->common.flags |= IRCITUI_F_CLIPPED;
  }
}

/* Flag a node and everything under it as clipped. Used when clipping is
   inherited: a descendant of a node given no space was given none either, and
   has no parent window to derive a derwin from. */
static void
sMarkClipped(IrcitUiNode *node_ptr)
{
  int i;
  node_ptr->common.flags |= IRCITUI_F_CLIPPED;
  for (i = 0; i < node_ptr->container.n_children; i++) sMarkClipped(node_ptr->container.children[i]);
}

PUBLIC_API void
IrcitUiLayout(IrcitUiNode *node_ptr, int x, int y, int w, int h)
{
  int i;

  sLayoutDepth++;
  node_ptr->common.x        = x;
  node_ptr->common.y        = y;

  node_ptr->common.is_dirty = 0;
  // Clear first: every pass re-derives clipping from the current extents, so a node clipped at a narrow size becomes visible again when it grows.
  node_ptr->common.flags    &= ~IRCITUI_F_CLIPPED;
  node_ptr->common.w        = w < 1 ? 1 : w;
  node_ptr->common.h        = h < 1 ? 1 : h;
  if (w < 1 || h < 1) node_ptr->common.flags |= IRCITUI_F_CLIPPED;
  sClipToParent(node_ptr);
  if (sIsCollapsed(node_ptr)) {
    for (i = 0; i < node_ptr->container.n_children; i++) sMarkClipped(node_ptr->container.children[i]);
  }
  else
    switch (node_ptr->common.kind) {
    case IRCITUI_LINEAR: sLayoutLinear(node_ptr);
      break;
    case IRCITUI_FRAME: sLayoutFrame(node_ptr);
      break;
    case IRCITUI_CONSTRAINT: sLayoutConstraint(node_ptr);
      break;
    case IRCITUI_RECYCLER: sLayoutRecycler(node_ptr);
      break;
    case IRCITUI_CONVERSATION_WINDOW: sLayoutConversation(node_ptr);
      break;
    default: break;
    }
  if (node_ptr->common.parent && (node_ptr->common.parent->common.flags & IRCITUI_F_CLIPPED) && !(node_ptr->common.flags & IRCITUI_F_CLIPPED)) sMarkClipped(node_ptr);
  for (i = 0; i < node_ptr->container.n_children; i++)
    if (sIsStacked(node_ptr->container.children[i]) && node_ptr->container.children[i]->common.w <= COLS && node_ptr-> container.children[i]->common.h <= LINES)
      IrcitUiDragTo(node_ptr->container.children[i], node_ptr->container.children[i]->common.x, node_ptr->container.children[i]->common.y);
  sLayoutDepth--;
}

/* Is this node already holding the window this pass would build for it?
 *
 * The rebuild was unconditional, and that is what put a whole-screen clear on the glass: a
 * window re-created at screen size comes back from ncurses with its clear flag up, so the
 * next doupdate owes the terminal `ESC[H ESC[2J` and a repaint of every cell. A streamed
 * message asks for a layout per message, so the demo cleared and redrew the screen once per
 * message. Measured in dev/probes/looper/p_realize_keep.c 'keep': 17 of 17 windows replaced
 * by a pass that moved no box.
 *
 * The shape is measured against the window's own parent, because a derwin is positioned
 * RELATIVE to its parent's window while a full-screen newwin is absolute. `parent_rebuilt` is
 * tested first on purpose: once the parent's window has been replaced, the child's window is
 * a derwin of an object that is no longer the parent's, so nothing about it may be read. */
static bool
sWindowIsCurrent(const IrcitUiNode *node_ptr, bool via_parent, int ry, int rx, int rh, int rw, bool parent_rebuilt)
{
  WINDOW *win_ptr = node_ptr->common.win;
  WINDOW *parent_win_ptr;

  if (!win_ptr || parent_rebuilt) return false;
  if (getmaxy(win_ptr) != rh || getmaxx(win_ptr) != rw) return false;
  if (!via_parent)
    return wgetparent(win_ptr) == NULL && getbegy(win_ptr) == node_ptr->common.y &&
           getbegx(win_ptr) == node_ptr->common.x;
  parent_win_ptr = node_ptr->common.parent->common.win;
  return wgetparent(win_ptr) == parent_win_ptr && getbegy(win_ptr) - getbegy(parent_win_ptr) == ry &&
         getbegx(win_ptr) - getbegx(parent_win_ptr) == rx;
}

/* Returns whether this node's window was replaced, which is what its children need to know. */
static bool
sRealizeSubtree(IrcitUiNode *node_ptr, bool parent_rebuilt)
{
  int  i, rh, rw, ry = 0, rx = 0;
  bool via_parent = false, rebuilt = false;

  // Re-point the alias at the field's own buffer, which outlives the window; clearing it here would eat typed text on a resize.
  if (node_ptr->common.kind == IRCITUI_EDIT_TEXT) IrcitUiEditTextAliasBind(node_ptr);
  // Clipping is inherited before it is acted on.
  if (node_ptr->common.parent && (node_ptr->common.parent->common.flags & IRCITUI_F_CLIPPED)) node_ptr->common.flags |=
    IRCITUI_F_CLIPPED;
  if (node_ptr->common.flags & IRCITUI_F_CLIPPED) {
    sMarkClipped(node_ptr);
    IrcitUiDeflate(node_ptr);
    return true;
  }
  rh = node_ptr->common.h < 1 ? 1 : node_ptr->common.h;
  rw = node_ptr->common.w < 1 ? 1 : node_ptr->common.w;
  // Stacked popups are independent newwins so mvwin/drag works. Nested widgets stay derwin so they sClip to the popup.
  if (!(node_ptr->common.flags & IRCITUI_F_STACKED) && node_ptr->common.parent && node_ptr->common.parent->common.win) {
    via_parent = true;
    ry = node_ptr->common.y - node_ptr->common.parent->common.y;
    rx = node_ptr->common.x - node_ptr->common.parent->common.x;
    if (ry < 0) {
      rh += ry;
      ry = 0;
    }
    if (rx < 0) {
      rw += rx;
      rx = 0;
    }
    if (ry + rh > node_ptr->common.parent->common.h) rh = node_ptr->common.parent->common.h - ry;
    if (rx + rw > node_ptr->common.parent->common.w) rw = node_ptr->common.parent->common.w - rx;
  }
  if (!sWindowIsCurrent(node_ptr, via_parent, ry, rx, rh, rw, parent_rebuilt)) {
    bool had_window = node_ptr->common.win != NULL;

    if (had_window) {
      delwin(node_ptr->common.win);
      node_ptr->common.win = NULL;
      if (IrcitUiShadow() && sIsStacked(node_ptr) && node_ptr->common.parent) sPaintDirty(node_ptr->common.parent);
    }
    if (via_parent) {
      if (rh > 0 && rw > 0) node_ptr->common.win = derwin(node_ptr->common.parent->common.win, rh, rw, ry, rx);
    }
    else {
      node_ptr->common.win = newwin(rh, rw, node_ptr->common.y, node_ptr->common.x);
    }
    // Unconditionally: a rebuilt window holds nothing, and a node coming back from a deflate
    // still carries IRCITUI_F_DRAWN from before it was clipped, so nothing else would repaint it.
    sPaintDirty(node_ptr);
    if (!node_ptr->common.win) return true;
    keypad(node_ptr->common.win, TRUE);
    // The engine draws the only cursor there is, so no window may also park the hardware one.
    leaveok(node_ptr->common.win, TRUE);
    if (node_ptr->common.kind == IRCITUI_IMAGE_VIEW) IrcitUiImagePrepare(node_ptr);
    rebuilt = true;
  }
  else {
    // Kept, and NOT repainted: whoever asked for this layout already marked itself (IrcitUiRequestLayout paints the node it was called on), and a node that asked for nothing has nothing to show.
  }
  for (i = 0; i < node_ptr->container.n_children; i++) {
    node_ptr->container.children[i]->common.parent = node_ptr;
    (void)sRealizeSubtree(node_ptr->container.children[i], rebuilt);
  }
  return rebuilt;
}

PUBLIC_API void
IrcitUiRealize(IrcitUiNode *node_ptr)
{
  if (node_ptr) (void)sRealizeSubtree(node_ptr, false);
}

/* Is this node the cursor row of its selectable parent? Pointer comparison
   against the parent's child table rather than an index, so a child that has
   been reordered or a cursor left over from a previous layout cannot mark the
   wrong row. sel0 is bounds-checked first, so an out-of-range cursor draws
   nothing instead of reading past the child table. */
static int
sMenuRowIsCursor(const IrcitUiNode *node_ptr)
{
  const IrcitUiNode *p = node_ptr->common.parent;
  if (!p || !(p->common.flags & IRCITUI_F_CHILDREN_SELECTABLE)) return 0;
  if (p->container.sel0 < 0 || p->container.sel0 >= p->container.n_children) return 0;
  return p->container.children[p->container.sel0] == node_ptr;
}

/* Whether the open indicator takes the container's top-RIGHT corner.
 *
 * The glyph sits on whichever top corner faces the parent's centre, so it always
 * faces the content it folds: a panel on the right of its parent gets its
 * top-left corner, one on the left gets its top-right. No parent, or a dead heat
 * between the two centres, falls to the left.
 *
 * Position against the parent is what decides it, not the orientation, because
 * what the reader needs is the glyph pointing at the content it will reveal -
 * and that is a question about where the container sits, not about how it packs
 * its own children. */
static int
sIndicatorOnRight(const IrcitUiNode *node_ptr)
{
  const IrcitUiNode *p = node_ptr->common.parent;
  if (!p) return 0;
  return (node_ptr->common.x + node_ptr->common.w / 2) < (p->common.x + p->common.w / 2);
}

/* The column the indicator occupies in the container's own window.
 *
 * One function because the paint and the hit test must agree: they are two
 * readers of the same decision, and a click that lands one cell from the glyph
 * is the failure that would follow from computing it twice.
 *
 * Folded, the answer is the origin whatever the side: a folded strip is one cell
 * across, so both corners are the same cell and there is nothing to choose. */
static int
sIndicatorCol(const IrcitUiNode *node_ptr)
{
  if (sIsCollapsed(node_ptr)) return 0;
  return sIndicatorOnRight(node_ptr) ? node_ptr->common.w - 1 : 0;
}

/* The glyph the indicator shows: the one the template named, or the framework
 * default for this container's collapse direction.
 *
 * Direction-dependent because the glyph has to agree with the axis the container
 * actually folds along - an up/down fold reads as a vertical triangle pair, a
 * left/right fold as a horizontal one. A template that names one wins, and any
 * single glyph it names is taken as given.
 *
 * The defaults live here rather than in the generated model on purpose: a node
 * built through IrcitUiWidgetDef has no template to inherit from and needs this
 * fallback anyway, so one copy stops the two routes drifting apart. */
/* The framework's default glyphs, one entry per fold case.
 *
 * Named rows rather than a ternary per call site: the two vertical pairs are
 * mirrors of each other, and writing them out twice is how a mirror stops being
 * one. The names carry the rule, so the table reads as the decision it encodes.
 *
 * They live here rather than in config_defs.h because that header holds integer
 * limits and says so in its own preamble, and rather than in the generated model
 * because a node built through IrcitUiWidgetDef has no template to inherit from
 * and needs this fallback either way. */
typedef struct
{
  const char *open;
  const char *folded;
} IrcitUiGlyphPair;

/* Folds up and down, so the side it sits on says nothing about the direction. */
static const IrcitUiGlyphPair s_glyphs_updown = {"▽", "△"};
/* Vertical, left of its parent: opens outward to the left, folds back right. */
static const IrcitUiGlyphPair s_glyphs_left   = {"◁", "▷"};
/* Vertical, right of its parent: the mirror of the above. */
static const IrcitUiGlyphPair s_glyphs_right  = {"▷", "◁"};

/* The glyph the indicator shows: the one the template named, or the row above
 * that matches this container's fold.
 *
 * A horizontal fold points the way the edge travels - outward while the
 * container is open, because that is where a click sends the edge, and back
 * inward once folded, because that is where a click brings it back. Which way
 * is "outward" for a vertical fold depends on the side of the parent the
 * container sits on, so the pair is mirrored for a left-hand rail. The same
 * helper that places the corner picks the row, so the arrow cannot end up
 * pointing away from the corner it is drawn on. */
static const char *
sIndicatorGlyph(const IrcitUiNode *node_ptr, int collapsed)
{
  const char *            g = collapsed ? IrcitUiDeclIndicatorClose(node_ptr) : IrcitUiDeclIndicatorOpen(node_ptr);
  const IrcitUiGlyphPair *pair;
  if (g && g[0]) return g;
  if (sOrientationOf(node_ptr) == IRCITUI_HORIZ) pair = &s_glyphs_updown;
  else pair = sIndicatorOnRight(node_ptr) ? &s_glyphs_left : &s_glyphs_right;
  return collapsed ? pair->folded : pair->open;
}

/* The fill for a folded strip: the template's glyph, or heavy shade. */
static const char *
sFillGlyph(const IrcitUiNode *node_ptr)
{
  const char *g = IrcitUiDeclCollapseFill(node_ptr);
  return (g && g[0]) ? g : "░";
}

/* Paint one glyph per cell across a w x h area.
 *
 * Steps by the glyph's display width rather than by one, and stops rather than
 * splitting a wide glyph at the right edge, which is the rule the edit field's
 * painter (ircitui_edit_text.c) already follows. The step has to be computed here
 * because IrcitUiAddUtf8 returns nothing to advance by. */
static void
sPaintGlyphArea(WINDOW *win_ptr, int w, int h, const char *glyph)
{
  int y, x, adv = sDispCols(glyph);
  if (adv < 1) adv = 1;
  for (y = 0; y < h; y++) for (x = 0; x + adv <= w; x += adv) IrcitUiAddUtf8(win_ptr, y, x, glyph, 1);
}

static void
sBarInner(const IrcitUiNode *node_ptr, int *x_ptr, int *y_ptr, int *w_ptr, int *h_ptr)
{
  int border = (node_ptr->common.flags & IRCITUI_F_BORDER) ? 1 : 0;
  int w = node_ptr->common.w - node_ptr->common.padding[1] - node_ptr->common.padding[3] - 2 * border;
  int h = node_ptr->common.h - node_ptr->common.padding[0] - node_ptr->common.padding[2] - 2 * border;

  *x_ptr = node_ptr->common.padding[3] + border;
  *y_ptr = node_ptr->common.padding[0] + border;
  *w_ptr = w > 0 ? w : 0;
  *h_ptr = h > 0 ? h : 0;
}

/* How many of @p inner_w columns are filled at @p pct, which is 0..100.

   Rounded half UP, in integer arithmetic. The design document uses roundf on a
   float product; this is the same rounding without the float, and it matters that
   it is stated rather than left to a cast, because a truncating (inner_w * pct) /
   100 would leave the last column of a 100%-wide bar unfilled at every value below
   the top and make a full bar only reachable exactly at 100.

   The two ends are returned outright rather than computed, so 100 is always a
   full bar and 0 always an empty one whatever the width -- the property a caller
   reads back and a probe asserts. */
static int
sBarSplit(int inner_w, int pct)
{
  int filled;
  if (inner_w <= 0) return 0;
  if (pct <= 0) return 0;
  if (pct >= 100) return inner_w;
  filled = (inner_w * pct + 50) / 100;
  if (filled > inner_w) filled = inner_w;
  return filled;
}

/* The bar's default glyphs, used when the template named none.

   U+2588 FULL BLOCK and U+2591 LIGHT SHADE, which is the pair the design document
   starts from. Both are single-column, so a run of them needs no wide-glyph
   handling -- the same property the frame profiles have, and for the same reason:
   a bar is a run of identical cells and a double-width glyph in it would have to
   decide what to do with a half-covered cell. */
static const char *const sBarDefaultFill  = "\xe2\x96\x88"; /* U+2588 */
static const char *const sBarDefaultEmpty = "\xe2\x96\x91"; /* U+2591 */

static void
sBarLabel(char *buf_ptr, size_t n, int pct)
{
  snprintf(buf_ptr, n, " %3d%% ", pct);
}

/* The width the bar's TRACK may use: its inner box, less an adjacent label's reservation.
 *
 * The single source for that number, which is the point. Two readers need it -- the
 * painter, to know where the run ends, and (when seeking lands) the hit test, to know what
 * to divide a column by -- and if they computed it separately they would eventually
 * disagree by a column, which is a click that seeks to the wrong cell.
 *
 * The reservation is DROPPED rather than the bar mangled when there is not room for both:
 * a four-cell bar with a six-column label beside it leaves nothing to show the value in,
 * and a label with no bar under it reports a percentage of nothing. The +4 margin is the
 * design document's own, and it keeps the two from ending up touching with no gap. */
static int
sBarTrackW(const IrcitUiNode *node_ptr, int inner_w)
{
  if (sBarLabelModeOf(node_ptr) != IRCITUI_BAR_LABEL_ADJACENT) return inner_w;
  if (inner_w <= PRIV_CONFIG_DEFAULT_BAR_LABEL_W + 4) return inner_w;
  return inner_w - PRIV_CONFIG_DEFAULT_BAR_LABEL_W;
}

/* The inverse of the metric: which fraction of the track a mouse column is.
 *
 * THE ONLY PLACE A COLUMN BECOMES A POSITION, on the same terms as sBarSplit being the only
 * place a position becomes columns. Both go through sBarInner and sBarTrackW, which is what
 * stops an adjacent label's reserved columns from being counted by the painter and
 * forgotten here -- the one-column disagreement that is invisible until a user clicks and
 * the bar lands somewhere they did not point at.
 *
 * The clamp is to the track's width and not to its last cell. A pointer at or past the
 * right edge reads 1.0, meaning "the end", which is what a user dragging past the end of a
 * bar means by it. Clamping to track_w - 1 would make 100% unreachable by dragging, and a
 * bar whose maximum cannot be reached with the mouse is a defect nobody diagnoses from the
 * picture: it just looks like the bar stops slightly short.
 *
 * Returns 0 without touching *frac_ptr when there is no track -- too narrow, or not a bar
 * at all -- so a caller cannot act on a fraction that was never computed. */
static int
sBarSeekAt(IrcitUiNode *node_ptr, int mouse_x, float *frac_ptr)
{
  int ix, iy, iw, ih, track_w, col;

  if (!node_ptr || !frac_ptr) return 0;
  sBarInner(node_ptr, &ix, &iy, &iw, &ih);
  track_w = sBarTrackW(node_ptr, iw);
  if (track_w < 1) return 0;

  col = mouse_x - ix;
  if (col < 0) col = 0;
  if (col > track_w) col = track_w;
  *frac_ptr = (float)col / (float)track_w;
  return 1;
}

/* Report a gesture to the application, if one is bound.
 *
 * A thin wrapper, and it earns its place by being called from three points in the mouse
 * path that all need the same two checks -- is there state, is there a callback. A bar with
 * no binding is the common case, since a decorative progress bar wants none, and the
 * wrapper is what keeps the engine's dispatch from growing three copies of that test.
 *
 * The id is passed from the node rather than stored beside the callback. It is the same
 * string either way -- a borrowed layout literal -- and reading it here means a node whose
 * id changed cannot report under the old one. */
static void
sBarSeekReport(IrcitUiNode *node_ptr, int mouse_x, IrcitUiSeekPhase phase)
{
  IrcitUiBarState *state = sBarState(node_ptr);
  float            frac;

  if (!state || !state->on_seek) return;
  if (!sBarSeekAt(node_ptr, mouse_x, &frac)) return;
  state->on_seek(node_ptr->common.id ? node_ptr->common.id : "", phase, frac, state->seek_user);
}

/* One arrow press on a focused bar: step it by one COLUMN and report where that lands.
 *
 * A COLUMN AND NOT A PERCENTAGE, so that a keypress and a click at the same place report
 * the same fraction. The callback's value means "where along the track", and a step that
 * reported whole-percent increments would report positions no click can produce -- the
 * fraction would stop being a place on the track and become two different things depending
 * on which device asked. A host that wants finer control owns the value anyway: it receives
 * a float and may set whatever number it likes, coarser or finer than the column it was
 * handed.
 *
 * THE STEP IS COMPUTED FROM THE COLUMN THE VALUE OCCUPIES, not from the value itself, and
 * that is what keeps repeated presses from drifting: value -> column -> value is the same
 * arithmetic sBarSplit and the painter already use, so pressing left k times moves the
 * drawn bar exactly k columns whatever the starting value.
 *
 * Returns 0 when the bar is unbound or has no track, so the caller does not consume the key
 * -- which is what stops a decorative progress bar from swallowing the arrow keys of the
 * list it sits in. */
static int
sBarKeySeek(IrcitUiNode *node_ptr, int delta)
{
  IrcitUiBarState *state = sBarState(node_ptr);
  int              ix, iy, iw, ih, track_w, col, filled;
  float            frac;

  if (!state || !state->on_seek) return 0;
  sBarInner(node_ptr, &ix, &iy, &iw, &ih);
  track_w = sBarTrackW(node_ptr, iw);
  if (track_w < 1) return 0;

  filled = sBarSplit(track_w, state->value);
  col    = filled + delta;
  if (col < 0) col = 0;
  if (col > track_w) col = track_w;

  frac = (float)col / (float)track_w;
  state->on_seek(node_ptr->common.id ? node_ptr->common.id : "", IRCITUI_PHASE_RELEASED, frac, state->seek_user);
  return 1;
}

/* Bring the gradient's registers up to date. Called from the paint pass, so it has to be
   cheap when nothing has changed -- which is what ramp_written records, and why setting
   the endpoints is the only thing that clears it.
 *
 * ACQUIRE ONCE AND KEEP IT. The ramp is taken at a FIXED resolution and held for as long
 * as the bar has a gradient, so a value change -- every tick of a running bar -- does not
 * touch the palette at all. That is the correction this makes to the design document,
 * which acquires one register per filled column and so releases and re-acquires on every
 * change: a malloc and a color_content() per register in, an init_color() per register
 * out, on the same path that draws the frame.
 *
 * A refusal is neither retried nor treated as an error: ui_palette_priv.h is explicit that
 * a caller handed -1 must draw without the ramp, and ramp_base stays -1 so the fact is
 * READABLE rather than silently flattened into "the gradient is subtle today". */
static void
sBarRampEnsure(IrcitUiBarState *state_ptr)
{
  int i;

  if (!state_ptr || !state_ptr->gradient) return;

  if (state_ptr->ramp_base < 0) {
    state_ptr->ramp_steps = PRIV_CONFIG_DEFAULT_BAR_RAMP_STEPS;
    state_ptr->ramp_base  = IrcitUiPaletteRampAcquire(state_ptr->ramp_steps, sBarRampOwner);
    if (state_ptr->ramp_base < 0) return;
    state_ptr->ramp_written = 0;
  }
  if (state_ptr->ramp_written) return;

  for (i = 0; i < state_ptr->ramp_steps; i++) {
    // Both endpoints are exact rather than approached: at i = 0 the factor is 0 and at i = steps-1 it is 1.
    int den = state_ptr->ramp_steps > 1 ? state_ptr->ramp_steps - 1 : 1;
    IrcitUiPaletteRampSet(state_ptr->ramp_base, i,
                          state_ptr->from_rgb[0] + (state_ptr->to_rgb[0] - state_ptr->from_rgb[0]) * i / den,
                          state_ptr->from_rgb[1] + (state_ptr->to_rgb[1] - state_ptr->from_rgb[1]) * i / den,
                          state_ptr->from_rgb[2] + (state_ptr->to_rgb[2] - state_ptr->from_rgb[2]) * i / den);
  }
  state_ptr->ramp_written = 1;
}

/* The style one filled column is drawn in: a step of the ramp when there is one, the
   node's own pair otherwise.
 *
 * `x * steps / bar_w` rather than `x`, because the ramp has a fixed number of steps and the
 * bar a variable number of columns -- this mapping is what lets those two differ, and it
 * is the whole reason a fixed resolution is possible. Clamped at the top because rounding
 * can push the last column's step to `steps` on a narrow bar. */
static chtype
sBarFillStyle(const IrcitUiNode *node_ptr, const IrcitUiBarState *state_ptr, int x, int bar_w)
{
  if (state_ptr && state_ptr->gradient && state_ptr->ramp_base >= 0 && bar_w > 0) {
    int step = bar_w > 1 ? (x * state_ptr->ramp_steps) / bar_w : 0;
    if (step >= state_ptr->ramp_steps) step = state_ptr->ramp_steps - 1;
    return IrcitUiPaletteRampStyle(state_ptr->ramp_base, step, node_ptr->common.fg);
  }
  return COLOR_PAIR(IrcitUiColourPair(node_ptr->common.fg, node_ptr->common.bg));
}

/* Paint the bar: the track, then the label in whichever of the two places it was asked
   for -- and the difference between them is the interesting part.
 *
 *   ADJACENT  the label sits in columns the track does not use, so nothing is masked.
 *   CENTERED  the label sits OVER the track, so each of its cells has to be drawn in a
 *             style that reads against whatever is underneath -- the design document's
 *             "dynamic attribute masking". A character over the filled run is reversed;
 *             over the empty lane it is bold on the muted slot. That is a per-CELL
 *             decision and not a per-run one, which is why it is taken inside the column
 *             loop rather than around it.
 *
 * THE TRACK HAS THREE RUNS, not two, and the middle one is what a streaming source needs:
 *
 *   [0, filled)          what has been PLAYED.  The node's own pair, or a step of the
 *                        gradient's ramp when one is live -- see sBarFillStyle.
 *   [filled, buffered)   what has ARRIVED but not yet been played.
 *   [buffered, bar_w)    nothing.
 *
 * The middle run is drawn in the fill glyph at IRCITUI_SLOT_ACCENT | A_DIM rather than in
 * the node's own pair, and that asymmetry is deliberate: the played run is the widget's
 * own colour, but "buffered" is a SEMANTIC band that should read the same on every bar --
 * the same accent, held back -- and not a second copy of whatever palette role the
 * template happened to give the node. On a bar whose own colour IS the accent (the
 * gallery's `bar_accent`) the two are the same hue and the difference is only the
 * dimming, which is the intended reading.
 *
 * DIM IS WHAT SEPARATES THEM, and it is the whole of the visual claim: same glyph, same
 * hue, lower intensity -- so the buffered run is legible as "the same thing, not yet
 * reached" rather than as a second value.
 *
 * The label is drawn with mvwaddch rather than through IrcitUiAddUtf8 because its characters are
 * ASCII by construction -- `%3d%%` produces digits, a space and a percent sign -- and
 * going through a mbstowcs for those would be work with no effect. The bar's own glyphs do
 * go through IrcitUiAddUtf8, because those are the ones a template chooses. */
static void
sBarPaint(IrcitUiNode *node_ptr)
{
  IrcitUiBarState *state_ptr;
  const char *     fill_glyph, *empty_glyph;
  chtype           empty_style, buffered_style;
  char             label[PRIV_CONFIG_DEFAULT_BAR_LABEL_W + 1];
  int              ix, iy, iw, ih, bar_w, filled, buffered, y, x, mid;
  int              label_len = 0, label_col = 0, adjacent = 0, centered = 0;
  int              value;

  if (!node_ptr || !node_ptr->common.win) return;

  state_ptr = sBarState(node_ptr);
  value     = state_ptr ? state_ptr->value : 0;
  sBarInner(node_ptr, &ix, &iy, &iw, &ih);
  bar_w  = sBarTrackW(node_ptr, iw);
  filled = sBarSplit(bar_w, value);
  mid    = ih / 2;

  buffered = sBarSplit(bar_w, state_ptr ? state_ptr->buffered : 0);
  if (buffered < filled) buffered = filled;

  sBarRampEnsure(state_ptr);

  fill_glyph  = sBarGlyphOf(state_ptr ? state_ptr->fill_glyph : NULL,
                            IrcitUiDeclBarFillGlyph(node_ptr));
  if (!fill_glyph) fill_glyph = sBarDefaultFill;
  empty_glyph = sBarGlyphOf(state_ptr ? state_ptr->empty_glyph : NULL,
                            IrcitUiDeclBarEmptyGlyph(node_ptr));
  if (!empty_glyph) empty_glyph = sBarDefaultEmpty;
  empty_style    = IrcitUiPaletteStyle(IRCITUI_SLOT_TEXT_MUTED) | A_DIM;
  buffered_style = IrcitUiPaletteStyle(IRCITUI_SLOT_ACCENT) | A_DIM;

  switch (sBarLabelModeOf(node_ptr)) {
  case IRCITUI_BAR_LABEL_ADJACENT:
    if (bar_w < iw) { adjacent = 1; label_col = ix + bar_w; }
    break;
  case IRCITUI_BAR_LABEL_CENTERED:
    if (bar_w > PRIV_CONFIG_DEFAULT_BAR_LABEL_W + 2) {
      centered  = 1;
      label_col = ix + (bar_w - PRIV_CONFIG_DEFAULT_BAR_LABEL_W) / 2;
    }
    break;
  default: break;
  }
  if (adjacent || centered) {
    sBarLabel(label, sizeof label, value);
    label_len = PRIV_CONFIG_DEFAULT_BAR_LABEL_W;
  }

  for (y = 0; y < ih; y++) {
    int middle = (y == mid);

    for (x = 0; x < bar_w; x++) {
      if (centered && middle && x >= (label_col - ix) && x < (label_col - ix) + label_len) {
        char   ch = label[x - (label_col - ix)];
        chtype st = (x < filled)   ? (sBarFillStyle(node_ptr, state_ptr, x, bar_w) | A_REVERSE)
                  : (x < buffered) ? (buffered_style | A_BOLD)
                                   : (empty_style | A_BOLD);
        mvwaddch(node_ptr->common.win, iy + y, ix + x, (chtype)(unsigned char)ch | st);
        continue;
      }
      if (x < filled) {
        chtype st = sBarFillStyle(node_ptr, state_ptr, x, bar_w);
        wattron(node_ptr->common.win, st);
        IrcitUiAddUtf8(node_ptr->common.win, iy + y, ix + x, fill_glyph, 1);
        wattroff(node_ptr->common.win, st);
      } else if (x < buffered) {
        // The buffered run: the FILL glyph, because it is data that has arrived -- the empty glyph would read as a gap, which is the opposite of what it means.
        wattron(node_ptr->common.win, buffered_style);
        IrcitUiAddUtf8(node_ptr->common.win, iy + y, ix + x, fill_glyph, 1);
        wattroff(node_ptr->common.win, buffered_style);
      } else {
        wattron(node_ptr->common.win, empty_style);
        IrcitUiAddUtf8(node_ptr->common.win, iy + y, ix + x, empty_glyph, 1);
        wattroff(node_ptr->common.win, empty_style);
      }
    }

    // The adjacent label, after the track because it is beside it rather than on it.
    if (adjacent && middle) {
      for (x = 0; x < label_len; x++)
        mvwaddch(node_ptr->common.win, iy + y, label_col + x,
                 (chtype)(unsigned char)label[x] | empty_style);
    }
  }
}

/* The frame, in the glyph profile the application asked for.
 *
 * STANDARD IS A CALL AND NOT A TABLE, and that is the whole reason this function exists
 * rather than a six-glyph row for the legacy box: `box(win, v, h)` draws the corners
 * ncurses picks for the terminal's own `acsc` capability, and it also handles the
 * degenerate widths a hand-written loop would have to special-case. Writing
 * ACS_ULCORNER into six strings would look equivalent and would not be, so the default
 * path stays exactly the call that shipped -- one branch, and byte-for-byte the same
 * cells as before this function existed.
 *
 * Every other profile is drawn glyph by glyph, through the same IrcitUiAddUtf8 the collapsible
 * indicator, the buttons and the emoji grid already use, so a profile corner is the same
 * kind of cell as every other non-ASCII glyph on the glass. dev/probes/border/
 * p_frame_chars.c is what says that is safe: it reads a hand-drawn profile back as its
 * codepoint, and reads box()'s corner back as the letter 'l'.
 *
 * The glyphs are drawn one cell at a time rather than as a run of waddwstr because the
 * call site has a per-cell attribute to honour later, and because a run would have to
 * decide what to do when a glyph is double-width -- a question a frame never has to
 * answer, since every profile is one column wide by construction. */
static void
sPaintFrame(WINDOW *win_ptr, int w, int h, IrcitUiBorderStyle style)
{
  const char *const *g_ptr = IrcitUiBorderGlyphs(style);
  int                x, y;

  if (!g_ptr || w < 2 || h < 2) {
    box(win_ptr, ACS_VLINE, ACS_HLINE);
    return;
  }

  IrcitUiAddUtf8(win_ptr, 0, 0, g_ptr[0], 1);
  for (x = 1; x < w - 1; x++) IrcitUiAddUtf8(win_ptr, 0, x, g_ptr[4], 1);
  IrcitUiAddUtf8(win_ptr, 0, w - 1, g_ptr[1], 1);

  for (y = 1; y < h - 1; y++) {
    IrcitUiAddUtf8(win_ptr, y, 0, g_ptr[5], 1);
    IrcitUiAddUtf8(win_ptr, y, w - 1, g_ptr[5], 1);
  }

  IrcitUiAddUtf8(win_ptr, h - 1, 0, g_ptr[2], 1);
  for (x = 1; x < w - 1; x++) IrcitUiAddUtf8(win_ptr, h - 1, x, g_ptr[4], 1);
  IrcitUiAddUtf8(win_ptr, h - 1, w - 1, g_ptr[3], 1);
}

/* One popup's drop shadow, stamped into its PARENT's cells.
 *
 * THE L, and why it is not a rectangle. A shadow that covered the popup's own rectangle
 * would be invisible -- the popup is pushed over it -- so the only cells worth writing are
 * the two strips the popup cannot cover: the column one past its right edge, and the row
 * one below its bottom. They meet at the outer corner, which the row writes. Each cell is
 * written exactly once, and the count is `h - 1 + w` for a `w x h` popup.
 *
 * It goes into the PARENT rather than into `stdscr`, and that is not a detail of style.
 * Nothing in this library ever refreshes `stdscr`: the draw pass paints per-node windows
 * and ends in wnoutrefresh, so a cell written to the screen window is overwritten by the
 * next refresh of whatever lies there -- ui_defaults_router.c records exactly that about a
 * listener that tried it, "visible as flicker, not as content". The parent is the window
 * whose storage the strip actually belongs to.
 *
 * A STACKED child is an independent newwin rather than a derwin, which is what makes this
 * work at all: the strip is the parent's storage alone, and the child's own buffer never
 * writes there. A derwin sibling DOES share that storage, which is why the caller stamps
 * this immediately before drawing the popup -- after every sibling that could erase it has
 * painted -- and pushes the parent again before the popup pushes itself.
 *
 * @return non-zero when any cell was written, so the caller knows a push is owed. A shadow
 *         entirely clipped away writes nothing and costs nothing. */
/* MEDIUM SHADE (U+2593), the glyph the strip falls back to where a cell holds nothing.
 * The shade rather than a solid block because a shadow is a soft edge in every other
 * toolkit, and the dim because it is meant to read as depth rather than as content -- the
 * two choices the section that asked for this made, and the only two it got right for free.
 *
 * File scope rather than a local of the painter, because sShadowCell decides per cell
 * whether to use it and is a function of its own. */
static const char *const sShade = "▓";

/* One cell of the shadow: keep the character and restyle it, or write the shade where
 * there is nothing to keep.
 *
 * THE HYBRID, AND WHY IT IS A HYBRID. Writing a glyph REPLACES a cell, so a shadow drawn
 * only as a shade destroys whatever it covers -- there is nothing left to see through.
 * Restyling instead leaves the character in place and changes only its attributes, which is
 * the closest a terminal gets to a translucent shadow: the content recedes but stays
 * legible. But restyling a BLANK cell would dim a space and produce no shadow at all, so
 * the shade is still the right answer where the cell is empty.
 *
 * The split was measured rather than assumed. dev/probes/border/p_shadow.c's `below` case
 * reports what the strip covers in the reference tree: 9 cells of content and 15 blank,
 * both cases present -- which is why the hybrid is worth writing rather than either half.
 *
 * AND THE MEASUREMENT ALSO FOUND THE TRAP: that content is not text, it is the tree's
 * FRAME LINES. In this terminal's encoding an ACS horizontal rule is the letter `q` and a
 * lower-left corner is `m`, distinguished from those letters only by A_ALTCHARSET. mvwchgat
 * sets a cell's attributes WHOLESALE, so restyling one of those cells without carrying the
 * bit over would turn every box-drawing character under a popup into the letter it is
 * encoded as -- a visible regression, and the same ACS trap DESIGN.md records for anyone
 * reading the glass back. Hence the `| (old_attrs & A_ALTCHARSET)` below.
 *
 * Reading the cell costs a mvwin_wch and a getcchar where the old code wrote blind. That is
 * two calls per cell over a strip a few dozen cells long, on a path that already walks the
 * whole parent window, and only while the shadow is on. */
static void
sShadowCell(WINDOW *win_ptr, int r, int c, attr_t attrs, short pair)
{
  cchar_t cc;
  wchar_t ws[8];
  attr_t  old_attrs = 0;
  short   old_pair  = 0;

  memset(&cc, 0, sizeof cc);
  memset(ws, 0, sizeof ws);
  if (mvwin_wch(win_ptr, r, c, &cc) != ERR
      && getcchar(&cc, ws, &old_attrs, &old_pair, NULL) != ERR
      && ws[0] != L' ') {
    mvwchgat(win_ptr, r, c, 1, attrs | (old_attrs & A_ALTCHARSET), pair, NULL);
    return;
  }
  IrcitUiAddUtf8(win_ptr, r, c, sShade, 1);
}

static int
sPaintShadow(const IrcitUiNode *parent_ptr, const IrcitUiNode *child_ptr)
{
  chtype                   style = IrcitUiPaletteStyle(IRCITUI_SLOT_TEXT_MUTED) | A_DIM;
  /* mvwchgat takes the attributes and the pair SEPARATELY, so the pair is unpacked here
     rather than passed inside a chtype. A_COLOR is masked out of the attributes because
     mvwchgat ORs what it is given -- handing it the colour bits as well would set the pair
     twice. */
  attr_t                   shadow_attrs = (attr_t)(style & A_ATTRIBUTES & ~(attr_t)A_COLOR);
  short                    shadow_pair  = (short)PAIR_NUMBER(style);
  int                      px = parent_ptr->common.x, py = parent_ptr->common.y;
  int                      pw = parent_ptr->common.w, ph = parent_ptr->common.h;
  int                      cx = child_ptr->common.x, cy = child_ptr->common.y;
  int                      cw = child_ptr->common.w, ch = child_ptr->common.h;
  int                      wrote = 0;
  int                      r, c;

  if (cw <= 0 || ch <= 0 || pw <= 0 || ph <= 0) return 0;

  wattron(parent_ptr->common.win, style);

  // The right-hand strip, from one row below the popup's top to its last row.
  c = cx + cw - px;
  if (c >= 0 && c < pw)
    for (r = cy + 1 - py; r <= cy + ch - 1 - py; r++) {
      if (r < 0 || r >= ph) continue;
      sShadowCell(parent_ptr->common.win, r, c, shadow_attrs, shadow_pair);
      wrote = 1;
    }

  // And the strip beneath it, which owns the outer corner.
  r = cy + ch - py;
  if (r >= 0 && r < ph)
    for (c = cx + 1 - px; c <= cx + cw - px; c++) {
      if (c < 0 || c >= pw) continue;
      sShadowCell(parent_ptr->common.win, r, c, shadow_attrs, shadow_pair);
      wrote = 1;
    }

  wattroff(parent_ptr->common.win, style);
  return wrote;
}

/* Painting a node erases the cells beneath it.
 *
 * A derwin child shares its parent's window storage -- the repo says as much
 * where it tears windows down -- so the parent's wbkgd and werase blank every
 * cell its descendants occupy. A child skipped because it still carried
 * IRCITUI_F_DRAWN would therefore be left blank, and blank for good: nothing
 * would clear the flag to bring it back.
 *
 * This is why the flag cascades downward, and it is the same rule
 * IrcitUiRealize applies to the windows it rebuilds. The node being painted is
 * not included: its own flag is set again at the end of this pass. */
static void
sMarkSubtreePaintDirty(IrcitUiNode *node_ptr)
{
  int i;
  for (i = 0; i < node_ptr->container.n_children; i++) {
    node_ptr->container.children[i]->common.flags &= ~IRCITUI_F_DRAWN;
    sMarkSubtreePaintDirty(node_ptr->container.children[i]);
  }
}


/* Axis-aligned overlap between two nodes' rectangles.
 *
 * No conversion is needed and none is done: common.x/y/w/h are absolute screen
 * cells. IrcitUiLayout writes them from the box the parent resolved, the hit
 * test compares them against absolute mouse coordinates, and IrcitUiRealize
 * subtracts the parent's origin only to derive the derwin offset -- the stored
 * values stay absolute throughout. */
static int
sNodesIntersect(const IrcitUiNode *a_ptr, const IrcitUiNode *b_ptr)
{
  return a_ptr->common.x < b_ptr->common.x + b_ptr->common.w && a_ptr->common.x + a_ptr->common.w > b_ptr->common.x &&
    a_ptr->common.y < b_ptr->common.y + b_ptr->common.h && a_ptr->common.y + a_ptr->common.h > b_ptr->common.y;
}

/* Clear IRCITUI_F_DRAWN on every LATER SIBLING of @p node_ptr whose cells overlap
 * its own, now that @p node_ptr has painted them.
 *
 * THIS IS OVERLAY, AND IT IS A REPAINT RATHER THAN A REFRESH. Two children of a
 * container placed at the same origin are both derwins of it, and a derwin SHARES
 * the parent's line storage: writing through one writes the cells the other reads.
 * That has a consequence the change set alone cannot cover. A node below repaints
 * those cells and is pushed; the node above is pushed too, because
 * sIntersectsChangeSet sees the overlap and marks it for refresh -- and pushing it
 * copies the SHARED cells, which now hold the node below. So the node above comes
 * out showing the node below, however many times it is refreshed. It wins only by
 * REWRITING its own text into those cells, which is what a repaint is.
 *
 * Measured, on the recycler's pinned header (SS15) with the row beneath it open for
 * editing: the composite over the two reads the row's text, and reads the header's
 * the moment the header is invalidated and one more frame runs. SS15's own note
 * says the opposite -- "a repaint below a clean node shows through it unless that
 * node is pushed again afterwards" -- and that is the claim this corrects. SS15's
 * probe could not see it: every frame it drove went through IrcitUiRealize, which
 * rebuilds every window and so clears DRAWN on the whole tree, so the header always
 * repainted. The gap opens on a frame that repaints a node WITHOUT a relayout,
 * which is why SS26 found it: the caret blink invalidates the field it is in and
 * nothing else.
 *
 * The walk goes backwards from the last child and stops at the node itself, so it
 * visits only the siblings drawn AFTER it -- the ones whose paint would undo this
 * one. A container whose children do not overlap pays one comparison each and
 * clears nothing. */
static void
sMarkOverlapSiblingsPaintDirty(const IrcitUiNode *node_ptr)
{
  const IrcitUiNode *parent_ptr = node_ptr->common.parent;
  int                i;

  if (!parent_ptr) return;
  for (i = parent_ptr->container.n_children - 1; i >= 0; i--) {
    IrcitUiNode *sibling_ptr = parent_ptr->container.children[i];
    if (sibling_ptr == node_ptr) break;
    if ((sibling_ptr->common.flags & IRCITUI_F_DRAWN) && sNodesIntersect(node_ptr, sibling_ptr)) {
      sibling_ptr->common.flags &= ~IRCITUI_F_DRAWN;
    }
  }
}

/* Does @p node_ptr share cells with anything that repainted this frame?
 *
 * This is the whole reason the change set exists. ncurses resolves stacking by
 * refresh order and has no per-node z-index, so a node repainted lower in the
 * order shows through a clean node above it unless that node is pushed again
 * afterwards. Without this test, gating the refresh would let a repaint below a
 * popup paint straight over it.
 *
 * The test is against the set rather than against one origin, because a frame
 * can have several: typing clears paint on the field and on the status echo, and
 * a blink on a third. */
static int
sIntersectsChangeSet(const IrcitUiNode *node_ptr, const IrcitUiDirtyList *list_ptr)
{
  size_t i;
  for (i = 0; i < list_ptr->count; i++) {
    if (list_ptr->entries[i] == node_ptr) continue; /* its own repaint is the paint path's business */
    if (sNodesIntersect(node_ptr, list_ptr->entries[i])) return 1;
  }
  return 0;
}

/* One toggle's mark: an ASCII frame, a glyph between the frame, then the caption.
 *
 * The glyph goes through mvwaddnwstr and not mvwprintw, because it is outside
 * ASCII. mvwprintw("%s", "<those bytes>") writes three BYTES and curses advances
 * the cursor three cells for them, so everything to the right of the mark is
 * displaced by two columns. It is the same trap the focus separator avoids, in
 * the same way.
 *
 * @p mark_w is the mark's width in CELLS, stated by the caller rather than
 * measured, so the caption's column does not depend on what a terminal thinks the
 * glyph's width is. Every mark passed here is one cell except the switch's, which
 * is two; either way both states of a toggle state the SAME width, which is what
 * keeps the caption still. It is also what fixes the switch: its old
 * "[on ]"/"[off]" pair is four cells and five, so its caption moved a column
 * every time it was toggled.
 *
 * One helper for BOTH paths that draw a toggle -- the per-kind case below and the
 * radio/check group's own row loop -- because a group paints its children itself
 * and a second copy of these marks is a second set to keep in step. §4.4's rule
 * about two implementations that drift applies to a glyph as much as to a
 * callback.
 *
 * @p open_ch of 0 means the mark carries NO frame, and the caption follows it
 * directly. The radio uses that: a filled or hollow circle already reads as a
 * choice, and parentheses around it are noise. The check box keeps its brackets,
 * because the brackets ARE the box -- an empty box has to look like an empty box
 * and a bare space does not.
 *
 * Note the consequence for a panel holding both: a framed row's caption starts at
 * column 4 and an unframed one at column 2, so the two blocks' captions do not
 * line up with each other. Within a block they do, which is the invariant that
 * matters.
 *
 * @p mark_pair is the colour pair the mark is drawn in, resolved by the caller
 * from C20's mark_colour: a node that never set one resolves to its own fg, which
 * is the colour the mark had before the key existed. The pair is applied around
 * the mark's cells only, so the caption keeps the window's own pair. */
static void
sPaintToggleMark(WINDOW *win_ptr, int row, char open_ch, char close_ch, const wchar_t *mark_ptr, int mark_w,
                 const char *text_ptr, short mark_pair)
{
  if (mark_pair > 0) wattron(win_ptr, COLOR_PAIR(mark_pair));
  if (!open_ch) {
    mvwaddnwstr(win_ptr, row, 0, mark_ptr, mark_w);
    if (mark_pair > 0) wattroff(win_ptr, COLOR_PAIR(mark_pair));
    mvwprintw(win_ptr, row, mark_w, " %s", text_ptr ? text_ptr : "");
    return;
  }
  mvwprintw(win_ptr, row, 0, "%c", open_ch);
  mvwaddnwstr(win_ptr, row, 1, mark_ptr, mark_w);
  if (mark_pair > 0) wattroff(win_ptr, COLOR_PAIR(mark_pair));
  mvwprintw(win_ptr, row, 1 + mark_w, "%c %s", close_ch, text_ptr ? text_ptr : "");
}

static short
sToggleMarkPair(const IrcitUiNode *node_ptr)
{
  return IrcitUiColourPair(sMarkFgOf(node_ptr) >= 0 ? sMarkFgOf(node_ptr) : node_ptr->common.fg,
                 node_ptr->common.bg);
}

/* The palette slot a node wears, or -1 when it wears none.
 *
 * The class is the name a layout spelled, and the palette owns the vocabulary that
 * gives it meaning -- so the name is resolved here, per paint, rather than a slot
 * number being stored on the node. A node whose class resolves to nothing keeps its
 * own colours: a -1 is a layout that C23 did not let through, or a class set from C,
 * and either way leaving the node alone is the answer that draws something. */
static int
sNodeSlot(const IrcitUiNode *node_ptr)
{
  return node_ptr->common.theme_class ? IrcitUiPaletteSlotForName(node_ptr->common.theme_class) : -1;
}

/* Whether this node's text should ride the canopy.
 *
 * Two conditions, and both are needed: the effect must be up (a canopy that holds its
 * registers), and the node must be wearing the text ROLE -- a widget whose colours are its
 * own was asked for that way and is not overridden. */
/* Whether a node sits inside a container wearing the `surface` role.
 *
 * This is what makes the canopy a BACKDROP rather than a rectangle: a container is the
 * thing that declares the role, and everything drawn inside it is on that surface, so the
 * ramp has to reach the leaves or the backdrop shows only in the container's own border
 * and gaps. The walk is bounded by the tree's depth and runs only while the effect is on.
 */
static int
sInsideSurface(const IrcitUiNode *node_ptr)
{
  const IrcitUiNode *p;

  for (p = node_ptr->common.parent; p; p = p->common.parent)
    if (sNodeSlot(p) == IRCITUI_SLOT_SURFACE) return 1;
  return 0;
}

/* Whether the canopy paints this node.
 *
 * Three cases. A node wearing `surface` IS the backdrop. A node wearing `text` is written
 * on it, and takes the blended foreground rather than the background. Anything else inside
 * a surface container is on the backdrop too and is filled with it.
 *
 * What that costs, stated because it is a real deviation: with the canopy enabled, a node
 * inside a surface container is drawn on the ramp even if the layout gave it a colour of
 * its own. Telling "the layout chose this colour" from "the layout said nothing and got the
 * default" would need a marker on the node, the way C20's mark_fg needs -1 -- 0 is a legal
 * colour, so the generator's default cannot double as "unset". That marker is the next
 * step if this override ever bites; today the effect is opt-in and off, so it bites only a
 * layout that asked for it.
 */
static int
sNodeCanopyOn(const IrcitUiNode *node_ptr)
{
  return IrcitUiPaletteCanopyActive() && (sNodeSlot(node_ptr) == IRCITUI_SLOT_TEXT || (sNodeSlot(node_ptr) < 0 && sInsideSurface(node_ptr)));
}

/* Fill a node's window with the canopy's per-row backgrounds.
 *
 * One mvwhline per row rather than one per cell: a row's style is a single chtype, so the
 * whole row goes down in one call. The absolute screen row, not the local one, is the
 * ramp's index -- the backdrop is a property of the screen, and a node scrolled down the
 * layout must not carry its own copy of the gradient with it. */
static void
sPaintCanopyRows(IrcitUiNode *node_ptr)
{
  int r;
  int h = node_ptr->common.h;


  for (r = 0; r < h; r++)
    mvwhline(node_ptr->common.win, r, 0, (chtype)' ' | IrcitUiPaletteCanopyStyle(node_ptr->common.y + r), node_ptr->common.w);
}

PUBLIC_API void
IrcitUiDraw(IrcitUiNode *node_ptr, const IrcitUiDirtyList *dirty_list_ptr)
{
  int         must_paint, must_refresh;
  int         i,          iy;
  chtype      p;   /* a full style -- attributes and pair -- not a pair index */
  const char *s;
  char        tmp[PRIV_CONFIG_DEFAULT_DRAW_SCRATCH_SZ];

  if (!node_ptr || !node_ptr->common.win || !(node_ptr->common.flags & IRCITUI_F_VISIBLE) || (node_ptr->common.flags &
    IRCITUI_F_CLIPPED)) return;

  must_paint = !(node_ptr->common.flags & IRCITUI_F_DRAWN);

  must_refresh = must_paint || (dirty_list_ptr && (dirty_list_ptr->overflow || sIntersectsChangeSet(
    node_ptr, dirty_list_ptr)));

  if (!must_paint) goto refresh;

  sMarkSubtreePaintDirty(node_ptr);
  sMarkOverlapSiblingsPaintDirty(node_ptr);
  p = sNodeSlot(node_ptr) >= 0 ? IrcitUiPaletteStyle((IrcitUiPaletteSlot)sNodeSlot(node_ptr))
                               : COLOR_PAIR(IrcitUiColourPair(node_ptr->common.fg, node_ptr->common.bg));
  // sRowForcedAttr is what carries a selected row's highlight: wbkgd ORs the background's attributes into every non-blank character written afterwards, so a reversed background reverses the row's text and its labels' text alike.
  wbkgd(node_ptr->common.win, p | sRowForcedAttr(node_ptr) | ((node_ptr->common.flags & IRCITUI_F_FOCUSED)
    ? A_BOLD : 0));
  werase(node_ptr->common.win);

  /* The canopy is painted after the erase and before the kind painter, so the widget's own glyphs and text land on the backdrop rather than under it. Only the two roles that
     compose a backdrop take part: `surface` is the backdrop itself, `text` is what is
     written on it. */
  if (IrcitUiPaletteCanopyActive()) {
    int slot = sNodeSlot(node_ptr);
    if (slot == IRCITUI_SLOT_SURFACE || sNodeCanopyOn(node_ptr)) sPaintCanopyRows(node_ptr);
  }

  // Folded containers are not framed. A one-cell strip has no room for a box, and the two rows it would need are exactly the chrome the fold drops - the id is not painted either, for the same reason.
  if ((node_ptr->common.flags & IRCITUI_F_BORDER) && IrcitUiIsContainer(node_ptr->common.kind) && !
    sIsCollapsed(node_ptr)) {
    wattron(node_ptr->common.win, COLOR_PAIR(IrcitUiColourPair(node_ptr->common.bdr, node_ptr->common.bg)));
    sPaintFrame(node_ptr->common.win, node_ptr->common.w, node_ptr->common.h, IrcitUiBorderStyleGet());
    if (node_ptr->common.id && node_ptr->common.id[0]) {
      if (!node_ptr->common.parent) mvwprintw(node_ptr->common.win, 0, 2, " %s  %s ", node_ptr->common.id,
                                              IrcitUiLibVersion());
      else mvwprintw(node_ptr->common.win, 0, 2, " %s ", node_ptr->common.id);
    }
    wattroff(node_ptr->common.win, COLOR_PAIR(IrcitUiColourPair(node_ptr->common.bdr, node_ptr->common.bg)));
  }

  if (node_ptr->common.flags & IRCITUI_F_COLLAPSIBLE) {
    int icol = sIndicatorCol(node_ptr);
    if (sIsCollapsed(node_ptr)) sPaintGlyphArea(node_ptr->common.win, node_ptr->common.w, node_ptr->common.h,
                                                sFillGlyph(node_ptr));
    IrcitUiAddUtf8(node_ptr->common.win, 0, icol, sIndicatorGlyph(node_ptr, sIsCollapsed(node_ptr)), 1);

    if (!sIsCollapsed(node_ptr) && (node_ptr->common.flags & IRCITUI_F_BORDER) && node_ptr->common.w >= 3)
      mvwaddch(node_ptr->common.win, 0, icol ? icol - 1 : icol + 1, ' ');
  }

  if ((node_ptr->common.flags & IRCITUI_F_SCROLLABLE) && node_ptr->common.h > 2 && node_ptr->container.scroll_y + 1
    /* overflow? */ && !sIsCollapsed(node_ptr)) {
    int         overflow = 0;
    const char *ts       = node_ptr->common.interaction_defaults.user ?
                             (char*)node_ptr->common.interaction_defaults.user :
                             node_ptr->common.text;
    if (IrcitUiIsContainer(node_ptr->common.kind)) overflow = node_ptr->container.columns;
      // set in linear layout as extra rows
    else overflow                                           = sLineCount(ts) - node_ptr->common.h;
    if (overflow > 0 && node_ptr->common.h > 2) {
      int track = node_ptr->common.h - ((node_ptr->common.flags & IRCITUI_F_BORDER) ? 2 : 0);
      int maxs  = overflow;
      int thumb = track * track / (track + maxs);
      int pos;
      if (thumb < 1) thumb = 1;
      if (thumb > track) thumb = track;
      pos = (node_ptr->common.flags & IRCITUI_F_BORDER) ? 1 : 0;
      pos += (track - thumb) * node_ptr->container.scroll_y / maxs;

      {
        int    bar_col = node_ptr->common.w - 1;
        int    base    = (node_ptr->common.flags & IRCITUI_F_BORDER) ? 1 : 0;
        int    y;
        chtype pair    = (chtype)COLOR_PAIR(IrcitUiColourPair(COLOR_GREEN, COLOR_BLACK));

        for (y = 0; y < track; y++)
          mvwaddch(node_ptr->common.win, base + y, bar_col, ACS_VLINE | A_ALTCHARSET | A_DIM);
        // A BLANK CELL IN REVERSE: black on the pair's colour, a solid block rather than a texture. It was a checkerboard only because a blank cell carrying a pair used to be invisible to the screen dump; the dump now reports one.
        for (y = 0; y < thumb; y++)
          mvwaddch(node_ptr->common.win, pos + y, bar_col, (chtype)' ' | A_REVERSE | pair);
      }
    }
  }

  s = node_ptr->common.interaction_defaults.user ?
        (char*)node_ptr->common.interaction_defaults.user :
        node_ptr->common.text;
  iy = node_ptr->common.padding[0];
  if (iy >= node_ptr->common.h) iy = 0;

  switch (node_ptr->common.kind) {
  case IRCITUI_TEXT_VIEW:
    sPaintSelText(node_ptr->common.win, node_ptr->common.padding[3],
                  (node_ptr->common.flags & IRCITUI_F_BORDER) ? 1 : 0,
                  node_ptr->common.w - ((node_ptr->common.flags & IRCITUI_F_BORDER) ? 2 : 0),
                  node_ptr->common.h - ((node_ptr->common.flags & IRCITUI_F_BORDER) ? 2 : 0), s ? s : "",
                  node_ptr->container.sel0, node_ptr->container.sel1, node_ptr->container.scroll_y,
                  sNodeCanopyOn(node_ptr) ? node_ptr->common.y : -1);
    break;
  case IRCITUI_BUTTON: mvwaddstr(node_ptr->common.win, 0, 0, "[ ");
    IrcitUiAddUtf8(node_ptr->common.win, 0, 2, s ? s : "ok", node_ptr->common.w);
    waddstr(node_ptr->common.win, " ]");
    // mvwchgat takes the pair NUMBER, not the chtype: passing p landed the row on pair 0, whose colours hid the label.
    if (sMenuRowIsCursor(node_ptr))
      mvwchgat(node_ptr->common.win, 0, 0, node_ptr->common.w, A_REVERSE, (short)PAIR_NUMBER(p), NULL);
    break;
  case IRCITUI_EDIT_TEXT: IrcitUiEditTextPaint(node_ptr);
    break;
  case IRCITUI_CHECK_BOX:
    // U+2714 HEAVY CHECK MARK. The unchecked side stays a blank cell rather than taking a glyph of its own: the brackets are the box, and an empty box is what an empty box looks like.
    sPaintToggleMark(node_ptr->common.win, 0, '[', ']',
                     (node_ptr->common.flags & IRCITUI_F_CHECKED) ? L"✔" : L" ", 1, s,
                     sToggleMarkPair(node_ptr));
    break;
  case IRCITUI_RADIO_BUTTON:
    // U+25C9 FISHEYE and U+25CB WHITE CIRCLE: a radio is round, and a filled round mark against a hollow one is the whole distinction. No frame -- see sPaintToggleMark.
    sPaintToggleMark(node_ptr->common.win, 0, 0, 0,
                     (node_ptr->common.flags & IRCITUI_F_CHECKED) ? L"◉" : L"○", 1, s,
                     sToggleMarkPair(node_ptr));
    break;
  case IRCITUI_SWITCH:
    // U+25CF BLACK CIRCLE, and the KNOB's POSITION carries the state, which is what a switch is: right is on. Two cells either way, so the caption does not move -- which the [on ]/[off] pair it replaces did, by one column.
    sPaintToggleMark(node_ptr->common.win, 0, '[', ']',
                     (node_ptr->common.flags & IRCITUI_F_CHECKED) ? L" ●" : L"● ", 2, s,
                     sToggleMarkPair(node_ptr));
    break;
  case IRCITUI_PROGRESS_BAR:
  case IRCITUI_SEEK_BAR: sBarPaint(node_ptr);
    break;
  case IRCITUI_IMAGE_VIEW:
    IrcitUiImageDraw(node_ptr);
    break;
  case IRCITUI_RADIO_GROUP:
  case IRCITUI_CHECK_GROUP: {
    int   i;
    short group_pair = sToggleMarkPair(node_ptr);
    for (i = 0; i < node_ptr->container.n_children && i < node_ptr->common.h; i++) {
      IrcitUiNode *c  = node_ptr->container.children[i];
      const char *tx = c->common.text ? c->common.text : "";
      int         on = !!(c->common.flags & IRCITUI_F_CHECKED);

      if (node_ptr->common.kind == IRCITUI_RADIO_GROUP)
        sPaintToggleMark(node_ptr->common.win, i, 0, 0, on ? L"◉" : L"○", 1, tx, group_pair);
      else
        sPaintToggleMark(node_ptr->common.win, i, '[', ']', on ? L"✔" : L" ", 1, tx, group_pair);
    }
    break;
  }
  case IRCITUI_EMOJI_TABLE: {
    const char *p    = node_ptr->common.text ? node_ptr->common.text : "";
    int         cols = node_ptr->container.columns > 0 ? node_ptr->container.columns : PRIV_CONFIG_DEFAULT_GRID_COLS;
    int         col  = 0, row = 0, idx = 0;
    mbstate_t   st;
    memset(&st, 0, sizeof st);
    while (*p) {
      wchar_t wc;
      char    tmp[PRIV_CONFIG_DEFAULT_UTF8_MAX_BYTES];
      size_t  k = mbrtowc(&wc, p, PRIV_CONFIG_DEFAULT_UTF8_MAX_BYTES, &st);
      int     ww;
      if (k == (size_t)-1 || k == 0) break;
      ww = wcwidth(wc);
      p  += k;
      if (ww <= 0) continue;
      memcpy(tmp, p - k, k);
      tmp[k] = 0;

      if (idx == node_ptr->container.sel0)
        wattron(node_ptr->common.win, A_REVERSE);
      IrcitUiAddUtf8(node_ptr->common.win, row, col * 2, tmp, 4);
      if (idx == node_ptr->container.sel0)
        wattroff(node_ptr->common.win, A_REVERSE);
      idx++;
      col++;
      if (col >= cols) {
        col = 0;
        row++;
      }
    }
    break;
  }
  case IRCITUI_CONVERSATION_WINDOW:
  case IRCITUI_MESSAGE_VIEWPORT:
      IrcitUiConversationWindowPaint(node_ptr);
    break;
  default:
    break;
  }
  node_ptr->common.flags |= IRCITUI_F_DRAWN;
  (void)tmp;
refresh:
  // Staging no longer trusts newscr to remember the frame: wnoutrefresh copies only lines
  // a window has marked changed, so every node is touched first. must_refresh is vestigial.
  (void)must_refresh;
  touchwin(node_ptr->common.win);
  wnoutrefresh(node_ptr->common.win);
  // Below the refresh: label rather than above it, so a node whose own paint is current still recurses. That is what a container's children need, and a recycler's rows need it for the same reason -- a row's readiness changes independently of its container's.
  for (i = 0; i < node_ptr->container.n_children; i++) {
    IrcitUiNode *c_ptr = node_ptr->container.children[i];

    if (IrcitUiShadow() && sIsStacked(c_ptr) && sPaintShadow(node_ptr, c_ptr) && node_ptr->common.win)
      wnoutrefresh(node_ptr->common.win);

    IrcitUiDraw(c_ptr, dirty_list_ptr);
  }
}

/* One frame: measure if the geometry moved, paint what is stale, then leave the
 * flush to the caller.
 *
 * The sequence is deliberately the one every caller performs by hand today, so
 * that adopting it changes nothing about what reaches the glass. What it adds is
 * the single place the frame's bookkeeping is reset, and the guarantee that the
 * change set a draw pass reads belongs to that pass alone.
 *
 * It does NOT flush. Staging newscr is this function's whole job; pushing it to
 * the terminal is device I/O and belongs to whoever owns the terminal, which is
 * the loop. Keeping the two apart is also what lets this be driven with no
 * terminal attached at all.  */
PUBLIC_API void
IrcitUiRenderFrame(void)
{
  if (IrcitUiPaletteEffects() & IRCITUI_PALETTE_EFFECT_CANOPY) IrcitUiPaletteCanopyEnsure(LINES);

  IrcitUiNode *     root_ptr = IrcitUiScreen.root;
  IrcitUiDirtyList *list_ptr = IrcitUiFrameDirtyList();

  if (!root_ptr) {
    list_ptr->count    = 0;
    list_ptr->overflow = 0;
    return;
  }

  if (root_ptr->common.is_dirty || !root_ptr->common.win) {
    IrcitUiLayout(root_ptr, 0, 0, COLS, LINES);
    IrcitUiRealize(root_ptr);
  }

  IrcitUiDraw(root_ptr, list_ptr);

  root_ptr->common.is_dirty = 0;
  list_ptr->count           = 0;
  list_ptr->overflow        = 0;
}

PUBLIC_API void
IrcitUiDeflate(IrcitUiNode *node_ptr)
{
  int i;
  for (i = node_ptr->container.n_children - 1; i >= 0; i--) IrcitUiDeflate(node_ptr->container.children[i]);
  if (node_ptr->common.win) {
    delwin(node_ptr->common.win);
    node_ptr->common.win = NULL;
  }
}

PUBLIC_API IrcitUiNode *
IrcitUiFind(IrcitUiNode *node_ptr, const char *id_ptr)
{
  int          i;
  IrcitUiNode *f;
  if (node_ptr->common.id && strcmp(node_ptr->common.id, id_ptr) == 0) return node_ptr;
  for (i = 0; i < node_ptr->container.n_children; i++) if ((f = IrcitUiFind(node_ptr->container.children[i], id_ptr)))
    return f;
  return NULL;
}

/* Walk a live tree for the node cloned from `target_ptr`.

   Identity, not text: every node IrcitUiClone produces records the model node it
   came from, so the clone of a known model node is found without comparing a
   single string. Pre-order and first match, which is the order IrcitUiFind uses
   and therefore the node it would have returned - two popups of the same
   template share their model nodes, so the first one wins, not the topmost, and
   the two functions must not disagree about which that is.

   `target_ptr` must not be NULL: a run-time node carries no reference, so every
   such node would compare equal to a NULL target. */
static IrcitUiNode *
sFindByRef(IrcitUiNode *node_ptr, const IrcitUiNode *target_ptr)
{
  int          i;
  IrcitUiNode *f;
  if (node_ptr->common.ref == target_ptr) return node_ptr;
  for (i = 0; i < node_ptr->container.n_children; i++) if ((f =
    sFindByRef(node_ptr->container.children[i], target_ptr))) return f;
  return NULL;
}

PUBLIC_API IrcitUiNode *
IrcitUiFindModel(const char *id_ptr)
{
  return (IrcitUiNode*)sLookupRef(id_ptr);
}

PUBLIC_API IrcitUiNode *
IrcitUiFindScreen(const char *id_ptr)
{
  IrcitUiNode *model_ptr;

  if (!IrcitUiScreen.root) return NULL;
  model_ptr = IrcitUiFindModel(id_ptr);
  if (model_ptr) return sFindByRef(IrcitUiScreen.root, model_ptr);
  // Not in the model: a node created at run time. It has no model entry to match on, so this is the one case that still searches by text.
  return IrcitUiFind(IrcitUiScreen.root, id_ptr);
}

PUBLIC_API IrcitUiNodePair
IrcitUiFindBoth(const char *id_ptr)
{
  IrcitUiNodePair pair;

  pair.model = IrcitUiFindModel(id_ptr);
  if (!IrcitUiScreen.root) {
    pair.screen = NULL;
    return pair;
  }
  pair.screen = pair.model ? sFindByRef(IrcitUiScreen.root, pair.model) : IrcitUiFind(IrcitUiScreen.root, id_ptr);
  return pair;
}

PUBLIC_API const IrcitUiNode *
IrcitUiModelOf(const IrcitUiNode *node_ptr)
{
  return node_ptr ? node_ptr->common.ref : NULL;
}

/* The read accessors. Each is a field read behind a null test: the point is not
   the work, it is that the field is reachable from an installed header at all.
   A callback that cannot ask a node how wide it is cannot lay itself out, and a
   listener that cannot read an event's key code cannot act on one -- DESIGN.md
   SS3.8, G25, G2, G35. */
PUBLIC_API IrcitUiKind
IrcitUiNodeGetKind(const IrcitUiNode *node_ptr)
{
  return node_ptr ? node_ptr->common.kind : IRCITUI_TEXT_VIEW;
}

PUBLIC_API const char *
IrcitUiNodeGetId(const IrcitUiNode *node_ptr)
{
  return node_ptr ? node_ptr->common.id : NULL;
}

PUBLIC_API const char *
IrcitUiNodeGetText(const IrcitUiNode *node_ptr)
{
  return node_ptr ? node_ptr->common.text : NULL;
}

PUBLIC_API void
IrcitUiNodeGetGeometry(const IrcitUiNode *node_ptr, int *x_ptr, int *y_ptr, int *w_ptr, int *h_ptr)
{
  if (!node_ptr) return;
  if (x_ptr) *x_ptr = node_ptr->common.x;
  if (y_ptr) *y_ptr = node_ptr->common.y;
  if (w_ptr) *w_ptr = node_ptr->common.w;
  if (h_ptr) *h_ptr = node_ptr->common.h;
}

PUBLIC_API unsigned
IrcitUiNodeGetFlags(const IrcitUiNode *node_ptr)
{
  return node_ptr ? node_ptr->common.flags : 0u;
}

PUBLIC_API int
IrcitUiEventGetKey(const IrcitUiEvent *ev_ptr)
{
  return ev_ptr ? ev_ptr->key_code : 0;
}

PUBLIC_API void
IrcitUiEventGetMouseCoords(const IrcitUiEvent *ev_ptr, int *x_ptr, int *y_ptr)
{
  if (!ev_ptr) return;
  if (x_ptr) *x_ptr = ev_ptr->mouse_x;
  if (y_ptr) *y_ptr = ev_ptr->mouse_y;
}

PUBLIC_API void
IrcitUiSetCollapsed(IrcitUiNode *node_ptr, int collapsed)
{
  if (!node_ptr || !(node_ptr->common.flags & IRCITUI_F_COLLAPSIBLE)) return;
  if (node_ptr->container.is_collapsed == (collapsed != 0)) return;
  node_ptr->container.is_collapsed = (collapsed != 0);

  IrcitUiRequestLayout(node_ptr);
}

PUBLIC_API int
IrcitUiIsCollapsed(const IrcitUiNode *node_ptr)
{
  if (!node_ptr || !(node_ptr->common.flags & IRCITUI_F_COLLAPSIBLE)) return 0;
  return node_ptr->container.is_collapsed ? 1 : 0;
}

PUBLIC_API void
IrcitUiMouseInit(void)
{
  mmask_t o;

  mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, &o);
  mouseinterval(0);
  fputs("\033[?1000h\033[?1002h\033[?1003h\033[?1006h", stdout);
  fflush(stdout);
}

PUBLIC_API IrcitUiNode *
IrcitUiHitTest(IrcitUiNode *node_ptr, int y, int x)
{
  int          i;
  IrcitUiNode *h;
  if (!node_ptr || !(node_ptr->common.flags & IRCITUI_F_VISIBLE) || (node_ptr->common.flags & IRCITUI_F_CLIPPED)) return
    NULL;
  if (y < node_ptr->common.y || x < node_ptr->common.x || y >= node_ptr->common.y + node_ptr->common.h || x >= node_ptr
    ->common.x + node_ptr->common.w)
    return NULL;
  for (i = node_ptr->container.n_children - 1; i >= 0; i--) if ((h = IrcitUiHitTest(
    node_ptr->container.children[i], y, x))) return h;
  return node_ptr;
}

static int
sGroupRowAt(IrcitUiNode *group_ptr, int screen_y)
{
  int row;
  if (!group_ptr || group_ptr->container.n_children < 1) return -1;
  row = screen_y - group_ptr->common.y;
  if (row < 0) row = 0;
  if (row >= group_ptr->container.n_children) row = group_ptr->container.n_children - 1;
  return row;
}

static void
sGroupApplyRow(IrcitUiNode *group_ptr, int row)
{
  int i;
  if (!group_ptr || row < 0 || row >= group_ptr->container.n_children) return;
  group_ptr->container.sel0 = row;
  if (group_ptr->common.kind == IRCITUI_RADIO_GROUP) {
    for (i = 0; i < group_ptr->container.n_children; i++) group_ptr->container.children[i]->common.flags &= ~
      IRCITUI_F_CHECKED;
    group_ptr->container.children[row]->common.flags |= IRCITUI_F_CHECKED;
  }
  else if (group_ptr->common.kind == IRCITUI_CHECK_GROUP) group_ptr->container.children[row]->common.flags ^=
    IRCITUI_F_CHECKED;
  // The group is what is invalidated, not the row: the rows are derwin children and repainting the group is what reaches them. Only the cursor and the ticks moved, so no box changed.
  IrcitUiInvalidate(group_ptr);
}

/* The cursor of a IRCITUI_F_CHILDREN_SELECTABLE container walks its children but
   skips any that is not selectable, so a title, a separator or a plain label is
   never highlighted. "Selectable" is IrcitUiIsFocusable, the predicate that already
   decides what the mouse and the focus system treat as reachable - so a menu and
   the keyboard agree on what counts as an item without a second notion of it. */
static int
sMenuSelectable(const IrcitUiNode *child_ptr)
{
  return IrcitUiIsFocusable(child_ptr);
}

/* First selectable child at or after from, or -1. Forward and backward are
   separate because the search direction is the whole difference. */
static int
sMenuRowFwd(IrcitUiNode *menu_ptr, int from)
{
  int i;
  for (i = from; i < menu_ptr->container.n_children; i++) if (sMenuSelectable(menu_ptr->container.children[i])) return
    i;
  return -1;
}

static int
sMenuRowBack(IrcitUiNode *menu_ptr, int from)
{
  int i;
  for (i = from; i >= 0; i--) if (sMenuSelectable(menu_ptr->container.children[i])) return i;
  return -1;
}

/* Put sel0 on a selectable child. Called before any read of sel0, so a freshly
   cloned menu - whose sel0 is zero by positional initialization and whose child
   0 may be a title - still has its cursor on a real item. This is what makes
   "the first item is highlighted on open" true rather than "child 0 is". */
static void
sMenuRepairRow(IrcitUiNode *menu_ptr)
{
  if (menu_ptr->container.n_children < 1) {
    menu_ptr->container.sel0 = -1;
    return;
  }
  if (menu_ptr->container.sel0 >= 0 && menu_ptr->container.sel0 < menu_ptr->container.n_children && sMenuSelectable(
    menu_ptr->container.children[menu_ptr->container.sel0])) return;
  menu_ptr->container.sel0 = sMenuRowFwd(menu_ptr, 0);
}

/* Take a menu's layer away, by identity rather than by z-order.

   IrcitUiCloseTopPopup pops IrcitUiTopLayer(), which is no longer this menu once
   anything is stacked over it, so the layer is re-resolved from its id instead.
   IrcitUiFind returns NULL for a layer that is already gone, which is the case when
   the item's own handler popped it - so an item that dismissed its own layer is
   never popped twice. This relies on at most one layer per id, which the three
   popup call sites already enforce by replacing an existing layer rather than
   stacking a second one.

   Takes the ID and not the node on purpose: callers call this after a handler
   that may have freed the node (IrcitUiPopStacked:1810-1811 frees the popup root).
   The id survives that because IrcitUiClone is a shallow copy (:1610), leaving it
   pointing at the generated table's string literal, which outlives every clone.
   IrcitUiFocusTop afterwards re-resolves focus into whatever is on top now. */
static void
sMenuDismiss(const char *mid_ptr)
{
  IrcitUiNode *again = mid_ptr ? IrcitUiFind(IrcitUiScreen.root, mid_ptr) : NULL;
  if (again && (again->common.flags & IRCITUI_F_STACKED)) IrcitUiPopStacked(again);
  IrcitUiFocusTop();
}

/* Enter on a menu row: run the item, then take exactly this layer away.

   The id is read BEFORE the dispatch, while the node is certainly alive, because
   the item's handler may already have popped the menu and freed it. */
static void
sMenuActivate(IrcitUiNode *menu_ptr, int key)
{
  IrcitUiNode *c;
  const char * mid;

  if (!menu_ptr || menu_ptr->container.sel0 < 0 || menu_ptr->container.sel0 >= menu_ptr->container.n_children) return;
  c = menu_ptr->container.children[menu_ptr->container.sel0];
  if (!c || !(c->common.flags & IRCITUI_F_ENABLED)) return;
  mid = menu_ptr->common.id;
  IrcitUiDispatcherPostKey(IrcitUiDispatcherInstance(), c, key);
  sMenuDismiss(mid);
}

/* The emoji grid's cursor is a linear cell index into n->common.text, which is how the
   painter already walks it. Counting has to agree with the painter exactly, so
   this repeats its rule rather than inventing one: mbrtowc for the grapheme and
   wcwidth <= 0 to skip a combining mark or a control instead of giving it a
   cell. If the two ever disagreed, the cursor would name a different cell than
   the one that gets drawn. */
static int
sEmojiCells(const IrcitUiNode *node_ptr)
{
  const char *p = node_ptr->common.text ? node_ptr->common.text : "";
  mbstate_t   st;
  int         cells = 0;

  memset(&st, 0, sizeof st);
  while (*p) {
    wchar_t wc;
    size_t  k = mbrtowc(&wc, p, PRIV_CONFIG_DEFAULT_UTF8_MAX_BYTES, &st);
    if (k == (size_t)-1 || k == 0) break;
    p += k;
    if (wcwidth(wc) <= 0) continue;
    cells++;
  }
  return cells;
}

/* The index-th cell's grapheme as UTF-8, written into out. Returns 0 when there
   is no such cell, so a caller cannot insert a stale or an empty string. */
static int
sEmojiCellText(const IrcitUiNode *node_ptr, int index, char *out_ptr, size_t outsz)
{
  const char *p = node_ptr->common.text ? node_ptr->common.text : "";
  mbstate_t   st;

  memset(&st, 0, sizeof st);
  while (*p) {
    wchar_t wc;
    size_t  k = mbrtowc(&wc, p, PRIV_CONFIG_DEFAULT_UTF8_MAX_BYTES, &st);
    if (k == (size_t)-1 || k == 0) break;
    if (wcwidth(wc) > 0) {
      if (index == 0) {
        if (k + 1 > outsz) return 0;
        memcpy(out_ptr, p, k);
        out_ptr[k] = 0;
        return 1;
      }
      index--;
    }
    p += k;
  }
  return 0;
}

/* Whether (x, y) is the indicator cell of a collapsible container.
 *
 * One comparison, not two: open or folded, the indicator owns the container's
 * content box origin. Folded, the chrome is dropped, so that origin is the
 * container's own corner; open, it is the first cell inside the border and
 * padding. Which edge the reservation came off is already folded into the box,
 * so the orientation does not have to be asked again here. */
static int
sOnIndicatorCell(const IrcitUiNode *node_ptr, int x, int y)
{
  if (!(node_ptr->common.flags & IRCITUI_F_COLLAPSIBLE)) return 0;
  // The same row and column the draw puts the glyph on, taken from the same helper, so the clickable cell cannot drift from the painted one.
  return y == node_ptr->common.y && x == node_ptr->common.x + sIndicatorCol(node_ptr);
}

/*  mouse matrix, in one place.
 *
 * THE GESTURE'S MEANING COMES FROM THE MODIFIER, NOT FROM THE MODE. That is
 * ruling 2: selection_mode stops being "what a click does" and becomes a
 * capability gate, so it decides whether a click may mark at all and how much of
 * the matrix is live, while ctrl and shift decide WHICH row is marked. Before
 * this, a plain click on a MULTI list toggled, and the router's own comment
 * called a click and SPACE one act -- that rule is retired for the mouse, which
 * is ruling 2's "permanently retired", and SPACE keeps toggling.
 *
 * The modifiers arrive in @p bstate as BUTTON_CTRL / BUTTON_SHIFT, which xterm
 * carries in the SGR mouse encoding and ncurses decodes -- measured, not assumed:
 * a plain click is 0x4, ctrl 0x2000004, shift 0x4000004, shift+ctrl 0x6000004.
 * BUTTON_ALT is deliberately not read; SS25's matrix has no use for it, and
 * inventing one would be a gesture nobody asked for.
 *
 * @p was is the cursor before the click, and it is what the SHIFT arm clears. The
 * span the previous gesture marked runs from the anchor to wherever the cursor
 * stood, so clearing anchor..was and setting anchor..target is the same
 * re-derive-don't-accumulate rule the range keys follow, and for the same reason:
 * a toggle on the rows that overlap would unmark them. */
/* SS27: is this press the second of a pair on the same row, and if so open the row
 * for editing?
 *
 * THE TIMING IS COMPUTED HERE RATHER THAN BORROWED FROM NCURSES, and the reason is
 * this library's own doing: IrcitUiMouseInit calls mouseinterval(0), which is what
 * keeps a single click immediate -- ncurses reports it on the frame it arrives
 * instead of holding it for up to an interval to see whether a second one follows.
 * That is the right trade for an interface meant to feel like a desktop
 * application, and it is also what stops ncurses pairing a human's two clicks: it
 * only reports BUTTON1_DOUBLE_CLICKED for presses that arrive in the same read.
 *
 * MEASURED, and the reason this is a clock rather than a bit test. What ncurses
 * reports for a click depends on how the bytes arrived, not on what the user did:
 *
 *   press+release in one read   -> ONE event, BUTTON1_CLICKED (0x4)
 *   press, 100ms, release       -> PRESSED (0x2) then RELEASED (0x1)
 *   two clicks 100ms apart      -> CLICKED, CLICKED -- and NEVER 0x8
 *
 * BUTTON1_DOUBLE_CLICKED appears only when all four events land in the same read,
 * which a terminal driven by a human hand does not do. So it is honoured when it
 * arrives, as a shortcut that skips the comparison, and it is NOT the mechanism:
 * the pair a user actually produces is two separate clicks, and this counts them.
 *
 * Which is also why the recording below looks at @p coalesced alone: the events to
 * time are the CLICK-COMPLETED ones, because that is the shape both a fast click
 * (CLICKED) and a slow one (PRESSED .. RELEASED) end up producing.
 *
 * The press is CONSUMED either way: a third click must not chain into a second
 * edit, and a double-click is not a selection gesture -- the first click of the
 * pair has already selected the row, which is what a desktop list does.
 *
 * Returns 1 only if the row was actually opened; a refusal from BeginEdit (no
 * editing type, no on_item_edited) falls through, so the press still means what a
 * press means in that application. */
static int
sRecyclerDoubleClick(IrcitUiNode *node_ptr, IrcitUiRecyclerState *state_ptr, size_t index, int coalesced)
{
  int64_t now;
  int     second;

  if (coalesced) {
    second = 1;
  }
  else {
    now    = sMonotonicNs();
    second = (index == state_ptr->last_press_index) &&
             (now - state_ptr->last_press_ns <= IRCITUI_DOUBLE_CLICK_NS);
    state_ptr->last_press_index = index;
    state_ptr->last_press_ns    = now;
  }
  if (!second) return 0;

  // Spent, either way: without this a third click would pair with the second and open the editor a second time, which the user would see as the text they had just typed being replaced by the stored value.
  state_ptr->last_press_index = IRCITUI_NO_SECTION;
  state_ptr->last_press_ns    = 0;
  return IrcitUiRecyclerBeginEdit(node_ptr, index);
}

static void
sRecyclerMouseSelect(IrcitUiNode *node_ptr, size_t target, mmask_t bstate)
{
  IrcitUiRecyclerState *state_ptr = node_ptr->common.widget_state;
  size_t                was       = state_ptr->cursor_index;
  int                   ctrl      = (bstate & BUTTON_CTRL) != 0;
  int                   shift     = (bstate & BUTTON_SHIFT) != 0;

  if (state_ptr->selection_mode == IRCITUI_SELECT_NONE) {
    IrcitUiRecyclerSetCursor(node_ptr, target);
    return;
  }

  if (state_ptr->selection_mode == IRCITUI_SELECT_SINGLE) {
    IrcitUiRecyclerSetCursor(node_ptr, target);
    return;
  }

  state_ptr->cursor_index = target;

  if (shift) {
    if (!state_ptr->has_selection_anchor) {
      state_ptr->selection_anchor_index = was;
      state_ptr->has_selection_anchor   = 1;
    }
    (void)IrcitUiRecyclerSelectRange(node_ptr, state_ptr->selection_anchor_index, was, 0);
    (void)IrcitUiRecyclerSelectRange(node_ptr, state_ptr->selection_anchor_index, target, 1);
  }
  else if (ctrl) {
    // Disjoint: the rest of the set is left exactly as it was, which is the whole difference between this and the plain click below.
    state_ptr->selection_anchor_index = target;
    state_ptr->has_selection_anchor   = 1;
    (void)IrcitUiRecyclerSelect(node_ptr, target, !IrcitUiRecyclerIsSelected(node_ptr, target));
  }
  else {
    state_ptr->selection_anchor_index = target;
    state_ptr->has_selection_anchor   = 1;
    IrcitUiRecyclerClearSelection(node_ptr);
    (void)IrcitUiRecyclerSelect(node_ptr, target, 1);
  }
  // The viewport is pulled in case the click moved the cursor out of it, and a pass is asked for because a range can change which rows are marked.
  sCursorFollow(node_ptr, state_ptr);
  IrcitUiRequestLayout(node_ptr);
}

/* The walk, and the depth it is at. The key is offered to the focused node ONCE,
   and only at the entry call: IrcitUiFocus does not depend on which node is being
   visited, so an offer inside the walk was the same offer repeated once per node. */
static int sDispatchKeyAt(IrcitUiNode *node_ptr, int key, int depth);

PUBLIC_API int
IrcitUiDispatchKey(IrcitUiNode *node_ptr, int key)
{
  return sDispatchKeyAt(node_ptr, key, 0);
}

static int
sDispatchKeyAt(IrcitUiNode *node_ptr, int key, int depth)
{
  int i, r;

  if (key == ERR) return 0;

  if (IrcitUiSnackbarKey(key)) return 1;

  if (sRecyclerEditKey(key)) return 1;

  if (key == KEY_MOUSE) {
    MEVENT       ev;
    IrcitUiNode *hit, * st, *  d;
    int          press, click, right, wheel_up, wheel_dn, on_bar, dbl;

    if (getmouse(&ev) != OK) return 0;
    sLastMouse = ev;
    press      = !!(ev.bstate & BUTTON1_PRESSED);
    click      = !!(ev.bstate & BUTTON1_CLICKED);
    // ncurses paired the two clicks itself -- see sRecyclerDoubleClick for when that happens and why it is not the only path.
    dbl        = !!(ev.bstate & BUTTON1_DOUBLE_CLICKED);
    right      = !!(ev.bstate & (BUTTON3_PRESSED | BUTTON3_CLICKED));
    wheel_up   = !!(ev.bstate & BUTTON4_PRESSED);
    wheel_dn   = !!(ev.bstate & BUTTON5_PRESSED);

    if (ev.bstate & BUTTON1_RELEASED) {
      if (sDragNode && sBarState(sDragNode)) sBarSeekReport(sDragNode, ev.x, IRCITUI_PHASE_RELEASED);
      sDragNode = sSelNode = sScrollDrag = NULL;
      return 1;
    }

    if ((press || click || dbl) && IrcitUiFocus && IrcitUiFocus->common.kind == IRCITUI_EDIT_TEXT) {
      IrcitUiNode *editing_list = NULL;
      IrcitUiNode *p;

      for (p = IrcitUiFocus->common.parent; p; p = p->common.parent) {
        if (p->common.kind == IRCITUI_RECYCLER) {
          editing_list = p;
          break;
        }
      }
      if (editing_list) {
        IrcitUiRecyclerState *es = editing_list->common.widget_state;
        if (es && es->editing_index != IRCITUI_NO_SECTION &&
            !(ev.y >= IrcitUiFocus->common.y && ev.y < IrcitUiFocus->common.y + IrcitUiFocus->common.h &&
              ev.x >= IrcitUiFocus->common.x && ev.x < IrcitUiFocus->common.x + IrcitUiFocus->common.w)) {
          sRecyclerEndEdit(editing_list, es, 1);
        }
      }
    }

    if (sDragNode) {
      if (sBarState(sDragNode)) {
        sBarSeekReport(sDragNode, ev.x, IRCITUI_PHASE_DRAGGING);
        return 1;
      }
      IrcitUiDragTo(sDragNode, ev.x - drag_off_x, ev.y - drag_off_y);
      return 1;
    }
    if (sSelNode && sSelNode->common.kind == IRCITUI_RECYCLER) {
      size_t       target;
      IrcitUiNode *under = IrcitUiScreen.root ? IrcitUiHitTest(IrcitUiScreen.root, ev.y, ev.x) : NULL;

      if (under && sRowBoundIndex(under, NULL, &target)) {
        sRecyclerMouseSelect(sSelNode, target, BUTTON_SHIFT);
      }
      return 1;
    }
    if (sSelNode) {
      const char *ts = sSelNode->common.interaction_defaults.user ?
                         (char*)sSelNode->common.interaction_defaults.user :
                         (sSelNode->common.text ? sSelNode->common.text : "");
      int col = ev.x - sSelNode->common.x - (sSelNode->common.kind == IRCITUI_EDIT_TEXT ? 2 : 0);
      int row = ev.y - sSelNode->common.y + sSelNode->container.scroll_y;
      if (sSelNode->common.kind == IRCITUI_EDIT_TEXT) {
        // Extend the selection with the same inverse mapping the press used, so a click and a drag cannot disagree about which character a column means; see IrcitUiEditTextSelectionExtendToCol for why the caret does not move with it.
        IrcitUiEditTextSelectionExtendToCol(sSelNode, col);
      }
      else {
        sSelNode->container.sel1 = sTextIndexAt(ts, col, row);
      }
      // The selection highlight is painted, and it moved. Inline in the dispatch path rather than behind a listener, so it is wired here.
      IrcitUiInvalidate(sSelNode);
      return 1;
    }
    if (sScrollDrag) {
      int track = sScrollDrag->common.h - 2, maxs, rel;
      if (track < 1) track = 1;
      maxs                            = sScrollDrag->container.columns > 0 ? sScrollDrag->container.columns : 1;
      rel                             = ev.y - sScrollDrag->common.y - 1;
      sScrollDrag->container.scroll_y = rel * maxs / track;
      if (sScrollDrag->container.scroll_y < 0) sScrollDrag->container.scroll_y = 0;
      if (sScrollDrag->container.scroll_y > maxs) sScrollDrag->container.scroll_y = maxs;
      // The offset is applied to children during layout, not at paint time, so scrolling is a geometry change even though nothing resizes. The frame that follows is what measures it.
      IrcitUiRequestLayout(sScrollDrag);
      return 1;
    }

    hit = IrcitUiHitTest(IrcitUiScreen.root ? IrcitUiScreen.root : node_ptr, ev.y, ev.x);

    IRCITUI_LOG_INFO("click at (%d,%d) bstate=0x%x -> %s", ev.x, ev.y, (unsigned)ev.bstate, hit ? (hit->common.id ? hit->common.id : "(no id)") : "(no node)");
    if (!hit) return 0;

    if (wheel_up || wheel_dn) {
      /* Routed, not applied here. The shift and the clamp that stood below are
         IrcitUiDefaultOnScroll, which the dispatch chain reaches - so the
         application's own on_scroll is consulted first and its veto leaves the
         pane where it is. Posted at the hit node rather than at the pane: the
         default makes the walk to the nearest scrollable ancestor itself, so an
         application can bind inside the pane as well as on it. */
      for (d = hit; d && !(d->common.flags & IRCITUI_F_SCROLLABLE); d = d->common.parent) {}
      IrcitUiDispatcherPostScroll(IrcitUiDispatcherInstance(), hit, wheel_up ? -1 : 1);
      /* Layout change, not a paint one: see the thumb-drag path above. Requested
         whenever a pane exists, because a listener that consumed the notch may
         have moved something this cannot see. */
      if (d) IrcitUiRequestLayout(d);
      return 1;
    }

    if (right) {
      IrcitUiSetFocus(hit);
      IrcitUiShowContextMenu(hit);
      return 1;
    }

    if (!(press || click)) return 1;

    on_bar = (hit->common.flags & IRCITUI_F_SCROLLABLE) && ev.x >= hit->common.x + hit->common.w - 1;
    if (!on_bar) {
      for (d = hit; d; d = d->common.parent)
        if ((d->common.flags & IRCITUI_F_SCROLLABLE) && ev.x >= d->common.x + d->common.w - 1) {
          hit    = d;
          on_bar = 1;
          break;
        }
    }
    if (on_bar) {
      sScrollDrag = hit;
      return 1;
    }

    // The conversation's one gesture is a TAP, and it sits below the branches that own motion
    // (the hover), the wheel and the right button, which it used to sit above and take.
    if ((press || click || dbl) && (hit->common.kind == IRCITUI_CONVERSATION_WINDOW || hit->common.kind ==
      IRCITUI_MESSAGE_VIEWPORT)) {
      if (IrcitUiConversationWindowHandleClick(hit, ev.y, ev.x)) return 1;
    }

    for (d = hit; d; d = d->common.parent)
      if (sOnIndicatorCell(d, ev.x, ev.y)) {
        IrcitUiSetCollapsed(d, !d->container.is_collapsed);
        return 1;
      }

    if (hit->common.kind == IRCITUI_RADIO_GROUP || hit->common.kind == IRCITUI_CHECK_GROUP || hit->common.kind ==
      IRCITUI_RADIO_BUTTON || hit->common.kind == IRCITUI_CHECK_BOX) {
      IrcitUiNode *g = hit;
      int          row;
      if (hit->common.kind == IRCITUI_RADIO_BUTTON || hit->common.kind == IRCITUI_CHECK_BOX) g = hit->common.parent;
      if (g && (g->common.kind == IRCITUI_RADIO_GROUP || g->common.kind == IRCITUI_CHECK_GROUP)) {
        row = sGroupRowAt(g, ev.y);
        if (g->common.kind == IRCITUI_CHECK_GROUP && hit->common.kind == IRCITUI_CHECK_BOX) {
          // row from child index
          int i;
          for (i = 0; i < g->container.n_children; i++) if (g->container.children[i] == hit) row = i;
        }
        sGroupApplyRow(g, row);
        IrcitUiSetFocus(g);
        return 1;
      }
    }
    if (hit->common.kind == IRCITUI_BUTTON || hit->common.kind == IRCITUI_EMOJI_TABLE || ((hit->common.flags &
      IRCITUI_F_CLICKABLE) && hit->common.kind != IRCITUI_TEXT_VIEW && hit->common.kind != IRCITUI_EDIT_TEXT)) {
      if (IrcitUiClickDispatch(hit) == IRCITUI_CLICK_SINK) return 1;
      const char *mid = NULL;
      if (hit->common.parent && (hit->common.parent->common.flags & IRCITUI_F_CHILDREN_SELECTABLE) &&
        IrcitUiIsContainer(hit->common.parent->common.kind)) {
        for (i = 0; i < hit->common.parent->container.n_children; i++)
          if (hit->common.parent->container.children[i] == hit) {
            hit->common.parent->container.sel0 = i;
            break;
          }
        // Read the id now, before the handler below can pop this layer and free the popup root; nothing reached through the container pointer may be touched after that.
        mid = hit->common.parent->common.id;
        IrcitUiSetFocus(hit->common.parent);
      }
      else IrcitUiSetFocus(hit);
      if (hit->common.kind == IRCITUI_CHECK_BOX || hit->common.kind == IRCITUI_SWITCH || hit->common.kind ==
        IRCITUI_RADIO_BUTTON)
        hit->common.flags ^= IRCITUI_F_CHECKED;
      // Toggled inline, before the click is posted -- which is why the routed default declines KEY_MOUSE rather than toggling a second time. The tick is painted, so the node is invalidated here.
      IrcitUiInvalidate(hit);
      // Read the id here for the same reason as `mid` above: the handler below can pop the layer this button lives in and free it.
      const char *hid = hit->common.id;
      // The framework's kind-routed default for a completed click. Recorded through the dispatcher rather than called directly so the application's own on_click, if it registered one, is consulted first.
      IrcitUiDispatcherPostClick(IrcitUiDispatcherInstance(), hit, sLastMouse.x, sLastMouse.y);
      st = IrcitUiFind(IrcitUiScreen.root, "status");
      if (st) {
        static char echo[PRIV_CONFIG_DEFAULT_STATUS_ECHO_SZ];
        snprintf(echo, sizeof echo, "click %s", hid ? hid : "?");
        st->common.interaction_defaults.user = echo;
        // The bytes behind the alias changed; the node's fields did not.
        IrcitUiInvalidate(st);
      }
      // Any activation dismisses. sMenuDismiss checks identity, so an item whose handler already took the layer away is not popped twice.
      if (mid) sMenuDismiss(mid);
      return 1;
    }

    if (hit->common.kind == IRCITUI_TEXT_VIEW) {
      for (d = hit; d; d = d->common.parent)
        if (IrcitUiIsContainer(d->common.kind) && (d->common.flags & IRCITUI_F_DRAGGABLE)) {
          sDragNode  = d;
          drag_off_x = ev.x - d->common.x;
          drag_off_y = ev.y - d->common.y;
          return 1;
        }
    }
    if (hit->common.kind == IRCITUI_EDIT_TEXT || hit->common.kind == IRCITUI_TEXT_VIEW) {

      for (d = hit; d; d = d->common.parent) {
        if (d->common.kind == IRCITUI_RECYCLER) {
          size_t                row_index;
          IrcitUiRecyclerState *row_state = d->common.widget_state;

          if (row_state && row_state->editing_index != IRCITUI_NO_SECTION && sRowBoundIndex(hit, NULL, &row_index) &&
              row_index == row_state->editing_index) {
            break;
          }
          if ((press || click || dbl) && row_state && sRowBoundIndex(hit, NULL, &row_index) &&
              sRecyclerDoubleClick(d, row_state, row_index, dbl)) {
            return 1;
          }
          IrcitUiSetFocus(d);
          if (sRowBoundIndex(hit, NULL, &row_index)) {
            sRecyclerMouseSelect(d, row_index, ev.bstate);
            if (press) sSelNode = d;
            (void)sRowActivated(d, row_index);
          }
          return 1;
        }
      }
      const char *ts = hit->common.interaction_defaults.user ?
                         (char*)hit->common.interaction_defaults.user :
                         (hit->common.text ? hit->common.text : "");
      int col = ev.x - hit->common.x - (hit->common.kind == IRCITUI_EDIT_TEXT ? 2 : 0);
      int row = ev.y - hit->common.y + hit->container.scroll_y;
      if (hit->common.kind == IRCITUI_EDIT_TEXT) IrcitUiEditTextCaretToCol(hit, col);
      else {
        // TEXT_VIEW keeps its byte-column geometry untouched: the canvas look depends on it.
        hit->container.sel0 = hit->container.sel1 = sTextIndexAt(ts, col, row);
      }
      // Caret and selection moved; the field's box did not.
      IrcitUiInvalidate(hit);
      sSelNode = press ? hit : NULL;
      IrcitUiSetFocus(hit);
      return 1;
    }

    if ((press || click) && sBarState(hit)) {
      IrcitUiSetFocus(hit);
      if (click && !press) {
        sBarSeekReport(hit, ev.x, IRCITUI_PHASE_RELEASED);
        return 1;
      }
      sDragNode  = hit;
      drag_off_x = 0;
      drag_off_y = 0;
      sBarSeekReport(hit, ev.x, IRCITUI_PHASE_DRAGGING);
      return 1;
    }

    for (d = hit; d; d = d->common.parent) {
      if (IrcitUiIsContainer(d->common.kind) && (d->common.flags & IRCITUI_F_DRAGGABLE)) {
        sDragNode  = d;
        drag_off_x = ev.x - d->common.x;
        drag_off_y = ev.y - d->common.y;
        IrcitUiSetFocus(d);
        return 1;
      }
    }
    return 1;
  }
  if (key == IRCITUI_KEY_ESC) { /* Esc */
    IrcitUiCloseTopPopup();
    // Whatever held focus was inside the popup that just went away. Re-resolve so Esc puts you back in the input field instead of leaving IrcitUiFocus on a node that has been freed.
    IrcitUiFocusTop();

    if (IrcitUiFocus) IrcitUiEditTextDismissHint(IrcitUiFocus);
    return 1;
  }
  if (IrcitUiFocus && (IrcitUiFocus->common.kind == IRCITUI_RADIO_GROUP || IrcitUiFocus->common.kind ==
    IRCITUI_CHECK_GROUP)) {
    if (key == ' ' || key == KEY_ENTER || key == '\n' || key == '\r') {
      if (IrcitUiFocus->container.sel0 < 0) IrcitUiFocus->container.sel0 = 0;
      sGroupApplyRow(IrcitUiFocus, IrcitUiFocus->container.sel0);
      return 1;
    }
    if (key == KEY_UP || key == KEY_LEFT) {
      if (IrcitUiFocus->container.sel0 > 0) IrcitUiFocus->container.sel0--;
      if (IrcitUiFocus->common.kind == IRCITUI_RADIO_GROUP) sGroupApplyRow(IrcitUiFocus, IrcitUiFocus->container.sel0);
      return 1;
    }
    if (key == KEY_DOWN || key == KEY_RIGHT) {
      if (IrcitUiFocus->container.sel0 < IrcitUiFocus->container.n_children - 1) IrcitUiFocus->container.sel0++;
      if (IrcitUiFocus->common.kind == IRCITUI_RADIO_GROUP) sGroupApplyRow(IrcitUiFocus, IrcitUiFocus->container.sel0);
      return 1;
    }
  }
  if (IrcitUiFocus && (key == KEY_LEFT || key == KEY_RIGHT) &&
      sBarKeySeek(IrcitUiFocus, key == KEY_LEFT ? -1 : 1))
    return 1;

  if (IrcitUiFocus && (IrcitUiFocus->common.flags & IRCITUI_F_CHILDREN_SELECTABLE) && IrcitUiIsContainer(
    IrcitUiFocus->common.kind)) {
    sMenuRepairRow(IrcitUiFocus);
    /* Invalidated once, ahead of the branches, because every one of them either
       moves the cursor row or leaves it where sMenuRepairRow just put it -- and
       the repair itself can move it. One node's highlight, no box change. */
    IrcitUiInvalidate(IrcitUiFocus);
    if (key == KEY_UP) {
      r = sMenuRowBack(IrcitUiFocus, IrcitUiFocus->container.sel0 - 1);
      if (r >= 0) IrcitUiFocus->container.sel0 = r;
      return 1;
    }
    if (key == KEY_DOWN) {
      r = sMenuRowFwd(IrcitUiFocus, IrcitUiFocus->container.sel0 + 1);
      if (r >= 0) IrcitUiFocus->container.sel0 = r;
      return 1;
    }
    if (key == KEY_ENTER || key == '\n' || key == '\r') {
      sMenuActivate(IrcitUiFocus, key);
      return 1;
    }
  }
  if (IrcitUiFocus && IrcitUiFocus->common.kind == IRCITUI_EMOJI_TABLE) {
    int cells = sEmojiCells(IrcitUiFocus);
    int cols  = IrcitUiFocus->container.columns > 0 ? IrcitUiFocus->container.columns : PRIV_CONFIG_DEFAULT_GRID_COLS;
    int last  = cells - 1;
    int cur   = IrcitUiFocus->container.sel0;
    int moved = 0;

    if (cur < 0 || cur > last) cur = 0;
    if (cells > 0) {
      if (key == KEY_UP) {
        cur   -= cols;
        moved = 1;
      }
      else if (key == KEY_DOWN) {
        cur   += cols;
        moved = 1;
      }
      else if (key == KEY_LEFT) {
        if (cur % cols != 0) cur--;
        moved = 1;
      }
      else if (key == KEY_RIGHT) {
        if (cur % cols != cols - 1) cur++;
        moved = 1;
      }
      if (moved) {
        if (cur < 0) cur = 0;
        if (cur > last) cur = last;
        IrcitUiFocus->container.sel0 = cur;
        // The cursor cell moved; the grid's extent did not.
        IrcitUiInvalidate(IrcitUiFocus);
        return 1;
      }
      if (key == KEY_ENTER || key == '\n' || key == '\r') {
        char tmp[PRIV_CONFIG_DEFAULT_UTF8_MAX_BYTES];
        IrcitUiFocus->container.sel0 = cur;
        if (sEmojiCellText(IrcitUiFocus, cur, tmp, sizeof tmp)) IrcitUiInputAppend(tmp);
        return 1;
      }
    }
  }
  if (key == IRCITUI_KEY_CTRL_C || key == IRCITUI_KEY_CTRL_V || key == IRCITUI_KEY_CTRL_X ||
    key == IRCITUI_KEY_CTRL_U) { /* ^C ^V ^X ^U */
    if (key == IRCITUI_KEY_CTRL_C) IrcitUiClipCopy(IrcitUiFocus);
    else if (key == IRCITUI_KEY_CTRL_V) IrcitUiClipPaste(IrcitUiFocus);
    // ^X kills from the caret rightwards, ^U the whole field -- but only a field has a caret to kill from. Any
    // other kind keeps the clear it had: its "end" is the whole text, which is what IrcitUiClipClear does.
    else if (key == IRCITUI_KEY_CTRL_X && IrcitUiFocus && IrcitUiFocus->common.kind == IRCITUI_EDIT_TEXT)
      IrcitUiEditTextDeleteToEnd(IrcitUiFocus);
    else if (key == IRCITUI_KEY_CTRL_U && IrcitUiFocus && IrcitUiFocus->common.kind == IRCITUI_EDIT_TEXT)
      IrcitUiEditTextClear(IrcitUiFocus);
    else IrcitUiClipClear(IrcitUiFocus);
    return 1;
  }

  if (key == '\t' && IrcitUiEditTextAcceptHint(IrcitUiFocus)) return 1;

  // THE PANEL TAKES THE ARROWS WHILE IT IS UP.
  if (IrcitUiFocus && (key == KEY_UP || key == KEY_DOWN) && IrcitUiEditTextSuggestKey(IrcitUiFocus, key)) return 1;

  // Once, and before the walk: the focused node's veto. A non-zero return stops the walk before any other node is reached.
  if (depth == 0 && IrcitUiDispatcherPostKey(IrcitUiDispatcherInstance(), IrcitUiFocus, key)) return 1;
  for (i = node_ptr->container.n_children - 1; i >= 0; i--) if ((r = sDispatchKeyAt(
    node_ptr->container.children[i], key, depth + 1))) return r;
  // The focused node was offered both blocks above; offering it again here is the second half of the same duplicate.
  if (node_ptr != IrcitUiFocus && IrcitUiDispatcherPostKey(IrcitUiDispatcherInstance(), node_ptr, key)) return 1;
  return 0;
}

static const IrcitUiNode *
sRefFind(const IrcitUiNode *node_ptr, const char *id_ptr)
{
  int                i;
  const IrcitUiNode *f;
  if (node_ptr->common.id && strcmp(node_ptr->common.id, id_ptr) == 0) return node_ptr;
  for (i = 0; i < node_ptr->container.n_children; i++) if ((f = sRefFind(node_ptr->container.children[i], id_ptr)))
    return f;
  return NULL;
}

/* The hash the id index is built on. tools/gen_ui.py's id_hash() computes the
   same value with the same two constants, and the two must be changed together:
   a mismatch would not fail loudly, it would simply stop finding ids that are
   present, and the walk below would quietly cover for it. */
#define IRCITUI_ID_HASH_INIT  0x811C9DC5u
#define IRCITUI_ID_HASH_PRIME 0x01000193u

static uint32_t
sIdHash(const char *s_ptr)
{
  uint32_t             h = IRCITUI_ID_HASH_INIT;
  const unsigned char *p = (const unsigned char*)s_ptr;
  while (*p) {
    h ^= *p++;
    h *= IRCITUI_ID_HASH_PRIME;
  }
  return h;
}

/* Resolve an id through the model's index, when it carries one.

   Open addressing with linear probing, and the probe is bounded by the value the
   generator proved for this table rather than by a guess at the hash's quality.
   A miss is not an error: the table holds only the ids the generator emitted, so
   an id that is not one of them - one registered at run time - simply is not
   there, and sLookupRef's walk is what answers for it. */
static const IrcitUiNode *
sLookupByIdIndex(const IrcitUiDataBindDescriptor *model_ptr, const char *id_ptr)
{
  size_t   i;
  uint32_t k = sIdHash(id_ptr) % (uint32_t)model_ptr->id_slot_count;

  for (i = 0; i <= model_ptr->id_slot_max_probe; i++) {
    uint32_t           slot = model_ptr->id_slots[k];
    const IrcitUiNode *node_ptr;
    if (slot == IRCITUI_SLOT_NONE) return NULL;
    node_ptr = &model_ptr->nodes[slot];
    if (node_ptr->common.id && strcmp(node_ptr->common.id, id_ptr) == 0) return node_ptr;
    k = (k + 1) % (uint32_t)model_ptr->id_slot_count;
  }
  return NULL;
}

/* Resolve an id against the model the framework is driving, rather than against
   the generated tree by name. The two are the same object while IrcitUiDataBind
   is the binder, and they part company as soon as an application supplies a model
   of its own - which is the case this reads through the context to serve.
   The index answers first when the model has one. The walk that follows is not
   dead code behind it: it is what serves a model assembled without a generator,
   which carries no table, and what serves any id the table does not hold. Its
   order is root first, then each unbound template root, which is the precedence
   a duplicate id would be shadowed by - though the generator refuses to emit a
   duplicate, so no emitted model relies on it. */
static const IrcitUiNode *
sLookupRef(const char *id_ptr)
{
  const IrcitUiDataBindDescriptor *model_ptr = IrcitUiModel();
  const IrcitUiNode *              f;
  size_t                           i;
  if (model_ptr->id_slots && model_ptr->id_slot_count && (f = sLookupByIdIndex(model_ptr, id_ptr))) return f;
  if (model_ptr->root && (f = sRefFind(model_ptr->root, id_ptr))) return f;
  for (i = 0; i < model_ptr->template_count; i++) if (model_ptr->templates[i] && (f = sRefFind(
    model_ptr->templates[i], id_ptr))) return f;
  return NULL;
}

PUBLIC_API IrcitUiNode *
IrcitUiClone(const IrcitUiNode *ref_ptr)
{
  IrcitUiNode *n;
  int          i;

  if (!ref_ptr) return NULL;
  n = calloc(1, sizeof *n);
  if (!n) return NULL;
  *n                                  = *ref_ptr;
  n->common.win                       = NULL;
  n->common.interaction_defaults.user = NULL;
  n->common.parent                    = NULL;
  n->common.ref                       = ref_ptr;
  n->common.flags                     &= ~(IRCITUI_F_FOCUSED | IRCITUI_F_DRAWN | IRCITUI_F_STACKED);
  n->common.is_dirty                  = 0;
  n->common.widget_state              = NULL;
  n->container.children               = NULL;
  n->container.n_children             = 0;

  IRCITUI_LOG_INFO("node constructed: id=%s kind=%d children=%d ref=%p", n->common.id ? n->common.id : "(no id)", (int)n->common.kind, ref_ptr->container.n_children, (const void *)ref_ptr);

  if (ref_ptr->container.n_children > 0) {
    n->container.children = calloc((size_t)ref_ptr->container.n_children, sizeof *n->container.children);
    if (!n->container.children) {
      free(n);
      return NULL;
    }
    n->container.n_children = ref_ptr->container.n_children;
    for (i = 0; i < ref_ptr->container.n_children; i++) {
      n->container.children[i] = IrcitUiClone(ref_ptr->container.children[i]);
      if (!n->container.children[i]) {
        sFreeTree(n);
        return NULL;
      }
      n->container.children[i]->common.parent = n;
    }
  }
  //  todo consider implementing under on_inflate() semantics
  sAllocWidgetState(n);

  return n;
}

/* Release a tree of nodes that this library owns: every node reachable from n
   must have come from IrcitUiClone() or sNodeFromDef(), so its child table is
   either NULL or a heap array this function may free.

   It must NOT be handed the reference tree. The generated model wires its
   container children to file-scope `static IrcitUiNode *..._ch[]` tables
   (generated/ui_generated.c:91), so freeing those pointers would free static
   storage. All four internal call sites pass clones, so the obligation stays
   inside this file. */
static void
sFreeTree(IrcitUiNode *node_ptr)
{
  int i;
  if (!node_ptr) return;
  if (IrcitUiFocus == node_ptr) IrcitUiSetFocus(NULL);
  sDirtyListForget(node_ptr);

  for (i = node_ptr->container.n_children - 1; i >= 0; --i) sFreeTree(node_ptr->container.children[i]);
  free(node_ptr->container.children);
  if (node_ptr->common.win) {
    delwin(node_ptr->common.win);
    node_ptr->common.win = NULL;
  }
  sFreeWidgetState(node_ptr);
  free(node_ptr);
}

/* The one place a node's extension struct is destroyed. It is not kind-aware
   about NODES, and that is the point: no kind's state owns a node. A recycler's
   rows -- which used to be walked from here -- are container.children, so
   sFreeTree's own recursion above is their teardown, and freeing them here as
   well would be a double free.
 *
 * It is kind-aware about arrays, because a state may own one that no traversal
 * can reach: the recycler's selection bit-array is keyed by dataset index, so it
 * is reachable from nothing but this struct. Anything else a kind's state comes
 * to own needs a line here; the rule is "owned by the state, unreachable from
 * the tree", which is why the rows needed the opposite answer. */
static void
sFreeWidgetState(IrcitUiNode *node_ptr)
{
  if (!node_ptr || !node_ptr->common.widget_state) return;
  if (node_ptr->common.kind == IRCITUI_CONVERSATION_WINDOW) {
    // What the widget copied, given back; the struct goes with every other kind's on the common tail below.
    IrcitUiConversationStateRelease(node_ptr->common.widget_state);
  } else if (node_ptr->common.kind == IRCITUI_RECYCLER) {
    free(((IrcitUiRecyclerState *)node_ptr->common.widget_state)->selected_bits);
  } else if (node_ptr->common.kind == IRCITUI_PROGRESS_BAR || node_ptr->common.kind == IRCITUI_SEEK_BAR) {

    IrcitUiBarState *bar = node_ptr->common.widget_state;
    if (bar->ramp_base >= 0) {
      IrcitUiPaletteRampRelease(bar->ramp_base, bar->ramp_steps, sBarRampOwner);
      bar->ramp_base = -1;
    }
  }
  free(node_ptr->common.widget_state);
  node_ptr->common.widget_state = NULL;
}

PUBLIC_API void
IrcitUiScreenReset(void)
{
  if (IrcitUiScreen.root) {
    IrcitUiDeflate(IrcitUiScreen.root);

    IrcitUiSetFocus(NULL);
    sFreeTree(IrcitUiScreen.root);
  }
  // Last, and after any invalidate the focus transition above may have raised: every entry named a node from the tree that has just been released.
  {
    IrcitUiDirtyList *dirty = IrcitUiFrameDirtyList();
    dirty->count            = 0;
    dirty->overflow         = 0;
  }
  IrcitUiScreen.root = IrcitUiClone(IrcitUiModel()->root);
  // The replacement tree has no measured geometry and no windows, so it needs a full measure pass before anything can be painted.
  if (IrcitUiScreen.root) IrcitUiScreen.root->common.is_dirty = 1;
}

/* Add child to the parent's child table. Deliberately does NOT set
   IRCITUI_F_STACKED: this serves both popup roots and ordinary widget children, and
   STACKED means "own newwin at absolute screen coordinates" (ui_decl.h), which
   would sPlace a plain widget outside its popup's window instead of clipping it
   to that window. The three popup call sites - sInflateAt,
   IrcitUiCreateFloatingWindow and IrcitUiShowContextMenu - each set the flag
   themselves before calling here. */
static int
sAttach(IrcitUiNode *parent_ptr, IrcitUiNode *child_ptr)
{
  IrcitUiNode **p;
  p = realloc(parent_ptr->container.children, (size_t)(parent_ptr->container.n_children + 1) * sizeof *p);
  if (!p) return 0;
  parent_ptr->container.children                                     = p;
  parent_ptr->container.children[parent_ptr->container.n_children++] = child_ptr;
  child_ptr->common.parent                                           = parent_ptr;
  // A new child can change what a wrap_content parent measures itself to, and the child itself has no window yet, so the parent is what is raised.
  IrcitUiRequestLayout(parent_ptr);
  return 1;
}

static void
sPopupSize(IrcitUiNode *node_ptr, int *w_ptr, int *h_ptr)
{
  int capw = PRIV_CONFIG_DEFAULT_POPUP_W, caph = PRIV_CONFIG_DEFAULT_POPUP_H;
  if (node_ptr->common.layout_w.mode == IRCITUI_SIZE_EXACT) *w_ptr = node_ptr->common.layout_w.cells;
  else *w_ptr                                                      = sWrapW(node_ptr);
  if (node_ptr->common.layout_h.mode == IRCITUI_SIZE_EXACT) *h_ptr = node_ptr->common.layout_h.cells;
  else *h_ptr                                                      = sWrapH(node_ptr);
  if (*w_ptr < PRIV_CONFIG_DEFAULT_POPUP_MIN_W) *w_ptr = PRIV_CONFIG_DEFAULT_POPUP_MIN_W;
  if (*h_ptr < PRIV_CONFIG_DEFAULT_POPUP_MIN_H) *h_ptr = PRIV_CONFIG_DEFAULT_POPUP_MIN_H;
  if (COLS > PRIV_CONFIG_DEFAULT_POPUP_EDGE_MARGIN && capw > COLS - PRIV_CONFIG_DEFAULT_POPUP_EDGE_MARGIN) capw = COLS -
    PRIV_CONFIG_DEFAULT_POPUP_EDGE_MARGIN;
  if (LINES > PRIV_CONFIG_DEFAULT_POPUP_EDGE_MARGIN && caph > LINES - PRIV_CONFIG_DEFAULT_POPUP_EDGE_MARGIN) caph =
    LINES - PRIV_CONFIG_DEFAULT_POPUP_EDGE_MARGIN;
  if (*w_ptr > capw) *w_ptr = capw;
  if (*h_ptr > caph) *h_ptr = caph;
}


static void
sClampRect(int *x_ptr, int *y_ptr, int w, int h)
{
  if (*x_ptr + w > COLS) *x_ptr = COLS - w;
  if (*y_ptr + h > LINES) *y_ptr = LINES - h;
  if (*x_ptr < 0) *x_ptr = 0;
  if (*y_ptr < 0) *y_ptr = 0;
}

static void
sOffsetTree(IrcitUiNode *node_ptr, int dx, int dy)
{
  int i;
  node_ptr->common.x += dx;
  node_ptr->common.y += dy;
  for (i = 0; i < node_ptr->container.n_children; i++) sOffsetTree(node_ptr->container.children[i], dx, dy);
}

PUBLIC_API void
IrcitUiDragTo(IrcitUiNode *node_ptr, int x, int y)
{
  int dx, dy;
  if (!node_ptr) return;
  sClampRect(&x, &y, node_ptr->common.w, node_ptr->common.h);
  dx = x - node_ptr->common.x;
  dy = y - node_ptr->common.y;
  if (!dx && !dy) return;
  sOffsetTree(node_ptr, dx, dy);
  if (node_ptr->common.win) mvwin(node_ptr->common.win, node_ptr->common.y, node_ptr->common.x);
  IrcitUiRealize(node_ptr); /* rebuild derwin children at new origin */
  // Guarded on the layout depth because IrcitUiLayout calls this for a stacked popup it is re-clamping.
  if (!sLayoutDepth) IrcitUiRequestLayout(node_ptr);
}

static IrcitUiNode *
sInflateAt(IrcitUiNode *parent_ptr, const char *ref_id_ptr, int x, int y)
{
  const IrcitUiNode *ref = sLookupRef(ref_id_ptr);
  IrcitUiNode *      n;
  int                w, h;

  if (!ref || !parent_ptr) return NULL;
  n = IrcitUiClone(ref);
  if (!n) return NULL;
  n->common.flags |= IRCITUI_F_STACKED | IRCITUI_F_VISIBLE;
  if (!sAttach(parent_ptr, n)) {
    sFreeTree(n);
    return NULL;
  }
  sPopupSize(n, &w, &h);
  sClampRect(&x, &y, w, h);

  IrcitUiLayout(n, x, y, w, h);
  IrcitUiDispatcherPostLifecycle(IrcitUiDispatcherInstance(), n, IRCITUI_EVENT_LIFECYCLE_INFLATE);
  IrcitUiRealize(n);

  IRCITUI_LOG_INFO("template loaded: id=%s from=%s at (%d,%d) size %dx%d under=%s",
                   n->common.id ? n->common.id : "(no id)", ref_id_ptr ? ref_id_ptr : "(null)", n->common.x,
                   n->common.y, n->common.w, n->common.h, parent_ptr->common.id ? parent_ptr->common.id : "(no id)");
  return n;
}

PUBLIC_API IrcitUiNode *
IrcitUiInflateAnchored(IrcitUiNode *parent_ptr, const char *ref_id_ptr, IrcitUiNode *anchor_ptr)
{
  const IrcitUiNode *ref = sLookupRef(ref_id_ptr);
  int                w, h, x, y;
  IrcitUiNode        tmp;

  if (!ref) return NULL;
  tmp                      = *ref;
  tmp.container.n_children = ref->container.n_children;
  sPopupSize((IrcitUiNode*)ref, &w, &h);
  // prefer below-left of the trigger; flip if it would leave the screen
  x = anchor_ptr ? anchor_ptr->common.x : 0;
  y = anchor_ptr ? anchor_ptr->common.y + anchor_ptr->common.h : 1;
  if (anchor_ptr && x + w > COLS) x = anchor_ptr->common.x + anchor_ptr->common.w - w; /* hang off the right edge */
  if (anchor_ptr && y + h > LINES) y = anchor_ptr->common.y - h;                       /* drop above */
  if (x + w > COLS) x = COLS - w;
  if (y + h > LINES) y = LINES - h;
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  (void)tmp;

  return sInflateAt(parent_ptr, ref_id_ptr, x, y);
}

PUBLIC_API IrcitUiNode *
IrcitUiInflateFloat(IrcitUiNode *parent_ptr, const char *ref_id_ptr)
{
  const IrcitUiNode *ref = sLookupRef(ref_id_ptr);
  int                w, h, x, y;

  if (!ref) return NULL;
  sPopupSize((IrcitUiNode*)ref, &w, &h);
  x = (COLS - w) / 2;
  y = (LINES - h) / 2;
  return sInflateAt(parent_ptr, ref_id_ptr, x, y);
}

PUBLIC_API IrcitUiNode *
IrcitUiInflate(IrcitUiNode *parent_ptr, const char *ref_id_ptr)
{
  const IrcitUiNode *ref = sLookupRef(ref_id_ptr);
  if (ref && (ref->common.flags & IRCITUI_F_DRAGGABLE)) return IrcitUiInflateFloat(parent_ptr, ref_id_ptr);
  return IrcitUiInflateAnchored(parent_ptr, ref_id_ptr, parent_ptr);
}

PUBLIC_API void
IrcitUiPopStacked(IrcitUiNode *node_ptr)
{
  IrcitUiNode *p;
  int          i, j;

  if (!node_ptr) return;
  p = node_ptr->common.parent;
  IrcitUiDispatcherPostLifecycle(IrcitUiDispatcherInstance(), node_ptr, IRCITUI_EVENT_LIFECYCLE_DEFLATE);
  IrcitUiDeflate(node_ptr);
  if (!p) return;
  for (i = 0; i < p->container.n_children; i++) {
    if (p->container.children[i] == node_ptr) {
      for (j = i; j < p->container.n_children - 1; j++) p->container.children[j] = p->container.children[j + 1];
      p->container.n_children--;
      break;
    }
  }
  // The parent lost a child, so what it measures itself to may have changed
  IrcitUiRequestLayout(p);

  if (IrcitUiFocus && sInSubtree(node_ptr, IrcitUiFocus)) IrcitUiSetFocus(NULL);
  if (sCtxTarget && sInSubtree(node_ptr, sCtxTarget)) sCtxTarget = NULL;

  if (sDragNode   && sInSubtree(node_ptr, sDragNode))   sDragNode   = NULL;
  if (sSelNode    && sInSubtree(node_ptr, sSelNode))    sSelNode    = NULL;
  if (sScrollDrag && sInSubtree(node_ptr, sScrollDrag)) sScrollDrag = NULL;
  sDirtyListForget(node_ptr);

  sFreeTree(node_ptr);
}

PUBLIC_API int
IrcitUiNodeDestroy(IrcitUiNode *node_ptr)
{
  IrcitUiNode *top_ptr, *parent_ptr;
  int          i, j;

  if (!node_ptr) return 0;

  top_ptr = node_ptr;
  while (top_ptr->common.parent) top_ptr = top_ptr->common.parent;

  {
    IrcitUiNode *model_root = IrcitUiModel() ? IrcitUiModelRoot() : NULL;
    if (model_root && top_ptr == model_root) {
      IRCITUI_LOG_ERROR("IrcitUiNodeDestroy: refused '%s': it is part of the reference model, whose "
                        "child tables are static storage and must not be freed",
                        node_ptr->common.id ? node_ptr->common.id : "(no id)");
      return -1;
    }
  }

  if (node_ptr == IrcitUiScreen.root) {
    IRCITUI_LOG_ERROR("IrcitUiNodeDestroy: refused '%s': it is the live screen root; "
                      "IrcitUiScreenReset is the call that tears that down",
                      node_ptr->common.id ? node_ptr->common.id : "(no id)");
    return -1;
  }

  IrcitUiDispatcherPostLifecycle(IrcitUiDispatcherInstance(), node_ptr, IRCITUI_EVENT_LIFECYCLE_DEFLATE);
  IrcitUiDeflate(node_ptr);

  parent_ptr = node_ptr->common.parent;
  if (parent_ptr) {
    for (i = 0; i < parent_ptr->container.n_children; i++) {
      if (parent_ptr->container.children[i] == node_ptr) {
        for (j = i; j < parent_ptr->container.n_children - 1; j++)
          parent_ptr->container.children[j] = parent_ptr->container.children[j + 1];
        parent_ptr->container.n_children--;
        break;
      }
    }
    IrcitUiRequestLayout(parent_ptr);
  }

  if (IrcitUiFocus && sInSubtree(node_ptr, IrcitUiFocus)) IrcitUiSetFocus(NULL);
  if (sCtxTarget && sInSubtree(node_ptr, sCtxTarget)) sCtxTarget = NULL;

  if (sDragNode   && sInSubtree(node_ptr, sDragNode))   sDragNode   = NULL;
  if (sSelNode    && sInSubtree(node_ptr, sSelNode))    sSelNode    = NULL;
  if (sScrollDrag && sInSubtree(node_ptr, sScrollDrag)) sScrollDrag = NULL;
  sDirtyListForget(node_ptr);

  // sFreeTree is the sweep that frees the nodes, their child arrays and their per-kind state, and it delwins defensively on the way.
  sFreeTree(node_ptr);
  return 0;
}

PUBLIC_API void
IrcitUiButtonInput(IrcitUiNode *node_ptr, int key)
{
  IrcitUiNode *root = IrcitUiScreen.root;
  IrcitUiNode *st;
  if (key != KEY_MOUSE && key != '\n' && key != '\r' && key != KEY_ENTER) return;
  if (IrcitUiDeclLayoutTemplate(node_ptr) && IrcitUiDeclLayoutTemplate(node_ptr)[0]) {
    IrcitUiNode *exist = IrcitUiFind(root, IrcitUiDeclLayoutTemplate(node_ptr));
    if (exist) IrcitUiPopStacked(exist);
    else {
      const IrcitUiDataBindDescriptor *model_ptr = IrcitUiModel();
      const IrcitUiNode *              ref       = NULL;
      size_t                           i;
      for (i = 0; i < model_ptr->template_count; i++) {
        const IrcitUiNode *tp = model_ptr->templates[i];
        if (tp && tp->common.id && strcmp(tp->common.id, IrcitUiDeclLayoutTemplate(node_ptr)) == 0) ref = tp;
      }
      if (ref && (ref->common.flags & IRCITUI_F_DRAGGABLE)) IrcitUiInflateFloat(root, IrcitUiDeclLayoutTemplate(node_ptr));
      else IrcitUiInflateAnchored(root, IrcitUiDeclLayoutTemplate(node_ptr), node_ptr);
    }
    return;
  }
  if ((node_ptr->common.id && strstr(node_ptr->common.id, "close")) || (node_ptr->common.text &&
    strcasecmp(node_ptr->common.text, "close") == 0) || (node_ptr->common.id && strcmp(node_ptr->common.id, "ctx_copy")
    == 0) || (node_ptr->common.id && strcmp(node_ptr->common.id, "ctx_paste") == 0) || (node_ptr->common.id &&
    strcmp(node_ptr->common.id, "ctx_clear") == 0) || (node_ptr->common.id && strcmp(node_ptr->common.id, "ctx_selall")
    == 0)) {
    {
      IrcitUiNode *tgt = sCtxTarget ? sCtxTarget : IrcitUiFocus;
      if (node_ptr->common.id && strcmp(node_ptr->common.id, "ctx_copy") == 0) IrcitUiClipCopy(tgt);
      else if (node_ptr->common.id && strcmp(node_ptr->common.id, "ctx_paste") == 0) IrcitUiClipPaste(tgt);
      else if (node_ptr->common.id && strcmp(node_ptr->common.id, "ctx_clear") == 0) IrcitUiClipClear(tgt);
      else if (node_ptr->common.id && strcmp(node_ptr->common.id, "ctx_selall") == 0 && tgt) {
        if (!IrcitUiEditTextSelectAll(tgt)) {
          const char *s = tgt->common.interaction_defaults.user ?
                            (char*)tgt->common.interaction_defaults.user :
                            (tgt->common.text ? tgt->common.text : "");
          tgt->container.sel0 = 0;
          tgt->container.sel1 = (int)strlen(s);
        }
        IrcitUiInvalidate(tgt);
      }

      IrcitUiCloseTopPopup();
      // Focus was on this button, which lives in the layer just popped, so re-resolve it into whatever is on top now.
      IrcitUiFocusTop();
    }
    return;
  }
  st = IrcitUiFind(root, "status");
  if (st) {
    static char echo[PRIV_CONFIG_DEFAULT_STATUS_ECHO_SZ];
    snprintf(echo, sizeof echo, "btn %s", node_ptr->common.id ? node_ptr->common.id : "?");
    st->common.interaction_defaults.user = echo;
    // The bytes behind the alias changed; the node's fields did not.
    IrcitUiInvalidate(st);
  }
}

static void
sApplyDef(IrcitUiNode *node_ptr, const IrcitUiWidgetDef *def_ptr)
{
  static const IrcitUiWidgetDef z;
  if (!def_ptr) def_ptr = &z;
  node_ptr->common.id               = def_ptr->id ? def_ptr->id : "anon";
  node_ptr->common.kind             = def_ptr->kind;
  node_ptr->common.text             = def_ptr->text ? def_ptr->text : "";
  node_ptr->common.layout_w.mode    = def_ptr->width_mode;
  node_ptr->common.layout_w.cells   = def_ptr->width;

  node_ptr->common.layout_w.percent = def_ptr->width_percent;
  node_ptr->common.layout_h.mode    = def_ptr->height_mode;
  node_ptr->common.layout_h.cells   = def_ptr->height;
  node_ptr->common.layout_h.percent = def_ptr->height_percent;
  node_ptr->common.flags            = def_ptr->flags ? def_ptr->flags : (IRCITUI_F_VISIBLE | IRCITUI_F_ENABLED);
  node_ptr->common.fg               = def_ptr->fg;
  node_ptr->common.bg               = def_ptr->bg;

  node_ptr->common.theme_class      = def_ptr->theme_class;
  node_ptr->common.bdr              = def_ptr->bdr ? def_ptr->bdr : COLOR_WHITE;

  /* The two a def may carry are overrides now, not node fields (§7): written only
     where the kind has one and only when the def asks for something other than the
     default, because "unset" and the default are the same request. */
  if (def_ptr->mark_fg >= 0) {
    IrcitUiDeclOverrides *over = sEnsureOverrides(node_ptr);
    if (over) over->mark_fg = def_ptr->mark_fg;
  }
  // The framework's own listeners, bound here for every node the C factory builds.
  IrcitUiInteractionBindDefaults(&node_ptr->common.interaction_defaults);
  node_ptr->common.gravity = IRCITUI_G_START;
  node_ptr->common.layout_gravity = IRCITUI_G_START;

  if (def_ptr->orientation != IRCITUI_VERT) {
    IrcitUiDeclOverrides *over = sEnsureOverrides(node_ptr);
    if (over) over->orientation = def_ptr->orientation;
  }
  node_ptr->common.c_top_top = node_ptr->common.c_top_bot = node_ptr->common.c_bot_top = node_ptr->common.c_bot_bot =
    IRCITUI_UNSET;
  node_ptr->common.c_start_start = node_ptr->common.c_start_end = node_ptr->common.c_end_start = node_ptr->common.
    c_end_end                    = IRCITUI_UNSET;
}

static IrcitUiNode *
sNodeFromDef(const IrcitUiWidgetDef *def_ptr)
{
  IrcitUiNode *n = calloc(1, sizeof *n);
  if (!n) return NULL;
  sApplyDef(n, def_ptr);
  // The clone path is not the only way a node is born; a def-built field needs its state too.
  sAllocWidgetState(n);
  return n;
}

PUBLIC_API IrcitUiNode *
IrcitUiRecyclerCreate(const char *id_ptr, const char *template_id_ptr, int row_height)
{
  IrcitUiWidgetDef      d = {0};
  IrcitUiNode *         n;
  IrcitUiRecyclerState *recycler;

  // No defaults for the two that matter: a recycler with no id cannot be found and one with no template has no rows to clone, so neither is worth inventing.
  if (!id_ptr || !template_id_ptr) return NULL;

  d.id    = id_ptr;
  d.kind  = IRCITUI_RECYCLER;
  d.text  = "";
  d.flags = IRCITUI_F_VISIBLE | IRCITUI_F_ENABLED;

  n = sNodeFromDef(&d); /* which is also what allocates the widget_state */
  if (!n) return NULL;


  recycler = n->common.widget_state;
  if (recycler) {
    recycler->template_id       = template_id_ptr;
    recycler->item_fixed_height = row_height <= 0 ? 1 : row_height;

    recycler->sticky_type = -1;
    // The same framework default the clone path applies. Set here as well so a recycler built through the C API highlights without configuration.
    recycler->highlight_attr_mask = A_REVERSE;
  }
  return n;
}

/* Pull a recycler's viewport the shortest way that brings the cursor back inside
 * it. The one copy of fork A's rule, called from the input route and from
 * IrcitUiRecyclerSetCursor: "the cursor is always visible" is a property of the
 * state, so it cannot be a thing one caller does and another forgets.
 *
 * visible_rows, not the pool: the pool carries SS13's two-row cache margin, and
 * following with it would let the cursor come to rest in that margin, scrolled
 * out of sight. Before the first pass visible_rows is 0, and the cursor is left
 * where the pass will clamp it. */
static void
sCursorFollow(IrcitUiNode *node_ptr, IrcitUiRecyclerState *state_ptr)
{
  (void)node_ptr;
  if (!state_ptr->visible_rows) return;
  if (state_ptr->cursor_index < state_ptr->scroll_offset_y) {
    state_ptr->scroll_offset_y = state_ptr->cursor_index;
  }
  else if (state_ptr->cursor_index >= state_ptr->scroll_offset_y + state_ptr->visible_rows) {
    state_ptr->scroll_offset_y = state_ptr->cursor_index - state_ptr->visible_rows + 1;
  }
}

/* Apply what a mode says the selection IS when the cursor has moved.
 *
 * Under SINGLE the selection is the cursor: the marked row is wherever the cursor
 * went, which is what makes an arrow key move the highlight rather than move
 * something invisible. Under MULTI the two are independent and the marks stay
 * where they were put.
 *
 * Called from every place the cursor moves -- the input route, the click and
 * IrcitUiRecyclerSetCursor -- so the three cannot leave different states behind.
 * It sets rather than toggles: a mode whose selection is the cursor cannot
 * unmark the row the cursor is on and still be that. */
static void
sCursorMoved(IrcitUiNode *node_ptr, IrcitUiRecyclerState *state_ptr)
{
  if (state_ptr->selection_mode != IRCITUI_SELECT_SINGLE) return;
  IrcitUiRecyclerSelect(node_ptr, state_ptr->cursor_index, 1);
}

/* Grow a recycler's selection bit-array to hold @p count bits.
 *
 * Returns the array, or NULL if it cannot be provided. capacity is in words and
 * is never allowed to shrink: a dataset that got smaller has left bits above it
 * that are meaningless rather than wrong, and reallocating down on every tick of
 * a shrinking item_count would be work for nothing.
 *
 * The newly added words are zeroed. realloc does not zero, and a stale bit there
 * would mark a row the application never selected -- which is the kind of defect
 * that shows up as a highlight on random data rows while scrolling. */
static uint32_t *
sBitsEnsure(IrcitUiRecyclerState *state_ptr, size_t count)
{
  size_t    words = (count + 31u) / 32u;
  uint32_t *grown;

  if (words == 0) return NULL;
  if (words <= state_ptr->bit_words) return state_ptr->selected_bits;
  grown = realloc(state_ptr->selected_bits, words * sizeof *grown);
  if (!grown) return NULL;
  memset(grown + state_ptr->bit_words, 0, (words - state_ptr->bit_words) * sizeof *grown);
  state_ptr->selected_bits = grown;
  state_ptr->bit_words     = words;
  return grown;
}

/* Whether @p index can be selected at all: the node must be a recycler with a
   state, and the index must be inside the dataset. An index outside it names no
   row, so a bit for it would be a mark on nothing -- and it would survive a
   later item_count growth to become a mark on the wrong row. */
static int
sSelectableIndex(const IrcitUiNode *node_ptr, const IrcitUiRecyclerState *state_ptr, size_t index)
{
  if (!node_ptr || node_ptr->common.kind != IRCITUI_RECYCLER || !state_ptr) return 0;
  return index < state_ptr->adapter.item_count;
}

PUBLIC_API int
IrcitUiRecyclerSelect(IrcitUiNode *node_ptr, size_t index, int selected)
{
  IrcitUiRecyclerState *state_ptr = node_ptr ? node_ptr->common.widget_state : NULL;
  uint32_t             *bits;
  uint32_t              mask;
  int                   was;

  if (!sSelectableIndex(node_ptr, state_ptr, index)) return 0;
  if (state_ptr->selection_mode == IRCITUI_SELECT_NONE) return 0;
  bits = sBitsEnsure(state_ptr, state_ptr->adapter.item_count);
  if (!bits) return 0;
  mask = 1u << (index & 31u);
  was  = (bits[index >> 5] & mask) != 0;
  if (was == (selected != 0)) return 0;

  if (selected && state_ptr->selection_mode == IRCITUI_SELECT_SINGLE) {
    memset(bits, 0, state_ptr->bit_words * sizeof *bits);
  }
  if (selected) bits[index >> 5] |= mask;
  else bits[index >> 5] &= ~mask;
  // The set changed, and what changed on screen is which rows carry the highlight. No box moves, so this is a repaint rather than a relayout.
  IrcitUiInvalidate(node_ptr);
  return 1;
}

/* Mark or unmark every DATASET index in [start, end], inclusive.
 *
 * The contiguous half of SS25. A range is applied by walking it and setting each
 * bit, not by clearing a word span and masking the ends -- the O(N/32) note in
 * SS25's reference describes the optimised form, and it is not taken here for the
 * same reason SS17.2's word-scan was rewritten only once it had a number behind
 * it: a range is bounded by what the user dragged over, and an unmeasured
 * micro-optimisation inside an input path is harder to be sure of than a loop
 * that reads exactly like what it does.
 *
 * THE MODE IS CHECKED HERE, in one place, for the reason IrcitUiRecyclerSelect
 * checks it in one place: a range is a MULTI idea. Under SINGLE the selection is
 * the cursor and a range would contradict it; under NONE there is no selection to
 * extend. Both are refusals, not errors -- there is simply nothing this call
 * could correctly do.
 *
 * @p start and @p end are given in either order, because a range dragged upwards
 * arrives backwards and having every caller sort it is how one of them forgets.
 * @p end is clamped to the dataset; @p start is not, and does not need to be -- a
 * start past the end leaves an empty span after the sort, which sets nothing and
 * reports 0.
 *
 * Returns whether anything changed, so a caller can skip a repaint. */
PUBLIC_API int
IrcitUiRecyclerSelectRange(IrcitUiNode *node_ptr, size_t start, size_t end, int selected)
{
  IrcitUiRecyclerState *state_ptr = node_ptr ? node_ptr->common.widget_state : NULL;
  uint32_t             *bits;
  size_t                i;
  int                   altered = 0;

  if (!node_ptr || node_ptr->common.kind != IRCITUI_RECYCLER || !state_ptr) return 0;
  if (state_ptr->selection_mode != IRCITUI_SELECT_MULTI) return 0;
  if (!state_ptr->adapter.item_count) return 0;

  if (start > end) {
    size_t swap = start;
    start       = end;
    end         = swap;
  }
  if (end >= state_ptr->adapter.item_count) end = state_ptr->adapter.item_count - 1;
  if (start > end) return 0;

  bits = sBitsEnsure(state_ptr, state_ptr->adapter.item_count);
  if (!bits) return 0;

  for (i = start; i <= end; i++) {
    uint32_t mask = 1u << (i & 31u);
    int      was  = (bits[i >> 5] & mask) != 0;
    if (was == (selected != 0)) continue;
    if (selected) bits[i >> 5] |= mask;
    else bits[i >> 5] &= ~mask;
    altered = 1;
  }
  // Repaint, not relayout: the set changed and what changed on screen is which rows carry the highlight. No box moves .
  if (altered) IrcitUiInvalidate(node_ptr);
  return altered;
}

PUBLIC_API int
IrcitUiRecyclerIsSelected(IrcitUiNode *node_ptr, size_t index)
{
  const IrcitUiRecyclerState *state_ptr = node_ptr ? node_ptr->common.widget_state : NULL;

  if (!sSelectableIndex(node_ptr, state_ptr, index)) return 0;
  if (!state_ptr->selected_bits) return 0;
  return (state_ptr->selected_bits[index >> 5] & (1u << (index & 31u))) != 0;
}

PUBLIC_API void
IrcitUiRecyclerClearSelection(IrcitUiNode *node_ptr)
{
  IrcitUiRecyclerState *state_ptr;

  if (!node_ptr || node_ptr->common.kind != IRCITUI_RECYCLER) return;
  state_ptr = node_ptr->common.widget_state;
  if (!state_ptr || !state_ptr->selected_bits) return;
  // The capacity is kept: the next selection of the same dataset would only have to grow it back, and the bits are what carry meaning, not the buffer.
  memset(state_ptr->selected_bits, 0, state_ptr->bit_words * sizeof *state_ptr->selected_bits);
  IrcitUiInvalidate(node_ptr);
}

PUBLIC_API IrcitUiNode *
IrcitUiRecyclerRowAt(IrcitUiNode *node_ptr, size_t index)
{
  const IrcitUiRecyclerState *state_ptr = node_ptr ? node_ptr->common.widget_state : NULL;
  size_t                      slot;

  if (!sSelectableIndex(node_ptr, state_ptr, index)) return NULL;
  // Before the offset, or past the pool, the row is simply not realised: the widget holds a viewport's worth of nodes, so an index outside it has no node to return.
  if (index < state_ptr->scroll_offset_y) return NULL;
  slot = index - state_ptr->scroll_offset_y;

  if (slot >= sPoolRowCount(node_ptr, state_ptr)) return NULL;
  return node_ptr->container.children[slot];
}

PUBLIC_API size_t
IrcitUiGetSelectedIndices(IrcitUiNode *node_ptr, size_t *out_ptr, size_t maxn)
{
  const IrcitUiRecyclerState *state_ptr = node_ptr ? node_ptr->common.widget_state : NULL;
  size_t                      n = 0, w, words;

  if (!sSelectableIndex(node_ptr, state_ptr, 0) || !state_ptr->selected_bits || !out_ptr) return 0;

  words = (state_ptr->adapter.item_count + 31u) / 32u;
  for (w = 0; w < words && n < maxn; w++) {
    uint32_t word = state_ptr->selected_bits[w];
    unsigned bit;

    if (!word) continue;

    if (w + 1u == words) {
      unsigned used = (unsigned)(state_ptr->adapter.item_count & 31u);
      if (used) word &= ((uint32_t)1u << used) - 1u;
    }
    for (bit = 0; word && n < maxn; bit++, word >>= 1u) {
      if (word & 1u) out_ptr[n++] = (size_t)w * 32u + bit;
    }
  }
  return n;
}

PUBLIC_API void
IrcitUiRecyclerSetSelectionMode(IrcitUiNode *node_ptr, IrcitUiSelectionMode mode)
{
  IrcitUiRecyclerState *state_ptr;

  if (!node_ptr || node_ptr->common.kind != IRCITUI_RECYCLER) return;
  state_ptr = node_ptr->common.widget_state;
  if (!state_ptr || state_ptr->selection_mode == mode) return;
  state_ptr->selection_mode = mode;

  if (state_ptr->selected_bits && mode != IRCITUI_SELECT_MULTI) {
    int keep = IrcitUiRecyclerIsSelected(node_ptr, state_ptr->cursor_index);
    memset(state_ptr->selected_bits, 0, state_ptr->bit_words * sizeof *state_ptr->selected_bits);
    if (mode == IRCITUI_SELECT_SINGLE && keep && sSelectableIndex(node_ptr, state_ptr, state_ptr->cursor_index)) {
      state_ptr->selected_bits[state_ptr->cursor_index >> 5] |= 1u << (state_ptr->cursor_index & 31u);
    }
  }
  IrcitUiInvalidate(node_ptr);
}

PUBLIC_API IrcitUiSelectionMode
IrcitUiRecyclerSelectionMode(IrcitUiNode *node_ptr)
{
  const IrcitUiRecyclerState *state_ptr = node_ptr ? node_ptr->common.widget_state : NULL;
  return state_ptr ? state_ptr->selection_mode : IRCITUI_SELECT_NONE;
}

PUBLIC_API size_t
IrcitUiRecyclerCursor(IrcitUiNode *node_ptr)
{
  const IrcitUiRecyclerState *state_ptr = node_ptr ? node_ptr->common.widget_state : NULL;
  return state_ptr ? state_ptr->cursor_index : 0;
}

PUBLIC_API void
IrcitUiRecyclerSetCursor(IrcitUiNode *node_ptr, size_t index)
{
  IrcitUiRecyclerState *state_ptr;

  if (!node_ptr || node_ptr->common.kind != IRCITUI_RECYCLER) return;
  state_ptr = node_ptr->common.widget_state;
  if (!state_ptr || !state_ptr->adapter.item_count) return;
  if (index >= state_ptr->adapter.item_count) index = state_ptr->adapter.item_count - 1;
  state_ptr->cursor_index = index;

  sCursorFollow(node_ptr, state_ptr);
  sCursorMoved(node_ptr, state_ptr);
  // The cursor's cell moved and no box did.
  IrcitUiInvalidate(node_ptr);
}

PUBLIC_API int
IrcitUiRecyclerSetAdapter(IrcitUiNode *node_ptr, const IrcitUiRecyclerAdapter *adapter_ptr)
{
  if (!node_ptr || node_ptr->common.kind != IRCITUI_RECYCLER || !adapter_ptr) return 0;

  if (!node_ptr->common.widget_state) sAllocWidgetState(node_ptr);
  if (!node_ptr->common.widget_state) return 0;
  ((IrcitUiRecyclerState *)node_ptr->common.widget_state)->adapter = *adapter_ptr;
  // On the model nothing reads the flag; on a live node this is what schedules the rebind. Called in both cases because the caller should not have to know which kind of node it is holding.
  IrcitUiRequestLayout(node_ptr);
  return 1;
}

PUBLIC_API int
IrcitUiRecyclerSetViewTypes(IrcitUiNode *node_ptr, const IrcitUiViewTypeMapping *maps_ptr, int count)
{
  IrcitUiRecyclerState *state_ptr;

  if (!node_ptr || node_ptr->common.kind != IRCITUI_RECYCLER) return 0;

  if (count < 0 || (count > 0 && !maps_ptr)) return 0;

  {
    int i;
    for (i = 0; i < count; i++) {
      if (!maps_ptr[i].template_id || !maps_ptr[i].template_id[0]) return 0;
    }
  }

  if (!node_ptr->common.widget_state) sAllocWidgetState(node_ptr);
  state_ptr = node_ptr->common.widget_state;
  if (!state_ptr) return 0;
  state_ptr->view_types   = maps_ptr;
  state_ptr->n_view_types = count;
  IrcitUiRequestLayout(node_ptr);
  return 1;
}

PUBLIC_API int
IrcitUiRecyclerSetStickyHeader(IrcitUiNode *node_ptr, int type_id)
{
  IrcitUiRecyclerState *state_ptr;
  int                   i;

  if (!node_ptr || node_ptr->common.kind != IRCITUI_RECYCLER) return 0;

  if (!node_ptr->common.widget_state) sAllocWidgetState(node_ptr);
  state_ptr = node_ptr->common.widget_state;
  if (!state_ptr) return 0;

  // A negative type_id turns pinning OFF, and is the only value that does not have to name something. Everything below is about the other case.
  if (type_id >= 0) {
    for (i = 0; i < state_ptr->n_view_types; i++) {
      if (state_ptr->view_types[i].type_id == type_id) break;
    }
    if (i >= state_ptr->n_view_types) return 0;
  }

  state_ptr->sticky_type              = type_id;

  // The header node itself is the layout pass's business: turning the feature on builds it on the next pass, and turning it off releases it there.
  IrcitUiRequestLayout(node_ptr);
  return 1;
}

/* --- SS26: the inline editable row ------------------------------------------
 *
 * A row of the list becomes a text field, the user types into it, and one
 * callback carries the result back to the application. The shape of the row is
 * the whole of the mechanism: the editing row is bound to a different registry
 * entry, so the layout pass rebuilds that one slot into an edit field and gives
 * it a fresh text buffer, and ending the edit lets the pass rebuild it back.
 * Nothing is mutated, and no node is added to or removed from the pool.
 *
 * That is what makes the feature safe in a virtualised list at all, and it is
 * the constraint SS26 opens with: a row node is a scavenger that will be bound to
 * a different dataset position by the next scroll, so an edit state parked on one
 * would follow the pool rather than the entry. Here it cannot -- the state lives
 * in the row's own text buffer, the pool never moves while the edit is open, and
 * the position, not the node, is what the commit names. */

/* End an edit, committing what was typed or discarding it.
 *
 * THE BUFFER IS READ HERE, BEFORE ANYTHING ASKS FOR A PASS, and that ordering is
 * the whole of this function's safety. The pass that follows unbinds the editing
 * position, which rebuilds that slot -- and rebuilding a slot frees the row it
 * replaces, text buffer and all. Handing the application a pointer into it after
 * the request would be a use-after-free with the shape of a race: the layout is
 * only a request, so the read would usually still land, and would land on freed
 * memory on the frames where the pass had already run.
 *
 * @p commit says which of the two endings this is. A cancel reaches the
 * application through nothing at all, which is what makes ESC mean "forget it"
 * rather than "store what I have". */
static void
sRecyclerEndEdit(IrcitUiNode *node_ptr, IrcitUiRecyclerState *state_ptr, int commit)
{
  // The position is taken before the state is cleared, because the callback names it and clearing first would hand the application the sentinel.
  size_t                position = state_ptr->editing_index;
  IrcitUiNode          *row      = IrcitUiRecyclerRowAt(node_ptr, position);
  const char           *row_text = IrcitUiEditTextGetValue(row);

  state_ptr->editing_index      = IRCITUI_NO_SECTION;
  state_ptr->edit_focus_pending = 0;

  if (sSelNode && row && sInSubtree(row, sSelNode)) sSelNode = NULL;

  IrcitUiSetFocus(node_ptr);

  if (commit && row_text && state_ptr->adapter.on_item_edited) {
    state_ptr->adapter.on_item_edited(position, row_text, state_ptr->adapter.user_dataset);
  }

  IrcitUiRequestLayout(node_ptr);
}

PUBLIC_API int
IrcitUiRecyclerSetEditType(IrcitUiNode *node_ptr, int type_id)
{
  IrcitUiRecyclerState *state_ptr;
  int                   i;

  if (!node_ptr || node_ptr->common.kind != IRCITUI_RECYCLER) return 0;
  if (!node_ptr->common.widget_state) sAllocWidgetState(node_ptr);
  state_ptr = node_ptr->common.widget_state;
  if (!state_ptr) return 0;
  if (state_ptr->editing_index != IRCITUI_NO_SECTION) sRecyclerEndEdit(node_ptr, state_ptr, 1);

  // A negative id turns the feature off, and is the only value that does not have to name something -- SS15's rule for the sticky type, for the same reason.
  if (type_id >= 0) {
    for (i = 0; i < state_ptr->n_view_types; i++) {
      if (state_ptr->view_types[i].type_id == type_id) break;
    }
    if (i >= state_ptr->n_view_types) return 0;
  }
  state_ptr->editing_type_id = type_id;
  return 1;
}

PUBLIC_API int
IrcitUiRecyclerBeginEdit(IrcitUiNode *node_ptr, size_t index)
{
  IrcitUiRecyclerState *state_ptr;
  const IrcitUiNode    *ref = NULL;
  int                   i;

  if (!node_ptr || node_ptr->common.kind != IRCITUI_RECYCLER) return 0;
  state_ptr = node_ptr->common.widget_state;
  if (!state_ptr || index >= state_ptr->adapter.item_count) return 0;

  if (state_ptr->editing_type_id < 0 || !state_ptr->adapter.on_item_edited) return 0;
  for (i = 0; i < state_ptr->n_view_types; i++) {
    if (state_ptr->view_types[i].type_id != state_ptr->editing_type_id) continue;
    ref = sRefOrNull(state_ptr->view_types[i].template_id);
    break;
  }
  if (!ref) return 0;

  if (state_ptr->editing_index != IRCITUI_NO_SECTION) sRecyclerEndEdit(node_ptr, state_ptr, 1);

  state_ptr->last_press_index = IRCITUI_NO_SECTION;
  state_ptr->last_press_ns    = 0;

  state_ptr->editing_index = index;
  IrcitUiRecyclerSetCursor(node_ptr, index);
  state_ptr->edit_focus_pending = 1;
  IrcitUiRequestLayout(node_ptr);
  return 1;
}

PUBLIC_API int
IrcitUiRecyclerCommitEdit(IrcitUiNode *node_ptr)
{
  IrcitUiRecyclerState *state_ptr;

  if (!node_ptr || node_ptr->common.kind != IRCITUI_RECYCLER) return 0;
  state_ptr = node_ptr->common.widget_state;
  if (!state_ptr || state_ptr->editing_index == IRCITUI_NO_SECTION) return 0;
  sRecyclerEndEdit(node_ptr, state_ptr, 1);
  return 1;
}

PUBLIC_API int
IrcitUiRecyclerCancelEdit(IrcitUiNode *node_ptr)
{
  IrcitUiRecyclerState *state_ptr;

  if (!node_ptr || node_ptr->common.kind != IRCITUI_RECYCLER) return 0;
  state_ptr = node_ptr->common.widget_state;
  if (!state_ptr || state_ptr->editing_index == IRCITUI_NO_SECTION) return 0;
  sRecyclerEndEdit(node_ptr, state_ptr, 0);
  return 1;
}

PUBLIC_API int
IrcitUiRecyclerIsEditing(IrcitUiNode *node_ptr)
{
  IrcitUiRecyclerState *state_ptr;

  if (!node_ptr || node_ptr->common.kind != IRCITUI_RECYCLER) return 0;
  state_ptr = node_ptr->common.widget_state;
  return state_ptr && state_ptr->editing_index != IRCITUI_NO_SECTION;
}

/* The keyboard half of SS26, at the top of IrcitUiDispatchKey because that is the
 * first place a key can be answered and the only place that sees it before the
 * routing matrix splits it.
 *
 * While a row is being edited the FOCUS IS THE FIELD, so the routing matrix
 * already sends the typing there -- left, right, backspace and the printable
 * characters need nothing from here. What it does not do is the three things
 * SS26's rulings give to the list rather than to the field:
 *
 *   Enter  commits. Without this it would reach the edit field's own handler,
 *          which is the command line's SEND -- it echoes the buffer to the status
 *          label and CLEARS it. That is the right meaning for a command line and
 *          the wrong one for a row, and it is the same field kind, so the
 *          difference has to be made here.
 *   ESC    cancels, and is consumed so that it does not also close a popup behind
 *          the list.
 *   Up/Down  are inert. SS26's ruling 3 freezes the list while editing, and these
 *          are the keys that would move it: the field declines them, so they would
 *          fall through the matrix to the recycler and scroll the row out from
 *          under the caret. Consumed rather than merely ignored, so that they do
 *          not reach an application listener behind the list either.
 *
 * Everything else is left to the normal path, which is what ruling 3 asks for:
 * the navigation keys target the text field and the list is frozen, so there is
 * nothing for this function to say about them. */
static int
sRecyclerEditKey(int key)
{
  IrcitUiNode          *field = IrcitUiFocus;
  IrcitUiNode          *list;
  IrcitUiRecyclerState *state_ptr;

  if (!field || field->common.kind != IRCITUI_EDIT_TEXT) return 0;
  for (list = field->common.parent; list; list = list->common.parent) {
    if (list->common.kind == IRCITUI_RECYCLER) break;
  }
  if (!list) return 0;
  state_ptr = list->common.widget_state;
  if (!state_ptr || state_ptr->editing_index == IRCITUI_NO_SECTION) return 0;

  if (key == KEY_ENTER || key == '\n' || key == '\r' || key == IRCITUI_KEY_CTRL_ENTER) {
    sRecyclerEndEdit(list, state_ptr, 1);
    return 1;
  }
  if (key == IRCITUI_KEY_ESC) {
    sRecyclerEndEdit(list, state_ptr, 0);
    return 1;
  }
  if (key == KEY_UP || key == KEY_DOWN || key == IRCITUI_KEY_SHIFT_UP || key == IRCITUI_KEY_SHIFT_DOWN) return 1;
  return 0;
}

PUBLIC_API int
IrcitUiRecyclerInput(IrcitUiNode *node_ptr, int key)
{
  IrcitUiRecyclerState *state_ptr;

  if (!node_ptr || node_ptr->common.kind != IRCITUI_RECYCLER) return 0;
  if (key != KEY_DOWN && key != KEY_UP && key != IRCITUI_KEY_SHIFT_DOWN && key != IRCITUI_KEY_SHIFT_UP &&
      key != IRCITUI_KEY_SPACE && key != KEY_ENTER && key != '\n' && key != '\r' &&
      key != IRCITUI_KEY_CTRL_ENTER) {
    return 0;
  }
  state_ptr = node_ptr->common.widget_state;
  if (!state_ptr || !state_ptr->adapter.item_count) return 0;

  if (state_ptr->editing_index != IRCITUI_NO_SECTION) return 0;

    if (key == IRCITUI_KEY_CTRL_ENTER) return IrcitUiRecyclerBeginEdit(node_ptr, state_ptr->cursor_index);

  // ENTER CHOOSES the cursor's row, the same act as clicking it.
  if (key == KEY_ENTER || key == '\n' || key == '\r') {
    return sRowActivated(node_ptr, state_ptr->cursor_index);
  }

  // SPACE is the same act as a click, and the mode decides what that is: under MULTI it toggles, under SINGLE the row is already the selection because the cursor is on it.
  if (key == IRCITUI_KEY_SPACE) {
    if (state_ptr->selection_mode == IRCITUI_SELECT_MULTI) {
      IrcitUiRecyclerSelect(node_ptr, state_ptr->cursor_index,
                            !IrcitUiRecyclerIsSelected(node_ptr, state_ptr->cursor_index));
    }
    else {
      IrcitUiRecyclerSelect(node_ptr, state_ptr->cursor_index, 1);
    }
    return 1;
  }

  /* THE ARROWS MOVE THE CURSOR, NOT THE VIEWPORT, and the viewport follows only
     when the cursor would leave it. That is fork A: the active row is always on
     screen, while a marked row may be scrolled far out of sight.
     The veto therefore means "the cursor cannot move" rather than "the viewport
     cannot move", which is why it fires at index 0 and at the last index instead
     of at the ends of the scroll range. */
  {
    int shift = (key == IRCITUI_KEY_SHIFT_DOWN || key == IRCITUI_KEY_SHIFT_UP);
    int down  = (key == KEY_DOWN || key == IRCITUI_KEY_SHIFT_DOWN);

    if (down) {
      if (state_ptr->cursor_index + 1 >= state_ptr->adapter.item_count) return 0;
    }
    else {
      if (state_ptr->cursor_index == 0) return 0;
    }

    if (shift && state_ptr->selection_mode == IRCITUI_SELECT_MULTI) {
      size_t was = state_ptr->cursor_index;

      if (!state_ptr->has_selection_anchor) {
        state_ptr->selection_anchor_index = was;
        state_ptr->has_selection_anchor   = 1;
      }
      state_ptr->cursor_index = down ? was + 1 : was - 1;
      (void)IrcitUiRecyclerSelectRange(node_ptr, state_ptr->selection_anchor_index, was, 0);
      (void)IrcitUiRecyclerSelectRange(node_ptr, state_ptr->selection_anchor_index, state_ptr->cursor_index, 1);
      sCursorFollow(node_ptr, state_ptr);
      IrcitUiRequestLayout(node_ptr);
      return 1;
    }

    if (!shift) state_ptr->has_selection_anchor = 0;
    if (down) state_ptr->cursor_index++;
    else state_ptr->cursor_index--;
  }

  sCursorFollow(node_ptr, state_ptr);
  sCursorMoved(node_ptr, state_ptr);

  IrcitUiRequestLayout(node_ptr);
  return 1;
}

PUBLIC_API IrcitUiNode *
IrcitUiNodeFromDef(const IrcitUiWidgetDef *def_ptr)
{
  if (!def_ptr) return NULL;

  return sNodeFromDef(def_ptr);
}

PUBLIC_API IrcitUiNode *
IrcitUiAddChild(IrcitUiNode *container_ptr, const IrcitUiWidgetDef *def_ptr)
{
  IrcitUiNode *c;
  if (!container_ptr || !IrcitUiIsContainer(container_ptr->common.kind) || !def_ptr) return NULL;
  /* A recycler's child array IS its viewport: entry i is row i, placed and
     rebound by sLayoutRecycler. An appended node would be laid out as a row and
     handed to the binder. Its rows are built from item_template, not declared. */
  if (container_ptr->common.kind == IRCITUI_RECYCLER) return NULL;
  /* A conversation window's child array is its VIEWPORT, which the widget
     synthesizes: the one entry is the matrix, and a host that appended a second
     node would have the packer place it as if it were part of the conversation.
     The widget's own synthesis goes through this same call, so what is refused is
     a FOREIGN child rather than any child. */
  if (container_ptr->common.kind == IRCITUI_CONVERSATION_WINDOW && def_ptr->kind != IRCITUI_MESSAGE_VIEWPORT)
    return NULL;
  c = sNodeFromDef(def_ptr);
  if (!c) return NULL;
  if (!sAttach(container_ptr, c)) {
    sFreeTree(c);
    return NULL;
  }
  return c;
}

PUBLIC_API int
IrcitUiAddChildNode(IrcitUiNode *container_ptr, IrcitUiNode *child_ptr)
{
  if (!container_ptr || !child_ptr) return 0;
  if (!IrcitUiIsContainer(container_ptr->common.kind)) return 0;
  // A recycler's child array is its viewport -- see IrcitUiAddChild.
  if (container_ptr->common.kind == IRCITUI_RECYCLER) return 0;

  if (child_ptr->common.parent) return 0;
  return sAttach(container_ptr, child_ptr);
}

PUBLIC_API IrcitUiNode *
IrcitUiAddButton(IrcitUiNode *container_ptr, const IrcitUiWidgetDef *def_ptr)
{
  IrcitUiWidgetDef d = def_ptr ? *def_ptr : (IrcitUiWidgetDef){0};
  d.kind             = IRCITUI_BUTTON;
  d.flags            |= IRCITUI_F_VISIBLE | IRCITUI_F_ENABLED | IRCITUI_F_CLICKABLE;
  if (d.width_mode == 0 && d.width == 0) d.width_mode = IRCITUI_SIZE_WRAP;
  if (d.height_mode == 0 && d.height == 0) d.height_mode = IRCITUI_SIZE_WRAP;
  return IrcitUiAddChild(container_ptr, &d);
}

PUBLIC_API IrcitUiNode *
IrcitUiCreateFloatingWindow(const IrcitUiWidgetDef *def_ptr)
{
  IrcitUiNode *    n, *root;
  IrcitUiWidgetDef d = def_ptr ? *def_ptr : (IrcitUiWidgetDef){0};
  int              w, h, x, y;

  root = IrcitUiScreen.root;
  if (!root) return NULL;
  if (!IrcitUiIsContainer(d.kind)) d.kind = IRCITUI_LINEAR;
  d.flags |= IRCITUI_F_VISIBLE | IRCITUI_F_ENABLED | IRCITUI_F_BORDER | IRCITUI_F_DRAGGABLE | IRCITUI_F_STACKED;
  n       = sNodeFromDef(&d);
  if (!n) return NULL;

  n->common.flags |= IRCITUI_F_STACKED | IRCITUI_F_DRAGGABLE | IRCITUI_F_BORDER;
  sAttach(root, n);
  sPopupSize(n, &w, &h);
  if (d.width_mode == IRCITUI_SIZE_EXACT && d.width > 0) w = d.width;
  if (d.height_mode == IRCITUI_SIZE_EXACT && d.height > 0) h = d.height;
  x = (COLS - w) / 2;
  y = (LINES - h) / 2;
  sClampRect(&x, &y, w, h);
  IrcitUiLayout(n, x, y, w, h);
  IrcitUiDispatcherPostLifecycle(IrcitUiDispatcherInstance(), n, IRCITUI_EVENT_LIFECYCLE_INFLATE);
  IrcitUiRealize(n);

  IRCITUI_LOG_INFO("window loaded: id=%s from=WidgetDef at (%d,%d) size %dx%d under=%s",
                   n->common.id ? n->common.id : "(no id)", n->common.x, n->common.y, n->common.w, n->common.h,
                   root->common.id ? root->common.id : "(no id)");
  return n;
}

PUBLIC_API void
IrcitUiEmojiInput(IrcitUiNode *node_ptr, int key)
{
  const char *p;
  int         cols, col, row, cx, cy, i;
  mbstate_t   st;

  if (key != KEY_MOUSE || !node_ptr->common.text) return;
  cols = node_ptr->container.columns > 0 ? node_ptr->container.columns : PRIV_CONFIG_DEFAULT_GRID_COLS;
  cx   = sLastMouse.x - node_ptr->common.x;
  cy   = sLastMouse.y - node_ptr->common.y;
  if (cx < 0 || cy < 0) return;
  col = cx / 2;
  row = cy;
  i   = row * cols + col;
  p   = node_ptr->common.text;
  memset(&st, 0, sizeof st);
  while (*p && i >= 0) {
    wchar_t wc;
    size_t  k = mbrtowc(&wc, p, PRIV_CONFIG_DEFAULT_UTF8_MAX_BYTES, &st);
    char    tmp[PRIV_CONFIG_DEFAULT_UTF8_MAX_BYTES];
    if (k == (size_t)-1 || k == 0) break;
    memcpy(tmp, p, k);
    tmp[k] = 0;
    p      += k;
    if (wcwidth(wc) <= 0) continue;
    if (i == 0) {
      IrcitUiInputAppend(tmp);
      return;
    }
    i--;
  }
}

PUBLIC_API IrcitUiNode *
IrcitUiAddEmojiTable(IrcitUiNode *container_ptr, const char *emojis_ptr, int columns)
{
  IrcitUiWidgetDef d = {0};
  IrcitUiNode *    n;
  d.id          = "emoji_grid";
  d.kind        = IRCITUI_EMOJI_TABLE;
  d.text        = emojis_ptr ? emojis_ptr : "😀😁😂";
  d.width_mode  = IRCITUI_SIZE_WRAP;
  d.height_mode = IRCITUI_SIZE_WRAP;
  d.flags       = IRCITUI_F_VISIBLE | IRCITUI_F_ENABLED | IRCITUI_F_CLICKABLE;
  d.fg          = COLOR_WHITE;
  n             = IrcitUiAddChild(container_ptr, &d);
  if (n) n->container.columns = columns > 0 ? columns : PRIV_CONFIG_DEFAULT_GRID_COLS;
  return n;
}

PUBLIC_API IrcitUiNode *
IrcitUiShowEmojiPicker(IrcitUiNode *anchor_ptr)
{
  IrcitUiNode *root = IrcitUiScreen.root;
  IrcitUiNode *exist;
  if (!root) return NULL;
  exist = IrcitUiFind(root, "emoji_picker");
  if (exist) {
    IrcitUiPopStacked(exist);
    return NULL;
  }
  if (sLookupRef("emoji_picker")) {
    if (anchor_ptr) return IrcitUiInflateAnchored(root, "emoji_picker", anchor_ptr);
    return IrcitUiInflateFloat(root, "emoji_picker");
  }
  {
    IrcitUiWidgetDef win = {
      .id = "emoji_picker", .kind = IRCITUI_LINEAR, .width_mode = IRCITUI_SIZE_WRAP, .height_mode = IRCITUI_SIZE_WRAP,
      .flags = IRCITUI_F_VISIBLE | IRCITUI_F_ENABLED | IRCITUI_F_BORDER | IRCITUI_F_DRAGGABLE, .fg = COLOR_WHITE,
      .bdr = COLOR_YELLOW
    };
    IrcitUiNode *w = IrcitUiCreateFloatingWindow(&win);
    if (w) IrcitUiAddEmojiTable(w, "😀😁😂🤣😃😄😅😆😉😊😋😎😍😘🥰😜🤔🙄😏😢😭😡👍👎❤️🔥⭐🎉", PRIV_CONFIG_DEFAULT_GRID_COLS);
    return w;
  }
}

static char sClip[PRIV_CONFIG_DEFAULT_CLIPBUF_SZ];

PUBLIC_API void
IrcitUiCloseTopPopup(void)
{
  IrcitUiNode *top = IrcitUiTopLayer();
  if (top) IrcitUiPopStacked(top);
}

PUBLIC_API void
IrcitUiClipCopy(IrcitUiNode *node_ptr)
{
  const char *s;
  int         a, b;
  if (!node_ptr) return;
  s = node_ptr->common.interaction_defaults.user ?
        (char*)node_ptr->common.interaction_defaults.user :
        (node_ptr->common.text ? node_ptr->common.text : "");
  // An edit field's selection lives in its own state; every other kind keeps the container pair.
  if (!IrcitUiEditTextSelection(node_ptr, &a, &b)) {
    a = node_ptr->container.sel0;
    b = node_ptr->container.sel1;
  }
  if (a > b) {
    int t = a;
    a     = b;
    b     = t;
  }
  if (b > a && a >= 0 && b <= (int)strlen(s)) {
    int m = b - a;
    if (m >= (int)sizeof sClip) m = (int)sizeof sClip - 1;
    memcpy(sClip, s + a, (size_t)m);
    sClip[m] = 0;
  }
  else {
    strncpy(sClip, s, sizeof sClip - 1);
    sClip[sizeof sClip - 1] = 0;
  }
}

PUBLIC_API void
IrcitUiClipPaste(IrcitUiNode *node_ptr)
{
  if (!node_ptr || node_ptr->common.kind != IRCITUI_EDIT_TEXT) node_ptr = IrcitUiFind(IrcitUiScreen.root, "cmdline");
  if (!node_ptr) return;
  // Paste into the node this call resolved, not into whatever happens to hold focus: IrcitUiInputAppend re-derives its target from IrcitUiFocus, which sent a ctx_paste on an unfocused field into a different field.
  IrcitUiEditTextAliasSync(node_ptr);
  IrcitUiEditTextAppend(node_ptr, sClip);
}

PUBLIC_API void
IrcitUiClipClear(IrcitUiNode *node_ptr)
{
  if (!node_ptr) return;
  if (node_ptr->common.kind == IRCITUI_EDIT_TEXT) {
    IrcitUiEditTextClipClear(node_ptr);
  }
  else if (node_ptr->common.interaction_defaults.user) {
    // Writes through the alias, into a buffer this node does not own -- a host application's, or the demo's static blob. No field of the node changes here, and no observer inside the engine could see it.
    ((char*)node_ptr->common.interaction_defaults.user)[0] = 0;
  }
  node_ptr->container.sel0 = node_ptr->container.sel1 = 0;
  // Unconditional, because the selection reset above is itself a visible change on every kind.
  IrcitUiInvalidate(node_ptr);
}

PUBLIC_API IrcitUiNode *
IrcitUiShowContextMenu(IrcitUiNode *target_ptr)
{
  IrcitUiNode *    w, *exist, *root;
  IrcitUiWidgetDef b;
  int              x, y, ww, hh, nitems, texty;

  root = IrcitUiScreen.root;
  if (!root || !target_ptr) return NULL;
  exist = IrcitUiFind(root, "ctx_menu");
  if (exist) IrcitUiPopStacked(exist);

  sCtxTarget = target_ptr;
  texty      = (target_ptr->common.kind == IRCITUI_EDIT_TEXT || target_ptr->common.kind == IRCITUI_TEXT_VIEW);
  nitems     = texty ? (target_ptr->common.kind == IRCITUI_EDIT_TEXT ? 4 : 2) : 1;
  ww         = PRIV_CONFIG_DEFAULT_CTXMENU_W;
  hh         = nitems + 2;

  w = calloc(1, sizeof *w);
  if (!w) return NULL;
  w->common.id          = "ctx_menu";
  w->common.kind        = IRCITUI_LINEAR;
  // Children selectable, like the templated menus. This one is hand-built and never goes through sInflateAt, so it has no template to hang an on_inflate binding on and is focused explicitly below instead.
  w->common.flags       = IRCITUI_F_VISIBLE | IRCITUI_F_ENABLED | IRCITUI_F_BORDER | IRCITUI_F_STACKED | IRCITUI_F_CHILDREN_SELECTABLE;
  w->common.fg             = COLOR_WHITE;
  w->common.bg             = 0;
  w->common.bdr            = COLOR_WHITE;
  w->common.layout_w.mode  = IRCITUI_SIZE_EXACT;
  w->common.layout_w.cells = ww;
  w->common.layout_h.mode  = IRCITUI_SIZE_EXACT;
  w->common.layout_h.cells = hh;
  sAttach(root, w);

  memset(&b, 0, sizeof b);
  b.width_mode  = IRCITUI_SIZE_MATCH;
  b.height_mode = IRCITUI_SIZE_WRAP;
  b.fg          = COLOR_WHITE;
  b.bg          = 0;
  b.bdr         = COLOR_WHITE;
  b.flags       = IRCITUI_F_VISIBLE | IRCITUI_F_ENABLED | IRCITUI_F_CLICKABLE;

  if (texty) {
    b.id   = "ctx_copy";
    b.text = "Copy";
    IrcitUiAddButton(w, &b);
    b.id   = "ctx_selall";
    b.text = "Select all";
    IrcitUiAddButton(w, &b);
    if (target_ptr->common.kind == IRCITUI_EDIT_TEXT) {
      b.id   = "ctx_paste";
      b.text = "Paste";
      IrcitUiAddButton(w, &b);
      b.id   = "ctx_clear";
      b.text = "Clear";
      IrcitUiAddButton(w, &b);
    }
  }
  else {
    b.id   = "ctx_close";
    b.text = "Close";
    IrcitUiAddButton(w, &b);
  }

  x = sLastMouse.x;
  y = sLastMouse.y + 1;
  if (x + ww > COLS) x = COLS - ww;
  if (y + hh > LINES) y = sLastMouse.y - hh;
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  IrcitUiLayout(w, x, y, ww, hh);
  IrcitUiRealize(w);

  /* Focus it, so the keyboard reaches the menu on open the same way the templated ones get it through on_inflate: IrcitUiPopupFocusOnOpen. sCtxTarget
     was set above, before focus moved, and every ctx_ action reads sCtxTarget rather than IrcitUiFocus, so the clipboard target is unaffected by
     focus sitting on the menu. */
  IrcitUiFocusInLayer(w);

  return w;
}

PUBLIC_API void
IrcitUiDemoFill(void)
{
  IrcitUiNode *canvas, *body, *root;
  static char  blob[PRIV_CONFIG_DEFAULT_DEMO_BLOB_SZ];
  int          i, n = 0;

  root = IrcitUiScreen.root;
  if (!root) return;
  canvas  = IrcitUiFind(root, "canvas");
  body    = IrcitUiFind(root, "body");
  blob[0] = 0;
  for (i = 0; i < PRIV_CONFIG_DEFAULT_DEMO_LINES && n < (int)sizeof blob - PRIV_CONFIG_DEFAULT_DEMO_BLOB_RESERVE; i++) n
    += snprintf(blob + n, sizeof blob - (size_t)n, "line %02d  the quick brown fox 0123456789\n", i);
  if (canvas) {
    canvas->common.interaction_defaults.user = blob;
    canvas->common.text                      = blob;
    canvas->container.sel0                   = canvas->container.sel1 = 0;
    canvas->container.scroll_y               = 0;
    canvas->common.flags                     |= IRCITUI_F_SCROLLABLE;

    IrcitUiRequestLayout(canvas);
  }
  (void)body;
  // The request above is the whole of it; the frame measures.
  (void)root;
}


PUBLIC_API void
IrcitUiRadioItemInput(IrcitUiNode *node_ptr, int key)
{
  IrcitUiNode *g;
  int          i;
  (void)key;
  g = node_ptr ? node_ptr->common.parent : NULL;
  if (!g) return;
  for (i = 0; i < g->container.n_children; i++) g->container.children[i]->common.flags &= ~IRCITUI_F_CHECKED;
  node_ptr->common.flags |= IRCITUI_F_CHECKED;
  // The group, not the row: the tick that cleared and the tick that was set are both painted into the group's window.
  IrcitUiInvalidate(g);
}

PUBLIC_API void
IrcitUiCheckItemInput(IrcitUiNode *node_ptr, int key)
{
  if (!node_ptr) return;
  if (key == KEY_MOUSE) return; /* dispatch already toggled CHECKED */
  node_ptr->common.flags ^= IRCITUI_F_CHECKED;
  IrcitUiInvalidate(node_ptr);
}

PUBLIC_API int
IrcitUiGroupSelectedIndex(IrcitUiNode *group_ptr)
{
  int i;
  if (!group_ptr) return -1;
  for (i = 0; i < group_ptr->container.n_children; i++) if (group_ptr->container.children[i]->common.flags &
    IRCITUI_F_CHECKED) return i;
  return -1;
}

PUBLIC_API const char *
IrcitUiGroupSelectedValue(IrcitUiNode *group_ptr)
{
  int i = IrcitUiGroupSelectedIndex(group_ptr);
  if (i < 0 || !group_ptr->container.children[i]->common.text) return "";
  return group_ptr->container.children[i]->common.text;
}

PUBLIC_API int
IrcitUiGroupSelectedValues(IrcitUiNode *group_ptr, const char **out_ptr, int maxn)
{
  int i, n = 0;
  if (!group_ptr || !out_ptr || maxn < 1) return 0;
  for (i = 0; i < group_ptr->container.n_children && n < maxn; i++)
    if (group_ptr->container.children[i]->common.flags & IRCITUI_F_CHECKED)
      out_ptr[n++] = group_ptr->container.children[i]->common.text ? group_ptr->container.children[i]->common.text : "";
  return n;
}
