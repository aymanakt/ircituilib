# IRCITUI demo — controls and what they produce

Every control the demo currently answers, and the effect each one has on the glass. Written
against `src/examples/ui_main.c`; where a control is known not to work, it says so rather
than describing the intent.

## Running it

```sh
cmake --build build/dev -j"$(nproc)"
./build/dev/ircitui_demo
```

**Run it from the repository root.** The predictive gallery reads
`assets/predictive_words.txt` by a relative path, and a build directory is not where that file
is; started from elsewhere the field still predicts, from the three sources that do not need a
file, and the readout says `file=0`.

It needs a **real terminal** — the library writes to ncurses' windows, and the test suite's
fake ncurses backend is a no-op, so nothing here is visible under `ctest`. The binary in
`build/dev` is the one to run; `build/dev-test` is the same source with sanitizers on, which
is useful when something crashes and noisy when nothing does.

Options: `--log-retention`, `--verify-layout` (prints the node tree and the paint grid and
exits without a terminal).

## A note on what a "key" is here

The arms in the demo's listener are **numeric key codes**, not the characters on your
keyboard. `case 7` is **Ctrl-G**, not the `7` key — and pressing `7` will not reach the
listener at all, because a printable character is handed to whatever edit field has focus
before the listener is consulted. `q` cannot quit for that reason. The same applies to every
number in the table below: they are all control keys.

`F12` is the **framework loop's** exit, not the demo's, and `F11` is eaten by most terminals
before ncurses sees it.

## Keyboard

### Session

| Key | Code | What it produces |
|---|---|---|
| **F10** | — | exits the loop and tears the application down |
| **F12** | — | the framework's own exit (see below) |
| `q` | — | **nothing.** A printable character goes to the focused edit field first |

### Asynchronous operations

| Key | Code | What it produces |
|---|---|---|
| **F2** | — | starts a fetch on the worker pool that **fails**. Thread 0 raises the framework's alert window when the failure arrives through the pipeline. Nothing in the demo calls the alert API |
| **F3** | — | starts a fetch that **succeeds** after three stages, and Thread 0 writes the result into the focused field |
| **Ctrl-D** | 4 | downloads a real PNG over the network and raises a floating window showing it |

F2 and F3 are the pair worth watching: both run a coroutine on the worker pool, both carry
their outcome back on a future, and the only difference is how they end.

### Panels and layout

| Key | Code | What it produces |
|---|---|---|
| **F4** | — | folds or unfolds **`side_panel`** — the right-hand rail, the one the body's split puts beside the conversation. The programmatic half of the collapse feature; clicking the indicator glyph is the other. The demo's other two collapsible containers, `side_panel_left` and `toolbar`, are gesture-only: no key is bound to them |
| **F5** | — | builds a floating window from the C API, by hand, showing the non-layout path |
| **F7** | — | raises or drops the **Tools** panel (a templated float) |
| **F8** | — | raises or drops the **File** menu, anchored under its button |
| **F9** | — | flips the demo's verdict on the **Tools** button between "allowed" and "denied". The button, the click and the panel are unchanged; only the application's answer is |
| **Ctrl-L** | 12 | raises or drops the **bars gallery** — see below |
| **Ctrl-A** | 1 | raises or drops the **predictive gallery** — see below. Also on the **Predict** header button |
| **Ctrl-R** | 18 | with the predictive gallery up: does what the **Learn** button does — teaches the field its own word — and then recomputes the readouts. With the panel down, says so in a notification |
| **Ctrl-F** | 6 | with the predictive gallery up: shows or hides the **suggestion list**. The list is walked with **↑/↓** — **←/→** belong to the field's caret |
| **F1** / **F6** | — | fills **`canvas`** with rows and turns scrolling on it — see *The screen* below |

**The screen.** The body is a frame holding one horizontal linear, `main_split`, and its four panes
are what that linear packs by, each on its own share: `canvas` takes `match_parent` — the remainder,
15 of the split's 57 columns at 80×24 — beside `log_list`, `chat_window` and `side_panel` at 25%
each (14 apiece). That is as equal as four panes get: there is no "equal" mode, a child is matched,
wrapped, fixed or a percentage, and a percentage is taken against the split rather than the screen.
Outside the body, `side_panel_left` runs the full height on the far left and `toolbar` sits above the
footer. Four things a reader will otherwise read as bugs:

