#!/usr/bin/env python3
"""Gate 6 pass D: full Doxygen for the 62 public functions in ui.h.

Holds the authored @brief / @details / @param / @return / @code{.c} content
keyed by function name, and re-emits each declaration VERBATIM from the current
file so the code can be proved unchanged by token comparison.

  --dry   report coverage only
  --emit  rewrite ui.h
  --dump  write every @code block to /tmp/g6_snippets/<fn>.c for compilation
"""
import os
import re
import sys
import textwrap

sys.path.insert(0, "/tmp")
from g6_uih import load, split  # noqa: E402

PREAMBLE = [
    'IrcitUiNode *root;',
    '',
    'initscr();',
    'cbreak();',
    'noecho();',
    'keypad(stdscr, TRUE);',
    '',
    'IrcitUiDataBind();',
    'IrcitUiScreenReset();',
    'root = IrcitUiScreen.root;',
]

TEARDOWN = [
    'IrcitUiDeflate(root);',
    'endwin();',
]

# ---------------------------------------------------------------- content ---
DOC = {}


def d(name, brief, params=None, ret=None, details=None, code=()):
    DOC[name] = dict(brief=brief, params=params or [], ret=ret,
                     details=details, code=list(code))


d("IrcitUiSetFocus",
  "Set @ref IrcitUiFocus to @p node_ptr, mirroring it in IRCITUI_F_FOCUSED; NULL clears.",
  [("node_ptr", "Node to focus, or NULL to clear focus altogether.")],
  details="Also resets the caret blink to lit, so focus never lands on a field "
          "whose caret is in the middle of a dark half-cycle.",
  code=PREAMBLE + [
      'IrcitUiNode *field = IrcitUiFind(root, "cmdline");',
      '',
      'if (!field) {',
      '  fprintf(stderr, "reference tree has no cmdline node\\n");',
      '  IrcitUiDeflate(root);',
      '  endwin();',
      '  return;',
      '}',
      '',
      'IrcitUiLayout(root, 0, 0, COLS, LINES);',
      'IrcitUiRealize(root);',
      'IrcitUiSetFocus(field);   /* NULL here would clear focus instead */',
      'IrcitUiDraw(root);',
      'doupdate();',
  ] + TEARDOWN)

d("IrcitUiBlinkReset",
  "Light the focused caret and reset the blink phase to lit.",
  code=PREAMBLE + [
      'IrcitUiLayout(root, 0, 0, COLS, LINES);',
      'IrcitUiRealize(root);',
      'IrcitUiSetFocus(IrcitUiFind(root, "cmdline"));',
      'IrcitUiBlinkReset();      /* caret lit immediately after focus moves */',
      'IrcitUiDraw(root);',
      'doupdate();',
  ] + TEARDOWN)

d("IrcitUiCaretVisible",
  "Should @p node_ptr's caret be drawn on this paint?",
  [("node_ptr", "Node being painted.")],
  ret="Non-zero to draw the glyph.",
  details="The focused field's caret blinks; every other edit field's stays "
          "steady, so two fields may show a caret and the blinking one is the "
          "insertion point. The caret's cell is reserved either way, so the "
          "line never jitters.",
  code=PREAMBLE + [
      'IrcitUiNode *field = IrcitUiFind(root, "cmdline");',
      '',
      'if (!field) {',
      '  fprintf(stderr, "reference tree has no cmdline node\\n");',
      '  IrcitUiDeflate(root);',
      '  endwin();',
      '  return;',
      '}',
      '',
      'IrcitUiLayout(root, 0, 0, COLS, LINES);',
      'IrcitUiRealize(root);',
      'IrcitUiSetFocus(field);',
      'if (!IrcitUiCaretVisible(field))',
      '  IrcitUiBlinkReset();   /* this paint would show an empty caret cell */',
      'IrcitUiDraw(root);',
      'doupdate();',
  ] + TEARDOWN)

d("IrcitUiBlinkAdvance",
  "Advance the blink phase.",
  ret="Non-zero if a repaint is needed.",
  details="0 when no visible focused edit field is on screen, so an idle tick "
          "with a button or menu focused produces no output at all.",
  code=PREAMBLE + [
      'int key;',
      '',
      'IrcitUiLayout(root, 0, 0, COLS, LINES);',
      'IrcitUiRealize(root);',
      'timeout(PRIV_CONFIG_DEFAULT_CARET_BLINK_MS);',
      '',
      'key = getch();',
      'if (key == ERR) {                 /* blink tick, not input */',
      '  if (IrcitUiBlinkAdvance()) {',
      '    IrcitUiDraw(root);',
      '    doupdate();',
      '  }',
      '} else {',
      '  IrcitUiDispatchKey(root, key);',
      '}',
  ] + TEARDOWN)

d("IrcitUiIsFocusable",
  "Is @p node_ptr a node the keyboard may land on?",
  [("node_ptr", "Node to test; NULL is not focusable.")],
  ret="Non-zero if @p node_ptr is shown, enabled, unclipped, and takes input.",
  code=PREAMBLE + [
      'IrcitUiNode *candidate = IrcitUiFind(root, "menu_file");',
      '',
      'if (!candidate) {',
      '  fprintf(stderr, "reference tree has no menu_file node\\n");',
      '  IrcitUiDeflate(root);',
      '  endwin();',
      '  return;',
      '}',
      '',
      'IrcitUiLayout(root, 0, 0, COLS, LINES);',
      'IrcitUiRealize(root);',
      'if (IrcitUiIsFocusable(candidate))',
      '  IrcitUiSetFocus(candidate);',
      'else',
      '  fputs("menu_file cannot take focus in this layout\\n", stderr);',
  ] + TEARDOWN)

d("IrcitUiTopLayer",
  "Topmost stacked popup, the one a key press should reach first.",
  ret="Topmost IRCITUI_F_STACKED child of the screen root, or NULL if none is open.",
  code=PREAMBLE + [
      'IrcitUiNode *top;',
      '',
      'IrcitUiLayout(root, 0, 0, COLS, LINES);',
      'IrcitUiRealize(root);',
      'IrcitUiInflate(root, "file_menu");',
      '',
      'top = IrcitUiTopLayer();',
      'if (!top) {',
      '  fputs("no popup open\\n", stderr);',
      '  IrcitUiDeflate(root);',
      '  endwin();',
      '  return;',
      '}',
      'IrcitUiCloseTopPopup();   /* tears down exactly this node */',
  ] + TEARDOWN)

d("IrcitUiFocusInLayer",
  "Move focus to the first focusable in @p layer_ptr, unless it is already there.",
  [("layer_ptr", "Subtree to resolve focus within; NULL is ignored.")],
  details="Retention rule: a focusable @ref IrcitUiFocus already inside "
          "@p layer_ptr keeps focus, so an activation cannot drag it off the "
          "node the user chose.",
  code=PREAMBLE + [
      'IrcitUiNode *popup;',
      '',
      'IrcitUiLayout(root, 0, 0, COLS, LINES);',
      'IrcitUiRealize(root);',
      'popup = IrcitUiInflate(root, "file_menu");',
      'if (!popup) {',
      '  fputs("file_menu template not found\\n", stderr);',
      '  IrcitUiDeflate(root);',
      '  endwin();',
      '  return;',
      '}',
      '',
      'IrcitUiFocusInLayer(popup);   /* NULL would simply be ignored */',
      'IrcitUiDraw(root);',
      'doupdate();',
  ] + TEARDOWN)

d("IrcitUiFocusTop",
  "Re-resolve focus in the top popup if one is open, else in the main tree.",
  code=PREAMBLE + [
      'IrcitUiLayout(root, 0, 0, COLS, LINES);',
      'IrcitUiRealize(root);',
      'IrcitUiInflate(root, "tools_panel");',
      '',
      'IrcitUiFocusTop();        /* focus follows the popup that just opened */',
      'IrcitUiDraw(root);',
      'doupdate();',
  ] + TEARDOWN)

d("IrcitUiPopupFocusOnOpen",
  "@ref IrcitUiEventCallback for a template that declares a focus request.",
  [("node_ptr", "The popup just inflated.")],
  details="Bind this to @c on_inflated in a popup template to have opening it "
          "move focus into it; a template that names nothing leaves focus "
          "where it was.",
  code=PREAMBLE + [
      'IrcitUiWidgetDef win = {',
      '  .id = "help_win", .kind = IRCITUI_FRAME,',
      '  .text = "Help",',
      '  .width_mode = IRCITUI_SIZE_EXACT, .width = 40,',
      '  .height_mode = IRCITUI_SIZE_EXACT, .height = 12,',
      '  .flags = IRCITUI_F_VISIBLE | IRCITUI_F_ENABLED | IRCITUI_F_BORDER | IRCITUI_F_STACKED,',
      '  .fg = 7, .bg = 0, .bdr = 2',
      '};',
      'IrcitUiNode *popup;',
      '',
      'IrcitUiLayout(root, 0, 0, COLS, LINES);',
      'IrcitUiRealize(root);',
      'popup = IrcitUiCreateFloatingWindow(&win);',
      'if (!popup) {',
      '  fputs("out of memory building the popup\\n", stderr);',
      '  IrcitUiDeflate(root);',
      '  endwin();',
      '  return;',
      '}',
      '',
      '/* on_inflated runs as the popup is attached, so this is the hook a',
      '   template uses to move focus into itself. */',
      'IrcitUiPopupFocusOnOpen(popup);',
      'IrcitUiDraw(root);',
      'doupdate();',
  ] + TEARDOWN)

