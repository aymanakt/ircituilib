/**
 * @file conversation_stream_server.c
 * @brief The conversation stream's simulation server: a daemon that writes the frozen
 *        reference's conversation down a TCP socket, one JSON frame per line.
 *
 * ## What this gate is, and what it is not
 *
 * This is Gate 1: the TRANSPORT. It binds, it accepts, it sends whole frames, it drops a
 * client that stops reading rather than stalling behind it, and it has a shutdown path
 * that is actually reached. What it emits is fixed -- the fixture's envelope, one frame of
 * each of the four shapes, and the nine golden messages replayed -- so that its output can
 * be checked frame-for-frame against the frozen fixture rather than against itself. The
 * live scenario generator that replaces the replay is Gate 2's.
 *
 * ## The four defects this design is built against
 *
 * They are the same class the MSP harness's own server records, and they are fixed as it
 * is written rather than after:
 *
 * 1. A partial frame is never left on the wire. The design document checks `send`'s
 *    result only for a negative return, so a short write is ignored and the peer's
 *    reassembler desynchronises permanently. Here `sSendAll` sends the whole frame or
 *    reports failure, and a failure closes the client.
 * 2. A stalled client cannot stall anyone else. The document's broadcast holds the pool
 *    lock across a blocking `send`, so one client that stops reading freezes every other
 *    client and the simulation thread with them. Here each client has its own thread and
 *    its own send mutex, and every client fd carries `SO_SNDTIMEO`, so a send that cannot
 *    make progress fails and drops that client alone.
 * 3. A failed `accept` does not spin. The document does `if (new_socket < 0) continue;`,
 *    which is a 100% CPU loop on any persistent error such as EMFILE.
 * 4. There is a shutdown path at all. The document's `server_running` is never cleared, so
 *    its `close(server_fd)` is unreachable and the only exit is a signal. Here SIGINT and
 *    SIGTERM clear a flag and shut the listening socket down, so the joins are reached.
 *
 * ## What it does NOT link
 *
 * Nothing. No ircituilib, no curses, no PNG, and not even uflib -- the golden replay is a
 * table transcribed from the frozen fixture rather than the fixture read at runtime. The
 * absent `target_link_libraries` in CMakeLists.txt is the assertion that the protocol and
 * its server are independent of the framework they feed. `dev/probes/conversation_streaming/`
 * asserts the replay against the fixture file, so a transcription error cannot hide here.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "conversation_stream_protocol.h"

#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

/* ── Shared state ───────────────────────────────────────────────────────────── */

static atomic_bool      g_running = true;
static atomic_int       g_clients = 0;
static atomic_int       g_ready = 0;
static int              g_listen_fd = -1;
static pthread_mutex_t  g_pool_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct ConvStreamSlot {
    int             fd;
    bool            active;
    /* ACTIVE IS NOT ENOUGH TO BROADCAST TO. A slot joins the pool in the accept loop, but its
     * client has not been sent the replay yet -- so a generator broadcasting in that window
     * would interleave live frames into the replay and the replay would stop being a contiguous
     * prefix a client can validate against the frozen fixture. `ready` is set once the burst has
     * gone out, and the broadcast skips a slot until it is. */
    bool            ready;
    pthread_mutex_t send_mutex;
    pthread_t       thread;
} ConvStreamSlot;

static ConvStreamSlot g_pool[CONV_STREAM_MAX_CLIENTS];

/* ── The frozen reference, transcribed ──────────────────────────────────────── */

typedef struct ConvStreamGolden {
    uint64_t                  message_id;
    const char               *sender_ptr;
    const char               *stamp_ptr;
    uint64_t                  epoch;
    int                       text_index;
    ConvStreamDirection      direction;
    ConvStreamStatus         status;
    ConvStreamKind           kind;
    bool                      is_collapsed;
    bool                      has_reply;
    uint64_t                  reply_msg_id;
    const char               *reply_sender_ptr;
    const char              *reply_snippet_ptr;
    const ConvStreamReaction *reactions_ptr; ///< NULL when the message carries none.
    int                      reaction_count;
} ConvStreamGolden;

/* The two badged messages, as their own arrays: `{ "...", n }` leaves emoji's remaining
 * bytes zero, so there is no partially-braced subobject to initialise. */
static const ConvStreamReaction kBadges1003[] = {
    {"\xF0" "\x9F" "\x91" "\x8D", 2},
    {"\xF0" "\x9F" "\x91" "\x80", 1},
};
static const ConvStreamReaction kBadges1006[] = {
    {"\xF0" "\x9F" "\x94" "\xA5", 4},
};

static const char *const kGoldenText[] = {
    "Encryption Key Verified. Channel secured via Affero GPL v3 standard.",
    "Hey team, did we manage to trace that heap-use-after-free defect in the sFreeWidgetState loop?",
    "I measured it on the test harness client, and it seems like the recycler rows are occasionally retaining the "
    "previous position's memory maps during scrolling boundaries.",
    "Never mind, I think the G10 fix path solves it. Let me know if you can replicate it.",
    "Acknowledged.",
    "Yes, I tracked it down to the frame's dirty list. The pointers were staying on the stack across deflate ticks.",
    "I am pushing the structural synchronization fix to master now.",
    "This is a framework verification macro-message used to test maximum volume layout rendering parameters inside the "
    "virtual cell matrix. It is designed to overflow the 65 percent width barrier, force a string chunk reflow "
    "iteration, trigger character truncation limits if configured, and test overall vertical grid budgeting.",
    "This block represents an expired temporary message to verify opacity changes.",
};

/* The fixture's nine messages. Two are status indicators rather than conversation: the
 * system notice, and 1005, whose override key names the local-side indicator. The epochs
 * are derived from the fixture's own clock stamps off the frozen base, so the gaps the
 * stamps show are the gaps the wire carries. */
static const ConvStreamGolden kGolden[] = {
    {1001, "System", "08:30", 1789979400ULL, 0, CONV_STREAM_DIR_SYSTEM, CONV_STREAM_STATUS_READ,
     CONV_STREAM_KIND_STATUS_INDICATOR, false, false, 0ULL, "", "", NULL, 0},
    {1002, "Alice", "08:31", 1789979460ULL, 1, CONV_STREAM_DIR_INBOUND, CONV_STREAM_STATUS_READ,
     CONV_STREAM_KIND_MESSAGE, false, false, 0ULL, "", "", NULL, 0},
    {1003, "Alice", "08:31", 1789979460ULL, 2, CONV_STREAM_DIR_INBOUND, CONV_STREAM_STATUS_READ,
     CONV_STREAM_KIND_MESSAGE, false, false, 0ULL, "", "",
     kBadges1003, 2},
    {1004, "Alice", "08:32", 1789979520ULL, 3, CONV_STREAM_DIR_INBOUND, CONV_STREAM_STATUS_READ,
     CONV_STREAM_KIND_MESSAGE, false, false, 0ULL, "", "", NULL, 0},
    {1005, "Bob", "08:33", 1789979580ULL, 4, CONV_STREAM_DIR_OUTBOUND, CONV_STREAM_STATUS_READ,
     CONV_STREAM_KIND_STATUS_INDICATOR, false, false, 0ULL, "", "", NULL, 0},
    {1006, "Bob", "08:34", 1789979640ULL, 5, CONV_STREAM_DIR_OUTBOUND, CONV_STREAM_STATUS_DELIVERED,
     CONV_STREAM_KIND_MESSAGE, false, true, 1003ULL, "Alice", "I measured it on the test harness...",
     kBadges1006, 1},
    {1007, "Bob", "08:34", 1789979640ULL, 6, CONV_STREAM_DIR_OUTBOUND, CONV_STREAM_STATUS_SENDING,
     CONV_STREAM_KIND_MESSAGE, false, false, 0ULL, "", "", NULL, 0},
    {1008, "Charlie", "08:35", 1789979700ULL, 7, CONV_STREAM_DIR_INBOUND, CONV_STREAM_STATUS_READ,
     CONV_STREAM_KIND_MESSAGE, true, false, 0ULL, "", "", NULL, 0},
    {1009, "Alice", "08:36", 1789979760ULL, 8, CONV_STREAM_DIR_INBOUND, CONV_STREAM_STATUS_EXPIRED,
     CONV_STREAM_KIND_MESSAGE, false, false, 0ULL, "", "", NULL, 0},
};

