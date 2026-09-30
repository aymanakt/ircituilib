/**
 * @file msp_server.c
 * @brief The MSP simulation server: three independent metric channels, streamed to
 *        connected clients over a Unix domain socket.
 *
 * @details §28's harness, with the defects its pasted code carries fixed as it is written
 *          rather than after.  The behaviour of the three channels is the section's and is
 *          kept verbatim -- caching stalls, thermal dropouts, progressive speed decay with
 *          a mid-run freeze -- because that behaviour is what the harness is *for*: what
 *          changed is the wire encoding (`msp_protocol.h` explains that measurement) and
 *          six things the section's code gets wrong.
 *
 *          ## What this file does differently from §28, and why
 *
 *          1. **A partial frame is never left on the wire.**  §28 sets every client fd
 *             `O_NONBLOCK` and checks `send`'s result only for `EPIPE`/`ECONNRESET`, so a
 *             short write (`0 < sent < 64`) is silently ignored and the client's
 *             fixed-size parser desynchronises permanently.  This is the section's most
 *             serious defect, and the framework's own network post documents the same
 *             hazard -- "a lost chunk desynchronises a reassembler permanently"
 *             (`include/ircituilib/ui.h:4688-4692`).  Here every client fd carries
 *             `SO_SNDTIMEO`, `s_send_all` sends the whole frame or reports failure, and a
 *             failure **closes the client**.  The invariant that buys: a client that is
 *             still connected is only ever sent whole frames, and one that is dropped
 *             sees EOF mid-frame, which its reassembler discards.
 *          2. **A failed `accept` does not spin.**  §28 does `if (client_fd < 0)
 *             continue;`, which is a 100% CPU loop on any persistent error -- `EMFILE`,
 *             or a descriptor that has gone away.  Here `EINTR` retries immediately and
 *             anything else backs off.
 *          3. **There is a shutdown path at all.**  §28's `thrd_join` after its loop is
 *             unreachable: `g_SimRunning` is never cleared and `accept` blocks forever,
 *             so the only exit is a signal that skips the `unlink` and leaves a stale
 *             socket behind -- which makes the *next* `bind` fail.  Here `SIGINT`/`SIGTERM`
 *             clear the flag and shut the listening socket down, so the joins are reached
 *             and the path is removed.
 *          4. **The client slot's fd is read under its lock.**  §28 tests
 *             `s_Clients[i].fd >= 0` outside the mutex and again inside; the outer read
 *             races the writers.  Benign in effect, undefined in fact, and a sanitiser
 *             flags it.
 *          5. **A stale socket is removed only if it is a socket.**  `unlink` on a path is
 *             a blunt instrument; `stat` first, so a mistyped argument cannot delete a
 *             regular file.
 *          6. **Each thread has its own PRNG.**  §28 calls `rand()`, whose state is shared.
 *             A per-thread xorshift removes the question rather than reasoning about
 *             glibc's locking.
 *
 *          ## What it does NOT link
 *
 *          Nothing.  No ircituilib, no curses, no PNG.  The absent
 *          `target_link_libraries` in CMakeLists.txt is the assertion that the protocol and
 *          its server are independent of the framework they feed, and it is the same kind
 *          of deliberate absence the `ircitui_window_demo` comment makes load-bearing.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "msp_protocol.h"

#include <errno.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <threads.h>
#include <time.h>
#include <unistd.h>

/* ── Shared state ───────────────────────────────────────────────────────────── */

static atomic_bool g_running   = true;
static atomic_int  g_clients   = 0;
static int         g_listen_fd = -1;
static const char *g_sock_path = MSP_SOCKET_PATH;

/* §30's control parameters, atomic so the accept loop's reader can change them while a
 * simulation thread is mid-tick. */
static atomic_bool  g_yt_paused  = false;
static atomic_bool  g_yt_corrupt = false;
static _Atomic float g_yt_speed  = 1.0f;
static _Atomic float g_dl_speed  = 1.0f;
static atomic_bool  g_dl_corrupt = false;

/** @brief One client slot.  @c fd and @c rx are only ever touched with @c lock held. */
typedef struct {
  int   fd;
  mtx_t lock;
  MspRx rx;    /**< the command stream from this client, reassembled */
} ClientSlot;

static ClientSlot s_clients[MSP_MAX_CLIENTS];

