/**
 * @file ui_bindings.h
 * @brief The binding surface: every way an application attaches its own listener to a node.
 *
 * Copyright (C) 2015-2026  unfacd works
 *
 * This file is part of ircituilib source code.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ---
 *
 * THE APPLICATION'S WHOLE WAY IN, AND NOTHING ELSE. These declarations were gathered out of
 * ui.h, which still includes this header so that a consumer reaching for the umbrella goes on
 * compiling unchanged. The arrangement is ircitui_edit_text.h's, for the same reason.
 *
 * WHAT MAKES THEM ONE FAMILY. Each is a call an application makes to attach behaviour to a
 * node it does not hold a pointer to and cannot see inside. @ref IrcitUiNode is opaque, so an
 * id is the only handle it has; the binders take one, resolve it, and refuse rather than
 * defer when it does not name a node in the reference tree.
 *
 * THE SEAM, WHICH IS THE WHOLE DESIGN. A binder resolves its id against the reference MODEL
 * and writes that model node's @c interactions_user block -- not the screen tree's. The
 * mechanism is one line of @ref IrcitUiClone: the copy is wholesale, and what it clears is the
 * runtime state, so @c interactions_user is inherited by every clone at the instant it is
 * made. Two things follow, and neither is available to a binding made against the screen tree:
 * the binding survives the @ref IrcitUiScreenReset @ref IrcitUiMainLooper performs before its
 * first frame, and a template that has no live node yet can be bound -- which is the whole
 * point of @ref IrcitUiBindInflate, whose event exists to run when the popup is raised.
 * A binding is therefore made once, in @c main(), after @ref IrcitUiDataBind.
 *
 * @ref IrcitUiBindSeek is the exception, and its own declaration says why: a bar's callback
 * lives in per-clone @c widget_state, which the clone does rebuild, so there is no model slot
 * for it to inherit from.
 *
 * WHAT IS DELIBERATELY NOT HERE. @ref IrcitUiEventKeyCode and @ref IrcitUiEventScrollDelta
 * stay in ui.h. They read an event a listener was handed rather than attach one, and they are
 * named for the event family, not this one. A listener bound through this header still needs
 * them, and @ref IrcitUiEventKeyCode says so on its own declaration.
 */
#ifndef IRCITUILIB_UI_BINDINGS_H
#define IRCITUILIB_UI_BINDINGS_H

#include "ircituilib/ircituilib_defs.h"
#include "ircituilib/ircit_ui_node_type.h"
#include "ircituilib/ircit_ui_event_type.h"
#include "ircituilib/ircit_ui_event_dispatcher_type.h"
#include "ircituilib/ircit_ui_click_callback_type.h"
#include "ircituilib/ircit_ui_seek_callback_type.h"
/* The edit field's header owns @ref IrcitUiBindTextChanged and its callback type, because
   it owns that field's whole public surface and the binder was declared with it. Carried
   here so that this header is the complete binding surface and not seven eighths of one:
   a caller that reaches for the binders by name gets every one of them. */
#include "ircituilib/ircitui_edit_text.h"


/**
 * @brief Empty the application callback table.
 *
 * @details Drops every row, and every `user_ptr` held in one. Nothing is freed:
 *          the application owns whatever those pointers point at, and what the
 *          table borrows it borrows.
 *
 *          There is no single-row removal. An id is bound once — re-binding one
 *          means resetting and registering again — which is a consequence of the
 *          table being a singleton rather than a decision about the interface.
 *
 * @code{.c}
 * IrcitUiDataBind();
 * IrcitUiBindingReset();
 * IrcitUiBindClick("menu_tools", on_tools, NULL);
 *
 * // later: the application's policy changed
 * IrcitUiBindingReset();
 * IrcitUiBindClick("menu_tools", on_tools_now_allowed, NULL);
 * @endcode
 */
PUBLIC_API void
IrcitUiBindingReset(void);

