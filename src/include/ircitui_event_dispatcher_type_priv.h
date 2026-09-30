#ifndef IRCITUILIB_IRCITUI_EVENT_DISPATCHER_TYPE_PRIV_H
#define IRCITUILIB_IRCITUI_EVENT_DISPATCHER_TYPE_PRIV_H

#include <stddef.h>
#include <stdint.h>
#include <signal.h>
#include <pthread.h>
#include <stdbool.h>

#include <uflib/cdt/lockless_ringbuffer/cdt_lockless_ringbuffer_type.h>

#include "ircituilib/config_defs.h"
#include "ircituilib/ircit_ui_event_dispatcher_type.h"
#include "ircitui_event_type_priv.h"
#include "watchdog_context_type_priv.h"

struct UfsrvScheduler;
typedef struct IrcitUiNode IrcitUiNode;

/**
 * @brief Categorized message identification tags for the main thread dispatcher loop.
 */
typedef enum
{
  MAIN_MSG_HARDWARE_KEY,    ///< Direct keyboard input events
  MAIN_MSG_HARDWARE_MOUSE,  ///< Mouse grid tracking/click/scroll adjustments
  MAIN_MSG_COROUTINE_READY, ///< Asynchronous background coroutine ready for UI resumption
  MAIN_MSG_LIFECYCLE_TICK,  ///< View infrastructure triggers (inflate, deflate)
  /* A future resolved on a worker and its result is in status_message. Its own
     discriminant rather than a MAIN_MSG_COROUTINE_READY carrying a NULL handle:
     the coroutine case is gated on that handle, so an overloaded one would be
     read as "nothing to resume" and the result would be dropped in silence. */
  MAIN_MSG_FUTURE_READY,    ///< A resolved future's result, for delivery on Thread 0
  /* The same delivery, when what arrived is the path of a file to show rather
     than a string to put in a field. Its own discriminant because the two are
     acted on differently on Thread 0, and guessing from the content would mean
     every result being inspected to find out what it was. */
  MAIN_MSG_FUTURE_IMAGE,    ///< A resolved future's result naming an image file
  /* A worker's INCREMENTAL report about a widget it is driving, as opposed to the
     two above, which are a future's single terminal result.

     Its own discriminant for the reason the two above are separate from each
     other: it is acted on differently on Thread 0. A resolved future carries a
     value to be stored and a context to be freed; this carries a NUMBER to be
     applied to a node that may have gone since the worker last looked, so the
     delivery resolves the target by ID rather than by pointer and drops the
     report in silence when the widget is no longer there. That difference is the
     whole reason a separate tag exists -- folding it into MAIN_MSG_FUTURE_READY
     would mean the delivery path had to guess which of the two it was holding.

     status_code carries the percentage, and status_message the target's id. */
  MAIN_MSG_FUTURE_PROGRESS, ///< A worker's incremental progress report, by node id
  /* The same incremental report, about the bar's SECOND position.

     Its own discriminant for the reason every tag in this family has one: Thread 0
     acts on the two differently -- a progress report reaches IrcitUiBarSetValue and
     this one IrcitUiBarSetBuffered -- so a shared tag would leave the delivery path
     having to be told which setter to call, and a field added to the envelope to
     tell it is a field every other producer then has to leave alone. status_code
     carries the percentage and status_message the target's id, exactly as above. */
  MAIN_MSG_FUTURE_BUFFERED, ///< A worker's incremental buffered-position report
  /* A chunk of bytes off a live socket, produced by an application worker and
     delivered to the application's own listener on Thread 0.

     Its own discriminant for the reason the two above are separate: it is acted
     on differently on Thread 0. A future resolves exactly once and its delivery
     frees the context that carried it; a stream is persistent, un-bounded, and
     posted at line rate. That last difference is why this is also the only tag
     whose case runs without the watchdog armed, and the only one the drain has
     to be capped against -- see IRCITUILIB_EVENTS_TCP_IP_DESIGN.md appendix A. */
  MAIN_MSG_NETWORK_DATA     ///< One chunk of an application network stream
} MainLoopMessageType;

/**
 * @brief High-performance, zero-allocation contiguous envelope used to cross thread boundaries.
 *
 * This structure avoids dynamic runtime allocation inside worker loops.
 * Everything travels inside flat inline buffers.
 */