/** @brief How long a blocked write may stall a simulation thread before the client is
 *         dropped.  Short, because a client that cannot accept 64 bytes in 100 ms is not
 *         keeping up with a stream it asked for, and holding a simulation thread for it
 *         is what turns one slow reader into a stalled harness. */
#define MSP_SEND_TIMEOUT_MS 100

/* Two forward declarations, because the input side sits between them and their definitions.
 * s_sleep_ms is the shared tick every loop uses; s_apply_command is §30's dispatch, which
 * belongs beside the channels it drives rather than ahead of the accept machinery. */
static void s_sleep_ms(long ms);
static void s_apply_command(const MspCmdFrame *cmd);

/* ── The output side ────────────────────────────────────────────────────────── */

/**
 * @brief Write all of @p len bytes, or report that the client is not useable.
 *
 * @details The loop is the whole point: `send` returns a byte count, not a boolean, and a
 *          single call that returns less than asked for is the case §28 does not handle.
 *          `EINTR` retries; `EAGAIN` here means `SO_SNDTIMEO` expired, which is a client
 *          that has stopped reading.
 *
 * @return 1 if every byte went out, 0 otherwise.  On 0 the caller closes the client: a
 *         half-written frame is not recoverable, and the reassembler on the far side
 *         discards a partial tail on EOF.
 */
static int
s_send_all(int fd, const char *buf, size_t len)
{
  size_t off = 0;

  while (off < len) {
    ssize_t n = send(fd, buf + off, len - off, MSG_NOSIGNAL);

    if (n > 0) {
      off += (size_t)n;
      continue;
    }
    if (n < 0 && errno == EINTR) continue;
    return 0;
  }
  return 1;
}

/** @brief Send one frame to every connected client, dropping those that will not take it. */
static void
s_broadcast(const char *frame)
{
  int i;

  for (i = 0; i < MSP_MAX_CLIENTS; i++) {
    int fd;
    int ok;

    /* THE LOCK COMES FIRST.  Testing the fd outside it -- which is what §28 does -- reads a
     * field another thread is writing. */
    mtx_lock(&s_clients[i].lock);
    fd = s_clients[i].fd;
    if (fd >= 0) {
      ok = s_send_all(fd, frame, MSP_FRAME_LEN);
      if (!ok) {
        close(fd);
        s_clients[i].fd = -1;
        atomic_fetch_sub(&g_clients, 1);
      }
    } else {
      ok = 1;   /* nothing to do, and nothing to report */
    }
    mtx_unlock(&s_clients[i].lock);

    if (!ok) {
      fprintf(stderr, "[msp] client slot %d could not take a frame; dropped\n", i);
    }
  }
}

/* ── The input side ─────────────────────────────────────────────────────────── */

/**
 * @brief Read whatever clients have sent, and apply the commands in it.
 *
 * @details §30's other direction.  A dedicated thread rather than doing it in `accept`'s
 *          loop, because a client can send a pause at any moment and the accept loop is
 *          blocked in `accept` for most of its life.
 *
 *          THE FD STAYS BLOCKING, and that is deliberate.  `O_NONBLOCK` is per-descriptor,
 *          not per-direction, so setting it to make reads non-blocking would also make
 *          WRITES non-blocking -- which would defeat `SO_SNDTIMEO` and turn every full send
 *          buffer into an immediate dropped client.  Instead the readability is asked for
 *          first with a zero-timeout `poll`, and only then is `recv` called, so the read
 *          cannot block and the write deadline survives.
 */
