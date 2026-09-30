# The conversation streaming server — the reference conversation, pushed down a socket

**Gates 1 and 2 are done, plus a control channel; the client is not.** A client that attaches is sent thirteen frames: the
frozen fixture's envelope, one frame of each of the four shapes, and the nine golden messages
replayed in order — a contiguous, deterministic prefix it can check against the reference. After
that the **live generator** takes over and writes a simulated multi-party conversation at it
until it leaves: clustered runs, threading, badges, decay, presence and transfers.

Nothing here links ircituilib. That absence is the assertion, not an oversight — see §3.

**This file is the operator's guide.** What was agreed against what was delivered, every deviation,
omission, issue, tradeoff and unauthorised action taken on the way, and every claim that is reasoned
rather than proven, is in **[DELIVERY.md](DELIVERY.md)**. Read that one before trusting a number here.

```sh
cmake -S . -B build/conv-stream -DIRCITUI_BUILD_CONVERSATION_STREAM=ON -DIRCITUI_BUILD_EXAMPLE=OFF
cmake --build build/conv-stream --target conversation_stream_server

# server up, 13 frames read and summarised, server down on any exit path
./src/examples/conversation_streaming/run_stream.sh
FRAMES=3 ./src/examples/conversation_streaming/run_stream.sh
```

Everything below is checked by `dev/probes/conversation_streaming/run_probes.sh`, which reports
**15 verdicts, 0 failures** — 2151 checks: 155 schema, 189 frame, 1272 control-parser, 240 socket
cases, and a 295-check census of the live generator.

---

## 1. Why it exists

The conversation window's reference is a **file**: `assets/conversation/conversation_golden_test.json`.
The demo can load it and draw it, and `dev/probes/conversation/` reads cells back off the glass.
What nothing can do is **push a conversation at a running widget** — and that is the gap this
track closes. The window's own ingest path is
`IrcitUiConversationWindowAppend(node, &IrcitUiConversationMessage)`, which takes one message at
a time, copies what it keeps, and computes the cluster position itself.

## 2. The wire

One JSON object per line over TCP. The delimiter is the only `\n` in a frame: every other byte
is in `[0x20,0x7E]`, a UTF-8 continuation byte, or nothing. That is forced, not chosen — the
framework's network seam hands a listener a `const char *` and **no length**
(`include/ircituilib/ircit_ui_event_dispatcher_type.h:66-69`), so `strlen()` is the only route
to a chunk's end and an embedded NUL would truncate a frame with no way to notice.

| Frame | Carries | Where it lands |
|---|---|---|
| `hello` | the fixture's own envelope: `test_suite`, `target_composite_id`, `generated_timestamp_epoch_ms`, `color_palette_mapping` | nowhere — a loader's identity check |
| `message` | the full field set below | `IrcitUiConversationWindowAppend` |
| `presence` | `is_typing`, `typing_sender` | `IrcitUiConversationWindowSetTyping` |
| `media_progress` | the message fields **and** `media_payload` | nothing yet — see §8 |
| `control` | the generator's state after a command | a client's own bookkeeping — see §7.8 |

### 2.1 Two spellings, written from one value each

The wire carries the frozen fixture's key names as primary and the design document's names as
aliases beside them, both emitted by the same statement from the same source value — so a
spelling cannot be edited without the other moving.

| Concept | Primary | Alias | Widget field |
|---|---|---|---|
| identity | `message_id` | `id` | `message_id` |
| sender | `sender_name` | `sender` | `sender_name[32]` |
| body | `text_content` | `text` | `text_content` |
| cluster | `cluster_position` | `cluster_mode` | **advisory** — the widget computes it |
| reply target | `reply_context.reply_msg_id` | `.parent_id` | `reply_msg_id` |
| reply sender | `.reply_sender` | `.sender` | `reply_sender` |
| reply snippet | `.reply_snippet` | `.snippet` | `reply_snippet` |
| badges | `reactions[].emoji/.count` | *(same)* | `reactions[8]`, `reaction_count` |
| stamps | `timestamp` (a clock string) | — | `timestamp[16]`, **printed verbatim** |
| epoch | `timestamp_epoch` | — | not a widget field; for ordering |
| kind | `kind` | — | `kind` |
| collapse | `interactivity.is_collapsed` | — | `is_collapsed` |
| direction | `direction` | *(same)* | `direction` |
| status | `status` | *(same)* | `status` |

