/**
 * @file ui_event_dispatcher.c
 * @brief The dispatcher instance, and the entry points that run a listener.
 * Has exactly one job: Ingesting hardware signals (keys, mouse) and routing them to target handlers.
 *
 * Every listener in the widget set has the same three-parameter shape and it
 * begins with the dispatcher: int (*)(IrcitUiEventDispatcher *, IrcitUiEvent *,
 * void *). That pointer is not decoration. It is the object a callback acts back
 * on the framework through - asking for focus to move, asking the loop to end,
 * staging another event - and without an instance of it there is nothing to pass
 * and no way to call a listener at all.
 *
 * That is what this file supplies. It is deliberately the smallest thing that
 * makes the listener contract callable: one instance, one event per kind, and
 * the two state transitions a listener can ask for. It is not the input queue,
 * the worker pool or the watchdog - those are the dispatcher's later sections,
 * and they are not in this revision.
 *
 * THE ORDER IS THE CONTRACT. For every event the application's listener runs
 * first, from interactions_user, and the framework's default runs only if the
 * application returned 0. A non-zero return stops the chain. That is the whole
 * of the override mechanism, and it is why a node carries two interaction
 * states rather than one.
 *
 * These entry points are what the engine calls in place of its old inline
 * hooks. The engine keeps the decisions that are its own - hit-testing, the
 * focus transition, the checked toggle - and hands the node the event resolved
 * to over to one of the four functions below.
 */
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <signal.h>
#include <time.h>
#include <setjmp.h>
#include <bits/types/siginfo_t.h>
#include <sys/eventfd.h>

#include "ui_priv.h"
#include "ircitui_event_dispatcher_type_priv.h"
#include "ui_log.h"
#include "ircituilib/config_defs.h"

/* Both of these come after the project's own headers, and must keep doing so.
   uflib defines PUBLIC_API as a visibility attribute and ircituilib_defs.h
   defines it empty; reached in the other order the second definition is a
   -Wmacro-redefined error under -Werror. Every other TU in the tree includes
   the project headers first for the same reason. */
#include <uflib/cdt/lockless_ringbuffer/cdt_lockless_ringbuffer.h>

#define SIG_WATCHDOG_ALERT (SIGRTMIN + 2)

static void watchdog_async_signal_handler(int sig, siginfo_t *si, void *context);

/* The instance.
 *
 * A singleton, and it will stop being one the way the binding table will: when
 * the UI becomes an object that owns its screen, this member moves into it and
 * IrcitUiDispatcherInstance returns that object's dispatcher instead. Nothing
 * else in the file changes.
 *
 * The mutex is initialised even though nothing locks it yet. A pthread_mutex_t
 * left zeroed is not a valid mutex, and the first caller to reach for the input
 * queue would find that out at a point far from the cause. Initialising it here
 * costs one line and removes the trap. */
static IrcitUiEventDispatcher s_dispatcher = {//todo make volatile ?
  .lock = PTHREAD_MUTEX_INITIALIZER,
  /* -1, not the 0 a zero-initialised static would carry. Destroy() closes this
     only when it is a real descriptor, and 0 is stdin: a Destroy that ran before
     a successful Create would otherwise close the terminal's input. */
  .event_fd = -1,
};