#define CONV_STREAM_GOLDEN_COUNT ((int)(sizeof kGolden / sizeof kGolden[0]))

static void
sGoldenToMessage(const ConvStreamGolden *gold_ptr, ConvStreamMessage *msg_ptr)
{
    int r;

    memset(msg_ptr, 0, sizeof *msg_ptr);
    msg_ptr->message_id = gold_ptr->message_id;
    snprintf(msg_ptr->sender_name, sizeof msg_ptr->sender_name, "%s", gold_ptr->sender_ptr);
    snprintf(msg_ptr->timestamp, sizeof msg_ptr->timestamp, "%s", gold_ptr->stamp_ptr);
    msg_ptr->timestamp_epoch = gold_ptr->epoch;
    msg_ptr->text_ptr = kGoldenText[gold_ptr->text_index];
    msg_ptr->direction = gold_ptr->direction;
    msg_ptr->status = gold_ptr->status;
    msg_ptr->cluster = CONV_STREAM_CLUSTER_LONE; /* ADVISORY: the widget computes this. */
    msg_ptr->kind = gold_ptr->kind;
    msg_ptr->is_collapsed = gold_ptr->is_collapsed;
    msg_ptr->has_reply_context = gold_ptr->has_reply;
    msg_ptr->reply_msg_id = gold_ptr->reply_msg_id;
    if (gold_ptr->has_reply) {
        snprintf(msg_ptr->reply_sender, sizeof msg_ptr->reply_sender, "%s", gold_ptr->reply_sender_ptr);
        msg_ptr->reply_snippet_ptr = gold_ptr->reply_snippet_ptr;
    }
    for (r = 0; gold_ptr->reactions_ptr && r < gold_ptr->reaction_count && r < CONV_STREAM_BADGES_MAX; r++) {
        memcpy(msg_ptr->reactions[r].emoji, gold_ptr->reactions_ptr[r].emoji, CONV_STREAM_EMOJI_MAX);
        msg_ptr->reactions[r].count = gold_ptr->reactions_ptr[r].count;
    }
    msg_ptr->reaction_count = gold_ptr->reaction_count;
}

/* ── The transport ──────────────────────────────────────────────────────────── */

/**
 * @brief Send every byte, or report failure.
 *
 * The invariant this buys: a client that is still connected was only ever sent WHOLE
 * frames. A peer that goes away mid-frame sees EOF part-way through a line, which its
 * reassembler discards rather than parsing as a shorter frame.
 *
 * @param flags MSG_DONTWAIT for a broadcast, which must not be held up by one reader; 0 for
 *              an attach burst, which runs on that client's own thread and may block.
 */
