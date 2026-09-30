/**
 * @file ui_defaults_router.c
 * @brief The shared master router: one framework default per listener, shared
 *        by every widget kind, branching on IrcitUiNode::common.kind.
 *
 * The generator binds all eight listeners of IrcitUiInteractionState to a single
 * entry point each (tools/gen_ui.py, DEFAULT_LISTENERS), so every node in the
 * generated model - button, edit field, container, decorative bar - carries the
 * same eight pointers. The alternative, a function per kind per listener, costs
 * a distinct instruction stream for each combination; this costs eight, total,
 * and the CPU keeps those eight warm instead of walking a scattered table.
 *
 * The kind is therefore resolved at the top of each entry point rather than at
 * the point of binding, which is also what makes a framework-wide rule - a
 * shortcut every widget obeys - writable once instead of once per widget.
 *
 * THE RETURN VALUE IS A CONTRACT, NOT A STATUS. Non-zero means the event was
 * consumed: the dispatcher stops, runs no further default, and does not walk the
 * parent chain. Zero means it was not: the event bubbles to the parent, which is
 * how a container can act on a key no leaf wanted. An entry point that acts and
 * then returns 0 has swallowed nothing, and the action happens once per
 * ancestor.
 *
 * WHAT THESE ARE NOT. They are not the engine's present behaviour re-expressed.
 * The engine invokes its hooks as void (*)(IrcitUiNode *, int) - no dispatcher,
 * no event - and it performs focus movement, hit-testing, layer selection and
 * checked-state toggling itself, inline, before it reaches a hook. These entry
 * points take the dispatcher and the event the design calls for, and they
 * delegate to the same engine functions the engine calls today, so the two agree
 * on who does what: the delegate encodes the split, and the split is not
 * duplicated here.
 *
 * The engine posts to them through ui_event_dispatcher.c rather than calling
 * them, so what a node does is reached the same way whether the event arrived by
 * key, by click or by a lifecycle transition. Three things that were obligations
 * when this file was written:
 *
 *   1. MET. The engine's mouse path recorded its event in sLastMouse
 *      (ui_engine.c:19) because IrcitUiEmojiInput reads its click column out of
 *      that record rather than out of an event. The dispatcher now writes the
 *      record itself, in IrcitUiDispatcherPostClick, before any listener runs,
 *      so a click routed here describes itself and not an earlier click.
 *   2. OPEN, with no caller. The engine's wheel path relayouts and realises the
 *      tree after it moves container.scroll_y, because the offset is applied as
 *      a child offset during realize. IrcitUiDefaultOnScroll moves the offset
 *      and leaves the repaint to the caller, so that a per-node default does not
 *      relayout the whole tree once per ancestor - and until the wheel path
 *      posts through the dispatcher, nothing calls it at all. It is bound on
 *      every node and dormant, which is the honest state rather than a
 *      half-wired one.
 *   3. STILL WHERE IT WAS. The inline focus and toggle in the mouse path have
 *      not moved. The delegates depend on them: IrcitUiCheckItemInput declines
 *      to toggle on KEY_MOUSE precisely because dispatch already has.
 */
#include <curses.h>
#include "ui_priv.h"
#include "ircituilib/config_defs.h"

/* The node a default acts on.
 *
 * Not the third parameter. IrcitUiInteractionState.user is already spoken for in
 * the framework's own instance: it is the alias an edit field points at its ebuf
 * through and the alias the status line points at its echo buffer through, so a
 * default cannot read it as a node. The event carries the node instead, in
 * target, which is the resolver's answer to which component the event landed on.
 */
static IrcitUiNode *
sEventTarget(IrcitUiEvent *ev_ptr)
{
  return ev_ptr ? ev_ptr->target : NULL;
}

/* Whether the framework may act on a node at all.
 *
 * The engine used to apply a copy of this test at each of its hook sites - once
 * on the key path (the old ui_engine.c:1678) and never on the mouse path, where
 * the hit-test called the hook with no gate in front of it. Both sites now post
 * through the dispatcher instead, so this is the only copy left and the two
 * paths cannot disagree: a key and a click on the same node get the same answer.
 *
 * The mouse path's answer is the one that changed. A click on a CLICKABLE node
 * that was not ENABLED used to activate it; it no longer does. That is the
 * intended reading of IRCITUI_F_ENABLED - the flag says a node may receive
 * input, and a flag that only binds the keyboard says something narrower - but
 * it is a behaviour change, and it is the only one the router brings to these
 * paths. */