- **Only one child of a linear may be on `match_parent`.** The engine hands *every* `match_parent`
  child the whole remainder and then advances past it, so a second one lands beyond the linear's right
  edge — measured once as a 1×13 node at x=80 that painted nothing.
- **Every pane is narrow at 80 columns** — 14 cells, so log rows and fox lines clip and a conversation
  bubble wraps at about 10. Four panes read properly from 160×48, where they are 29–30 cells each.
- **`canvas` is the pane that grows.** Folding a rail hands it the freed columns (15 → 28), because it
  is the remainder. Give the remainder to a different pane, or to none, and a fold leaves its columns
  unclaimed — and its old pixels still on screen, since no window owns those cells to repaint them.
- **`gravity: center` on `canvas` does nothing.** The key reaches the model, but `common.gravity` is
  read nowhere in the engine, so the label paints at the pane's origin — which is where it has always
  painted.

### Visual effects

| Key | Code | What it produces |
|---|---|---|
| **Ctrl-B** | 2 | cycles the **frame profile**: standard (the legacy ACS box) → rounded → thick → double. Every frame in the tree and the notification box change together; the current profile is named in the footer |
| **Ctrl-P** | 16 | toggles the **drop shadow** under stacked popups. Drag a panel with it on to see the shadow follow |
| **Ctrl-G** | 7 | cycles **palette effects**: off → gradient → canopy → canopy + pulse. These are process-wide, and `canopy` borrows two of the four colour ramps |
| **Ctrl-T** | 20 | cycles the **palette profile**: dark → warm-ish → light → high contrast |
| **Ctrl-Y** | 25 | **dissolves** the notification box. With no box up it raises one first |
| **Ctrl-N** | 14 | raises a **notification** (the snackbar — a `WINDOW`, not a node: the layout does not move to make room, and it takes no focus) |
| **Ctrl-O** | 15 | deletes the cursor row in the list and offers **`[UNDO]`** as a notification action |

## Mouse

| Gesture | What it produces |
|---|---|
| Click a header button | activates it — **File**, **😀**, **Tools**, **Bars**, each raising its own layer |
| Click the **Tools** button after F9 | the application's registered handler **declines**, and nothing else happens: this is the point of the demo |
| Drag a panel by its frame | moves it. Popups are draggable; the demo's own windows are not |
| Click the collapse indicator | folds or unfolds that container. All three are on screen: the left and right rails (folding on width — their glyphs are the mirrors `◁` / `▷`) and the toolbar above the footer (folding on height — `▽` / `△`) |
| Click a list row | marks it |
| Click a **bar** in the gallery | **currently produces nothing** — see the gallery section |
| Click **Learn** / **Report** / **Close** on the predictive panel | all three work — Learn teaches the field its word, Report recomputes the readouts, Close drops the panel |

## The bars gallery (Ctrl-L)

Raises a floating panel of progress bars and starts an asynchronous run that fills them.

**What is on it** — each node exists to exercise a different path, so the panel is coverage
rather than decoration:

| Node | What it shows |
|---|---|
| `bar_plain` | the defaults: `█` filled, `░` empty |
| `bar_glyphs` | fill and empty glyphs **from the layout** (`fill_glyph`, `empty_glyph`) |
| `bar_bordered` | a frame, which narrows the paintable track by two columns |
| `bar_padded` | padding, which narrows it further |
| `bar_accent` | wearing the ninth palette role, `accent` |
| `bar_wide` | a second width, so the fill's rounding can be compared side by side |
| `bar_adjacent` | `label_mode: adjacent` — the percentage sits beside the bar, **which makes the bar narrower** |
| `bar_center` | `label_mode: centered` — the percentage sits over the bar, which keeps its width and is masked per cell |
| `bar_grad` | a gradient, set from C rather than from the layout |