static int
sSendAll(int fd, const char *buf_ptr, size_t len, int flags)
{
    size_t sent = 0;

    while (sent < len) {
        ssize_t n = send(fd, buf_ptr + sent, len - sent, MSG_NOSIGNAL | flags);

        if (n > 0) {
            sent += (size_t)n;
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        return -1;
    }
    return 0;
}

/** @brief One frame to one client, under that client's send mutex. */
static int
sSendFrame(ConvStreamSlot *slot_ptr, const char *buf_ptr, int len, int flags)
{
    int rc;

    if (len <= 0) return -1;
    pthread_mutex_lock(&slot_ptr->send_mutex);
    rc = sSendAll(slot_ptr->fd, buf_ptr, (size_t)len, flags);
    pthread_mutex_unlock(&slot_ptr->send_mutex);
    return rc;
}

/* ── Clusters, computed the way the widget computes them ────────────────────────
 *
 * A run is a maximal stretch of one sender's messages in one direction, and neither side of a
 * pair may be a system notice or a status indicator -- the widget's own rule, `sSameRun` at
 * src/ui_conversation.c:63-70. The wire therefore carries the cluster each message will
 * ACTUALLY be rendered with rather than a value the widget is about to overwrite, and the
 * census re-derives the same decomposition from the stream and fails if the two ever differ.
 *
 * THE FIXTURE DISAGREES WITH THIS FOR THREE OF ITS NINE MESSAGES. It names 1005 `lone` and
 * 1006 `top`, while the rule above makes 1005..1007 one outbound run; FIXTURE-FIELDS.md
 * records exactly that ("a shape the algorithm never emits"). The advisory field loses
 * nothing by being correct, and one rule for every frame is what lets the census check them
 * all without a special case for the replay.
 */

/** @brief The widget's run predicate, restated for the wire's own type. */
static bool
sSameRun(const ConvStreamMessage *a_ptr, const ConvStreamMessage *b_ptr)
{
    if (a_ptr->direction == CONV_STREAM_DIR_SYSTEM || b_ptr->direction == CONV_STREAM_DIR_SYSTEM) return false;
    if (a_ptr->kind != CONV_STREAM_KIND_MESSAGE || b_ptr->kind != CONV_STREAM_KIND_MESSAGE) return false;
    if (a_ptr->direction != b_ptr->direction) return false;
    return strcmp(a_ptr->sender_name, b_ptr->sender_name) == 0;
}

/** @brief Assign every message's cluster from the runs it sits in. */
static void
sAssignClusters(ConvStreamMessage *msgs_ptr, int count)
{
    int i = 0;

    while (i < count) {
        int j = i;
        int k;

        while (j + 1 < count && sSameRun(&msgs_ptr[j], &msgs_ptr[j + 1])) j++;
        for (k = i; k <= j; k++) {
            if (i == j) msgs_ptr[k].cluster = CONV_STREAM_CLUSTER_LONE;
            else if (k == i) msgs_ptr[k].cluster = CONV_STREAM_CLUSTER_TOP;
            else if (k == j) msgs_ptr[k].cluster = CONV_STREAM_CLUSTER_BOTTOM;
            else msgs_ptr[k].cluster = CONV_STREAM_CLUSTER_MIDDLE;
        }
        i = j + 1;
    }
}

/**
 * @brief Retire one client and free its slot.
 *
 * Under the pool lock, and re-checking the fd, because a slot's own thread can release it at
 * any moment and the slot be handed to somebody else in between -- deactivating on a bare
 * index would close the NEW client's socket.
 */
static void
sDropSlot(int slot_index, int fd)
{
    pthread_mutex_lock(&g_pool_mutex);
    if (g_pool[slot_index].active && g_pool[slot_index].fd == fd) {
        close(fd);
        g_pool[slot_index].fd = -1;
        g_pool[slot_index].active = false;
        if (g_pool[slot_index].ready) atomic_fetch_sub(&g_ready, 1);
        g_pool[slot_index].ready = false;
        atomic_fetch_sub(&g_clients, 1);
        printf("[SERVER] slot %d retired (%d attached)\n", slot_index, atomic_load(&g_clients));
    }
    pthread_mutex_unlock(&g_pool_mutex);
}

/**
 * @brief One frame to every attached client.
 *
 * THE POOL LOCK IS NOT HELD ACROSS A SEND. That is the design document's second defect: its
 * broadcast holds the lock for the whole loop, so one client that stops reading freezes every
 * other client and the simulation thread with them. Here the active slots are SNAPSHOT under
 * the lock, the lock is released, and only then is anything written -- and the write is
 * non-blocking, so a reader that has fallen behind is dropped rather than waited for.
 *
 * @return How many clients took the whole frame.
 */
static int
sBroadcast(const char *buf_ptr, int len)
{
    struct {
        int index;
        int fd;
    } taken[CONV_STREAM_MAX_CLIENTS];
    int n = 0, i, delivered = 0;

    pthread_mutex_lock(&g_pool_mutex);
    for (i = 0; i < CONV_STREAM_MAX_CLIENTS; i++) {
        if (g_pool[i].active && g_pool[i].ready) {
            taken[n].index = i;
            taken[n].fd = g_pool[i].fd;
            n++;
        }
    }
    pthread_mutex_unlock(&g_pool_mutex);

    for (i = 0; i < n; i++) {
        if (sSendFrame(&g_pool[taken[i].index], buf_ptr, len, MSG_DONTWAIT) != 0) {
            printf("[SERVER] slot %d is not keeping up; dropping it rather than delaying the others\n",
                   taken[i].index);
            sDropSlot(taken[i].index, taken[i].fd);
            continue;
        }
        delivered++;
    }
    return delivered;
}

/* ── The simulation's clock ─────────────────────────────────────────────────────
 *
 * ONE CLOCK, RENDERED TWO WAYS. `timestamp_epoch` and the display `timestamp` are both derived from
 * `sim_millis`, so they cannot disagree about how much time has passed. The pair they replace did:
 * the epoch advanced 7 seconds per message while the display advanced a whole minute, measured at
 * 8.6x. A client ordering by one field and showing the other was reading two different stories.
 *
 * THE RATE IS THE DISPLAY'S, because that is the one the frozen fixture agrees with -- its nine
 * stamps step about a minute apart. The epoch was the field that was wrong.
 *
 * THE BASE IS CHOSEN SO THEY AGREE ABSOLUTELY TOO. 1789979400 is 08:30:00 UTC, which is where the
 * fixture's first stamp sits, so a client comparing the two fields finds the same instant rather
 * than a constant zone offset, and the live stream continues from the replay's last stamp (08:37)
 * instead of restarting the day.
 *
 * ONLY MESSAGE-SHAPED FRAMES ADVANCE IT. A presence frame carries the current instant and moves
 * nothing: a typing row is transient state, not an event in the conversation's timeline, and
 * letting it advance the clock would make the stamps jump a minute for a keystroke. */

#define CONV_STREAM_SIM_BASE_EPOCH 1789979400ULL  /* 08:30:00 UTC */
/* 08:38, and not 08:37: the replay's LAST frame is the media slice stamped 08:37, so starting the
 * live clock on that same minute would put two frames on one instant and the step between them at
 * zero. The census asserts every step is one step, which is how this was found. */
#define CONV_STREAM_SIM_START_MILLIS 480000ULL
#define CONV_STREAM_SIM_STEP_MS 60000ULL          /* a message a minute, the fixture's own pace */

static uint64_t
sSimEpoch(uint64_t sim_millis)
{
    return CONV_STREAM_SIM_BASE_EPOCH + sim_millis / 1000;
}

/** @brief The display stamp for a simulated instant: UTC, so it matches the epoch beside it. */
static void
sSimStamp(uint64_t sim_millis, char *out_ptr, size_t cap)
{
    time_t    when = (time_t)sSimEpoch(sim_millis);
    struct tm parts;

    gmtime_r(&when, &parts);
    snprintf(out_ptr, cap, "%02d:%02d", parts.tm_hour, parts.tm_min);
}

/* ── The generator's own state ──────────────────────────────────────────────── */

/** The most recent messages, so a reply can name its target and quote it. */
#define CONV_STREAM_HISTORY 4
#define CONV_STREAM_MAX_RUN 4
#define CONV_STREAM_SNIPPET_CLIP 32

typedef struct ConvStreamHistoryEntry {
    uint64_t message_id;
    char     sender_name[CONV_STREAM_NAME_MAX];
    char     snippet[CONV_STREAM_SNIPPET_MAX];
} ConvStreamHistoryEntry;

/* The fixture's own cast, kept exactly: Alice and Charlie speak inbound, Bob is the local
 * user's outbound identity, and System carries the notices. No new identity is invented. */
static const char *const kInboundSenders[] = {"Alice", "Charlie"};
#define CONV_STREAM_INBOUND_COUNT ((int)(sizeof kInboundSenders / sizeof kInboundSenders[0]))
#define CONV_STREAM_OUTBOUND_SENDER "Bob"

static struct {
    uint64_t               next_id;
    ConvStreamHistoryEntry history[CONV_STREAM_HISTORY];
    int                    history_count;
    char                   last_sender[CONV_STREAM_NAME_MAX];
    ConvStreamDirection    last_direction;
    ConvStreamKind         last_kind;
    bool                   has_last;
    int                    scene;
    int                    run_left;
    int                    run_index;
    /* The current run, built once so its clusters come from sAssignClusters rather than from
     * a second copy of the arithmetic. Stable storage, because the frames borrow these. */
    ConvStreamMessage      run_msgs[CONV_STREAM_MAX_RUN];
    char                   run_text[CONV_STREAM_MAX_RUN][CONV_STREAM_TEXT_MAX];
    char                   run_snip[CONV_STREAM_MAX_RUN][CONV_STREAM_SNIPPET_MAX];
    int                    media_index;
    int                    base_cadence_ms;
    uint64_t               sim_millis;
    /* The emit path's two facts about the current run, kept here rather than as a whole plan
     * because ConvStreamPlan is declared with the generator, below this state. */
    bool                   run_is_media;
    int                    run_count;
    /* The control channel's four fields. Written by whichever client thread parsed a command and
     * read by the generator, so they live under g_gen_mutex rather than being reasoned about. */
    bool                   paused;
    int                    speed_pct;      /* 100 is the configured cadence */
    int                    pending_scene;  /* -1 when the cycle is running free */
    bool                   step_once;
} g_gen;

static pthread_mutex_t g_gen_mutex = PTHREAD_MUTEX_INITIALIZER;
static atomic_ullong   g_gen_frames = 0;

/* Defined with the generator below, and declared here because the attach burst remembers the
 * replay's last message so the generator cannot open a run that would join it. */
static void sRemember(const ConvStreamMessage *msg_ptr);

/* Also defined with the generator: a client thread parses a command and this answers it, so the
 * declaration has to reach back past the generator's own section. */
static void sAnswerCommand(ConvStreamCommandKind kind, int arg, const char *raw_ptr);

/** @brief Stamp one message from the clock, and step the clock on. The one place either happens. */
static void
sStampAndAdvance(ConvStreamMessage *msg_ptr)
{
    msg_ptr->timestamp_epoch = sSimEpoch(g_gen.sim_millis);
    sSimStamp(g_gen.sim_millis, msg_ptr->timestamp, sizeof msg_ptr->timestamp);
    g_gen.sim_millis += CONV_STREAM_SIM_STEP_MS;
}

/**
 * @brief What a client is sent when it attaches: the envelope, one frame of each shape, and
 *        the nine golden messages replayed.
 *
 * The replay's cluster positions are COMPUTED, not the fixture's -- see the note above
 * sAssignClusters for why three of the nine differ from the file. The replay's last message is
 * remembered so the generator's first run cannot open a continuation the widget would join.
 */
static int
sSendAttachBurst(ConvStreamSlot *slot_ptr)
{
    /* ON THE STACK, AND NOT `static`. A function-static buffer is shared by every thread that calls
     * the function, and this one is called by EVERY client thread -- so two clients attaching at the
     * same moment wrote their frames into the same bytes and each other's frames arrived spliced.
     * Measured: `"id": 004` where a field had been overwritten, and two frames' text interleaved.
     * A caller-local buffer is per-thread by construction, which is the only version of this that
     * cannot race. Nothing here outlives the call: the frame is copied into the socket by send(). */
    char              line[CONV_STREAM_LINE_MAX];
    ConvStreamMessage msgs[CONV_STREAM_GOLDEN_COUNT];
    ConvStreamGolden  media;
    int               i, n;

    if ((n = ConvStreamBuildHello(line, sizeof line)) < 0) return -1;
    if (sSendFrame(slot_ptr, line, n, 0) != 0) return -1;

    if ((n = ConvStreamBuildPresence(1, "Alice", 1789979760ULL, line, sizeof line)) < 0) return -1;
    if (sSendFrame(slot_ptr, line, n, 0) != 0) return -1;

    for (i = 0; i < CONV_STREAM_GOLDEN_COUNT; i++) sGoldenToMessage(&kGolden[i], &msgs[i]);
    sAssignClusters(msgs, CONV_STREAM_GOLDEN_COUNT);
    for (i = 0; i < CONV_STREAM_GOLDEN_COUNT; i++) {
        if ((n = ConvStreamBuildMessage(&msgs[i], line, sizeof line)) < 0) {
            fprintf(stderr, "[SERVER] golden message %d does not fit a line\n", i);
            return -1;
        }
        if (sSendFrame(slot_ptr, line, n, 0) != 0) return -1;
    }
    sRemember(&msgs[CONV_STREAM_GOLDEN_COUNT - 1]);

    /* A transfer slice, carrying the message fields a client needs to make a row for an id it
     * has not seen. Its status is `sending`, which is the state the widget draws a hollow
     * circle for. */
    memset(&media, 0, sizeof media);
    media.message_id = 1010;
    media.sender_ptr = CONV_STREAM_OUTBOUND_SENDER;
    media.stamp_ptr = "08:37";
    media.epoch = 1789979820ULL; /* 08:37 */
    media.direction = CONV_STREAM_DIR_OUTBOUND;
    media.status = CONV_STREAM_STATUS_SENDING;
    media.kind = CONV_STREAM_KIND_MESSAGE;
    sGoldenToMessage(&media, &msgs[0]);
    msgs[0].text_ptr = "[Transferring Media File: harness_dump.core]";
    msgs[0].cluster = CONV_STREAM_CLUSTER_LONE;
    if ((n = ConvStreamBuildMediaProgress(&msgs[0], "harness_dump.core", 25, line, sizeof line)) < 0) return -1;
    if (sSendFrame(slot_ptr, line, n, 0) != 0) return -1;
    /* Remembered too, and not just the last golden message: this frame is Bob's and outbound, so
     * a generator whose next run was also Bob's would join it and the LONE already sent would be
     * wrong. The scene order happens to avoid that today; this keeps the invariant a property of
     * the state rather than of the ordering. */
    sRemember(&msgs[0]);

    if ((n = ConvStreamBuildPresence(0, NULL, 1789979820ULL, line, sizeof line)) < 0) return -1;
    return sSendFrame(slot_ptr, line, n, 0);
}

/**
 * @brief One client's whole life: its burst, then read until it goes away.
 *
 * Reading is not decoration. A client that stops reading fills the kernel's receive
 * window, which is what makes the next send time out and this client be dropped -- so the
 * thread has to be here to keep the socket honest, and the slot is released on this thread
 * rather than by whoever noticed the failure.
 */
static void *
sClientRoutine(void *arg_ptr)
{
    ConvStreamSlot *slot_ptr = arg_ptr;
    char            sink[512];
    int             fd = slot_ptr->fd;

    if (sSendAttachBurst(slot_ptr) != 0) {
        printf("[SERVER] slot %ld: the attach burst did not complete; dropping the client\n",
               (long)(slot_ptr - g_pool));
    }
    else {
        /* Under the pool lock, because the generator reads it: this is the moment the slot
         * becomes a broadcast destination, and the two must not race. */
        pthread_mutex_lock(&g_pool_mutex);
        if (slot_ptr->active && slot_ptr->fd == fd && !slot_ptr->ready) {
            slot_ptr->ready = true;
            atomic_fetch_add(&g_ready, 1);
        }
        pthread_mutex_unlock(&g_pool_mutex);
        printf("[SERVER] slot %ld: attach burst sent, replay is contiguous\n", (long)(slot_ptr - g_pool));
    }

    /* Until the peer closes, or the daemon is shutting down. SO_RCVTIMEO bounds the wait
     * so shutdown is deterministic without a live client being dropped for being quiet.
     *
     * THIS IS ALSO WHERE A CLIENT TALKS BACK. Until the control channel existed this loop read
     * into a sink and threw the bytes away -- the server had no inbound path at all, so a person
     * testing a widget could only wait for the generator's cycle to reach the state they wanted.
     * Now a line is accumulated and parsed, and a command changes what the generator does next. */
    {
        char cmd[CONV_STREAM_CMD_LINE_MAX];
        size_t used = 0;

        for (;;) {
            ssize_t n = recv(fd, sink, sizeof sink, 0);

            if (n > 0) {
                const char *p_ptr = sink;
                size_t      left = (size_t)n;

                while (left > 0) {
                    const char *nl_ptr = memchr(p_ptr, '\n', left);
                    size_t      take = nl_ptr ? (size_t)(nl_ptr - p_ptr) : left;

                    if (used + take < sizeof cmd) {
                        memcpy(cmd + used, p_ptr, take);
                        used += take;
                    }
                    else {
                        /* An over-long line is refused whole rather than truncated into something
                         * that might parse: a client that cannot stay inside the bound is not one
                         * whose half a command should be acted on. */
                        printf("[SERVER] slot %ld sent a command line past %d bytes; refused\n",
                               (long)(slot_ptr - g_pool), CONV_STREAM_CMD_LINE_MAX);
                        sAnswerCommand(CONV_STREAM_CMD_UNKNOWN, 0, "");
                        used = 0;
                    }
                    if (nl_ptr) {
                        char                  raw[CONV_STREAM_CMD_VALUE_MAX];
                        int                   arg = 0;
                        ConvStreamCommandKind kind;

                        cmd[used] = '\0';
                        if (used > 0) {
                            kind = ConvStreamParseCommand(cmd, &arg, raw, sizeof raw);
                            sAnswerCommand(kind, arg, raw);
                        }
                        used = 0;
                    }
                    if (!nl_ptr) break;
                    p_ptr = nl_ptr + 1;
                    left -= take + 1;
                }
                continue;
            }
            if (n < 0 && errno == EINTR) continue;
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                if (!atomic_load(&g_running)) break;
                continue;
            }
            break;
        }
    }

    pthread_mutex_lock(&g_pool_mutex);
    if (slot_ptr->active && slot_ptr->fd == fd) {
        close(fd);
        slot_ptr->fd = -1;
        slot_ptr->active = false;
        if (slot_ptr->ready) atomic_fetch_sub(&g_ready, 1);
        slot_ptr->ready = false;
        /* THE SEND MUTEX IS NOT DESTROYED HERE, and that is the fix for a real hazard
         * rather than an omission. The slot goes back into the pool and is handed to the
         * next client; destroying the mutex on the way out would leave the next attach
         * locking a destroyed one, which is undefined. The mutexes are initialised once,
         * for the process, and never destroyed -- which is the correct lifetime for a
         * fixed pool that outlives every client that passes through it. */
        atomic_fetch_sub(&g_clients, 1);
        printf("[SERVER] slot %ld: client gone, %d attached\n", (long)(slot_ptr - g_pool), atomic_load(&g_clients));
    }
    pthread_mutex_unlock(&g_pool_mutex);
    return NULL;
}