static int
sIsActionable(const IrcitUiNode *node_ptr)
{
  return node_ptr != NULL && (node_ptr->common.flags & IRCITUI_F_ENABLED) != 0;
}

/* The keys IrcitUiEditInput acts on, and only those.
 *
 * The list mirrors that function's own branches rather than being wider, because
 * this is what decides consumption: a key the field would ignore must not be
 * reported as swallowed, or it never reaches a shortcut bound on an ancestor.
 * The window for a printable character is the one the editor itself uses,
 * IRCITUI_KEY_SPACE up to but not including IRCITUI_KEY_DEL, so DEL and the
 * control codes below 32 fall through to the dedicated branches above it. */
static int
sEditAcceptsKey(int key)
{
  if (key == KEY_LEFT || key == KEY_RIGHT) return 1;
  if (key == KEY_BACKSPACE || key == IRCITUI_KEY_DEL || key == IRCITUI_KEY_BS) return 1;
  if (key == '\n' || key == '\r' || key == KEY_ENTER) return 1;
  return key >= IRCITUI_KEY_SPACE && key < IRCITUI_KEY_DEL;
}

/* The keys a two-state item toggles on. Enter and Space are the two a terminal
 * user reaches for; everything else is left to bubble, so a container can still
 * claim an arrow key that arrived on a check box.
 *
 * This is narrower than what the old hook did, and deliberately so. The engine
 * handed a check box whatever key reached it - sMenuActivate passed the key
 * through unchanged, so an idle blink tick reached IrcitUiCheckItemInput, which
 * toggles on anything that is not KEY_MOUSE. That is why IrcitUiDispatchKey has
 * to filter ERR at its own top (ui_engine.c:1373) to stop a tick flipping one.
 * Naming the two keys here is what makes the filter belt-and-braces rather than
 * load-bearing. */
static int
sToggleAcceptsKey(int key)
{
  return key == KEY_ENTER || key == '\n' || key == '\r' || key == IRCITUI_KEY_SPACE;
}

/* The kind-routed activation matrix: what a completed gesture does.
 *
 * Shared by on_click and by on_key_down's button case. That sharing is the point
 * of the master router - Enter on a button and a click on a button are the same
 * act, so they must not become two implementations that drift apart.
 *
 * The delegates are called with KEY_MOUSE because that is the gesture they
 * already model, and because they are written for it: IrcitUiButtonInput accepts
 * KEY_MOUSE, '\n', '\r' and KEY_ENTER alike, and IrcitUiCheckItemInput returns
 * early on KEY_MOUSE exactly because dispatch has already toggled the flag.
 * Handed a raw key they would redo work the engine has already done. */
static int
sActivateByKind(IrcitUiNode *node_ptr)
{
  switch (node_ptr->common.kind)
  {
    case IRCITUI_BUTTON:
      // The whole of what a click on a button does: inflate the layout_template it names, or run the window action its id names.
      IrcitUiButtonInput(node_ptr, KEY_MOUSE);
      return 1;

    case IRCITUI_CHECK_BOX:
    case IRCITUI_SWITCH:
      /* Two-state, so both toggle alike, and both read the same handler: it
         declines to flip the flag on KEY_MOUSE because the hit-test already
         did. */
      IrcitUiCheckItemInput(node_ptr, KEY_MOUSE);
      return 1;

    case IRCITUI_RADIO_BUTTON:
      /* Exclusive rather than toggling: this clears CHECKED across the siblings
         and sets it on this node, which is why it needs the parent and the check
         handler does not. */
      IrcitUiRadioItemInput(node_ptr, KEY_MOUSE);
      return 1;

    case IRCITUI_EMOJI_TABLE:
      // Appends the clicked glyph to the input field. Read obligation 1 in the file header before wiring this one up.
      IrcitUiEmojiInput(node_ptr, KEY_MOUSE);
      return 1;

    case IRCITUI_EDIT_TEXT:
      /* Unreachable from the engine's own click path, which excludes EDIT_TEXT
         by its predicate (ui_engine.c:1492) - a field is clicked to place the
         caret and to take focus, and both of those have already happened by the
         time a click is posted. It is answered here anyway because
         IrcitUiDispatcherPostClick is an entry point any caller may use, and an
         edit field that a click resolved to belongs to the field: consumed, so
         the click does not climb to a container and activate that instead.
         IrcitUiEditInput is not called: handed KEY_MOUSE it returns before doing
         anything but repairing its own caret, and that repair is the writer's
         business, not a click's. */
      return 1;

    default:
      /* Layout and decorative kinds - linear, frame, constraint, the groups,
         image, progress and seek bars, static text - have no activation. Not
         consumed, so the event climbs to an ancestor that may have one. */
      return 0;
  }
}