IrcitUiEventDispatcher *
IrcitUiEventDispatcherCreate(void)
{
  IrcitUiEventDispatcher *disp = &s_dispatcher;
  struct sigevent  sev = {0};
  struct sigaction sa  = {0};

  /* 1. Establish the native wake channel.
     EFD_NONBLOCK: a producer must never stall on the doorbell - see
     IrcitUiDispatcherPostMessage, which is called from worker threads.
     EFD_CLOEXEC: the descriptor must not survive into a spawned child, where
     nothing would ever read it. */
  disp->event_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (disp->event_fd < 0) {
    perror("Failed to allocate the framework wake descriptor");
    // No free: disp is the file-scope singleton above, not an allocation. Releasing a pointer that is not the heap's was undefined behaviour.
    return NULL;
  }

  // One deadline, one member. It was set on both this and the context, and only this one is ever read - the context copy was write-only.
  disp->watchdog_timeout_ms = 40;

  // 2. The handler is registered before the timer that names it.
  sa.sa_sigaction = watchdog_async_signal_handler;
  sa.sa_flags = SA_SIGINFO;
  sigemptyset(&sa.sa_mask);

  if (sigaction(SIG_WATCHDOG_ALERT, &sa, NULL) < 0) {
    perror("Watchdog initialization failed - framework vulnerable to callback hangs");
  }

  /* 3. The timer itself, which nothing created before this. posix_timer_id was
     left the zero-valued static, so every timer_settime below ran against an
     invalid id and failed with EINVAL: the watchdog could never fire, and the
     disarm that follows each callback was equally a no-op.
     CLOCK_MONOTONIC because the deadline must not move when the wall clock does.

     SIGEV_THREAD_ID, not SIGEV_SIGNAL. SIGEV_SIGNAL lets the kernel deliver to
     any thread with the signal unblocked, and the handler siglongjmp()s into a
     sigjmp_buf this thread owns -- a worker running it would jump across threads,
     which is undefined. Naming a thread id makes that impossible: the handler can
     only run here, where the landing zone is. It binds to whichever thread calls
     this, which is the thread that owns the loop. Temporary only in that a pool
     spawning threads must still not unblock this signal; it can no longer be the
     thread that runs the handler. */
  sev.sigev_notify = SIGEV_THREAD_ID;
  sev.sigev_signo  = SIG_WATCHDOG_ALERT;
  sev.sigev_value.sival_ptr = &disp->watchdog_context.posix_timer_id;
  sev._sigev_un._tid = (pid_t)syscall(SYS_gettid);

  if (timer_create(CLOCK_MONOTONIC, &sev, &disp->watchdog_context.posix_timer_id) < 0) {
    perror("Watchdog timer allocation failed - framework vulnerable to callback hangs");
  }

  // No step 4 here: the pipe needed an fcntl per end to become non-blocking, and EFD_NONBLOCK at creation is what replaces both calls.

  return disp;
}

void
IrcitUiEventDispatcherDestroy(IrcitUiEventDispatcher *disp)
{
  if (!disp) return;
  if (disp->event_fd >= 0) {
    close(disp->event_fd);
    disp->event_fd = -1;
  }
  timer_delete(disp->watchdog_context.posix_timer_id);
  /* The ring is the one thing here that IS an allocation - IrcitUiInitialiseFrameworkContext
     makes it - so it is the one thing that has to be released. Nulled after, so a second
     Destroy is a no-op rather than a double free, and so the loop's TryPop cannot reach
     freed memory. */
  LocklessRingBufferDestroy(disp->async_response_queue);
  disp->async_response_queue = NULL;
  pthread_mutex_destroy(&disp->lock);
  // No free: disp is the file-scope singleton, not an allocation.
}

/* The descriptor a poller waits on. Named for what it is rather than for the
   pipe it used to be: there is one fd now, and it is both the read and the
   write end. */
int
IrcitUiEventDispatcherWakeFd(const IrcitUiEventDispatcher *disp)
{
  return disp ? disp->event_fd : -1;
}

/**
 * @brief Thread-safe submission hub to post value-type messages onto the Main UI Thread.
 * Invoked by any thread or pool context.
 */
bool IrcitUiDispatcherPostMessage(IrcitUiEventDispatcher *disp, const MainLoopMessage *msg)
{
  if (!disp || !msg) return false;

  // Direct, lock-free memory copy pass directly into the ring slot. Drop, never retry: the caller may be the only thread able to clear the ring.
  if (!LocklessRingBufferTryPush(disp->async_response_queue, msg)) {
    IRCITUI_LOG_ERROR("pipeline full (%zu slots): event type %d dropped, the loop is not draining",
                      LocklessRingBufferCapacity(disp->async_response_queue), (int)msg->type);
    return false;
  }

  // Wake the main looper's blocking multiplexer loop. eventfd takes an 8-byte counter increment, not a byte.
  uint64_t wake_token = 1;
  (void)write(disp->event_fd, &wake_token, sizeof(wake_token));
  return true;
}