/* ── Binding, and the way out ───────────────────────────────────────────────── */

/* ── The generator ──────────────────────────────────────────────────────────────
 *
 * Its unit is a RUN rather than a message, and that is forced by the wire's honesty about
 * clusters: a cluster position depends on the message AFTER it, so a generator that decided one
 * message at a time could not know whether the message it is sending is a TOP or a LONE. A run
 * is planned whole, its clusters are assigned by sAssignClusters, and one frame is emitted per
 * tick.
 *
 * THE SCENE TABLE IS CYCLED, NOT SAMPLED, and that is a deliberate difference from the design
 * document's probability table. The gate's exit criterion is "every enum value in every field
 * observed over a run", and a cycle makes that a property of the table rather than of luck --
 * the census asserts the coverage, and it is reachable deterministically. The variation inside
 * a cycle (which sender, which body, which badge) keeps the stream from looking mechanical.
 *
 * EVERY SCENE BOUNDARY IS A RUN BOUNDARY. Two adjacent runs of the same sender and direction
 * would be one run to the widget, and the clusters already sent would be wrong; so the plan
 * picks a sender that does not continue the previous message, and the scenes are ordered so no
 * two same-direction message runs are adjacent.
 */

typedef enum {
    SCENE_RUN_INBOUND_3 = 0, /* top/middle/bottom */
    SCENE_PRESENCE_ON,
    SCENE_RUN_OUTBOUND_2, /* sending, delivered */
    SCENE_SYSTEM,         /* a status indicator and never in a block */
    SCENE_RUN_INBOUND_4,  /* two middles */
    SCENE_INDICATOR,      /* local-side indicator, also never in a block */
    SCENE_PRESENCE_OFF,
    SCENE_MEDIA,          /* a transfer, three slices sharing one id */
    SCENE_RUN_EXPIRED,    /* the decay state */
    SCENE_RUN_BADGED,     /* a reply, badges, and a collapsed body */
    SCENE_COUNT
} ConvStreamScene;

