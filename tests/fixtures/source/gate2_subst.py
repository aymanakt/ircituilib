#!/usr/bin/env python3
"""Gate 2: replace bare literals with named constants from config_defs.h.

Every substitution is a whole-token replacement keyed by (file, line). Line
numbers are safe to key on because no substitution adds or removes a line; the
include insertion happens afterwards, as a separate step.

Refuses to run if any anchor does not match exactly once on its line.
"""
import sys
from pathlib import Path

ROOT = Path("/home/devops/repos/ircit/packages/ircituilib")

E = "src/ui_engine.c"
I = "src/ui_image.c"
V = "src/ui_verify.c"
T = "src/ui_theme.c"

UTF8 = "PRIV_CONFIG_DEFAULT_UTF8_MAX_BYTES"
GRID = "PRIV_CONFIG_DEFAULT_GRID_COLS"
WCS = "PRIV_CONFIG_DEFAULT_WCS_BUF_SZ"
PAIR_CACHE = "PRIV_CONFIG_DEFAULT_PAIR_CACHE_SZ"
PAIR_CEIL = "PRIV_CONFIG_DEFAULT_PAIR_CEILING"
STATUS_ECHO = "PRIV_CONFIG_DEFAULT_STATUS_ECHO_SZ"
IMG_CELL_W = "PRIV_CONFIG_DEFAULT_IMG_CELL_W"
IMG_CELL_H = "PRIV_CONFIG_DEFAULT_IMG_CELL_H"
IMG_PAIR_CACHE = "PRIV_CONFIG_DEFAULT_IMG_PAIR_CACHE_SZ"