d("IrcitUiIsContainer",
  "May @p k own children?",
  [("k", "Kind to test.")],
  ret="Non-zero if @p k may own children.",
  code=[
      'IrcitUiWidgetDef def = {0};',
      'IrcitUiNode     *node;',
      '',
      'if (!IrcitUiIsContainer(def.kind)) {',
      '  fputs("kind cannot take children; IrcitUiAddChild would return NULL\\n", stderr);',
      '  return;',
      '}',
      '',
      'IrcitUiDataBind();',
      'IrcitUiScreenReset();',
      'node = IrcitUiAddChild(IrcitUiScreen.root, &def);',
      'if (!node)',
      '  fputs("child rejected\\n", stderr);',
  ])

d("IrcitUiLayout",
  "Assign @p node_ptr a box and pack its children.",
  [("node_ptr", "Node to place."),
   ("x", "Origin column."),
   ("y", "Origin row."),
   ("w", "Width in cells; clamped to at least 1."),
   ("h", "Height in cells; clamped to at least 1.")],
  details="Re-run on every resize, then @ref IrcitUiRealize, so the windows "
          "follow the new extents.",
  code=PREAMBLE + [
      'int key;',
      '',
      'root = IrcitUiScreen.root;',
      'IrcitUiLayout(root, 0, 0, COLS, LINES);',
      'IrcitUiRealize(root);',
      'IrcitUiDraw(root);',
      'doupdate();',
      '',
      'key = getch();',
      'if (key == KEY_RESIZE) {',
      '  IrcitUiLayout(root, 0, 0, COLS, LINES);   /* COLS/LINES are new now */',
      '  IrcitUiRealize(root);',
      '  IrcitUiDraw(root);',
      '  doupdate();',
      '}',
  ] + TEARDOWN)

d("IrcitUiRealize",
  "Create or recreate curses windows for @p node_ptr and descendants.",
  [("node_ptr", "Subtree to realise.")],
  details="STACKED nodes get `newwin`; nested nodes get `derwin`. Safe to call "
          "again after a resize: each node's edit buffer survives, because the "
          "buffer outlives the window.",
  code=PREAMBLE + [
      'IrcitUiLayout(root, 0, 0, COLS, LINES);',
      'IrcitUiRealize(root);',
      'IrcitUiDraw(root);',
      'doupdate();',
      '',
      '/* A resize must re-layout before it re-realises, or the windows are',
      '   rebuilt at the old extents. */',
      'IrcitUiLayout(root, 0, 0, COLS, LINES);',
      'IrcitUiRealize(root);',
  ] + TEARDOWN)

d("IrcitUiDraw",
  "Paint @p node_ptr then children. Does not call `doupdate()`.",
  [("node_ptr", "Subtree to paint.")],
  details="The caller owns the flush. Paint the whole tree rather than one "
          "field: `IrcitUiDraw` ends in `wnoutrefresh`, so refreshing a single "
          "window would copy its cells over a stacked popup's rectangle.",
  code=PREAMBLE + [
      'IrcitUiLayout(root, 0, 0, COLS, LINES);',
      'IrcitUiRealize(root);',
      'IrcitUiDraw(root);',
      'doupdate();               /* the flush IrcitUiDraw leaves to the caller */',
  ] + TEARDOWN)

d("IrcitUiDeflate",
  "`delwin` @p node_ptr and descendants; does not free the node.",
  [("node_ptr", "Subtree to tear down; must not be NULL.")],
  details="The nodes themselves survive, and so does the text they hold. To "
          "drop a popup and free it, use @ref IrcitUiPopStacked instead.",
  code=PREAMBLE + [
      'IrcitUiLayout(root, 0, 0, COLS, LINES);',
      'IrcitUiRealize(root);',
      'IrcitUiDraw(root);',
      'doupdate();',
      '',
      'IrcitUiDeflate(root);     /* windows gone, nodes and text still there */',
      'endwin();',
  ])

d("IrcitUiDispatchKey",
  "Route one key or `KEY_MOUSE` through capture, hit-test, and focus.",
  [("node_ptr", "Subtree to dispatch into, normally the screen root."),
   ("key", "Key returned by `getch()`, or `KEY_MOUSE`.")],
  ret="Non-zero if consumed.",
  details="`ERR` is not a key: an idle blink tick hands back `ERR`, and this "
          "returns 0 for it rather than letting a toggle handler fire on it.",
  code=PREAMBLE + [
      'int key;',
      '',
      'IrcitUiLayout(root, 0, 0, COLS, LINES);',
      'IrcitUiRealize(root);',
      'IrcitUiSetFocus(IrcitUiFind(root, "cmdline"));',
      '',
      'key = getch();',
      'if (key != ERR && !IrcitUiDispatchKey(root, key))',
      '  fputs("key not consumed by any node\\n", stderr);',
      'IrcitUiDraw(root);',
      'doupdate();',
  ] + TEARDOWN)

d("IrcitUiFind",
  "Depth-first search for an id under @p node_ptr.",
  [("node_ptr", "Subtree to search; must not be NULL."),
   ("id_ptr", "Id to match; must not be NULL.")],
  ret="First node whose id matches, or NULL if there is none.",
  code=PREAMBLE + [
      'IrcitUiNode *field = IrcitUiFind(root, "cmdline");',
      '',
      'if (!field) {',
      '  fprintf(stderr, "%s\\n", "no node with id cmdline");',
      '  IrcitUiDeflate(root);',
      '  endwin();',
      '  return;',
      '}',
      '',
      'IrcitUiLayout(root, 0, 0, COLS, LINES);',
      'IrcitUiRealize(root);',
      'IrcitUiSetFocus(field);',
  ] + TEARDOWN)

d("IrcitUiMouseInit",
  "Enable mouse reports on stdout and in ncurses.",
  details="Call once after `initscr`, before the first `getch`: the mask "
          "deliberately includes motion events, without which a press-and-drag "
          "can never extend a selection.",
  code=[
      'initscr();',
      'cbreak();',
      'noecho();',
      'keypad(stdscr, TRUE);',
      '',
      'IrcitUiMouseInit();',
      '',
      'IrcitUiDataBind();',
      'IrcitUiScreenReset();',
      'IrcitUiLayout(IrcitUiScreen.root, 0, 0, COLS, LINES);',
      'IrcitUiRealize(IrcitUiScreen.root);',
      'IrcitUiDraw(IrcitUiScreen.root);',
      'doupdate();',
      '',
      'fputs("\\033[?1006l\\033[?1003l\\033[?1002l\\033[?1000l", stdout);',
      'fflush(stdout);                       /* undo the terminal side */',
      'IrcitUiDeflate(IrcitUiScreen.root);',
      'endwin();',
  ])

d("IrcitUiHitTest",
  "Deepest visible node containing (@p x,@p y).",
  [("node_ptr", "Subtree to test, normally the screen root."),
   ("y", "Row in screen cells."),
   ("x", "Column in screen cells.")],
  ret="Deepest matching node, or NULL if the point falls on none.",
  code=PREAMBLE + [
      'IrcitUiNode *hit;',
      '',
      'IrcitUiLayout(root, 0, 0, COLS, LINES);',
      'IrcitUiRealize(root);',
      '',
      'hit = IrcitUiHitTest(root, LINES / 2, COLS / 2);',
      'if (hit)',
      '  IrcitUiSetFocus(hit);   /* only if it is focusable; test first */',
      'else',
      '  fputs("centre of the canvas is empty\\n", stderr);',
  ] + TEARDOWN)

d("IrcitUiDataBind",
  "Fill the static nodes declared in `generated/ui_generated.c`.",
  details="Call once per process, before the first @ref IrcitUiScreenReset or "
          "@ref IrcitUiClone: it is what gives the reference model its text "
          "and ids. Calling it twice re-fills the same nodes, which is "
          "harmless but pointless.",
  code=PREAMBLE + [
      '/* root is now a clone of the reference model; without the bind above',
      '   the clone would be structurally correct and textually empty. */',
      'if (!IrcitUiRoot.root) {',
      '  fputs("reference tree is empty\\n", stderr);',
      '  endwin();',
      '  return;',
      '}',
      '',
      'if (IrcitUiVerifyLayout(COLS, LINES) != 0)',
      '  fputs("reference model is not structurally sound\\n", stderr);',
      '', 'IrcitUiDeflate(root);', 'endwin();',
  ])