typedef struct ConvStreamPlan {
    bool                is_run;
    int                 count;
    ConvStreamDirection direction;
    ConvStreamStatus    status;
    ConvStreamKind      kind;
    bool                with_reply;
    bool                with_badges;
    bool                collapsed;
    bool                progresses; ///< An outbound run whose statuses advance: sending -> delivered.
    bool                is_media;   ///< A transfer: media_progress frames sharing one id.
} ConvStreamPlan;

static const char *const kBodies[] = {
    "Did we manage to trace that heap-use-after-free defect in the sFreeWidgetState loop?",
    "Looking at the layout canvas updates right now.",
    "Acknowledged -- the recycler rows are fixed.",
    "Can someone review the memory-leak patch on master?",
    "The frame's dirty list is where the pointers were surviving a deflate tick.",
    "I am pushing the structural synchronization fix now.",
};

static const char *const kMediaFiles[] = {"harness_dump.core", "ui_canvas_spec.pdf", "theme_matrix.json"};

/** @brief A long body, the one the collapse ceiling exists for. */
static const char *const kMacro =
    "This is a framework verification macro-message used to test maximum volume layout rendering "
    "parameters inside the virtual cell matrix. It is designed to overflow the 65 percent width "
    "barrier, force a string chunk reflow iteration, trigger character truncation limits if "
    "configured, and test overall vertical grid budgeting.";

static ConvStreamPlan
sPlanFor(int scene)
{
    ConvStreamPlan plan;

    memset(&plan, 0, sizeof plan);
    plan.kind = CONV_STREAM_KIND_MESSAGE;
    plan.direction = CONV_STREAM_DIR_INBOUND;
    plan.status = CONV_STREAM_STATUS_READ;

    switch ((ConvStreamScene)scene) {
        case SCENE_RUN_INBOUND_3:
            plan.is_run = true;
            plan.count = 3;
            break;
        case SCENE_PRESENCE_ON:
        case SCENE_PRESENCE_OFF:
            break;
        case SCENE_RUN_OUTBOUND_2:
            plan.is_run = true;
            plan.count = 2;
            plan.direction = CONV_STREAM_DIR_OUTBOUND;
            plan.status = CONV_STREAM_STATUS_SENDING;
            plan.progresses = true;
            break;
        case SCENE_SYSTEM:
            plan.direction = CONV_STREAM_DIR_SYSTEM;
            plan.kind = CONV_STREAM_KIND_STATUS_INDICATOR;
            break;
        case SCENE_RUN_INBOUND_4:
            plan.is_run = true;
            plan.count = 4;
            break;
        case SCENE_INDICATOR:
            plan.direction = CONV_STREAM_DIR_OUTBOUND;
            plan.kind = CONV_STREAM_KIND_STATUS_INDICATOR;
            break;
        case SCENE_MEDIA:
            plan.is_run = true;
            plan.count = 3;
            plan.direction = CONV_STREAM_DIR_OUTBOUND;
            plan.status = CONV_STREAM_STATUS_SENDING;
            plan.is_media = true;
            break;
        case SCENE_RUN_EXPIRED:
            plan.is_run = true;
            plan.count = 1;
            plan.status = CONV_STREAM_STATUS_EXPIRED;
            break;
        case SCENE_RUN_BADGED:
            plan.is_run = true;
            plan.count = 1;
            plan.with_reply = true;
            plan.with_badges = true;
            plan.collapsed = true;
            break;
        case SCENE_COUNT:
            break;
    }
    return plan;
}

/** @brief Push one message onto the reply history, with the snippet a reply would quote. */
static void
sRemember(const ConvStreamMessage *msg_ptr)
{
    ConvStreamHistoryEntry *entry_ptr;
    size_t                  len, clip = CONV_STREAM_SNIPPET_CLIP;

    if (g_gen.history_count == CONV_STREAM_HISTORY) {
        memmove(&g_gen.history[0], &g_gen.history[1], sizeof g_gen.history[0] * (CONV_STREAM_HISTORY - 1));
        g_gen.history_count--;
    }
    entry_ptr = &g_gen.history[g_gen.history_count++];
    entry_ptr->message_id = msg_ptr->message_id;
    snprintf(entry_ptr->sender_name, sizeof entry_ptr->sender_name, "%s", msg_ptr->sender_name);

    len = strlen(msg_ptr->text_ptr);
    if (len < clip) clip = len;
    snprintf(entry_ptr->snippet, sizeof entry_ptr->snippet, "%.*s%s", (int)clip, msg_ptr->text_ptr,
             len > clip ? "..." : "");

    snprintf(g_gen.last_sender, sizeof g_gen.last_sender, "%s", msg_ptr->sender_name);
    g_gen.last_direction = msg_ptr->direction;
    g_gen.last_kind = msg_ptr->kind;
    g_gen.has_last = true;
}