void
IrcitUiInteractionBindDefaults(IrcitUiInteractionState *state_ptr)
{
  if (!state_ptr) return;
  state_ptr->lifecycle_listeners.on_key_down      = IrcitUiDefaultOnKeyDown;
  state_ptr->lifecycle_listeners.on_click         = IrcitUiDefaultOnClick;
  state_ptr->lifecycle_listeners.on_scroll        = IrcitUiDefaultOnScroll;
  state_ptr->lifecycle_listeners.on_draw          = IrcitUiDefaultOnDraw;
  state_ptr->lifecycle_listeners.on_resize        = IrcitUiDefaultOnResize;
  state_ptr->lifecycle_listeners.on_focus_changed = IrcitUiDefaultOnFocusChanged;
  state_ptr->lifecycle_listeners.on_inflate       = IrcitUiDefaultOnInflate;
  state_ptr->lifecycle_listeners.on_deflate       = IrcitUiDefaultOnDeflate;
}

int
IrcitUiDefaultOnKeyDown(IrcitUiEventDispatcher *disp_ptr, IrcitUiEvent *ev_ptr, void *user_ptr)
{
  IrcitUiNode *node_ptr = sEventTarget(ev_ptr);
  int          key;

  /* Named so their absence does not read as an oversight. The dispatcher is the
     object a default would act back on the engine through, and this file has
     nothing to act back with: Layer 1 below is where that would happen, and
     Layer 1 is empty. */
  (void)disp_ptr;
  (void)user_ptr;

  if (!sIsActionable(node_ptr)) return 0;
  key = ev_ptr->key_code;

  /* --- LAYER 1: UNIVERSAL FRAMEWORK SHORTCUT INTERCEPTS ----------------
   *
   * This is where a rule every widget obeys, whatever its kind, belongs, and it
   * is empty because neither of the two rules the design names for it can be
   * written yet.
   *
   *   F10 -> exit. There is no dispatcher to signal through:
   *   IrcitUiDispatcher_exit does not exist, and struct IrcitUiEventDispatcher
   *   is declared but never instantiated anywhere in the tree, so there is no
   *   object with an exit flag to set. Exit is presently the application loop's
   *   own decision - examples/ui_main.c breaks its loop on KEY_F(10) - and
   *   moving that decision into the framework changes where the application's
   *   exit lives rather than filling in a default.
   *
   *   Tab -> focus cycling. Deliberately not built. The traversal order, the
   *   wrapping rule, and whether the cycle is scoped to a layer or to the whole
   *   tree are all unsettled, and a router that guessed would fix that guess
   *   into every widget at once. This is the one place it will be added when
   *   they are settled.
   *
   * Leaving the layer empty is not the same as leaving it out. F10 and Tab fall
   * through to the switch below, return 0, and bubble, so an application that
   * binds either one on a node today still receives it. */

  // --- LAYER 2: TYPE-SPECIFIC WIDGET ROUTING MATRIX --------------------
  switch (node_ptr->common.kind)
  {
    case IRCITUI_EDIT_TEXT:
      if (sEditAcceptsKey(key))
      {
        IrcitUiEditInput(node_ptr, key);
        /* Consumed even when the field declined to change - a full buffer
           swallows no character but must not bubble a plain 'a' to a container
           either, or the parent would act on ordinary typing. */
        return 1;
      }
      break;

    case IRCITUI_BUTTON:
      /* Enter is the button's second activation gesture, so it is routed to the
         click default rather than to a second copy of the button's behaviour.
         KEY_MOUSE is excluded: a real click resolves through on_click, and
         running both would activate the button twice. */
      if (key == '\n' || key == '\r' || key == KEY_ENTER) return IrcitUiDefaultOnClick(disp_ptr, ev_ptr, user_ptr);
      break;

    case IRCITUI_CHECK_BOX:
    case IRCITUI_SWITCH:
      if (sToggleAcceptsKey(key))
      {
        /* Not KEY_MOUSE, so the handler does the toggle itself - which is the
           one path where it is the router, not the hit-test, that changes the
           flag. */
        IrcitUiCheckItemInput(node_ptr, key);
        return 1;
      }
      break;

    case IRCITUI_RADIO_BUTTON:
      if (sToggleAcceptsKey(key))
      {
        IrcitUiRadioItemInput(node_ptr, key);
        return 1;
      }
      break;

    case IRCITUI_RECYCLER:
      /* A focused list owns the character stream. The scroll keys are the ones
         it can act on, and they keep SS13.3's veto: one that cannot move the
         viewport returns 0 so it bubbles. A printable character is claimed,
         because the alternative is not "nobody gets it" -- IrcitUiDispatchKey
         offers a declined key to every node in the tree, so it lands in the
         command line, and the user's typing goes to a field they never pointed
         at. Claiming it stops the scan there and leaves the key with the list.
         Function keys are deliberately NOT claimed: they are how the
         application is driven, and swallowing them here would take F4 and F10
         away for as long as the list held focus.
         The one case in this matrix that returns a value at all, for the reason
         the header gives: whether a scroll key should act depends on where the
         viewport already is, which a predicate here cannot see. */
      /* Offered every key: the input function answers for the ones it owns --
         the two scroll keys and SPACE, which marks the cursor row the way it
         toggles a check box -- and returns 0 for the rest, so that a scroll key
         which cannot move the cursor still falls through to the break below and
         bubbles. */
      if (IrcitUiRecyclerInput(node_ptr, key)) return 1;
      if (key >= 32 && key < 127) return 1;
      break;

    case IRCITUI_EMOJI_TABLE:
      /* Nothing. A grid of glyphs is driven by a pointer and has no keyboard
         gesture of its own: arrowing around it would need a cell cursor that
         does not exist, and today the table is not even focusable
         (IrcitUiIsFocusable). Left to bubble rather than swallowed, so a
         container can still use the keys. */
      break;

    default:
      // Passive layout views and container boxes ignore keys by default.
      break;
  }

  /* --- LAYER 3: HIERARCHICAL FALLBACK BUBBLING -------------------------
   *
   * The focused widget did not swallow the key, so the event climbs the parent
   * pointers to see whether a layout container or an application binding wants
   * it. 0 is what authorises that climb; returning anything else here would
   * strand every unhandled key at the leaf that happened to hold focus. */
  return 0;
}