d("IrcitUiEditInput",
  "Default editor: insert and delete into @p node_ptr's edit buffer.",
  [("node_ptr", "The edit_text node."),
   ("key", "Key to apply; `KEY_MOUSE` and `ERR` are ignored.")],
  details="Bind it to @c on_input with @c IrcitUiAddChild or @c "
          "IrcitUiAddButton, or let the generated tree bind it for you.",
  code=PREAMBLE + [
      'IrcitUiWidgetDef def = {',
      '  .id = "note", .kind = IRCITUI_EDIT_TEXT, .text = "",',
      '  .width_mode = IRCITUI_SIZE_MATCH, .height_mode = IRCITUI_SIZE_WRAP,',
      '  .flags = IRCITUI_F_VISIBLE | IRCITUI_F_ENABLED,',
      '  .on_input = IrcitUiEditInput,',
      '  .fg = 7, .bg = 0',
      '};',
      'IrcitUiNode *field;',
      '',
      'IrcitUiLayout(root, 0, 0, COLS, LINES);',
      'IrcitUiRealize(root);',
      'field = IrcitUiAddChild(IrcitUiFind(root, "canvas"), &def);',
      'if (!field) {',
      '  fputs("canvas is not a container\\n", stderr);',
      '  IrcitUiDeflate(root);',
      '  endwin();',
      '  return;',
      '}',
      '',
      'IrcitUiSetFocus(field);',
      'IrcitUiEditInput(field, \'h\');',
      'IrcitUiEditInput(field, KEY_BACKSPACE);',
      'IrcitUiDraw(root);',
      'doupdate();',
  ] + TEARDOWN)

d("IrcitUiButtonInput",
  "Default button: inflate its template, or pop its own popup closed.",
  [("node_ptr", "The button node."),
   ("key", "Key to apply; only `KEY_MOUSE`, Enter, and newline activate.")],
  code=PREAMBLE + [
      'IrcitUiWidgetDef def = {',
      '  .id = "btn_help", .text = "Help",',
      '  .width_mode = IRCITUI_SIZE_MATCH, .height_mode = IRCITUI_SIZE_WRAP,',
      '  .flags = IRCITUI_F_VISIBLE | IRCITUI_F_ENABLED | IRCITUI_F_CLICKABLE,',
      '  .on_input = IrcitUiButtonInput,',
      '  .fg = 7, .bg = 0',
      '};',
      'IrcitUiNode *btn;',
      '',
      'IrcitUiLayout(root, 0, 0, COLS, LINES);',
      'IrcitUiRealize(root);',
      'btn = IrcitUiAddChild(IrcitUiFind(root, "canvas"), &def);',
      'if (!btn) {',
      '  fputs("canvas is not a container\\n", stderr);',
      '  IrcitUiDeflate(root);',
      '  endwin();',
      '  return;',
      '}',
      '',
      'IrcitUiButtonInput(btn, \'\\n\');   /* a bare letter would be ignored */',
      'IrcitUiDraw(root);',
      'doupdate();',
  ] + TEARDOWN)

d("IrcitUiEmojiInput",
  "Default emoji cell click: append the clicked glyph via @ref IrcitUiInputAppend.",
  [("node_ptr", "The emoji_table node."),
   ("key", "Key to apply; anything but `KEY_MOUSE` is ignored.")],
  details="The cell is derived from the coordinates of the last mouse event, "
          "so this only does anything inside a `KEY_MOUSE` dispatch.",
  code=PREAMBLE + [
      'IrcitUiNode *grid;',
      'int key;',
      '',
      'IrcitUiLayout(root, 0, 0, COLS, LINES);',
      'IrcitUiRealize(root);',
      'grid = IrcitUiAddEmojiTable(IrcitUiFind(root, "canvas"), NULL, 8);',
      'if (!grid) {',
      '  fputs("canvas is not a container\\n", stderr);',
      '  IrcitUiDeflate(root);',
      '  endwin();',
      '  return;',
      '}',
      '',
      'key = getch();',
      'if (key == KEY_MOUSE)',
      '  IrcitUiEmojiInput(grid, key);',
      'IrcitUiDraw(root);',
      'doupdate();',
  ] + TEARDOWN)

d("IrcitUiInputAppend",
  "Append UTF-8 onto the focused edit_text, or onto `cmdline`.",
  [("utf8_ptr", "NUL-terminated UTF-8; NULL or empty is ignored.")],
  details="Truncated to fit at a character boundary; a control byte is "
          "dropped. The target is re-derived from @ref IrcitUiFocus on every "
          "call, so append to a specific field with @ref IrcitUiClipPaste "
          "instead if focus is not where you want the text.",
  code=PREAMBLE + [
      'IrcitUiLayout(root, 0, 0, COLS, LINES);',
      'IrcitUiRealize(root);',
      'IrcitUiSetFocus(IrcitUiFind(root, "cmdline"));',
      '',
      'IrcitUiInputAppend("hello ");',
      'IrcitUiInputAppend("\\xf0\\x9f\\x98\\x80");   /* +1 emoji, 4 bytes */',
      'IrcitUiDraw(root);',
      'doupdate();',
  ] + TEARDOWN)

d("IrcitUiCloseTopPopup",
  "Destroy the last STACKED child of the screen root.",
  details="A no-op when no popup is open. Equivalent to @ref IrcitUiTopLayer "
          "followed by @ref IrcitUiPopStacked.",
  code=PREAMBLE + [
      'IrcitUiLayout(root, 0, 0, COLS, LINES);',
      'IrcitUiRealize(root);',
      'IrcitUiInflateFloat(root, "tools_panel");',
      '',
      'IrcitUiCloseTopPopup();   /* safe with nothing open */',
      'IrcitUiFocusTop();',
      'IrcitUiDraw(root);',
      'doupdate();',
  ] + TEARDOWN)

d("IrcitUiClipCopy",
  "Copy the selection, or the whole text, of @p node_ptr into the process clip.",
  [("node_ptr", "Node to copy from; NULL is ignored.")],
  details="The clip is a single process-wide buffer, not the X or Wayland "
          "selection.",
  code=PREAMBLE + [
      'IrcitUiNode *field = IrcitUiFind(root, "cmdline");',
      '',
      'if (!field) {',
      '  fputs("no cmdline node\\n", stderr);',
      '  IrcitUiDeflate(root);',
      '  endwin();',
      '  return;',
      '}',
      '',
      'IrcitUiLayout(root, 0, 0, COLS, LINES);',
      'IrcitUiRealize(root);',
      'IrcitUiClipCopy(field);    /* whole text when nothing is selected */',
  ] + TEARDOWN)

d("IrcitUiClipPaste",
  "Paste the clip into @p node_ptr, or into `cmdline`.",
  [("node_ptr", "Target edit_text; NULL, or a node of another kind, falls "
                "back to `cmdline`.")],
  details="Pastes into the node this call resolves rather than into whatever "
          "holds focus, so a context-menu paste cannot land in a different "
          "field.",
  code=PREAMBLE + [
      'IrcitUiNode *field = IrcitUiFind(root, "cmdline");',
      '',
      'IrcitUiLayout(root, 0, 0, COLS, LINES);',
      'IrcitUiRealize(root);',
      'IrcitUiClipCopy(IrcitUiFind(root, "canvas"));',
      'IrcitUiClipPaste(field);   /* NULL would target cmdline anyway */',
      'IrcitUiDraw(root);',
      'doupdate();',
  ] + TEARDOWN)

d("IrcitUiClipClear",
  "Clear @p node_ptr's text and selection.",
  [("node_ptr", "Node to clear; NULL is ignored.")],
  details="An edit field comes back empty with its caret at position 0; other "
          "kinds only lose their selection.",
  code=PREAMBLE + [
      'IrcitUiNode *field = IrcitUiFind(root, "cmdline");',
      '',
      'IrcitUiLayout(root, 0, 0, COLS, LINES);',
      'IrcitUiRealize(root);',
      'IrcitUiClipClear(field);',
      'IrcitUiDraw(root);',
      'doupdate();',
  ] + TEARDOWN)