/**
 * @brief Make @p on_click_ptr the application's verdict on activating the node
 *        whose id is @p node_id_ptr.
 *
 * @details The id must name a node in the reference tree — the model @ref
 *          IrcitUiDataBind returns, including its unbound template roots — so
 *          the call belongs after the framework has been fed, which is when the
 *          tree the id resolves against exists. A call made before it is refused
 *          rather than deferred: a registration that silently took effect later
 *          would fire at a moment the application did not choose and cannot see.
 *
 *          One registration covers every instance of the id. A popup is inflated
 *          by cloning its template, so the node the user activates while a layer
 *          is open is not the node the reference tree holds; keying on the id is
 *          what makes a single registration reach all of them, and what lets a
 *          registration survive a pop and the re-open that follows.
 *
 *          The callback decides whether the engine acts. Return
 *          @ref IRCITUI_CLICK_SINK and the engine does nothing further with the
 *          activation: no focus move, no @ref IRCITUI_F_CHECKED toggle, no call
 *          to the node's own @ref IrcitUiNode::on_input, no status echo, no menu
 *          dismissal. That is the whole of the permission model — an
 *          application refuses an action it owns and posts its own explanation.
 *
 * @param node_id_ptr  Id of the node to attach to. Must not be NULL.
 * @param on_click_ptr Callback to run on activation. Must not be NULL; to
 *                     detach, reset the table.
 * @param user_ptr     Passed through to the callback untouched.
 *
 * @return 1 when the row was added; 0 when it was refused — a NULL argument, an
 *         id that names no node in the reference tree, an id already bound, or
 *         @ref PRIV_CONFIG_DEFAULT_BINDING_CAP rows already registered. A refusal
 *         never displaces a row the application already holds.
 *
 * @note The table is a library-wide singleton, because @ref IrcitUiScreen is a
 *       global: one application has one table. One table per instance of the UI
 *       is the target, at which point these functions gain that instance as a
 *       first parameter.
 *
 * @code{.c}
 * static IrcitUiClickDisposition on_tools(IrcitUiNode *node_ptr, void *user_ptr) {
 *   (void)node_ptr; (void)user_ptr;
 *   if (!session_may_open_tools())
 *     return IRCITUI_CLICK_SINK;    // the panel will not open
 *   return IRCITUI_CLICK_CARRY;
 * }
 *
 * IrcitUiDataBind();                // the tree the id resolves against
 * IrcitUiBindingReset();
 * if (!IrcitUiBindClick("menu_tools", on_tools, NULL))
 *   fputs("menu_tools is not in the reference tree\n", stderr);
 * @endcode
 */
PUBLIC_API int
IrcitUiBindClick(const char *node_id_ptr, IrcitUiClickCallback on_click_ptr, void *user_ptr);
/**
 * @brief Wrap a node's callback type in the signature the framework calls.
 *
 * @details The engine invokes a key listener as
 *          `int (*)(IrcitUiEventDispatcher *, IrcitUiEvent *, void *)` -- the shape
 *          `ircit_ui_interaction_state_type.h` declares on `on_key_down`. That signature
 *          needs two types an application is not meant to hold, so this names it once and
 *          @ref IrcitUiBindKey takes it. Return non-zero to CONSUME the key: it is a veto
 *          interceptor, and a listener that returns 1 stops the framework acting on it.
 *
 * @param disp_ptr The dispatcher the key arrived on. Ignored if NULL.
 *
 * @param ev_ptr   The event. Its `key_code` is the curses key, numeric as `getch` gives it.
 *
 * @param user_ptr The pointer given to @ref IrcitUiBindKey. Handed back unread.
 *
 * @see IrcitUiBindKey
 */
typedef int (*IrcitUiKeyDownCallback)(IrcitUiEventDispatcher *disp_ptr,
                                      IrcitUiEvent *ev_ptr,
                                      void *user_ptr);
