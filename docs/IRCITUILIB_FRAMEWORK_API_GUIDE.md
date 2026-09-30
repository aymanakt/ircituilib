# ircituilib — the framework API guide

**What this is.** The framework's public API, gathered out of the design record into one place and
organised by **seam** — the boundary each part of the API sits on, rather than the document it
happened to be written in. Every section below was **moved verbatim** from the document that owned
it; nothing was rewritten on the way, and the originals now carry a summary and a pointer to the
seam that took their material.

**Where authority sits.** This guide explains the API; it does not define it. The headers in
`include/ircituilib/` are the API, and where this document and a header disagree, **the header is
right**. Behind that, `IRCITUILIB_DESIGN.md` §1.2 states the authority chain for the whole design
set, and the per-track As-IS documents (`IRCITUILIB_PREDICTIVE_AUTO_COMPLETE_DESIGN.md`,
`IRCITUILIB_PROGRESS_INDICATORS_DESIGN.md`, `IRCITUILIB_EVENTS_DESIGN.md`,
`IRCITUILIB_EVENTS_TCP_IP_DESIGN.md`, `IRCITUILIB_CONVERSATION_WINDOW_DESIGN.md`) carry the reasoning
and the evidence for each surface. This guide carries the calling convention and the worked code.

**What is deliberately not here**, and where to read it instead:

| Not in this guide | Why, and where it lives |
|---|---|
| **the callback ABI** — what a listener is handed and what its return value means | `IRCITUILIB_DATA_BINDING_DESIGN.md` owns it; `IRCITUILIB_DESIGN.md` §1.2 ranks that document first in the authority chain and takes its rulings from it |
| **the layout grammar and the generator** | `IRCITUILIB_LAYOUT_GENERATION.md` — how `tools/gen_ui.py` turns YAML into the C that populates the tree. **The one exception is §13**, which states the two `conversation_window` attributes and the C25 rules around them, because an application writing a chat layout needs them and they have no other home |
| **the invalidation model** | `IRCITUILIB_DIRTYFLAG_MODE.md` — how an interaction reaches a widget without drawing, and how the loop flushes the marks |
| **the windowing seam in depth** | `IRCITUILIB_WINDOW_ABSTRACTION_DESIGN.md`; §2.3 here is its public surface |
| **the demo's controls** | `IRCITUILIB_DEMO_MANUAL.md` — an operator's manual, not an API |
| **the design rationale** | the As-IS documents listed above. This guide says *how to call it*; they say *why it is shaped that way* |

**How a seam is laid out.** Each section opens with a short statement of what the seam is and who
owns each side of it, then the API itself: the vocabulary, the calls, the rules that bite, and what
is deliberately absent. Where a section was assembled from more than one document the provenance is
in the original's pointer, not repeated here.

---

## Contents