/* The wait slice. Small enough that a producer's latency to a ring that has just
   drained is not noticeable, large enough not to spin a core while the consumer
   catches up. A scheduling detail rather than a policy, so it is not a
   config_defs entry. */
#define IRCITUI_POST_RETRY_SLICE_US 1000

int
IrcitUiDispatcherPostNetworkChunk(IrcitUiEventDispatcher *disp_ptr,
                                  void                  *stream_token,
                                  const char            *bytes_ptr,
                                  size_t                 bytes_len,
                                  int                    timeout_ms)
{
  MainLoopMessage envelope;
  int             waited_ms = 0;

  if (!disp_ptr || !bytes_ptr) return 0;

  /* THE BOUND IS THE FRAMEWORK'S, NOT THE CALLER'S. The envelope's inline buffer
     is a fixed 512 bytes, and the document's answer to a larger read is that the
     producer chunks it. That contract is enforced here rather than trusted,
     because the failure it prevents is a stack buffer overflow in the caller's
     thread -- and an overflow that corrupts the envelope is corrupting the
     structure the framework is about to hand to Thread 0.

     Refused rather than truncated: a silent truncation of a chat message is data
     loss that reads as success, and the producer would never learn. */
  if (bytes_len > sizeof envelope.status_message - 1) {
    IRCITUI_LOG_ERROR("network chunk of %zu bytes exceeds the %zu-byte envelope: refused, the producer must chunk",
                      bytes_len, sizeof envelope.status_message - 1);
    return 0;
  }

  memset(&envelope, 0, sizeof envelope);
  envelope.type                         = MAIN_MSG_NETWORK_DATA;
  envelope.target_node                  = NULL;
  envelope.payload.network.stream_token = stream_token;
  memcpy(envelope.status_message, bytes_ptr, bytes_len);
  envelope.status_message[bytes_len] = '\0';

  if (timeout_ms < 0) timeout_ms = 0;

  /* The wait, and the only copy of it. The push, the wake and the saturation
     policy live together here -- which is ui_async_bridge.c's rule ("the post
     owns the wake-up and the saturation policy; copying the loop's own push and
     pipe-write would give the pipeline a second producer that could drift from
     the first"), applied to the blocking case.

     Where the non-blocking post DROPS on a full ring, this one waits: a lost
     keyboard event is a keystroke, a lost stream chunk is a desynchronised
     reassembler. The give-up decision stays the caller's, which is why this
     takes a deadline rather than the caller's own cancellation flag -- the
     framework has no business naming an application's shutdown type. */
  for (;;) {
    if (LocklessRingBufferTryPush(disp_ptr->async_response_queue, &envelope)) {
      uint64_t wake_token = 1;
      (void)write(disp_ptr->event_fd, &wake_token, sizeof(wake_token));
      return 1;
    }

    if (waited_ms >= timeout_ms) {
      IRCITUI_LOG_ERROR("network chunk dropped: pipeline full for %d ms, the loop is not draining",
                        timeout_ms);
      return 0;
    }

    usleep(IRCITUI_POST_RETRY_SLICE_US);
    waited_ms += IRCITUI_POST_RETRY_SLICE_US / 1000;
  }
}

/**
 * ASYNC-SIGNAL-SAFE THREAD PRE-EMPTION INTERCEPT. Guard against an application callback hanging the
 * disarming thread: the POSIX interval timer armed around it fires SIG_WATCHDOG_ALERT
 * @param sig
 * @param si
 * @param context
 */