### 2.2 The three things a name is not enough to reconcile

**`timestamp` is the clock string, not the epoch.** The widget prints it and never parses it
(`src/ui_conversation.c:1490`), so an epoch would render as a raw number. Measured both ways:
`mjson_get_string` on a NUMBER returns `-1`, so the document's `"timestamp":<epoch>` does not
arrive corrupted — **it does not arrive at all** and the stamp field takes the reader's fallback.

**`kind` and `interactivity` are primary-only.** The document's schema has no key for either, so
its spelling cannot express a collapsed message (1008 arrives expanded) or the local-side status
indicator (1005 decodes as conversational).

**Presence is a frame type, not a field — and this one is load-bearing.** The design document's
server folds `presence_update` into every frame, and its own client classifies by `is_typing`:

```c
if (strcmp(presence_typing, "true") == 0)      { /* typing on */ }
else if (strstr(line, "\"is_typing\":false"))  { /* typing off — THE MESSAGE IS DROPPED */ }
else                                           { /* append — UNREACHABLE */ }
```

Every frame its server emits carries `"is_typing":false`, so the third branch never runs and
**that pair cannot display a single message**. Here a message frame carries no presence key at
all, which is what makes the classifier reach its third branch — so the document's own client
would work against this server unmodified.

## 3. What it does not link

`ldd` on the built daemon shows libc, libm, libgcc and the sanitizers. No ircituilib, no curses,
no PNG, **and not even uflib** — the golden replay is a table transcribed from the frozen fixture
rather than the file parsed at runtime. The absent `target_link_libraries` line in
`CMakeLists.txt` is the mechanical form of that claim, and
`dev/probes/conversation_streaming/server_cases.py` is what stops the transcription drifting: it
compares all nine replayed messages against the fixture file, field by field.

## 4. The four defects this is built against

Fixed as it was written rather than after, the way the MSP harness's own server records §28's.

| # | The document's defect | What is here instead |
|---|---|---|
| 1 | `send`'s result checked only for `< 0`, so a short write desynchronises the peer permanently | `sSendAll` sends every byte or fails; a failure closes that client |
| 2 | The broadcast holds the pool lock across a blocking `send`, so one stalled client freezes all of them and the simulation thread | The active slots are snapshotted **under** the pool lock and the lock is released before anything is written; each client has its own send mutex; and the broadcast's send is **non-blocking**, so a reader that has fallen behind is dropped rather than waited for |
| 3 | `accept() < 0` → `continue`, a 100% CPU spin on any persistent error such as `EMFILE` | `EINTR` retries; anything else backs off 100 ms |
| 4 | `server_running` is never cleared, so `close(server_fd)` is unreachable and only a signal exits | SIGINT/SIGTERM clear the flag and `shutdown()` the listener, so the path is reached — measured, exit 0 |

Two more, found while writing it and worth naming because they are the same class:

- The slot's send mutex **is not destroyed when a client leaves**. It used to be, in a first
  draft — and the slot goes back into the pool, so the next attach would have locked a destroyed
  mutex. The mutexes are initialised once, for the process, which is the correct lifetime for a
  fixed pool that outlives every client passing through it.
- `SO_RCVTIMEO` bounds every client thread's wait, so a shutdown does not hang on a client that
  is connected and silent — without a live-but-quiet client being dropped for being quiet.

## 5. The bound, and the arithmetic that set it

`CONV_STREAM_LINE_MAX` is **16384**, and the number is derived rather than picked. Writing both
spellings **doubles the dominant term**, so the worst frame the contract permits is:

```
6 bytes per byte (the widest escape, \u00XX)
  x 2 spellings
  x (CONV_STREAM_TEXT_MAX + CONV_STREAM_SNIPPET_MAX)
  + envelope                                    = 14328 bytes, measured
```

At the first draft's 8192 that frame was **refused** — a bound that refuses a value the header
itself calls legal is a defect in the server, not a defence. `p_stream_frames`, case `bounds`,
now fails if this bound and the two it is derived from ever stop agreeing, so the arithmetic
cannot rot silently.

## 6. Evidence