d("IrcitUiShowContextMenu",
  "Open a STACKED context menu next to the last mouse event.",
  [("target_ptr", "Node that was right-clicked.")],
  ret="The menu node, or NULL if there is no screen root or no target.",
  details="An already-open `ctx_menu` is found and reused rather than "
          "duplicated.",
  code=PREAMBLE + [
      'IrcitUiNode *target = IrcitUiFind(root, "canvas");',
      'IrcitUiNode *menu;',
      'int key;',
      '',
      'IrcitUiLayout(root, 0, 0, COLS, LINES);',
      'IrcitUiRealize(root);',
      '',
      'key = getch();',
      'if (key != KEY_MOUSE) {',
      '  IrcitUiDeflate(root);',
      '  endwin();',
      '  return;',
      '}',
      '',
      'menu = IrcitUiShowContextMenu(target);',
      'if (!menu) {',
      '  fputs("no context menu: screen root or target missing\\n", stderr);',
      '  IrcitUiDeflate(root);',
      '  endwin();',
      '  return;',
      '}',
      'IrcitUiDraw(root);',
      'doupdate();',
      'IrcitUiPopStacked(menu);',
  ] + TEARDOWN)

d("IrcitUiDemoFill",
  "Fill the `canvas` text_view with many lines.",
  details="A demo helper for the F6 scroll and selection example, not part of "
          "the widget contract. A no-op when the screen root or the `canvas` "
          "node is absent.",
  code=PREAMBLE + [
      'IrcitUiLayout(root, 0, 0, COLS, LINES);',
      'IrcitUiRealize(root);',
      'IrcitUiDemoFill();',
      'IrcitUiDraw(root);',
      'doupdate();',
  ] + TEARDOWN)

d("IrcitUiImageDraw",
  "Paint an `image_view` with half-blocks or Braille.",
  [("node_ptr", "The image_view node; must already be realised.")],
  details="A no-op when the node has no window. The image is loaded on first "
          "use, so the first paint may be the one that reads the file.",
  code=PREAMBLE + [
      'IrcitUiWidgetDef def = {',
      '  .id = "photo", .kind = IRCITUI_IMAGE_VIEW, .text = "assets/logo.png",',
      '  .width_mode = IRCITUI_SIZE_WRAP, .height_mode = IRCITUI_SIZE_WRAP,',
      '  .flags = IRCITUI_F_VISIBLE | IRCITUI_F_ENABLED | IRCITUI_F_BORDER,',
      '  .fg = 7, .bg = 0, .bdr = 2',
      '};',
      'IrcitUiNode *img;',
      '',
      'IrcitUiLayout(root, 0, 0, COLS, LINES);',
      'IrcitUiRealize(root);',
      'img = IrcitUiAddChild(IrcitUiFind(root, "canvas"), &def);',
      'if (!img) {',
      '  fputs("canvas is not a container\\n", stderr);',
      '  IrcitUiDeflate(root);',
      '  endwin();',
      '  return;',
      '}',
      '',
      'IrcitUiImagePrepare(img);   /* size it, then load and mark overflow */',
      'IrcitUiLayout(root, 0, 0, COLS, LINES);',
      'IrcitUiRealize(root);',
      'IrcitUiImageDraw(img);',
      'doupdate();',
  ] + TEARDOWN)

d("IrcitUiImagePrepare",
  "Load the image if needed, size the node, and mark SCROLLABLE on overflow.",
  [("node_ptr", "The image_view node.")],
  details="Call before @ref IrcitUiLayout: it is what writes the wrap_content "
          "width and height the layout pass then reads.",
  code=PREAMBLE + [
      'IrcitUiNode *img = IrcitUiFind(root, "canvas");',
      '',
      'if (!img) {',
      '  fputs("no canvas node\\n", stderr);',
      '  IrcitUiDeflate(root);',
      '  endwin();',
      '  return;',
      '}',
      '',
      'IrcitUiImagePrepare(img);',
      'IrcitUiLayout(root, 0, 0, COLS, LINES);',
      'IrcitUiRealize(root);',
      'IrcitUiImageDraw(img);',
      'doupdate();',
  ] + TEARDOWN)

d("IrcitUiImageWrapW",
  "Content width in cells for `wrap_content`.",
  [("node_ptr", "The image_view node.")],
  ret="Width in cells, or a small default when the image is absent or not yet loaded.",
  code=PREAMBLE + [
      'IrcitUiWidgetDef def = {',
      '  .id = "photo", .kind = IRCITUI_IMAGE_VIEW, .text = "assets/logo.png",',
      '  .width_mode = IRCITUI_SIZE_WRAP, .height_mode = IRCITUI_SIZE_WRAP,',
      '  .flags = IRCITUI_F_VISIBLE | IRCITUI_F_ENABLED,',
      '  .fg = 7, .bg = 0',
      '};',
      'IrcitUiNode *img;',
      'int w, h;',
      '',
      'IrcitUiDataBind();',
      'IrcitUiScreenReset();',
      'img = IrcitUiAddChild(IrcitUiFind(root, "canvas"), &def);',
      'if (!img) {',
      '  fputs("canvas is not a container\\n", stderr);',
      '  return;',
      '}',
      '',
      'w = IrcitUiImageWrapW(img);',
      'h = IrcitUiImageWrapH(img);',
      'printf("image wants %dx%d cells\\n", w, h);',
      '',
      'IrcitUiDeflate(root);',
  ])

d("IrcitUiImageWrapH",
  "Content height in cells for `wrap_content`.",
  [("node_ptr", "The image_view node.")],
  ret="Height in cells, or a small default when the image is absent or not yet loaded.",
  code=PREAMBLE + [
      'IrcitUiNode *img;',
      'int h;',
      '',
      'IrcitUiLayout(root, 0, 0, COLS, LINES);',
      'IrcitUiRealize(root);',
      'img = IrcitUiFind(root, "canvas");',
      'if (!img) {',
      '  fputs("no canvas node\\n", stderr);',
      '  IrcitUiDeflate(root);',
      '  endwin();',
      '  return;',
      '}',
      '',
      'h = IrcitUiImageWrapH(img);',
      'printf("canvas would need %d rows\\n", h);',
  ] + TEARDOWN)

d("IrcitUiRadioItemInput",
  "Exclusive-select this radio inside its group.",
  [("node_ptr", "The radio_button node."),
   ("key", "Ignored; the operation is unconditional.")],
  details="Clears IRCITUI_F_CHECKED on every sibling, so exactly one member of "
          "the parent group stays checked. A node with no parent is ignored.",
  code=PREAMBLE + [
      'IrcitUiWidgetDef group = {',
      '  .id = "mode", .kind = IRCITUI_RADIO_GROUP,',
      '  .width_mode = IRCITUI_SIZE_MATCH, .height_mode = IRCITUI_SIZE_WRAP,',
      '  .flags = IRCITUI_F_VISIBLE | IRCITUI_F_ENABLED,',
      '  .fg = 7, .bg = 0',
      '};',
      'IrcitUiWidgetDef item = {',
      '  .id = "mode_a", .kind = IRCITUI_RADIO_BUTTON, .text = "Mode A",',
      '  .width_mode = IRCITUI_SIZE_MATCH, .height_mode = IRCITUI_SIZE_WRAP,',
      '  .flags = IRCITUI_F_VISIBLE | IRCITUI_F_ENABLED | IRCITUI_F_CLICKABLE,',
      '  .on_input = IrcitUiRadioItemInput,',
      '  .fg = 7, .bg = 0',
      '};',
      'IrcitUiNode *g, *a;',
      '',
      'IrcitUiLayout(root, 0, 0, COLS, LINES);',
      'IrcitUiRealize(root);',
      'g = IrcitUiAddChild(IrcitUiFind(root, "canvas"), &group);',
      'a = g ? IrcitUiAddChild(g, &item) : NULL;',
      'if (!a) {',
      '  fputs("radio group not built\\n", stderr);',
      '  IrcitUiDeflate(root);',
      '  endwin();',
      '  return;',
      '}',
      '',
      'IrcitUiRadioItemInput(a, 0);',
      'printf("selected index %d\\n", IrcitUiGroupSelectedIndex(g));',
  ] + TEARDOWN)

d("IrcitUiCheckItemInput",
  "Toggle this check box.",
  [("node_ptr", "The check_box node; NULL is ignored."),
   ("key", "Key to apply; `KEY_MOUSE` is ignored because dispatch has already "
           "toggled the flag.")],
  code=PREAMBLE + [
      'IrcitUiWidgetDef def = {',
      '  .id = "opt_bold", .kind = IRCITUI_CHECK_BOX, .text = "Bold",',
      '  .width_mode = IRCITUI_SIZE_MATCH, .height_mode = IRCITUI_SIZE_WRAP,',
      '  .flags = IRCITUI_F_VISIBLE | IRCITUI_F_ENABLED | IRCITUI_F_CLICKABLE,',
      '  .on_input = IrcitUiCheckItemInput,',
      '  .fg = 7, .bg = 0',
      '};',
      'IrcitUiNode *box;',
      '',
      'IrcitUiLayout(root, 0, 0, COLS, LINES);',
      'IrcitUiRealize(root);',
      'box = IrcitUiAddChild(IrcitUiFind(root, "canvas"), &def);',
      'if (!box) {',
      '  fputs("canvas is not a container\\n", stderr);',
      '  IrcitUiDeflate(root);',
      '  endwin();',
      '  return;',
      '}',
      '',
      'IrcitUiCheckItemInput(box, \' \');    /* toggles on */',
      'IrcitUiCheckItemInput(box, \' \');    /* and back off */',
  ] + TEARDOWN)

