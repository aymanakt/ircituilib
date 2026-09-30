# IrcitUI — Test Strategies

This records how `packages/ircituilib` has actually been tested: the harnesses,
the drivers, the reconstruction technique, and what each layer catches that the
others miss. It is descriptive, not aspirational — every technique below was
used, and the worked examples are real runs.

The scripts themselves are archived under [`fixtures/`](fixtures/), with a
generated index in [`fixtures/INDEX.md`](fixtures/INDEX.md). This document
explains the technique; the index says what each file is.

Like its siblings in `dev/technical_designs/`, this document maintains **no
Version Management block**. Change history is the git history of this file.

---

## 1. Why this package resists ordinary testing

Three properties shape every technique below.

**The output is a screen, not a return value.** The library's job is to paint
cells. A function that lays out a popup returns `void`; correctness is a
rectangle of characters in the right place with the right attributes. There is
nothing to assert on unless you reconstruct that screen.

**The contract is a callback ABI.** The interesting behaviour is *whether a
listener was consulted, in what order, and what its verdict did*. That is
observable only as a difference in what ends up on screen afterwards.

**ncurses is an adversary to naive measurement.** It writes only *changed*
cells, positioned absolutely. The byte stream on the wire is a delta, so a
substring search for a label that is already painted returns nothing. This one
fact is the origin of the whole approach, and of the worst harness bug found
(§3.3).

---

## 2. The layers that already exist in the repo

These run under `ctest` and need no fixtures.

| Layer | Where | What it covers |
|---|---|---|
| gtest suites | `tests/ircitui_core/` — `api_test.cpp`, `lifecycle_test.cpp`, `theme_test.cpp`, `version_test.cpp` | API surface, node lifecycle, theme resolution, version |
| Generator test | `tests/test_generator.py`, registered in `tests/CMakeLists.txt` | `tools/gen_ui.py` exit behaviour |
| Fake curses backend | `tests/fake_ncurses/`, selected by the `fake` preset (`IRCITUI_USE_FAKE_CURSES`, option at `CMakeLists.txt:55`) | Links the library with no terminal at all |
| Layout verifier | `--verify-layout` mode of `examples/ui_main.c` | Geometry, headless, no PTY |

Run:

```sh
cmake --preset dev -B build/dev-test -D_PACKAGE_TESTS=ON
cmake --build build/dev-test -j4
(cd build/dev-test && ctest --output-on-failure)
```

**The preset is not decoration — it is what selects clang, and without it the build
does not compile.** uflib's `utils_str.h:78` declares a block, which clang parses
only with `-fblocks`; `ircitui_build_options.cmake` adds that flag for clang alone,
because GCC rejects it outright. A bare `cmake -S . -B …` takes `cc` from the
environment, and on this machine that is GCC, so the build dies inside a uflib
header with `expected ')' before '^' token` — an error that names neither the
compiler nor curses. Use a preset, or `./dev_build.sh --test`, which is the same
thing in one word.

The same suite runs with no terminal at all under the `fake` preset, which
inherits `dev` and so keeps clang, the sanitizers and the rest:

```sh
./dev_build.sh --preset fake --test
(cd build/fake-test && ctest --output-on-failure)
```

```
1/5 Test #1: ircitui_core_generator_tests .....   Passed    0.08 sec
2/5 Test #2: ircitui_core_api_tests ...........   Passed    0.01 sec
3/5 Test #3: ircitui_core_theme_tests .........   Passed    0.01 sec
4/5 Test #4: ircitui_core_lifecycle_tests .....   Passed    0.01 sec
5/5 Test #5: ircitui_core_version_tests .......   Passed    0.01 sec

100% tests passed, 0 tests failed out of 5
```

**A zero-test run is a failure, not a pass.** A tree configured with
`_PACKAGE_TESTS=OFF` exits 0 and looks green. The `add_c_test()` helper in
`tests/CMakeLists.txt:17` is the only thing that registers a test, so a
`No tests were found!!!` line means the layer above is absent, not satisfied.

**The generator is a prerequisite, not an afterthought.** Before relying on
regeneration, prove it reproduces the checked-in artefact byte-for-byte:

```sh
python3 tools/gen_ui.py layout/ircitui.yaml /tmp/regen_check.c
diff -q /tmp/regen_check.c generated/ui_generated.c && echo REPRODUCIBLE
```

```
REPRODUCIBLE: byte-identical
```

If that ever fails, stop: hand-editing a `_generated` artefact is prohibited,
and the only lawful route in — the YAML plus the generator — would have been
shown to be broken.

---

