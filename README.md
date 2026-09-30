# ircituilib

A declarative ncurses UI framework engine. The UI interface is authored as YAML, compiled to
C, cloned into a live tree at runtime, and driven by listeners the host application owns.
As far as I am aware it is the first of its kind in the c-language domain, heralding new idiomatics for writing 
ui-rich c applications.

This is highly influenced by my experience writing mobile apps for the android platform, 
which was a pain in the back, but it grew on me over the years and I developed some kind of intuition for it. 
As a bonus, you also get a cool javarx like idioms and semantics (futures, promises, coroutines) courtesy of my 
ufsrvrxlib package. 

The framework was written to replace UI primitives I originally developed for the IRCIT irc client (written over 25 
years ago), which was bloody good for its time, but after 25 years you'd kinda be forgiven for thinking I learned how 
to do it better since. I am now more focused in using it to write a terminal CLI client for my unfacd messaging network.

## What it does

| Area | What it covers |
|---|---|
| Declarative layout | YAML authored against an EBNF grammar, validated, then compiled to C. The generator is the only writer of `src/generated/` — the build regenerates none of it |
| Object model | One opaque node type. A static reference tree is cloned into a live screen tree, plus any stacked popups |
| Listeners | Eight per node, two instances per node, one consumption contract |
| Dispatch | One route, one owner. A single default router per listener branches on kind |
| Async pipeline | Worker pool → lockless ring buffer → eventfd → Thread 0 drains. A worker never reaches the tree |
| Layout engine | Four modes, linear packing, frame placement, constraint resolution, clipping, the recycler, the transient overlay, the unread banner |
| Input and editing | Focus, dispatch order, the edit buffer, mouse |
| Popups and layers | A popup is an ordinary child; dragging |
| Groups and selection | Radio and check groups  |
| Theme | A selector cascade — all / kind / class / id — with deterministic specificity |
| Image | PNG → half-block / Braille pipeline |
| Clipboard and emoji | One file-scope buffer, not a system clipboard, bound to `^C`/`^V`/`^X` on the focused node; an `emoji_grid` table that appends the clicked grapheme, and a picker toggle |
| Windowing | An opaque registry and driver behind a translation table, with two backends |


## How it is put together

Three chains, and they meet in one place.

```
AUTHORING
  tools/layout-grammar.ebnf + the C1-C15 semantic rules
        ↓
  layout/*.yaml  →  validate_layout.py  →  gen_ui.py
        ↓
  src/generated/ui_generated.c      tracked, compiled, never hand-edited

RUNTIME
  IrcitUiDataBind()  →  IrcitUiDataBindDescriptor
                        (root · pool · templates · id index)
        ↓  IrcitUiScreenReset() → IrcitUiClone()
  IrcitUiScreen        the live tree, owned by Thread 0
        ↓  IrcitUiLayout → IrcitUiRealize → IrcitUiRenderFrame → doupdate()

  WINDOW DRIVER LAYER
  IrcitUiWindowRegistry  →  translation table (18 slots)  →  IrcitUiWindowDriver
        ├── ncurses adapter    the only translation unit that includes <ncurses.h>
        └── ANSI adapter       no curses header, no curses library

ASYNC
  worker pool (ufsrvrxlib) → LocklessRingBuffer → eventfd → Thread 0 drains
```

The **authoring** chain turns YAML into compiled C. The **runtime** chain turns that into
a tree and paints it through the driver layer. The **async** chain lets a worker hand a
value back for Thread 0 to apply.

### Requirements

| Dependency | Terms |
|---|---|
| CMake | ≥ 3.25; C17 |
| **Wide ncurses** | Required. The build fails if `CURSES_LIBRARIES` does not match `ncursesw` — narrow libncurses cannot link `waddnwstr` / `wadd_wch` / `setcchar` |
| **libpng** | Required unconditionally, and linked `PUBLIC`, so it is transitive |
| **pkg-config** | Required |
| **`ufsrvrxlib` and `uflib`** | Required, resolved through `pkg-config` and linked `PUBLIC`. A tree without both `.pc` files fails to configure |
| Googletest | `release-1.12.0` via `FetchContent`, tests-ON only |

### Build

```sh
cmake --preset dev            # configure into build/dev/
cmake --build build/dev -j
./build/dev/ircitui_demo      # the demo builds alongside the library
```

Presets configure into `build/<preset>/`, so they do not disturb one another. `dev` is
the default posture: **ASan + UBSan + LSan**. There are 14 declared configure presets (13
listed — `ircitui-base` is hidden), covering individual sanitizers, combinations,
coverage, and plain `debug` / `release`.

`msan` and `msan-ubsan` are **known-noisy**: MemorySanitizer needs every dependency
instrumented, and libc, libpng and ncursesw here are not. A red MSan run is not a
regression.

### Tests are OFF by default

This has a sharp edge: a tests-OFF tree builds no test targets, so `ctest` prints
`No tests were found!!!` and **exits 0**. A plain `cmake --preset dev && ctest` is not a
test run. Enable tests explicitly, in a separate directory:

```sh
cmake --preset dev -D_PACKAGE_TESTS=ON -B build/dev-test
cmake --build build/dev-test -j
ctest --test-dir build/dev-test --output-on-failure
```

There are no `testPresets`, so `ctest --preset` is unavailable — use `--test-dir`.

### The artefact

The archive is **`ircituilib.a`**, without CMake's usual `lib` prefix. Target, project and
artefact are all `ircituilib`; only the prefix differs from what CMake would produce. This
is deliberate, not a misconfiguration — deleting the `PREFIX ""` property restores the
platform convention.

`-Wall -Wextra -Wpedantic -Werror` applies to the **library only**; the demo gets only the
sanitizer interface.