/**
 * @brief Route key input on a node to the application.
 *
 * @details The third of the binders, beside @ref IrcitUiBindClick and @ref IrcitUiBindSeek,
 *          and the one the input path needs: without it an application cannot see a keystroke
 *          at all, because a key handler is otherwise reachable only through
 *          `interactions_user.lifecycle_listeners.on_key_down` -- a structure the application
 *          does not hold, since @ref IrcitUiNode is opaque.
 *
 *          **The id is resolved once, against the reference model**, and the binding is
 *          refused if it does not resolve. Writing the model rather than the screen is what
 *          makes the binding outlive the tree: @ref IrcitUiClone copies `interactions_user`
 *          wholesale, so every clone inherits the listener at the moment it is made, and the
 *          binding survives @ref IrcitUiScreenReset and every popup that comes and goes.
 *
 *          **It is therefore callable from `main()`**, once @ref IrcitUiDataBind has built the
 *          model. Only @ref IrcitUiBindSeek cannot be, and it says why.
 *
 *          **A key listener is consulted for the focused node and its ancestors**, so binding
 *          a panel's root is how a panel takes the keys inside it -- and binding the reference
 *          model's root is how an application takes them everywhere.
 *
 *          **The node's `user` pointer is shared by all of its listeners.** Passing NULL
 *          clears it for every other listener bound on the same node; see the note on the
 *          implementation for why this call does not invent a per-binding slot.
 *
 * @param node_id_ptr Target node's id. Ignored if NULL or empty.
 *
 * @param callback_ptr The listener, or NULL to unbind.
 *
 * @param user_ptr Handed to @p callback_ptr unread on every call.
 *
 * @return Non-zero if the id resolved and the binding was made, 0 otherwise -- an id that
 *         does not resolve, or a model that has not been built yet.
 *
 * @see IrcitUiKeyDownCallback
 * @see IrcitUiBindClick
 */
PUBLIC_API int
IrcitUiBindKey(const char *node_id_ptr, IrcitUiKeyDownCallback callback_ptr, void *user_ptr);
/**
 * @brief Wrap a routed listener in the signature the framework calls.
 *
 * @details Every event the dispatcher routes to a node's `lifecycle_listeners` shares one
 *          signature -- `int (*)(IrcitUiEventDispatcher *, IrcitUiEvent *, void *)`. The
 *          dispatcher is the proof: it holds all six of them in a single local of that type
 *          (`ui_event_dispatcher.c:351`) and picks between them by tag.
 *          @ref IrcitUiKeyDownCallback already names it for the key family; this names it for
 *          the rest, after the struct the listeners live in rather than after an event kind.
 *
 *          Return non-zero to CONSUME the event. For @ref IrcitUiBindScroll that veto has
 *          teeth -- the framework's own scrolling is the fallback and does not run -- so a
 *          listener that consumes a wheel notch and does nothing else has swallowed it.
 *
 * @param disp_ptr The dispatcher. Never NULL.
 *
 * @param ev_ptr   The event. Its tag says which payload is meaningful, and the accessors are
 *                 how an application reads one: @ref IrcitUiEventKeyCode,
 *                 @ref IrcitUiEventGetMouseCoords, @ref IrcitUiEventScrollDelta, and
 *                 @ref IrcitUiNodeGetGeometry for the node's own rectangle.
 *
 * @param user_ptr The pointer given to the binder. Handed back unread.
 *
 * @return Non-zero to consume the event, 0 to let the framework's own handling continue.
 *
 * @see IrcitUiBindInflate
 * @see IrcitUiBindDeflate
 * @see IrcitUiBindScroll
 * @see IrcitUiBindResize
 */
typedef int (*IrcitUiLifecycleCallback)(IrcitUiEventDispatcher *disp_ptr, IrcitUiEvent *ev_ptr, void *user_ptr);
/**
 * @brief Route a node's inflation to the application.
 *
 * @details Fires when a node is instantiated from a blueprint -- a popup or floating window
 *          raised through @ref IrcitUiInflateFloat, @ref IrcitUiInflateAnchored or
 *          @ref IrcitUiCreateFloatingWindow (`ui_engine.c:4324`, `:5304`). Posted after
 *          @ref IrcitUiLayout and before @ref IrcitUiRealize, so the node has its rectangle and
 *          no window yet: @ref IrcitUiNodeGetGeometry answers, and anything needing a window
 *          does not.
 *
 *          **This is the binder the model seam exists for.** An inflated popup is a clone, made
 *          at the moment it is raised, so a binding written onto the screen tree could not exist
 *          before the node it names. Bound here by the *template's* id, it is already on the model
 *          node every clone is copied from, and is inherited at the instant of creation.
 *
 * @param node_id_ptr The template node's id, as the generator knows it. Resolved once, against
 *                    the reference model.
 *
 * @param callback_ptr The listener, or NULL to unbind.
 *
 * @param user_ptr Handed to @p callback_ptr unread.
 *
 * @return Non-zero if the id resolved and the binding was made, 0 otherwise.
 *
 * @see IrcitUiLifecycleCallback
 */