## 3. The core technique: PTY plus screen reconstruction

### 3.1 Shape

Spawn the real binary on a pseudo-terminal, hold the master side, drive it with
bytes, and feed everything it writes into a terminal model. Assert on the
*reconstructed screen*. Fixture: `fixtures/pty/vt.py` (the model),
`fixtures/pty/cap.py` (a minimal driver).

```python
pid, fd = pty.fork()
if pid == 0:                       # child: become the app
    os.environ["TERM"] = "xterm-256color"
    os.chdir(pkg_dir)              # the demo loads its YAML relative to CWD
    os.execv(binary, [binary])
fcntl.ioctl(fd, termios.TIOCSWINSZ,
            struct.pack("HHHH", ROWS, COLS, 0, 0))   # size BEFORE first paint
...
os.write(fd, key)                  # drive
...
screen.feed(os.read(fd, 65536))    # reconstruct
```

Four details that are easy to get wrong and each cost a debugging cycle:

- **Set the window size before the first read.** A PTY starts at 0×0; a driver
  that resizes late reconstructs the initial paint at the wrong geometry.
- **`os.chdir()` to the package.** `tools/gen_ui.py` bakes the popups in, but
  the demo still resolves assets relative to CWD. A driver run from `/tmp`
  silently exercises a different tree.
- **Drain on a deadline, not on EOF.** A TUI never closes its output. Every
  `drain()` in these fixtures is `select()` with a timeout, not `read()` to
  completion.
- **Kill with `SIGKILL` after closing the fd**, and reap. A leaked child holds
  the PTY and the next run inherits a stale screen.

### 3.2 Why a byte-stream grep is not a test

ncurses repaints only what changed. The first paint of a label is visible in
the stream; every subsequent repaint of the same cells is not. So:

- A grep for `tools_panel` passes on the frame where the popup opened, and
  fails on the frame where it was merely *already* open — same screen.
- A grep for text that was *erased* passes forever, because the erase is a
  cursor move and a write of blanks elsewhere, not a rewrite of the label.

The reconstruction exists to make "what is on screen" the unit of assertion.

### 3.3 The harness bug that produced a false finding

The screen model initially did not implement **ECH** (`ESC[nX`, erase n
characters in place). ncurses uses ECH to clear a field without repainting it.
The model therefore left characters standing that the real terminal had erased,
and a "stale focus marker" was reported as a library bug.

Two fixtures are preserved as the A/B record of that:

| Fixture | Model |
|---|---|
| `fixtures/pty/emulator-ech.py` | ECH implemented — the correct model |
| `fixtures/pty/emulator-noech.py` | ECH absent — *"HISTORICAL, for A/B only"* |

and the re-check is `fixtures/probe/ab/{p_echo,p_noecho}/stale_marker.py`,
which differ only in which model they import:

```diff
-sys.path.insert(0, "/tmp/irctui-ab/ech")
+sys.path.insert(0, "/tmp/irctui-ab/noech")
```

**The lesson generalises past this bug.** A reconstructed-screen assertion is
only as trustworthy as the emulator, and an incomplete emulator produces
findings that look exactly like real ones. When a result is surprising, the
first question is what the model does not implement — not what the library did
wrong.

### 3.4 Worked example — does clicking Tools open the menu?

```python
# fixtures/pty/cap.py, abridged
def click(x, y):                       # SGR mouse, 1-based
    return f"\x1b[<0;{x};{y}M\x1b[<0;{x};{y}m".encode()

open("/tmp/clk_menu.raw","wb").write(run([click(85, 3)]))
```

then render it:

```sh
python3 fixtures/pty/vt.py /tmp/clk_menu.raw
```

```
08|│┌─ body ─────────────────────────────────────────────────────────────────────────────────────────┐│
09|││││                             ┌─ tools_panel ──────────────────┐
10|││││                             │tools (drag me)                 ▒
11|││││                             │▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▒▒
...
16|││││                             │● 1                             ▒
17|││││                             │○ 2                             ▒
18|││││                             │○ 3                             ▒
20|││││                             │☑ a                             ▒
21|││││                             │☐ b                             ▒
22|││││                             │☑ c                             ▒
24|││││                             │[ Close ]                       │
25|││││                             └────────────────────────────────┘
```

To sweep rather than guess coordinates, `fixtures/pty/sweep.py` clicks a range
and asserts on the reconstructed text:

```
x= 78  denied=False  panel=False
x= 82  denied=False  panel=True
x= 86  denied=False  panel=True
x= 90  denied=False  panel=True
x= 94  denied=False  panel=False
```