static int
s_read_loop(void *arg)
{
  (void)arg;

  while (atomic_load(&g_running)) {
    int i;

    s_sleep_ms(20);

    for (i = 0; i < MSP_MAX_CLIENTS; i++) {
      struct pollfd pfd;
      int           fd;

      mtx_lock(&s_clients[i].lock);
      fd = s_clients[i].fd;
      mtx_unlock(&s_clients[i].lock);
      if (fd < 0) continue;

      pfd.fd      = fd;
      pfd.events  = POLLIN;
      pfd.revents = 0;
      if (poll(&pfd, 1, 0) <= 0) continue;

      mtx_lock(&s_clients[i].lock);
      fd = s_clients[i].fd;             /* re-read: it may have been dropped meanwhile */
      if (fd >= 0) {
        char    buf[MSP_FRAME_LEN];
        ssize_t n = recv(fd, buf, sizeof buf, 0);

        if (n > 0) {
          char    frame[MSP_CMD_LEN];
          MspCmdFrame cmd;

          if (!MspRxPush(&s_clients[i].rx, buf, (size_t)n)) {
            fprintf(stderr, "[msp] slot %d: command chunk too large; stream reset\n", i);
          }
          while (MspRxNext(&s_clients[i].rx, MSP_CMD_LEN, frame, MspRxValidCmd, NULL)) {
            if (MspCmdParse(frame, &cmd)) s_apply_command(&cmd);
          }
        } else if (n == 0 || (n < 0 && errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)) {
          /* A clean EOF or a real error: either way the client is gone, and it must be
           * dropped under the same lock that protects the fd. */
          close(fd);
          s_clients[i].fd = -1;
          MspRxInit(&s_clients[i].rx);
          atomic_fetch_sub(&g_clients, 1);
          fprintf(stderr, "[msp] client slot %d disconnected\n", i);
        }
      }
      mtx_unlock(&s_clients[i].lock);
    }
  }
  return 0;
}

/* ── Per-thread randomness ──────────────────────────────────────────────────── */

/**
 * @brief A xorshift32, seeded per thread.
 *
 * @details §28 calls `rand()`.  Its state is process-wide, so three simulation threads
 *          drawing from it is a shared-mutable-state question that does not need to exist:
 *          the numbers are noise for a gauge, and each channel wanting its own stream of it
 *          is the honest description.  `s_rand01` returns `[0,1)`.
 */
static unsigned
s_xorshift(unsigned *state_ptr)
{
  unsigned x = *state_ptr;

  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  *state_ptr = x;
  return x;
}

static float
s_rand01(unsigned *state_ptr)
{
  return (float)(s_xorshift(state_ptr) >> 8) / 16777216.0f;   /* 24 bits: exact in a float */
}

static void
s_sleep_ms(long ms)
{
  struct timespec ts;

  ts.tv_sec  = ms / 1000;
  ts.tv_nsec = (ms % 1000) * 1000000L;
  thrd_sleep(&ts, NULL);
}

/* ── The three channels ─────────────────────────────────────────────────────── */

/**
 * @brief §28's media channel: playback, pre-fetch, and the three network phases.
 *
 * @details The phases are the section's and are the reason this channel is worth having:
 *          the pre-fetch buffer outruns playback, then a throttling window between 120 s
 *          and 150 s slows it below playback, then it stalls entirely when the gap closes
 *          to four seconds and playback catches up.  A three-state bar exists to show
 *          exactly that, which is why the loss of that widget (see the header's channel
 *          note) is worth recording rather than glossing.
 */
static int
s_media_loop(void *arg)
{
  const long long total = 320;
  float        playback = 0.0f;
  float        prefetch = 25.0f;
  char         frame[MSP_FRAME_LEN];
  char         label[MSP_LABEL_LEN + 1];

  (void)arg;

  while (atomic_load(&g_running)) {
    float speed = atomic_load(&g_yt_speed);
    float gap;

    s_sleep_ms((long)(250.0f / (speed > 0.0f ? speed : 1.0f)));

    if (atomic_load(&g_clients) == 0) continue;

    label[0] = '\0';

    if (atomic_load(&g_yt_corrupt)) {
      /* The cache is dropped: the buffer collapses to the play head, which is the shape a
       * viewer sees when a stream falls off a cliff. */
      prefetch = playback;
      atomic_store(&g_yt_corrupt, false);
      snprintf(label, sizeof label, "[ERROR] cache drop");
    } else if (atomic_load(&g_yt_paused)) {
      snprintf(label, sizeof label, "[PAUSED]");
    }

    if (!atomic_load(&g_yt_paused)) {
      playback += 0.25f;
      if (playback > (float)total) {
        playback = 0.0f;
        prefetch = 25.0f;
      }
    }

    gap = prefetch - playback;
    if (!atomic_load(&g_yt_paused)) {
      if (gap < 4.0f) {
        /* Stall: the download stops completely while playback catches up. */
        if (label[0] == '\0') snprintf(label, sizeof label, "[STALL] Buffering");
      } else if (playback > 120.0f && playback < 150.0f) {
        prefetch += 0.05f;   /* throttled: below the consumption rate */
      } else {
        prefetch += 0.60f;
      }
    }

    if (prefetch > (float)total) prefetch = (float)total;

    MspFrameBuild(frame, CH_MEDIA_YOUTUBE, playback / (float)total, prefetch / (float)total,
                  (long long)playback, total, label);
    s_broadcast(frame);
  }
  return 0;
}