PUBLIC_API int
IrcitUiBindInflate(const char *node_id_ptr, IrcitUiLifecycleCallback callback_ptr, void *user_ptr);
/**
 * @brief Route a node's teardown to the application.
 *
 * @details Fires when a node is about to be stripped and freed, through @ref IrcitUiPopStacked
 *          or @ref IrcitUiNodeDestroy (`ui_engine.c:4387`, `:4439`). It runs **before** the
 *          deflate, so the node is still whole: geometry, id and flags all still answer.
 *
 *          The counterpart of @ref IrcitUiBindInflate and model-bound for the same reason. Note
 *          what a *screen-wide* teardown does and does not reach: @ref IrcitUiScreenReset frees
 *          the tree without posting per node, so this fires for a popup being popped or a node
 *          being destroyed, and not once per node for a reset.
 *
 * @param node_id_ptr The template node's id. Resolved once, against the reference model.
 *
 * @param callback_ptr The listener, or NULL to unbind.
 *
 * @param user_ptr Handed to @p callback_ptr unread.
 *
 * @return Non-zero if the id resolved and the binding was made, 0 otherwise.
 *
 * @see IrcitUiLifecycleCallback
 */
PUBLIC_API int
IrcitUiBindDeflate(const char *node_id_ptr, IrcitUiLifecycleCallback callback_ptr, void *user_ptr);
/**
 * @brief Route wheel motion over a node to the application.
 *
 * @details Posted for the nearest @ref IRCITUI_F_SCROLLABLE ancestor of whatever the pointer is
 *          over -- so binding a scroll pane's id catches a wheel anywhere inside it -- and posted
 *          **before** the offset moves. The framework's own shift is the fallback
 *          (@ref IrcitUiDefaultOnScroll), so a listener that returns non-zero has taken the
 *          notch and the pane stays where it is. Read the direction with
 *          @ref IrcitUiEventScrollDelta; negative is up.
 *
 *          A listener that consumes a notch to do something other than scroll should
 *          @ref IrcitUiRequestLayout whatever it changed, as the engine does.
 *
 * @param node_id_ptr The scrollable node's id. Resolved once, against the reference model.
 *
 * @param callback_ptr The listener, or NULL to unbind.
 *
 * @param user_ptr Handed to @p callback_ptr unread.
 *
 * @return Non-zero if the id resolved and the binding was made, 0 otherwise.
 *
 * @see IrcitUiEventScrollDelta
 * @see IrcitUiLifecycleCallback
 */
PUBLIC_API int
IrcitUiBindScroll(const char *node_id_ptr, IrcitUiLifecycleCallback callback_ptr, void *user_ptr);
/**
 * @brief Route a terminal resize to the application.
 *
 * @details Posted once per coalesced `KEY_RESIZE`, **on the screen root**, from the main loop's
 *          ingest path. The event carries no payload: the new size is the terminal's, so read it
 *          from `COLS`/`LINES` or from @ref IrcitUiNodeGetGeometry on a laid-out node, and note
 *          that at the moment this fires the relayout has not run yet -- binding the root and
 *          reading geometry gives the *old* rectangle.
 *
 *          **Only the layout root's id is accepted, and any other id is refused.** The framework
 *          relayouts centrally and once (`IrcitUiLayout(root, ...)` then `IrcitUiRealize(root)`),
 *          and @ref IrcitUiDefaultOnResize documents why a per-node default would do that once per
 *          node; the event is posted on that same one node. An inner widget has no isolated
 *          geometry loop -- layout is computed top-down from the root -- so a binding on one would
 *          return success and never fire. This binder refuses it instead, which turns the mistake
 *          into a 0 the caller must handle rather than a callback that silently never runs.
 *
 *          The root is identified by **node identity against the model's own root**, not by
 *          comparing against a literal id, so a layout that renames the root does not silently
 *          turn every resize binding into a refusal. Read the id with
 *          `IrcitUiNodeGetId(IrcitUiModelRoot())` if the caller needs to name it.
 *
 *          A host that wants to react per widget binds the root and calls down to its own
 *          sub-controllers from there.
 *
 * @param node_id_ptr The layout root's id. Any other id, including a valid inner node's, is
 *                    refused.
 *
 * @param callback_ptr The listener, or NULL to unbind.
 *
 * @param user_ptr Handed to @p callback_ptr unread.
 *
 * @return Non-zero if the id resolved to the model's root and the binding was made, 0 otherwise --
 *         including for an id that resolves to some other node.
 *
 * @see IrcitUiLifecycleCallback
 */