1. [The calling contract](#1-the-calling-contract)
2. [The public surface](#2-the-public-surface)
3. [Lifecycle, bootstrap and the loop](#3-lifecycle-bootstrap-and-the-loop)
4. [The tree, the widgets and the factories](#4-the-tree-the-widgets-and-the-factories)
5. [The asynchronous seam](#5-the-asynchronous-seam)
6. [The palette and the roles](#6-the-palette-and-the-roles)
7. [The notification overlay and its action](#7-the-notification-overlay-and-its-action)
8. [The frame profile and the popup shadow](#8-the-frame-profile-and-the-popup-shadow)
9. [The recycler](#9-the-recycler)
10. [The edit field — the widget, and the prediction attached to it](#10-the-edit-field--the-widget-and-the-prediction-attached-to-it)
11. [Progress, the bars and the seek](#11-progress-the-bars-and-the-seek)
12. [The TCP/IP extension — the event-type seam](#12-the-tcpip-extension--the-event-type-seam)
13. [The conversation window — the composite seam](#13-the-conversation-window--the-composite-seam)
14. [Worked examples](#14-worked-examples)
15. [The rules that bite](#15-the-rules-that-bite)
16. [If it does not work](#16-if-it-does-not-work)

---



## 1. The calling contract

This chapter is the contract a caller works to: the order calls must be made in,
who owns what, which calls may be repeated, how failure is reported, the binding
calls themselves, and the rules that were each found by breaking something.

### Framework machinery
Application code (host app) must maintain the core architectural rules of a clean framework by not interfering with 
abstractions and using assumptions that belong inside a private modules layer.
So, a clean breakdown  isolates the framework from the host application. Components must remain completely blind to 
one another:
------------------------------
#### Architectural Delineation: Framework vs. Host API
* Framework Layer (ircituilib): Implements reusable, atomic UI primitives (IRCITUI_EDIT_TEXT, IRCITUI_CONVERSATION_WINDOW, IRCITUI_MESSAGE_VIEWPORT). It owns its private allocations inside widget_state, handles metrics, and exposes generic public manipulation functions. It knows nothing about how or where components are wired together.
* Host Application Layer: Owns the active layout initialization setup. It coordinates individual widgets entirely through public API boundaries (using IDs or public nodes) from inside generic event lifecycle listeners.

------------------------------
#### The Clean Framework API Design Using EditText Example
To manipulate a text field without tight coupling, your framework needs to provide clean public endpoints that wrap the private IrcitUiTextEditState hidden in widget_state.
## 1. Public Framework API Headers (ircituilib/ui.h)
Add these decoupled definitions to your framework's public contract so any host application can interact with text edit widgets cleanly:



### 1.1 Required call order

1. `IrcitUiLogInit(level, file)` if a log is wanted. It must precede
   `IrcitUiDataBind()`, which is the first call that logs. The demo opens it
   *before* `initscr()` so that a refusal can still be reported on stderr
   rather than onto a screen ncurses has taken over.
2. `initscr()`, and `start_color()` for colour.
3. `IrcitUiDataBind()`.
4. `IrcitUiBindClick(...)` for each bound node — after bind, before or after reset.
5. `IrcitUiScreenReset()`.
6. `IrcitUiLayout(root, 0, 0, COLS, LINES)` → `IrcitUiRealize(root)` →
   `IrcitUiDraw(root)` → `doupdate()`.
7. `IrcitUiMouseInit()` after `initscr()`, if mouse input is wanted.
8. Per iteration: `IrcitUiDispatchKey`, re-read `IrcitUiScreen.root`,
   `IrcitUiDraw`, `doupdate()`.
9. On `KEY_RESIZE`: re-run layout and realize. The library does not handle it.
10. `IrcitUiLogFini()` after the last call that can log — after `IrcitUiDeflate`,
    before `endwin()`. Skipping it is not a fault: the stream is closed when the
    process exits. It exists so the close happens at a point the caller chooses.

### 1.2 Ownership and lifetime

- Popups and clones belong to their parent's child array and are released by
  `IrcitUiPopStacked`, `IrcitUiScreenReset`, or the internal free.
- `IrcitUiClone` returns a **detached, caller-owned** deep copy with **no public
  destructor**. Plain `free()` on a clone leaks every descendant node and every
  child array. `IRCITUILIB_DESIGN.md` §21 G9 records this.
- `IrcitUiThemeSetCurrent` takes ownership.
- `IrcitUiBindClick` copies nothing — the `user_ptr` is the caller's to keep alive.
- The **reference tree must never be freed** — it is `static` storage.
- Node `id`, `text`, listener and `theme_class` pointers in a clone are shallow
  copies into static storage, so they live as long as the program.

### 1.3 Idempotency

| Call | Repeatable? |
|---|---|
| `IrcitUiDeflate` | yes — NULLs its windows |
| `IrcitUiDataBind` | yes for the reference tree, but it also re-zeroes `IrcitUiScreen.root` **without freeing it**, orphaning a live screen tree |
| `IrcitUiScreenReset` | yes, but it frees each time and leaves the focus pointer dangling |
| `IrcitUiBindingReset` | yes — clears the table only, touches no node |
| theme calls | yes |

`IrcitUiDispatchKey` calls `getmouse()` itself, so the caller's loop must be in
`KEY_MOUSE` mode with ncurses mouse enabled.

### 1.4 Error convention

There is **no** `assert`, `errno`, `abort`, error enum or error-string API
anywhere in the library.

| Style | Examples | Meaning |
|---|---|---|
| `void` + silent no-op | `IrcitUiLayout`, `IrcitUiDraw`, `IrcitUiSetFocus`, the clipboard calls | a NULL or wrong-kind argument is ignored |
| `int` as boolean | `IrcitUiIsContainer`, `IrcitUiIsFocusable` | |
| `int` 1 = ok, 0 = refused | `IrcitUiBindClick`, the theme setters | a refusal is diagnosable at the call site |
| `int` count / index | `IrcitUiGroupSelectedIndex` (−1), `IrcitUiGroupSelectedValues` | |
| `int` status | `IrcitUiVerifyLayout` — the only function that reports to the user | 0 sound, 1 errors |
| pointer, NULL = failure | `IrcitUiClone`, the inflate family, `IrcitUiFind`, `IrcitUiHitTest`, the factory calls | |

`IrcitUiShowEmojiPicker` returns NULL on **success**: it is a toggle, and a second
call pops the existing picker and returns NULL having done what was asked. That is
the one function in the library where NULL does not mean failure, and it is the
most likely thing to document wrongly.

`IrcitUiShowContextMenu` does **not** behave that way, despite the symmetry being
tempting. When a menu already exists it pops it at `src/ui_engine.c:2400` and then
continues building — the replacement is returned normally at `:2468`. NULL from it
means only that the tree or the target was NULL, or that `calloc` failed.

### 1.5 The binding API

```c
IrcitUiClickDisposition IrcitUiClickDispatch(IrcitUiNode *node_ptr);
int                     IrcitUiBindClick(const char *node_id_ptr,
                                         IrcitUiClickCallback on_click_ptr,
                                         void *user_ptr);
void                    IrcitUiBindingReset(void);
```

`IrcitUiBindClick` returns 0 — and binds nothing — when the reference tree is not
yet bound, when the id names nothing in either `IrcitUiRoot` or any template, when
the id is already bound, or when the table is full. It refuses rather than
silently shadowing.

### 1.6 The rules that bite

Each of these was found by a failure, not by reasoning.

1. **Thread 0 owns the tree.** A worker posts; it never reads or writes a node.
2. **Every yield needs a resumer.** A coroutine suspended with nothing left to
   resume it never completes, never reports, and fails silently.
3. **Do not poll the future after completion.** Completing a promise releases
   it. Signal completion with a flag you own.
4. **The continuation borrows the result.** Copy what you need; do not free it
   and do not keep the pointer.
5. **The ring drops when full — it never retries.** The loop posts its own input
   and is the ring's only consumer, so a producer waiting for room waits on a
   pop only it could have performed.
6. **The failure path carries an id, not a node.** A result can outlive the
   widget it was fetched for.
7. **Only an edit field owns text.** An `IRCITUI_BRIDGE_TEXT` result for any
   other kind is logged and dropped.
8. **The continuation frees its own context.** A future abandoned before it
   completes never runs its continuation, so that context leaks. A caller that
   can abandon a future owns its disposal.
9. **`curl_global_init` once, from one thread** — if you are using libcurl on a
   worker.

## 2. The public surface

This chapter states what an application may call and from which header: the
framework's full public surface, the palette track's account of what is public
and what deliberately is not, and the windowing seam's three headers.

### 2.1 The full public surface

**175** `PUBLIC_API` declarations in `include/ircituilib/ui.h` (recounted 2026-09-23, by
`grep -c PUBLIC_API` on the header rather than carried forward). **The fourteen it lost are not
gone from the surface: they moved to `ircituilib/ircitui_edit_text.h`, which carries 16.** The
edit field's calls — `SetValue`, `SetDictionary`, `GetDictionary`, `Hint`, `ShowSuggestions`,
`SuggestionCount`, `Suggestion`, `SuggestionSelected`, `AcceptHint`, `EditInput`,
`InputAppend`, and the caret's `BlinkReset`, `CaretVisible`, `BlinkAdvance` — now sit with the
unit that implements them, plus `IrcitUiEditTextGetValue` and `IrcitUiEditTextClear`, which are
new. **`ui.h` includes that header**, so a consumer reaching for the umbrella goes on compiling
unchanged and this is a relocation rather than a narrowing; the count below is a count of
`ui.h`, which is why it falls while the surface does not. The previous count follows,
for the history: it was 189 as recounted 2026-09-21, which stood
at 182 as counted on 2026-09-20 and had drifted, which is the third time this number has been wrong in
this document's own history — it is a count of a file that other tracks edit, so it is re-measured
rather than trusted. The refactor adds **one** here — `IrcitUiNodeFromDef`, §3 — and its other
**twelve** are in `ircituilib/ircitui_conversation.h`, described in §13. (Measured 2026-09-23:
that header carries **15**, not twelve — a drift this change did not introduce and has not
resolved.) It was
150 before the predictive-field track, whose 22 are the `IrcitUiDictionary` set in §10).
Every one has a definition
in the archive; there are no declared-but-undefined functions. The count was 97
before the public-API track's GATE 3; the 19 it gained are the dispatcher and
framework-defaults set below, `IrcitUiNodeDestroy`, `IrcitUiLogEmit` and the
logging surface. The 117th is `IrcitUiRecyclerSetViewTypes`, from §9's
heterogeneous rows.

**Counted again on 2026-09-19, and it had drifted.** 117 → 129, and the twelve are
each accounted for: §15's `IrcitUiRecyclerSetStickyHeader`; §25's
`IrcitUiRecyclerSetSelectionMode`; §26's six — `SetEditType`, `BeginEdit`,
`CommitEdit`, `CancelEdit`, `IsEditing` and `IrcitUiEditTextSetValue`; and the
network pipeline's four — `IrcitUiDispatcherRegisterNetworkListener`,
`IrcitUiDispatcherPostNetworkChunk`, `IrcitUiEventDispatcherDestroy` and
`IrcitUiDestroyFrameworkContext`. The register is only useful if it is read off the
header rather than carried forward, which is how it fell behind.

**Counted a third time on 2026-09-19, for the transient overlay.** 129 → **134**,
and the five are the snackbar's: `IrcitUiSnackbarShow`, `IrcitUiSnackbarDismiss`,
`IrcitUiSnackbarActive`, `IrcitUiSnackbarAdvance` and `IrcitUiSnackbarRender`. The
overlay's teardown, `IrcitUiSnackbarDestroy`, is deliberately **not** on this list:
it is declared in `src/include/ui_snackbar_priv.h` and called by
`IrcitUiDestroyFrameworkContext`, because a window the framework owns is released
by the framework and not by the application. See `IRCITUILIB_DESIGN.md` §11.8.

**Counted a fourth time on 2026-09-20, and it had drifted twice over.** 134 → **146**,
and the twelve are the palette track's ten — `IrcitUiPaletteStyle`,
`IrcitUiPaletteSetSlot`, `IrcitUiPaletteApplyProfile`, `IrcitUiPaletteProfileByName`,
`IrcitUiPaletteRepaint`, `IrcitUiPaletteSlotForName`, `IrcitUiPaletteSetEffects`,
`IrcitUiPaletteEffects`, `IrcitUiPaletteCanopyActive` and `IrcitUiPaletteRestore`
(recorded in `IRCITUILIB_PROGRESS_INDICATORS_DESIGN.md`, not in this document) — plus this
track's two, `IrcitUiSnackbarShowAction` and `IrcitUiSnackbarKey`. **That counting also
found a second kind of drift: the palette's ten were in the total and in no group, so
the list below summed to 136 against a stated 146.** A count and a list can disagree
while both are "up to date". The group is added below. Note what the drift corrections
have in common: the register was carried forward rather than re-read, and each time the
gap was larger than the edit that revealed it. **Count it and list it, do not carry it.**

**Counted a fifth time on 2026-09-20, for the frame profile and the drop shadow.**
146 → **150**, and the four are `IrcitUiSetBorderStyle`, `IrcitUiBorderStyleGet`,
`IrcitUiSetShadow` and `IrcitUiShadow`. This one is re-read off the header rather than
carried, and the group below is written in the same change, so the total and the list
agree by construction rather than by a later audit — which is the lesson the fourth
counting paid for. Re-measured twice: `grep -c` and `grep -o` on `ui.h` agree at 182.

**And the fifth counting found the fourth one's drift again, five times larger — open, and
not reconciled here.** Every `PUBLIC_API` function name in `ui.h` was extracted and looked
for in this section: **103 of the 150 are named, and 47 are in the total and in no group.**
They are the accessor set (`IrcitUiNodeGetText`, `IrcitUiNodeGetGeometry`,
`IrcitUiEventGetKey`, …), the whole recycler policy surface
(`IrcitUiRecyclerSetAdapter`, `IrcitUiRecyclerBeginEdit`, `IrcitUiRecyclerSetCursor`, …),
the dispatcher's posting set (`IrcitUiDispatcherExit`, `IrcitUiDispatcherExited`,
`IrcitUiDispatcherPostKey`, …) and all eight `IrcitUiDefaultOn*` listeners. Each was
confirmed absent by a second pass on the section alone, and the section carries no
disclaimer saying it is a curated subset — it is titled "The full public surface" and
states a total its own list does not reach. **Reconciling it is its own task and is not
done here**, because it is 47 prose entries unrelated to the frame work that measured it;
what is recorded is the measurement, so the next counting starts from 103 rather than
from the belief that the list is complete. The four this track added *are* grouped, above,
in the same change that added them.

**Focus and caret** — `IrcitUiSetFocus`, `IrcitUiBlinkReset`,
`IrcitUiCaretVisible`, `IrcitUiBlinkAdvance`, `IrcitUiIsFocusable`.

**Layers and popups** — `IrcitUiTopLayer`, `IrcitUiFocusInLayer`,
`IrcitUiFocusTop`, `IrcitUiPopupFocusOnOpen`, `IrcitUiInflate`,
`IrcitUiInflateAnchored`, `IrcitUiInflateFloat`, `IrcitUiPopStacked`,
`IrcitUiCloseTopPopup`, `IrcitUiShowContextMenu`, `IrcitUiCreateFloatingWindow`.

**Layout, realization, drawing** — `IrcitUiLayout`, `IrcitUiRealize`,
`IrcitUiDraw`, `IrcitUiDeflate`, `IrcitUiDragTo`, `IrcitUiIsContainer`.

**The transient overlay** (`IRCITUILIB_DESIGN.md` §11.8) — `IrcitUiSnackbarShow`,
`IrcitUiSnackbarDismiss`,
`IrcitUiSnackbarActive`, `IrcitUiSnackbarAdvance`, `IrcitUiSnackbarRender`,
`IrcitUiSnackbarShowAction`, `IrcitUiSnackbarKey`. The first five
are the application's half: raise a notification, take one away, ask whether one is up,
and the two calls a frame loop makes — advance before the frame, render after it.
`ShowAction` raises the same box with an action label and one hotkey, and it returns
`int` where `Show` returns `void` because a caller that has hidden data behind an undo
must be able to tell a refused affordance from a raised one. `Key` is that hotkey's
path: `IrcitUiDispatchKey` calls it before anything else, and it is public too for an
application routing keys its own way.
`IrcitUiSnackbarDestroy` is the framework's half and is not public; see
`IRCITUILIB_DESIGN.md` §11.8.

**The colour palette** (`IRCITUILIB_DESIGN.md` §11.8's slots; recorded in
`IRCITUILIB_PROGRESS_INDICATORS_DESIGN.md`) — `IrcitUiPaletteStyle`,
`IrcitUiPaletteSetSlot`, `IrcitUiPaletteSlotForName`, `IrcitUiPaletteApplyProfile`,
`IrcitUiPaletteProfileByName`, `IrcitUiPaletteRepaint`, `IrcitUiPaletteSetEffects`,
`IrcitUiPaletteEffects`, `IrcitUiPaletteCanopyActive`, `IrcitUiPaletteRestore`. A slot
is a ROLE and resolves to a colour pair; `Style` is what a painter asks for one, and
`SetSlot` redefines one at run time without touching a node. The last four are the
register-driven effects and their exit mitigation, which are **off by default** because
writing a colour register makes ncurses emit `OSC 104` at exit and the terminal's
palette returns to the emulator's defaults. There is no declarative surface for the
effects — a gap recorded in that document's §9.9.

**The frame's profile, and the popup's shadow** — `IrcitUiSetBorderStyle`,
`IrcitUiBorderStyleGet`, `IrcitUiSetShadow`, `IrcitUiShadow`. Both live in
`src/ui_palette.c` beside the effects because both are one setting for the whole process
rather than a property of a node, and neither is a register: the profile changes which
GLYPHS a frame is drawn from, and the shadow decides whether a stacked popup writes an
L-shaped strip into its **parent's** cells. `IrcitUiBorderStyleGet`'s
`IRCITUI_BORDER_STANDARD` is the ACS box this library has always drawn, so the default is
not "off" but "unchanged" — and it is drawn through the same `box()` call, not through a
table of ACS strings, because `box()` picks the corners ncurses has for the terminal's own
`acsc` capability and a table would not. The shadow is **off by default** and costs one
extra refresh per stacked child per moving frame, for the reason its own comment gives.
Disposition of the two sections that asked for these:
`IRCITUILIB_PREDICTIVE_AUTO_COMPLETE_DESIGN.md` Appendix B. Evidence: `dev/probes/border/`.

**Tree construction and lookup** — `IrcitUiDataBind`, `IrcitUiScreenReset`,
`IrcitUiClone`, `IrcitUiFind`, `IrcitUiHitTest`, `IrcitUiAddChild`,
`IrcitUiAddButton`, `IrcitUiAddChildNode`, `IrcitUiNodeDestroy`. `IrcitUiNodeDestroy`
is the destructor G9 names: it deflates, detaches and frees a subtree, and refuses
both the reference model — whose child tables are static storage — and the live
screen root, which the engine holds pointers into.

**Input** — `IrcitUiDispatchKey`, `IrcitUiEditInput`, `IrcitUiButtonInput`,
`IrcitUiEmojiInput`, `IrcitUiInputAppend`, `IrcitUiMouseInit`,
`IrcitUiRadioItemInput`, `IrcitUiCheckItemInput`.

**The dispatcher** — `IrcitUiDispatcherInstance`, `IrcitUiDispatcherSetFocus`,
`Exit`, `Exited`, `PostKey`, `PostClick`, `PostLifecycle`, `IrcitUiRecordMouse`.
Declared with GATE 3 of the public-API track. They were exported from the archive
and absent from this list, which is precisely what G29 records. **`PostMessage` is
deliberately not among them**: §5 places it on the asynchronous surface, so it is
declared with the bridge rather than here, and G29 therefore closes in two steps
rather than one.

**The framework's eight listeners** — `IrcitUiInteractionBindDefaults`,
`IrcitUiDefaultOnKeyDown`, `OnClick`, `OnScroll`, `OnDraw`, `OnResize`,
`OnFocusChanged`, `OnInflate`, `OnDeflate`, and the `IrcitUiInteractionState` their
type is published as. They are the generator's binding targets (`IRCITUILIB_DESIGN.md` §4.5), which
is why
they must be nameable: `generated/ui_generated.c` names all eight in every node it
emits.

**Clipboard** — `IrcitUiClipCopy`, `IrcitUiClipPaste`, `IrcitUiClipClear`.

**Groups** — `IrcitUiGroupSelectedIndex`, `IrcitUiGroupSelectedValue`,
`IrcitUiGroupSelectedValues`.

**Image** — `IrcitUiImageDraw`, `IrcitUiImagePrepare`, `IrcitUiImageWrapW`,
`IrcitUiImageWrapH`.

**Emoji** — `IrcitUiAddEmojiTable`, `IrcitUiShowEmojiPicker`.

**Theme** — `IrcitUiThemeCreate`, `IrcitUiThemeDestroy`, `IrcitUiThemeSetBase`,
`IrcitUiThemeAddRule`, `IrcitUiThemeApply`, `IrcitUiThemeSetCurrent`,
`IrcitUiThemeCurrent`, `IrcitUiThemeApplyCurrent`, `IrcitUiSetThemeClass`,
`IrcitUiThemeClass`.

**Binding** — `IrcitUiBindClick`, `IrcitUiClickDispatch`, `IrcitUiBindingReset`.

**Diagnostics** — `IrcitUiVerifyLayout`, `IrcitUiDemoFill`.

**Not on this list: the asynchronous surface.** The bridge, the post, and the
alert have entry points but they live in `src/include/`, which is not installed —
so an out-of-tree application cannot reach them. §5 is the guide to what does
exist, where it lives, and what promoting it would take.

**Logging** — `IrcitUiLogInit`, `IrcitUiLogFini`, `IrcitUiLogEmit` and the five
`IRCITUI_LOG_*` macros. `LogInit` and `LogFini` are the only declarations in the
list that touch a third-party dependency, and they do it without naming it: the
signatures take a level and a path, never a `zlog_category_t`. `IRCITUILIB_DESIGN.md` §19.8 is the
whole of
that wiring.

The macros are the exception that proves the rule, and the facade is how it is kept.
They are the library's own logging surface, so promoting them — an application that
opens a log has to be able to write to it — would have put `zlog_info`, a
function-like macro, into an installed header. `IrcitUiLogEmit` is the answer:
the macros expand to it, it is the only thing that names zlog, and it stays in
`src/ui_log.c`. It carries the call site as parameters because zlog captures
`__FILE__`, `__func__` and `__LINE__` at the writing macro; the library's own log
format does not currently render them, which the declaration says plainly rather
than implying an improvement it does not deliver.

### 2.2 What is public, and what is not

| Public (`ircituilib/ui.h`) | Purpose |
|---|---|
| `IrcitUiPaletteStyle(slot)` | the style a role is drawn in |
| `IrcitUiPaletteSetSlot(slot, fg, bg, attr)` | redefine one role |
| `IrcitUiPaletteApplyProfile(profile)` | apply a whole palette |
| `IrcitUiPaletteProfileByName(name)` | a built-in profile, `"dark"` or `"light"` |
| `IrcitUiPaletteRepaint()` | make a palette change reach the screen |
| `IrcitUiPaletteSlotForName(name)` | a role's enumerator from its YAML spelling |
| `IrcitUiPaletteSetEffects(mask)` / `IrcitUiPaletteEffects()` | the effect switches |
| `IrcitUiPaletteCanopyActive()` | whether a canopy actually came up |
| `IrcitUiPaletteRestore()` | hand every borrowed register back |
| `IRCITUI_PALETTE_EFFECT_*` | the four effect bits |
| `IrcitUiPaletteSlot`, `IrcitUiPaletteProfile`, `IrcitUiPaletteSlotDef` | the types |
| `IrcitUiSetBorderStyle(style)` / `IrcitUiBorderStyleGet()` | the frame's glyph profile, one setting for the process (§8) |
| `IrcitUiSetShadow(on)` / `IrcitUiShadow()` | whether stacked popups cast a drop shadow, off by default (§8) |
| `IrcitUiBorderStyle` | the profile type, four values |

**Not public, and this is the honest state:** the fade API (`IrcitUiPaletteFadeStart` and its
`Style`, `Tick`, `Active`, `Handle`), the ramp API (`Acquire`, `Set`, `Style`, `Release`), the
canopy internals (`Ensure`, `Style`, `CanopyTextStyle`, `Pulse`, `Animating`), and
`IrcitUiPairCount` / `PairReset`. So **an application can enable the FADE bit and cannot start
a fade** — the guides below use no private call, and §6 says what is missing.

**And the frame's glyph table**, which is private on purpose: `IrcitUiBorderGlyphs(style)`
returns the six glyphs of a profile, and it is in `src/include/ui_palette_priv.h` rather than
`ui.h` because it is the *engine's* and the overlay's shared lookup, not something an
application draws with. A caller asks for a profile by name — `IrcitUiSetBorderStyle` — which
is a role rather than six codepoints, for the reason
`IRCITUILIB_PROGRESS_INDICATORS_DESIGN.md` §9.25 gives about the section's
`IrcitUiBorderProfile`.

**And the overlay's own surface, because one of the eight roles is only reachable through
it.** The notification box is not a node and joins no layout, so `overlay_action` has no
`theme_class` path — the only way to put something in that role is to raise a box with an
action. These are the calls; §7 is how to use them.

| Public (`ircituilib/ui.h`) | Purpose |
|---|---|
| `IrcitUiSnackbarShow(text, ms)` | raise a plain notification |
| `IrcitUiSnackbarShowAction(text, ms, label, hotkey, cb, user)` | raise one **with an action**: a label drawn in `overlay_action` and one key that runs `cb` |
| `IrcitUiSnackbarKey(key)` | offer a key to the overlay; non-zero means it took it |
| `IrcitUiSnackbarDismiss()` / `Active()` | take one away / ask whether one is up |
| `IrcitUiSnackbarAdvance()` / `Render()` | the two calls a frame loop makes, before and after the frame |
| `IrcitUiSnackbarActionCallback` | the hook's type — `void (*)(void *user_ptr)` |
| `IRCITUI_SNACKBAR_LENGTH_SHORT` / `_LONG` | 1500 and 2750 ms, the conventional hold times |

The overlay's teardown, `IrcitUiSnackbarDestroy`, is **not** on this list and is not in
`ui.h` at all: it is private, and `IrcitUiDestroyFrameworkContext` calls it, because a window
the framework owns is released by the framework.

### 2.3 The windowing seam's public surface

Three headers, one type each, in `include/ircituilib/window_driver/`:

| Header | Declares |
|---|---|
| `ircitui_window_type.h` | the opaque handles, the value types, the translation table, the capability bits, the two accessors |
| `ircitui_window_registry.h` | the opaque registry and its five calls |
| `ircitui_driver_registry_user_callbacks_type.h` | `IrcitUiDriverRegistryUserCallbacks` |

#### The translation table

Eighteen slots. A slot is NULL when a backend does not provide it — this is a
superset contract, and no implementation is obliged to fill it, so a caller
tests before calling.

| # | Slot | Maps to (ncurses) |
|---|---|---|
| 1 | `destroy` | `endwin` — the only non-`const` slot, because it consumes the handle |
| 2 | `clear_screen` | `werase(stdscr)` |
| 3 | `flush_hardware` | `wnoutrefresh` + `doupdate` |
| 4 | `get_terminal_dimensions` | `LINES` / `COLS` |
| 5 | `set_cursor_visibility` | `curs_set` |
| 6 | `handle_terminal_resize` | `resize_term` |
| 7 | `window_create_root` | `newwin` |
| 8 | `window_create_sub` | `derwin` |
| 9 | `window_destroy` | `delwin` |
| 10 | `window_resize` | `wresize` + `mvwin` |
| 11 | `window_move` | `mvwin` |
| 12 | `window_clear` | `werase` |
| 13 | `window_box` | `box` |
| 14 | `draw_char` | `setcchar` + `mvwadd_wch` |
| 15 | `draw_string` | `draw_char`, per decoded scalar |
| 16 | `move_window_caret` | `wmove` |
| 17 | `fetch_key` | `wgetch` — a poll, not a block |
| 18 | `fetch_mouse` | `getmouse` |

Every slot that operates on a driver takes it **`const`**. A backend's mutable
state lives behind the handle, in the backend's own allocation, not in the
handle itself. Only `destroy` consumes it.

`window_create_sub` takes no driver at all: the parent window already refers to
the context that allocated it, so a sub-window is created through its parent.
That is what lets two drivers be live at once without a process-wide "current
driver".

#### The value types

`IrcitUiWindowRect`, `IrcitUiWindowVideoAttr`, `IrcitUiWindowMouseAction` and
`IrcitUiWindowMouseEvent` are **defined here, with their fields visible**. That
is deliberate rather than an oversight: they cross the interface *by value*, so
their fields are the contract. There is no invariant to protect and nothing to
hide. The handles are the opposite case, and `IRCITUILIB_WINDOW_ABSTRACTION_DESIGN.md` §3 is about
them.

#### The capability bits

| Bit | Means |
|---|---|
| `IRCITUI_WINDOW_CAP_TRUE_COLOR` | honours 24-bit fg/bg rather than quantising to a palette |
| `IRCITUI_WINDOW_CAP_HARDWARE_DIM` | DIM is a real terminal attribute, not simulated |
| `IRCITUI_WINDOW_CAP_SUBWINDOWS` | native sub-windows; without it, `window_create_sub` is emulated |
| `IRCITUI_WINDOW_CAP_MOUSE` | reports packets through `fetch_mouse` |

Read through `IrcitUiWindowDriverCapabilities`. Test before asking for something
a backend may not have, rather than discovering the gap by calling an unbound
slot.

## 3. Lifecycle, bootstrap and the loop

The seam between the application and the loop: what an application calls to bootstrap the
framework, drive it, post into it, and tear it down — and the order those four have to
happen in.

### 3.1 The host's surface

**An application does four things with this subsystem: bootstrap it, drive it, post to it, and tear
it down.** The demo (`src/examples/ui_main.c`) is the worked example of all four.

```c
  /* 1. Bootstrap: the context owns the dispatcher, the ring and the pool. */
  IrcitUiContext *ctx = IrcitUiInitialiseFrameworkContext(IrcitUiDataBind, NULL);
  if (!ctx) return 1;

  /* 2. Install anything that crosses a thread -- BEFORE the loop, not from a callback. */
  IrcitUiDispatcherRegisterNetworkListener(IrcitUiDispatcherInstance(), sOnChunk, &g_app);

  /* 3. Drive it. This returns when IrcitUiDispatcherExit has been called. */
  IrcitUiMainLooper(ctx);

  /* 4. Stop and join your producers FIRST, then tear the framework down. */
  sStopAndJoinMyWorkers();
  IrcitUiDispatcherRegisterNetworkListener(IrcitUiDispatcherInstance(), NULL, NULL);
  IrcitUiDestroyFrameworkContext(ctx);
```

| Call | What it is |
|---|---|
| `IrcitUiInitialiseFrameworkContext(binder, registry)` | makes the context, the dispatcher, the ring and the scheduler pool |
| `IrcitUiMainLooper(ctx)` | the loop. **It no longer tears anything down** — the teardown is the application's, so it can quiesce its own threads first |
| `IrcitUiDispatcherInstance()` | the process's dispatcher. Also what a listener is handed as its `disp`, so this is how it is reached outside a callback |
| `IrcitUiDispatcherPostKey` / `PostClick` / `PostLifecycle` | **dispatch now**, on the calling thread, into the tree. Not queue posts |
| `IrcitUiDispatcherPostMessage` | **queue** one envelope. Drops on a full ring **and returns false**, so a caller that packed an owned pointer into the envelope can reclaim it |
| `IrcitUiDispatcherPostNetworkChunk` | queue one chunk of a stream, **waiting** for room. `IRCITUILIB_EVENTS_DESIGN.md` §6 |
| `IrcitUiDispatcherExit` / `Exited` | ask the loop to end; read it after a pass |
| `IrcitUiDestroyFrameworkContext(ctx)` | the dispatcher, then the pool, then the input queue |
| `IrcitUiEventDispatcherDestroy(disp)` | the pipeline alone, for an application that needs it gone before the rest |

**Three things that bite, in the order they bite:**

1. **A worker may only post a value.** It may not touch a node, a window or the tree — that is the
   whole of the contract, and nothing enforces it but the design.
2. **A result can outlive its widget.** Post by **node id**, not by pointer, for anything that
   reports more than once. `IRCITUILIB_EVENTS_DESIGN.md` §7.
3. **A message already posted cannot be withdrawn.** If it must not be acted on, the *consumer* has
   to refuse it — which is what the id resolution does.

## 4. The tree, the widgets and the factories

The factory route to a widget: the declarative `IrcitUiWidgetDef`, the defaults
`sApplyDef` fills in, the eight framework listeners it binds, and the two sharp
edges in `IrcitUiAddButton` and the close-id heuristic.

### 4.1 The C factory API

`IrcitUiWidgetDef` is the declarative struct for heap-allocated widgets.
`sApplyDef` defaults `id` to `"anon"`, `text` to `""`, `flags` to
`VISIBLE|ENABLED` when 0, `bdr` to `COLOR_WHITE` when 0, forces `gravity` /
`layout_gravity` to start, `orientation` to vertical, and all eight constraints to
unset.

It also **binds the eight framework defaults** — `IrcitUiInteractionBindDefaults`
at `src/ui_engine.c:2162`. A node built through the C factory therefore carries the
same eight entry points as one emitted by the generator, from the same list
(`DEFAULT_LISTENERS`, `tools/gen_ui.py:57-66`). The two construction routes are
equivalent in this respect by construction, not by coincidence.

It does **not** touch `margin`, `padding`, `layout_template`, `interactions_user`,
or `theme_class`. So a factory-built node gets framework behaviour and no
application policy, which is what the caller then writes.

| Function | Notes |
|---|---|
| `IrcitUiAddChild` | NULL on a NULL or non-container parent, a NULL def, or allocation failure. **Does not lay out or realize** — the caller must |
| `IrcitUiAddButton` | forces `kind=BUTTON` (a caller-supplied kind is discarded), ORs `VISIBLE｜ENABLED｜CLICKABLE` |
| `IrcitUiCreateFloatingWindow` | coerces non-container kinds to `LINEAR`; forces `VISIBLE｜ENABLED｜BORDER｜DRAGGABLE｜STACKED`; attaches to the screen root |

Two sharp edges:

- `IrcitUiAddButton` defaults both size modes to `WRAP` when `mode == 0 && size ==
  0`. Since `IRCITUI_SIZE_EXACT == 0`, an explicit EXACT-with-width-0 request is
  silently converted to WRAP.
- The framework button default treats any id **containing** the substring
  `"close"` as a close action, so an id like `"disclose"` would pop the layer.

## 5. The asynchronous seam

Where the async entry points live and which of them an out-of-tree application
can reach; the thread contract that makes the rest follow; futures, promises and
their completion traps; the bridge that lands a result on Thread 0; and
coroutines on the worker pool.

### 5.1 The asynchronous surface, and where it lives

Everything in this subsection is the async guide, and it starts with a warning
that is not a formality.

| Symbol | Header | Installed? |
|---|---|---|
| `IrcitUiMainLooper` | `include/ircituilib/main_looper.h` | **yes** |
| `IrcitUiInitialiseFrameworkContext` | `include/ircituilib/ircitui_context.h` | **yes** |
| `IrcitUiScheduler` | `include/ircituilib/ircitui_context.h` | **yes** |
| `IrcitUiDispatcherPostMessage` | `src/include/ui_event_dispatcher_priv.h` | **no** |
| `IrcitUiPostFutureContinuation` | `src/include/ui_async_bridge.h` | **no** |
| `IrcitUiBridgeShowImageResult` | `src/include/ui_async_bridge.h` | **no** |
| `IrcitUiWatchdogAlertShow` | `src/include/ui_watchdog_alert.h` | **no** |
| `IrcitUiFocusedNode` | `src/include/ui_priv.h` | **no** |

**An out-of-tree application cannot use the bridge today.** It can run the loop,
and it can hand the loop a binder; it cannot register a future continuation,
post a message, or raise the alert, because those entry points are not in
`include/`. The demo can only because it compiles against `src/include` — the
same private path it uses for `ui_priv.h`.

That is a gap, not a design. Promoting the four private symbols to a public
`ircituilib/ui_async.h` is the work; `IRCITUILIB_DESIGN.md` §21 does not yet
record it.

### 5.2 The thread contract

One rule, and everything else follows from it:

> **Thread 0 owns the tree. A worker posts; it never touches a node.**

Every layout pass, every paint, every mutation happens on the thread that runs
`IrcitUiMainLooper`. A worker that wrote to a node would race the thread drawing
it, and no locking makes that coherent — the frame would be composed from two
half-states.

So a worker has exactly one legal action: build a `MainLoopMessage`, call
`IrcitUiDispatcherPostMessage`, and return. The loop drains the ring on its next
pass and does whatever the message asks, on the right thread.

The call answers whether the envelope reached the ring. Most producers may ignore
that — the envelope is copied by value and carries nothing they own — but a caller
that packed an owned pointer into `status_message`, as LiveData's dispatch blocks
do, must reclaim it on `false`, because no dispatch will ever run to take it.

```
   worker thread                          Thread 0
   ─────────────                          ────────
   resolve / fetch / compute
        │  IrcitUiDispatcherPostMessage(disp, &msg)
        ▼
   LocklessRingBuffer (MPSC)  ──wake──►  poll() → drain → dispatch
```

### 5.3 Futures and promises

A promise writes a result; a future reads it. The promise owns the future until
it completes, and the caller holds a reference.

```c
UfsrvFuture *future_ptr = NULL;

/* 1. Create. The future is the handle; hold it, and release it when done. */
UfsrvPromise *promise_ptr = UfsrvPromiseCreate(&future_ptr);
if (!promise_ptr) return;                 /* out of memory */

/* 2. Register the continuation BEFORE anything can complete it. */
UfsrvFutureThen(future_ptr, on_result, context_ptr);

/* 3. Hand the promise to whatever will do the work. */
submit(promise_ptr, ...);

/* 4. Drop the caller's reference. The promise keeps the future alive. */
UfsrvFutureRelease(future_ptr);
```

Completion — exactly one of these, from whatever thread finished the work:

```c
UfsrvPromiseSetValue(promise_ptr, value_ptr, free_value);  /* success */
UfsrvPromiseSetError(promise_ptr, error_code);             /* failure */
```

**Ownership of the value.** `UfsrvPromiseSetValue` takes ownership: `value_ptr`
is released with `free_value` when the future is. Passing a borrowed pointer
with a `NULL` release function is legal and means "this outlives the future" —
which is what a string literal or a file-scope buffer is.

**The callback borrows.** `UfsrvFutureCallback`'s contract is explicit:

> Terminal continuation invoked when a future completes. **Borrows** the result —
> it must not modify `*result_ptr` or take ownership of the value; the future
> still owns it.

So a callback reads `result_ptr->value` and copies what it needs. It does not
free it, does not write through it, and does not keep the pointer.

**Completing a promise releases the future.** This is the trap. After
`UfsrvPromiseSetValue` returns, the future may already be gone; a subsequent
`UfsrvFutureIsReady(future_ptr)` is a use-after-free. Completion is something
your own code must observe — a flag you own — not something you poll the future
for afterwards.

**And it destroys the promise itself.** A second `UfsrvPromiseSetValue` on the
same promise is a use-after-free inside ufsrvrxlib: the first completion frees
the promise at `ufsrv_future.c:273` and the second writes through it at `:255`.
One completion, one promise — there is no error to observe if you get it wrong,
only heap corruption. `tests/bridge_stress.c` found this by doing it, and records
it rather than guarding it (`IRCITUILIB_DESIGN.md` §20.5).

**A terminal continuation runs exactly once**, on the thread that completed the
promise. `UfsrvFutureThen` on a promise-backed future is terminal;
`UfsrvFutureMap` and friends return a *new* future and can be chained.

### 5.4 The bridge: getting a result onto Thread 0

```c
typedef struct {
  IrcitUiEventDispatcher *dispatcher;   /* where to post */
  IrcitUiNode            *target_node;  /* the node the result belongs to */
  const char             *target_id;    /* its id, captured while it is alive */
  int                     kind;         /* IRCITUI_BRIDGE_TEXT or _IMAGE */
  void                   *user_context; /* yours; the bridge never reads it */
} IrcitUiBridgeContext;

void IrcitUiPostFutureContinuation(UfsrvFutureResult *result_ptr, void *context_ptr);
```

The context is **owned by the continuation** — it frees it on the way out. One
context per future, because a terminal continuation runs once:

```c
IrcitUiBridgeContext *bridge_ptr = calloc(1, sizeof(*bridge_ptr));
bridge_ptr->dispatcher  = disp_ptr;
bridge_ptr->target_node = IrcitUiFocusedNode();
bridge_ptr->target_id   = target_id_of_that_node;   /* may be NULL */
bridge_ptr->kind        = IRCITUI_BRIDGE_TEXT;

UfsrvFutureThen(future_ptr, IrcitUiPostFutureContinuation, bridge_ptr);
```

**`target_id` is not optional in spirit.** On the failure path the bridge
reports by **id** and clears `target_node`, because a result can outlive the
widget it was fetched for and a freed pointer is indistinguishable from a live
one on the receiving side. The id survives the node — nodes borrow their ids
from the generated table (`IRCITUILIB_DESIGN.md` §8.9).

What Thread 0 then does, per `kind` and outcome:

| `kind` | outcome | Thread 0 |
|---|---|---|
| `IRCITUI_BRIDGE_TEXT` | `status_code != 0` | raises the watchdog alert, named by `status_message` |
| `IRCITUI_BRIDGE_TEXT` | success | copies the string into the target's owned `ebuf`, repoints `common.text`, requests layout |
| `IRCITUI_BRIDGE_IMAGE` | `status_code != 0` | raises the alert |
| `IRCITUI_BRIDGE_IMAGE` | success | `IrcitUiBridgeShowImageResult(path)` — raises a floating window showing the PNG |

**A text result only lands on an `IRCITUI_EDIT_TEXT` node.** That is the only
kind that owns string storage (`IRCITUILIB_DESIGN.md` §10.5). Anything else has
nowhere to put it, and the framework logs and drops rather than assigning a
pointer that dangles when the loop's message slot is reused.

### 5.5 Coroutines on the worker pool

```c
UfsrvCoroutine *UfsrvCoroutineCreate(UfsrvCoroutineEntry entry, void *arg_ptr);
void            UfsrvCoroutineResume(UfsrvCoroutine *coroutine_ptr);
void            UfsrvCoroutineYield(void);
void            UfsrvCoroutineExit(void);
void *          UfsrvCoroutineGetArg(void);   /* inside the entry */

bool UfsrvSchedulerPoolSubmit(UfsrvSchedulerPool *pool_ptr,
                              UfsrvSchedulerJobCallback callback, void *context_ptr);
UfsrvSchedulerPool *IrcitUiScheduler(void);   /* the framework's pool */
```

`UfsrvCoroutineEntry` is `void (*)(void)` — it takes no argument and reads its
own through `UfsrvCoroutineGetArg()`.

**Every yield needs a resumer.** `UfsrvCoroutineYield` hands control back to
whoever resumed the coroutine. If the job that owns it returns after the first
resume, the coroutine is suspended forever: the promise is never completed, the
continuation never runs, and nothing arrives on Thread 0. This is the single
most common way to write a coroutine that silently does nothing.

The working shape — create, resume to the first yield, then resume until the
work reports:

```c
static void sJob(void *context_ptr)
{
  DemoTask *task_ptr = context_ptr;
  /* Read into locals BEFORE the first resume: the body frees the task. */
  UfsrvCoroutineEntry entry    = task_ptr->entry;
  int *               done_ptr = task_ptr->done_ptr;
  int                 guard    = 0;

  UfsrvCoroutine *co = UfsrvCoroutineCreate(entry, task_ptr);
  if (!co) { free(task_ptr); return; }

  UfsrvCoroutineResume(co);                    /* runs to the first yield */
  while (!*done_ptr && ++guard < 500) {
    nanosleep(&pause, NULL);                   /* the wait the worker is doing */
    UfsrvCoroutineResume(co);                  /* deliver the next stage */
  }
  free(done_ptr);
}
```

and the body signals its own completion, last thing before it exits:

```c
static void sEntry(void)
{
  DemoTask *task_ptr = UfsrvCoroutineGetArg();

  UfsrvCoroutineYield();                       /* suspend; worker is freed */

  UfsrvPromiseSetError(task_ptr->promise, -1); /* or SetValue */
  *task_ptr->done_ptr = 1;
  free(task_ptr);
  UfsrvCoroutineExit();
}
```

Note what the completion signal is **not**: it is not `UfsrvFutureIsReady`. The
promise frees the future on completion, so polling it is a use-after-free — the
flag is a small allocation the job owns and the body writes.

## 6. The palette and the roles

Roles, the profiles that define them, and the effects that ride on the registers behind
them. A role is named in a layout's `theme_class`, set on a widget definition in C, or
asked for directly, and it resolves to a colour pair per paint. These five subsections are
the five ways an application meets that seam.

### 6.1 Style a widget by role from a layout

The usual way, and no C at all. Give the node a `theme_class` naming one of the eight roles:

```yaml
- id: panel_left_note
  type: text_view
  text: click the indicator
  theme_class: text_muted
```

The generator validates the name (**C23**), so a typo fails the build rather than producing a
node that silently keeps its own colours. **A class cannot be combined with `text_colour` or
`bg_colour`** — the slot supplies the surface, so one of the two would be ignored, and the
rule refuses rather than allowing a key that does nothing. `border_colour` is still allowed:
the eight roles name a surface and a border is not one of them.

### 6.2 Style a widget by role from C

For a node you build rather than inflate, set the same field on the definition:

```c
IrcitUiWidgetDef title;
memset(&title, 0, sizeof title);      /* REQUIRED — see the rule below */
title.id          = "alert_title";
title.kind        = IRCITUI_TEXT_VIEW;
title.text        = "SYSTEM PROTECTION WARNING";
title.theme_class = "text_error";
IrcitUiAddChild(parent, &title);
```

> **`theme_class` is borrowed and must be zero-initialised.** The node keeps your pointer and
> never copies the string, so it must outlive the node — a literal, or storage you own. And a
> def that is *not* zeroed carries a garbage pointer that is indistinguishable from a good
> one, which the palette will then read. `memset` the def, or use a designated initialiser,
> which zeroes the fields you leave out.

### 6.3 Ask for a role's style directly

For something the library does not draw — your own window, your own overlay:

```c
WINDOW *w = newwin(5, 40, 2, 2);
wbkgd(w, IrcitUiPaletteStyle(IRCITUI_SLOT_PANEL));
werase(w);
mvwaddstr(w, 1, 2, "on the panel role");
wnoutrefresh(w);
```

The return is a composite `chtype` — attributes and colour pair — ready for `wbkgd`,
`wattron` or to be OR'd into a cell. Resolve it **per paint** rather than caching it: that is
what makes a later redefinition arrive on the next frame, and the lookup is an array index
plus a registry hit.

To go the other way — a name from a config file or a command line — use
`IrcitUiPaletteSlotForName`, which returns the enumerator or `-1`:

```c
int slot = IrcitUiPaletteSlotForName(argv[1]);
if (slot < 0) { fprintf(stderr, "no such role: %s\n", argv[1]); return 1; }
```

### 6.4 Redefine a role, and switch profiles

```c
/* One role. -1 means "leave this one"; for the attributes the sentinel is (chtype)-1,
   because A_NORMAL is 0 and a role has to be able to become plain again. */
IrcitUiPaletteSetSlot(IRCITUI_SLOT_TEXT_ERROR, 208, 234, A_BOLD);
IrcitUiPaletteRepaint();                       /* see the rule below */

/* A whole palette. */
const IrcitUiPaletteProfile *p = IrcitUiPaletteProfileByName("light");
if (p) {
  IrcitUiPaletteApplyProfile(p);
  IrcitUiPaletteRepaint();
}
```

> **`IrcitUiPaletteRepaint()` is not optional and not a formality.** A slot resolves to a
> colour *pair*, and a pair's colours live in the cells that carry it — so a redefinition
> reaches the screen only when those cells are drawn again. Skip it and the palette changes
> while the screen keeps the old colours, until something unrelated happens to dirty a node.
> This is the single most likely mistake with this API; it was made while building it.

**Writing your own profile.** The safest pattern is to start from a built-in, because a
partially-specified profile leaves the slots you did not mention at zero — black on black:

```c
IrcitUiPaletteProfile mine = *IrcitUiPaletteProfileByName("dark");   /* all eight, defined */
mine.name = "mine";
mine.slots[IRCITUI_SLOT_TEXT_ERROR] = (IrcitUiPaletteSlotDef){196, 16, COLOR_RED, COLOR_BLACK, A_BOLD};
IrcitUiPaletteApplyProfile(&mine);
IrcitUiPaletteRepaint();
```

Each slot definition carries **both colour levels** — `fg256/bg256` for a 256-colour terminal
and `fg8/bg8` for an eight-colour one. They are not the same numbers scaled: the eight-colour
entry is the role re-chosen for a terminal with fewer colours and attributes to spare. A
profile that specified only the 256-colour pair would leave an eight-colour terminal with a
role it cannot render.

### 6.5 The effects

Off by default, and that default is a correctness decision rather than taste: writing any
colour register makes ncurses reset the terminal's palette at exit, so a user's hand-tuned
scheme does not survive the program. Opt in explicitly:

```c
IrcitUiPaletteSetEffects(IRCITUI_PALETTE_EFFECT_CANOPY | IRCITUI_PALETTE_EFFECT_PULSE);
```

| Bit | Needs | Notes |
|---|---|---|
| `GRADIENT` | nothing | a horizontal ramp behind the notification box |
| `CANOPY` | a container wearing the **`surface`** role | a vertical ramp; nodes wearing `text` get the pre-blended foreground |
| `PULSE` | `CANOPY` | animates the canopy; the loop takes its fast cadence only while this is live |
| `FADE` | — | **the bit is public but the API is not** (§2.2) |

Check what actually happened rather than assuming:

```c
if (IrcitUiPaletteEffects() & IRCITUI_PALETTE_EFFECT_CANOPY && !IrcitUiPaletteCanopyActive())
  fprintf(stderr, "this terminal cannot lend colour registers; no canopy\n");
```

At exit, if you care about the user's palette:

```c
endwin();
IrcitUiPaletteRestore();     /* AFTER endwin: OSC 104 is emitted at exit, and this must follow it */
```

It restores what ncurses **believed** the registers held, not what the emulator had, so treat
it as an approximation.

## 7. The notification overlay and its action

The box that rises out of the footer — a `WINDOW` rather than a node, raised by a call and
joined to no layout. Its action is the only way to put anything in the `overlay_action`
role.

### 7.1 The notification overlay, and its action

The box that rises out of the footer. It is a `WINDOW` and not a node — it joins no layout,
takes no focus, and moves nothing to make room for itself — so it is raised by a call rather
than declared. Its authoritative account is `IRCITUILIB_DESIGN.md` §11.8; this is how to use
it, and the part of it that is colour.

```c
/* A plain notification. The text is COPIED: the box outlives this buffer. */
IrcitUiSnackbarShow("settings saved", IRCITUI_SNACKBAR_LENGTH_SHORT);
```

**With an action**, which is what puts something in the `overlay_action` role:

```c
static void
sOnUndo(void *user_ptr)
{
  AppCtx *ctx = user_ptr;          /* whatever you registered */
  restore_the_thing(ctx);
  IrcitUiSnackbarShow("restored", IRCITUI_SNACKBAR_LENGTH_SHORT);   /* optionally report it */
}

/* ... */
if (!IrcitUiSnackbarShowAction("message deleted", IRCITUI_SNACKBAR_LENGTH_LONG,
                               "[UNDO]", 15 /* ^O — see the rule below */, sOnUndo, &ctx)) {
  /* The affordance is NOT on screen. The message may still be up: a refused ACTION is not
     a refused MESSAGE. If you have already hidden the data behind it, put it back now —
     this return value is the only way you will know. */
  restore_the_thing(&ctx);
}
```

Six things about that call are decisions rather than details, and each has a reason that is
not visible from the signature:

| | |
|---|---|
| **The action exists iff the callback is non-NULL** | There is no `has_action` flag and no magic hotkey value: key `0` is a real key (`^@`), so a sentinel would make one legal key unaskable. Given a callback, a **label is required** — a hotkey with nothing drawn to advertise it is refused, on the tree's rule that a key resolving to nothing is a trap rather than a reservation (`IRCITUILIB_PROGRESS_INDICATORS_DESIGN.md` §4) |
| **A printable hotkey is refused** (`32`–`127`) | The loop's edit-field fast path hands every printable character straight to a focused edit field without reaching the dispatcher, so such a hotkey would fire only while no field held focus — intermittently, with nothing to explain why |
| **Five control codes never arrive either** | `^C` (3), `^Q` (17), `^S` (19), `^Z` (26), `^\` (28). The window-driver contract has the factory run `cbreak()`, not `raw()`, so `ISIG` and `IXON` stay set and the line discipline keeps those five. **A hotkey bound to one of them is dead and looks exactly like a live one.** 28 of the 31 C0 codes are swept in `dev/probes/snackbar/p_hotkeys.c`. This is not a palette fact, but it is the mistake this feature's own specification made — it binds its undo to `^Z` |
| **Enter's three spellings are one key** | `10`, `13` and `KEY_ENTER` normalise to one value, so a caller who maps Enter gets it whichever the terminal sends |
| **The callback runs after the box is dismissed** | So raising a second notification from inside it works — which is what the code above does. The reverse order discards the notification the callback raised |
| **The label is clipped by character, not byte**, and the message is clipped to leave it a one-column gap | The box is wide-character throughout; a byte clip would hand the window half a UTF-8 sequence |

**Nothing is needed to make the key work.** `IrcitUiDispatchKey` offers every key to the
overlay before anything else — before the recycler's edit intercept and before the whole
routing matrix — so an application that dispatches keys through the engine has nothing to do.
An application routing keys its own way asks the same question directly and must not also
deliver a key the overlay took:

```c
if (!IrcitUiSnackbarKey(key)) IrcitUiDispatchKey(IrcitUiScreen.root, key);
```

**A live fade takes precedence over the label's role, deliberately.** `sStyleOf` returns the
fade's register pair when one is in flight, and that applies to the action label as well as
to the box — so a box dissolving through a fade takes its label with it rather than leaving a
bright `[UNDO]` on a dying surface. The reason is that the action is already dead by then:
`IrcitUiSnackbarKey` answers only while the box is rising or settled, so a label that stayed
legible during the fall would be advertising something the API has stopped honouring. If you
want the label to keep its role colour during a fade, the fade is the wrong dismissal —
`IrcitUiSnackbarDismiss()` is the one that leaves the label in place while the box falls.

**Restyling the label**, which needs no repaint demand — the box rewrites its own lines every
pass, so a slot redefinition reaches it on the next frame
(`IRCITUILIB_PROGRESS_INDICATORS_DESIGN.md` §1):

```c
IrcitUiPaletteSetSlot(IRCITUI_SLOT_OVERLAY_ACTION, 214, 236, A_BOLD);
/* no IrcitUiPaletteRepaint() needed for the box; the tree still needs one for its own nodes */
```

**An application driving its own frame loop** does what `IrcitUiMainLooper` does, in this
order — and the `Advance` return is not optional, because the box leaves cells no change set
covers:

```c
if (IrcitUiSnackbarAdvance() && IrcitUiScreen.root)
    IrcitUiInvalidate(IrcitUiScreen.root);   /* it moved or left; the tree owes a repaint */
IrcitUiRenderFrame();
IrcitUiSnackbarRender();                     /* after the tree — that ordering IS the z-order */
doupdate();
```

## 8. The frame profile and the popup shadow

Two process-wide settings that sit beside the effects: the frame's glyph profile, and
whether stacked popups cast a drop shadow. Both reach the whole tree, and both need a
repaint.

### 8.1 The frame's profile, and the popup's shadow

Two settings for the whole process, both in `src/ui_palette.c` beside the effects, both
needing a repaint:

```c
IrcitUiSetBorderStyle(IRCITUI_BORDER_ROUNDED);   /* or THICK, DOUBLE, STANDARD */
IrcitUiPaletteRepaint();

IrcitUiSetShadow(1);                              /* stacked popups cast a drop shadow */
IrcitUiPaletteRepaint();
```

`IRCITUI_BORDER_STANDARD` is the default and is **not** merely "the square one" — it is the
whole of the behaviour that shipped before the enum existed, drawn through the same `box()`
call, so an application that never asks gets a frame byte-for-byte as it always had. Every
other profile is drawn glyph by glyph, and it reaches **both** framed things: the engine's
containers and the notification box, which is not a node and asks for the glyphs itself.

The shadow is the second consumer of `IRCITUI_SLOT_TEXT_MUTED`, and unlike a slot it is
**off by default** and costs a refresh: the strip is written into the **parent's** cells, so
the parent must be painted and pushed again whenever a popup moves. What it reaches is every
node carrying `IRCITUI_F_STACKED`, which is what `IrcitUiCreateFloatingWindow` builds. What it
does not reach: the notification overlay, which has no parent window to cast onto. See
`IRCITUILIB_PROGRESS_INDICATORS_DESIGN.md` §9.27
for the case that is covered by the demo rather than by a probe, and
`IRCITUILIB_PROGRESS_INDICATORS_DESIGN.md` §10 for what was refused.

The demo is the worked example: `^B` cycles the profile and `^H` toggles the shadow, with the
current state written into the footer's status label so the two can be compared without
remembering which press did what.

## 9. The recycler

The recycler's three remaining surfaces: heterogeneous rows and the headers pinned
over them, the selection and its cursor, and rows edited in place. Each is built;
each carries its own costs and its own rules, stated with it.

### 9.1 The view types and pinned headers

Two capabilities on top of `IRCITUILIB_DESIGN.md` §11.7's list, and they are one mechanism seen
twice.

**Heterogeneous rows** let one dataset contain rows of different shapes: an info
line, a warning line, a section divider, each cloned from its own template.
**Pinned headers** then take the rows of one nominated type and keep the one
covering the top of the viewport drawn there, over the rows scrolling under it.

The second is built on the first, and it could not be built without it: the
engine has no view of the dataset, so the only thing it can be told is *what type
is the row at this position* — which is exactly what heterogeneous rows
introduce.

#### The vocabulary: `type_id`

A `type_id` is **a tag the application invents**. It is not an index, a count, a
row number or a dataset position. The engine only ever compares it for equality.

```yaml
- id: log_list
  type: recycler
  row_height: 1
  view_types:
    - type_id: 0
      template: log_row.yaml
    - type_id: 1
      template: log_row_warn.yaml
    - type_id: 10                    # arbitrary. See below.
      template: log_row_section.yaml
  sticky_headers: 10                 # rows of type 10 also pin
```

**The numbers are yours.** `0`, `1`, `10` above are a convention — small values
for the common shapes, a gap before the special ones — and nothing depends on
them. §15.1.1 of `IRCITUILIB_DESIGN_REFACTOR_V1.md` uses `10` for a section
header, and the reference layout follows it so the doc's example and the working
layout agree. Reordering the list changes nothing: the lookup is a comparison,
not a position.

Three things must agree, and `type_id` is the name they share:

| | |
|---|---|
| the registry | `view_types` says **what a row of that type looks like** |
| the application | `get_item_view_type` says **which positions are that type** |
| `sticky_headers` | says **that type's rows also pin** |

An application's own enum is the intended use — `LOG_INFO`, `LOG_WARN`,
`SECTION` — because the engine never interprets the value.

#### C21 — what a registry may say

| Rule | Why |
|---|---|
| recycler only | no other kind has rows to clone |
| mutually exclusive with `item_template` | both answer *what shape is one row*; a node asking both would leave the answer to whichever the engine read first |
| not empty | an empty registry **is** the homogeneous case, which `item_template` spells plainly |
| `type_id` a unique non-negative integer | duplicates would be resolved by position, and which won would be an implementation detail a layout depends on; the non-negative rule is the *declaration's* and has no engine reason — see the note in `ircit_ui_view_type_mapping_type.h` |
| entries carry only `type_id` and `template` | anything else is a typo that would resolve to nothing |

#### C22 — what `sticky_headers` may say

`sticky_headers: <type_id>`, on a recycler, requires `view_types`, and the value
must be a `type_id` that registry declares. It is an id rather than a boolean
because the layout already declares the ids a few lines above — the layout knows
which one is a divider, and making the application say it again would be a second
copy of a fact that can disagree.

A dataset where the application never returns that type is **not** an error: no
dividers means nothing pins and the header stays hidden. That is the state every
list begins in.

#### Binding it

The adapter is `IRCITUILIB_DESIGN.md` §11.7's, plus one callback:

```c
IrcitUiRecyclerAdapter adapter = {0};   /* ← zero-init: see the rules below */

adapter.item_count         = 10000;
adapter.user_dataset       = my_data;
adapter.get_item_view_type = my_discriminator;   /* optional; NULL = homogeneous */
adapter.on_bind_view       = my_binder;
adapter.on_item_clicked    = my_click_handler;   /* optional */

IrcitUiRecyclerSetAdapter(IrcitUiFind(IrcitUiModelRoot(), "log_list"), &adapter);
```

| Callback | Contract |
|---|---|
| `get_item_view_type(position, dataset)` | returns the type of that position. Called once per pooled slot per layout pass, so it must be cheap, side-effect free and **stable** — a tag that changed between two calls in one pass would rebuild that slot twice |
| `on_bind_view(node, position, dataset)` | fills a row. Called on **every pass** for every visible row, including rows whose position has not changed, so it must be **idempotent**. The node is **borrowed** — never retain it |
| `on_item_clicked(node, position, dataset)` | a click or `Enter` on a row. `position` is the stable thing; the node is borrowed |

**There is no `view_type` argument on `on_bind_view`.** The framework needs the
type to choose a template; the binder does not, because the application wrote the
discriminator and can re-derive what it already knows. Adding a parameter to a
published public callback for that convenience was not worth breaking every
existing binder — so keep **one** predicate and call it from both places.

Install on the **model** node, not the screen tree's: a screen tree is a clone of
the model, and `IrcitUiScreenReset` rebuilds it on every start.

#### Filling a row

```c
static void
my_binder(IrcitUiNode *row_ptr, size_t position, void *user_dataset)
{
  IrcitUiNode *label = IrcitUiFind(row_ptr, "row_text_line");
  if (label) IrcitUiNodeSetText(label, my_text[position]);
}
```

`IrcitUiNodeSetText` **borrows** the string: the node never frees it and nothing
copies it. It must outlive the node, and a row is rebound many times — so a stack
buffer is a `stack-use-after-return` inside the painter, a long way from the
cause. Keep the strings in static or heap storage the application owns.

`IrcitUiNodeSetText` is the **only** node setter in the public header.

#### What pinning does

One node, cloned from the sticky type's template, held as the **last child** of
the recycler and placed at the top of its content box. The rows keep indices
`0..n-1` and their bindings are untouched, so the header never displaces a row:
the entry at the top of the viewport is still shown at its own slot, with the
pinned copy drawn over it.

Overlay, not a reserved row. Two consequences worth knowing:

- **The pinned copy is a second `on_bind_view` call for the same position.** Your
  binder will receive the divider's position twice — once for the row in the flow
  of the list, once for the copy pinned above it — and the same branch fills both,
  because both are clones of one template.
- **It is repainted whenever anything under it repaints.** The change set is
  geometric, so a row repainting beneath a clean header marks the header paint-stale
  as well as refresh-stale. That is what makes the overlay work without the header
  repainting every frame.

  **A refresh alone would not be enough**, and the distinction is worth stating
  because the obvious reading of ncurses is wrong. The header and the row beneath
  it are both `derwin`s of the list, placed at the same offset, so they *share*
  their cells: a node that repaints writes the header's cells, and a header that is
  merely pushed copies those same cells back out. It wins only by rewriting its own
  text, which is a repaint. Measured on an editing row (see §9.3): with the
  refresh-only rule the composite over the two reads the row; with the repaint rule
  it reads the header. A frame that relayouts hides the difference, because
  `IrcitUiRealize` rebuilds every window and clears the painted flag on the whole
  tree — which is why this was not visible until something repainted a row without
  a relayout.

**The push `IRCITUILIB_DESIGN_REFACTOR_V1.md` §15.3 describes is not implemented, and is inert at
one fixed row
height**: a divider and the pinned header cannot overlap, because the moment a
divider reaches the top slot the scan below pins *it* instead — every transition
is a clean swap. It becomes live only when a header and a row can differ in
height, which is `IRCITUILIB_DESIGN_REFACTOR_V1.md` §22.2.

#### Styling

| Route | How | Reaches |
|---|---|---|
| the template | `text_colour` / `bg_colour` / `border_colour`, and any widget the template contains | the divider row **and** the pinned copy — one template, so they cannot drift |
| a theme rule | `IrcitUiThemeAddRule` with `IRCITUI_THEME_ID` (or `_KIND` / `_CLASS`) | both, likewise |
| `IrcitUiNodeSetText` in the binder | text only | both |

**The pinned copy cannot be styled differently from the divider row.** Both
arrive at `on_bind_view` with the same node shape, and when the divider is on
screen, the same position.

There is a discriminator, and it is measured rather than guessed:
`IrcitUiRecyclerRowAt` refuses the header on purpose, so it can never return it —

```c
int is_the_pinned_copy = (IrcitUiRecyclerRowAt(my_list, position) != row_ptr);
```

true for the pinned header, false for the row in the flow of the list. It needs
the application to hold the list pointer and **it is not a documented contract**;
it is a consequence of an internal guard. If differing styles are wanted, that
wants a first-class answer instead.

#### The C API

```c
int IrcitUiRecyclerSetAdapter(IrcitUiNode *node_ptr, const IrcitUiRecyclerAdapter *adapter_ptr);
int IrcitUiRecyclerSetViewTypes(IrcitUiNode *node_ptr, const IrcitUiViewTypeMapping *maps_ptr, int count);
int IrcitUiRecyclerSetStickyHeader(IrcitUiNode *node_ptr, int type_id);
```

`IrcitUiRecyclerSetViewTypes` takes a **borrowed** array that must outlive the
list, exactly as `item_template` does. It refuses a negative count, a count with
no array, and any entry whose `template_id` is NULL or empty — an entry with no
template resolves to nothing, so its slots would go blank every pass with nothing
said about why.

**`IrcitUiRecyclerSetStickyHeader` is the C counterpart of the `sticky_headers`
key.** A tree built entirely through the C API can pin, and this is how:

```c
static const IrcitUiViewTypeMapping s_types[] = {
  { LOG_INFO,    "log_row"         },
  { LOG_SECTION, "log_row_section" },
};

IrcitUiNode *list = IrcitUiRecyclerCreate("log_list", "log_row", 1);
IrcitUiRecyclerSetAdapter(list, &adapter);
IrcitUiRecyclerSetViewTypes(list, s_types, 2);      /* the registry FIRST */
IrcitUiRecyclerSetStickyHeader(list, LOG_SECTION);  /* then the pin */
```

**The order is required, not incidental.** The pin names a type the registry
declares, so `SetStickyHeader` refuses a `type_id` the registry does not hold —
accepted and then quietly blank is the failure C22 (`IRCITUILIB_DESIGN.md` §8.2) exists to keep a
layout from
expressing, and there is no reason a call should be allowed it either. A negative
`type_id` turns pinning off and is always accepted.

Turning it **off releases the header node**, returning its slot to the pool. The
node is not left hidden: `sPoolRowCount` subtracts the header from the child
count, so a header kept in the array with the feature off would make the pool one
row short for the rest of the tree's life.

Both setters write **the node and the state**. The node is what a clone inherits
— a screen tree is rebuilt from the model on every `IrcitUiScreenReset` — and the
state is what this instance reads. Writing one alone is the mistake to avoid:
`sAllocWidgetState` copies the node's members once, at allocation, so a later
node-only write reaches every future clone and not the tree in front of you. This
is the same two-write shape `SetViewTypes` needs, and the same one
`IrcitUiRecyclerCreate` needs for the `-1` that means "off".

#### What it costs

- **Crossing a type boundary frees and re-clones the slots that crossed it.** The
  pool is not partitioned into per-type buckets: a slot whose shape does not match
  the position it is about to show is rebuilt in place. Inside one run of a single
  type nothing is rebuilt at all. The pool's *size* never changes with
  heterogeneity — still viewport height plus `IRCITUILIB_DESIGN_REFACTOR_V1.md` §13's cache margin —
  so the list
  stays virtual.
- **The section scan is O(distance to the divider).** One layout pass costs the
  section's length in discriminator calls. At a divider every 25 rows that is
  nothing; a list with one divider at position 0 and fifty thousand rows walks the
  whole prefix on every pass spent at the bottom. Measured as **unmeasured**: the
  cost is stated and not yet bounded.

#### The rules that bite

1. **Zero-initialise the adapter** — `IrcitUiRecyclerAdapter a = {0};`, or a
   designated initialiser, which zeroes what it does not name. Every field but
   `item_count` is optional and "not set" is spelled NULL, so an indeterminate
   member is not a default: for `get_item_view_type` it is **a garbage function
   pointer the engine will call**. The exposure is bounded — the engine reaches it
   only when the node carries a registry, which only a layout declaring
   `view_types` produces — so a list bound the ordinary way never calls it.
2. **A `type_id` is arbitrary; the three places that name it must agree.** A
   registry entry no position ever returns is legal and does nothing.
3. **The binder is called every pass, including for positions that did not
   change.** Idempotent, not incremental.
4. **Row nodes are borrowed and rebound.** Keep the `position`, look the value up
   in your own data. Anything stored on a row follows the pool slot, not the
   dataset entry.
5. **Strings handed to `IrcitUiNodeSetText` must outlive the node.** Static or
   heap; never a stack buffer.
6. **Configure the model, not the screen tree.** `IrcitUiScreenReset` clones
   afresh on every start.
7. **A position with no registered template leaves its slot blank** rather than
   being given another type's template. A row of the wrong shape is a wrong
   picture; a gap is not.
8. **The pinned copy and the divider row are indistinguishable in the binder**
   without the undocumented `IrcitUiRecyclerRowAt` test above.
9. **`SetStickyHeader` needs the registry to exist first**, and refuses a type it
   does not declare. `SetViewTypes`, then `SetStickyHeader` — in that order.
10. **Every field the engine reads as "off by -1" is written, never omitted.**
    A member whose zero value is a legal one cannot be left to
    zero-initialisation: `sticky_header_type`'s zero is a valid `type_id`, so a
    node defaulting to it pins type 0's rows with nobody having asked. The
    emitter writes `-1` explicitly for exactly this reason, `mark_fg` did it
    first, and `IrcitUiRecyclerCreate` sets both the node and the state.

### 9.2 Selection

Marking rows. A row's mark is **derived from its dataset index**, never stored on
the row node — the same rule `IRCITUILIB_DESIGN_REFACTOR_V1.md` §14's row shapes follow and for the
same reason: rows
are pooled and rebound as the list scrolls, so a mark kept on a row would follow
the pool slot and appear on a different entry after any scroll.

**Read §9.1 first if you have not.** Selection shares the recycler's adapter,
its cursor and its view-type registry, and several rules below are that section's
rules seen from another side.

#### The mode, and what it gates

```c
typedef enum {
  IRCITUI_SELECT_SINGLE = 0,   /* the zero value, deliberately */
  IRCITUI_SELECT_MULTI,
  IRCITUI_SELECT_NONE
} IrcitUiSelectionMode;
```

`SINGLE` is `0` on purpose: a state that was never configured must select one row
at a time, and a hand-built state is `calloc`'d — putting `NONE` at zero would
make every such state silently refuse to mark anything, which reads as a broken
list rather than a setting.

| Mode | The selection is | `SelectRange` |
|---|---|---|
| `NONE` | always empty; marking is refused | refused |
| `SINGLE` | **the cursor** — it follows wherever the cursor goes | refused |
| `MULTI` | independent of the cursor; marks stay where they were put | accepted |

Set it on the **model** node like the adapter, so every screen tree cloned from it
inherits:

```c
IrcitUiRecyclerSetSelectionMode(IrcitUiFind(IrcitUiModelRoot(), "log_list"),
                                IRCITUI_SELECT_MULTI);
```

`SINGLE` is enforced inside `IrcitUiRecyclerSelect` and nowhere else. Every input
funnels through that one function, so a rule written once cannot hold on the
click path and not on the `SPACE` path — which is what a mode flag invites when
each input site tests it for itself.

#### The API

```c
int    IrcitUiRecyclerSelect(IrcitUiNode *node_ptr, size_t index, int selected);
int    IrcitUiRecyclerSelectRange(IrcitUiNode *node_ptr, size_t start, size_t end, int selected);
int    IrcitUiRecyclerIsSelected(IrcitUiNode *node_ptr, size_t index);
void   IrcitUiRecyclerClearSelection(IrcitUiNode *node_ptr);
size_t IrcitUiGetSelectedIndices(IrcitUiNode *node_ptr, size_t *out_ptr, size_t maxn);
int    IrcitUiRecyclerSetSelectionMode(IrcitUiNode *node_ptr, IrcitUiSelectionMode mode);
IrcitUiSelectionMode IrcitUiRecyclerSelectionMode(IrcitUiNode *node_ptr);
size_t IrcitUiRecyclerCursor(IrcitUiNode *node_ptr);
void   IrcitUiRecyclerSetCursor(IrcitUiNode *node_ptr, size_t index);
IrcitUiNode *IrcitUiRecyclerRowAt(IrcitUiNode *node_ptr, size_t index);
```

- `Select` and `SelectRange` **return whether anything changed**, so a caller can
  skip a repaint. Both take a dataset index, and both are no-ops for an index
  outside the dataset.
- `SelectRange` takes its bounds in either order — a range dragged upwards arrives
  backwards, and sorting here rather than at each call site is how one of them
  does not forget. `end` is clamped to the dataset; a `start` past the end leaves
  an empty span, which sets nothing and returns 0. It requires `MULTI`.
- `GetSelectedIndices` is the one that crosses to a worker — see the thread
  contract below.
- `RowAt` is the pooled node showing an index, or NULL if that index is not
  currently realised. `RowAt(list, Cursor(list))` is the cursor's row when it is
  on screen.

#### The interaction contracts

| Input | Mode | Effect |
|---|---|---|
| `KEY_DOWN` / `KEY_UP` | any | moves the cursor. Under `SINGLE` that **is** the selection; under `MULTI` the marks are untouched. Drops the anchor. |
| **Shift+`KEY_DOWN` / `KEY_UP`** | `MULTI` | extends a contiguous range from the anchor to the cursor |
| Shift+arrow | `SINGLE`/`NONE` | an ordinary move — ranges are a `MULTI` idea |
| `IRCITUI_KEY_SPACE` | `MULTI` | toggles the bit at the cursor |
| `IRCITUI_KEY_SPACE` | `SINGLE` | sets it (already set, since the cursor is the selection) |
| `Enter` | any | **chooses** the cursor's row — `on_item_clicked`, not a selection change |
| click | any | wipes the set, moves the cursor, marks the clicked row |
| Ctrl+click | `MULTI` | toggles the clicked row, leaves the rest |
| Shift+click | `MULTI` | range from the anchor to the click |
| press + drag | `MULTI` | sweeps the range as the pointer moves |

**All of it is built**, and the gesture's meaning comes from the **modifier**
rather than from the mode: `selection_mode` is a capability gate, and Ctrl and
Shift decide which row is marked. The old rule that a click and `SPACE` are one
act is retired for the mouse — `SPACE` still toggles, and a plain click wipes.

The drag-sweep is the SHIFT arm driven by motion, not a gesture of its own. The
press runs the plain-click arm and arms; each motion asks what a shift+click asks;
the release disarms. That is not a shortcut — **motion carries no button state**.
The framework asks the terminal for motion on every move (DECSET 1003), so a held
drag and a free hover both arrive as `bstate=0x10000000`, measured. What makes it
work is that the sweep is armed only between a press (`0x2`) and a release
(`0x1`), and inside that window the only reason to be moving is that the button is
down. A hover after the release changes nothing.

**Auto-scroll on a drag past the edge is NOT implemented.** A pointer not over any
row leaves the range where it was, which is the safe reading: the alternative
silently truncates the selection at the end of the list.

#### The range is re-derived, not accumulated

Shift+Down twice from a click on row 5 must leave 5..7 marked. Toggling the
newly-reached row looks right on the first press and is wrong on the second,
because a toggle on an already-set bit **clears** it — row 6 would come back
unmarked. So the previous span is cleared and the new one set, which also makes
the operation idempotent in the direction the user expects.

#### The anchor

A pivot, held in `selection_anchor_index`, so a run of Shift+arrows expands from
one place rather than chasing the cursor.

- **Set** by any event that establishes a new target baseline: a plain click, a
  Ctrl+click, or a plain keyboard move.
- **Established once** by the first Shift+arrow, at wherever the cursor stood
  before it — not at each press.
- **Dropped** by an unshifted move, so the next range starts from where the user
  is rather than from an old pivot.
- **Not set** by `SPACE`, which toggles at the cursor without establishing
  anything new.

It is a `size_t` plus an `int` flag rather than a negative sentinel. That is the
opposite of `mark_fg` and `sticky_header_type`, deliberately: those need a value
below zero because their zero is a legal value, so an unset field sharing it would
make the real one unaskable. Here the datum and the flag are separable, `0` is a
perfectly good anchor, and a pair cannot be got wrong by a caller who forgets
which negative means what.

#### The highlight

Selected rows are drawn with `highlight_attr_mask`, which defaults to `A_REVERSE`
and is inherited from the model the same way. It is **derived per row at paint
time**, not stored: the painter asks the enclosing row which dataset index it is
bound to and reads the bit. `IrcitUiStyleOverride` cannot serve here — it carries
fg/bg/bdr and no attribute, so it can recolour a row but cannot reverse it.

Setting it on the model overrides the default. A mask of `0` means the model never
chose one and the default stands.

#### The set, and what it costs

One bit per **dataset** index, little-endian within each word, grown on demand.
It is the one allocation in the widget that does not stay virtual: 1.25 KB at ten
thousand rows, 6.25 KB at fifty thousand. Inherent to answering "is row N marked"
in constant time, and `IRCITUILIB_DESIGN_REFACTOR_V1.md` §17 snapshots it.

Two properties that surprise people:

- **The array never shrinks.** A dataset that got smaller leaves bits set above
  the new end — they are meaningless rather than wrong, and reallocating down on
  every tick of a shrinking `item_count` is work for nothing. `Select` and
  `SelectRange` refuse an index outside the dataset, so those bits cannot be
  re-created; anything counting the set must stop at `item_count`, as every
  reader does.
- **A marked row may be scrolled out of sight.** The set is not confined to the
  viewport — only the cursor is.

#### The thread contract

**`IrcitUiGetSelectedIndices` is a Thread 0 call.** It dereferences the live
bit-array with no lock, and Thread 0 can `realloc` that array underneath it from
`IrcitUiRecyclerSelect`. What goes to a worker is the **buffer you filled**, not
the list — `IRCITUILIB_DESIGN_REFACTOR_V1.md` §17.1's diagram has "Extract Snapshot" inside the Main
Thread 0 box.

#### The rules that bite

1. **A mark is keyed by dataset index, never by row node.** Storing it on the row
   is `IRCITUILIB_DESIGN_REFACTOR_V1.md` §16's named anti-pattern; the mark would follow the pool
   slot.
2. **`SINGLE` is the zero value.** A list you never configured selects one row at
   a time, which is the correct default and not a bug in your setup.
3. **Ranges need `MULTI`.** Under `SINGLE` a Shift+arrow is an ordinary move, and
   `SelectRange` returns 0 without touching anything.
4. **`SPACE` and `Enter` are different acts.** `SPACE` marks; `Enter` chooses and
   fires `on_item_clicked`. A row is not a button.
5. **`Select` is a toggle under `MULTI`, not an assignment.** A second call on a
   marked row clears it — which is why `SelectRange` exists and why the range code
   clears before it sets.
6. **`GetSelectedIndices` may return fewer than the set holds.** `return == maxn`
   means "there may be more"; the buffer is yours and the walk stops at its end.

### 9.3 Inline editable rows

Editing a row **in place**, without adding a second widget to the layout. The row
becomes a text field, the user types into it, and one callback hands the result
back. **Read §9.1 first**: an editing row is a *shape*, and shapes are what that
section is about.

#### What an editing row is

The row at the editing position is bound to a different registry entry, so the
layout pass rebuilds that one pool slot as a clone of the edit template — with a
fresh text buffer — and rebuilds it back when the edit ends. Nothing is mutated and
no node is added to or removed from the pool. That is what makes the feature safe in
a virtualised list at all: a row node is a scavenger that the next scroll rebinds to
a different dataset entry, so an edit state parked on one would follow the pool
rather than the entry. Here the position, not the node, is what the commit names.

The template the editing type names must be an **`edit_text`**, and it should be
bare rather than a container: a row's box is the pool's cell, not the template's
business — `row_height` gives every row its height and the layout does not measure a
row's contents — so a bordered template would have no room for its border. A bare
`edit_text` draws its own `> ` prefix and its value on the one line it is handed.

#### Opening a row

**Two gestures, and both belong to the framework** rather than to the application:

| gesture | notes |
|---|---|
| **double left-click** on a row | the first click selects the row as any click does — a double-click is not a selection gesture — and the second opens it |
| **Ctrl+Enter** on the cursor's row | the keyboard half; see the delivery caveat below |

Timing is the engine's, with `CLOCK_MONOTONIC` and a 300 ms window
(`IRCITUI_DOUBLE_CLICK_NS`). It has to be, and it is worth knowing why: ncurses
reports `BUTTON1_DOUBLE_CLICKED` only when both clicks land in the same read,
which a hand on a mouse does not do, and this library calls `mouseinterval(0)` so
that a single click is reported on the frame it arrives rather than held back to
see whether a second one follows. Measured: two clicks 100 ms apart come back as
`CLICKED, CLICKED`, never `0x8`. Both events are honoured — `0x8` as a shortcut
that skips the comparison, and the pair otherwise.

**Ctrl+Enter is only deliverable where the terminal sends a sequence for it** —
`XTerm*modifyOtherKeys: 2`, or a terminal speaking CSI-u. A default terminal puts
a bare CR on the wire for Ctrl+Enter, byte-identical to Enter, and no parser can
tell them apart; `cat -v` settles which you have. The engine hand-parses both
encodings and takes any modifier with the ctrl bit set, so `27;6;13~`
(ctrl+shift) works too — but for a terminal that sends neither, the gesture does
not exist and the application must bind a key that arrives. The demo binds
**Ctrl-E** for exactly that reason; it is one byte and every terminal delivers it.

#### The three calls

```c
/* once, on the MODEL, after SetViewTypes -- it names a registry entry */
IrcitUiRecyclerSetEditType(list, LOG_EDIT);

IrcitUiRecyclerBeginEdit(list, 42);   /* open */
IrcitUiRecyclerIsEditing(list);       /* ask */
IrcitUiRecyclerCommitEdit(list);      /* or IrcitUiRecyclerCancelEdit(list) */
```

`BeginEdit` must be told where the result goes: it refuses when the adapter has no
`on_item_edited`, because a field whose result nobody receives is a user typing into
a void. It also refuses an index past the dataset, and a type the registry does not
hold, or one whose template does not resolve — each of which would leave the row in
the shape it already had, so the user would be typing into an ordinary row that
swallows the keystrokes and shows none of them.

```c
static void on_edited(size_t position, const char *revised_text, void *ds) {
  snprintf(((char (*)[96])ds)[position], 96, "%s", revised_text);   /* copy it */
}
```

**`revised_text` is borrowed and dies with the call** — it points into the row's own
buffer, and the layout pass that follows the commit frees that row. Copy it.

#### Filling the field

The framework calls `on_bind_view` for the editing row **once**, on the pass that
builds it, and leaves it alone afterwards — the row's contents are the user's from
that moment. So the binder fills it there, and nowhere else:

```c
if (IrcitUiNodeGetKind(row) == IRCITUI_EDIT_TEXT)
  IrcitUiEditTextSetValue(row, my_dataset[position]);   /* then return */
```

The **kind** is how a binder tells an editing row from a display row.
`IrcitUiEditTextSetValue` copies the string in — unlike `IrcitUiNodeSetText`, whose
borrowed pointer could not be typed into — and parks the caret at the end. It is the
only setter that reaches a field's own buffer; `IrcitUiNodeSetText` is shadowed
there by the engine's own alias, which is the blind spot §1.2 documents.

#### While an edit is open

| Input | Effect |
|---|---|
| printable, `Backspace`, `←` `→` | the field: inserted, deleted, caret moved |
| `Enter` | **commits** — `on_item_edited` fires, the row reverts |
| `Esc` | **cancels** — nothing is reported, the row reverts |
| a click outside the field | commits, then the click does what it was going to |
| `↑` `↓` | inert |
| a click on the row, on a button, on `SPACE`, `Enter` on the list | the list **does not act** |

Two of those are worth spelling out. `Enter` **does not activate the row**: it does
not reach `on_item_clicked`. An edited row is not a chosen one, and this is the one
place a list's `Enter` means something other than "choose this". And **the list is
frozen** — it does not scroll and it does not mark — because scrolling rebinds rows,
and the row holding the text buffer would be the first to go.

**A second `BeginEdit` commits the first**, so starting to edit row B while row A is
open carries A's text home rather than dropping it.

#### The rules that bite

1. **The editing type is not a YAML key.** The type is a declaration's business and
   the registry already carries it; *when* a row is being edited is a run-time fact.
   Configure it from C, on the model, like the adapter.
2. **Register before you name.** `SetEditType` refuses a type the registry does not
   hold — so `SetViewTypes` comes first, exactly as for `SetStickyHeader`.
3. **The value being edited is the binder's job, once.** Re-filling the row on later
   passes overwrites what the user typed; the framework will not call you again, but
   an application that fills the row from somewhere else must not either.
4. **`on_item_edited` is not called on a cancel.** A cancel and a field that was
   never opened are indistinguishable from the dataset's point of view.
5. **Editing a pinned divider hides the field.** The pinned header is drawn over the
   row it pins (§9.1), so editing *that* row puts the field underneath it. Nothing
   breaks; the user simply cannot see what they are typing. Edit a non-divider row,
   or turn `sticky_headers` off.

## 10. The edit field — the widget, and the prediction attached to it

An `IRCITUI_EDIT_TEXT` is one row of editable text: a buffer the user types into, a caret, a
selection, and — optionally — a prediction drawn after the caret. **The prediction is an
attachment, not a mode of the widget.** A field with no dictionary is a complete field, and every
call in §10.1 works without one; §10.2 is the capability a host adds on top. The two are kept
apart here because they fail apart: a field that will not accept a character is a widget problem,
and a field that suggests the wrong word is a dictionary problem.

**Where the code lives** The field's sixteen `PUBLIC_API` calls and their
documentation are in **`ircituilib/ircitui_edit_text.h`**, and the implementation is
**`src/ircitui_edit_text.c`**. `ui.h` includes that header, so a consumer that includes only the
umbrella still reaches every call. The engine owns the *paths* — it paints, focuses, hit-tests and
routes a field — and reaches it through an internal seam in
`src/include/ircitui_edit_text_priv.h`, which is not installed and hands out no member of the
field's private state.

### 10.1 The widget

#### What it owns

A field's state is private, allocated once when the node is created, and reached only through the
calls below:

| | |
|---|---|
| **the buffer** | fixed capacity, 256 bytes including the terminator. Overflow is met by truncation — there is no error return for "too long". |
| **the caret** | a byte index, always on a character boundary. Every writer in the field maintains that, so a caller may read it as a character offset for any UTF-8 text. |
| **the selection** | a `[start, end)` byte pair that **may be reversed**: the anchor stays put and the extent moves, so `start` is not always the earlier of the two. |
| **the prediction** | the candidate, the token it was computed for, and whether the list is on. All of §10.2. |

The capacity is stated for accuracy rather than as contract: it lives in a private header, and a
caller should depend not on the number but on the rule — a set or an append that does not fit is
truncated, and an append cuts on a character boundary so the buffer never ends mid-codepoint.

#### The calls

| Call | What it does |
|---|---|
| `IrcitUiEditTextGetValue` | the buffer, **borrowed** |
| `IrcitUiEditTextSetValue` | copy a string in; caret to the end of what was stored |
| `IrcitUiEditTextClear` | empty it: caret and selection to 0, prediction dropped |
| `IrcitUiEditInput` | the editor — one keycode in: insert, backspace, arrows, Enter |
| `IrcitUiInputAppend` | append UTF-8: what the paste and the emoji picker call |
| `IrcitUiCaretVisible` | is the caret lit on this paint |
| `IrcitUiBlinkAdvance` | tick the blink; 0 when there is nothing to animate |
| `IrcitUiBlinkReset` | light the caret now — what focus arriving does |

**The clipboard is the engine's, not the field's.** `IrcitUiClipCopy`, `IrcitUiClipPaste` and
`IrcitUiClipClear` are declared in `ui.h` beside every other kind, because they dispatch on kind —
copy and paste serve a `TEXT_VIEW` as readily as a field. They are still how a field's selection
reaches the clipboard, and `IrcitUiClipPaste` falls back to the node named `cmdline` when it is
handed nothing.

#### The rules that bite

1. **`GetValue` is borrowed and stays live.** It is the buffer itself, not a copy — the field goes
   on writing into it as the user types. Read it, act on it, re-read it; do not hold it across a
   dispatch.
2. **Setting a field's text does not go through `IrcitUiNodeSetText`.** That call sets the node's
   *declared* text, which for a field is the **placeholder** the painter falls back to while the
   buffer is empty. A value written there is shadowed by the buffer and is not the string the user
   then edits. Use `IrcitUiEditTextSetValue`.
3. **`EditInput` takes one key, not a string.** A host injecting text uses `IrcitUiInputAppend`,
   which takes UTF-8 and drops control bytes: the field is single-line and paints one row, so a
   retained `\n` would be invisible yet still reach the Enter path.
4. **Typing has two routes into the buffer.** The dispatcher runs the application's `on_key_down`
   and the field's editor (§10.3); separately, the loop's fast lane runs its own
   intercept–mutate–notify pipeline for plain printable ASCII, without posting to the dispatcher at
   all. The two routes apply **different precedence rules to the same listener**, and §10.3 is the
   only place that distinction is written down.
5. **One caret blinks: the focused field's.** Every other field still draws a caret, steadily,
   because a field with no caret at all reads as a field with nowhere to type. `CaretVisible`
   answers for the node it is handed, and `BlinkAdvance` returns 0 when the focused node is not a
   field — so an idle tick with a button focused repaints nothing at all.
6. **`Clear` empties; it does not detach.** A dictionary stays attached and the prediction is
   simply re-derived from the now-empty buffer.

#### What is deliberately not here

- **No field factory.** A field is authored in a layout and configured by setter, like every other
  widget here.
- **No multi-line field.** The buffer is one row, the painter draws one row, and `\n` is dropped
  rather than stored.
- **No public capacity, and no public caret type.** Nothing in the header names the buffer size or
  hands out an offset, because a caller who wants one wants the string — which is `GetValue`.

The dictionary seam: an edit field that offers the rest of the word as the user types, the
opaque `IrcitUiDictionary` a host implements to supply it, and the demo that drives both.
The design record is `IRCITUILIB_PREDICTIVE_AUTO_COMPLETE_DESIGN.md`.

### 10.2 The prediction attached to it — the dictionary seam

An edit field that suggests the rest of the word as the user types, and a list of the candidates
it could have suggested instead. **This section is the API guide for it; the design record is
`IRCITUILIB_PREDICTIVE_AUTO_COMPLETE_DESIGN.md`'s Appendix A and the evidence is
`dev/probes/predict/`.**

#### The vocabulary

| Name | What it is |
|---|---|
| `IrcitUiDictionary` | An opaque prediction source: a host's instance pointer and a vtable. |
| `IrcitUiDictionaryInterface` | `best`, `next`, `replace`, `destroy`. Every entry optional. |
| `IrcitUiEditTextSetDictionary` | Attaches one to a field. **The switch is the presence of a dictionary** — there is no separate "enable". |
| `IrcitUiEditTextHint` | The whole candidate the field is predicting, or NULL. |
| `IrcitUiEditTextAcceptHint` | Writes it in. What Tab calls. |
| `IrcitUiEditTextShowSuggestions` | Turns the **list** on for a field. Off by default. |
| `IrcitUiEditTextSuggestionCount` / `Suggestion` / `SuggestionSelected` | What the list is showing, and which row is highlighted. |

#### The rules

1. **The token is the word under the caret**, delimited by spaces — not the whole buffer. A field
   holding `status app` predicts `app`.
2. **A candidate that does not begin with the token is refused by the field**, not by the
   dictionary. The interface may be implemented by anyone; the check that keeps arbitrary text
   off the glass belongs where the glass is.
3. **A candidate equal to what was typed is not a completion.** There is nothing left to offer.
4. **The hint is drawn, never written.** It is not in the buffer, so the clipboard, `Enter` and
   the application's `on_key_down` never see it.
5. **`replace` is asked at accept time, and it replaces the whole token.** That is what makes
   `:fir` complete to `:fire:` and *accept* to a fire — the tag the user read is not the text
   they get, and the dictionary is what decides that, not the field.
6. **The list and the hint are one piece of state.** Moving the selection changes the dimmed
   tail; `SuggestionSelected` and `Hint` cannot disagree.
7. **The list does not take focus.** Forced, not chosen: the loop's edit fast path only fires for
   a focused `IRCITUI_EDIT_TEXT`, so a list that took focus would stop the field accepting
   characters.

#### Binding it

```c
/* Once, on the MODEL node -- see the lifetime rule below. */
static const char *const words[] = { "apple", "application", "apply" };
IrcitUiDictionary *dict = IrcitUiDictionaryCreateList(words, 3);

IrcitUiNode *field = IrcitUiFind(IrcitUiModelRoot(), "cmdline");
IrcitUiEditTextSetDictionary(field, dict);      /* the inline hint      */
IrcitUiEditTextShowSuggestions(field, 1);       /* and the list, if you want it */

/* Tab is already bound to the accept by the engine. A host that wants another key: */
if (key == MY_ACCEPT_KEY) IrcitUiEditTextAcceptHint(field);
```

#### The lifetime rules, which are the ones that bite

1. **The dictionary is borrowed and the caller owns it.** Two fields may share one; the field
   never calls `destroy`. Destroy it after every field using it is gone.
2. **A composite borrows its members too**, for the same reason — one source may be in two
   chains.
3. **Attach to the MODEL node.** `IrcitUiMainLooper` calls `IrcitUiScreenReset` before its first
   frame, and a clone inherits the dictionary through `common.ref`. A dictionary attached to a
   node of the live screen tree is replaced by that reset, and the field then predicts nothing —
   silently, because a field with no dictionary looks exactly like one with nothing to suggest.
4. **A profile is written when the application says so**, never at teardown. `Export` and
   `Import` are calls, not hooks.

#### What is deliberately not here

- **No declarative surface.** A dictionary is a pointer to a runtime object, so there is no
  `dictionary="main"` key and no registry for a name to resolve against. The layout chain is
  untouched by this feature — no new grammar production, no verifier rule.
- **No `IrcitUiProgressBarCreate`-shaped factory for fields.** A field is authored in a layout and
  configured by setter, like every other widget here.
- **No braille arc, no fuzzy arc, no arcs at all** — the dial is a separate proposal.

### 10.3 Wiring a field into a host application

The framework and the host are blind to one another. The framework implements the primitives and
owns their private allocations; the host owns the layout and coordinates widgets through public
boundaries — by id, or by node — from inside generic lifecycle listeners. Neither knows the
other's structure: the framework does not know that a field is wired to a conversation window, and
the host does not know what a field keeps in its private state.

#### The intercept — `on_key_down`

A listener wired on a node runs **before** the field's own editor, and a non-zero return consumes
the event so the framework's default handling never runs. `src/ui_event_dispatcher.c` states the
order as *"the application first, the framework second, and the first non-zero wins"*. Both halves
matter: a listener that returns 0 hands the key on, so an intercept interested only in Enter does
not have to re-implement typing.

```c
/* Wire on the MODEL node: a screen node is replaced by the reset the loop runs first. */
static int
sOnChatInputKeyDown(IrcitUiEventDispatcher *disp_ptr, IrcitUiEvent *ev_ptr, void *user_ptr)
{
  IrcitUiNode *input_field;
  IrcitUiNode *conv_window;
  const char  *payload;

  (void)disp_ptr;
  (void)user_ptr;
  if (!ev_ptr) return 0;
  if (ev_ptr->key_code != '\r' && ev_ptr->key_code != '\n' && ev_ptr->key_code != KEY_ENTER) return 0;

  input_field = IrcitUiFind(IrcitUiScreen.root, APP_FIELD_ID);
  conv_window = IrcitUiFind(IrcitUiScreen.root, APP_CONV_ID);
  if (!input_field || !conv_window) return 0;

  /* Read through the public accessor. Borrowed, and live: the field keeps writing to it. */
  payload = IrcitUiEditTextGetValue(input_field);
  if (!payload || !payload[0]) return 1;   /* nothing to send, but the newline is still ours */

  {
    static uint64_t s_next_id = 1;
    IrcitUiConversationMessage msg;

    memset(&msg, 0, sizeof msg);
    msg.message_id   = s_next_id++;        /* the host's identity for the message, see §13 */
    msg.kind         = IRCITUI_MSG_KIND_MESSAGE;
    msg.direction    = IRCITUI_MSG_OUTBOUND;
    msg.status       = IRCITUI_STATUS_SENDING;
    msg.text_content = (char *)payload;    /* Append COPIES it, so borrowing the buffer is safe */
    snprintf(msg.sender_name, sizeof msg.sender_name, "%s", "Me");
    snprintf(msg.timestamp, sizeof msg.timestamp, "%s", "12:34");

    IrcitUiConversationWindowAppend(conv_window, &msg);
  }

  IrcitUiEditTextClear(input_field);
  return 1;                                /* the raw newline never reaches the buffer */
}

void
HostAppInitModelWiring(void)
{
  IrcitUiNode *model_input = IrcitUiFind(IrcitUiModelRoot(), APP_FIELD_ID);
  if (model_input) {
    model_input->interactions_user.lifecycle_listeners.on_key_down = sOnChatInputKeyDown;
  }
}
```

#### The fast lane — a second route, under its own rule

`IrcitUiMainLooper` intercepts a key in `[32, 126]` aimed at a focused edit field and handles it
without posting it to the dispatcher at all. That path is `IrcitUiExecuteFastPathTyping`, and it
runs three stages of its own:

| Stage | What runs | Effect of a non-zero return |
|---|---|---|
| 1 — intercept | `on_key_down`, application slot first | swallows the key: no mutation, no notify |
| 2 — mutation | the insert, in place, on the field's own buffer | — |
| 3 — notify | `on_text_changed`, application slot first | — |

**Its precedence rule is replacement, not chain.** If the application registered an `on_key_down`,
the framework's default is bypassed for that key *whatever the application returns* — on this path
the framework's listener exists only as the fallback for a field whose host registered nothing. The
dispatcher's rule is the other one: application first, framework second, the framework running
whenever the application returned 0. Both are deliberate, and they are not the same rule, so an
`on_key_down` that means "let the field handle it" on one route means "I have taken over" on the
other.

So `on_key_down` **is** called for ordinary typing after all — by the fast lane, under the
replacement rule, not by the dispatcher:

| Key | Route | `on_key_down` |
|---|---|---|
| `[32, 126]`, field focused | fast lane | called — replacement rule |
| ENTER · arrows · control codes · >126 · mouse | dispatcher | called — chain rule |

A character filter is therefore possible, and it is written twice if it must hold for both bands:
once as the fast-lane veto and once on the dispatcher route.

**The return value is a repaint signal, not a consumption signal.** It answers "did the buffer
move", so a veto answers 0 and the loop skips the one-row repaint for a row that did not change.
The loop consumes the key either way: stage 1 has already given the application its say, and
re-posting it would offer the same key twice and then run it through the editor as well.

#### The sync — `on_text_changed`

`on_text_changed` **is** invoked — from the fast lane's stage 3, and from nowhere else. It is not
reachable through the dispatcher: there is no `IRCITUI_EVENT_TEXT_CHANGED` in `IrcitUiEventType`,
and the dispatcher's listener switch, which invokes six lifecycle listeners, has no case for it. It
has no framework default either — `ui_defaults_router.c` initialises the eight
`lifecycle_listeners` and never touches `post_interceptor_listeners` — so the slot holds only what
a host put there. `on_draw` and `on_focus_changed` remain assigned and never invoked; the events
track owns those two.

Two consequences follow, and both surprise:

1. **It fires only for a host that also registered `on_key_down`.** With nothing registered, the
   framework's default is the fallback, the editor mutates and returns non-zero, and stage 1 ends
   the call — so stages 2 and 3 never run. Wire the sync hook alone and it never fires; wire it
   beside an `on_key_down` that returns 0 and it does.
2. **It covers the fast lane's band only.** A key that reaches the buffer through the dispatcher —
   ENTER, the arrows, everything above 126 — mutates without notifying.

A character counter therefore works, provided it is wired alongside an `on_key_down`, and it counts
the printable band. The field's private header draws the notification pass as following any buffer
mutation; today it follows the mutations this path makes.

## 11. Progress, the bars and the seek

The determinate track: one painter and one state struct behind `IRCITUI_PROGRESS_BAR` and
`IRCITUI_SEEK_BAR`, the three layout keys that make a style, and the live value that could
not be declarative.

### 11.1 The progress bar

A determinate track: a value drawn as a filled run against an empty one, with the percentage
optionally beside it or over it. **This section is the API guide for it; the grammar rule is
C24 in `tools/layout-grammar.ebnf:368`, the design record is
`IRCITUILIB_PROGRESS_INDICATORS_DESIGN.md`, and the evidence is `dev/probes/progress/`.**

#### The vocabulary

| Name | What it is |
|---|---|
| `IRCITUI_PROGRESS_BAR`, `IRCITUI_SEEK_BAR` | The two kinds. **One painter and one state struct** — they differ in the name a layout writes and nothing else, so every call below answers both. |
| `fill_glyph`, `empty_glyph` | The two glyphs, from the template. **Exactly one code point each**, and the only two keys a bar has that a button has not (C24). Defaults `U+2588` `█` and `U+2591` `░`. |
| `label_mode` | `off` (default), `adjacent`, `centered`. **`adjacent` narrows the bar** — see rule 2. |
| `IrcitUiBarSetValue` / `IrcitUiBarValue` | The position, as a whole percentage. Clamped to 0–100 and never refused; an unchanged value is a no-op that does not invalidate. |
| `IrcitUiBarSetGlyphs` | The C half of the two glyph keys. Refused **whole** — one bad glyph sets neither, so the bar is never left in a third state. |
| `IrcitUiBarSetLabelMode` | The C half of `label_mode`. An out-of-range mode is ignored, following `IrcitUiSetBorderStyle`. |
| `IrcitUiBarSetGradient`, `IrcitUiBarClearGradient`, `IrcitUiBarGradientRequested`, `IrcitUiBarGradientLive` | A ramp across the filled run. **No template key exists for it** — the endpoints are live colour data — and the last two are how a caller tells an asked-for ramp from one the terminal actually granted. |
| `IrcitUiPostBarProgress` | The same position, from a worker thread, **by node id** rather than by pointer. |
| `IrcitUiBindSeek` | Makes a bar seekable: the user's gestures arrive as a fraction of the track. |

#### The rules

1. **A bar's appearance is a combination of C24's three keys** — a kind, two glyphs and a label
   mode. There is no "style" attribute, no preset name, and no per-kind painter to add.
2. **`adjacent` changes the bar's WIDTH, not only its paint.** The label's six columns are
   reserved *inside* the node's content box, so the node's box is unchanged and the track is
   narrower. Because a seek divides a mouse column by that same track width, the reservation is
   resolved by **one function both readers call** (`sBarTrackW`, `src/ui_engine.c:3380`). Two
   call sites computing it would eventually disagree by a column, and the symptom is a bar that
   seeks to the cell next to the one that was clicked.
3. **The label is six columns whatever the value** — `" %3d%% "` (`sBarLabel`,
   `src/ui_engine.c:3363`) — so a one-digit and a three-digit percentage occupy the same space
   and the bar does not jitter as it fills. That constancy is what lets an adjacent label's
   reservation be the compile-time constant `PRIV_CONFIG_DEFAULT_BAR_LABEL_W` (6).
4. **The value is rounded exactly once.** `filled = (inner_w * pct + 50) / 100`
   (`sBarSplit`, `src/ui_engine.c:3314`) is the only place a percentage becomes columns, and
   `sBarSeekAt` (`:3404`) the only place a column becomes a percentage. Everything between the
   two passes an `int` through.
5. **A worker never touches a node.** `IrcitUiPostBarProgress` copies an id and a number into a
   fixed envelope and returns; Thread 0 applies it. The id, not a pointer, is also what makes a
   panel closed mid-run safe — the report resolves to nothing and is dropped, which *is* the
   cancellation.
6. **The two runs differ in attribute, not only in glyph.** The filled run carries the node's own
   colour pair; the empty lane carries `IRCITUI_SLOT_TEXT_MUTED | A_DIM`. The attribute is the
   half a caller cannot redefine away by choosing glyphs.
7. **A node's two glyphs are independent.** A template may name one and default the other; only
   the C setter is all-or-nothing, because a caller typing a call has both in front of them and a
   template naming one key plainly means it.

#### The two surfaces, and which one a given appearance needs

Every bar property lives in exactly one of two places, and which one is not a preference — it is
**"could this be written in a file?"**

| Where | What goes there | Why |
|---|---|---|
| **the layout** (C24 keys) | the kind, the two glyphs, the label mode | each is a scalar, fixed at build time, and known before the program runs |
| **the C API** | the value, the gradient, the seek binding | each is either live data or resolved against the terminal at run time |

**A style is a combination of keys that already exist, so adding one is a layout edit and not a
framework change.** The parallelogram bar —

```
▰▰▰▰▰▰▰▰▱▱▱▱▱▱▱▱▱▱▱▱▱▱▱▱▱▱▱▱▱▱▱▱▱▱▱▱▱▱▱▱ 20%
```

— is `fill_glyph: "▰"` + `empty_glyph: "▱"` + `label_mode: adjacent`, three keys C24 already
declared and `tools/gen_ui.py` already emitted. It cost one node in `layout/progress_panel.yaml`
and **zero lines of library source**: the validator's C24 check, the emitter's
`.bar_fill_glyph` / `.bar_empty_glyph` / `.bar_label_mode` writes and the verifier's **R19** rule
all already covered those keys, because *a key the grammar accepts is a key every stage of the
chain already handles*. Nothing was added to the grammar, the validator, the generator or the
verifier.

**The value is the one thing that is not a style and cannot be declarative** — which is why
`layout/progress_panel.yaml` carries glyphs and label modes and no `value:` anywhere.

#### Binding it

```yaml
# The declarative half: a style, fixed at build time.
- id: bar_parallel
  type: progress_bar
  fill_glyph: "▰"
  empty_glyph: "▱"
  label_mode: adjacent
  layout_width: match_parent
  layout_height: 1
```

```c
/* The live half. Same three fields if you prefer to set the style from C: */
IrcitUiBarSetGlyphs(bar, "\xe2\x96\xb0", "\xe2\x96\xb1");   /* ▰ ▱ */
IrcitUiBarSetLabelMode(bar, IRCITUI_BAR_LABEL_ADJACENT);

/* The position, which had to be a call: */
IrcitUiBarSetValue(bar, 20);

/* Or from a worker, which may not know a node: */
IrcitUiPostBarProgress(disp, "bar_parallel", 20);
```

#### What bites

1. **Borrowed glyph pointers.** A node never owns its text, and these are strings like any other:
   the generated model points them at literals, and a caller passing a buffer must keep it alive
   as long as the node.
2. **An adjacent label is dropped, not truncated, when there is no room.** Below
   `PRIV_CONFIG_DEFAULT_BAR_LABEL_W + 4` the reservation is abandoned and the label is not drawn —
   a four-cell bar with a six-column label beside it would be a percentage of nothing.
3. **A centered label needs cells of bar either side** (`bar_w > LABEL_W + 2`) for the same
   reason: a label with no track under it reads as text, not as a value.
4. **A float is capped at `PRIV_CONFIG_DEFAULT_POPUP_W` × `H` (34 × 18)**
   (`include/ircituilib/config_defs.h:166,168`), whatever its `layout_width`/`layout_height` say.
   A panel authored wider or taller than that is **clipped silently**, and its tail is never
   drawn — measured on the demo's own gallery, which is authored 40 wide and 24 tall and shows
   neither its last four bars nor its Close button. See
   `IRCITUILIB_PROGRESS_INDICATORS_DESIGN.md` §9.35.

#### What is deliberately not here

- **No `value:` key.** A position is live data and a layout is compiled at build time; a bar
  frozen at a number written in a file is not a progress bar.
- **No `gradient:` key**, for the same reason one step further out: two endpoint colours are
  resolved against the terminal's register space at run time, and a key would give one fact two
  homes.
- **No new palette slot.** The frame, the shadow and the selection styling all come from the
  profiles the theme already has.

## 12. The TCP/IP extension — the event-type seam

The event-type seam carried over a socket: the listener registered once on Thread 0, the
producer that chunks to 511 and treats a refusal as fatal, and the teardown order that has
to hold between them.

### 12.1 The producer and the consumer

**Two halves, in two threads, and the framework is the seam between them.** The full worked example
is `src/examples/ui_main.c`: `--connect host:port` starts a worker, and the demo's recycler shows what
arrives.

**The consumer: one function, registered once.**

```c
#include "ircituilib/ui.h"
#include "ircituilib/ircit_ui_event_dispatcher_type.h"

/* Runs on Thread 0. `bytes` lives only for this call -- copy what you keep. */
static int
sOnChunk(IrcitUiEventDispatcher *disp, void *token, const char *bytes, void *user)
{
  AppState *app = user;              /* handed back unread, every call */
  Stream   *st  = sStreamFor(app, token);   /* which connection: the token says */

  if (!st) return 0;
  sReassemble(st, bytes);            /* framing is yours; the framework has none */
  return 1;
}
```

**Register it at bootstrap — before the loop, not from inside a callback** — and clear it before the
tree goes:

```c
  IrcitUiDispatcherRegisterNetworkListener(IrcitUiDispatcherInstance(), sOnChunk, &g_app);

  IrcitUiMainLooper(ctx);

  IrcitUiDispatcherRegisterNetworkListener(IrcitUiDispatcherInstance(), NULL, NULL);  /* first */
  IrcitUiDestroyFrameworkContext(ctx);                                                /* then  */
```

**The producer: read, chunk to 511, post, and treat a refusal as fatal to that stream.**

```c
static void *
sWorker(void *arg)
{
  Stream *st = arg;
  char    buf[PRIV_CONFIG_DEFAULT_CLIPBUF_SZ - 1];   /* 511: the envelope's bound */

  while (!st->stop) {
    ssize_t n = recv(st->fd, buf, sizeof buf, 0);
    if (n <= 0) { if (n < 0 && (errno == EINTR || errno == EAGAIN)) continue; break; }

    if (!IrcitUiDispatcherPostNetworkChunk(IrcitUiDispatcherInstance(), st, buf, (size_t)n, 200)) {
      /* Full for the whole deadline. That chunk is lost and the reassembler cannot
         recover its framing from the next one, so the stream ends here. */
      break;
    }
  }
  return NULL;
}
```

**Stop the worker before the pipeline, and join it.** `shutdown(fd, SHUT_RDWR)` — never `close()`,
which is undefined against a thread blocked in `recv()` and does not reliably wake it — then set the
flag, then `pthread_join`, then `close`. `sNetStop` at `src/examples/ui_main.c:2674-2692` is the
sequence, and its comment names it as the contract.

**The four things that will bite, in the order they bite:**

1. **The token's storage must outlive every message posted with it.** Nothing checks this.
2. **`bytes` is borrowed for one call.** A listener that keeps a pointer has a dangling one by the
   next message of any kind.
3. **A chunk of more than 511 bytes is refused, not truncated.** Chunk the read; do not assume the
   post took it.
4. **A blocking listener wedges the whole application**, because nothing pre-empts it.
`IRCITUILIB_EVENTS_TCP_IP_DESIGN.md` §5.

## 13. The conversation window — the composite seam

**What it is for.** A messaging conversation rendered as bubbles: asymmetric anchoring, runs of
consecutive messages from one sender grouped into a single shape, reply threading, reaction badges with
their tallies, delivery marks, a timestamps margin and a typing row. The widget supplies all of that
from the messages you hand it. A layout names the widget and says nothing else.

**The division of labour, because it is the whole design.** §4 of the design document states it: *"the
widget autonomously handles how those states map to the ncurses visual matrix."* You declare **what the
messages are**; the widget decides **where they go and what they look like**. Everything below follows
from that, including the things a layout is refused.

### 13.1 Declaring one in a layout

**The type**, and it takes no `children:`. `WidgetType` has it and `ContainerType` deliberately does
not:

```ebnf
WidgetType   = "text_view" | "button" | "image_view" | "edit_text"
             | "check_box" | "radio_button" | "switch"
             | "progress_bar" | "seek_bar" | "emoji_table"
             | "conversation_window" ;
```

**It IS a container to the engine, and that divergence is deliberate.** `IrcitUiIsContainer` answers
true for this kind, because the widget owns exactly one child — the message viewport, §13.2 — which it
synthesizes itself. What answering true buys is the engine's own frame (a bordered **leaf** is never
framed, which this widget was) and the engine's own scrollbar, wheel and scroll-thumb, all of which key
on that predicate. It is the first kind that is an engine container while **not** being a grammar
`ContainerType`; those two lists are otherwise kept in step, and here they must not be.

**The three attributes**, and they are the whole of what a layout may say:

| Key | Type | Range | Default | What it does |
|---|---|---|---|---|
| `bubble_max_width_percent` | integer | 1-100 | **65** | The share of the widget's width one bubble may occupy. What makes a long message reflow rather than run the full width of the glass |
| `bubble_collapse_lines` | integer | ≥ 1 | **6** | The truncation ceiling: a longer message is drawn to this many lines with an ellipsis, and opened in place by a click |
| `bubble_avatar_gutter_cols` | integer | 6-12 | **absent: none** | Columns reserved at the viewport's left edge for an inbound block's avatar. Six is five columns of avatar and one of clearance, and it is the floor because the timestamps occupy the same columns. **Absent means no avatars**, so a layout that never asked for them does not grow a gutter |

Two rules the engine adds around them, both measurements rather than preferences:

- the body width is **floored at 25 columns** — the percentage alone would allow an unreadable bubble on
  a narrow widget;
- and **capped at 58 columns**. A percentage is not a ceiling: at 65 % of a 200-column terminal a bubble
  would be 130 columns wide, *no* message would ever reach the truncation limit, and nothing would ever
  collapse.

**Three things a layout may NOT say**, each refused by the validator *and* the generator, neither
relying on the other having been run:

| Refused | Why |
|---|---|
| `children:` | the widget's rows are not nodes, and the one child it owns is its own business. The viewport is in neither `WidgetType` nor `ContainerType`, so it cannot be declared either |
| `layout_width: wrap_content` (either axis) | the widget's width drives its own reflow, so a measured width would come from a measurement of itself. The engine's fallback for a kind with no wrap branch answers **one column** — a wrong answer rather than a conservative one |
| `scrollable: false` | a conversation is scrollable by its nature and the widget sets that flag itself. A conversation that cannot be scrolled is a defect rather than a choice, and the bar is drawn only when there is overflow, so one that fits its box carries none either way |

**There are no per-message keys.** No `bubble_direction`, no `bubble_cluster`, no `bubble_status`. Those
are **live values** — they arrive from your application as messages are appended, and a layout file
cannot hold a live value. A key that resolves to nothing is a trap rather than a reservation, which is
what C25 enforces.

**A template**, from `layout/conversation_panel.yaml`:

```yaml
  - id: conv_window
    type: conversation_window
    layout_width: match_parent
    layout_height: match_parent      # NOT a fixed height: see below
    bubble_max_width_percent: 65
    bubble_collapse_lines: 6
    theme_class: surface
```

`layout_height: match_parent` is not decoration. A `conversation_window` given a fixed height stays
that height however large its parent is — measured, a full-screen panel still produced a thirteen-row
widget, so the conversation showed one message and the tail and the typing row collided whatever else
was fixed.

### 13.2 The widget's parts

**A container, one child, and the engine's chrome.** The widget owns a single
`IRCITUI_MESSAGE_VIEWPORT`, which it synthesizes in the layout pass. The viewport is **stateless** — the
messages, the geometry and the scroll offset all live on the conversation window — so nothing is
allocated, freed or re-bound for it across `IrcitUiScreenReset`. That it can be found by id is an
accident of the tree walk, not a contract; resolve the conversation window.

The split buys three things: the matrix has **its own window**, so the bubbles are clipped by ncurses
rather than by the painter's own arithmetic; the hit test resolves to it, so "the matrix" and "the
chrome" are different nodes; and the container's last column is free for the scrollbar.

**The scrollbar is the engine's, not the widget's.** `ui_engine.c` paints it, scrolls it by wheel and
drags its thumb in `IrcitUiDispatchKey`, all keyed on `IRCITUI_F_SCROLLABLE` and the overflow the
widget publishes. Nothing in the widget draws one. A cell on the bar resolves to the **container**; a
cell inside the matrix resolves to the **viewport**. The bar appears only when the content is longer
than the box, so there is no key for it and nothing to switch off.

### 13.3 The application's seam

Twelve functions, in `ircituilib/ircitui_conversation.h`. Every one takes the node, which you resolve
with `IrcitUiFind`.

```c
int  IrcitUiConversationWindowAppend(IrcitUiNode *n, const IrcitUiConversationMessage *msg);
void IrcitUiConversationWindowSetTyping(IrcitUiNode *n, int is_typing, const char *sender);
void IrcitUiConversationWindowToggleTimestamps(IrcitUiNode *n, int visible);
int  IrcitUiConversationWindowScroll(IrcitUiNode *n, int delta_rows);
int  IrcitUiConversationWindowToggleCollapse(IrcitUiNode *n, int index);
int  IrcitUiConversationWindowHandleClick(IrcitUiNode *n, int screen_y, int screen_x);
int  IrcitUiConversationWindowRowCount(const IrcitUiNode *n);
int  IrcitUiConversationWindowRowInfo(const IrcitUiNode *n, int i, IrcitUiConversationRow *out);
int  IrcitUiConversationWindowMessageInfo(const IrcitUiNode *n, int i, IrcitUiConversationMessage *out);
int  IrcitUiConversationWindowRowAt(const IrcitUiNode *n, int y, int x);
IrcitUiConversationTokenTarget
     IrcitUiConversationWindowResolveTokenAt(const IrcitUiNode *n, int y, int x, uint64_t *msg_id);
void IrcitUiConversationWindowPaint(IrcitUiNode *n);   /* the engine's — do not call it */
```

**Either node is accepted, and the coordinates are the viewport's.** The engine hands the *viewport* to
the click path, because a hit test resolves to it; an application resolves the *conversation window*.
Both are resolved to the one owner inside the widget. `y` and `x` are local to the widget's content
box, which is the viewport's box — a host doing its own arithmetic subtracts that, not the container's
frame.

**`RowAt` and `ResolveTokenAt` are two depths of one question.** `RowAt` says which message a cell is
on; `ResolveTokenAt` says which part of it — `IRCITUI_TOKEN_NAME_TAG`, `IRCITUI_TOKEN_REPLY`,
`IRCITUI_TOKEN_BODY`, `IRCITUI_TOKEN_BADGE`, or `IRCITUI_TOKEN_NONE` for a cell no bubble claims.
`NONE` is a real answer and not a fallback: a router that returned the nearest message would route
clicks to a bubble the pointer never touched.

**The message contract**, and the three fields where a caller's expectation will be wrong:

```c
typedef struct IrcitUiConversationMessage {
    uint64_t                 message_id;
    char                     sender_name[IRCITUI_SENDER_NAME_MAX];    /*  32 */
    char                     timestamp[IRCITUI_TIMESTAMP_MAX];        /*  16 */
    char                    *text_content;                            /* COPIED on append */
    IrcitUiMessageDirection  direction;   /* inbound | outbound | system */
    IrcitUiMessageStatus     status;      /* sending | delivered | read | expired */
    IrcitUiClusterPosition   cluster_pos; /* ADVISORY: the widget computes it */
    signed char              sender_color_tag;                        /* NOT READ */
    bool                     has_reply_context;
    uint64_t                 reply_msg_id;
    char                    *reply_snippet;                           /* COPIED, or NULL */
    char                     reply_sender[IRCITUI_SENDER_NAME_MAX];
    IrcitUiMessageReaction   reactions[IRCITUI_REACTION_BADGES_MAX];  /*   8 */
    int                      reaction_count;
    bool                     is_collapsed;
} IrcitUiConversationMessage;
```

- **`cluster_pos` is advisory.** The widget computes it — it is a property of the messages either side of
  this one — and overwrites whatever you set.
- **`sender_color_tag` is not read at all.** Group identity has no palette slot and none was invented.
  Senders are told apart by the bubble's **background**, which the widget derives from the sender's name.
- **`is_collapsed` is the truncation flag.** Set it and the message draws to the ceiling with an ellipsis;
  **a click on it clears it and the message opens in place**, moving every row below it. A body long
  enough to need it is collapsed for you when it is appended.

**Ownership: the widget COPIES.** `text_content` and `reply_snippet` are duplicated on append, so you own
what you passed and may free or reuse it the moment `Append` returns. That is what a network reader with
a reused buffer needs. It is the **opposite** of `IrcitUiWidgetDef`'s borrowed ids (§4), and the two are
documented differently because they are different problems.

**Which node to append to: whichever one the framework is driving.** `IrcitUiMainLooper` calls
`IrcitUiScreenReset()` before its first frame, and the reset re-clones the screen tree — so a node you
captured before it is discarded by it. Each clone is given its **own, empty** conversation state, so
appending to the *model* node does not show on the clone. The working order is **reset, then resolve the
node from the screen tree, then append**: that is what `src/examples/conversation/conversation_demo.c`
does, and the note is here because it is not discoverable from the outside.

### 13.4 What the widget does on its own

None of this needs a call from you:

- **clustering** — a forward pass over runs of one sender, so a run of one is `lone`, a two-message run is
  `top`/`bottom`, and a longer one gains `middle`s. Every run is closed by the pass, so a single-message
  run followed by a different sender cannot be left open;
- **the shape** — a top rule only at a run's start, a bottom rule only at its end, and the corners from
  two rules: **line count** sets the profile, one line being a pill (`╭ ╮ ╰ ╯`) and two or more a
  paragraph box (`┌ ┐ └ ┘`); and **the wall side** squares a corner where it meets a neighbour, so a run
  reads as one stack against the wall. A lone one-line bubble is therefore a whole pill, and a lone
  multi-line one a whole box;
- **reflow and heights** — the body wraps at the ceiling above, and the row's height follows from what it
  contains: a name tag, a reply header, the body's lines, a badge row;
- **the viewport** — bottom-anchored, so the newest message is the one you see. Scroll up and a new
  message does **not** move the view out from under you: the offset grows by exactly the new row's
  height;
- **the marks** — `○ ✓ ✓✓` for sending, delivered and read on an outbound bubble; `A_DIM` over a whole
  expired one; a centred borderless line for a system notice;
- **the fill** — the sender's own dark shade inbound, the `accent` role outbound, and the `panel` role
  where the terminal has too few colours for shades. The whole bubble is filled, not only its text;
- **the badges** — the emoji **and its tally**, fused to the sender's edge;
- **the avatar** — with a gutter reserved (`bubble_avatar_gutter_cols`), an inbound message that opens a
  block carries a 5 × 3 half-block grid at the gutter's left column: the sender's primary as its colour,
  the first column of their name as its initial, and one of six stipples as its texture, all three from
  the name's hash. Every other row of the block reserves the same columns and draws nothing, so the block
  stays aligned. A tap on it resolves to `IRCITUI_TOKEN_AVATAR` and `HandleClick` **declines** it, as it
  declines a status indicator's, so the click is yours;
- **the pulse** — the sender who is typing, and only them, has their avatar lit (`IrcitUiConversationWindowAnimate`,
  phase 0 `A_BOLD` / phase 1 `A_DIM`). Nothing drives it for you: `IrcitUiConversationWindowAnimationDue`
  answers whether anything wants a tick, and **the cadence is the caller's**, the same split the caret's
  blink follows;
- **the stamps** — `IrcitUiConversationWindowToggleTimestamps` reveals one dimmed row inside each bubble,
  below the badges and above the end rule. A stamp closes a **run, not a bubble**: joined consecutive
  messages carry one on their last member, because a block's time is its end's. A system notice has no box
  and carries none.

### 13.5 What it does not do

Better said here than assumed: **no search and no images**. A message body is reflowed and truncated; it
is not parsed for markup. **Animation is the caller's** (see the pulse above) and there is no per-sender
*image* avatar: the grid is drawn from the character set, and the only per-sender variation is the letter,
the colour and the texture.

**The wheel does not reach the widget.** The engine's wheel handler writes `container.scroll_y` counting
rows **from the top**, while this widget's model counts **back from the newest**, so a wheel turn handed
to the widget through the engine arrives inverted and, at the default position, clamped. A host that wants
the wheel can call `IrcitUiConversationWindowScroll` from its own input path, which takes the field the
way the model reads it — that is what the conversation demo does. The mismatch is recorded as an open
defect rather than papered over.

And the one thing you cannot do yet. `IrcitUiInputAppend` is the library's **only** public input
function: there is no `IrcitUiInputGet` and no `IrcitUiInputClear`. A host can put text into the focused
field and cannot read it back, so **type-and-send needs those two functions to exist**. Recorded in
`IRCITUILIB_CONVERSATION_WINDOW_DESIGN.md` §3, with the signatures it would take.

### 13.6 Scrolling, and the scrollbar

**The bar is not the widget's and there is no key for it.** The widget sets
`IRCITUI_F_SCROLLABLE` on itself in its layout pass, so `ui_engine.c`'s bar painter, its wheel
handler and its scroll-thumb drag all attach with no code in the widget — and no layout key can turn
any of it off. The bar is drawn only when the content is longer than the box, so a conversation that
fits carries none either way, and there is nothing to suppress.

**Where it is drawn.** In the container's **last column**. For a *bordered* container that column is
the border, which the content box already excludes, so nothing is reserved and the bar sits on the
frame; for a *borderless* one the widget's packer gives up a column to it, which is why the message
viewport is one column narrower than its container. Measured on a bordered scratch container: box
columns 2..25, bar 25, content 3..24.

**The two numbers the engine reads, and the widget publishes.**

| field | meaning |
|---|---|
| `container.scroll_y` | the viewport offset in **rows above the bottom**. `0` is pinned to the newest message, which is the state a chat is in almost all the time |
| `container.columns` | the **overflow**: how many rows of content do not fit. This is the bar's `maxs` |

Both are written by `sComputeRows` from the model's own geometry, in the same pass that clamps the
offset — so the scrollbar cannot disagree with the model about how much there is to scroll. That is
deliberate rather than incidental: the engine's **linear** packer derives its overflow one row high,
because it measures the content against the container's origin while a bordered container's first
content row sits one row lower. This widget's packer does not reproduce that; it publishes the number
the model computed, and the two agree by construction.

**The thumb's geometry**, all of it in `ui_engine.c`:

```
track = h − (border ? 2 : 0)
thumb = track * track / (track + maxs)          maxs = container.columns
pos   = (border ? 1 : 0) + (track − thumb) * container.scroll_y / maxs
```

A proportional thumb, not a fixed one: the track is the bar's height and the thumb's share of it is
the visible fraction of the content. The track is `ACS_VLINE` with `A_DIM`; the thumb is a **blank
cell** carrying `A_REVERSE` and a green pair, which renders as a solid block. The cell carries its
attributes as part of the `chtype` and not under a `wattron` — measured, an attribute set around a
blank write does not reliably reach the cells.

**Three ways to move the offset, and they end in the same field:**

| route | how it works |
|---|---|
| the API | `IrcitUiConversationWindowScroll(n, delta_rows)` writes `container.scroll_y` and asks for a layout, in which the model re-clamps it. Returns the offset actually taken |
| the wheel | the engine walks **up from the node the wheel landed on** to the nearest `IRCITUI_F_SCROLLABLE` ancestor — so a wheel event over a bubble scrolls the conversation, with no code in the widget |
| the thumb drag | a press on the bar's column takes the engine's drag register, and a following motion event maps the pointer position back to an offset |

**Keys are the application's.** The widget binds none. The demo binds `↑`/`↓` to the API route, which
is why the two look identical on the glass — they are the same field.

**One consequence a host should know.** The engine writes `container.scroll_y` and then asks for a
layout *without calling the widget*, so the model's rows are a function of (width, height, **offset**)
and the offset is part of the validity test. A host that writes `container.scroll_y` behind the
widget's back gets a correct frame; one that caches its own copy of the rows does not.

### 13.7 The demo, and its reference mode

`src/examples/conversation/conversation_demo.c` is the whole of it — **and it owns its tree.** It used
to read `chat_window` out of `layout/ircitui.yaml`, inheriting `ircitui_demo`'s header, body and footer
and using one node of it. It now supplies its own model through the binder
(`IrcitUiInitialiseFrameworkContext`): a command strip, a framed main window that takes the remainder,
and an input line, all built with `IrcitUiNodeFromDef` and no constraint keys — `IrcitUiWidgetDef`
carries no edge fields, so the shape is a vertical linear whose middle child is `match_parent`.

It links `ircituilib` and **no header from `src/include`** — its compiling is the assertion that
everything in this section is reachable from outside the library.

```sh
./build/dev/ircitui_conversation_demo                        # the live chat
./build/dev/ircitui_conversation_demo --dump-all             # the frame, as text
./build/dev/ircitui_conversation_demo --dump-model           # the computed geometry
./build/dev/ircitui_conversation_demo --dump-screen          # cells and their attributes
./build/dev/ircitui_conversation_demo --sweep                # every cell of every bubble vs the router
./build/dev/ircitui_conversation_demo --corners              # the corner cases, live
./build/dev/ircitui_conversation_demo --dump-reference       # the whole frame, labelled, with hex
./build/dev/ircitui_conversation_demo --prove-composite      # the parts, as addressable nodes
./build/dev/ircitui_conversation_demo --click-engine         # a real click through the engine, twice
./build/dev/ircitui_conversation_demo --cols 120 --rows 40 --dump-all
```

Keys in the live view: **↑/↓** scroll, **t**/**T** the timestamps margin, **q** quit; the scrollbar
drags and the wheel works; printable keys go to the input line. `--cols`/`--rows` resize the **virtual
screen**, so a dump at any geometry is what that terminal would show.

Its dumps are frozen references, regenerated whenever the painter or the model changes, and diffed by
the probe runner. `dev/probes/conversation/FIXTURE-FIELDS.md` says which of the fixture's key paths the
widget reads, which are inert, and why — that table is the difference between a fixture and a test.

## 14. Worked examples

Three examples from `src/examples/ui_main.c`, each going through the pipeline;
then the palette's own worked example, from the progress-indicator track.

### 13.1 Worked examples — the framework

All three are in `src/examples/ui_main.c` and all three go through the pipeline.

**(a) A fetch that fails — F2.** One yield, then `UfsrvPromiseSetError(-1)`.
Thread 0 raises the alert naming the target. The demo contains no call to
`IrcitUiWatchdogAlertShow` at all.

**(b) A fetch that succeeds across stages — F3.** Three yields, then
`UfsrvPromiseSetValue(promise, payload, NULL)`. Thread 0 copies the string into
the focused edit field's `ebuf`:

```c
static char payload[] = "staged_fetch_ok.png  (24 KB)";
...
for (i = 0; i < task_ptr->stages; i++) UfsrvCoroutineYield();
UfsrvPromiseSetValue(task_ptr->promise, payload, NULL);
```

The payload is a file-scope buffer and the release function is `NULL`, which is
the honest pairing: nothing owns it, so nothing frees it.

**(c) A real download — Ctrl-D.** `curl_multi` driven from the coroutine, which
yields on every poll that finds the socket not ready:

```c
curl_multi_add_handle(multi_ptr, easy_ptr);
do {
  curl_multi_perform(multi_ptr, &running);
  if (running) {
    curl_multi_poll(multi_ptr, NULL, 0, 100, NULL);
    UfsrvCoroutineYield();          /* the worker goes back to the pool */
  }
} while (running && ++guard < 400);
```

The body writes a **file** and reports its **path** — the value is the path, and
`kind` is `IRCITUI_BRIDGE_IMAGE`. Thread 0 raises a window showing it. The body
never allocates an image into the UI and never touches a node.

Two details that are the demo's and not the framework's, but which any real
caller will hit: `curl_global_init` must be called **once, from one thread**,
before any worker uses libcurl (its implicit initialisation is not thread-safe),
and the payload must be a **PNG**, because the loader is libpng.

### 13.2 Worked example — the palette

Switching palette on a key, which is the shape most applications want:

```c
/* Bind this to a key in your application's own key handling. */
static void
sCyclePalette(void)
{
  static const char *names[] = {"dark", "light"};
  static unsigned    i;

  const IrcitUiPaletteProfile *p = IrcitUiPaletteProfileByName(names[i++ % 2]);
  if (!p) return;

  IrcitUiPaletteApplyProfile(p);
  IrcitUiPaletteRepaint();                    /* both lines, always */
}
```

Enabling the canopy, for a layout whose backdrop container wears `surface`:

```c
IrcitUiPaletteSetEffects(IRCITUI_PALETTE_EFFECT_CANOPY);

/* Nothing else to arrange: the layout declares which container is the backdrop
   (theme_class: surface), and the loop's cadence is the framework's business. */
```

## 15. The rules that bite

The rules this API enforces, collected in one place, each with the mistake it exists to
prevent.

### 14.1 The rules, collected

1. **A palette change needs a repaint.** Set a slot or apply a profile, then
   `IrcitUiPaletteRepaint()`. Registers are the exception — the effects need none — and the
   notification overlay is the other one, because it repaints itself every frame
   (`IRCITUILIB_PROGRESS_INDICATORS_DESIGN.md` §1).
2. **Thread 0 only.** These call into ncurses; nothing in this library touches colour off the
   main loop, and the slot table is unsynchronised for that reason.
3. **Borrowed strings.** `IrcitUiWidgetDef.theme_class` and `IrcitUiPaletteProfile.name` are
   kept, not copied. Both must outlive their user, and a def must be zero-initialised.
4. **`-1` means "leave it"** in `IrcitUiPaletteSetSlot` for `fg` and `bg`; for `attr` the
   sentinel is `(chtype)-1`, because `A_NORMAL` is 0 and a role has to be able to become
   plain. A profile, by contrast, says *everything* about every slot.
5. **Return values mean something.** `IrcitUiPaletteProfileByName` gives `NULL` for an unknown
   name — check it. `IrcitUiPaletteSlotForName` gives `-1`. `IrcitUiPaletteStyle` gives
   `A_NORMAL` for a slot out of range. **`IrcitUiSnackbarShowAction` gives 0 when the action
   was refused and the message still went up** (§7.1) — the one return value here that can
   mean "half of what you asked for".
6. **A class beats the node's own colours**, and a theme rule selecting on
   `IRCITUI_THEME_CLASS` can therefore never win on a classed node
   (`IRCITUILIB_PROGRESS_INDICATORS_DESIGN.md` §9.11). Style by `ALL`,
   `KIND` or `ID` if you want rules to apply.
7. **A slot is global.** Redefining it changes every node wearing it, including widgets inside
   the library — the notification box and the watchdog alert wear roles of their own. There is
   no per-widget override; the way to keep a node's own colours is not to give it a class.
8. **An action hotkey must be a control code or a `KEY_*` code, and five control codes are
   dead.** Printable is refused by the API; `^C`, `^Q`, `^S`, `^Z` and `^\` never reach
   `getch()` at all (§7.1). Nothing in the signature can tell you that a key you chose is
   unreachable — the sweep in `dev/probes/snackbar/p_hotkeys.c` is the check.
9. **The overlay's callback runs after its box is dismissed**, so it may raise another
   notification. It runs on Thread 0 inside key dispatch and must not block.
10. **A slot is not the only way to reach a role.** `overlay_action` has no `theme_class` path
    — the overlay joins no layout — so the only thing that can occupy that role is a
    notification raised with an action. Seven of the eight roles are reachable from a layout;
    this one is not.
11. **A frame profile is a look, not an effect, and it needs a repaint.** `IrcitUiSetBorderStyle`
    is not a bit in `IRCITUI_PALETTE_EFFECT_*`: it borrows no register, costs nothing at exit,
    and its default is not "off" but "the box that always shipped". It is still a **whole-tree**
    change and every frame has to be written again, so follow it with
    `IrcitUiPaletteRepaint()` — rule 1's reason, in a second place. The same call covers
    `IrcitUiSetShadow`, whose switch turns cells on and off rather than only recolouring them.
12. **Wide profile glyphs need a UTF-8 locale, and the library does not set one.** `╭` is
    U+256D and reaches the screen through `mbstowcs`; under a non-UTF-8 locale that conversion
    fails and the frame falls back to the ACS box — a square frame rather than mojibake, which
    is the deliberate half of the choice, but still not what was asked for. Call
    `setlocale(LC_ALL, "")` as §14.2's own example does.
13. **A key the terminal delivers is not a key the tree acts on.** Rule 8 covers the codes the
    terminal never sends; the other half is the codes the **framework** claims — `IRCITUI_KEY_BS`
    **8**, `IRCITUI_KEY_CTRL_C` 3, `IRCITUI_KEY_CTRL_V` 22, `IRCITUI_KEY_CTRL_U` 21,
    `IRCITUI_KEY_CTRL_X` 24, `IRCITUI_KEY_ESC` 27, `IRCITUI_KEY_DEL` 127, the loop's
    `KEY_F(11)`/`KEY_F(12)` and the `KEY_*` range — none of which is refused with an error. A
    key bound to one of them is consumed by whatever holds focus and **produces nothing**,
    which is indistinguishable from a key that never arrived. `^H` is `IRCITUI_KEY_BS` and
    that is how this was found. `IRCITUILIB_PROGRESS_INDICATORS_DESIGN.md` §9.34
    carries the missing single list.

## 16. If it does not work

Symptom-to-cause, for the failures this API produces. Each row names the rule or the call it
comes back to.

### 15.1 If it does not work

| Symptom | Likely cause |
|---|---|
| The palette changed and the screen did not | `IrcitUiPaletteRepaint()` was not called (rule 1) |
| A classed node kept the colours the layout gave it | the class name is not a slot — C23 should have refused it at build time, so this means the class was set from C, and `IrcitUiPaletteSlotForName` returned `-1` |
| A widget is black on black | a profile left slots unspecified; unmentioned entries are zero (§6.4) |
| The canopy is on but nothing changed | the terminal refused the registers — check `IrcitUiPaletteCanopyActive()` |
| The image in my layout has the wrong colours | **`IRCITUILIB_PROGRESS_INDICATORS_DESIGN.md` §9.7 — a real, open defect.** An `image_view` and a canopy or gradient must not be on screen together |
| The user's terminal colours were reset after exit | expected whenever any effect is enabled (§6.5), and `IrcitUiPaletteRestore()` after `endwin()` is the approximation of a fix |
| The action hotkey does nothing at all | it is one of the five the line discipline keeps — `^C`, `^Q`, `^S`, `^Z`, `^\` (§7.1). Nothing is logged, because the library never sees the key |
| `ShowAction` returned 0 but the box is up | that is the contract: the *action* was refused (printable hotkey, or a callback with no label) and the *message* was raised. If you hid data behind the affordance, put it back (§7.1) |
| The action label overlaps the message | the message is clipped to leave a one-column gap, measured in `p_action label` — so this means a caller set a label wider than the row and `sLabelCols` clipped it to `width - 8`, or the terminal is narrower than 8 columns of box, in which case there is no label at all |
| The label vanished the instant the action fired | the label is deliberately kept while the box falls (§7.1); if it is gone, the box was already hidden, which means something dismissed it twice |
| A box raised from inside an action callback never appeared | check the callback runs *after* the dismiss — that is the shipped order. A caller who calls their own `IrcitUiSnackbarDismiss()` first and then raises will replace correctly; one who re-raises and then dismisses will lose it |
| **A key does nothing at all, and the terminal is delivering it** | the framework claimed the code. Rule 13 lists them — `IRCITUI_KEY_BS` **8** is the one that catches people, because `^H` looks free and is Backspace. Nothing is logged and nothing is drawn, so it is indistinguishable from a dead key. The other half — the five the line discipline keeps — is the row above |
| **I asked for a rounded frame and got a square one** | the locale is not UTF-8. `╭` reaches the screen through `mbstowcs` and fails to convert, and the fallback is the ACS box by design rather than mojibake — so a square frame is the *symptom* of a missing `setlocale(LC_ALL, "")` (rule 12) |
| **The frames did not change at all** | `IrcitUiPaletteRepaint()` was not called. A profile is a different set of glyphs, so no cell already on the glass becomes correct by being reinterpreted — the same reason rule 1 gives, in a second place (rule 11) |
| **The shadow is not there** | in order: `IrcitUiSetShadow(1)` was never called, and it is **off** by default; or the node is not `IRCITUI_F_STACKED` (a plain node is not a popup, and only popups cast — `IrcitUiCreateFloatingWindow` is what sets the flag); or it is the notification box, which is not a node and has no parent window to cast onto (`IRCITUILIB_PROGRESS_INDICATORS_DESIGN.md` §10) |
| **The shadow stayed behind after the popup moved or closed** | the parent did not repaint. The strip lives in the *parent's* cells, so the popup's own disappearance cannot take it away — that is why `IrcitUiPopStacked` raises a layout request. If it happens on a **move**, it is `IRCITUILIB_PROGRESS_INDICATORS_DESIGN.md` §9.29: a non-stacked sibling ordered after the stacked child erases the strip, and no probe builds that tree |
| **A shadow appeared in the wrong place, or two popups interfere** | **`IRCITUILIB_PROGRESS_INDICATORS_DESIGN.md` §9.30 — unverified.** Each stamp is followed by a full-window push of the parent, and with two popups up that push may blank the one drawn earlier. Open one at a time while this is open |