d("IrcitUiGroupSelectedIndex",
  "Which child of @p group_ptr is checked?",
  [("group_ptr", "The group container; NULL yields -1.")],
  ret="Index of the first CHECKED child, or -1 when none is.",
  code=PREAMBLE + [
      'IrcitUiNode *group = IrcitUiFind(root, "canvas");',
      'int idx;',
      '',
      'IrcitUiLayout(root, 0, 0, COLS, LINES);',
      'IrcitUiRealize(root);',
      '',
      'idx = IrcitUiGroupSelectedIndex(group);',
      'if (idx < 0)',
      '  fputs("nothing selected yet\\n", stderr);',
      'else',
      '  printf("selected child %d\\n", idx);',
  ] + TEARDOWN)

d("IrcitUiGroupSelectedValue",
  "Caption of the checked child of @p group_ptr.",
  [("group_ptr", "The group container; NULL yields an empty string.")],
  ret="Text of that child, or `\"\"` when nothing is checked.",
  details="Never NULL, so the result can be passed straight to `printf`.",
  code=PREAMBLE + [
      'IrcitUiNode *group = IrcitUiFind(root, "canvas");',
      'const char *value;',
      '',
      'IrcitUiLayout(root, 0, 0, COLS, LINES);',
      'IrcitUiRealize(root);',
      '',
      'value = IrcitUiGroupSelectedValue(group);',
      'printf("group value: %s\\n", value);   /* never NULL */',
  ] + TEARDOWN)

d("IrcitUiGroupSelectedValues",
  "Collect the captions of every CHECKED child.",
  [("group_ptr", "The group container."),
   ("out_ptr", "Caller-owned array to fill; never written past @p maxn."),
   ("maxn", "Capacity of @p out_ptr; fewer than 1 writes nothing.")],
  ret="Number of captions written, at most @p maxn.",
  details="The pointers stored are the nodes' own text, not copies: they stay "
          "valid only while those nodes live.",
  code=PREAMBLE + [
      'IrcitUiNode *group = IrcitUiFind(root, "canvas");',
      'const char *chosen[8];',
      'int n, i;',
      '',
      'IrcitUiLayout(root, 0, 0, COLS, LINES);',
      'IrcitUiRealize(root);',
      '',
      'n = IrcitUiGroupSelectedValues(group, chosen, 8);',
      'if (n == 0)',
      '  fputs("nothing selected\\n", stderr);',
      'for (i = 0; i < n; i++)',
      '  printf("selected: %s\\n", chosen[i]);',
  ] + TEARDOWN)

d("IrcitUiAddEmojiTable",
  "Append an `emoji_table` child to @p container_ptr.",
  [("container_ptr", "Container to append to."),
   ("emojis_ptr", "UTF-8 sequence of cells; NULL selects a built-in default."),
   ("columns", "Grid width in cells; 0 or less selects the grid default.")],
  ret="The new node, or NULL when the container cannot take children.",
  code=PREAMBLE + [
      'IrcitUiNode *grid;',
      '',
      'IrcitUiLayout(root, 0, 0, COLS, LINES);',
      'IrcitUiRealize(root);',
      'grid = IrcitUiAddEmojiTable(IrcitUiFind(root, "canvas"), "\\xf0\\x9f\\x98\\x80", 8);',
      'if (!grid) {',
      '  fputs("canvas is not a container\\n", stderr);',
      '  IrcitUiDeflate(root);',
      '  endwin();',
      '  return;',
      '}',
      'IrcitUiLayout(root, 0, 0, COLS, LINES);',
      'IrcitUiRealize(root);',
  ] + TEARDOWN)

d("IrcitUiShowEmojiPicker",
  "Toggle the `emoji_picker` template, or build one in code.",
  [("anchor_ptr", "Node the picker is positioned against.")],
  ret="The picker node when one was built, or NULL when an open one was "
      "closed instead.",
  details="The NULL return is therefore not an error: it means the call "
          "closed the picker.",
  code=PREAMBLE + [
      'IrcitUiNode *anchor = IrcitUiFind(root, "cmdline");',
      'IrcitUiNode *picker;',
      '',
      'IrcitUiLayout(root, 0, 0, COLS, LINES);',
      'IrcitUiRealize(root);',
      '',
      'picker = IrcitUiShowEmojiPicker(anchor);',
      'if (!picker)',
      '  puts("picker was open, so this call closed it");',
      'IrcitUiDraw(root);',
      'doupdate();',
  ] + TEARDOWN)

d("IrcitUiClone",
  "Deep-copy @p ref_ptr.",
  [("ref_ptr", "Reference node to copy; NULL yields NULL.")],
  ret="A fresh heap node, or NULL on allocation failure. The caller owns it.",
  details="Windows are cleared, @ref IrcitUiNode::user is unbound, and the "
          "inline edit buffer is copied with the rest and re-aliased by "
          "@ref IrcitUiRealize. The copy is detached: it has no parent.",
  code=PREAMBLE + [
      'IrcitUiNode *copy = IrcitUiClone(IrcitUiRoot.root);',
      '',
      'if (!copy) {',
      '  fputs("clone failed: out of memory\\n", stderr);',
      '  endwin();',
      '  return;',
      '}',
      '',
      'IrcitUiLayout(copy, 0, 0, COLS, LINES);',
      'IrcitUiRealize(copy);',
      'IrcitUiDraw(copy);',
      'doupdate();',
      '',
      'IrcitUiDeflate(copy);   /* windows down... */',
      'IrcitUiDeflate(root);',
      'endwin();',
      '/* ...but IrcitUiClone hands back a detached subtree, so nothing frees it',
      '   here. Screen trees are freed by IrcitUiScreenReset. */',
  ])

d("IrcitUiScreenReset",
  "Replace the screen root with a fresh clone of the reference model.",
  details="The previous screen tree and its popups are destroyed, so any node "
          "pointer held from before this call is dangling. @ref IrcitUiFocus "
          "is not reset for you: follow with @ref IrcitUiFocusTop.",
  code=PREAMBLE + [
      'IrcitUiLayout(root, 0, 0, COLS, LINES);',
      'IrcitUiRealize(root);',
      'IrcitUiInflateFloat(root, "tools_panel");',
      '',
      'IrcitUiScreenReset();          /* popup and all edits are gone */',
      'root = IrcitUiScreen.root;     /* re-read it; the old pointer is dead */',
      'IrcitUiFocusTop();',
      'IrcitUiLayout(root, 0, 0, COLS, LINES);',
      'IrcitUiRealize(root);',
  ] + TEARDOWN)

d("IrcitUiInflate",
  "Clone template @p ref_id_ptr and attach it to @p parent_ptr as STACKED.",
  [("parent_ptr", "Node to attach to."),
   ("ref_id_ptr", "Id of the template root in the reference model.")],
  ret="The new node, or NULL when no template carries that id.",
  details="A template flagged DRAGGABLE is routed to @ref IrcitUiInflateFloat; "
          "everything else to @ref IrcitUiInflateAnchored against @p parent_ptr.",
  code=PREAMBLE + [
      'IrcitUiNode *popup;',
      '',
      'IrcitUiLayout(root, 0, 0, COLS, LINES);',
      'IrcitUiRealize(root);',
      '',
      'popup = IrcitUiInflate(root, "file_menu");',
      'if (!popup) {',
      '  fputs("no template with id file_menu\\n", stderr);',
      '  IrcitUiDeflate(root);',
      '  endwin();',
      '  return;',
      '}',
      'IrcitUiFocusInLayer(popup);',
      'IrcitUiDraw(root);',
      'doupdate();',
      'IrcitUiPopStacked(popup);   /* teardown: detach, deflate, free */',
  ] + TEARDOWN)

d("IrcitUiInflateAnchored",
  "Inflate @p ref_id_ptr next to @p anchor_ptr, flipping on the right and "
  "bottom screen edges.",
  [("parent_ptr", "Node to attach to."),
   ("ref_id_ptr", "Id of the template root."),
   ("anchor_ptr", "Node the popup is positioned against.")],
  ret="The new node, or NULL when no template carries that id.",
  code=PREAMBLE + [
      'IrcitUiNode *anchor = IrcitUiFind(root, "menu_file");',
      'IrcitUiNode *popup;',
      '',
      'IrcitUiLayout(root, 0, 0, COLS, LINES);',
      'IrcitUiRealize(root);',
      '',
      'popup = IrcitUiInflateAnchored(root, "file_menu", anchor);',
      'if (!popup) {',
      '  fputs("no template with id file_menu\\n", stderr);',
      '  IrcitUiDeflate(root);',
      '  endwin();',
      '  return;',
      '}',
      'IrcitUiDraw(root);',
      'doupdate();',
      'IrcitUiPopStacked(popup);',
  ] + TEARDOWN)