```sh
sh dev/probes/conversation_streaming/run_probes.sh     # 10 verdicts, 0 failures
```

| Probe | What it holds |
|---|---|
| `p_stream_schema` | the fixture decodes into the widget's own message type; either spelling decodes to the same struct; the stamp collision, the classifier trap, the enum vocabulary |
| `p_stream_frames` | the four builders against mjson; a hostile escaper; the fitting bound |
| `server_cases.py` | the running daemon: 13 frames per attach, the replay against the fixture, the port fallback, the 16-client ceiling, slot reuse, a non-reader not delaying anyone, SIGTERM and SIGINT |

**What `server_cases.py` does not claim: that a stalled reader is dropped.** The attach burst is
about 5 KB, which the kernel's send buffer absorbs whole, so no send ever has to make progress
and the `SO_SNDTIMEO` path cannot fire at this size. What is claimed is the property that policy
exists for — a client that never reads does not delay any other client — and that is measured.

## 7. Operationalising it

The delivery record — what was agreed, deviated, omitted and left unproven — is in
[DELIVERY.md](DELIVERY.md). This section is how to run it and how to read what comes back.

### 7.1 Build

Two builds exist and they are not the same binary.

| Build | Command | Sanitizers |
|---|---|---|
| the target the README names, and the one to use | `cmake -S . -B build/conv-stream -DIRCITUI_BUILD_CONVERSATION_STREAM=ON -DIRCITUI_BUILD_EXAMPLE=OFF` then `cmake --build build/conv-stream --target conversation_stream_server` | **on** — ASan + UBSan, from the project's defaults |
| the probe runner's own copy | `sh dev/probes/conversation_streaming/run_probes.sh` — compiles into a fresh `mktemp -d` | off (see [DELIVERY.md](DELIVERY.md) §2.7) |

The option is `OFF` by default and set by no preset, so an existing `cmake -S . -B build` is
unchanged — verified: with the option off, no target named `conversation_stream_server` exists.

### 7.2 Run

```sh
./src/examples/conversation_streaming/run_stream.sh            # 13 frames, summarised
FRAMES=3 ./src/examples/conversation_streaming/run_stream.sh   # fewer
./build/conv-stream/conversation_stream_server                 # or standalone, in the foreground
nc 127.0.0.1 6667                                              # or any reader you like
```

`run_stream.sh` puts the frames on **stdout** (raw, one per line, comparable between runs and
pipeable to `jq`) and the summary on **stderr** (the port bound, a per-frame type and byte count,
and the log path) — verified as 13 stdout lines and 17 stderr lines with no crossover. It waits for
the port the server *reports* rather than sleeping, because the port is not known in advance, and it
checks the server is alive while waiting so a bind failure surfaces as the server's own message.
`Ctrl-C` takes the same trap as a clean exit.

**It takes no arguments and the port is not one** — the server accepts none either, so a busy 6667
needs nothing from the caller. `FRAMES=<n>` is the only knob; an unexpected argument exits 2.

### 7.3 What a client receives

**First, thirteen frames of replay**, in this order. Measured byte counts:

| # | Frame | Bytes | What it is |
|---|---|---|---|
| 0 | `hello` | 305 | the fixture's envelope: identity, target composite, palette mapping |
| 1 | `presence` | 185 | Alice starts typing |
| 2–10 | `message` ×9 | 608 … 1111 | the nine golden messages, in fixture order |
| 11 | `media_progress` | 668 | one transfer slice at 25 %, `status:"sending"` |
| 12 | `presence` | 172 | the typing row cleared |

**Then the live generator**, one frame per cadence tick (400 ms by default, `CONV_STREAM_CADENCE_MS`
to change it). It cycles a table of ten scenes rather than sampling probabilities, because this
gate's exit criterion is that *every* value of every enumerated field is observed — a cycle makes
that a property of the table rather than of luck. Measured over 200 live frames:

```
frame_type       hello, message, presence, media_progress
direction        inbound, outbound, system
status           sending, delivered, read, expired
kind             message, status_indicator
cluster_position lone, top, middle, bottom
```

Each scene is a **run**: a maximal stretch of one sender's messages in one direction, planned whole
so its cluster positions can be assigned before any of it is sent. A run's first frame is never one
the previous message would join — that would make the clusters already sent wrong — and the census
asserts the property rather than trusting it.

