/**
 * @file ui_image.c
 * @brief PNG load and half-block / Braille image_view painter.
 */
#include "ui_priv.h"
#include "ircituilib/config_defs.h"
#include <png.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdint.h>

typedef struct
{
  int            pw, ph;
  unsigned char *rgb; /* pw*ph*3 */
} IrcitUiImage;

enum { RENDER_HALF = 0, RENDER_BRAILLE = 1 };

static IrcitUiImage *
sImgFromUser(IrcitUiNode *node_ptr)
{
  return node_ptr && node_ptr->common.interaction_defaults.user ? (IrcitUiImage*)node_ptr->common.interaction_defaults.user : NULL;
}

static int
sRgb256(int r, int g, int b)
{
  if (r < 0) r = 0;
  if (r > 255) r = 255;
  if (g < 0) g = 0;
  if (g > 255) g = 255;
  if (b < 0) b = 0;
  if (b > 255) b = 255;
  return 16 + 36 * (r * 5 / 255) + 6 * (g * 5 / 255) + (b * 5 / 255);
}


static int
sLum(const unsigned char *px_ptr)
{
  return (px_ptr[0] * 30 + px_ptr[1] * 59 + px_ptr[2] * 11) / 100;
}

static const unsigned char *
sPx(const IrcitUiImage *im_ptr, int x, int y)
{
  static const unsigned char z[3];
  if (!im_ptr || x < 0 || y < 0 || x >= im_ptr->pw || y >= im_ptr->ph) return z;
  return im_ptr->rgb + ((y * im_ptr->pw) + x) * 3;
}

static IrcitUiImage *
sImageLoad(const char *path_ptr)
{
  FILE *      fp;
  png_structp png;
  png_infop   info;
  IrcitUiImage *   im;
  int         w, h, y;
  size_t      rowbytes;
  png_bytep * rows;

  char try[PRIV_CONFIG_DEFAULT_PATH_BUF_SZ];
  if (!path_ptr || !path_ptr[0]) return NULL;
  fp = fopen(path_ptr, "rb");
  if (!fp) {
    snprintf(try, sizeof try, "./%s", path_ptr);
    fp = fopen(try, "rb");
  }
  if (!fp) return NULL;
  png  = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  info = png ? png_create_info_struct(png) : NULL;
  if (!png || !info || setjmp(png_jmpbuf(png))) {
    if (png) png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);
    return NULL;
  }
  png_init_io(png, fp);
  png_read_info(png, info);
  w = (int)png_get_image_width(png, info);
  h = (int)png_get_image_height(png, info);
  if (png_get_color_type(png, info) == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
  if (png_get_color_type(png, info) == PNG_COLOR_TYPE_GRAY || png_get_color_type(png, info) ==
    PNG_COLOR_TYPE_GRAY_ALPHA) png_set_gray_to_rgb(png);
  if (png_get_bit_depth(png, info) == 16) png_set_strip_16(png);
  if (png_get_color_type(png, info) & PNG_COLOR_MASK_ALPHA) png_set_strip_alpha(png);
  png_read_update_info(png, info);
  w        = (int)png_get_image_width(png, info);
  h        = (int)png_get_image_height(png, info);
  rowbytes = png_get_rowbytes(png, info);
  if (w <= 0 || h <= 0 || rowbytes < (size_t)w * 3 || (size_t)h > SIZE_MAX / ((size_t)w * 3)) {
    png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);
    return NULL;
  }
  im = calloc(1, sizeof *im);
  if (!im) {
    png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);
    return NULL;
  }
  im->pw  = w;
  im->ph  = h;
  im->rgb = calloc((size_t)w * (size_t)h * 3, 1);
  rows    = malloc(sizeof(*rows) * (size_t)h);
  if (!im->rgb || !rows) {
    free(im->rgb);
    free(im);
    free(rows);
    png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);
    return NULL;
  }
  for (y = 0; y < h; y++) rows[y] = im->rgb + (size_t)y * (size_t)w * 3;
  png_read_image(png, rows);
  png_read_end(png, NULL);
  free(rows);
  png_destroy_read_struct(&png, &info, NULL);
  fclose(fp);
  return im;
}