d("IrcitUiInflateFloat",
  "Inflate @p ref_id_ptr centred on the screen.",
  [("parent_ptr", "Node to attach to."),
   ("ref_id_ptr", "Id of the template root.")],
  ret="The new node, or NULL when no template carries that id.",
  details="Intended for draggable templates; pair with @ref IrcitUiDragTo.",
  code=PREAMBLE + [
      'IrcitUiNode *popup;',
      '',
      'IrcitUiLayout(root, 0, 0, COLS, LINES);',
      'IrcitUiRealize(root);',
      '',
      'popup = IrcitUiInflateFloat(root, "tools_panel");',
      'if (!popup) {',
      '  fputs("no template with id tools_panel\\n", stderr);',
      '  IrcitUiDeflate(root);',
      '  endwin();',
      '  return;',
      '}',
      'IrcitUiDragTo(popup, 2, 2);',
      'IrcitUiDraw(root);',
      'doupdate();',
      'IrcitUiPopStacked(popup);',
  ] + TEARDOWN)

d("IrcitUiDragTo",
  "Move stacked node @p node_ptr so its origin is (@p x,@p y).",
  [("node_ptr", "Node to move; NULL is ignored."),
   ("x", "New origin column, clamped to the screen."),
   ("y", "New origin row, clamped to the screen.")],
  details="Updates the tree, `mvwin`s the newwin, and re-realises the `derwin` "
          "children. A zero delta returns without doing any of it.",
  code=PREAMBLE + [
      'IrcitUiNode *popup;',
      'int key;',
      '',
      'IrcitUiLayout(root, 0, 0, COLS, LINES);',
      'IrcitUiRealize(root);',
      'popup = IrcitUiInflateFloat(root, "tools_panel");',
      'if (!popup) {',
      '  fputs("no template with id tools_panel\\n", stderr);',
      '  IrcitUiDeflate(root);',
      '  endwin();',
      '  return;',
      '}',
      '',
      'key = getch();',
      'if (key == KEY_RIGHT)',
      '  IrcitUiDragTo(popup, COLS / 4, LINES / 4);',
      'IrcitUiDraw(root);',
      'doupdate();',
      'IrcitUiPopStacked(popup);',
  ] + TEARDOWN)

d("IrcitUiVerifyLayout",
  "Validate the reference model and print ASCII and PPM previews.",
  [("cols", "Preview width in cells."),
   ("rows", "Preview height in cells.")],
  ret="0 if structurally sound, non-zero if any problem was reported.",
  details="Writes no escape sequences, so it runs without a tty: it is the "
          "hook a `--verify-layout` command-line flag uses. Requires "
          "@ref IrcitUiDataBind to have run first, and a UTF-8 locale for "
          "display-width measurement.",
  code=[
      'int cols = 80, rows = 24;   /* from --cols= / --rows= */',
      'int status;',
      '',
      'setlocale(LC_ALL, "");   /* display widths need a UTF-8 locale */',
      'IrcitUiDataBind();',
      'status = IrcitUiVerifyLayout(cols, rows);',
      'if (status != 0) {',
      '  fprintf(stderr, "layout verification reported %d problem(s)\\n", status);',
      '  return;',
      '}',
      'puts("reference model is structurally sound");',
  ])

d("IrcitUiCreateFloatingWindow",
  "Heap a linear popup: STACKED, DRAGGABLE, centred.",
  [("def_ptr", "Description of the window; NULL yields a zeroed default.")],
  ret="The new node, or NULL when there is no screen root or allocation fails.",
  details="The definition's id, text, size modes, colours, and `on_input` are "
          "honoured; STACKED, DRAGGABLE, and visible are forced on.",
  code=PREAMBLE + [
      'IrcitUiWidgetDef win = {',
      '  .id = "code_win", .kind = IRCITUI_LINEAR, .text = "",',
      '  .width_mode = IRCITUI_SIZE_EXACT, .width = 24,',
      '  .height_mode = IRCITUI_SIZE_EXACT, .height = 6,',
      '  .flags = IRCITUI_F_VISIBLE | IRCITUI_F_ENABLED | IRCITUI_F_BORDER | IRCITUI_F_DRAGGABLE,',
      '  .fg = 7, .bg = 0, .bdr = 2',
      '};',
      'IrcitUiNode *win_node;',
      '',
      'IrcitUiLayout(root, 0, 0, COLS, LINES);',
      'IrcitUiRealize(root);',
      '',
      'win_node = IrcitUiCreateFloatingWindow(&win);',
      'if (!win_node) {',
      '  fputs("no screen root, or out of memory\\n", stderr);',
      '  IrcitUiDeflate(root);',
      '  endwin();',
      '  return;',
      '}',
      'IrcitUiDraw(root);',
      'doupdate();',
      'IrcitUiPopStacked(win_node);',
  ] + TEARDOWN)

d("IrcitUiAddChild",
  "Append a child built from @p def_ptr.",
  [("container_ptr", "Container to append to."),
   ("def_ptr", "Description of the child; must not be NULL.")],
  ret="The new node, or NULL when @p container_ptr is not a container, when "
      "@p def_ptr is NULL, or on allocation failure.",
  details="On a failed attach the freshly built child is freed before "
          "returning, so a NULL return leaks nothing.",
  code=PREAMBLE + [
      'IrcitUiWidgetDef def = {',
      '  .id = "label", .kind = IRCITUI_TEXT_VIEW, .text = "from C API",',
      '  .width_mode = IRCITUI_SIZE_MATCH, .height_mode = IRCITUI_SIZE_WRAP,',
      '  .flags = IRCITUI_F_VISIBLE | IRCITUI_F_ENABLED,',
      '  .fg = 2, .bg = 0',
      '};',
      'IrcitUiNode *child;',
      '',
      'IrcitUiLayout(root, 0, 0, COLS, LINES);',
      'IrcitUiRealize(root);',
      '',
      'child = IrcitUiAddChild(IrcitUiFind(root, "canvas"), &def);',
      'if (!child) {',
      '  fputs("canvas is not a container, or out of memory\\n", stderr);',
      '  IrcitUiDeflate(root);',
      '  endwin();',
      '  return;',
      '}',
      'IrcitUiLayout(root, 0, 0, COLS, LINES);',
      'IrcitUiRealize(root);',
  ] + TEARDOWN)

d("IrcitUiAddButton",
  "@ref IrcitUiAddChild, then force BUTTON kind and a clickable default.",
  [("container_ptr", "Container to append to."),
   ("def_ptr", "Description of the caption; may be NULL for a zeroed default.")],
  ret="The new node, or NULL when @p container_ptr is not a container or "
      "allocation fails.",
  details="Sets kind to BUTTON, adds VISIBLE, ENABLED and CLICKABLE, and "
          "binds @ref IrcitUiButtonInput where @p def_ptr named no `on_input`.",
  code=PREAMBLE + [
      'IrcitUiWidgetDef def = {',
      '  .id = "btn_close", .text = "Close",',
      '  .width_mode = IRCITUI_SIZE_MATCH, .height_mode = IRCITUI_SIZE_WRAP',
      '};',
      'IrcitUiNode *btn;',
      '',
      'IrcitUiLayout(root, 0, 0, COLS, LINES);',
      'IrcitUiRealize(root);',
      '',
      'btn = IrcitUiAddButton(IrcitUiFind(root, "canvas"), &def);',
      'if (!btn) {',
      '  fputs("canvas is not a container\\n", stderr);',
      '  IrcitUiDeflate(root);',
      '  endwin();',
      '  return;',
      '}',
      'IrcitUiLayout(root, 0, 0, COLS, LINES);',
      'IrcitUiRealize(root);',
  ] + TEARDOWN)

d("IrcitUiPopStacked",
  "Remove @p node_ptr from its parent and destroy it.",
  [("node_ptr", "The stacked node; NULL is ignored.")],
  details="Fires `on_deflated`, deflates the subtree, detaches it, and frees "
          "it. Any pointer to the node or to anything under it is dangling "
          "afterwards.",
  code=PREAMBLE + [
      'IrcitUiNode *popup;',
      '',
      'IrcitUiLayout(root, 0, 0, COLS, LINES);',
      'IrcitUiRealize(root);',
      'popup = IrcitUiInflateFloat(root, "tools_panel");',
      'if (!popup) {',
      '  fputs("no template with id tools_panel\\n", stderr);',
      '  IrcitUiDeflate(root);',
      '  endwin();',
      '  return;',
      '}',
      '',
      'IrcitUiPopStacked(popup);   /* popup is dangling from here on */',
      'popup = NULL;',
      'IrcitUiFocusTop();',
      'IrcitUiDraw(root);',
      'doupdate();',
  ] + TEARDOWN)