The replay is a **contiguous prefix**: a slot does not become a broadcast destination until its
burst has gone out, so live frames never interleave into it. Measured at a 2 ms cadence, the
widest the window ever gets.

### 7.4 Reading the results

Each frame is one JSON object. The three things to check first:

| Question | Where to look |
|---|---|
| Did the transport work? | 13 objects arrived, each on one line, each parseable. A partial or interleaved line means a short write got through — it should not be possible |
| Did the replay survive? | `text_content` for each `message` equals the fixture's `conversation_flow[].text_content`, in order |
| Is presence a separate frame? | Every `message` frame has **no** `is_typing` key. If one has it, the protocol's central decision is broken |

Then, per frame type:

| Frame | Interpret it as |
|---|---|
| `hello` | the loader's identity check. `test_suite` and `target_composite_id` are the fixture's own values, and `generated_timestamp_epoch_ms` is the **frozen** `1790000000000`, not the current time — a reference must be inert to time |
| `message` | one `IrcitUiConversationWindowAppend` call. `kind:"status_indicator"` means it is not conversational (two of the nine are: the system notice and message 1005). `interactivity.is_collapsed` is `true` for exactly one message, 1008 |
| `presence` | one `IrcitUiConversationWindowSetTyping` call. `is_typing:true` carries the sender; `false` clears the row |
| `media_progress` | **nothing renders this.** It carries the message fields *and* `media_payload`, because a client with no row for that id needs everything to make one — but the widget has no media field and no mutation API, so the three slices of one transfer would append three bubbles rather than advance one. The generator emits them as a sequence sharing one id, advancing to 100 % |

**`cluster_position` is now honest and still advisory.** The wire carries the position the widget
will actually compute, from the same run rule (`src/ui_conversation.c:63-70`) — so a reader can
trust it, and the widget will still overwrite it. The census re-derives every position from the
stream alone and fails if the two ever differ.

**Three replayed messages deliberately differ from the fixture here.** The fixture names 1005
`lone` and 1006 `top`; the widget's own rule makes 1005–1007 one outbound run, and
`FIXTURE-FIELDS.md` records the fixture contradicting the algorithm at exactly that point. The wire
sends what will be rendered.

`timestamp` is the **display string** (`"08:31"`), never an epoch: the widget prints it verbatim and
never parses it. The epoch is in `timestamp_epoch`, and **the two are one clock**: both are rendered
from a single simulated instant, so the stamp always names the instant the epoch is — verified over
188 frames with no mismatch, and asserted by the census (see below).

`hello` is the exception, and deliberately: its `generated_timestamp_epoch_ms` is the fixture's
**frozen** `1790000000000`, not a live instant, because a reference must be inert to time.

### 7.5 Diagnosing

The server logs to stdout, so `run_stream.sh` sends that to a file (`/tmp/conv_stream_server.log` by
default) and tells you where. What the lines mean:

| Log line | Meaning | What to do |
|---|---|---|
| `listening on 0.0.0.0:NNNN, up to 16 clients` | bound; this is the port to use | read the port from here, never assume 6667 |
| `generator running at N ms per frame` | the cadence in force, from `CONV_STREAM_CADENCE_MS` | if it is not 400, something set the variable |
| `port 6667 is taken, trying 6668` | the fallback walked | nothing, unless you needed that exact port |
| `client attached to slot N` | the slot is taken, but the client is **not** a broadcast target yet | — |
| `slot N: attach burst sent, replay is contiguous` | all 13 replay frames went out and the slot is now a broadcast destination | this is the line that proves no live frame interleaved into the replay |
| `slot N is not keeping up; dropping it rather than delaying the others` | a broadcast send could not complete: the reader's window is full | that client alone is dropped; the others are unaffected by construction |
| `slot N: the attach burst did not complete; dropping the client` | a send failed mid-burst; that client alone was dropped | check the peer stopped reading; other clients are unaffected by construction |
| `all 16 slots are in use, refusing the connection` | the ceiling | wait for a slot, or raise `CONV_STREAM_MAX_CLIENTS` |
| `accept failed (...), backing off` | a persistent accept error, e.g. `EMFILE` | check the fd limit; the loop backs off rather than spinning |
| `control: <verb> accepted` | a command was acted on | — |
| `control: <verb> refused -- <why>` | a command was not; the reason is in the `control` frame too | fix the command, or read the reason |
| `slot N sent a command line past N bytes; refused` | a client sent an over-long line | the client is not speaking this protocol |
| `slot N: client gone, M attached` | the client closed, or its thread saw the shutdown | — |
| `slot N retired (M attached)` | a slot was released by the broadcast's drop path rather than by its own thread | — |
| `N client(s) did not drain` | shutdown waited 5 s and some client threads had not noticed | usually a client that never closed; the daemon exits anyway |
| `exited cleanly` | the shutdown path was reached | this is the line that proves defect #4 of the design document is fixed |