static void watchdog_async_signal_handler(int sig, siginfo_t *si, void *context) {
  (void)sig; (void)si; (void)context;

  volatile IrcitUiEventDispatcher *disp = &s_dispatcher;
  if (!disp || !disp->watchdog_context.in_callback) return;

  // Write diagnostic warning directly using an async-signal-safe system call
  const char msg[] = "\n[WATCHDOG PRE-EMPTION] Quantum threshold breached! Unwinding stalled stack frame.\n";
  (void)write(STDERR_FILENO, msg, sizeof(msg) - 1);

  // Safely trigger a non-local long jump to break out of the frozen coroutine
  siglongjmp(*(sigjmp_buf*)&disp->watchdog_context.env, 1);
}

IrcitUiEventDispatcher *
IrcitUiDispatcherInstance(void)
{
  return &s_dispatcher;
}

void
IrcitUiDispatcherSetFocus(IrcitUiEventDispatcher *disp_ptr, IrcitUiNode *node_ptr)
{
  /* IrcitUiSetFocus owns the transition: it clears IRCITUI_F_FOCUSED on the
     outgoing node, sets it on the incoming one, repoints the global the painter
     and the key walk both read, and resets the blink. The global stays
     authoritative for now, and focused_node below is a mirror of it written for
     the readers that have already been moved over. Reading the mirror before
     that move is complete would read stale state, so nothing does. */
  IrcitUiSetFocus(node_ptr);
  if (disp_ptr) disp_ptr->focused_node = node_ptr;
}

void
IrcitUiDispatcherExit(IrcitUiEventDispatcher *disp_ptr)
{
  if (disp_ptr) disp_ptr->exit_requested = 1;
}

int
IrcitUiDispatcherExited(const IrcitUiEventDispatcher *disp_ptr)
{
  return disp_ptr && disp_ptr->exit_requested;
}

/* Installs -- or, given a NULL listener, clears -- the application's entry point
   for MAIN_MSG_NETWORK_DATA.

   Registration is a bootstrap act, not configuration. The listener runs on
   Thread 0 from inside the loop's envelope drain, so calling this from another
   thread once the loop is running is a race rather than a reconfiguration. And
   clearing it before the tree is torn down is what stops a stale listener being
   reached with a stream token whose storage the application has already
   released: the framework cannot know when that happened, so the application has
   to say.

   One slot and one user for every stream. Which connection a chunk belongs to
   travels in the envelope's token, so a second call replaces the first rather
   than adding to it. */
void
IrcitUiDispatcherRegisterNetworkListener(IrcitUiEventDispatcher *disp_ptr,
                                        IrcitUiNetworkListener   listener_ptr,
                                        void                   *user_ptr)
{
  if (!disp_ptr) return;

  disp_ptr->network_listener = listener_ptr;
  disp_ptr->network_user     = user_ptr;
}

/* Run one listener.
 *
 * The two states are reached by the same expression because the listener block
 * inside them is the same type, anonymous so that it has no name for a caller to
 * learn. Both slots' user pointer is passed on unchanged: for the framework's
 * instance that is the field's text buffer or the status line's echo, and for
 * the application's it is whatever the application stored, and neither is this
 * function's to interpret.
 *
 * The tag picks the slot, and a tag with no slot returns 0 - the Event
 * Consumption Contract's "not resolved", which is what lets the caller run its
 * own default instead. on_focus_changed is the one listener not reachable from
 * here, and cannot be: it takes the fourth has_focus parameter, so it is a
 * different function type and cannot be held in the same local. It gets its own
 * entry point when the focus transition moves off IrcitUiSetFocus. */