d("IrcitUiMarkOverlap",
  "Fire `on_overlapped` and `on_hidden` for covered siblings.",
  [("node_ptr", "Subtree to walk; NULL is ignored.")],
  details="Reports each pair once, from the child that is behind. Call after "
          "a popup opens, closes, or moves, so the covered nodes can react.",
  code=PREAMBLE + [
      'IrcitUiLayout(root, 0, 0, COLS, LINES);',
      'IrcitUiRealize(root);',
      'IrcitUiInflateFloat(root, "tools_panel");',
      '',
      'IrcitUiMarkOverlap(root);   /* nodes under the popup hear about it */',
      'IrcitUiDraw(root);',
      'doupdate();',
  ] + TEARDOWN)

d("IrcitUiThemeCreate",
  "Allocate an empty theme.",
  ret="A new zeroed theme, or NULL on allocation failure.",
  details="The theme starts with a zeroed base style and no rules, so a node "
          "it is applied to keeps the colours it already had. The caller owns "
          "the result and releases it with @ref IrcitUiThemeDestroy or by "
          "handing it to @ref IrcitUiThemeSetCurrent.",
  code=PREAMBLE + [
      'IrcitUiTheme *theme = IrcitUiThemeCreate();',
      'IrcitUiStyleOverride base = {0};',
      '',
      'if (!theme) {',
      '  fputs("theme allocation failed\\n", stderr);',
      '  IrcitUiDeflate(root);',
      '  endwin();',
      '  return;',
      '}',
      '',
      'base.set_fg = 1; base.fg = 7;',
      'base.set_bg = 1; base.bg = 0;',
      'if (!IrcitUiThemeSetBase(theme, base)) {',
      '  IrcitUiThemeDestroy(theme);',
      '  IrcitUiDeflate(root);',
      '  endwin();',
      '  return;',
      '}',
      '',
      'IrcitUiLayout(root, 0, 0, COLS, LINES);',
      'IrcitUiRealize(root);',
      'if (!IrcitUiThemeApply(theme, root))',
      '  fputs("apply failed\\n", stderr);',
      'IrcitUiDraw(root);',
      'doupdate();',
      '',
      'IrcitUiThemeDestroy(theme);',
      'IrcitUiDeflate(root);',
      'endwin();',
  ])

d("IrcitUiThemeDestroy",
  "Release a theme and its rule array.",
  [("theme_ptr", "Theme to free; NULL is a no-op.")],
  details="Also clears the module's current theme if @p theme_ptr was it, so "
          "@ref IrcitUiThemeCurrent starts returning NULL rather than a "
          "dangling pointer. Never destroy a theme you handed to "
          "@ref IrcitUiThemeSetCurrent: that call took ownership and destroys "
          "it for you.",
  code=PREAMBLE + [
      'IrcitUiTheme *theme = IrcitUiThemeCreate();',
      '',
      'if (!theme) {',
      '  fputs("theme allocation failed\\n", stderr);',
      '  IrcitUiDeflate(root);',
      '  endwin();',
      '  return;',
      '}',
      '',
      'IrcitUiThemeDestroy(theme);   /* rules and struct both released */',
      'theme = NULL;',
      'IrcitUiThemeDestroy(NULL);    /* and NULL is a safe no-op */',
      '',
      'IrcitUiDeflate(root);',
      'endwin();',
  ])

d("IrcitUiThemeSetBase",
  "Set the style every node starts from.",
  [("theme_ptr", "Theme to change."),
   ("style", "Base override, by value.")],
  ret="1 on success, 0 when @p theme_ptr is NULL.",
  details="Only the attributes whose `set_` flags are non-zero take effect, "
          "so a base that names no border colour leaves borders alone. Call "
          "it before @ref IrcitUiThemeApply; changing the base afterwards "
          "needs another apply.",
  code=PREAMBLE + [
      'IrcitUiTheme *theme = IrcitUiThemeCreate();',
      'IrcitUiStyleOverride base = {0};',
      '',
      'if (!theme) {',
      '  fputs("theme allocation failed\\n", stderr);',
      '  IrcitUiDeflate(root);',
      '  endwin();',
      '  return;',
      '}',
      '',
      'base.set_fg = 1; base.fg = 7;    /* black on ... */',
      'base.set_bg = 1; base.bg = 0;    /* ... nothing set for borders */',
      'if (!IrcitUiThemeSetBase(theme, base)) {',
      '  fputs("NULL theme\\n", stderr);',
      '  IrcitUiThemeDestroy(theme);',
      '  IrcitUiDeflate(root);',
      '  endwin();',
      '  return;',
      '}',
      '',
      'IrcitUiLayout(root, 0, 0, COLS, LINES);',
      'IrcitUiRealize(root);',
      'IrcitUiThemeApply(theme, root);',
      'IrcitUiThemeDestroy(theme);',
      'IrcitUiDeflate(root);',
      'endwin();',
  ])

d("IrcitUiThemeAddRule",
  "Append one selector-plus-style rule.",
  [("theme_ptr", "Theme to append to."),
   ("rule", "Rule to copy in, by value.")],
  ret="1 on success, 0 when @p theme_ptr is NULL or the rule array cannot grow.",
  details="Order is significant: later rules of the same specificity win, and "
          "a more specific selector beats a less specific one regardless of "
          "position. The rule is copied, so a local may go out of scope.",
  code=PREAMBLE + [
      'IrcitUiTheme *theme = IrcitUiThemeCreate();',
      'IrcitUiThemeRule by_id = {0};',
      '',
      'if (!theme) {',
      '  fputs("theme allocation failed\\n", stderr);',
      '  IrcitUiDeflate(root);',
      '  endwin();',
      '  return;',
      '}',
      '',
      'by_id.selector = IRCITUI_THEME_ID;',
      'by_id.value    = "cmdline";',
      'by_id.style.set_fg = 1; by_id.style.fg = 3;',
      'if (!IrcitUiThemeAddRule(theme, by_id)) {',
      '  fputs("could not append the rule\\n", stderr);',
      '  IrcitUiThemeDestroy(theme);',
      '  IrcitUiDeflate(root);',
      '  endwin();',
      '  return;',
      '}',
      '',
      'IrcitUiLayout(root, 0, 0, COLS, LINES);',
      'IrcitUiRealize(root);',
      'IrcitUiThemeApply(theme, root);',
      'IrcitUiDraw(root);',
      'doupdate();',
      '',
      'IrcitUiThemeDestroy(theme);',
      'IrcitUiDeflate(root);',
      'endwin();',
  ])

d("IrcitUiThemeApply",
  "Resolve the theme onto @p root_ptr and everything under it.",
  [("theme_ptr", "Theme to resolve."),
   ("root_ptr", "Root of the subtree to recolour.")],
  ret="1 on success, 0 when either argument is NULL.",
  details="Overwrites the nodes' colours in place; it does not repaint, so "
          "follow with @ref IrcitUiDraw. Applying a theme twice is "
          "idempotent, because the resolution starts from the theme's base "
          "each time rather than from the node's current colour.",
  code=PREAMBLE + [
      'IrcitUiTheme *theme = IrcitUiThemeCreate();',
      '',
      'if (!theme) {',
      '  fputs("theme allocation failed\\n", stderr);',
      '  IrcitUiDeflate(root);',
      '  endwin();',
      '  return;',
      '}',
      '',
      'IrcitUiLayout(root, 0, 0, COLS, LINES);',
      'IrcitUiRealize(root);',
      'if (!IrcitUiThemeApply(theme, root)) {',
      '  fputs("NULL theme or NULL root\\n", stderr);',
      '  IrcitUiThemeDestroy(theme);',
      '  IrcitUiDeflate(root);',
      '  endwin();',
      '  return;',
      '}',
      'IrcitUiDraw(root);',
      'doupdate();',
      '',
      'IrcitUiThemeDestroy(theme);',
      'IrcitUiDeflate(root);',
      'endwin();',
  ])

d("IrcitUiThemeSetCurrent",
  "Make @p theme_ptr the module's current theme, taking ownership of it.",
  [("theme_ptr", "Theme to install, or NULL to clear the slot.")],
  details="Ownership passes to the engine: the previously current theme is "
          "destroyed here, and @p theme_ptr will be destroyed by the next "
          "call that replaces it. Do not free it yourself afterwards. A "
          "second call with the same pointer is a no-op.",
  code=PREAMBLE + [
      'IrcitUiTheme *theme = IrcitUiThemeCreate();',
      'IrcitUiStyleOverride base = {0};',
      '',
      'if (!theme) {',
      '  fputs("theme allocation failed\\n", stderr);',
      '  IrcitUiDeflate(root);',
      '  endwin();',
      '  return;',
      '}',
      '',
      'base.set_fg = 1; base.fg = 7;',
      'IrcitUiThemeSetBase(theme, base);',
      '',
      'IrcitUiThemeSetCurrent(theme);   /* ownership moves to the engine */',
      'theme = NULL;                    /* so do not destroy it here */',
      '',
      'IrcitUiLayout(root, 0, 0, COLS, LINES);',
      'IrcitUiRealize(root);',
      'IrcitUiThemeApplyCurrent(root);',
      'IrcitUiDraw(root);',
      'doupdate();',
      '',
      'IrcitUiThemeSetCurrent(NULL);    /* destroys the installed theme */',
      'IrcitUiDeflate(root);',
      'endwin();',
  ])