That output is what located the button's real hit box, and it was the check
that confirmed a fix — before it, the same sweep read
`x=85  denied=True  panel=False`, i.e. the click was reaching a listener that
refused it.

---

## 4. Assertion strategies on a reconstructed screen

A screen dump is evidence, not an assertion. Four ways of turning it into one,
in increasing order of rigour.

### 4.1 Whole-screen text

The crudest and the most common: join the buffer and test membership of a
label. Fine for "did the popup open", blind to position, attributes and
erasure. `fixtures/probe/menu/g0b_reach.py` is the worked pattern.

### 4.2 Column-exact reading

Wide characters occupy two cells but one codepoint, so an index into a Python
string is not a screen column, and every box-drawing character is a trap for
`.find()`. Read the row as cells:

- `fixtures/probe/menu/marker/cells.py` — *"Column-exact dump of one screen
  row, so wide chars cannot fool the reading."*
- `fixtures/probe/menu/marker/region.py` — *"Full picker region,
  column-indexed, so box geometry is read not inferred."*

### 4.3 Ground truth over reconstruction

The strongest form: stop modelling and read what ncurses actually emitted.
`fixtures/probe/menu/marker/rawdump.py` lists *"the absolute-positioned writes
ncurses actually made"*. When a reconstructed screen and a raw dump disagree,
the model is wrong — this is the tool that settles it.

### 4.4 Cell-level differential between two builds

`fixtures/probe/menu/marker/mkdiff.py` takes two binaries and diffs their
reconstructed screens cell by cell. This is the assertion that matters for a
refactor: not "does the new build look right" but "does it differ from the old
one anywhere at all". Every difference is then either intended or a defect, and
the list is finite.

### 4.5 Geometry read, not inferred

`fixtures/probe/menu/g4/rect.py` — *"Read the menu's rect off the screen, cell
by cell."* Fixtures that assert hardcoded rectangles go stale the moment a
layout constant changes; ones that read the box back do not.

---

## 5. Differential testing against the baseline

For a package that must preserve behaviour, ordinary pass/fail tests prove very
little. The useful question is *"is the new build identical to the old one
under the same input"*. Fixtures: `fixtures/parity/`.

`fixtures/parity/diffdrv.py` sends one named key sequence to one binary:

```
Usage: diffdrv.py <binary> <sequence-name>
```

with the sequences themselves in a table:

```python
SEQ = {
    "idle":        [],
    "f7_tools":    [b"\x1b[18~"],
    "f8_menu":     [b"\x1b[19~"],
    "typed":       [b"h", b"e", b"l", b"l", b"o"],
    "f8_move_ent": [b"\x1b[19~", b"\x1b[B", b"\x1b[B", b"\r"],
    ...
}
```

Run one against each binary and diff:

```sh
python3 fixtures/parity/diffdrv.py build/base/ircitui_demo f7_tools > /tmp/a.txt
python3 fixtures/parity/diffdrv.py build/new/ircitui_demo  f7_tools > /tmp/b.txt
diff /tmp/a.txt /tmp/b.txt
```

`fixtures/parity/diffdrv2.py` widens the interaction surface;
`diffdrv3.py` adds mouse and resize. `cmp_set.sh` / `cmp_set3.sh` run a whole
sequence set against a driver, and `cmp_canon*.sh` are the canonicalising
variants.

**Two caveats this collection records honestly:**

- The drivers hardcode `os.chdir("/home/devops/repos/ircit/packages/ircituilib")`
  with the comment *"assets live here"* — they assume the tree under test is
  the working tree.
- The `SEQ` table names the demo's function keys, and the demo's key map has
  since changed (`f9_emoji` is now the deny toggle, not the picker). A
  differential driver's sequence table is versioned against the demo, and goes
  quietly stale when the demo moves.

---

## 6. Sanitizer sweeps

Behavioural equivalence says nothing about undefined behaviour, and this
library has historically had lifetime questions around popped nodes.
`fixtures/build/asan-build.sh` builds with sanitizers; `fixtures/build/asan-sweep.sh`
drives *every* probe against that build:

> *"Drive every probe against the ASan build of the current tree. All of them
> must [pass clean]"*

The technique that matters is **using a sanitised build to separate a crash
from latent UB**. `fixtures/probe/menu/g0_uaf.py` asks exactly that — *"G-0: is
the free-then-read in ui_button_input a crash, or only a latent UB?"* — and
`g0c_leak.py` measures a leak instead of inferring one. On a deterministic
sanitiser build the question has an answer; by reading the code it does not.

