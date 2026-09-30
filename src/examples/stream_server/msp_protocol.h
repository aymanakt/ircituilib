/**
 * @file msp_protocol.h
 * @brief The MSP wire contract: one 64-byte frame per metric update, shared by the
 *        simulation server and the TUI client.
 *
 * @details THIS HEADER BELONGS TO NEITHER PROGRAM.  It includes nothing from
 *          ircituilib, nothing from curses and nothing from pthread: the server links
 *          no UI library at all, and that is the point -- the absent
 *          `target_link_libraries` on that target is the assertion that the protocol
 *          is independent of the framework it feeds.  A symbol from either side
 *          appearing here would break that assertion silently.
 *
 *          ## Why the frames are text and not the packed struct §28 specifies
 *
 *          `dev/technical_designs/IRCITUILIB_TEST_STREAMING_SERVER_PROPOSAL.md`
 *          §28 specifies "fixed-width binary
 *          frames" of packed bytes.  That cannot cross this framework's own
 *          application-network seam.  `IrcitUiNetworkListener` takes a `const char *`
 *          and **no length**
 *          (`include/ircituilib/ircit_ui_event_dispatcher_type.h:66-69`), and
 *          `IrcitUiDispatcherPostNetworkChunk` NUL-terminates the chunk it copies
 *          (`src/ui_event_dispatcher.c:219-220`), so the only route a listener has to
 *          the chunk's length is `strlen()`.  A frame of `float` and `int64_t` fields
 *          contains a 0x00 almost always, and the listener would then see a length
 *          shorter than what was sent with no way to discover the truth.  Measured, not
 *          argued: `dev/probes/harness/p_nul.c`, where §28's own struct is posted at 56
 *          bytes and arrives as 5, while a frame of this file's shape is posted at 64
 *          and arrives as 64.
 *
 *          So the frame length and the channel semantics are §28's; only the encoding
 *          changes.  Every byte of a frame is in `[0x20,0x7E]` except the trailing CR
 *          LF, which makes `strlen()` exact, makes the parser O(1) per frame, and makes
 *          the stream readable with `socat` or `strace` when something goes wrong.
 *
 *          ## Why 64 and not the 56 the section's own struct compiles to
 *
 *          §28 heads its layout table "Frame Layout (Fixed 64 Bytes)" and draws
 *          4+4+8+8+8+8+24 = 64 -- but the struct printed underneath declares both metric
 *          fields as `float`, which is 4+4+4+4+8+8+24 = **56**.  The section's diagram
 *          and its own code disagree and neither is marked wrong.  This file takes the
 *          diagram's 64, because the diagram is what the section's prose describes, and
 *          because a round 64 leaves room for the label field §31 asks for.
 *
 *          ## The frame
 *
 *          ```
 *           MMSP | cc | pppppppp | bbbbbbbb |  tttttttttt | TTTTTTTTTT | label x20 | CR LF
 *            4     2       8          8           10            10          20        2
 *           magic  %02d  %8.5f    %8.5f    %10lld        %10lld      space-padded  framing
 *          ```
 *
 *          ## What the parser does NOT reject
 *
 *          A non-finite metric is **delivered**, not refused.  `nan` and `inf` are what
 *          `strtof` returns from a legal field, and the place to decide what a bar does
 *          with them is the value mapper that owns the float-to-percent conversion --
 *          `dev/probes/harness/p_cast.c` measures that conversion as undefined behaviour
 *          without a guard, and puts the guard in exactly one place.  Rejecting here as
 *          well would give two policies for one decision, which is how they drift.
 */
#ifndef MSP_PROTOCOL_H
#define MSP_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── The frame ─────────────────────────────────────────────────────────────── */

/** @brief The greppable first four bytes.  Characters, not a token: §28 writes
 *         `0x50534D4D // ASCII for 'MMSP'`, which is the byte-reverse of those
 *         characters and reads as 'PSMM' on a big-endian wire.  Writing the
 *         characters removes the question. */