d("IrcitUiThemeCurrent",
  "The theme installed by @ref IrcitUiThemeSetCurrent.",
  ret="The current theme, or NULL when none is installed.",
  details="Borrowed, not owned: the engine destroys it, so do not free it and "
          "do not keep the pointer past a @ref IrcitUiThemeSetCurrent call.",
  code=PREAMBLE + [
      'IrcitUiTheme *current;',
      '',
      'current = IrcitUiThemeCurrent();',
      'if (!current) {',
      '  fputs("no theme installed; ApplyCurrent would report 0\\n", stderr);',
      '  IrcitUiDeflate(root);',
      '  endwin();',
      '  return;',
      '}',
      '',
      'IrcitUiLayout(root, 0, 0, COLS, LINES);',
      'IrcitUiRealize(root);',
      'IrcitUiThemeApply(current, root);',
      'IrcitUiDraw(root);',
      'doupdate();',
  ] + TEARDOWN)

d("IrcitUiThemeApplyCurrent",
  "Resolve the installed theme onto @p root_ptr.",
  [("root_ptr", "Root of the subtree to recolour.")],
  ret="1 on success, 0 when no theme is installed or @p root_ptr is NULL.",
  details="The convenience form of @ref IrcitUiThemeCurrent followed by "
          "@ref IrcitUiThemeApply, for the redraw path where a theme may or "
          "may not have been installed.",
  code=PREAMBLE + [
      'IrcitUiLayout(root, 0, 0, COLS, LINES);',
      'IrcitUiRealize(root);',
      '',
      'if (!IrcitUiThemeApplyCurrent(root))',
      '  fputs("no theme installed, or NULL root; colours unchanged\\n", stderr);',
      'IrcitUiDraw(root);',
      'doupdate();',
  ] + TEARDOWN)

d("IrcitUiSetThemeClass",
  "Tag @p node_ptr with a class name for @ref IRCITUI_THEME_CLASS rules.",
  [("node_ptr", "Node to tag; NULL is ignored."),
   ("class_name_ptr", "Class name; stored by pointer, not copied.")],
  details="The pointer is stored as given, so the string must outlive the "
          "node: pass a literal or a buffer you own, never a stack array that "
          "is about to go out of scope.",
  code=PREAMBLE + [
      'IrcitUiNode *field = IrcitUiFind(root, "cmdline");',
      '',
      'if (!field) {',
      '  fputs("no cmdline node\\n", stderr);',
      '  IrcitUiDeflate(root);',
      '  endwin();',
      '  return;',
      '}',
      '',
      'IrcitUiSetThemeClass(field, "field");   /* a literal: static storage */',
      'IrcitUiLayout(root, 0, 0, COLS, LINES);',
      'IrcitUiRealize(root);',
      'IrcitUiThemeApplyCurrent(root);',
  ] + TEARDOWN)

d("IrcitUiThemeClass",
  "The class name tagged on @p node_ptr.",
  [("node_ptr", "Node to read; NULL yields NULL.")],
  ret="The stored class name, or NULL when there is none.",
  details="Borrowed, not owned: it is the very pointer that was passed to "
          "@ref IrcitUiSetThemeClass.",
  code=PREAMBLE + [
      'IrcitUiNode *field = IrcitUiFind(root, "cmdline");',
      'const char *cls;',
      '',
      'IrcitUiSetThemeClass(field, "field");',
      'cls = IrcitUiThemeClass(field);',
      'if (!cls)',
      '  fputs("node is untagged\\n", stderr);',
      'else',
      '  printf("class: %s\\n", cls);',
      '',
      'IrcitUiDeflate(root);',
      'endwin();',
  ])


# --------------------------------------------------------------- emitter ---
def sanitize(lines):
    """A `/* ... */` inside a Doxygen block comment would close it early.

    Rewrite every block comment in a snippet as a line comment. Uniform for the
    single-line and multi-line cases, so the text in the header is exactly the
    text that gets compiled by --dump.
    """
    out, in_block = [], False
    for l in lines:
        if in_block:
            body = l.split("*/", 1)[0].rstrip()
            if "*/" in l:
                in_block = False
            out.append(f"//{(' ' + body.strip()) if body.strip() else ''}")
            continue
        if "/*" in l:
            head, _, rest = l.partition("/*")
            if "*/" in rest:
                body, _, tail = rest.partition("*/")
                out.append(f"{head}// {body.strip()}{tail}".rstrip())
            else:
                out.append(f"{head}// {rest.strip()}")
                in_block = True
            continue
        out.append(l)
    return out


LINE = 100


def wrap(prefix, text):
    """Wrap a tagged paragraph, aligning continuations under the tag's text."""
    room = LINE - 3 - len(prefix)
    return textwrap.wrap(" ".join(text.split()), width=room) or [""]


def tagged(prefix, text, out):
    lines = wrap(prefix, text)
    out.append(f" * {prefix}{lines[0]}".rstrip())
    for l in lines[1:]:
        out.append(f" * {' ' * len(prefix)}{l}".rstrip())


def block(entry):
    out = ["/**"]
    tagged("@brief ", entry["brief"], out)
    if entry["details"]:
        out.append(" *")
        tagged("@details ", entry["details"], out)
    for n, desc in entry["params"]:
        out.append(" *")
        tagged(f"@param {n} ", desc, out)
    if entry["ret"]:
        out.append(" *")
        tagged("@return ", entry["ret"], out)
    out.append(" *")
    out.append(" * @code{.c}")
    for l in sanitize(entry["code"]):
        out.append(f" * {l}".rstrip())
    out.append(" * @endcode")
    out.append(" */")
    return "\n".join(out) + "\n"


def main():
    mode = "--emit" if "--emit" in sys.argv else "--dry"
    srcdir = "/tmp/g6_snippets"
    if "--dump" in sys.argv:
        os.makedirs(srcdir, exist_ok=True)

    src = load()
    head, body, tail, items, prev_end = split(src)
    names = []
    for _, decl, _ in items:
        from g6_uih import params_of
        (ret, name), ps = params_of(decl)
        names.append(name)

    missing = [n for n in names if n not in DOC]
    extra = [n for n in DOC if n not in names]
    print(f"declarations={len(names)} doc_entries={len(DOC)}")
    if missing:
        print("MISSING DOC:", missing)
    if extra:
        print("ORPHAN DOC:", extra)

    problems = 0
    for _, decl, _ in items:
        from g6_uih import params_of
        (ret, name), ps = params_of(decl)
        e = DOC.get(name)
        if not e:
            continue
        need = [p for p, _ in ps]
        have = [p for p, _ in e["params"]]
        if need != have:
            print(f"PARAM MISMATCH {name}: decl={need} doc={have}")
            problems += 1
        if ret != "void" and not e["ret"]:
            print(f"NO @return {name} (ret={ret!r})")
            problems += 1
        if ret == "void" and e["ret"]:
            print(f"SPURIOUS @return {name}")
            problems += 1
        # every @p / @ref mention of a param must exist
        for p in need:
            if p not in e["brief"] and p not in str(e["details"]) \
               and not any(p in d for _, d in e["params"]) \
               and f"@p {p}" in e["brief"]:
                pass
        if not e["code"]:
            print(f"NO @code {name}")
            problems += 1
        if "--dump" in sys.argv:
            with open(f"{srcdir}/{name}.c", "w", encoding="utf-8") as fh:
                fh.write("\n".join(sanitize(e["code"])) + "\n")

    print(f"problems={problems}")
    if mode == "--dry":
        return
    assert not missing and not extra and problems == 0, "refusing to emit"

    out = []
    pos = 0
    for doc, decl, gap in items:
        ds = body.index(decl, pos)          # decl start
        # gap runs from `pos` to the start of the doc (or of the decl)
        gstart = pos
        gend = ds - (len(doc) if doc else 0)
        out.append(body[gstart:gend])
        from g6_uih import params_of
        (ret, name), ps = params_of(decl)
        out.append(block(DOC[name]))
        out.append(decl)
        pos = ds + len(decl)
    out.append(body[pos:])
    new = head + "".join(out) + tail
    open("include/ircituilib/ui.h", "w", encoding="utf-8").write(new)
    print("emitted include/ircituilib/ui.h")


main()