int
IrcitUiDefaultOnClick(IrcitUiEventDispatcher *disp_ptr, IrcitUiEvent *ev_ptr, void *user_ptr)
{
  IrcitUiNode *node_ptr = sEventTarget(ev_ptr);

  (void)disp_ptr;
  (void)user_ptr;

  if (!sIsActionable(node_ptr)) return 0;
  return sActivateByKind(node_ptr);
}

int
IrcitUiDefaultOnScroll(IrcitUiEventDispatcher *disp_ptr, IrcitUiEvent *ev_ptr, void *user_ptr)
{
  IrcitUiNode *d;

  (void)disp_ptr;
  (void)user_ptr;

  if (!ev_ptr) return 0;

  /* The walk is the engine's: a wheel event that landed on a line inside a
     scroll pane resolves to the line, and the pane is the nearest scrollable
     ancestor. So the offset moves on the pane and not on whatever the pointer
     happened to be over. */
  for (d = ev_ptr->target; d; d = d->common.parent)
    if (d->common.flags & IRCITUI_F_SCROLLABLE) break;
  if (!d) return 0;

  /* Offset grows downwards, so a positive delta is the wheel moving down and a
     negative one is it moving up - the engine's convention
     (ui_engine.c:1406), which this matches rather than invents. */
  d->container.scroll_y += ev_ptr->scroll_delta;
  if (d->container.scroll_y < 0) d->container.scroll_y = 0;
  /* The upper bound is the engine's too, and is kept identical rather than
     corrected here: it bounds a row offset by the column count, which reads
     like a mistake, but correcting it is a change to scrolling behaviour and
     not a change to the router. See ui_engine.c:1408. */
  if (d->container.columns && d->container.scroll_y > d->container.columns) d->container.scroll_y = d->container.columns;
  return 1;
}