#define MSP_MAGIC     "MMSP"
/** @brief The first four bytes of a command frame, same reasoning.  §28's constant for
 *         this one, `0x434D444D`, matches "MCMD" under *neither* byte order -- it is the
 *         characters rotated one position -- so there was nothing to preserve. */
#define MSP_CMD_MAGIC "MCMD"

/** @brief One frame, on the wire.  Fixed, so the parser is a counter and not a scan. */
#define MSP_FRAME_LEN 64
/** @brief Bytes of label carried per frame.  §31 asks for an 88-byte arena; 88 does not
 *         fit in a 64-byte frame and a second frame length would double the
 *         reassembler's state for no benefit, so the label is 20 and the reduction is
 *         recorded rather than silent. */
#define MSP_LABEL_LEN 20

/* Field offsets and widths, in one place so the builder, the parser and the probe cannot
 * disagree.  The sum is asserted below. */
#define MSP_OFF_MAGIC      0
#define MSP_W_MAGIC        4
#define MSP_OFF_CHANNEL    4
#define MSP_W_CHANNEL      2
#define MSP_OFF_PRIMARY    6
#define MSP_W_PRIMARY      8
#define MSP_OFF_SECONDARY  14
#define MSP_W_SECONDARY    8
#define MSP_OFF_TIME_CUR   22
#define MSP_W_TIME_CUR     10
#define MSP_OFF_TIME_TOTAL 32
#define MSP_W_TIME_TOTAL   10
#define MSP_OFF_LABEL      42
#define MSP_W_LABEL        20
#define MSP_OFF_CRLF       62
#define MSP_W_CRLF         2

/** @brief The default rendezvous.  Both programs take an override on argv[1], which is how
 *         the probe set runs a server without disturbing a real one. */
#define MSP_SOCKET_PATH "/tmp/ircitui_sim_metrics.sock"

/** @brief §28's cap.  The ninth client is refused rather than queued. */
#define MSP_MAX_CLIENTS 8

/* ── Channels and commands ─────────────────────────────────────────────────── */

/**
 * @brief §28's three simulation channels.
 *
 * @details The names are the section's; the WIDGETS they drive are not, and that is a
 *          decision this file records rather than hides.  §28 maps CH_DIAL_CPU to a
 *          circular dial gauge and CH_MEDIA_YOUTUBE to a three-state media bar with a
 *          pre-fetch track, and **neither widget exists in this tree** -- there is no
 *          dial kind, no media-bar kind and no pre-fetch field anywhere in `src/`,
 *          `include/`, `layout/` or `tools/`.  The client maps these three channels onto
 *          bars it does have; when §21/§24/§25 land, the mapping changes and this enum
 *          does not.
 */
typedef enum {
  CH_MEDIA_YOUTUBE = 0x01, /**< playback position, and pre-fetch buffering */
  CH_DIAL_CPU      = 0x02, /**< active core load, and the frequency throttle factor */
  CH_DL_LARGE      = 0x03  /**< download progress, and instantaneous throughput */
} MspChannel;

/** @brief §30's three control commands, for the client-to-server direction. */
typedef enum {
  CMD_TOGGLE_PAUSE = 0x01, /**< flips the channel's playback state */
  CMD_ADJUST_SPEED = 0x02, /**< a multiplier, carried in the value field */
  CMD_INJECT_ERROR = 0x03  /**< forces a failure on the channel */
} MspCmd;

/** @brief One decoded frame, in host order.  What the parser fills and the client reads. */
typedef struct {
  int       channel;
  float     primary;      /**< may be non-finite: the mapper decides, not the parser */
  float     secondary;
  long long time_current;
  long long time_total;
  char      label[MSP_LABEL_LEN + 1];
  int       label_len;    /**< trailing spaces stripped, so an empty label is len 0 */
} MspFrame;

/** @brief One decoded command, in host order. */
typedef struct {
  int   channel;
  int   command;
  float value;
} MspCmdFrame;

