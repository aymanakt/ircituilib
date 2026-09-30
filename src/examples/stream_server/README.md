# The MSP stream server — a simulation daemon and a TUI client

Two programs, in `src/examples/stream_server/` alongside the other example, and the reason they
exist is that the demo had become the only vehicle for everything.

## Contents

- [1. The architecture](#1-the-architecture) — two processes, one socket, and where the thread boundary falls
  - [1.1 One metric frame's journey](#11-one-metric-frames-journey)
- [2. The wire](#2-the-wire) — the 64-byte frame, and why it is not §28's binary one
- [3. What each program is](#3-what-each-program-is)
  - [3.1 Which widget each channel drives, and what is missing](#31-which-widget-each-channel-drives-and-what-is-missing)
  - [3.2 Controls](#32-controls)
- [4. Three defects this pair's own probes found](#4-three-defects-this-pairs-own-probes-found)
- [5. Evidence](#5-evidence)
- [6. The protocol's own defects, for a reader of §28](#6-the-protocols-own-defects-for-a-reader-of-28)
- [7. Flicker: what it was, and what is still wrong](#7-flicker-what-it-was-and-what-is-still-wrong)
  - [7.1 What it was blamed on, and what it was not](#71-what-it-was-blamed-on-and-what-it-was-not) — the demo-UI reuse is not the cause of the flicker
  - [7.2 The readouts cleared the whole terminal (FIXED)](#72-the-readouts-cleared-the-whole-terminal-fixed)
  - [7.3 The client inherits the demo's key handlers (NOT FIXED — owner's call)](#73-the-client-inherits-the-demos-key-handlers-not-fixed--owners-call)
- [8. Not delivered](#8-not-delivered) — stated plainly, so nothing here reads as finished when it is not
- [9. A note on the instruments, because it cost a turn](#9-a-note-on-the-instruments-because-it-cost-a-turn)
- [Appendix A — the plan this track was built to](#appendix-a--the-plan-this-track-was-built-to) — the approved plan, verbatim, plus a path map for what has moved
  - the plan's own sections: [Context](#context) · [The finding that shapes the protocol](#the-finding-that-shapes-the-protocol) · [The specification's defects, verified](#the-specifications-defects-verified) · [The stages](#the-stages) · [Files](#files) · [Verification](#verification) · [Left open, deliberately](#left-open-deliberately) · [Standing constraints, unchanged](#standing-constraints-unchanged)

Sections 1–9 are this README's; the `§` numbers throughout are the source specification's; the
numbering inside Appendix A is the plan's own and is not continued here.

| | |
|---|---|
| **What this is** | A multi-threaded simulation daemon streaming metrics over a Unix domain socket, and a separate client program that renders them |
| **Where** | `src/examples/stream_server/` — `msp_server.c`, `msp_protocol.h`, `client/harness_client.c`, `run_pair.sh`, this README |
| **Source specification** | **`dev/technical_designs/IRCITUILIB_TEST_STREAMING_SERVER_PROPOSAL.md`** — the standalone copy of the sections from `# 28.` on, which is what this track was built from. Every cadence, behaviour, frame field and channel binding below is that document's, and every `§` number quoted in this README and in the sources is a section of it. Its parent document is `../../../dev/technical_designs/IRCITUILIB_PROGRESS_INDICATORS_DESIGN_PROPOSAL.md`, which the §9.x citations from the shared design belong to |
| **Evidence** | `dev/probes/harness/` — the probes, and a README saying what each case asserts |
| **The plan it was built to** | **Appendix A**, at the foot of this file — the approved plan, preserved as it was approved, with a path map for the one thing that has moved since |

**The demo is not touched by either program.** `src/examples/ui_main.c` and `layout/ircitui.yaml`
are not inputs to this pair and are not changed by it.

**This directory was renamed.** The pair was built in a top-level `harness/`, and the README you
are reading moved with it from there. The directory is the *only* thing that changed: the CMake
option (`IRCITUI_BUILD_HARNESS`), the two target names (`msp_server`, `ircitui_harness_client`),
the build directory (`build/harness`) and the probe directory (`dev/probes/harness/`) all keep
the name they were given, so every command in this file and in the probes is unchanged.

```sh
cmake --preset dev -B build/harness -DIRCITUI_BUILD_HARNESS=ON
cmake --build build/harness -j

./src/examples/stream_server/run_pair.sh                # both of them, in order, with the teardown
./src/examples/stream_server/run_pair.sh /tmp/other.sock
```

`run_pair.sh` starts the server, waits for its socket, runs the client in the foreground with
the terminal, and takes the server down when the client leaves -- **F12 quits, and Ctrl-C does
the same**, because the teardown is on a trap rather than on the happy path. The server's
output goes to a log file (`$LOG`, `/tmp/msp_server.log` by default) and is printed at the end,
because anything the server wrote to the terminal would land in the middle of the client's
ncurses frame. `SERVER=`, `CLIENT=`, `LOG=` and the first argument override the paths.

Or by hand, which is what to do when one of the two needs watching on its own:

```sh
./build/harness/msp_server /tmp/msp.sock &    # or no argument for the default path
./build/harness/ircitui_harness_client /tmp/msp.sock
```

`IRCITUI_BUILD_HARNESS` is **OFF by default**, and no preset sets it, so an existing
`cmake -S . -B build` is unchanged and the harness gets its own build directory. It is not
nested inside `IRCITUI_BUILD_EXAMPLE`: the harness must build with the demo unbuilt.

Start the server first, or start the client first — the client runs with no server and says so
on stderr rather than refusing to open.

## 1. The architecture

Two processes and one socket. The server never links the library; the client never reaches into
`src/include`. Everything between them is the 64-byte frame below.

```text
SERVER  ·  msp_server                                  (links nothing from this project)
────────────────────────────────────────────────────────────────────────────────────────

  SIMULATION — 3 threads                BROADCAST
  ┌──────────────────┐                 ┌─────────────────────────────────────┐
  │ media     250 ms │──┐              │ s_broadcast(frame)                  │
  ├──────────────────┤  │              │   per-slot mutex → send()           │
  │ cpu       100 ms │──┼─────────────►│   SO_SNDTIMEO → slow reader DROPPED │
  ├──────────────────┤  │              └──────────────────┬──────────────────┘
  │ download  200 ms │──┘                                 │
  └──────────────────┘                                    │
    per-thread xorshift PRNG                              │   MMSP    server → client
                                                          │   64 B, CRLF, printable
  CONTROL — 4th thread                                    │
  ┌──────────────────────────────┐                        │
  │ s_read_loop                  │                        │
  │   poll(timeout 0) → MCMD     │                        │
  │   → g_ctl: pause·error·speed │                        │
  └──────────────▲───────────────┘                        │
                 │                                        │
                 │   MCMD    client → server              │
                 │   64 B, CRLF, digits and printable     │
─────────────────┼────────────────────────────────────────┼─────────────────────────────
    AF_UNIX · SOCK_STREAM · /tmp/ircitui_sim_metrics.sock       (argv[1] overrides)
─────────────────┼────────────────────────────────────────┼─────────────────────────────
                 │                                        │
                 │                                        ▼
CLIENT  ·  ircitui_harness_client                   (public API only; no src/include)
────────────────────────────────────────────────────────────────────────────────────────

  ┌──────────────┴──────────────┐        ┌──────────────────────────────┐
  │ sSendCommand()   [THREAD 0] │        │ sReaderThread                │
  │   poll(POLLOUT, 0) gate     │        │   recv() → PostChunk()       │
  │   send(MSG_NOSIGNAL)        │        └─────────────────┬────────────┘
  └──────────────▲──────────────┘                         │
                 │ dispatcher queue — waits, never drops  │
                 │                                        │
  ┌──────────────┴────────────────────────────────────────┴───────────────────────┐
  │ THREAD 0 — IrcitUiMainLooper · the only thread that ever touches the tree     │
  │                                                                               │
  │  sOnChunk()         MspRxPush → MspRxNext → sPercent → IrcitUiBarSetValue     │
  │                                → IrcitUiBarSetBuffered                        │
  │                                → IrcitUiNodeSetTextLive    (readouts, label)  │
  │  sTickExpiry()      clock tick → the label's expiry (§32)                     │
  │  sOnControlClick()  prog_close click → sSendCommand()                         │
  │  sOnKey()           IrcitUiBindKey — bound, NEVER FIRES (open finding)        │
  └───────────────────────────────────────────────────────────────────────────────┘
```

**Reading it.** Four properties are the design, and each is a deliberate choice rather than an
accident of the code:

| | |
|---|---|
| **The thread boundary is the whole point.** | No server thread ever touches a node, and neither does the client's reader. The only thread that touches the tree is Thread 0, and it is the only one that may — which is why the reader *posts* rather than calls, and why the control path is entered only from a binding, which runs on Thread 0. |
| **The post waits; the broadcast drops.** | Opposite policies, for opposite reasons. `IrcitUiDispatcherPostNetworkChunk` blocks for its deadline rather than discarding, because a lost chunk desynchronises the reassembler permanently — so a full pipeline slows the reader down instead, and that pushes back on the sender through the kernel's receive window. The server faces eight slots and may not let one slow reader stall a simulation thread, so it carries `SO_SNDTIMEO` and **closes** the laggard instead. |
| **The two lanes are not symmetric.** | The metric lane is one-way broadcast to every attached client; the command lane is per-connection and its whole discipline is a zero-timeout `poll` before the `send`, because blocking on a full send buffer from Thread 0 would wedge the application. |
| **None of these threads belong to the framework.** | The server's four and the client's reader are this track's. The framework contributes one thread — Thread 0, inside `IrcitUiMainLooper` — plus the queue between them. It spawns nothing itself. |

### 1.1 One metric frame's journey

1. A simulation thread wakes from its cadence sleep (media 250 ms, CPU 100 ms, download 200 ms) and builds an `MspFrame` from its own state.
2. `s_broadcast` walks the client slots under each slot's mutex and `send`s the frame whole.
3. The kernel hands those 64 bytes to `sReaderThread`'s blocking `recv`.
4. The reader posts the chunk — bytes, length, and a 200 ms deadline — and goes straight back to `recv`.
5. Thread 0's drain calls the registered network listener, `sOnChunk`, which is the one callback in the framework that runs **without the watchdog**.
6. `MspRxPush` / `MspRxNext` reassemble it: a frame is the 64 bytes ending at a line feed, and anything else is one byte of noise shifted off the front and counted.
7. `sPercent` guards the value — `isfinite` → clamp to `[0,1]` → round-half-up — and the three setters write the bar, its buffered band, and the readout rows.
8. The frame is drawn on the next pass of the loop.

The return path is the same shape in reverse, and it only ever starts on Thread 0: a click on
`prog_close` runs `sOnControlClick`, which polls the fd for writability and then sends an `MCMD`
frame; the server's fourth thread picks it up with a zero-timeout `poll` and applies it to `g_ctl`,
which is what the three simulation threads read on their next pass.

## 2. The wire

`IRCITUILIB_TEST_STREAMING_SERVER_PROPOSAL.md` §28 specifies "fixed-width binary frames" of
packed bytes. **That cannot cross this framework's own network seam**, and the measurement is in
`dev/probes/harness/p_nul.c`: the listener takes a `const char *` and *no length*, and the post
NUL-terminates what it copies, so the only route to a chunk's length is `strlen()`. §28's own
struct, posted at 56 bytes, arrives as **5**. So the frame length and the channel semantics are
the section's and only the encoding changed.

**The proposal file is the contract for everything below.** Where this README says "the
section", that is a section of
`dev/technical_designs/IRCITUILIB_TEST_STREAMING_SERVER_PROPOSAL.md`; where it quotes a `§9.x`,
that is the shared design document the proposal was extracted from, at
`../../../dev/technical_designs/IRCITUILIB_PROGRESS_INDICATORS_DESIGN_PROPOSAL.md`. Read alongside the server
source (`msp_server.c`), the proposal is enough to rebuild this pair from nothing.

```
 MMSP │ cc │ pppppppp │ bbbbbbbb │  tttttttttt │ TTTTTTTTTT │ label ×20 │ CR LF
  4     2       8          8           10            10          20        2   = 64
 magic  %02d  %8.5f    %8.5f    %10lld        %10lld      padded      framing
```

Every payload byte is in `[0x20,0x7E]`, so `strlen` is exact. The parser is
**self-synchronising** — a frame is the 64 bytes ending at a line feed, and anything else is one
byte of noise shifted off the front and counted — which is the property a fixed-size binary
parser cannot have, and the reason the client survives a desynchronised stream rather than
staying wrong for ever.

The command frame, client to server, is the same length and the same discipline: `MCMD`, the
channel, the command, a `%10.4f` value, and padding.

## 3. What each program is

| | |
|---|---|
| `msp_server` | Three simulation threads on §28's own cadences (media 250 ms, CPU 100 ms, download 200 ms) with the section's behaviour kept verbatim: caching stalls, thermal dropouts, progressive speed decay and a mid-run freeze. It **links nothing from this project** — the absent `target_link_libraries` on that target is the assertion that the protocol is independent of the framework it feeds. |
| `ircitui_harness_client` | A TUI that connects, reassembles, and drives the bars gallery. `generated/ui_generated.c` is **not** among its sources: that file includes `ui_priv.h`, so it cannot be compiled outside the library — it does not need to be, because it is already an archive member and the linker extracts it. There is therefore exactly one model in the process. |

### 3.1 Which widget each channel drives, and what is missing

§28's table maps the three channels onto a three-state media bar with a pre-fetch track, a
circular dial gauge, and a standard bar. **Two of those three widgets do not exist in this
tree** — there is no dial kind and no media-bar kind, and no `prefetch` field anywhere in
`src/`, `include/`, `layout/` or `tools/`. So:

| channel | value | buffered | readout |
|---|---|---|---|
| `CH_MEDIA_YOUTUBE` | played | pre-fetch | time, buffer, label |
| `CH_DIAL_CPU` | core load | — | throttle, label |
| `CH_DL_LARGE` | download | — | throughput, label |

The media row is the one that earns its keep: §21's pre-fetch track *is* the bar's second
position, so one channel drives both fields of `bar_buffered` and the band between them is the
cache. When §21 and §24/§25 land, this table changes and the wire does not.

### 3.2 Controls

**One**, and it is a click rather than a key. `prog_close` carries a click binding that sends
`CMD_TOGGLE_PAUSE`, which exercises the transmit path end to end — build, writability gate,
`MSG_NOSIGNAL`, and the server applying it.

THE KEYS ARE BOUND AND DO NOT FIRE, which is an open finding rather than a design. `IrcitUiBindKey`
and `IrcitUiEventKeyCode` were added BECAUSE of this client — the public surface had
`IrcitUiBindClick` and `IrcitUiBindSeek` and no way to bind a key at all, and `struct IrcitUiEvent`
is defined only in `src/include`, so a bound callback could not read the key it was handed. Both
now exist and the binder **resolves** (the client reports no failure). But a listener bound on the
panel, and on the tree root, is never called for `' '` or `'x'`, while F12 — the framework's own
exit — demonstrably reaches the loop. Two candidates, neither diagnosed: the engine may consult
only the focused leaf rather than walking ancestors, or a default listener on the model may consume
the key first. See `dev/probes/harness/README.md` for the same item in the track's own record.

`F12` exits: that is the framework loop's own exit, not this program's.

Once the key path works, the map in `sOnKey` is space = pause, `x` = a media error, `d` = a
download error, `1`/`4` = the speed multiplier.

## 4. Three defects this pair's own probes found

Recorded here because each was in this track's code and each was invisible by inspection:

- **A seven-frame burst was refused whole.** `MSP_RX_CAP` was four frames (256 B); the
  transport's largest chunk is 511 B, which carries seven. A cap below the transport's own
  maximum is not a safety bound, it is a silent frame dropper.
- **A `0x00` at offset 18 of every command frame.** `snprintf`'s terminator, uncovered by the
  next field — so the parser refused every frame the builder produced. Each end was
  self-consistent; the pair was useless.
- **The client drew nothing, twice.** The first version never called `initscr`, so the program
  ran its loop and emitted not one escape sequence. The fix is in `sOnDriverInitialised`, and
  the sequence there was read from the demo's own hook rather than invented.

## 5. Evidence

`dev/probes/harness/` — `run_probes.sh`, seven probes, and `README.md` saying what each case
asserts. The server is measured over a real socket by `server_cases.py` (23 cases); the client's
end-to-end run is a pty session and is named as the weakest link in that README rather than
presented as proof.

## 6. The protocol's own defects, for a reader of §28

Fixed as the server was written, and listed so nothing was fixed silently: the short `send` on
a non-blocking fd that left half a frame on the wire; the `accept` that spins at 100 % CPU on
any persistent error; the `thrd_join` that is unreachable because nothing clears the run flag;
the client fd read outside its lock; the `unlink` that would remove a regular file; the shared
`rand()`; and the `1000000_long` literal, which is not a C17 suffix and does not compile.

---

## 7. Flicker: what it was, and what is still wrong

Two separate faults, found by running the pair. The first is fixed and measured; the second is
not, and is the reason the client can still be made to lose its text. §7.1 records what they were
blamed on first, because the more natural answer was the wrong one.

### 7.1 What it was blamed on, and what it was not

Two attributions were made before this was understood. They are recorded because one of them was
right and the other is the more natural guess, and because the two faults arrived together and
read as one for as long as they did.

| blamed | verdict |
|---|---|
| **The library** — the owner's own first read of it | **Right, as to origin.** The cost is in `IrcitUiNodeSetText` and in the layout pass it triggers. The client is the caller that pays it, not the thing that invented it |
| **The client reusing the demo's UI verbatim** — the same decision that put the demo's bars gallery on screen, and with the model the demo's key handlers | **Wrong for the flicker.** It is the cause of a *different* fault, §7.3 — and it is a real fault. It is just not this one |

The evidence that separates them: the flicker comes from the setter → layout → re-realize path,
which **every** caller of `IrcitUiNodeSetText` reaches whatever template it inflated, and the fix is
a different setter — nothing to do with which layout is on screen. §7.3 is the other way round: it
disappears entirely if the client is handed a tree with no demo listeners on it. The template is
all of that fault and none of this one.

**And it is not a harness-only fault.** Reuse is what made it *visible* here, because this client
writes three readouts at frame rate where the demo writes one on demand. The demo carries the same
two shapes: `IrcitUiNodeSetText` on its `status` label (`src/examples/ui_main.c:2140`), and
`IrcitUiRequestLayout` on the **whole root** from its network line handler
(`src/examples/ui_main.c:2525`) — a broader request than any this client makes. The owner reports
flicker in the demo as well. Neither was changed: the demo is out of bounds by ruling, and
`SetTextLive` is opt-in, so the demo still calls the layout-requesting setter. That is recorded in
§8 rather than quietly fixed.

### 7.2 The readouts cleared the whole terminal (FIXED)

`IrcitUiNodeSetText` requests a layout pass — deliberately, because a text change can change a
node's wrapped height. A layout pass re-realizes the subtree: windows are torn down and rebuilt,
and a window re-created at screen size carries ncurses' own clear flag, so the next refresh
emits `ESC[2J` and the terminal repaints wholesale. The client writes three readouts a few times
a second, so it was clearing the screen continuously.

Measured, `ESC[2J` per second over a six-second run, against a no-server control:

| | bytes/s | `ESC[2J`/s |
|---|---|---|
| before | 156,785 | **15.5** |
| after (`IrcitUiNodeSetTextLive`) | 2,899 | **0.2** |
| no-server control | 1,885 | 0.3 |

The fix is a second public setter, `IrcitUiNodeSetTextLive`, which assigns the text and marks
the node for **repaint only**, requesting no layout. The caller asserts the text cannot change
the node's box — true for every readout here (fixed height, `match_parent` width). The
layout-requesting setter stays for `wrap_content` nodes, where the pass is what makes a change
visible at all.

`ESC[2J` per second is the objective flicker metric and is worth keeping: it is how this was
found and how any future regression should be judged, in preference to looking at a terminal.

**A caveat on the fix.** It removes the *symptom* the client produced. The underlying cost — a
layout request re-realizing a subtree and clearing the screen — is the library's behaviour and
is unchanged; `SetTextLive` is an opt-out that each caller must choose correctly. A caller that
picks it wrongly gets a stale box.

### 7.3 The client inherits the demo's key handlers (NOT FIXED — owner's call)

**Symptom.** Type in the client and the panel blanks except for the bar glyphs.

**Cause, reproduced.** The client reuses `generated/ui_generated.c`, so its screen tree is the
**demo's model** — and with the model come the demo's interaction listeners. Typing a control
key fires the demo's handler. `Ctrl-G` is the demo's palette-effect cycle; the canopy/gradient it
turns on repaints over the panel and wipes the text.

| keys sent | what is on screen |
|---|---|
| none | title, captions, `Close`, and all three readouts |
| `Ctrl-G` | title, captions, `Close` — **all three readouts gone** |

Reproduced at the owner's own geometry, 74×231, which otherwise renders the whole panel
correctly with or without ordinary typing.

**Why it is not a one-line fix.** The public API cannot clear the model's `on_key_down`
listeners: `IrcitUiBindingReset` covers `IrcitUiBindClick` and nothing else, and the listener
lives in `src/include/ui_priv.h`. Three options, all the owner's to choose:

| | |
|---|---|
| **A** | A purpose-built template for the client. Blocked on the same thing as the dashboard question in §8: a second generated model cannot be compiled by a public-API-only target, so either the client takes `src/include` (and stops being the proof that the public API suffices) or the template must be added to `layout/ircitui.yaml`, which is out of bounds by ruling |
| **B** | A public call to detach a node's listeners, in the family of `IrcitUiBindKey`. Small, and it makes model reuse safe for any consumer |
| **C** | The client forces the palette effects off and re-asserts them each frame. Reduces the damage; does not stop the key |

**Recommended: B.** This is the third fault on this track with the same shape — a public surface
missing one piece — and the only one whose fix generalises.

## 8. Not delivered

Stated plainly, so nothing here reads as finished when it is not.

| | |
|---|---|
| **The client's dashboard is the demo's bars gallery** | 18 of its 24 widgets are inert; only three bars and three readout rows are driven. The escape (a purpose-built layout) was priced in the plan and deferred at Gate 3, and the deferral was never put to the owner |
| **`IrcitUiBindKey` binds, resolves, and never fires** | Added for this track (with `IrcitUiEventKeyCode`), and the client's key map — space, `x`, `d`, `1`, `4` — is dead. Keys demonstrably reach the framework (F12 exits the client cleanly); a listener bound on the panel, and on the tree root, is never consulted. Not diagnosed: the engine may consult only the focused leaf, or a default listener on the model may consume first |
| **§31 severity styling** | The label is carried and displayed; it is not *styled* by severity, because there is no per-node runtime style setter and `IrcitUiPaletteSetSlot` recolours a role for every user of it. Severity travels in the label's text |
| **Gate 5a's expiry is unverified end to end** | The mechanism is measured (`dev/probes/harness/p_expiry.c`, 23 checks: continuations run inline on the completing thread, a cancelled arming stays cancelled). The client is wired and builds. No label has ever been *watched* appear and expire on the glass |
| **Gates 5b–5d and 6** | Not started: the `UfsrvFutureZip` join-barrier, the race timeout, the flat-map diagnostics chain, and the track's own record. Note that `UfsrvFutureZip`/`Any`/`Map`/`FlatMap` have never been called by any application on this machine |
| **A short terminal still clips the panel** | The panel now sizes to the terminal and re-fits on resize (it was hard-coded 40×30, which hung off anything shorter than 32 rows, and a *stacked* node is never re-laid by the framework, so a resize left it there permanently). But the content is ~26 rows, so a short terminal still clips the tail — correct, and not the same as fixed |
| **The 34×18 float cap (§9.35)** | Worked around by re-laying the panel out by hand, not fixed |
| **The demo's own flicker** | §7.1 records it: the demo reaches the same re-realize path twice over — `IrcitUiNodeSetText` on its `status` label (`src/examples/ui_main.c:2140`) and `IrcitUiRequestLayout` on the whole root from its network line handler (`:2525`). `IrcitUiNodeSetTextLive` is opt-in, so the demo still uses the layout-requesting setter and still pays. Not changed: the demo is out of bounds by ruling, and the owner has seen this on the glass |
| **`p_live.c` is written but not wired into `run_probes.sh`** | It pins the flicker fix's shape — inflate, re-lay out, paint-only update, through `IrcitUiRenderFrame` — and should run with the rest |
| **Nothing is pushed** | The track's commits sit on the local branch; pushing needs its own authorisation |

## 9. A note on the instruments, because it cost a turn

Twice on this track a conclusion was drawn from grepping the pty byte stream for a contiguous
string. That is not a valid instrument here and this repository's own test-strategy document
says so: ncurses writes cells with escape sequences interleaved, so text arrives in fragments.
A "the readouts are frozen" finding was produced that way and was wrong — an instrumented build
showed the client setting them 61 times in four seconds. Where a claim is about cells, read
cells (`mvwin_wch` + `getcchar`, as the probes do) or measure bytes and clears; do not grep.

---

# APPENDIX A — the plan this track was built to

Reproduced **verbatim**, headings and all, so it can be diffed against the copy it came from
rather than taken on trust. It is the approved plan, not a summary of what happened: where the
build diverged from it, the divergence is recorded in the body of this README above (Gates 0–4
were built; Gate 0 falsified the plan's own Gate 3 premise about anchored inflation; Gates 5b–6
were not started).

**One thing in it has moved.** The plan names the pair's sources as `harness/…`, which is where
they were written. They now live in this directory. Everything else it names is still where it
says:

| the plan says | it is now |
|---|---|
| `harness/msp_protocol.h` | `src/examples/stream_server/msp_protocol.h` |
| `harness/msp_server.c` | `src/examples/stream_server/msp_server.c` |
| `harness/client/harness_client.c` | `src/examples/stream_server/client/harness_client.c` |
| `harness/README.md` | `src/examples/stream_server/README.md` — this file |
| `./harness/run_pair.sh` | `./src/examples/stream_server/run_pair.sh` |
| `IRCITUILIB_VISUAL_EFFECTS_DESIGN.md` | split: `dev/technical_designs/IRCITUILIB_TEST_STREAMING_SERVER_PROPOSAL.md` is `# 28.` onward — sections 28 and 30–35, the part this track was built from; its parent, now `../../../dev/technical_designs/IRCITUILIB_PROGRESS_INDICATORS_DESIGN_PROPOSAL.md`, holds `# 1.`–`# 27.` plus two empty headings, `# 36.` and `# 37.`, which were already empty before the split. There is no §29 in either, and never was |
| `build/harness`, `IRCITUI_BUILD_HARNESS`, `msp_server`, `ircitui_harness_client`, `dev/probes/harness/` | **unchanged** — the rename stopped at the directory |

The source specification for the plan's subject matter — the sections from `# 28.` on, as a
standalone document — is `dev/technical_designs/IRCITUILIB_TEST_STREAMING_SERVER_PROPOSAL.md`.
Read that for *what the system is meant to do*; read this appendix for *how it was to be built
and in what order*.

---

# The MSP streaming harness — a server daemon, a client program, and the buffered bar

## Context

`IRCITUILIB_VISUAL_EFFECTS_DESIGN.md` §28–§37 (lines 4261–6335) is a pasted specification for taking the
simulation *out* of the demo: a multi-threaded simulation daemon streaming metrics over a Unix domain socket,
and a separate client program that renders them. The demo (`src/examples/ui_main.c`, 130 KB, 22 key arms, six
templated panels) has become the place everything lands. This track builds the harness as its own pair of
programs so the demo stops being the only vehicle — and, in the same track, gives the progress bar the
**buffered track** §21 specifies, because none of the three channels §28 tabulates has a widget to land on
without it.

This plan was written against the tree at `b91eca9` / `IRCITUI_INTERNAL 49`. Everything in the tables below was
read or measured in this session; the two claims marked **verified** were proved by running something.

**Your four answers, and what each one settles:**

| | Your answer | What it fixes in this plan |
|---|---|---|
| Widgets | **Add a buffered track** | Gate 2 builds the second fill on the existing bar. No dial and no media bar are built — the CPU channel maps onto a bar, and the dial arrives whenever §24/§25 do, as a layout edit |
| Depth | **Everything, in stages, advanced last; get the core right** | Gates 0–4 are the core (protocol, server, buffered bar, client, label). Gates 5a–5d are §32–§35, staged, each gated on the previous |
| Where | **Reuse the demo's model** | The client links the *existing* `generated/ui_generated.c`, so there is no second generated artefact — which removes the symbol-collision risk entirely (see below). The client inflates one of the demo's panel templates as its whole UI |
| Demo | **Leave the demo alone** | `src/examples/ui_main.c` and `layout/ircitui.yaml` are not touched. `git diff --stat src/examples layout/ircitui.yaml` must be empty at every gate |

**The one cost your third answer buys, stated plainly.** Reusing the demo's model means reusing one of the
demo's *panels*. The only panel that is a metrics surface is `layout/progress_panel.yaml` — the bars gallery
raised on Ctrl-L. So **the harness client's dashboard is the bars gallery**, which has two consequences worth
knowing before Gate 1 starts:

- The buffered bar (Gate 2) is added to that gallery, so `layout/progress_panel.yaml` changes and
  `generated/ui_generated.c` is regenerated. `layout/ircitui.yaml`'s reference to it
  (`layout/ircitui.yaml:79`) already exists, so the demo's layout file does not change — but the demo's Ctrl-L
  panel gains a node.
- The gallery is a *gallery*, not a dashboard: ten bars of ten different styles. It drives the harness
  faithfully and it looks like what it is. If that reads wrong on the glass at Gate 3, the escape is a
  purpose-built `layout/harness_client.yaml` plus one generator run — one new pair, no change to the client's
  code beyond the template id it inflates. **Gate 3 is where that call is made, on the glass, not now.**

---

## The finding that shapes the protocol

**The framework's own network seam cannot carry a NUL byte.** Verified, verbatim:

- `IrcitUiNetworkListener` takes `const char *bytes` and **no length** —
  `include/ircituilib/ircit_ui_event_dispatcher_type.h:66-69`.
- `IrcitUiDispatcherPostNetworkChunk` copies `bytes_len` bytes and then writes
  `envelope.status_message[bytes_len] = '\0'` — `src/ui_event_dispatcher.c:219-220`.
- Thread 0 hands the listener `msg->status_message` — `src/main_looper.c:384-387`.

So the listener's only route to the chunk's length is `strlen()`. A 64-byte **binary** frame of `float` and
`int64_t` fields contains `0x00` essentially always — `0.0f` is four of them — so it would be truncated and the
fixed-size reassembler would desynchronise **permanently and silently**. §28's "fixed 64 bytes of packed binary"
cannot cross this tree's own stream seam as written.

**Recommendation: keep the 64-byte fixed frame and the channel semantics exactly, and change only the
encoding** — every byte in `[0x20,0x7E]`, `\r\n`-terminated, one frame length, no second parser state:

```
 MMSP │ cc │ pppppppp │ bbbbbbbb │  tttttttttt │ TTTTTTTTTT │ label × 20 │ CR LF
  4     2       8          8           10            10          20         2      = 64
 magic  chan  %8.5f    %8.5f    %10lld        %10lld      space-padded  framing
```

NUL-free by construction, so `strlen` is exact; fixed-width, so the parser is O(1) per frame; inspectable with
`socat` or `strace`; and it removes the binary float representation question from the wire while leaving the
*value-mapping* guard (Gate 0) fully necessary, because `nan` is a legal thing for `strtof` to produce.

**The alternative, and why it is refused.** A hand-rolled loop in the client that polls the socket itself and
calls `IrcitUiRenderFrame` directly would keep binary frames — `main_looper.c`'s `struct pollfd fds[2]`
(`:572-576`) is a hard-coded stack array with no application-fd hook, so the framework loop cannot take the
socket. Refused because it discards the framework's drain, its watchdog, its eventfd doorbell and the
watchdog-exempt reassembler contract that `ircit_ui_event_dispatcher_type.h:56-63` exists to provide — a
second loop, maintained forever, to save an encoding. **Gate 0 puts the measurement in front of you and this
fork is its authorisation item**, so the decision is made on a number rather than on an argument.

---

## The specification's defects, verified

Each of these is in the pasted text, not in the tree. The plan fixes them as it builds; the list is here so
nothing is fixed silently.

| # | §28–§35 says | Verified | Consequence |
|---|---|---|---|
| 1 | `base_sleep_ms * 1000000_long` (`:4813`, `:5142`); `104857600_long` (`:4427`, `:4853`) | **`clang -std=c17 -fsyntax-only` → `error: invalid suffix '_long' on integer constant`**; the control `1000000L` compiles clean | The pasted server **does not compile**. A draft artefact throughout |
| 2 | `#define MSP_CMD_TOKEN 0x434D444D // ASCII for 'MCMD'` | LE wire bytes `MDMC`, BE `CMDM` — matches `MCMD` under **neither**. It is `MCMD` rotated one position. `MSP_MAGIC_TOKEN 0x50534D4D` *is* `bswap32("MMSP")` | The two tokens use opposite conventions in one header. Harmless while both ends are this tree; wrong the moment a third party writes a client. The text framing resolves it: the magic becomes the characters it claims to be |
| 3 | `(MspFrame *)(read_buffer + offset)` then field reads | `#pragma pack(push,1)`, but every field lands at its natural offset anyway, and the static buffer is 16-aligned | **Not** a defect in practice. Pinned by a probe asserting `sizeof` and every field offset, so the layout cannot drift unnoticed |
| 4 | `send()` to a client fd made `O_NONBLOCK` (`:4911`); `sent < 0` checked only for `EPIPE`/`ECONNRESET` | A short `send` returns `0 < sent < 64` and is **ignored** | A partial frame permanently desynchronises the client. **The most serious defect in the spec** — and the framework's own post documents this exact hazard: *"a lost chunk desynchronises a reassembler permanently"* (`ui.h:4688-4692`) |
| 5 | `if (client_fd < 0) continue;` (`:4506`, `:4906`) | No `EINTR`/`EMFILE` distinction, no backoff | A persistent `accept` error is a **100 % CPU spin** |
| 6 | `while (g_SimRunning) { … }` then `thrd_join(t1,t2,t3)` (`:4530`) | `g_SimRunning` is never cleared and `accept` blocks | The teardown is **unreachable**. The daemon has no shutdown path; Ctrl-C is the only exit and it skips `unlink`, leaving a stale socket that makes the next `bind` fail |
| 7 | `if (s_Clients[i].fd >= 0)` (outer) then again inside the mutex (`:4322-4324`) | The outer read is **unlocked** while another thread writes the field | A data race. Benign in effect, UB in fact, and TSan will flag it |
| 8 | §33–§35 centre on `IRCITUI_F_PANEL_CRASH_OVERLAY` | The flags enum ends at `1u << 12` (`IRCITUI_F_COLLAPSIBLE`); the symbol appears **only in the design document** | The overlay mechanism needs a new **public** flag, or a different mechanism. Gate 5b's authorisation item |
| 9 | §31/§33/§34 call `IrcitUiColourPair(COLOR_RED, …)` | Declared in `src/include/ui_palette_priv.h:51` — **private, not installed**. Raw `COLOR_*` also regresses this tree's own nine-slot role rule | The client cannot call it and must not. Use `IRCITUI_SLOT_TEXT_ERROR` / `_ACCENT` |
| 10 | `IrcitUiFindNodeById(root, "youtube_bar")` | No such symbol; the function is `IrcitUiFind(node, id)` (`ui.h:1681`) | Mechanical |
| 11 | `IrcitUiMediaProgressBarSetPrefetch` / `…SetTime` / `IrcitUiDialGaugeSetProgress` / `IrcitUiProgressBarSetProgress` | **None exist.** No media bar, no dial, no prefetch field, no `…SetProgress` (the API is `IrcitUiBarSetValue`) | §28's channel table targets two widgets the tree does not have — your first answer, and Gate 2 |
| 12 | §35's `sDiagnosticsCollectionMapper` does `fopen`/`fprintf` in the timeout continuation (`:6151-6155`) | Continuations run **synchronously inline on the completing thread** (`ufsrv_future.h:6-7`); here that is the clock tick, on Thread 0 | A file write in the frame path. Gate 5d moves it off Thread 0 |
| 13 | §30 item 4 *redefines* `IrcitUiDispatchKey` | That is the framework's entire key dispatcher | Adopting it would delete key handling wholesale. §30's own "Framework vs. Host API Boundary" says the **host** intercepts keys — the section contradicts itself. The client's keys live in the client |
| 14 | §28's frame is packed binary | See the finding above | |
| 16 | §28 heads its layout table "Frame Layout (Fixed 64 Bytes)" and draws 4+4+8+8+8+8+24 = 64 | The struct printed underneath declares both metric fields as `float`: **4+4+4+4+8+8+24 = 56**. Measured (`p_nul frame`) | The section's diagram and its own code disagree by 8 bytes, and no reader can tell which was meant. The text framing this plan proposes is 64 bytes and so matches the diagram; the struct as written is 56 |
| 15 | `progress_panel.yaml:3` and its title label `:45` say "F12" | The binding is Ctrl-L, code 12; F12 is the framework's own exit | Pre-existing and already recorded. The client inherits this panel, so it is fixed in the Gate 2 regeneration |

---

## The stages

Every gate stops for debrief, carries its own evidence, and is all-or-nothing. `IRCITUI_INTERNAL` moves only on
**"wrap up"**. Anchored inflation and the gallery id table are Gate 3's; the numbered authorisation items are
the places this plan expects to come back to you.

### GATE 0 — the ground truth. No product code. **DONE — green, 2026-09-20.**

`dev/probes/harness/` — `run_probes.sh` plus `p_nul.c` (5 cases), `p_cast.c` (2), `p_cap.c` (4),
and `README.md`. All 11 cases pass; the five pre-existing probe sets re-run green. What it found,
and what each finding changed:

| Probe | Measured | What it changed |
|---|---|---|
| `p_nul seam` | 11 posted, 11 seen | the control: the seam is not lossy in general |
| `p_nul nul` / `ambig` | 5 posted → **2** seen; a 7-byte chunk and an 8-byte chunk ending in NUL both seen as 7 | the NUL finding, and proof that no "scan for the terminator" workaround exists |
| `p_nul frame` | §28's struct: 56 posted, **5** seen | the binary framing is unusable **and** the section's own diagram (64) disagrees with its struct (56) |
| `p_nul text` | 64 posted, **64** seen | the proposed NUL-free framing survives the seam intact |
| `p_cast unguarded` / `guarded` | `runtime error: nan is outside the range of representable values of type 'int'` | `float-cast-overflow` **is** in this build's UBSan group; the guard is load-bearing, and 1001 sweep points produce no diagnostic |
| `p_cap float` / `anchored` | both capped (34×18 and 33×17); the tail row's ink is unchanged with the panel raised | **this plan's own Gate 3 premise was wrong** — anchoring does not escape the cap |
| `p_cap relaid` | 40×30; the tail row goes 3 → 36 ink | the re-layout workaround works |

- **Authorisation item, now with the numbers in front of you.** The wire-encoding fork: NUL-free
  64-byte frames over the framework's own seam (recommended — measured working), or binary frames
  over a hand-rolled client loop that abandons `IrcitUiMainLooper`.
- **Back-out:** none needed. Probes only; no product file was created.

### GATE 1 — the protocol and the server. **DONE — green, 2026-09-20.**

`harness/msp_protocol.h` (the wire contract, shared), `harness/msp_server.c` (the daemon),
`CMakeLists.txt` (`IRCITUI_BUILD_HARNESS`, default OFF, plus the one `msp_server` target),
`dev/probes/harness/p_wire.c` and `dev/probes/harness/server_cases.py`.

The server links **nothing from this project** — `nm -u` shows no UI symbol, and the only link
entries are `Threads::Threads` and `m`. It is reachable through a *new* build directory
(`build/harness`, configured `-DIRCITUI_BUILD_HARNESS=ON`), so `build/dev` and `build/dev-test`
are untouched and still show the option as `OFF`.

All six defects the gate planned to fix are fixed, plus one the probe found and one the wire
format needed:

| | |
|---|---|
| §28's short `send` leaving half a frame | every client fd carries `SO_SNDTIMEO`; `s_send_all` sends the whole frame or **closes the client**. A live client is only ever sent whole frames |
| §28's `accept` spin | `EINTR` retries, anything else backs off to a 100 ms ceiling |
| §28's unreachable `thrd_join` | `SIGINT`/`SIGTERM` clear the flag and `shutdown()` the listening fd, so the joins are reached and the path is `unlink`ed |
| §28's unlocked `fd` read | the lock is taken before the fd is read, in both the broadcast and the new reader |
| §28's blind `unlink` | `stat` first; only a socket is removed |
| §28's shared `rand()` | a per-thread xorshift |
| **found by `p_wire`** | `MSP_RX_CAP` was 4 frames (256 B) but the transport's largest chunk is 511 B = 7 frames — a seven-frame burst was refused outright and every frame in it lost. Now sized from `residue + 511` |
| **found by `p_wire`** | the builder left a `0x00` at offset 18 of **every** command frame (`snprintf`'s terminator, uncovered by the next field), so the parser refused every frame the builder wrote. Now a `MspScrub` pass makes NUL-freedom the builder's guarantee rather than a property of field ordering |
| **found by a live run** | `MspLabelPack` read past the label's NUL and put uninitialised stack on the wire — an empty label arrived as a fragment of a libc string |
| **found by a live run** | `%2d` space-pads, so the channel field was `" 1"` and the digit-only parser refused it. The channel and command fields are now `%02d` |

**Evidence.** `dev/probes/harness/run_probes.sh` — 11 Gate-0 cases, 5 `p_wire` cases (295
checks) and 23 `server_cases` cases, all passing; the server cases run against an
ASan+UBSan build. `server_cases` measures live: all three channels streaming in cadence order,
every frame 64 bytes and CR-LF terminated and NUL-free, the control path (`[PAUSED]` appears,
the clock resumes, 4× **more than doubles** the cadence, `[ERROR]` on both channels), five
hostile control frames with no desync, the ninth client refused, a slow reader dropped rather
than stalling a thread, SIGTERM reaching the joins with the socket removed, and a stale socket
from a `SIGKILL`ed server not blocking the next bind. The five pre-existing probe sets re-run
green; `ctest` is 5/6 on the same pre-existing `dirty_tests` leak; the generator is still
byte-identical.

**Back-out:** the server is a leaf — two files, one CMake block, one build directory.



### GATE 2 — the buffered track

The one widget feature this track adds, and the reason §28's media channel has anywhere to land.

- `src/include/ircitui_bar_state_type_priv.h`: a buffered value plus an explicit *is-set* flag — the state
  header's own rule is that **0 is a real value**, so a bare `0` cannot mean "absent".
- `src/ui_engine.c`: `IrcitUiBarSetBuffered` / `IrcitUiBarBuffered` beside `IrcitUiBarSetValue`
  (`:1042-1067`), same clamp discipline; `sBarSplit` and `sBarPaint` (`:3564-3640`) gain the middle run.
- **No grammar change and no new declarative key.** A buffered level is a *live value*, which cannot be written
  in a file — `IRCITUILIB_FRAMEWORK_API_GUIDE.md` §11.1's own test. So there is no `C25`, no `KNOWN` entry in `validate_layout.py`, no `gen_ui.py`
  emit path and no new verifier rule. The buffered run reuses `fill_glyph` at a dimmed attribute, so it needs no
  new glyph key either.
- `layout/progress_panel.yaml`: one node demonstrating it, and defect 15's stale "F12" comment and label fixed
  in the same edit. Then `python3 tools/gen_ui.py layout/progress_panel.yaml`'s root and the verifier, and
  `generated/ui_generated.c` regenerated once.

- **Authorisation item.** The buffered run's *style*. No new palette slot (that ruling stands), so the proposal
  is the fill glyph at `IRCITUI_SLOT_ACCENT | A_DIM` against the played fill at `IRCITUI_SLOT_ACCENT`. If that
  does not separate on the glass, the alternatives are the empty glyph at accent, or reversing which run is
  dim.
- **Authorisation item.** The new gallery node will read 0 % buffered under the demo's Ctrl-L, because
  `sBarTargets` (`src/examples/ui_main.c:729`) is where the demo posts gallery values and you have ruled that
  file untouched. The harness client drives it instead. If you would rather Ctrl-L showed it moving, that is one
  line in `sBarTargets` — your call at this gate.
- **Evidence.** A probe reading cells for the three runs *and their attributes* (the `mvwin_wch` + `getcchar`
  path, with real `attrs`/`color_pair` — the trap that cost an earlier track an afternoon); the adversarial
  cases — buffered behind filled, buffered past the track, buffered with no fill, buffered set then cleared,
  buffered on a bar whose label is adjacent (the label narrows the track: `sBarTrackW`, `:3379`).
- **Back-out:** additive and local. The setter and the state field revert by deletion; the gallery node is one
  YAML entry and one regeneration.

### GATE 3 — the client

`harness/client/harness_client.c`, the new `ircitui_harness_client` target.

| | |
|---|---|
| UI | **Inflate, then re-lay out at the client's size.** `IrcitUiInflateFloat(root, "progress_panel")`, then `IrcitUiLayout(panel, x, y, w, h)` + `IrcitUiRealize(panel)`. **Gate 0 falsified this plan's original premise here:** it said "anchored, not floating, to escape the 34×18 cap", but `p_cap` measured that *all three* inflate routes funnel through `sInflateAt` → `sPopupSize` and are capped — the anchored route included (33×17). The re-layout is a **workaround for §9.35**, named as one. A genuine public-API consumer: the target gets `include/` through `ircituilib` and **deliberately no `src/include`**, the boundary `ircitui_window_demo` holds |
| The socket | A reader **pthread** owns the fd's read side, exactly the demo's shape — `sNetWorker` (`src/examples/ui_main.c:2569-2593`) reads into `PRIV_CONFIG_DEFAULT_CLIPBUF_SZ - 1` and posts with `IrcitUiDispatcherPostNetworkChunk(disp, token, buf, n, 200)`; a zero from the post ends the stream rather than desyncing it |
| The reassembler | Thread 0's registered listener, the demo's `sNetOnChunk` role (`:2521-2560`). Durable `(frame, len)` across calls — safe precisely because the network listener is the one callback that runs **without the watchdog** (`ircit_ui_event_dispatcher_type.h:56-63`). Bound is one frame length, not the demo's 4096 line buffer. Bad magic resyncs **one byte at a time** and counts, because a length taken from a corrupt header is not trustworthy |
| The mapping | A channel → node-id table, looked up with `IrcitUiFind` (`ui.h:1681`). **Ids only, never node pointers**: `IrcitUiMainLooper` calls `IrcitUiScreenReset()` at `src/main_looper.c:578` before its first frame, which re-clones the tree, so anything captured at bootstrap is stale by frame one |
| The value mapping | `isfinite` first → clamp to `[0,1]` → `(int)(v * 100.0f + 0.5f)`. `IrcitUiBarSetValue` defends itself completely **for int inputs** (`:1042-1067`) and cannot defend the float→int conversion, which happens in the caller. Round-half-up, not truncation: truncation makes 0.997 render as 99 and never reach 100 |
| The seek bar | Bound **from inside the loop**, not at bootstrap — `IrcitUiBindSeek` resolves the id against `IrcitUiScreen.root` and returns 0 while that is NULL (`src/ui_engine.c:1191-1212`), and the reset happens inside the loop. The demo binds after raising its float for exactly this reason (`:800-803`) |
| Shutdown | The demo's `sNetStop` order (`:2660-2680`), which its own comment calls "the order is the contract": `shutdown(fd, SHUT_RDWR)` → `stop = 1` → `pthread_join` → `close` → clear the listener **last**. Then `IrcitUiDestroyFrameworkContext`, the framework's own documented order (`ui.h:4695-4706`) |
| Duplex (§30) | Control frames written from **Thread 0**, gated on `poll(POLLOUT, 0)` so a full send buffer can never block the drain — the listener warning is explicit that blocking here wedges the whole application. `SIGPIPE` ignored at bootstrap **and** `MSG_NOSIGNAL` on the call: without both, a server that has gone away kills the client with no diagnostic. No `O_NONBLOCK` on the fd, which would turn the reader's blocking `recv` into a busy loop |

- **Authorisation item.** Control-frame rate. `IrcitUiSeekCallback` fires per motion column, and DECSET 1003
  reports motion continuously, so a frame per event is a `poll` + `send` on Thread 0 per mouse report. Proposal:
  coalesce to at most one control frame per 100 ms, carrying the latest target. If you would rather not have a
  keyboard back-channel at all, this gate drops §30 and the client is read-only — say so and the plan loses one
  section, not one gate.
- **Evidence.** A pty run of the client against the real server, read off the reconstructed screen: bars move,
  the label row follows, a control key changes the server's cadence. Then the protocol's adversarial cases
  driven through the socket: a chunk that splits a frame, a 1-byte chunk, a bad magic and a resync count, an
  out-of-range channel — the one input that would be memory-unsafe rather than merely wrong — `NaN`/`Inf`
  fields, `time_total == 0`, a partial frame at EOF, a burst of frames in one chunk (at most 7 fit in 511
  bytes, and the post refuses an oversized chunk rather than truncating it: `ui_event_dispatcher.c:212-216`).
- **Back-out:** the client is a new program. Delete the target and the file; the demo and the library are
  untouched by it.

### GATE 4 — the label

§31's text arena, reduced to a **20-column label inside the same 64-byte frame** — one frame length, one parser
state. §31's 88-byte arena does not fit in 64 and a second frame kind would double the reassembler's state for
no benefit. The label drives a `text_view` in the gallery; the styles come from the nine slots, not from
`IrcitUiColourPair` (defect 9). The label is masked to `[0x20,0x7E]` server-side, because a stray `0x00` in a
label is the one way the text framing can still be defeated.

**Evidence:** a label with exactly 20 characters, one with 19 and one with 21; a label containing a byte outside
the range; a label that is all spaces (present, not empty); and a frame whose label field is not
space-terminated.

### GATE 5 — the futures layer, in four sub-stages

§32–§35, deliberately last, each gated on the one before. This tree would be the **first application on this
machine ever to call `UfsrvFutureZip`, `UfsrvFutureAny`, `UfsrvFutureMap` or `UfsrvFutureFlatMap`** — their only
executable callers anywhere are `ufsrvrxlib`'s own gtest (`/home/devops/repos/1.ufsrv/ufsrvrxlib/tests/ufsrv_future/ufsrv_future_tests.cpp`).
ircituilib today uses exactly five entry points: `Create`, `Then`, `Release`, `SetValue`, `SetError`. Each
sub-stage opens with a probe that exercises the operator on its own before any UI is wired to it.

| | | |
|---|---|---|
| **5a** | the label's expiry (§32) | A promise resolved by the clock tick, its continuation clearing the label. **The continuation must run on Thread 0** — it runs on *whichever thread completes the promise*, and nothing may touch the tree from anywhere else. §32's `log_expiration_ticks` counter stays, but the promise is the mechanism |
| **5b** | the join-barrier (§33) | `UfsrvFutureZip` over two channel-recovery promises. **Authorisation item:** §33's crash overlay needs a public flag the tree does not have. Recommendation — **do not add one.** A stacked overlay node in the client's own tree is this tree's existing idiom (the snackbar, `IrcitUiShowContextMenu`) and needs no framework change at all |
| **5c** | the race timeout (§34) | `UfsrvFutureMap` (ticks → ETIME) raced by `UfsrvFutureAny` against the zipped barrier |
| **5d** | the flat-map chain and manual reset (§35) | `UfsrvFutureFlatMap`; the diagnostics write moves off Thread 0 (defect 12); `R` re-issues the reset control frame. §35's `sIrcitUiGlobalAlertClearContinuation` and `ui_flatmap.c`'s `extern IrcitUiNode *s_GlobalTelemetryNode` against a `static` definition elsewhere is a linkage contradiction in the spec — here the continuation's context is owned by whoever allocated it, and freed exactly once |

### GATE 6 — the record

- `IRCITUILIB_VISUAL_EFFECTS_DESIGN.md` — a new appendix in Appendix F's shape: a section-by-section
  disposition of `# 28.`–`# 37.`, what is refused and named as refused, what was found on the way, and the
  evidence. The pasted sections **stay exactly as pasted** — the document says so at `:8-11` — and its Contents
  block does not list §20–§37, so it is not extended for them. A `### Delivered` row and the baseline moves
  (nodes/templates, `PUBLIC_API` count) go in beside the existing ones.
- `dev/probes/harness/README.md` — what each case asserts, in the convention `border`, `palette` and `snackbar`
  follow.
- `harness/README.md` — how to build and run the pair, and the protocol's field table.
- **No Version Management block is added.** This repo's design docs carry none and say so; the grep for the
  phrase returns eight disclaimers and no blocks.

---

## Files

| | |
|---|---|
| **New** | `harness/msp_protocol.h` · `harness/msp_server.c` · `harness/client/harness_client.c` · `harness/README.md` · `dev/probes/harness/` (`run_probes.sh` + `p_nul.c`, `p_cast.c`, `p_cap.c`, `p_wire.c`, `p_buffered.c`, `README.md`) |
| **Edited** | `CMakeLists.txt` (one new option, two targets) · `src/include/ircitui_bar_state_type_priv.h` · `src/ui_engine.c` · `include/ircituilib/ui.h` · `layout/progress_panel.yaml` · `IRCITUILIB_VISUAL_EFFECTS_DESIGN.md` |
| **Regenerated, never edited** | `generated/ui_generated.c` — exactly once, at Gate 2, via `tools/gen_ui.py`, with the verifier's verdict as its evidence |
| **Untouched, and diffed to prove it** | `src/examples/ui_main.c` · `layout/ircitui.yaml` · `generated/*.c` other than the one regeneration · `tests/fixtures/` (archival, read-only) · every other `layout/*.yaml` |

New CMake option `IRCITUI_BUILD_HARNESS`, default **OFF** and not nested inside `IRCITUI_BUILD_EXAMPLE` — an
existing `cmake -S . -B build` must be unchanged, and the harness must build with the demo unbuilt, which is
part of what "the demo is untouched" means. The server target gets no `target_link_libraries` at all; the
client gets `ircituilib` and no `src/include`.

---

## Verification

```sh
# Gate 0 onward, every gate
bash dev/probes/harness/run_probes.sh            # new: the protocol, the server, the bar
bash dev/probes/palette/run_probes.sh            # must stay green
bash dev/probes/border/run_probes.sh             # must stay green
bash dev/probes/progress/run_probes.sh           # must stay green
bash dev/probes/snackbar/run_probes.sh           # must stay green
bash dev/probes/predict/run_probes.sh            # must stay green

# Gate 2 only, in this order
python3 tools/gen_ui.py layout/ircitui.yaml /tmp/regen.c && diff -q /tmp/regen.c generated/ui_generated.c
                                                 # BEFORE the edit: determinism, so the diff below is exactly the change
python3 tools/gen_ui.py --validate-layout layout/ircitui.yaml generated/ui_generated.c
python3 tools/gen_ui.py layout/ircitui.yaml generated/ui_generated.c   # regenerate, once
python3 tools/post_generation_verifier.py layout/ircitui.yaml generated/ui_generated.c
bash tests/run_generator_test.sh

cmake --build build/dev-test -j"$(nproc)" && ctest --test-dir build/dev-test --output-on-failure
cmake --build build/dev      -j"$(nproc)"        # both dirs, every gate

# the boundary claims, as commands rather than assurances
git diff --stat src/examples layout/ircitui.yaml          # must be empty at every gate
clang -std=c17 -Wall -Wextra -Werror -fsyntax-only -I include dev/probes/harness/<each>.c
TSAN_OPTIONS= ./build/tsan/msp_server                      # the broadcast path, once at Gate 1

# end to end, both programs
./build/dev/msp_server &
TERM=xterm-256color script -q -c "stty rows 40 cols 120; ./build/dev/ircitui_harness_client" /dev/null
```

**What `ctest` cannot see, and must not be read as passing.** The fake shim's `box()` is a no-op and
`mvwadd_wch`/`mvwaddwstr` are stubs (`tests/fake_ncurses/ncurses.h:154-159`), and `--verify-layout` synthesises
geometry and reads no cell. The suite is structurally blind to every cell this track writes. There is also
**no test anywhere covering the bar widget today** — the probes and your terminal are the whole of the evidence.

Stated limits, honestly: probes are not built by CMake and are not wired into CI, so this harness's evidence
runs when a person runs it. A reconstructed screen is a model, not ground truth; the pty run at Gate 3 is the
weakest link in the chain and is named as such rather than presented as proof.

---

## Left open, deliberately

| | |
|---|---|
| The dial and the media bar | Not built. The CPU channel maps onto a bar and the media channel onto a played bar plus the Gate 2 buffered run. When §21/§24/§25 land, the channel table changes — a layout edit, not a framework change |
| A purpose-built client layout | Deferred to Gate 3's on-the-glass call, and priced there: one YAML plus one generator run |
| `dev/probes/harness` in CI | The test-strategy document's §13 already names wiring the reusable probes into a build target as the obvious next step. Not this track's scope, and not silently done here |
| The 34×18 float cap (§9.35) | A pre-existing defect, not this track's to fix. The client routes around it by inflating anchored; the gallery under Ctrl-L still clips its lower bars, unchanged |

---

## Standing constraints, unchanged

Commit by explicit pathspec, never `git add -A`; no pushes without their own authorisation; no fetch, no rebase,
no `git reset --hard`, no `git clean`; `IRCITUI_INTERNAL` only on **"wrap up"**; `_generated` files are
regenerated, never edited; `clang`, not `gcc`; `-fsyntax-only` as intermediate QA; `build/dev-test` for tests
and **rebuild both build directories**; the shell is `zsh` and does not word-split an unquoted variable;
scratch probes under a `timeout`, named literally rather than by wildcard.

---

*End of Appendix A.*