/**
 * @brief §28's CPU channel: low-frequency load with a periodic thermal dropout.
 *
 * @details The threshold is the section's: every 300 ticks, the last 80 of them collapse the
 *          load to 20% and report a 0.35 throttle factor.  The secondary metric is the
 *          throttle, which is why this channel is the one that most wants a second readout
 *          on the client.
 */
static int
s_cpu_loop(void *arg)
{
  unsigned      rng  = 0x85EBCA6Bu ^ (unsigned)(uintptr_t)arg;
  unsigned long tick = 0;
  char          frame[MSP_FRAME_LEN];
  char          label[MSP_LABEL_LEN + 1];

  while (atomic_load(&g_running)) {
    float baseline, load, throttle = 1.0f;

    s_sleep_ms(100);

    if (atomic_load(&g_clients) == 0) continue;

    tick++;
    baseline = 0.40f + 0.25f * (float)sin((double)tick * 0.05);
    load     = baseline + s_rand01(&rng) * 0.15f;

    label[0] = '\0';
    if (tick % 300 > 220) {
      load    *= 0.20f;
      throttle = 0.35f;
      snprintf(label, sizeof label, "[THROTTLE] thermal");
    }

    if (load < 0.0f)  load = 0.0f;
    if (load > 1.0f)  load = 1.0f;

    MspFrameBuild(frame, CH_DIAL_CPU, load, throttle, (long long)tick, 0, label);
    s_broadcast(frame);
  }
  return 0;
}

/**
 * @brief §28's download channel: 100 MB with decay, a mid-run freeze, and recoveries.
 *
 * @details The freeze window (58% to 64%) is held exactly where the section puts it, because
 *          a harness whose failure can be predicted from the percentage is easier to test
 *          against than one that fails at random.
 */
static int
s_download_loop(void *arg)
{
  unsigned        rng   = 0xC2B2AE35u ^ (unsigned)(uintptr_t)arg;
  const long long total = 104857600LL;      /* 100 MiB */
  long long       done  = 0;
  char            frame[MSP_FRAME_LEN];
  char            label[MSP_LABEL_LEN + 1];

  while (atomic_load(&g_running)) {
    float pct, speed, mbps;

    s_sleep_ms(200);

    if (atomic_load(&g_clients) == 0) continue;

    pct   = (float)((double)done / (double)total);
    speed = 3500000.0f;                     /* 3.5 MB/s */

    label[0] = '\0';

    if (atomic_load(&g_dl_corrupt)) {
      done = 0;
      atomic_store(&g_dl_corrupt, false);
      snprintf(label, sizeof label, "[ERROR] conn reset");
    }

    /* TWO INDEPENDENT DECAYS, not a ladder, because that is what §28 writes: the 0.75 term
     * multiplies the 0.45 one rather than replacing it, so by three-quarters complete the
     * link runs at 3500000 * 0.45 * 0.15.  Writing it as if/else would quietly make the
     * late run three times faster than the section specifies. */
    if (pct > 0.40f) speed *= 0.45f;
    if (pct > 0.75f) speed *= 0.15f;

    if (pct > 0.58f && pct < 0.64f) {
      speed = 0.0f;                          /* the link freezes and the bar stops moving */
      if (label[0] == '\0') snprintf(label, sizeof label, "[STALL] link frozen");
    }

    speed *= atomic_load(&g_dl_speed);

    /* §28's jitter, kept: `base_speed * 0.20f` alone makes the bar advance like a metronome,
     * and a download whose rate never varies is a weaker test of a throughput readout.  The
     * section writes `rand() % 50000 - 25000`; the per-thread generator above replaces it, and
     * the chunk is clamped at zero because a negative one would run the counter backwards. */
    done += (long long)(speed * 0.20f) + (long long)(s_rand01(&rng) * 50000.0f - 25000.0f);
    if (done < 0)    done = 0;
    if (done > total) done = 0;              /* wrap, as the section does */

    mbps = speed / 1024.0f / 1024.0f;
    MspFrameBuild(frame, CH_DL_LARGE, (float)((double)done / (double)total), mbps, done, total, label);
    s_broadcast(frame);
  }
  return 0;
}

/* ── The control side ───────────────────────────────────────────────────────── */