**The run.** Opening the panel submits a coroutine to the worker pool. It yields twenty
times, and at each stage posts a percentage for every bar through the lock-free ring; Thread
0 applies them on its next pass. The worker holds only an id string and an integer — it never
touches the tree. The bars fill over about two seconds with no further input, and stop.

**Seeking.** Dragging or clicking a bar is meant to report the position to the application,
which sets the value back — the host owns the value and the bar only reports.

> **Known not to work.** With the panel raised, clicking or dragging a bar produces no
> visible change, and the arrow keys produce none either. One cause was found and fixed (a
> click delivers no `BUTTON1_PRESSED`, so the arm missed and the gesture dragged the panel);
> the symptom survives that fix and the cause is **not yet established**. The bars otherwise
> fill correctly.
>
> To try it: raise the panel with `Ctrl-L`, click a bar, then press `←` / `→`. If it worked,
> the footer would name the phase — `seek bar_plain: DRAGGING (preview) raw=0.318 -> api
> IrcitUiBarSetValue=32`, switching to `RELEASED (commit)` on release or on an arrow key.

**Reading the footer.** The status line is the demo's diagnostic surface, and it prints the
**public API's** view next to the **framework's internals**, on purpose — so the split
between the two is visible rather than asserted. On the gallery it reads:

```
api bar_wide=0/100 grad req=1 live=0 | internal widget_state->value=0
```

`api` is `IrcitUiBarValue`, which is public. `internal` reads the node's `widget_state`
directly, which is not — the demo reaches a private header for it, as it does in five other
places, each with its reason stated. `MISMATCH` appears if the two ever disagree.

`grad req` is whether a gradient was **asked for**; `grad live` is whether one is actually
being drawn. They differ when the process is out of colour ramps — four exist, and the
canopy effect holds two — and the pair exists because a bar drawing flat because nothing
asked, and a bar drawing flat because there was no ramp, look identical on the glass.

## The predictive field (Ctrl-A)

A field that suggests the rest of the word as you type. Press **Ctrl-A**, or click the
**Predict** header button, and a panel comes up with the field already focused — type straight
into it.

**It is also on the command line**, at the bottom of the screen, without opening anything: the
`> ` field there carries the same dictionary, and it is focused when the demo starts. That is
the quickest way to see it.

### What you see

Type `app`. The field shows `app` in white, then a blank cell where the caret is, then **`le`**
in a dimmer colour. The `le` is not in the field — it is the rest of `apple`, drawn where the
text *would* go, and it is a suggestion rather than content. `Ctrl-V`, the clipboard, and
`Enter` all see `app` and nothing more.

| Type | What happens |
|---|---|
| `app` | hint `le` — the best completion of the word under the caret |
| `l` then `e` | the hint shrinks to `e`, then disappears: a word equal to what you typed is not a completion |
| Backspace | the hint comes back |
| `x` | the hint goes instantly — nothing completes `applx` |
| **Tab** | **accepts**: the field becomes `apple` and the caret moves past it |
| **Esc** | declines the hint and leaves `app` alone. The next keystroke brings it back |
| `←` `→` | moving the caret into the middle of a word drops the hint: there is nothing sensible to complete from the middle |

The **token** is the word under the caret, not the whole field. Type `status app` and the hint
is still for `app`.

### The four dictionaries

The field holds **one** dictionary. It is a composite of four, asked in this order, and the
first that has an answer wins:

| Order | Source | Where it comes from |
|---|---|---|
| 1 | **learned** | words you taught it in this session — see Ctrl-R below |
| 2 | **emoji** | a fixed table of `:tag:` shortcodes |
| 3 | **file** | `assets/predictive_words.txt`, read at startup |
| 4 | **embedded** | an array compiled into the demo |

The order is visible rather than asserted. Type `pre`: the **file** answers, with
`precedence` — not the embedded list's `predictive`, because the file is asked first.

### Emoji: a suggestion that writes something else

Type `:fir`. The hint is `:fire:` — you read the shortcode. **Tab** writes 🔥. This is the one
place where what an accept inserts is not what the hint showed, and it is the dictionary that
decides it, not the field.