SITES = {
    E: {
        225:  [("mbrtowc(&wc, s + p, 8, &st)", "mbrtowc(&wc, s + p, %s, &st)" % UTF8)],
        335:  [("wchar_t w[256]", "wchar_t w[%s]" % WCS)],
        339:  [("mbstowcs(w, s, 255)", "mbstowcs(w, s, %s - 1)" % WCS)],
        351:  [("wchar_t w[256]", "wchar_t w[%s]" % WCS)],
        354:  [("mbstowcs(w, s, 255)", "mbstowcs(w, s, %s - 1)" % WCS)],
        411:  [("char tmp[8]", "char tmp[%s]" % UTF8)],
        431:  [("return 5 +", "return PRIV_CONFIG_DEFAULT_GLYPH_W +")],
        434:  [("return 12 + extra;", "return PRIV_CONFIG_DEFAULT_BAR_MIN_W + extra;")],
        436:  [(": 8;", ": %s;" % GRID)],
        440:  [("return t + 4 + extra;", "return t + PRIV_CONFIG_DEFAULT_BUTTON_PAD + extra;")],
        441:  [("(t + 2 < 16 ? 16 : t + 2)",
                "(t + 2 < PRIV_CONFIG_DEFAULT_EDIT_MIN_W ? PRIV_CONFIG_DEFAULT_EDIT_MIN_W : t + 2)")],
        466:  [(": 8;", ": %s;" % GRID)],
        473:  [("mbrtowc(&wc, s, 8, &st)", "mbrtowc(&wc, s, %s, &st)" % UTF8)],
        698:  [("pass < 3", "pass < PRIV_CONFIG_DEFAULT_LAYOUT_PASSES")],
        897:  [("static short cache[64];", "static short cache[%s];" % PAIR_CACHE)],
        902:  [("next >= 63", "next >= %s" % PAIR_CEIL)],
        926:  [("char        tmp[128];", "char        tmp[PRIV_CONFIG_DEFAULT_DRAW_SCRATCH_SZ];")],
        1030: [(": 8;", ": %s;" % GRID)],
        1036: [("char    tmp[8];", "char    tmp[%s];" % UTF8)],
        1037: [("mbrtowc(&wc, p, 8, &st)", "mbrtowc(&wc, p, %s, &st)" % UTF8)],
        1237: [("mbrtowc(&wc, p, 8, &st)", "mbrtowc(&wc, p, %s, &st)" % UTF8)],
        1256: [("mbrtowc(&wc, p, 8, &st)", "mbrtowc(&wc, p, %s, &st)" % UTF8)],
        1429: [("static char echo[64];", "static char echo[%s];" % STATUS_ECHO)],
        1494: [("key == 27", "key == IRCITUI_KEY_ESC")],
        1564: [(": 8;", ": %s;" % GRID)],
        1594: [("char tmp[8];", "char tmp[%s];" % UTF8)],
        1601: [("key == 3 || key == 22 || key == 24",
                "key == IRCITUI_KEY_CTRL_C || key == IRCITUI_KEY_CTRL_V || key == IRCITUI_KEY_CTRL_X")],
        1602: [("key == 3", "key == IRCITUI_KEY_CTRL_C")],
        1603: [("key == 22", "key == IRCITUI_KEY_CTRL_V")],
        1696: [("key == 127 || key == 8", "key == IRCITUI_KEY_DEL || key == IRCITUI_KEY_BS")],
        1708: [("static char echo[280];", "static char echo[PRIV_CONFIG_DEFAULT_EDIT_ECHO_SZ];")],
        1716: [("key >= 32 && key < 127", "key >= IRCITUI_KEY_SPACE && key < IRCITUI_KEY_DEL")],
        1839: [("int capw = 34, caph = 18;",
                "int capw = PRIV_CONFIG_DEFAULT_POPUP_W, caph = PRIV_CONFIG_DEFAULT_POPUP_H;")],
        1844: [("if (*w < 12) *w = 12;",
                "if (*w < PRIV_CONFIG_DEFAULT_POPUP_MIN_W) *w = PRIV_CONFIG_DEFAULT_POPUP_MIN_W;")],
        1845: [("if (*h < 5) *h = 5;",
                "if (*h < PRIV_CONFIG_DEFAULT_POPUP_MIN_H) *h = PRIV_CONFIG_DEFAULT_POPUP_MIN_H;")],
        1846: [("if (COLS > 4 && capw > COLS - 4) capw = COLS - 4;",
                "if (COLS > PRIV_CONFIG_DEFAULT_POPUP_EDGE_MARGIN && capw > COLS - PRIV_CONFIG_DEFAULT_POPUP_EDGE_MARGIN) capw = COLS - PRIV_CONFIG_DEFAULT_POPUP_EDGE_MARGIN;")],
        1847: [("if (LINES > 4 && caph > LINES - 4) caph = LINES - 4;",
                "if (LINES > PRIV_CONFIG_DEFAULT_POPUP_EDGE_MARGIN && caph > LINES - PRIV_CONFIG_DEFAULT_POPUP_EDGE_MARGIN) caph = LINES - PRIV_CONFIG_DEFAULT_POPUP_EDGE_MARGIN;")],
        2059: [("static char echo[64];", "static char echo[%s];" % STATUS_ECHO)],
        2079: [("d->bdr ? d->bdr : 7", "d->bdr ? d->bdr : COLOR_WHITE")],
        2171: [(": 8;", ": %s;" % GRID)],
        2182: [("mbrtowc(&wc, p, 8, &st)", "mbrtowc(&wc, p, %s, &st)" % UTF8)],
        2183: [("char    tmp[8];", "char    tmp[%s];" % UTF8)],
        2207: [("d.fg          = 7;", "d.fg          = COLOR_WHITE;")],
        2210: [("columns > 0 ? columns : 8", "columns > 0 ? columns : %s" % GRID)],
        2231: [(".fg = 7, .bdr = 3", ".fg = COLOR_WHITE, .bdr = COLOR_YELLOW")],
        2235: [(", 8);", ", %s);" % GRID)],
        2240: [("static char clip[512];", "static char clip[PRIV_CONFIG_DEFAULT_CLIPBUF_SZ];")],
        2312: [("ww         = 16;", "ww         = PRIV_CONFIG_DEFAULT_CTXMENU_W;")],
        2324: [("w->common.fg             = 7;", "w->common.fg             = COLOR_WHITE;")],
        2326: [("w->common.bdr            = 7;", "w->common.bdr            = COLOR_WHITE;")],
        2336: [("b.fg          = 7;", "b.fg          = COLOR_WHITE;")],
        2338: [("b.bdr         = 7;", "b.bdr         = COLOR_WHITE;")],
        2381: [("static char blob[4096];", "static char blob[PRIV_CONFIG_DEFAULT_DEMO_BLOB_SZ];")],
        2389: [("i < 60 && n < (int)sizeof blob - 80",
                "i < PRIV_CONFIG_DEFAULT_DEMO_LINES && n < (int)sizeof blob - PRIV_CONFIG_DEFAULT_DEMO_BLOB_RESERVE")],
    },
    I: {
        60:  [("char try[512];", "char try[PRIV_CONFIG_DEFAULT_PATH_BUF_SZ];")],
        124: [("int      capw = (COLS > 8) ? COLS - 6 : 40;",
               "int      capw = (COLS > PRIV_CONFIG_DEFAULT_IMG_MIN_CANVAS) ? COLS - PRIV_CONFIG_DEFAULT_IMG_HMARGIN : PRIV_CONFIG_DEFAULT_IMG_FALLBACK_COLS;")],
        125: [("int      caph = (LINES > 8) ? LINES - 8 : 16;",
               "int      caph = (LINES > PRIV_CONFIG_DEFAULT_IMG_MIN_CANVAS) ? LINES - PRIV_CONFIG_DEFAULT_IMG_VMARGIN : PRIV_CONFIG_DEFAULT_IMG_FALLBACK_ROWS;")],
        127: [("if (capw > 48) capw = 48;",
               "if (capw > PRIV_CONFIG_DEFAULT_IMG_CAP_COLS) capw = PRIV_CONFIG_DEFAULT_IMG_CAP_COLS;")],
        128: [("if (caph > 20) caph = 20;",
               "if (caph > PRIV_CONFIG_DEFAULT_IMG_CAP_ROWS) caph = PRIV_CONFIG_DEFAULT_IMG_CAP_ROWS;")],
        130: [("*cw = 8;", "*cw = %s;" % IMG_CELL_W)],
        131: [("*ch = 4;", "*ch = %s;" % IMG_CELL_H)],
        195: [("static unsigned keys[200];", "static unsigned keys[%s];" % IMG_PAIR_CACHE)],
        196: [("static short    pairs[200];", "static short    pairs[%s];" % IMG_PAIR_CACHE)],
        201: [("if (used >= 200) {", "if (used >= %s) {" % IMG_PAIR_CACHE)],
        206: [("pairs[i] = (short)(32 + i);",
               "pairs[i] = (short)(PRIV_CONFIG_DEFAULT_IMG_PAIR_BASE + i);")],
        275: [("if (lum(p) > 96) {", "if (lum(p) > PRIV_CONFIG_DEFAULT_LUMA_THRESHOLD) {")],
        284: [(": 80;", ": PRIV_CONFIG_DEFAULT_IMG_FALLBACK_RGB;")],
        285: [(": 80;", ": PRIV_CONFIG_DEFAULT_IMG_FALLBACK_RGB;")],
        286: [(": 80;", ": PRIV_CONFIG_DEFAULT_IMG_FALLBACK_RGB;")],
        326: [("return cw > 0 ? cw : 8;", "return cw > 0 ? cw : %s;" % IMG_CELL_W)],
        334: [("return ch > 0 ? ch : 4;", "return ch > 0 ? ch : %s;" % IMG_CELL_H)],
    },
    V: {
        190: [("int   x, y, cw = 8, ch = 14;",
               "int   x, y, cw = PRIV_CONFIG_DEFAULT_VERIFY_CELL_W, ch = PRIV_CONFIG_DEFAULT_VERIFY_CELL_H;")],
        332: [("const char *seen[256];", "const char *seen[PRIV_CONFIG_DEFAULT_VERIFY_SEEN_SZ];")],
        341: [(", 256);", ", PRIV_CONFIG_DEFAULT_VERIFY_SEEN_SZ);")],
        343: [(", 256);", ", PRIV_CONFIG_DEFAULT_VERIFY_SEEN_SZ);")],
        357: [("char    label[64];", "char    label[PRIV_CONFIG_DEFAULT_LABEL_SZ];")],
        362: [("cols < 40 ? cols : 40",
                "cols < PRIV_CONFIG_DEFAULT_VERIFY_PREVIEW_COLS ? cols : PRIV_CONFIG_DEFAULT_VERIFY_PREVIEW_COLS"),
               ("rows < 20 ? rows : 20",
                "rows < PRIV_CONFIG_DEFAULT_VERIFY_PREVIEW_ROWS ? rows : PRIV_CONFIG_DEFAULT_VERIFY_PREVIEW_ROWS")],
    },
    T: {
        36:  [("t->cap * 2 : 8", "t->cap * 2 : PRIV_CONFIG_DEFAULT_THEME_RULE_CAP")],
    },
}