static int
sDispatchedToListener(IrcitUiInteractionState *state_ptr, IrcitUiEventDispatcher *disp_ptr, IrcitUiEvent *ev_ptr, IrcitUiEventType type)
{
  int (*fn)(IrcitUiEventDispatcher *, IrcitUiEvent *, void *);

  switch (type)
  {
    case IRCITUI_EVENT_INPUT_KEY:         fn = state_ptr->lifecycle_listeners.on_key_down; break;
    case IRCITUI_EVENT_INPUT_CLICK:       fn = state_ptr->lifecycle_listeners.on_click;    break;
    case IRCITUI_EVENT_INPUT_SCROLL:      fn = state_ptr->lifecycle_listeners.on_scroll;   break;
    case IRCITUI_EVENT_LAYOUT_RESIZE:     fn = state_ptr->lifecycle_listeners.on_resize;   break;
    case IRCITUI_EVENT_LIFECYCLE_INFLATE: fn = state_ptr->lifecycle_listeners.on_inflate;  break;
    case IRCITUI_EVENT_LIFECYCLE_DEFLATE: fn = state_ptr->lifecycle_listeners.on_deflate;  break;
    default: return 0;
  }
  if (!fn) return 0;
  return fn(disp_ptr, ev_ptr, state_ptr->user);
}

/* The application first, the framework second, and the first non-zero wins.
 *
 * One copy of the order for all four entry points below. The event is built by
 * the caller because the payload differs per kind, and this only stamps the tag
 * and the target onto it - the target being the node the event resolved to,
 * which is the resolver's answer and not something a listener derives. */
static int
sDispatchToListener(IrcitUiEventDispatcher *disp_ptr, IrcitUiNode *node_ptr, IrcitUiEvent *ev_ptr, IrcitUiEventType type)
{
  ev_ptr->type   = type;
  ev_ptr->target = node_ptr;

  if (sDispatchedToListener(&node_ptr->interactions_user, disp_ptr, ev_ptr, type)) return 1;
  if (sDispatchedToListener(&node_ptr->common.interaction_defaults, disp_ptr, ev_ptr, type)) return 1;

  return 0;
}

int
IrcitUiDispatcherPostKey(IrcitUiEventDispatcher *disp_ptr, IrcitUiNode *node_ptr, int key_code)
{
  IrcitUiEvent ev = {0};

  if (!node_ptr) return 0;

  /* Nothing here reads the payload fields, but they are cleared rather than left
     indeterminate so that a listener which inspects the event - as the design's
     defaults do, for the scroll delta - reads zero rather than whatever the
     frame held. */
  ev.key_code = key_code;
  return sDispatchToListener(disp_ptr, node_ptr, &ev, IRCITUI_EVENT_INPUT_KEY);
}

int
IrcitUiDispatcherPostClick(IrcitUiEventDispatcher *disp_ptr, IrcitUiNode *node_ptr, int mouse_x, int mouse_y)
{
  IrcitUiEvent ev = {0};

  if (!node_ptr) return 0;

  ev.mouse_x = mouse_x;
  ev.mouse_y = mouse_y;

  /* The click is recorded in the engine's own last-mouse store before the
     listeners run, because IrcitUiEmojiInput reads the coordinates from there
     rather than from the event it is handed. A click routed through here and
     not recorded would act on the coordinates of some earlier click. */
  IrcitUiRecordMouse(mouse_x, mouse_y);

  return sDispatchToListener(disp_ptr, node_ptr, &ev, IRCITUI_EVENT_INPUT_CLICK);
}

int
IrcitUiDispatcherPostScroll(IrcitUiEventDispatcher *disp_ptr, IrcitUiNode *node_ptr, int delta)
{
  IrcitUiEvent ev = {0};

  if (!node_ptr) return 0;

  ev.scroll_delta = delta;

  return sDispatchToListener(disp_ptr, node_ptr, &ev, IRCITUI_EVENT_INPUT_SCROLL);
}

int
IrcitUiDispatcherPostLifecycle(IrcitUiEventDispatcher *disp_ptr, IrcitUiNode *node_ptr, IrcitUiEventType type)
{
  IrcitUiEvent ev = {0};

  if (!node_ptr) return 0;

  return sDispatchToListener(disp_ptr, node_ptr, &ev, type);
}