static void
sImageCellSize(IrcitUiNode *node_ptr, int *cw_ptr, int *ch_ptr)
{
  IrcitUiImage *im   = sImgFromUser(node_ptr);
  int      mode = node_ptr->container.columns;
  int      capw = (COLS > PRIV_CONFIG_DEFAULT_IMG_MIN_CANVAS) ? COLS - PRIV_CONFIG_DEFAULT_IMG_HMARGIN : PRIV_CONFIG_DEFAULT_IMG_FALLBACK_COLS;
  int      caph = (LINES > PRIV_CONFIG_DEFAULT_IMG_MIN_CANVAS) ? LINES - PRIV_CONFIG_DEFAULT_IMG_VMARGIN : PRIV_CONFIG_DEFAULT_IMG_FALLBACK_ROWS;
  int      raww, rawh;
  if (capw > PRIV_CONFIG_DEFAULT_IMG_CAP_COLS) capw = PRIV_CONFIG_DEFAULT_IMG_CAP_COLS;
  if (caph > PRIV_CONFIG_DEFAULT_IMG_CAP_ROWS) caph = PRIV_CONFIG_DEFAULT_IMG_CAP_ROWS;
  if (!im) {
    *cw_ptr = PRIV_CONFIG_DEFAULT_IMG_CELL_W;
    *ch_ptr = PRIV_CONFIG_DEFAULT_IMG_CELL_H;
    return;
  }
  if (mode == RENDER_BRAILLE) {
    raww = (im->pw + 1) / 2;
    rawh = (im->ph + 3) / 4;
  }
  else {
    raww = im->pw;
    rawh = (im->ph + 1) / 2;
  }
  if (raww > capw || rawh > caph) {
    double sx = (double)capw / (double)raww;
    double sy = (double)caph / (double)rawh;
    double s  = sx < sy ? sx : sy;
    raww      = (int)(raww * s);
    if (raww < 1) raww = 1;
    rawh = (int)(rawh * s);
    if (rawh < 1) rawh = 1;
  }
  *cw_ptr = raww;
  *ch_ptr = rawh;
}

static void
sEnsureLoaded(IrcitUiNode *node_ptr)
{
  const char *path;
  if (node_ptr->common.interaction_defaults.user || node_ptr->common.kind != IRCITUI_IMAGE_VIEW) return;
  path = node_ptr->common.text && node_ptr->common.text[0] ? node_ptr->common.text : NULL;
  if (!path) return;
  node_ptr->common.interaction_defaults.user = sImageLoad(path);
}

PUBLIC_API void
IrcitUiImagePrepare(IrcitUiNode *node_ptr)
{
  int cw, ch;
  sEnsureLoaded(node_ptr);
  if (node_ptr->common.kind != IRCITUI_IMAGE_VIEW) return;
  sImageCellSize(node_ptr, &cw, &ch);
  if (node_ptr->common.layout_w.mode == IRCITUI_SIZE_WRAP) node_ptr->common.w = cw + ((node_ptr->common.flags & IRCITUI_F_BORDER) ? 2 : 0);
  if (node_ptr->common.layout_h.mode == IRCITUI_SIZE_WRAP) node_ptr->common.h = ch + ((node_ptr->common.flags & IRCITUI_F_BORDER) ? 2 : 0);
  if (cw > node_ptr->common.w || ch > node_ptr->common.h) node_ptr->common.flags |= IRCITUI_F_SCROLLABLE;
}

static const unsigned char *
sPix(IrcitUiImage *im_ptr, int x, int y)
{
  return sPx(im_ptr, x, y);
}

static const unsigned char *
sSample(IrcitUiImage *im_ptr, int cell_x, int cell_y, int cells_w, int cells_h, int half_row)
{
  int px_, py_;
  if (cells_w < 1) cells_w = 1;
  if (cells_h < 1) cells_h = 1;
  px_ = cell_x * im_ptr->pw / cells_w;
  py_ = (cell_y * 2 + half_row) * im_ptr->ph / (cells_h * 2);
  return sPx(im_ptr, px_, py_);
}

static short
sPairFor(const unsigned char *fg_ptr, const unsigned char *bg_ptr)
{
  return IrcitUiColourPair((short)sRgb256(fg_ptr[0], fg_ptr[1], fg_ptr[2]), (short)sRgb256(bg_ptr[0], bg_ptr[1], bg_ptr[2]));
}

static void
sPutHalfcell(WINDOW *win_ptr, int y, int x, const unsigned char *fg_ptr, const unsigned char *bg_ptr)
{
  cchar_t cc;
  wchar_t wch[2] = {0x2584, 0}; /* ▄ */
  setcchar(&cc, wch, A_NORMAL, sPairFor(fg_ptr, bg_ptr), NULL);
  mvwadd_wch(win_ptr, y, x, &cc);
}

static void
sPutBraille(WINDOW *win_ptr, int y, int x, wchar_t glyph, const unsigned char *fg_ptr)
{
  cchar_t       cc;
  wchar_t       wch[2] = {glyph, 0};
  unsigned char bg[3]  = {0, 0, 0};
  setcchar(&cc, wch, A_NORMAL, sPairFor(fg_ptr, bg), NULL);
  mvwadd_wch(win_ptr, y, x, &cc);
}