int
IrcitUiDefaultOnFocusChanged(IrcitUiEventDispatcher *disp_ptr, IrcitUiEvent *ev_ptr, void *user_ptr, int has_focus)
{
  IrcitUiNode *node_ptr = sEventTarget(ev_ptr);

  (void)disp_ptr;
  (void)user_ptr;

  if (!node_ptr) return 0;

  /* IrcitUiSetFocus owns the whole transition: it clears IRCITUI_F_FOCUSED on
     the outgoing node, sets it on the incoming one, repoints the global that
     both the painter and the key walk read, and resets the blink so a field
     does not gain focus mid-blink. Touching the flag directly here would leave
     three of those four out. */
  if (has_focus)
  {
    if (node_ptr != IrcitUiFocus) IrcitUiSetFocus(node_ptr);
    return 1;
  }
  if (node_ptr == IrcitUiFocus) IrcitUiSetFocus(NULL);
  return 1;
}

/* The four that return 0 for a reason.
 *
 * Not stubs. Each of these listeners has exactly one correct framework default
 * today, and it is "not consumed", because the work the listener is named for is
 * done centrally by the engine and not by the node. Reporting consumption from
 * any of them would suppress the engine pass that actually does the job.
 *
 *   on_draw     IrcitUiDraw paints the tree into per-node windows and ends in
 *               wnoutrefresh, so the doupdate the application makes is the only
 *               thing that reaches the terminal. A default that drew would draw
 *               to stdscr behind that discipline, and its cells would be
 *               overwritten by the next refresh - visible as flicker, not as
 *               content.
 *   on_resize   Relayout on KEY_RESIZE is IrcitUiLayout(root, 0, 0, COLS, LINES)
 *               followed by IrcitUiRealize(root), driven once from the
 *               application loop. A per-node default would relayout the whole
 *               tree once per node.
 *   on_inflate  The setup the listener is named for - buffer, caret and scroll
 *               reset - is the opposite of what the engine needs here. The edit
 *               buffer is inline in the node precisely so that typed text
 *               survives IrcitUiDeflate and the IrcitUiRealize that follows it
 *               (ui_priv.h:78), and a default that cleared it on every inflate
 *               would throw that away.
 *   on_deflate  Teardown is sFreeTree's, and it is the one place that knows a
 *               node came from IrcitUiClone rather than from the generated
 *               static tables, which decides whether its child array may be
 *               freed at all. Nothing a node-local default can see distinguishes
 *               the two.
 *
 * Each becomes real when the engine's central pass is split into a per-node hook
 * and the ownership question above lands in one place. */
int
IrcitUiDefaultOnDraw(IrcitUiEventDispatcher *disp_ptr, IrcitUiEvent *ev_ptr, void *user_ptr)
{
  (void)disp_ptr;
  (void)ev_ptr;
  (void)user_ptr;
  return 0;
}

int
IrcitUiDefaultOnResize(IrcitUiEventDispatcher *disp_ptr, IrcitUiEvent *ev_ptr, void *user_ptr)
{
  (void)disp_ptr;
  (void)ev_ptr;
  (void)user_ptr;
  return 0;
}

int
IrcitUiDefaultOnInflate(IrcitUiEventDispatcher *disp_ptr, IrcitUiEvent *ev_ptr, void *user_ptr)
{
  (void)disp_ptr;
  (void)ev_ptr;
  (void)user_ptr;
  return 0;
}

int
IrcitUiDefaultOnDeflate(IrcitUiEventDispatcher *disp_ptr, IrcitUiEvent *ev_ptr, void *user_ptr)
{
  (void)disp_ptr;
  (void)ev_ptr;
  (void)user_ptr;
  return 0;
}