typedef struct
{
  MainLoopMessageType type;        ///< Target routing discriminant
  IrcitUiNode *       target_node; ///< Target component instance pointer

  union
  {
    int key_code; ///< Value for hardware key codes
    struct
    {
      int           mouse_x;
      int           mouse_y;
      unsigned long mouse_state;
    } mouse; ///< Absolute geometry snapshot
    struct
    {
      void *coroutine_handle; ///< Coroutine reference handle (UfsrvCoroutine*)
      int   status_code;      ///< Operation diagnostic tracking field
    } async_work;
    struct
    {
      /* Which connection this chunk belongs to. Opaque to the framework: stored,
         carried and handed back, never interpreted and never freed. The
         application sets it and resolves it.

         Its own member rather than a reuse of async_work.coroutine_handle. The
         two are the same size so the union costs nothing either way, but a
         reader who finds a coroutine handle inside a network message has been
         told something false. */
      void *stream_token; ///< Per-stream identity, opaque to the framework
    } network; ///< Value for MAIN_MSG_NETWORK_DATA
  } payload;

  /* Fixed inline text allocation block to transfer string information securely without dynamic allocations */
  char status_message[PRIV_CONFIG_DEFAULT_CLIPBUF_SZ];
} MainLoopMessage;

/**
 * @brief The framework context controller handle.
 */
struct IrcitUiEventDispatcher
{
  /* --- 1. LOCKLESS MPSC ASYNC RESPONSE PIPELINE --- */

  /**
   * @brief Consolidated lock-free messaging queue operating in Multi-Producer Single-Consumer (MPSC) mode.
   * Manages all keys, mouse interactions, and worker thread completions.
   */
  LocklessRingBuffer *async_response_queue;

  /**
   * @brief The native Linux wakeup channel: one eventfd, read and written by
   *        the same descriptor number.
   *
   * It replaced a POSIX self-pipe pair. The pipe was non-blocking on both ends,
   * so a full 64KB buffer made the producer's write fail with EAGAIN and the
   * wake byte was dropped in silence - the loop then slept to its next timeout
   * with work already sitting in the ring. An eventfd accumulates a counter
   * instead, so a wake cannot be lost, and it is one descriptor rather than two
   * ends to keep in step. Nothing ever read the pipe's byte value; the channel
   * carries a doorbell, not data.
   *
   * -1 until IrcitUiEventDispatcherCreate makes it, and again after Destroy.
   */
  int event_fd;

  /**
   * @brief System fallback mutex shield.
   * Maintained strictly to defend legacy subsystem calls or component hooks.
   */
  pthread_mutex_t lock;

  /* --- 2. SCHEDULER & PROTECTED RUNTIME ENGINE STATE --- */
  struct UfsrvScheduler *scheduler;                  ///< Core worker engine handle
  WatchdogContext        watchdog_context;           ///< Thread 0 pre-emption recovery envelope
  uint64_t               watchdog_timeout_ms;        ///< Safety threshold execution limit (~40ms)
  const char *           current_processing_node_id; ///< Active view string tracer

  /* --- 3. GLOBAL UI NAVIGATION AND FOCUS CARRIERS --- */
  IrcitUiNode *focused_node;      ///< Component holding active keyboard input focus
  IrcitUiNode *active_modal_node; ///< Active structural modal (forces immediate intersection event capture)

  /**
   * @brief Safe atomic flag signaled by IrcitUiDispatcherExit to cleanly close the engine.
   */
  volatile sig_atomic_t exit_requested;

  /* --- 4. NON-UI PIPELINE TRAFFIC --- */

  /**
   * @brief The application's entry point for MAIN_MSG_NETWORK_DATA.
   *
   * NULL until IrcitUiDispatcherRegisterNetworkListener installs one, and the
   * zero value is the correct "nothing registered" state -- unlike event_fd,
   * this needs no explicit sentinel at construction.
   *
   * A chunk that arrives with this NULL is logged and dropped. The producer's
   * push has already succeeded by the time the case runs, so nothing upstream
   * can tell the data went nowhere; the log line is the only signal there is.
   */
  IrcitUiNetworkListener network_listener;

  /**
   * @brief The application's own pointer, handed back to network_listener
   *        unread. NULL until registered.
   *
   * One slot and one user for every stream: which connection a chunk belongs to
   * travels in the envelope's token, not here. It is cleared alongside the
   * listener so a torn-down application cannot be called back.
   */
  void *network_user;
};

#endif // IRCITUILIB_IRCITUI_EVENT_DISPATCHER_TYPE_PRIV_H