/* ── The codec ─────────────────────────────────────────────────────────────── */

/** @brief Whether a byte may appear in the payload: `[0x20,0x7E]`. */
static inline int
MspByteIsPrintable(int c)
{
  return c >= 0x20 && c <= 0x7E;
}

/**
 * @brief Whether a fixed-width numeric field's conversion consumed all of it.
 *
 * @details A field is `" 0.09375"`: leading spaces are padding and trailing spaces are
 *          padding, but anything the conversion did not consume and that is not a space is
 *          a malformed field.  Without this, `"  1.0xyz"` reads as 1.0 and the protocol
 *          accepts a frame no builder would produce.
 *
 * @param field  The field, NUL-terminated at its width.
 * @param end    Where the conversion stopped.
 * @param wid    The field's fixed width.
 */
static inline int
MspFieldTailOk(const char *field, const char *end, size_t wid)
{
  size_t used;

  if (end == field) return 0;
  for (used = (size_t)(end - field); used < wid; used++) {
    if (field[used] != ' ') return 0;
  }
  return 1;
}

/**
 * @brief Fold a label into the frame's field discipline.
 *
 * @details Three rules, all required.  The label STOPS AT ITS NUL and the rest of the field
 *          is padding -- reading the full width regardless, which this function's first
 *          draft did, reads whatever follows the terminator in the caller's buffer and puts
 *          it on the wire.  That is not theoretical: it was caught in the server's own live
 *          run, where an empty label arrived as a fragment of a libc string.  Any byte
 *          outside `[0x20,0x7E]` becomes `.`, because a single stray 0x00 is the only way
 *          the text framing can still be defeated -- it would truncate the chunk at the seam
 *          exactly as §28's binary frame does.  And the field is space-padded to its width,
 *          because the parser reads a fixed width.
 *
 * @param dst  @p MSP_LABEL_LEN bytes, written in full.
 * @param src  The label, or NULL for an empty one.
 * @return @p MSP_LABEL_LEN, the field width the caller must have room for.
 */
static inline int
MspLabelPack(char *dst, const char *src)
{
  int i, ended = 0;

  for (i = 0; i < MSP_LABEL_LEN; i++) {
    int c = ' ';

    if (!ended && src) {
      if (src[i] == '\0') ended = 1;
      else                c = (unsigned char)src[i];
    }
    dst[i] = MspByteIsPrintable(c) ? (char)c : '.';
  }
  return MSP_LABEL_LEN;
}

/**
 * @brief The builder's own guarantee: no payload byte is NUL.
 *
 * @details `snprintf` terminates every field it writes, and the LAST field's terminator lands
 *          inside the payload unless a later writer happens to cover it.  That is not
 *          hypothetical: this file's first draft left a 0x00 at offset 18 of every command
 *          frame, and the parser -- which requires every payload byte printable, because that
 *          is what makes `strlen` exact at the seam -- refused **every command the builder
 *          produced**.  A scrub rather than a careful field ordering, so that adding a field
 *          later cannot reintroduce it.
 *
 * @param out          The frame being built.
 * @param payload_len  Bytes before the terminator.
 */
static inline void
MspScrub(char *out, size_t payload_len)
{
  size_t i;

  for (i = 0; i < payload_len; i++) {
    if (out[i] == '\0') out[i] = ' ';
  }
}

/**
 * @brief Build one metric frame.
 *
 * @details A fixed-width `snprintf` per field rather than a hand-rolled formatter, so the
 *          C library owns the number formatting and this file owns only the layout.  Each
 *          field's width is chosen to hold every value the range permits: `%8.5f` holds
 *          `[0,1]` to five places including the leading zero, and `%10lld` holds ten
 *          digits of seconds or bytes.  A value that overflows its field would push the
 *          CR LF off the end, which is why the caller's buffer is exactly
 *          @p MSP_FRAME_LEN and the terminator is written last and unconditionally rather
 *          than left where a `snprintf` happened to put it.
 *
 * @param out  @p MSP_FRAME_LEN bytes, written in full.  Never NUL-terminated: this is a
 *             wire record, not a string, and treating it as one is the bug `p_nul`
 *             records in its own first draft.
 */
