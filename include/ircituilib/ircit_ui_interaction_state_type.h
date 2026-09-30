/**
 * @file ircit_ui_interaction_state_type.h
 * @brief One node's listeners: the framework's set and the application's set.
 *
 * Public because the nodes carry it. Its definition was private until the
 * framework's router entry points were declared: @ref
 * IrcitUiInteractionBindDefaults takes this type by pointer, and
 * `generated/ui_generated.c` writes both blocks as a literal initialiser, so a
 * generated tree cannot be compiled by anything that cannot see the layout.
 */
#ifndef IRCITUI_IRCIT_UI_INTERACTION_STATE_TYPE_H
#define IRCITUI_IRCIT_UI_INTERACTION_STATE_TYPE_H

#include "ircituilib/config_defs.h"
#include "ircituilib/ircit_ui_event_dispatcher_type.h"
#include "ircituilib/ircit_ui_event_type.h"
#include "ircituilib/ircit_ui_node_type.h"

/*!
 * @struct IrcitUiInteractionState
 * @brief The listeners a node answers an event with.
 *
 * @details There are two instances of this type on every node, and the
 *          distinction between them is the whole of the framework's override
 *          rule.
 *
 *          `interaction_defaults` in @ref IrcitUiCommonState is what the widget
 *          set does natively -- the mechanics that make a text field accept
 *          typing or a button activate on Enter. The generator fills it, so no
 *          node is left without one and no application has to ask for it.
 *
 *          `interactions_user` in @ref IrcitUiNode is what the application
 *          decided about that node. The engine consults the user's listeners
 *          first and falls back to the framework's when they are absent or
 *          return 0, so an application overrides one behaviour without having to
 *          restate the rest.
 *
 *          The listener block is anonymous so both instances are reached the same
 *          way, by name, without an intermediate type for a caller to learn.
 *
 * @warning Publishing this type publishes its layout, including the inline
 *          @ref ebuf. This struct is a member of @ref IrcitUiNode, which stays
 *          opaque, so the size and member order here are now part of the
 *          contract: a member may not be reordered or resized without breaking
 *          every application compiled against an earlier revision.
 */
typedef struct IrcitUiInteractionState
{
  void *user;
  char  ebuf[PRIV_CONFIG_DEFAULT_EDIT_CAP]; /**< An edit field owns its text
      outright. The buffer is inline rather than heap so there is nothing to
      free, it survives IrcitUiClone by copy, and it is not lost when
      IrcitUiDeflate tears the window down and IrcitUiRealize rebuilds it -
      which is what lets typed text outlive a resize. */
  int   caret;

  // The separation of callbacks based on timing (Before/Veto/Interceptor vs After/Observer/Notification) is a fundamental pattern in the framework:
  // It ensures  control over state changes while enabling decoupled components to stay perfectly synchronised.
  // This the interception layer: Veto Interceptors: Return 1 when event is swallowed, or zero to let it through
  struct
  {
    /* Triggers when a standard keyboard character or functional escape
       sequence (like arrow keys, KEY_F(7), or KEY_ENTER) lands on a focused
       element. */
    int (*on_key_down)(IrcitUiEventDispatcher *disp, IrcitUiEvent *ev, void *user);
    /* Triggers when an ncurses KEY_MOUSE event is captured, and the hit-test
       resolver maps the (x, y) coordinate boundary back to a specific
       component. */
    int (*on_click)(IrcitUiEventDispatcher *disp, IrcitUiEvent *ev, void *user);
    /* Triggers when a mouse wheel motion button state (BUTTON4_PRESSED or
       BUTTON5_PRESSED inside ncurses) matches your container coordinates.
       Essential for scroll-panes and text viewport scrolling. */
    /* Veto Interceptor: Return 1 to freeze scroll action, 0 to allow */
    int (*on_scroll)(IrcitUiEventDispatcher *disp, IrcitUiEvent *ev, void *user);

    /* Layout Matrix Hooks */

    /* Executed during a frame tick when a component needs to write characters
       or apply text styles directly to its allocated window area using
       mvwaddstr or wattron. */
    int (*on_draw)(IrcitUiEventDispatcher *disp, IrcitUiEvent *ev, void *user);
    /* Triggers following a coalesced KEY_RESIZE frame. This gives widgets a
       hook to re-evaluate their layout strategies, change padding, or shuffle
       child elements before the next draw ticket occurs. */
    int (*on_resize)(IrcitUiEventDispatcher *disp, IrcitUiEvent *ev, void *user);

    /* Lifecycle Matrix Hooks */

    /* Triggers when a component gains or loses input focus (e.g., when a user
       tabs out of a text input box). Essential for rendering a visible blinking
       terminal caret or changing border highlight colors. */
    int (*on_focus_changed)(IrcitUiEventDispatcher *disp, IrcitUiEvent *ev, void *user, int has_focus);
    /* Runs immediately after the AOT layout engine instantiates a live
       IrcitUiNode from a blueprint definition, but before its dimensions are
       calculated. This is the optimal window to assign dynamic runtime context
       memory buffers. */
    int (*on_inflate)(IrcitUiEventDispatcher *disp, IrcitUiEvent *ev, void *user);
    /* Runs right before a node is stripped from the active screen tree and
       freed. Crucial for releasing custom application memory allocations or
       closing local descriptors safely to avoid memory leaks. */
    int (*on_deflate)(IrcitUiEventDispatcher *disp, IrcitUiEvent *ev, void *user);
  } lifecycle_listeners;
  struct
  {
    /* State Observers: Notification event */
    void (*on_text_changed)(IrcitUiEventDispatcher *disp, IrcitUiNode *node, int inserted_char, const char *new_text, void *user_data);
    void (*on_scrolled)(IrcitUiEventDispatcher *disp, IrcitUiNode *node, size_t new_top_row);
  } post_interceptor_listeners;
} IrcitUiInteractionState;

#endif /* IRCITUI_IRCIT_UI_INTERACTION_STATE_TYPE_H */
