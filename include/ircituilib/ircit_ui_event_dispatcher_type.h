/**
 * @file ircit_ui_event_dispatcher_type.h
 * @brief The dispatcher handle handed to a listener, opaque to callers.
 */
#ifndef IRCITUI_IRCIT_UI_EVENT_DISPATCHER_TYPE_H
#define IRCITUI_IRCIT_UI_EVENT_DISPATCHER_TYPE_H

/*!
 * @struct IrcitUiEventDispatcher
 * @brief The context controller a listener is handed by the framework.
 *
 * Opaque by design: the members live in the library's private header. A
 * listener never chooses or constructs one — the engine passes its own
 * dispatcher into the callback at the moment it invokes it — so the handle
 * exists for the listener to act back on the engine through, not for the
 * listener to own or inspect.
 */
typedef struct IrcitUiEventDispatcher IrcitUiEventDispatcher;

/*!
 * @typedef IrcitUiNetworkListener
 * @brief The application's entry point for non-UI pipeline traffic.
 *
 * Invoked on the thread that owns the tree -- Thread 0 -- synchronously, from
 * the main loop's envelope drain. Registered through
 * @ref IrcitUiDispatcherRegisterNetworkListener.
 *
 * **One slot, not one per stream.** The application registers a single listener
 * and demultiplexes inside it: @p user is the pointer given at registration and
 * @p stream_token says which connection this chunk came from. That split is the
 * whole reason the token exists, and it is why there is no per-connection
 * registration and no unregister.
 *
 * @param disp_ptr     The dispatcher. Never NULL.
 *
 * @param stream_token Opaque per-stream identity, set by whoever produced the
 *                     message. The framework never interprets it, never frees
 *                     it, and never outlives it by design -- but it cannot
 *                     enforce that, so **the storage behind it must outlive
 *                     every message posted with it**.
 *
 * @param bytes        The payload. **Borrowed, and valid only for the duration
 *                     of this call**: it points into the loop's own envelope
 *                     slot, which is reused for the very next message of any
 *                     kind. A listener that needs to keep the bytes copies them.
 *                     This is safe only because the delivery is synchronous; a
 *                     deferred delivery would make it a dangling pointer with no
 *                     compile-time signal.
 *
 * @param user         The pointer given at registration. The framework stores it
 *                     and hands it back unread.
 *
 * @return Non-zero if the listener consumed the message. The network path has
 *         no chain behind it, so nothing currently acts on this; it is part of
 *         the signature to match the listener family, and is reserved.
 *
 * @warning This listener runs **without the watchdog armed**. A callback that
 *          blocks here stops Thread 0, which stops the envelope drain, which
 *          stops the producer's backpressure from clearing -- the whole
 *          application wedges. Every other callback in this tree is evicted
 *          after a deadline; this one is trusted to return, deliberately,
 *          because the eviction path abandons a stack mid-update and a stream
 *          reassembler cannot survive that. An over-budget call is reported
 *          through the log rather than pre-empted.
 */
typedef int (*IrcitUiNetworkListener)(IrcitUiEventDispatcher *disp_ptr,
                                      void                  *stream_token,
                                      const char            *bytes,
                                      void                  *user);

#endif /* IRCITUI_IRCIT_UI_EVENT_DISPATCHER_TYPE_H */