### Teaching it — Ctrl-R

The **learned** source is empty when the demo starts and is asked *first*, so anything you put
in it beats every list. Press **Ctrl-R** with the panel up and:

- it teaches the learned source **the word under the caret** in the panel's field, and
- it recomputes the four readouts.

So: type `prediction`, **Ctrl-R** (the footer says `learned "prediction": 11 nodes`), clear the
field, type `pre` — and the hint is now `prediction` rather than the file's `precedence`. The
word you taught is winning.

Pressing Ctrl-R twice on the same word makes it heavier still. The weight is a tie-break, and
it saturates rather than wrapping, so a word typed a million times cannot roll over and lose to
one typed twice.

### The suggestion list

The field can list its candidates rather than only hinting at one. Turn it on with **Ctrl-F**
while the panel is up: type `app` and a list appears under the field with the completions in it.

| Key | What it does |
|---|---|
| **↑** **↓** | walk the list. **The dimmed hint follows the highlighted row** — they are one piece of state with two views, so they cannot disagree about what Tab would accept |
| **Tab** | accepts whichever row is highlighted |
| **Esc** | closes the list with the top layer |

Three things about it are worth knowing because they are design rather than accident:

- **It does not take focus.** The list is a list; the field keeps the keyboard. That is forced:
  the loop's edit fast path only fires for a *focused* edit field, so a list that took focus
  would stop the field accepting characters.
- **It needs more than one candidate.** One candidate is what the inline hint already shows, and
  a one-row list saying the same thing is noise.
- **It shows only on the panel's field**, not on the command line — a popup following you around
  the main screen is a behaviour nobody asked for.

It inherits the frame profile (**Ctrl-B**), the drop shadow (**Ctrl-P**) and the theme
(**Ctrl-T**) from the frames track, because it is an ordinary container of rows rather than a
hand-painted window.

### Reading the panel

| Line | What it is |
|---|---|
| `api hint=... dict=...` | **the public API's** answer: `IrcitUiEditTextHint` and `IrcitUiEditTextGetDictionary`, both in `ui.h` |
| `internal hint_len=... token_at=... caret=...` | **the framework's** own state, read through a private header. `MISMATCH` appears if the two ever disagree |
| `sources learned=N emoji=9 file=N (refused N) embedded=N` | what each source holds. `refused` counts lines the word file gave up on |
| `learned trie=N nodes, weight-driven: ...` | the learned trie's size, and what it is offering now |

The first two lines exist to be compared: they are the API's view and the framework's of the
same fact, side by side, rather than one asserted to match the other.

> **The two lines do not refresh themselves.** A printable character typed into a focused field
> is taken by the loop's edit fast path and never reaches an application listener, so the demo
> is never told about the keystroke that changed the hint. **Ctrl-R** is how you ask. It is not
> an oversight — it is what the framework's input path does, and the demo is built around it.

### The buttons, and a correction

**Both buttons work.** An earlier version of this manual claimed they did not, and that claim
was wrong: it came from clicking the wrong rows. The panel's layout had been read off the glass
with a hand-rolled terminal parser, the parser was off by two rows, and the clicks landed on the
readout lines *above* the buttons. When the diagnostic was finally put where the engine resolves
a click rather than where a scraper guesses it, the buttons resolved exactly as they should, and
the handler behind `Learn` was observed learning the word and reporting `11 nodes`.

The lesson is worth the paragraph it costs, because this repository has now paid for it twice:
**read the engine's view of an event, not a screen scraper's.** `dev/probes/border/README.md`
records the same shape of mistake with a broken cell reader.

**What is genuinely unresolved:** whether the two readout lines repaint after a *click*. On the
Ctrl-R path they demonstrably do. On the click path the action happens and the notification
confirms it, but a screen scraper cannot settle whether the readout line also updated — ncurses
emits only changed cells, so a row that changed in place does not come back as a contiguous
string. Pressing **Ctrl-R** afterwards is the certain way to have both the action and the
readout, which is why it exists.

## The conversation demo (`ircitui_conversation_demo`)