Related standing hazard, recorded in memory and worth repeating here: reading a
node *after its popup has been popped* is not a use-after-free in this design.
`ui_deflate` must never free nodes. A probe that reports one is more likely to
be wrong than the library.

---

## 7. Size and resize matrices

Layout defects cluster at boundaries. Two fixtures:

- `fixtures/build/verify-g2.sh` — `--verify-layout` across **12 sizes**, run
  from a scratch CWD so the verifier cannot pick up assets from the tree.
- `fixtures/pty/pty_resize.py` — *"The scenario D3 exists for: a popup is open
  when the terminal shrinks."*

The resize path has its own diagnostics, because "the screen went blank" has
at least three causes: `fixtures/pty/diag-resize.py` measures *what ncurses
actually emits* on a shrink, `diag-resize-2.py` separates stale state from a
wedged app, and `diag-resize-3.py` asks whether it is specific to a popup being
open.

Every popup assertion should be run at more than one geometry. The panel in
§3.4 is capped by `PRIV_CONFIG_DEFAULT_POPUP_W/H` (`include/ircituilib/config_defs.h:93-95`),
which is a *cap*, not a floor — `sPopupSize()` (`src/ui_engine.c:1939`) clamps
with `if (*h_ptr > caph) *h_ptr = caph;`. A size increase that looks correct in
the YAML can therefore be silently discarded at runtime, and at a small enough
terminal the cap itself is clamped again by `PRIV_CONFIG_DEFAULT_POPUP_EDGE_MARGIN`.

---

## 8. Source-level equivalence proofs

A rename, a reformat or a documentation sweep must not change behaviour, and
"the tests still pass" is weak evidence for that. The stronger claim is that
the code is *the same code*.

Technique: tokenise the file before and after, drop comments, normalise
whitespace, and compare the token streams. Any difference is an unintended
edit.

| Fixture | Applied to |
|---|---|
| `fixtures/source/gate2_equiv.py` | literal → named constant substitution |
| `fixtures/source/gate3_proof.py` | the module-wide rename |
| `fixtures/source/g6_toks.py` | comment-only edits |
| `fixtures/source/gate5_tokdelta.py` | *classification* of each token delta |

This is what makes a 2,500-line rename reviewable: the proof reduces it to the
short list of token deltas that were intended, and everything else is noise.

The transform side is deliberately **not** `sed`:

- `fixtures/source/gate3_rename.py` applies the rename with an
  identifier-aware tokenizer, so it cannot rewrite a string literal or a
  mention inside prose.
- `fixtures/source/mkmap.py` builds the rename map first (Gate 1), and
  `fixtures/source/gate3_audit.py` reconciles that map against the tree
  *before* anything is touched — it refuses to run if the map and the tree
  disagree.
- `fixtures/source/gate5_pairs.py` recovers parameter rename pairs
  positionally, and `fixtures/source/gate5_shadow.py` measures the shadowing
  risk *before* renaming.
- `fixtures/source/gate5_prose.py` finds comments that still name a parameter
  by its old name — the residue a pure rename always leaves.

---

## 9. Clang-backed inventory