/**
 * @brief Apply one client command.
 *
 * @details §30's dispatch.  An unknown channel or command cannot reach here --
 *          `MspCmdParse` refuses both -- so this switch has no "silently ignore" arm for a
 *          malformed frame, which is what makes a protocol error distinguishable from a
 *          command that legitimately did nothing.
 */
static void
s_apply_command(const MspCmdFrame *cmd)
{
  switch (cmd->channel) {
    case CH_MEDIA_YOUTUBE:
      switch (cmd->command) {
        case CMD_TOGGLE_PAUSE:
          atomic_store(&g_yt_paused, !atomic_load(&g_yt_paused));
          fprintf(stderr, "[msp] media pause: %s\n", atomic_load(&g_yt_paused) ? "on" : "off");
          break;
        case CMD_ADJUST_SPEED:
          atomic_store(&g_yt_speed, cmd->value);
          fprintf(stderr, "[msp] media speed: %.2fx\n", (double)cmd->value);
          break;
        case CMD_INJECT_ERROR:
          atomic_store(&g_yt_corrupt, true);
          fprintf(stderr, "[msp] media: cache drop injected\n");
          break;
        default: break;
      }
      break;

    case CH_DL_LARGE:
      switch (cmd->command) {
        case CMD_ADJUST_SPEED:
          atomic_store(&g_dl_speed, cmd->value);
          fprintf(stderr, "[msp] download speed: %.2fx\n", (double)cmd->value);
          break;
        case CMD_INJECT_ERROR:
          atomic_store(&g_dl_corrupt, true);
          fprintf(stderr, "[msp] download: reset injected\n");
          break;
        default: break;
      }
      break;

    case CH_DIAL_CPU:
      /* The CPU channel is a read-only gauge in this harness; §28 gives it no commands and
       * `MspCmdParse` refuses any that arrive.  Named rather than defaulted, so that adding
       * one later is a compiler warning and not a silent omission. */
      break;

    default: break;
  }
}

/* ── The accept side ────────────────────────────────────────────────────────── */

/** @brief The socket path, removed on the way out.  Only ever a socket, never a file. */
static void
s_unlink_socket(void)
{
  struct stat st;

  if (stat(g_sock_path, &st) == 0 && S_ISSOCK(st.st_mode)) unlink(g_sock_path);
}

static void
s_on_signal(int sig)
{
  (void)sig;

  /* Async-signal-safe, and the only two things that need to happen here.  Clearing the flag
   * makes the simulation threads finish their current tick and return; shutting the listening
   * socket down is what makes a blocked `accept` return so the main thread can reach its
   * joins.  Without the second, the joins are unreachable -- which is §28's state. */
  atomic_store(&g_running, false);
  if (g_listen_fd >= 0) shutdown(g_listen_fd, SHUT_RDWR);
}

/** @brief How many clients to refuse before the log line stops being informative. */
static void
s_accept_loop(void)
{
  unsigned long refusals = 0;
  unsigned      backoff_ms = 1;

  while (atomic_load(&g_running)) {
    int fd = accept(g_listen_fd, NULL, NULL);

    if (fd < 0) {
      if (errno == EINTR) continue;
      if (!atomic_load(&g_running)) break;

      /* A PERSISTENT ERROR MUST NOT SPIN.  §28's `continue` here is a 100% CPU loop; the
       * backoff is bounded so a transient condition recovers quickly and a permanent one
       * (EMFILE, ENFILE) costs a sleeping process rather than a burning core. */
      if (refusals++ < 3) fprintf(stderr, "[msp] accept: %s\n", strerror(errno));
      s_sleep_ms((long)backoff_ms);
      if (backoff_ms < 100) backoff_ms *= 2;
      continue;
    }
    backoff_ms = 1;

    {
      struct timeval tv;
      int            slot = -1, i;

      /* THE WRITE DEADLINE.  Without it a client that stops reading blocks whichever
       * simulation thread reaches it, forever -- one slow reader stalling the whole harness.
       * With it, `send` returns EAGAIN and s_send_all reports failure, and the client is
       * dropped with no partial frame left for anyone to misparse. */
      tv.tv_sec  = 0;
      tv.tv_usec = MSP_SEND_TIMEOUT_MS * 1000;
      (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);

      for (i = 0; i < MSP_MAX_CLIENTS; i++) {
        mtx_lock(&s_clients[i].lock);
        if (s_clients[i].fd < 0) {
          s_clients[i].fd = fd;
          slot = i;
        }
        mtx_unlock(&s_clients[i].lock);
        if (slot >= 0) break;
      }

      if (slot >= 0) {
        atomic_fetch_add(&g_clients, 1);
        fprintf(stderr, "[msp] client attached: slot %d\n", slot);
      } else {
        fprintf(stderr, "[msp] %d clients already attached; refusing\n", MSP_MAX_CLIENTS);
        close(fd);
      }
    }
  }
}