/**
 * @brief The reply target: the most recent remembered message from a DIFFERENT sender.
 *
 * The design document hardcodes the target to `id - 1` and the sender to "Alice" whatever the
 * message actually was, so its reply header names the wrong person whenever the previous
 * message was not Alice's. Here the sender and the snippet both come from the message the id
 * actually names.
 */
static const ConvStreamHistoryEntry *
sReplyTarget(void)
{
    int i;

    for (i = g_gen.history_count - 1; i >= 0; i--) {
        if (strcmp(g_gen.history[i].sender_name, g_gen.last_sender) != 0) return &g_gen.history[i];
    }
    return g_gen.history_count > 0 ? &g_gen.history[g_gen.history_count - 1] : NULL;
}

/**
 * @brief Pick the sender for a run, never one that would continue the previous message's run.
 *
 * A run that joined the one before it would make the clusters already sent wrong, and the
 * widget's own pass would group them -- so the continuation is refused here rather than
 * discovered as a mismatch by the census.
 */
static void
sPickSender(ConvStreamDirection dir, char *out_ptr, size_t cap)
{
    int i;

    if (dir == CONV_STREAM_DIR_SYSTEM) {
        snprintf(out_ptr, cap, "System");
        return;
    }
    if (dir == CONV_STREAM_DIR_OUTBOUND) {
        snprintf(out_ptr, cap, "%s", CONV_STREAM_OUTBOUND_SENDER);
        return;
    }
    for (i = 0; i < CONV_STREAM_INBOUND_COUNT; i++) {
        bool continues = g_gen.has_last && g_gen.last_kind == CONV_STREAM_KIND_MESSAGE &&
                         g_gen.last_direction == CONV_STREAM_DIR_INBOUND &&
                         strcmp(kInboundSenders[i], g_gen.last_sender) == 0;
        if (!continues) {
            snprintf(out_ptr, cap, "%s", kInboundSenders[i]);
            return;
        }
    }
    snprintf(out_ptr, cap, "%s", kInboundSenders[0]);
}

/** @brief Build the whole run, so sAssignClusters sees it and the clusters are right. */
static void
sBuildRun(const ConvStreamPlan *plan_ptr)
{
    int i;

    for (i = 0; i < plan_ptr->count && i < CONV_STREAM_MAX_RUN; i++) {
        ConvStreamMessage      *msg_ptr = &g_gen.run_msgs[i];
        char                   *text_ptr = g_gen.run_text[i];
        const ConvStreamHistoryEntry *target_ptr = NULL;

        memset(msg_ptr, 0, sizeof *msg_ptr);
        msg_ptr->message_id = g_gen.next_id++;
        /* NO STAMP HERE. A run is built whole so its clusters can be assigned, but its messages are
         * emitted one per tick -- so their stamps belong to the instant each was SENT, not to the
         * instant the run was planned. sStampAndAdvance is the single place either happens. */
        msg_ptr->direction = plan_ptr->direction;
        msg_ptr->kind = plan_ptr->kind;
        msg_ptr->text_ptr = text_ptr;

        if (plan_ptr->is_media) {
            /* ONE ID FOR THE WHOLE TRANSFER, which is what makes it a sequence rather than
             * three unrelated rows -- and what the widget cannot yet render in place. */
            msg_ptr->message_id = i == 0 ? msg_ptr->message_id : g_gen.run_msgs[0].message_id;
            msg_ptr->status = plan_ptr->status;
            snprintf(text_ptr, CONV_STREAM_TEXT_MAX, "[Transferring Media File: %s]",
                     kMediaFiles[g_gen.media_index % 3]);
            sPickSender(plan_ptr->direction, msg_ptr->sender_name, sizeof msg_ptr->sender_name);
        }
        else if (plan_ptr->count > 1) {
            /* Only the scene that declares it progresses its statuses; an inbound run takes the
             * scene's status throughout. */
            if (plan_ptr->progresses && i == plan_ptr->count - 1) {
                msg_ptr->status = CONV_STREAM_STATUS_DELIVERED;
            }
            else {
                msg_ptr->status = plan_ptr->status;
            }
            snprintf(text_ptr, CONV_STREAM_TEXT_MAX, "%s", kBodies[(g_gen.next_id + (uint64_t)i) % 6]);
            sPickSender(plan_ptr->direction, msg_ptr->sender_name, sizeof msg_ptr->sender_name);
        }
        else {
            msg_ptr->status = plan_ptr->status;
            snprintf(text_ptr, CONV_STREAM_TEXT_MAX, "%s", plan_ptr->collapsed ? kMacro : kBodies[g_gen.next_id % 6]);
            sPickSender(plan_ptr->direction, msg_ptr->sender_name, sizeof msg_ptr->sender_name);
        }

        if (plan_ptr->collapsed && !plan_ptr->with_badges) msg_ptr->is_collapsed = true;

        if (plan_ptr->with_badges && i == 0) {
            snprintf(msg_ptr->reactions[0].emoji, sizeof msg_ptr->reactions[0].emoji, "\xF0" "\x9F" "\x94" "\xA5");
            msg_ptr->reactions[0].count = 1 + (int)(g_gen.next_id % 4);
            snprintf(msg_ptr->reactions[1].emoji, sizeof msg_ptr->reactions[1].emoji, "\xF0" "\x9F" "\x91" "\x8D");
            msg_ptr->reactions[1].count = 1 + (int)(g_gen.next_id % 3);
            msg_ptr->reaction_count = 2;
            msg_ptr->is_collapsed = true;
        }

        if (plan_ptr->with_reply && i == 0) {
            target_ptr = sReplyTarget();
            if (target_ptr) {
                msg_ptr->has_reply_context = true;
                msg_ptr->reply_msg_id = target_ptr->message_id;
                snprintf(msg_ptr->reply_sender, sizeof msg_ptr->reply_sender, "%s", target_ptr->sender_name);
                snprintf(g_gen.run_snip[i], sizeof g_gen.run_snip[i], "%s", target_ptr->snippet);
                msg_ptr->reply_snippet_ptr = g_gen.run_snip[i];
            }
        }
    }
    sAssignClusters(g_gen.run_msgs, plan_ptr->count);
}

static void
sEmitPresence(int is_typing, const char *sender_ptr, char *line_ptr, size_t cap)
{
    /* The clock is read, not stepped: a typing row moves nothing. */
    int n = ConvStreamBuildPresence(is_typing, sender_ptr, sSimEpoch(g_gen.sim_millis), line_ptr, cap);

    if (n > 0) sBroadcast(line_ptr, n);
}

/** @brief The scene table's names, so a person can read back what the generator is about to do. */
static const char *
sSceneName(int scene)
{
    static const char *const kNames[SCENE_COUNT] = {"inbound_run_3", "presence_on",  "outbound_run_2", "system",
                                                    "inbound_run_4", "indicator",    "presence_off",   "media",
                                                    "expired",       "badged"};

    return (scene >= 0 && scene < SCENE_COUNT) ? kNames[scene] : "?";
}

/** @brief The cadence the speed multiplier works out to right now. The caller holds g_gen_mutex. */
static int
sEffectiveCadence(void)
{
    long pct = g_gen.speed_pct > 0 ? g_gen.speed_pct : 100;
    long ms = (long)g_gen.base_cadence_ms * 100 / pct;

    if (ms < 1) ms = 1;
    if (ms > 60000) ms = 60000;
    return (int)ms;
}