static inline void
MspFrameBuild(char *out, int channel, float primary, float secondary,
              long long time_current, long long time_total, const char *label)
{
  memset(out, ' ', MSP_FRAME_LEN);

  memcpy(out + MSP_OFF_MAGIC, MSP_MAGIC, MSP_W_MAGIC);
  /* ZERO-padded, not space-padded: the channel is an index, and a field reading " 1" invites
   * an off-by-one in a reader that indexes the raw byte.  The numeric fields below are
   * space-padded because strtof/strtoll skip leading whitespace and a sign needs the room;
   * these two are exactly two ASCII digits, which is what the parser requires of them. */
  snprintf(out + MSP_OFF_CHANNEL,     MSP_W_CHANNEL + 1,     "%02d",   channel);
  snprintf(out + MSP_OFF_PRIMARY,     MSP_W_PRIMARY + 1,     "%8.5f",  (double)primary);
  snprintf(out + MSP_OFF_SECONDARY,   MSP_W_SECONDARY + 1,   "%8.5f",  (double)secondary);
  snprintf(out + MSP_OFF_TIME_CUR,    MSP_W_TIME_CUR + 1,    "%10lld", time_current);
  snprintf(out + MSP_OFF_TIME_TOTAL,  MSP_W_TIME_TOTAL + 1,  "%10lld", time_total);
  MspLabelPack(out + MSP_OFF_LABEL, label);
  MspScrub(out, MSP_OFF_CRLF);

  out[MSP_OFF_CRLF]     = '\r';
  out[MSP_OFF_CRLF + 1] = '\n';
}

/**
 * @brief Parse one frame that has already been reassembled to exactly @p MSP_FRAME_LEN.
 *
 * @details Strict on structure, permissive on values -- see the file comment for why the
 *          two differ.  Structural checks, in order, each of which a corrupt or hostile
 *          frame can fail:
 *
 *            1. the magic, compared as four characters;
 *            2. the channel, range-checked BEFORE it can become an array index anywhere
 *               downstream -- this is the one input that is a memory-safety question
 *               rather than a wrong number;
 *            3. the CR LF terminator, which is the framing;
 *            4. every payload byte printable, which is what makes `strlen` exact at the
 *               seam and is therefore a property the whole protocol rests on;
 *            5. each numeric field consuming its whole width, so `"  1.0xyz"` is refused
 *               rather than read as 1.0.
 *
 * @return 1 if @p out_ptr was filled, 0 if the frame is not one of ours.
 */