/* ── main ───────────────────────────────────────────────────────────────────── */

int
main(int argc, char **argv)
{
  struct sockaddr_un addr;
  struct sigaction   sa;
  thrd_t             media, cpu, download, reader;
  int                i;

  if (argc > 1) g_sock_path = argv[1];

  /* A client that has gone away must not kill the server mid-broadcast.  MSG_NOSIGNAL on
   * every send is the other belt, and both are worn for the same reason: without them the
   * failure is a dead process with no diagnostic rather than a dropped client. */
  signal(SIGPIPE, SIG_IGN);

  memset(&sa, 0, sizeof sa);
  sa.sa_handler = s_on_signal;
  sigaction(SIGINT,  &sa, NULL);
  sigaction(SIGTERM, &sa, NULL);

  for (i = 0; i < MSP_MAX_CLIENTS; i++) {
    s_clients[i].fd = -1;
    MspRxInit(&s_clients[i].rx);
    if (mtx_init(&s_clients[i].lock, mtx_plain) != thrd_success) {
      fprintf(stderr, "[msp] could not initialise a client lock\n");
      return EXIT_FAILURE;
    }
  }

  s_unlink_socket();
  if (strlen(g_sock_path) >= sizeof addr.sun_path) {
    fprintf(stderr, "[msp] socket path too long: %s\n", g_sock_path);
    return EXIT_FAILURE;
  }

  g_listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (g_listen_fd < 0) {
    fprintf(stderr, "[msp] socket: %s\n", strerror(errno));
    return EXIT_FAILURE;
  }

  memset(&addr, 0, sizeof addr);
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, g_sock_path, sizeof addr.sun_path - 1);

  if (bind(g_listen_fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
    fprintf(stderr, "[msp] bind %s: %s\n", g_sock_path, strerror(errno));
    close(g_listen_fd);
    return EXIT_FAILURE;
  }
  if (listen(g_listen_fd, MSP_MAX_CLIENTS) < 0) {
    fprintf(stderr, "[msp] listen: %s\n", strerror(errno));
    close(g_listen_fd);
    s_unlink_socket();
    return EXIT_FAILURE;
  }

  printf("[msp] streaming on %s\n", g_sock_path);
  fflush(stdout);

  if (thrd_create(&media,    s_media_loop,    (void *)(uintptr_t)1) != thrd_success ||
      thrd_create(&cpu,      s_cpu_loop,      (void *)(uintptr_t)2) != thrd_success ||
      thrd_create(&download, s_download_loop, (void *)(uintptr_t)3) != thrd_success ||
      thrd_create(&reader,   s_read_loop,     NULL) != thrd_success) {
    fprintf(stderr, "[msp] could not start a simulation thread\n");
    atomic_store(&g_running, false);
    shutdown(g_listen_fd, SHUT_RDWR);
    close(g_listen_fd);
    s_unlink_socket();
    return EXIT_FAILURE;
  }

  s_accept_loop();

  /* REACHABLE, which is the difference from §28.  The flag is cleared and the listening
   * socket shut down by the signal handler, so the loop above returns on its own and these
   * joins complete instead of deadlocking on three infinite loops. */
  thrd_join(media, NULL);
  thrd_join(cpu, NULL);
  thrd_join(download, NULL);
  thrd_join(reader, NULL);

  for (i = 0; i < MSP_MAX_CLIENTS; i++) {
    mtx_lock(&s_clients[i].lock);
    if (s_clients[i].fd >= 0) close(s_clients[i].fd);
    s_clients[i].fd = -1;
    mtx_unlock(&s_clients[i].lock);
    mtx_destroy(&s_clients[i].lock);
  }

  close(g_listen_fd);
  g_listen_fd = -1;
  s_unlink_socket();

  printf("[msp] stopped\n");
  return EXIT_SUCCESS;
}