PUBLIC_API void
IrcitUiImageDraw(IrcitUiNode *node_ptr)
{
  IrcitUiImage *im;
  int      ox, oy, maxw, maxh, sx, sy, cw, ch, cy, cx;
  int      mode;

  sEnsureLoaded(node_ptr);
  im = sImgFromUser(node_ptr);
  if (!node_ptr->common.win) return;
  ox   = (node_ptr->common.flags & IRCITUI_F_BORDER) ? 1 : 0;
  oy   = ox;
  maxw = node_ptr->common.w - 2 * ox;
  maxh = node_ptr->common.h - 2 * oy;
  if (maxw < 1 || maxh < 1) return;
  if (!im) {
    mvwaddstr(node_ptr->common.win, oy, ox, "[img]");
    return;
  }
  mode = node_ptr->container.columns;
  sImageCellSize(node_ptr, &cw, &ch);
  sx = node_ptr->container.sel0; /* scroll_x */
  sy = node_ptr->container.scroll_y;
  if (sx < 0) sx = 0;
  if (sy < 0) sy = 0;
  if (cw - maxw > 0 && sx > cw - maxw) sx = cw - maxw;
  if (ch - maxh > 0 && sy > ch - maxh) sy = ch - maxh;
  node_ptr->container.sel0     = sx;
  node_ptr->container.scroll_y = sy;
  node_ptr->container.columns  = mode; /* keep mode — don't smash. store overflow in unused? */
  /* overflow for scrollbar painter uses columns — conflict.
     keep mode in gravity? Too late. Scrollbar uses sLineCount for non-container.
     For image, set flags SCROLLABLE and use custom bar here. */

  if (mode == RENDER_BRAILLE) {
    for (cy = 0; cy < maxh; cy++) {
      for (cx = 0; cx < maxw; cx++) {
        int              bit     = 0, k;
        static const int dx[8]   = {0, 0, 0, 1, 1, 1, 0, 1};
        static const int dy[8]   = {0, 1, 2, 0, 1, 2, 3, 3};
        static const int mask[8] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80};
        int              lr      = 0, lg = 0, lb = 0, cnt = 0;
        wchar_t          wc      = 0x2800;
        unsigned char    fg[3];
        for (k = 0; k < 8; k++) {
          int                  px_ = (sx + cx) * im->pw / (cw > 0 ? cw : 1) + dx[k];
          int                  py_ = (sy + cy) * im->ph / (ch > 0 ? ch : 1) + dy[k];
          const unsigned char *p   = sPix(im, px_, py_);
          if (sLum(p) > PRIV_CONFIG_DEFAULT_LUMA_THRESHOLD) {
            bit |= mask[k];
            lr  += p[0];
            lg  += p[1];
            lb  += p[2];
            cnt++;
          }
        }
        wc    = (wchar_t)(0x2800 + bit);
        fg[0] = cnt ? (unsigned char)(lr / cnt) : PRIV_CONFIG_DEFAULT_IMG_FALLBACK_RGB;
        fg[1] = cnt ? (unsigned char)(lg / cnt) : PRIV_CONFIG_DEFAULT_IMG_FALLBACK_RGB;
        fg[2] = cnt ? (unsigned char)(lb / cnt) : PRIV_CONFIG_DEFAULT_IMG_FALLBACK_RGB;
        sPutBraille(node_ptr->common.win, oy + cy, ox + cx, wc, fg);
      }
    }
  }
  else {
    for (cy = 0; cy < maxh; cy++) {
      for (cx = 0; cx < maxw; cx++) {
        const unsigned char *top = sSample(im, sx + cx, sy + cy, cw, ch, 0);
        const unsigned char *bot = sSample(im, sx + cx, sy + cy, cw, ch, 1);
        sPutHalfcell(node_ptr->common.win, oy + cy, ox + cx, bot, top);
      }
    }
  }
  if (cw > maxw || ch > maxh) {
    int track = maxh > 1 ? maxh : 1;
    int maxs  = ch > maxh ? ch - maxh : 1;
    int thumb = track * track / (track + maxs);
    int pos   = oy + (track - (thumb < 1 ? 1 : thumb)) * sy / maxs;
    if (thumb < 1) thumb = 1;
    mvwvline(node_ptr->common.win, oy, node_ptr->common.w - 1, ACS_VLINE, track);
    mvwvline(node_ptr->common.win, pos, node_ptr->common.w - 1, ACS_CKBOARD, thumb);
    if (cw > maxw) {
      int tw  = maxw > 1 ? maxw : 1;
      int mx  = cw - maxw;
      int th  = tw * tw / (tw + mx);
      int pxp = ox + (tw - (th < 1 ? 1 : th)) * sx / mx;
      if (th < 1) th = 1;
      mvwhline(node_ptr->common.win, node_ptr->common.h - 1, ox, ACS_HLINE, tw);
      mvwhline(node_ptr->common.win, node_ptr->common.h - 1, pxp, ACS_CKBOARD, th);
    }
  }
}

/* used by wrap_* in engine via weak-style wrappers */
PUBLIC_API int
IrcitUiImageWrapW(IrcitUiNode *node_ptr)
{
  int cw, ch;
  sEnsureLoaded(node_ptr);
  sImageCellSize(node_ptr, &cw, &ch);
  return cw > 0 ? cw : PRIV_CONFIG_DEFAULT_IMG_CELL_W;
}

PUBLIC_API int
IrcitUiImageWrapH(IrcitUiNode *node_ptr)
{
  int cw, ch;
  sEnsureLoaded(node_ptr);
  sImageCellSize(node_ptr, &cw, &ch);
  return ch > 0 ? ch : PRIV_CONFIG_DEFAULT_IMG_CELL_H;
}