static inline int
MspFrameParse(const char *in, MspFrame *out_ptr)
{
  char   field[MSP_LABEL_LEN + 1];
  char  *end_ptr = NULL;
  float  f;
  long long ll;
  int    i, n;

  if (memcmp(in + MSP_OFF_MAGIC, MSP_MAGIC, MSP_W_MAGIC) != 0) return 0;

  n = 0;
  for (i = 0; i < MSP_W_CHANNEL; i++) {
    int c = (unsigned char)in[MSP_OFF_CHANNEL + i];
    if (c < '0' || c > '9') return 0;
    n = n * 10 + (c - '0');
  }
  if (n != CH_MEDIA_YOUTUBE && n != CH_DIAL_CPU && n != CH_DL_LARGE) return 0;

  if (in[MSP_OFF_CRLF] != '\r' || in[MSP_OFF_CRLF + 1] != '\n') return 0;

  for (i = 0; i < MSP_OFF_CRLF; i++) {
    if (!MspByteIsPrintable((unsigned char)in[i])) return 0;
  }

  /* Each numeric field is fixed-width and may carry leading spaces.  Copy, terminate, and
   * require the conversion to consume everything that is not trailing space.  Two macros
   * rather than one because `strtof` and `strtoll` do not take the same arguments -- the
   * first draft of this file passed a base to both and the compiler said so. */
#define MSP_PARSE_FLOAT(off, wid, dst)                        \
  do {                                                        \
    memcpy(field, in + (off), (wid));                         \
    field[(wid)] = '\0';                                      \
    (dst) = strtof(field, &end_ptr);                          \
    if (!MspFieldTailOk(field, end_ptr, (wid))) return 0;     \
  } while (0)

#define MSP_PARSE_LL(off, wid, dst)                           \
  do {                                                        \
    memcpy(field, in + (off), (wid));                         \
    field[(wid)] = '\0';                                      \
    (dst) = strtoll(field, &end_ptr, 10);                     \
    if (!MspFieldTailOk(field, end_ptr, (wid))) return 0;     \
  } while (0)

  MSP_PARSE_FLOAT(MSP_OFF_PRIMARY,    MSP_W_PRIMARY,    f);
  if (out_ptr) out_ptr->primary = f;
  MSP_PARSE_FLOAT(MSP_OFF_SECONDARY,  MSP_W_SECONDARY,  f);
  if (out_ptr) out_ptr->secondary = f;
  MSP_PARSE_LL(MSP_OFF_TIME_CUR,      MSP_W_TIME_CUR,   ll);
  if (out_ptr) out_ptr->time_current = ll;
  MSP_PARSE_LL(MSP_OFF_TIME_TOTAL,    MSP_W_TIME_TOTAL, ll);
  if (out_ptr) out_ptr->time_total = ll;

#undef MSP_PARSE_FLOAT
#undef MSP_PARSE_LL

  if (out_ptr) {
    out_ptr->channel = n;
    memcpy(field, in + MSP_OFF_LABEL, MSP_LABEL_LEN);
    field[MSP_LABEL_LEN] = '\0';
    /* Trailing spaces are padding, not content: an all-space label is empty, which is how
     * "no message" is distinguished from a message of blanks. */
    for (i = MSP_LABEL_LEN; i > 0 && field[i - 1] == ' '; i--) field[i - 1] = '\0';
    memcpy(out_ptr->label, field, MSP_LABEL_LEN + 1);
    out_ptr->label_len = i;
  }
  return 1;
}

/* ── The command frame, client to server ───────────────────────────────────── */

/*   MCMD | cc | kk |  vvvvvvvvvv | pad x44 | CR LF
 *    4     2    2        10          44       2   = 64
 *  magic  chan  cmd   %10.4f     spaces   framing
 */
#define MSP_CMD_LEN        64
#define MSP_CMD_OFF_MAGIC  0
#define MSP_CMD_OFF_CHANNEL 4
#define MSP_CMD_OFF_COMMAND 6
#define MSP_CMD_OFF_VALUE  8
#define MSP_CMD_W_VALUE    10
#define MSP_CMD_OFF_CRLF   62

static inline int
MspCmdChannelIsKnown(int channel)
{
  return channel == CH_MEDIA_YOUTUBE || channel == CH_DIAL_CPU || channel == CH_DL_LARGE;
}

static inline int
MspCmdIsKnown(int command)
{
  return command == CMD_TOGGLE_PAUSE || command == CMD_ADJUST_SPEED || command == CMD_INJECT_ERROR;
}

/** @brief Build one control frame.  Same fixed-width discipline as the metric frame. */
static inline void
MspCmdBuild(char *out, int channel, int command, float value)
{
  memset(out, ' ', MSP_CMD_LEN);

  memcpy(out + MSP_CMD_OFF_MAGIC, MSP_CMD_MAGIC, 4);
  snprintf(out + MSP_CMD_OFF_CHANNEL, 3,         "%02d",   channel);
  snprintf(out + MSP_CMD_OFF_COMMAND, 3,         "%02d",   command);
  snprintf(out + MSP_CMD_OFF_VALUE,   MSP_CMD_W_VALUE + 1, "%10.4f", (double)value);
  MspScrub(out, MSP_CMD_OFF_CRLF);

  out[MSP_CMD_OFF_CRLF]     = '\r';
  out[MSP_CMD_OFF_CRLF + 1] = '\n';
}