/**
 * @brief Answer a client's command, and tell every client what the state now is.
 *
 * BROADCAST AND NOT UNICAST, because the state is global: a pause one client asked for is a pause
 * every client is now in, and a client left believing the stream is running would have no way to
 * find out otherwise. The refusal reason therefore goes to all of them too, which is the price of
 * nobody holding a stale view.
 */
static void
sAnswerCommand(ConvStreamCommandKind kind, int arg, const char *raw_ptr)
{
    ConvStreamControl ctl;
    /* Per-call, for the reason sSendAttachBurst's comment gives: two clients may command at once,
     * and each of their threads runs this function. */
    char              line[CONV_STREAM_LINE_MAX];
    int               n;

    memset(&ctl, 0, sizeof ctl);
    ctl.event_ptr = raw_ptr && raw_ptr[0] ? raw_ptr : "unknown";
    ctl.detail_ptr = "";

    pthread_mutex_lock(&g_gen_mutex);
    /* ---- act ---- */
    switch (kind) {
        case CONV_STREAM_CMD_PAUSE:
            g_gen.paused = true;
            ctl.accepted = true;
            break;
        case CONV_STREAM_CMD_RESUME:
            g_gen.paused = false;
            ctl.accepted = true;
            break;
        case CONV_STREAM_CMD_STEP:
            g_gen.step_once = true;
            ctl.accepted = true;
            break;
        case CONV_STREAM_CMD_SPEED:
            if (arg < 1 || arg > 10000) {
                ctl.detail_ptr = "speed must be a percentage between 1 and 10000";
            }
            else {
                g_gen.speed_pct = arg;
                ctl.accepted = true;
            }
            break;
        case CONV_STREAM_CMD_SCENE:
            if (arg < 0 || arg >= SCENE_COUNT) {
                ctl.detail_ptr = "no such scene";
            }
            else {
                /* TAKES EFFECT AT THE NEXT RUN BOUNDARY, not now. A run's cluster positions were
                 * assigned before its first frame went out, so cutting one short would leave a TOP
                 * with no BOTTOM and the widget would compute something else. */
                g_gen.pending_scene = arg;
                ctl.accepted = true;
            }
            break;
        case CONV_STREAM_CMD_STATUS:
            ctl.accepted = true;
            break;
        case CONV_STREAM_CMD_UNKNOWN:
        default:
            ctl.detail_ptr = "not a command this server knows";
            break;
    }
    /* ---- report the state as it now stands ---- */
    ctl.command_ptr = kind == CONV_STREAM_CMD_STATUS ? NULL : ctl.event_ptr;
    ctl.paused = g_gen.paused;
    ctl.speed_pct = g_gen.speed_pct;
    ctl.cadence_ms = sEffectiveCadence();
    ctl.next_scene = g_gen.pending_scene >= 0 ? g_gen.pending_scene : g_gen.scene;
    ctl.next_scene_name_ptr = sSceneName(ctl.next_scene);
    ctl.frames = atomic_load(&g_gen_frames);
    if (kind == CONV_STREAM_CMD_STATUS) ctl.event_ptr = "state";
    pthread_mutex_unlock(&g_gen_mutex);

    if ((n = ConvStreamBuildControl(&ctl, line, sizeof line)) > 0) sBroadcast(line, n);
    printf("[SERVER] control: %s%s%s\n", ctl.event_ptr, ctl.accepted ? " accepted" : " refused -- ",
           ctl.accepted ? "" : ctl.detail_ptr);
}

/** @brief One tick: emit the next frame of the current run, or plan and start a new one. */
static void
sGenerateOne(const char *cadence_unused)
{
    char              line[CONV_STREAM_LINE_MAX];
    ConvStreamPlan    plan;
    ConvStreamMessage msg;
    int               n;

    (void)cadence_unused;

    if (g_gen.run_left == 0) {
        pthread_mutex_lock(&g_gen_mutex);
        if (g_gen.pending_scene >= 0) {
            g_gen.scene = g_gen.pending_scene;
            g_gen.pending_scene = -1;
        }
        pthread_mutex_unlock(&g_gen_mutex);
        for (;;) {
            ConvStreamScene scene = (ConvStreamScene)g_gen.scene;

            g_gen.scene = (g_gen.scene + 1) % SCENE_COUNT;
            plan = sPlanFor((int)scene);

            if (scene == SCENE_PRESENCE_ON) {
                char who[CONV_STREAM_NAME_MAX];

                sPickSender(CONV_STREAM_DIR_INBOUND, who, sizeof who);
                sEmitPresence(1, who, line, sizeof line);
                return;
            }
            if (scene == SCENE_PRESENCE_OFF) {
                sEmitPresence(0, NULL, line, sizeof line);
                return;
            }
            if (scene == SCENE_SYSTEM || scene == SCENE_INDICATOR) {
                memset(&msg, 0, sizeof msg);
                msg.message_id = g_gen.next_id++;
                msg.direction = plan.direction;
                msg.kind = CONV_STREAM_KIND_STATUS_INDICATOR;
                msg.status = plan.status;
                msg.text_ptr = scene == SCENE_SYSTEM ? "Encryption Key Verified. Channel secured."
                                                     : "Acknowledged.";
                msg.cluster = CONV_STREAM_CLUSTER_LONE;
                sPickSender(plan.direction, msg.sender_name, sizeof msg.sender_name);
                sStampAndAdvance(&msg);
                if ((n = ConvStreamBuildMessage(&msg, line, sizeof line)) > 0) sBroadcast(line, n);
                sRemember(&msg);
                return;
            }
            break;
        }
        if (plan.is_media) g_gen.media_index++;
        g_gen.run_left = plan.count > CONV_STREAM_MAX_RUN ? CONV_STREAM_MAX_RUN : plan.count;
        g_gen.run_index = 0;
        g_gen.run_is_media = plan.is_media;
        g_gen.run_count = plan.count;
        sBuildRun(&plan);
    }

    memcpy(&msg, &g_gen.run_msgs[g_gen.run_index], sizeof msg);
    sStampAndAdvance(&msg);
    if (g_gen.run_is_media) {
        int percent = 100 * (g_gen.run_index + 1) / (g_gen.run_count > 0 ? g_gen.run_count : 1);

        n = ConvStreamBuildMediaProgress(&msg, kMediaFiles[(g_gen.media_index - 1) % 3], percent, line, sizeof line);
    }
    else {
        n = ConvStreamBuildMessage(&msg, line, sizeof line);
    }
    if (n > 0) sBroadcast(line, n);
    sRemember(&msg);
    g_gen.run_index++;
    g_gen.run_left--;
}

/** @brief The generator thread: one frame per cadence tick, and nothing while nobody is on. */
static void *
sGeneratorRoutine(void *arg_ptr)
{
    struct timespec pause;

    g_gen.next_id = 1011; /* the replay owns 1001..1010 */
    pthread_mutex_lock(&g_gen_mutex);
    g_gen.base_cadence_ms = (int)(intptr_t)arg_ptr;
    g_gen.speed_pct = 100;
    g_gen.pending_scene = -1;
    g_gen.sim_millis = CONV_STREAM_SIM_START_MILLIS;
    pthread_mutex_unlock(&g_gen_mutex);

    for (;;) {
        int  cadence;
        bool paused, step;

        if (!atomic_load(&g_running)) break;

        pthread_mutex_lock(&g_gen_mutex);
        cadence = sEffectiveCadence();
        paused = g_gen.paused;
        step = g_gen.step_once;
        g_gen.step_once = false;
        pthread_mutex_unlock(&g_gen_mutex);

        if (atomic_load(&g_ready) == 0) {
            /* The design document's standby: with nobody attached there is nothing to talk to,
             * and standing by rather than emitting keeps a late client from joining mid-run. */
            struct timespec idle = {0, 50 * 1000 * 1000};

            nanosleep(&idle, NULL);
            continue;
        }
        if (paused && !step) {
            /* Paused is not stopped: a step may arrive at any moment, so this polls rather than
             * waiting on something, and the poll costs nothing when nobody is asking. */
            struct timespec idle = {0, 20 * 1000 * 1000};

            nanosleep(&idle, NULL);
            continue;
        }

        sGenerateOne(NULL);
        atomic_fetch_add(&g_gen_frames, 1);

        pause.tv_sec = cadence / 1000;
        pause.tv_nsec = (cadence % 1000) * 1000000L;
        nanosleep(&pause, NULL);
    }
    return NULL;
}