# The include the convention requires in every implementation file that uses a
# config constant ("Implementation files include this header"), inserted after
# the private header. Applied after substitution so line keys stay valid.
INCLUDE_ANCHOR = '#include "ui_internal.h"\n'
INCLUDE_LINE = '#include "ircituilib/config_defs.h"\n'

APPLY = "--apply" in sys.argv


def main():
    failures, applied = [], []
    for rel, sites in SITES.items():
        path = ROOT / rel
        lines = path.read_text(encoding="utf-8").split("\n")
        for ln, repls in sites.items():
            if ln > len(lines):
                failures.append("%s:%d is past end of file" % (rel, ln))
                continue
            orig = lines[ln - 1]
            cur = orig
            for old, new in repls:
                n = cur.count(old)
                if n != 1:
                    failures.append("%s:%d anchor %r occurs %d time(s)" % (rel, ln, old, n))
                    break
                cur = cur.replace(old, new)
            else:
                lines[ln - 1] = cur
                applied.append((rel, ln, orig, cur))
        if failures:
            continue
        if APPLY:
            text = "\n".join(lines)
            if INCLUDE_LINE.strip() not in text:
                if text.count(INCLUDE_ANCHOR) != 1:
                    failures.append("%s: include anchor occurs %d time(s)"
                                    % (rel, text.count(INCLUDE_ANCHOR)))
                else:
                    text = text.replace(INCLUDE_ANCHOR, INCLUDE_ANCHOR + INCLUDE_LINE, 1)
            path.write_text(text, encoding="utf-8")

    print("substitutions: %d" % len(applied))
    for rel, ln, o, n in applied:
        print("\n%s:%d\n  - %s\n  + %s" % (rel, ln, o.strip(), n.strip()))

    if failures:
        print("\nFAILURES:")
        for f in failures:
            print("  " + f)
        return 1
    print("\nall %d anchors matched exactly once" % len(applied))
    print("mode: %s" % ("APPLIED" if APPLY else "dry run"))
    return 0


sys.exit(main())