/**
 * @brief Parse one command frame.
 *
 * @details Rejects an unknown channel or an unknown command rather than passing them on,
 *          because the server's dispatch is a `switch` over exactly these three and a
 *          silent no-op for anything else would make a protocol error indistinguishable
 *          from a command that legitimately did nothing.
 *
 * @return 1 if @p out_ptr was filled.
 */
static inline int
MspCmdParse(const char *in, MspCmdFrame *out_ptr)
{
  char      field[MSP_CMD_W_VALUE + 1];
  char     *end_ptr = NULL;
  int       channel = 0, command = 0, i;
  float     value;

  if (memcmp(in + MSP_CMD_OFF_MAGIC, MSP_CMD_MAGIC, 4) != 0) return 0;

  for (i = 0; i < 2; i++) {
    int c = (unsigned char)in[MSP_CMD_OFF_CHANNEL + i];
    if (c < '0' || c > '9') return 0;
    channel = channel * 10 + (c - '0');
  }
  for (i = 0; i < 2; i++) {
    int c = (unsigned char)in[MSP_CMD_OFF_COMMAND + i];
    if (c < '0' || c > '9') return 0;
    command = command * 10 + (c - '0');
  }
  if (!MspCmdChannelIsKnown(channel) || !MspCmdIsKnown(command)) return 0;

  if (in[MSP_CMD_OFF_CRLF] != '\r' || in[MSP_CMD_OFF_CRLF + 1] != '\n') return 0;

  for (i = 0; i < MSP_CMD_OFF_CRLF; i++) {
    if (!MspByteIsPrintable((unsigned char)in[i])) return 0;
  }

  memcpy(field, in + MSP_CMD_OFF_VALUE, MSP_CMD_W_VALUE);
  field[MSP_CMD_W_VALUE] = '\0';
  value = strtof(field, &end_ptr);
  if (!MspFieldTailOk(field, end_ptr, MSP_CMD_W_VALUE)) return 0;

  if (out_ptr) {
    out_ptr->channel = channel;
    out_ptr->command = command;
    out_ptr->value   = value;
  }
  return 1;
}

/* ── The reassembler, shared by both programs ──────────────────────────────── */

/**
 * @brief How many bytes of a partially-received frame are held.
 *
 * @details SIZED AGAINST THE TRANSPORT, not chosen.  On the client the reassembler is fed
 *          through `IrcitUiDispatcherPostNetworkChunk`, whose envelope is 512 bytes and
 *          which refuses anything larger -- so the largest chunk it can ever see is 511
 *          bytes, and a 511-byte chunk can carry seven whole 64-byte frames with 63 bytes
 *          left over.  A buffer holding the residue from the previous drain (under one
 *          frame) plus that chunk is therefore at most 63 + 511 = 574 bytes.
 *
 *          THIS WAS WRONG IN THE FIRST DRAFT, at four frames, and the probe found it: a
 *          seven-frame burst -- exactly what a 511-byte read delivers -- did not fit, so
 *          `MspRxPush` refused the whole chunk and every frame in it was lost.  A cap below
 *          the transport's own maximum is not a safety bound, it is a silent frame dropper.
 *          Ten frames leaves margin over the 574 the arithmetic gives.
 */
#define MSP_RX_CAP (MSP_FRAME_LEN * 10)

/**
 * @brief How many one-byte shifts before the buffer is declared lost.
 *
 * @details Resynchronisation advances **one byte at a time**, never a whole frame: a length
 *          taken from a corrupt header is not trustworthy, and skipping a whole frame can
 *          carry a desynchronisation forward for ever.  The counter exists so that a stream
 *          which resynchronises constantly is visible as a number rather than as a quietly
 *          wrong screen -- a non-zero rate means the two ends disagree about the format.
 */