Exit codes: `0` on SIGINT/SIGTERM **and** on SIGTERM received mid-burst (`shutdown()` unblocks
`accept`); `EXIT_FAILURE` only if it could not bind any port in 6667–6676 or a socket call failed
outright. A peer going away mid-frame never kills the daemon — `SIGPIPE` is ignored and `MSG_NOSIGNAL`
is set on every send.

### 7.8 Steering it: the control channel

Until this existed the server had **no inbound path at all** — a person testing a widget could only
wait for the generator's cycle to reach the state they wanted. Now a client sends commands on the
same socket, and the server answers with a `control` frame:

```sh
printf '{"command":"pause"}\n' | nc 127.0.0.1 6667     # and watch a `control` frame come back
```

| Verb | Effect |
|---|---|
| `pause` | the generator stops emitting. **The replay is unaffected** — it is per-attach and already sent |
| `resume` | it starts again |
| `step` | exactly one frame, whether paused or not. This is what pausing is for |
| `speed` with `arg` | a **percentage** of the configured cadence: 400 is 4×, 25 is a quarter. Clamped 1–10000 |
| `scene` with `arg` | jump to a scene index, 0–9. **Takes effect at the next run boundary**, never mid-run |
| `status` | change nothing; report the state |

Every command is answered by one `control` frame carrying the whole state — `event`, `command`,
`accepted`, `paused`, `speed_pct`, `cadence_ms`, `next_scene`, `next_scene_name`, `frames`, and a
`detail` that **says why a refusal was refused**. The answer is **broadcast to every client**, not
just the one that asked: the state is global, so a client left believing the stream is running would
have no way to find out otherwise.

**A scene jump waits for the run boundary, and that is not laziness.** A run's cluster positions were
assigned before its first frame went out, so cutting one short would leave a `top` with no `bottom`
and the widget would compute something else.

**The command grammar is a strict JSON subset**, because the server links no JSON library and that
absence is load-bearing (see §3). No nesting, no escapes, no duplicate keys, and hard bounds on every
field; anything outside the subset is **refused with a reason**, never partially acted on.
`p_stream_control` attacks it with **46 hostile lines** and canaries around the parser's output.

### 7.6 Operational limits

| Limit | Value | Consequence |
|---|---|---|
| clients | 16 | the 17th is accepted then closed — it sees EOF, not a refusal message |
| send timeout | 5 s | a genuinely dead client holds its slot up to 5 s per frame |
| receive timeout | 1 s | bounds shutdown; a live-but-quiet client is not dropped |
| replay per attach | 13 frames | contiguous, then the generator's frames follow |
| command line | 512 bytes | a longer line is refused whole rather than truncated into something parseable |
| command verbs | six | `pause`, `resume`, `step`, `speed`, `scene`, `status`. Anything else is refused by name |
| cadence | 400 ms | `CONV_STREAM_CADENCE_MS`, clamped to 1–60000. Lower it for a census, not for a person |
| generator standby | idles at 50 ms | with no client past its replay there is nobody to talk to, and standing by keeps a late client from joining mid-run |
| broadcast send | **non-blocking** | a reader that has fallen behind is **dropped**, not waited for — unlike the attach burst, which may block on its own thread |
| drain budget at shutdown | 5 s | then it exits regardless |

### 7.7 Checking a change

```sh
sh dev/probes/conversation_streaming/run_probes.sh      # 15 verdicts; non-zero if any claim broke
```

It compiles every probe and the server from scratch, runs the 5 schema cases, the 3 frame cases, the
4 command-parser cases, the 12 socket cases and the 6-case generator census, and exits non-zero if
any verdict fails. Every count in this README and in [DELIVERY.md](DELIVERY.md) came from that run.