A second binary, and a second application. It loads
`assets/conversation/conversation_golden_test.json` — the frozen golden input — injects its
nine messages through the conversation widget's public seam, and renders them. It is the
widget's **reference**, not a toy: its four dumps are frozen files the probe runner diffs
byte-for-byte.

**It owns its tree.** Until this pass it called `IrcitUiScreenReset()` and read `chat_window`
out of `layout/ircitui.yaml` — the tree `ircitui_demo` owns — so it inherited that demo's
header with seven menu buttons, a scrollable canopy body and a footer, and used one node of
it. It now supplies its own model through the binder: a command strip, a framed main window
that takes the remainder, and an input line.

**Nothing in it names a cell count.** The root is a vertical linear and the main window takes
`match_parent` height, which in a vertical linear is the remainder — so it grows with the
terminal and the strip and input line do not. `IrcitUiWidgetDef` carries no edge fields, so a
C-built tree cannot pin siblings; that is why the shape is a linear rather than a constraint.

```sh
./build/dev/ircitui_conversation_demo                       # live: arrows or wheel scroll, t stamps, q quit
./build/dev/ircitui_conversation_demo --dump-model          # the computed geometry, one line per message
./build/dev/ircitui_conversation_demo --dump-screen         # cells and their attributes
./build/dev/ircitui_conversation_demo --dump-all            # the frame, as text
./build/dev/ircitui_conversation_demo --scroll 30 --dump-all
./build/dev/ircitui_conversation_demo --sweep               # every cell vs the router
./build/dev/ircitui_conversation_demo --prove-composite     # the composite, as claims
./build/dev/ircitui_conversation_demo --click-engine        # a real click, twice
./build/dev/ircitui_conversation_demo --cols 120 --rows 40 --dump-all
```

**The avatar switches.** The gutter is on by default at its standard six columns, so every mode above
shows the avatars; `--gutter N` changes it (0 is the pre-avatar look, and the captures it reproduces are
byte-identical to the ones this tree froze before the feature existed), and the four modes below are for
the feature itself:

```sh
./build/dev/ircitui_conversation_demo --avatars             # per message: glyph, width, box, pulse, stamp
./build/dev/ircitui_conversation_demo --avatar-clicks       # every avatar cell clicked; the flag must not move
./build/dev/ircitui_conversation_demo --animate-phase 1 --dump-screen   # the pulse's dimmed half
./build/dev/ircitui_conversation_demo --timestamps --dump-all           # the stamp rows, revealed
```

**The live view's keys.** `↑`/`↓` — or `k`/`j`, or the raw escape sequence, because a terminal whose
bytes do not match the terminfo entry sends the plain form — scroll one row, `t`/`T` reveal and hide the
stamps, `q` quits. **The wheel scrolls**, and the demo routes it itself: the engine's wheel handler
writes `container.scroll_y` counting rows from the top while this widget's model counts back from the
newest, so a wheel turn handed through the engine arrives inverted and clamped. The mouse is enabled with
the terminal's hover reporting turned back off, which is the difference between a usable view and a
strobe: `IrcitUiMouseInit` forces any-event tracking on, so a pointer that merely moves is a `KEY_MOUSE`
and the loop would repaint for every one.

**Typing goes to the engine.** Printable keys are dispatched, and the input line holds focus, so
the field is real — but the demo cannot read it back: there is no public call that returns a
field's buffer, which the design record names as the one thing this widget set out to
demonstrate and cannot.

**`--cols`/`--rows` resize the virtual screen, not just the layout.** `newterm()` sizes it from
terminfo — 80×24 for `xterm-256color` — so without that a 120×40 layout put most of the tree
outside the screen and the sweep reported 847 mismatched cells, all of them reads past the real
screen. The live view takes the terminal's size and follows a resize.

## What this manual does not cover

- **Anything only a terminal can show.** Every claim here was checked against the demo on a
  pty or against the library's own probes; the visual judgement is yours.
- **The `--verify-layout` grid.** It synthesises the tree's geometry and never reads a cell,
  so it is byte-identical whatever the frame profile, the shadow or a bar's fill. It is a
  regression guard, not evidence about appearance.