#define MSP_RX_RESYNC_MAX 512

/** @brief A bounded, self-synchronising reassembler. */
typedef struct {
  char          buf[MSP_RX_CAP];
  size_t        len;      /**< bytes held, always < MSP_RX_CAP */
  unsigned long resyncs;  /**< one-byte shifts, cumulative */
  unsigned long abandoned;/**< times the buffer was thrown away, cumulative */
} MspRx;

/** @brief Reset a reassembler.  @p rx must not be NULL. */
static inline void
MspRxInit(MspRx *rx)
{
  rx->len       = 0;
  rx->resyncs   = 0;
  rx->abandoned = 0;
}

/**
 * @brief Append a chunk.
 *
 * @details Refuses a chunk that cannot fit rather than truncating it: the caller's read
 *          buffer is at most one envelope wide, but a caller that passed something larger
 *          should learn about it here instead of discovering a lost frame later.
 *
 * @return 1 if the chunk was taken, 0 if it was too large for the remaining room.
 */
static inline int
MspRxPush(MspRx *rx, const char *bytes, size_t n)
{
  if (n > MSP_RX_CAP - rx->len) {
    rx->len = 0;
    rx->abandoned++;
    return 0;
  }
  memcpy(rx->buf + rx->len, bytes, n);
  rx->len += n;
  return 1;
}

/**
 * @brief Take the next complete frame from the front of the buffer, if there is one.
 *
 * @details THE SELF-SYNCHRONISING RULE, and the reason this is one function rather than two
 *          programs' worth of near-identical code: a frame is the @p frame_len bytes ending
 *          at a `\n`.  Anything else is one byte of noise, and noise is shifted off the
 *          front and counted.
 *
 *          It follows that this is driven by the terminator and not by the length.  A
 *          desynchronised stream therefore re-locks on the next `\n` after the point where
 *          it lost its place, rather than staying wrong for ever -- which is the property a
 *          fixed-size binary parser cannot have, and the second reason the frames are text.
 *
 * @param rx         The reassembler, which is advanced by the call.
 * @param frame_len  The frame length in bytes.
 * @param out        @p frame_len bytes, filled only on success.
 * @param valid      Called with the candidate frame; must not have side effects.
 * @param ctx        Passed to @p valid unread.
 * @return 1 if @p out was filled, 0 if more bytes are needed.
 */
static inline int
MspRxNext(MspRx *rx, size_t frame_len, char *out,
          int (*valid)(const char *, void *), void *ctx)
{
  while (rx->len >= frame_len) {
    if (rx->buf[frame_len - 1] == '\n' && valid(rx->buf, ctx)) {
      memcpy(out, rx->buf, frame_len);
      memmove(rx->buf, rx->buf + frame_len, rx->len - frame_len);
      rx->len -= frame_len;
      return 1;
    }

    memmove(rx->buf, rx->buf + 1, rx->len - 1);
    rx->len--;
    if (++rx->resyncs >= MSP_RX_RESYNC_MAX) {
      /* The buffer is not going to re-lock.  Throw it away rather than shifting through it
       * for ever, and record that it happened. */
      rx->len = 0;
      rx->abandoned++;
      rx->resyncs = 0;
      return 0;
    }
  }
  return 0;
}

/** @brief Whether a candidate is a metric frame.  For @ref MspRxNext. */
static inline int
MspRxValidFrame(const char *candidate, void *ctx)
{
  (void)ctx;
  return MspFrameParse(candidate, NULL);
}

/** @brief Whether a candidate is a command frame.  For @ref MspRxNext. */
static inline int
MspRxValidCmd(const char *candidate, void *ctx)
{
  (void)ctx;
  return MspCmdParse(candidate, NULL);
}

#endif /* MSP_PROTOCOL_H */