PUBLIC_API int
IrcitUiBindResize(const char *node_id_ptr, IrcitUiLifecycleCallback callback_ptr, void *user_ptr);
/**
 * @brief Make a bar seekable: report the user's gestures on it to @p callback_ptr.
 *
 * @details Bound by id, like @ref IrcitUiBindClick and for the same reason -- a node
 *          pointer captured in a binding has no defined lifetime, and a bar inside a popup
 *          panel is freed the moment the panel is closed.
 *
 *          **The host owns the value; the bar only reports.** A drag on a bound bar calls
 *          @p callback_ptr with the position the pointer is over, and the bar does NOT move
 *          itself. Moving it is the host's call, made by calling @ref IrcitUiBarSetValue
 *          from the callback -- which is idempotent and cheap, so the natural
 *          implementation is one line and costs nothing per motion.
 *
 *          That split is not ceremony. A bar's value frequently has an owner already: a
 *          media player's playback position belongs to the player, and a bar that sought
 *          itself would fight the clock advancing it, with the winner decided by which
 *          wrote last. With the host as the single writer there is no race to lose, and
 *          the policies that a self-seeking bar would have to grow -- snapping to a
 *          chapter, quantising to a step, refusing a position the medium cannot reach --
 *          fall out of the callback for free, because the host is already deciding the
 *          number and can simply decide a different one.
 *
 *          See @ref IrcitUiSeekCallback for the two phases and why the preview and the
 *          commit are distinguished.
 *
 *          **The id must resolve when this is called.** Binding a bar that does not exist
 *          yet fails and returns 0 -- so an application whose bars live in a popup panel
 *          binds them after raising it, exactly as it would have to for
 *          @ref IrcitUiBindClick on a node in the same panel.
 *
 * @param node_id_ptr The bar's id. Borrowed, and it must outlive the binding.
 *
 * @param callback_ptr Called on each motion and once on release. NULL unbinds.
 *
 * @param user_ptr Passed back to every call, untouched.
 *
 * @return 1 if the binding was recorded, 0 if no node with that id exists, it is not a bar,
 *         or the id is NULL or empty.
 *
 * @see IrcitUiBarSetValue
 * @see IrcitUiSeekPhase
 */
PUBLIC_API int
IrcitUiBindSeek(const char *node_id_ptr, IrcitUiSeekCallback callback_ptr, void *user_ptr);

/**
 * @brief Ask the application what to do about an activation of @p node_ptr.
 *
 * @details The engine calls this before it acts on an activation, and carries out
 *          the verdict. A node with no registered callback carries, so an
 *          application that registers nothing sees exactly the behaviour it saw
 *          before this existed.
 *
 *          Public because it is the seam rather than because an application
 *          needs to call it — the engine is the caller. It is exposed so that a
 *          caller driving the tree by hand, the classic loop, reaches the same
 *          decision the engine would instead of a second implementation of it.
 *
 * @param node_ptr The activated node. NULL carries.
 *
 * @return What the application registered for that node's id, or
 *         @ref IRCITUI_CLICK_CARRY when nothing is registered.
 *
 * @code{.c}
 * // What the engine does at the top of its activation branch:
 * if (IrcitUiClickDispatch(hit) == IRCITUI_CLICK_SINK)
 *   return 1;                       // consumed: nothing further happens
 * @endcode
 */
PUBLIC_API IrcitUiClickDisposition
IrcitUiClickDispatch(IrcitUiNode *node_ptr);

#endif //IRCITUILIB_UI_BINDINGS_H