Claims about a C tree ("no prototype is missing", "these 82 functions are
static") should come from a compiler, not a regex.

`fixtures/source/gate5_build.py` states the division of labour plainly: *"clang
supplies facts, the file text supplies geometry."* The pattern is
`-Xclang -ast-dump=json` (via `compile_commands.json`) for the semantic
questions — `gate5_scan.py`, `gate5_inv2.py`, `gate5_inv3.py`, `gate5_inv4.py` —
and a plain tokeniser (`gate5_locate.py`) for the positional ones.

The same discipline applies to the public header: `fixtures/source/g6_uih.py`
splits `include/ircituilib/ui.h` into `(doc-block, declaration)` items so that
"all 62 declarations are byte-identical to the previous revision" is a
measurable statement rather than an assurance.

---

## 10. Document linting

The design documents carry a dozen Mermaid diagrams and a conformance register,
and both rot silently — a diagram that fails to parse renders as an error box
that nobody reads, and a cross-reference to a renumbered section still looks
like a link.

`fixtures/doc/mermaid-lint.py` is a structural Mermaid linter:

```
Usage: mmlint.py FILE.md
```

It encodes the breakers that are not obvious from Mermaid's documentation —
most importantly that `;` is a *statement separator*, so
`REG->>REG: reject duplicate ids; reject on cap` parses as a second, malformed
statement and the whole diagram fails. The same class of break includes
parentheses inside a `participant ... as` alias, inline `%%` in a flowchart
label, and unbalanced `subgraph`/`end`.

A one-off sweep of all diagrams in a document looks like this:

```
blocks: 12
issues: 0
```

Two notes on reading that output honestly. `classDef dead fill:#fdd` is not an
entity defect — `#` is legitimate colour syntax, and a scanner that strips only
quoted strings will flag it falsely. And **no offline Mermaid renderer was
available** in this environment (`mmdc` absent, `npx` needs network), so the
linter is the substitute for rendering, not a replacement for it: it can prove
a block is well-formed, not that it draws what was intended.

`fixtures/doc/doxygen-block-builder.py` is the bulk generator for the public
header's documentation, and `fixtures/source/gate6_census.py` measures what is
*missing* against a baseline rather than asserting completeness by hand.

---

## 11. Build and syntax gates

Cheap, fast, and they catch a whole class of mistake before a link:

```sh
fixtures/build/syntax-real-ncurses.sh   # -fsyntax-only against real ncursesw
fixtures/build/dev-build-verctl.sh      # configure (+tests) → build
fixtures/build/build-caret.sh <tree> <label>
fixtures/build/build-focus.sh <tree> <label>
fixtures/build/asan-build.sh
```

`-fsyntax-only` is the intermediate QA step: it resolves headers and types
without producing an object file, so it isolates "did I write valid C" from
"does the whole program link". The multi-tree build helpers
(`build-caret.sh`, `build-focus.sh`) exist for the differential work in §5 —
they build the *same* probe set against two trees so the comparison is
apples-to-apples.

---

## 12. The fixture collection

```
tests/fixtures/
  INDEX.md          generated — every file, its own description, its origin
  make-index.py     regenerates INDEX.md
  pty/              terminal model + PTY drivers              (26 files)
  probe/            behaviour probes: menu, caret, A/B       (60 files)
  source/           token equivalence, rename, clang census   (36 files)
  build/            sanitizer, syntax and multi-tree builds   (10 files)
  doc/              Mermaid linter, Doxygen block builder      (2 files)
  parity/           baseline differential drivers              (7 files)

  141 scripts in total; INDEX.md and make-index.py sit at the root.
```

Regenerate the index after adding or removing a fixture:

```sh
python3 tests/fixtures/make-index.py
```

### Provenance and honesty about it

Every file is an archival copy of a scratch script that lived under `/tmp`
during the work. The `origin` column in `INDEX.md` records where each came
from. Three consequences a reader must know:

- **They will not run unmodified.** Most hardcode `/tmp/...` paths to the build
  trees and binaries they were written against, and some hardcode
  `os.chdir("/home/devops/repos/ircit/packages/ircituilib")`. They are
  preserved as technique and as a record of what was measured, not as a
  turnkey suite.
- **Byte-identical duplicates were collapsed.** Seven copies of one generic
  PTY driver existed under different names; four copies of one screen model;
  the `asan`/`sweep`/`verify` helpers existed both loose and inside their probe
  directories. One canonical copy of each is kept. The A/B pair in
  `probe/ab/` is **not** a duplicate — those differ in which emulator they
  import, which is the whole point of §3.3.
- **One pseudo-fixture was discarded.** A file named `syntax-sandbox.sh`
  contained a single line: a captured `compile_commands.json not found` error
  message. It was a failed invocation, not a script.

### What is not in here

The probes for a given gate were written against a specific pair of trees and
a specific demo key map. They age. When reusing one, check its assumptions
first: which keys it sends, which CWD it needs, and which binary path it
hardcodes.

---

## 13. Limits — what this strategy does not prove

Stated plainly, because the failure mode of a test strategy is misplaced
confidence:

- **A reconstructed screen is a model.** §3.3 is the proof. Only
  `rawdump.py`-style ground truth escapes this, and it is far harder to assert
  on.
- **No offline renderer for Mermaid.** §10's linter is a substitute, and it
  establishes well-formedness, not appearance.
- **Differential testing inherits the baseline's defects.** "Identical to the
  old build" is not "correct" — it is only "unchanged". The conformance
  register in `IRCITUILIB_DESIGN.md` §17 exists precisely because the baseline
  is not the standard.
- **Sanitizers cover what the probes drive.** A path no probe reaches is not
  covered, however clean the sweep looks.
- **The suite is thin where the risk is highest.** Five gtest cases against an
  engine of several thousand lines; the real coverage is in these fixtures,
  which do not run under `ctest` and are not wired into CI. Wiring the
  reusable subset (`pty/vt.py`, `parity/diffdrv*.py`,
  `probe/menu/marker/mkdiff.py`) into a build target is the obvious next step.