## 8. Not delivered

| Item | Why |
|---|---|
| **The client** | Its own authorisation. It must use `IrcitUiConversationWindowAppend`; the document's `IrcitUiMessageViewportCreate` does not exist in this tree. |
| **Anything rendering a `media_progress` frame** | The widget has **no media field and no mutation API** — it is append-only. A progress sequence sharing a message id appends N bubbles rather than advancing one, so these frames are emitted for a seam that is not built. |
| **Time-gap dividers** | The widget clusters by runs of same sender and direction with **no timestamp input** (`src/ui_conversation.c:225-234`). The document's 300-second rule and its timeline divider have no implementation to drive. |
| **`cluster_position` being honoured** | It is advisory: the widget computes it and overwrites whatever the caller set. The wire now agrees with that computation, but the field still steers nothing. |
| **Time gaps in the stream** | The generator's cadence is sub-second, so no 300-second gap ever appears on the wire — that rule is the widget's and is unimplemented anyway (above). |

## 9. One discrepancy this track found and did not repair

`dev/probes/conversation/FIXTURE-FIELDS.md` heads its census *"136 value instances across 30
distinct key paths"*. The script it names as its own authority, re-run against the committed
fixture, reports **129 across 29** — and nothing in the repository asserts either number. It is
recorded by `run_probes.sh` as a DISCREPANCY and left alone: that file belongs to the
conversation track, not this one.

---

# APPENDIX A — the plan this track was built to

## Context

The conversation window renders a frozen reference well and can be handed nothing live. This
track gives it a source: a daemon that writes that conversation down a socket in a frame shape
the window's own ingest path can consume.

## The finding that shaped the protocol

The design document's server and client disagree with each other in a way that makes the pair
useless: every frame carries `presence_update.is_typing=false` and the client's classifier reads
any such frame as "typing off" and drops it. So the protocol could not be adopted verbatim, and
the frame vocabulary had to be reconcilable with the frozen fixture the widget already reads.
Both are satisfied by carrying both spellings from one field list and making presence a frame
type — see §2.

## The gates

| Gate | Content | Exit criterion | Status |
|---|---|---|---|
| **G0** | No product code. Does the planned wire decode into the widget's own type? | The fixture's 9 messages reconstruct field-for-field; the census | **DONE — green.** One discrepancy found and recorded, §9. |
| **G1** | Protocol header, the transport, the fixed replay | Builds clean under `-Wall -Wextra -Wpedantic -Werror`; binds with fallback, serves, and is dropped by a socket probe that asserts frames against the fixture | **DONE — green.** §3–§6. |
| **G2** | The live scenario generator: clustering runs, all four statuses, decay, threading, presence, media slices, and a census probe over N frames | Every enum value in every field observed over a run | **DONE — green.** 290-check census; see §7.3 and [DELIVERY.md](DELIVERY.md) §1 |
| **G3** | The delivery record, `run_stream.sh`, and a reproducible build | A cold clone reproduces | **DONE, with one caveat** — see below |
| — | **The client** | Separate authorisation | not started |

## G3's exit criterion, and the one part of it that cannot be met yet

"A cold clone reproduces" is three claims, and only two of them are testable while this work is
uncommitted:

| Claim | Status |
|---|---|
| A clean **build directory** reproduces from the documented commands | **verified**: `rm -rf build/conv-stream`, re-configure, re-build, re-run the probes — 10 verdicts, 0 failures |
| The probe set reproduces with no state carried over | **verified**: `run_probes.sh` compiles every probe and the server into a fresh `mktemp -d` on each run, and `server_cases.py` starts the daemon itself |
| A literal `git clone` reproduces | **cannot be tested, and would FAIL today** — a clone carries only what is committed, and none of these files are. It becomes testable at the first commit of this track, which needs its own authorisation |

So G3 is complete as a *document* and as a *script*, and its third claim is gated on a commit
rather than on more work.

## Standing constraints, unchanged

No commits without instruction. `IRCITUI_INTERNAL` is bumped only on **"wrap-up gate"**, not per
file. No file outside `src/examples/conversation_streaming/`, `dev/probes/conversation_streaming/`
and the one CMake block has been touched.