static void
sOnSignal(int sig)
{
    (void)sig;
    atomic_store(&g_running, false);
    /* Unblocks accept() without the handler touching anything the loop owns. */
    if (g_listen_fd >= 0) shutdown(g_listen_fd, SHUT_RDWR);
}

/**
 * @brief Bind the first free port in the range, and say which one it was.
 *
 * @return The listening fd, or -1. The port taken is printed rather than assumed: a
 *         fallback that is silent is a fallback a client cannot find.
 */
static int
sBindListener(int *port_out_ptr)
{
    int port;

    for (port = CONV_STREAM_PORT_BASE; port < CONV_STREAM_PORT_BASE + CONV_STREAM_PORT_TRIES; port++) {
        struct sockaddr_in addr;
        int                fd, opt = 1;

        if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
            perror("[SERVER] socket");
            return -1;
        }
        if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof opt) != 0) {
            perror("[SERVER] SO_REUSEADDR");
            close(fd);
            return -1;
        }
        memset(&addr, 0, sizeof addr);
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons((uint16_t)port);

        if (bind(fd, (struct sockaddr *)&addr, sizeof addr) == 0) {
            if (listen(fd, 5) != 0) {
                perror("[SERVER] listen");
                close(fd);
                return -1;
            }
            *port_out_ptr = port;
            return fd;
        }
        /* Only a taken port moves on; anything else is reported rather than walked past. */
        if (errno != EADDRINUSE) {
            perror("[SERVER] bind");
            close(fd);
            return -1;
        }
        printf("[SERVER] port %d is taken, trying %d\n", port, port + 1);
        close(fd);
    }
    fprintf(stderr, "[SERVER] every port %d..%d is taken\n", CONV_STREAM_PORT_BASE,
            CONV_STREAM_PORT_BASE + CONV_STREAM_PORT_TRIES - 1);
    return -1;
}

int
main(void)
{
    struct sigaction sa;
    int              port = 0;
    int              i;

    setvbuf(stdout, NULL, _IOLBF, 0);
    memset(g_pool, 0, sizeof g_pool);
    for (i = 0; i < CONV_STREAM_MAX_CLIENTS; i++) {
        g_pool[i].fd = -1;
        pthread_mutex_init(&g_pool[i].send_mutex, NULL);
    }

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = sOnSignal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    /* A peer that goes away mid-frame must not kill the daemon. */
    signal(SIGPIPE, SIG_IGN);

    /* THE CADENCE IS AN ENVIRONMENT VARIABLE AND NOT AN ARGUMENT, so that the "takes no
     * arguments" contract the README makes stays true. A census run turns it down to get
     * hundreds of frames in seconds; the default is a pace a person can read. */
    {
        const char *env_ptr = getenv("CONV_STREAM_CADENCE_MS");
        long        cadence = env_ptr ? strtol(env_ptr, NULL, 10) : 400;
        pthread_t   gen_thread;

        if (pthread_create(&gen_thread, NULL, sGeneratorRoutine, (void *)(intptr_t)cadence) != 0) {
            perror("[SERVER] pthread_create (generator)");
            return EXIT_FAILURE;
        }
        pthread_detach(gen_thread);
        printf("[SERVER] generator running at %ld ms per frame; `pause`, `resume`, `step`, "
               "`speed`, `scene` and `status` are accepted on the same socket\n",
               cadence);
    }

    if ((g_listen_fd = sBindListener(&port)) < 0) return EXIT_FAILURE;
    printf("[SERVER] listening on 0.0.0.0:%d, up to %d clients\n", port, CONV_STREAM_MAX_CLIENTS);
    printf("[SERVER] %d golden messages, %d frame shapes per attach\n", CONV_STREAM_GOLDEN_COUNT, 4);

    while (atomic_load(&g_running)) {
        struct sockaddr_in peer;
        socklen_t          peer_len = sizeof peer;
        int                fd = accept(g_listen_fd, (struct sockaddr *)&peer, &peer_len);
        int                slot = -1;

        if (fd < 0) {
            if (!atomic_load(&g_running)) break;
            /* EINTR is expected and costs nothing; anything else would spin at 100% CPU if
             * it were simply retried, so it backs off. */
            if (errno == EINTR) continue;
            printf("[SERVER] accept failed (%s), backing off\n", strerror(errno));
            {
                struct timespec pause = {0, 100 * 1000 * 1000};

                nanosleep(&pause, NULL);
            }
            continue;
        }

        pthread_mutex_lock(&g_pool_mutex);
        for (i = 0; i < CONV_STREAM_MAX_CLIENTS; i++) {
            if (!g_pool[i].active) {
                slot = i;
                g_pool[i].fd = fd;
                g_pool[i].active = true;
                break;
            }
        }
        pthread_mutex_unlock(&g_pool_mutex);

        if (slot < 0) {
            printf("[SERVER] all %d slots are in use, refusing the connection\n", CONV_STREAM_MAX_CLIENTS);
            close(fd);
            continue;
        }

        {
            struct timeval tmo = {5, 0};

            if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tmo, sizeof tmo) != 0) {
                perror("[SERVER] SO_SNDTIMEO");
            }
            /* A one-second receive timeout, so a client thread notices a shutdown request
             * without a live-but-silent client being dropped for being quiet. */
            tmo.tv_sec = 1;
            if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tmo, sizeof tmo) != 0) {
                perror("[SERVER] SO_RCVTIMEO");
            }
        }
        atomic_fetch_add(&g_clients, 1);
        if (pthread_create(&g_pool[slot].thread, NULL, sClientRoutine, &g_pool[slot]) != 0) {
            perror("[SERVER] pthread_create");
            close(fd);
            g_pool[slot].fd = -1;
            g_pool[slot].active = false;
            atomic_fetch_sub(&g_clients, 1);
            continue;
        }
        pthread_detach(g_pool[slot].thread);
        printf("[SERVER] client attached to slot %d, %d attached\n", slot, atomic_load(&g_clients));
    }

    printf("[SERVER] shutting down\n");
    close(g_listen_fd);
    g_listen_fd = -1;
    /* Every client thread owns its slot's release, so the wait is for them to notice EOF
     * rather than for a count the loop maintains itself. */
    for (i = 0; i < 50 && atomic_load(&g_clients) > 0; i++) {
        struct timespec pause = {0, 100 * 1000 * 1000};

        nanosleep(&pause, NULL);
    }
    if (atomic_load(&g_clients) > 0) {
        printf("[SERVER] %d client(s) did not drain\n", atomic_load(&g_clients));
    }
    printf("[SERVER] exited cleanly\n");
    return EXIT_SUCCESS;
}
