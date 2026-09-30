/**
 * @file ui_verify.c
 * @brief Offline layout verifier for the reference + template graphs.
 *
 * @details
 * Invoked as `./ui_demo --verify-layout [--cols=N] [--rows=N]`.
 * Does not open a tty. Lays out the main tree and every template
 * at a capped popup size, dumps structure, ASCII preview, PPM.
 */
#include "ui_priv.h"
#include "generated/ui_decl_access.h"   /* the generated accessors: one per declared member */
#include "ircituilib/config_defs.h"
#include "ircituilib/ircitui_context.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/** @brief Human-readable name for @p k; "?" if out of range. */
static const char *
sKindName(IrcitUiKind k)
{
  static const char *n[] = {
    "text_view", "button", "image_view", "edit_text", "check_box", "radio_button", "switch", "progress_bar", "seek_bar",
    "emoji_table", "linear", "frame", "constraint", "radio_group", "check_group"
  };
  if (k < 0 || k > IRCITUI_CHECK_GROUP) return "?";
  return n[k];
}

/** @brief Accumulated error count for this verifier run. */
static int sErrs;

/**
 * @brief Record a structural error.
 * @param id_ptr  node id or "-"
 * @param msg_ptr description
 */
static void
sErr(const char *id_ptr, const char *msg_ptr)
{
  fprintf(stderr, "error: %s: %s\n", id_ptr ? id_ptr : "-", msg_ptr);
  sErrs++;
}

/**
 * @brief Record a non-fatal warning.
 * @param id_ptr  node id or "-"
 * @param msg_ptr description
 */
static void
sWarn(const char *id_ptr, const char *msg_ptr)
{
  fprintf(stderr, "warning: %s: %s\n", id_ptr ? id_ptr : "-", msg_ptr);
}

/**
 * @brief Kind-specific structural rules (groups, images, emoji).
 * @param node_ptr node to inspect
 */
static void
sCheckKind(IrcitUiNode *node_ptr)
{
  int i, checked = 0;

  if (node_ptr->common.kind == IRCITUI_RADIO_GROUP) {
    if (node_ptr->container.n_children < 1) sErr(node_ptr->common.id, "radio_group has no values/children");
    for (i = 0; i < node_ptr->container.n_children; i++) {
      if (node_ptr->container.children[i]->common.kind != IRCITUI_RADIO_BUTTON) sErr(
        node_ptr->common.id, "radio_group child is not radio_button");
      if (node_ptr->container.children[i]->common.flags & IRCITUI_F_CHECKED) checked++;
    }
    if (checked != 1) sWarn(node_ptr->common.id, "radio_group should have exactly one selected child");
  }
  if (node_ptr->common.kind == IRCITUI_CHECK_GROUP) {
    if (node_ptr->container.n_children < 1) sErr(node_ptr->common.id, "check_group has no values/children");
    for (i = 0; i < node_ptr->container.n_children; i++)
      if (node_ptr->container.children[i]->common.kind != IRCITUI_CHECK_BOX)
        sErr(node_ptr->common.id, "check_group child is not check_box");
  }
  if (node_ptr->common.kind == IRCITUI_IMAGE_VIEW) {
    if (!node_ptr->common.text || !node_ptr->common.text[0]) sWarn(node_ptr->common.id, "image_view has empty path");
    if (node_ptr->container.columns != 0 && node_ptr->container.columns != 1) sWarn(node_ptr->common.id,
                                                                                    "image_view columns should be 0=half-block or 1=braille");
  }
  if (node_ptr->common.kind == IRCITUI_EMOJI_TABLE && (!node_ptr->common.text || !node_ptr->common.text[0])) sWarn(
    node_ptr->common.id, "emoji_table has empty glyph string");
}

/**
 * @brief Walk @p node_ptr: unique ids, templates resolve, flags legal.
 * @param node_ptr    subtree root
 * @param seen_ptr id table
 * @param ns_ptr   in/out count
 * @param cap  table capacity
 */
static void
sCheckIds(IrcitUiNode *node_ptr, const char **seen_ptr, int *ns_ptr, int cap)
{
  int i;
  if (!node_ptr->common.id || !node_ptr->common.id[0]) sErr(node_ptr->common.id, "missing id");
  else {
    for (i = 0; i < *ns_ptr; i++)
      if (strcmp(seen_ptr[i], node_ptr->common.id) == 0) sErr(node_ptr->common.id, "duplicate id");
    if (*ns_ptr < cap) seen_ptr[(*ns_ptr)++] = node_ptr->common.id;
  }
  if (IrcitUiDeclLayoutTemplate(node_ptr) && IrcitUiDeclLayoutTemplate(node_ptr)[0]) {
    const IrcitUiDataBindDescriptor *model_ptr = IrcitUiModel();
    int    ok = 0;
    size_t t;
    for (t = 0; t < model_ptr->template_count; t++) {
      const IrcitUiNode *tp = model_ptr->templates[t];
      if (tp && tp->common.id && strcmp(tp->common.id, IrcitUiDeclLayoutTemplate(node_ptr)) == 0) ok = 1;
    }
    if (!ok) sErr(node_ptr->common.id, "layout_template id not in reference templates");
  }
  if ((node_ptr->common.flags & IRCITUI_F_DRAGGABLE) && !IrcitUiIsContainer(node_ptr->common.kind)) sErr(
    node_ptr->common.id, "draggable is only valid on containers");
  if (!IrcitUiIsContainer(node_ptr->common.kind) && node_ptr->container.n_children > 0) sErr(
    node_ptr->common.id, "widget must not have children");
  if (IrcitUiIsContainer(node_ptr->common.kind) && node_ptr->common.layout_w.mode == IRCITUI_SIZE_WRAP && node_ptr->container.n_children == 0) sWarn(
    node_ptr->common.id, "empty wrap_content container");
  sCheckKind(node_ptr);
  for (i = 0; i < node_ptr->container.n_children; i++) sCheckIds(node_ptr->container.children[i], seen_ptr, ns_ptr, cap);
}

/**
 * @brief Stamp @p node_ptr into an ASCII occupancy grid.
 * @param grid_ptr  row-major rows of gw+1 (NUL)
 * @param gw    columns
 * @param gh    rows
 * @param node_ptr     node
 * @param depth 0 = root
 */
static void
sPaint(char *grid_ptr, int gw, int gh, IrcitUiNode *node_ptr, int depth)
{
  int  x, y, i;
  char ch;

  if (!node_ptr || !(node_ptr->common.flags & IRCITUI_F_VISIBLE)) return;
  if (node_ptr->common.kind == IRCITUI_RADIO_GROUP) ch = 'R';
  else if (node_ptr->common.kind == IRCITUI_CHECK_GROUP) ch = 'K';
  else if (node_ptr->common.kind == IRCITUI_IMAGE_VIEW) ch = 'I';
  else if (node_ptr->common.kind == IRCITUI_EMOJI_TABLE) ch = 'E';
  else ch = IrcitUiIsContainer(node_ptr->common.kind) ? (node_ptr->common.flags & IRCITUI_F_DRAGGABLE ? '#' : '+') : '.';
  for (y = node_ptr->common.y; y < node_ptr->common.y + node_ptr->common.h && y < gh; y++) {
    for (x = node_ptr->common.x; x < node_ptr->common.x + node_ptr->common.w && x < gw; x++) {
      int border = (y == node_ptr->common.y || y == node_ptr->common.y + node_ptr->common.h - 1 || x == node_ptr->common.x || x == node_ptr->common.x + node_ptr->
        common.w - 1);
      char *cell = &grid_ptr[y * (gw + 1) + x];
      if (border && (node_ptr->common.flags & IRCITUI_F_BORDER)) *cell = (y == node_ptr->common.y || y == node_ptr->common.y + node_ptr->common.h - 1) ?
                                                               '-' :
                                                               '|';
      else if (!border) *cell = (depth == 0) ? ' ' : ch;
    }
  }
  if (node_ptr->common.id && node_ptr->common.y >= 0 && node_ptr->common.y < gh && node_ptr->common.x + 2 < gw) {
    const char *s = node_ptr->common.id;
    for (i = 0; s[i] && node_ptr->common.x + 2 + i < node_ptr->common.x + node_ptr->common.w - 1 && node_ptr->common.x + 2 + i < gw; i++) grid_ptr[node_ptr->
      common.y * (gw + 1) + node_ptr->common.x + 2 + i] = s[i];
  }
  for (i = 0; i < node_ptr->container.n_children; i++) sPaint(grid_ptr, gw, gh, node_ptr->container.children[i], depth + 1);
}

/**
 * @brief Print one indented tree line plus children.
 * @param node_ptr   node
 * @param ind indent spaces
 */
static void
sDumpTree(IrcitUiNode *node_ptr, int ind)
{
  int i, checked = 0;
  printf("%*s- %s [%s] %dx%d @%d,%d", ind, "", node_ptr->common.id ? node_ptr->common.id : "-", sKindName(node_ptr->common.kind),
         node_ptr->common.w, node_ptr->common.h, node_ptr->common.x, node_ptr->common.y);
  if (node_ptr->common.flags & IRCITUI_F_BORDER) fputs(" border", stdout);
  if (node_ptr->common.flags & IRCITUI_F_DRAGGABLE) fputs(" draggable", stdout);
  if (node_ptr->common.flags & IRCITUI_F_SCROLLABLE) fputs(" scrollable", stdout);
  if (node_ptr->common.flags & IRCITUI_F_CHECKED) fputs(" checked", stdout);
  if (IrcitUiDeclLayoutTemplate(node_ptr) && IrcitUiDeclLayoutTemplate(node_ptr)[0]) fputs(" template", stdout);
  if (node_ptr->common.kind == IRCITUI_IMAGE_VIEW)
    printf(" path=%s mode=%s", node_ptr->common.text ? node_ptr->common.text : "?", node_ptr->container.columns ? "braille" : "half-block");
  if (node_ptr->common.kind == IRCITUI_RADIO_GROUP || node_ptr->common.kind == IRCITUI_CHECK_GROUP) {
    for (i = 0; i < node_ptr->container.n_children; i++)
      if (node_ptr->container.children[i]->common.flags & IRCITUI_F_CHECKED) checked++;
    printf(" selected=%d/%d", checked, node_ptr->container.n_children);
  }
  putchar('\n');
  for (i = 0; i < node_ptr->container.n_children; i++) sDumpTree(node_ptr->container.children[i], ind + 2);
}

/**
 * @brief Write a crude cell-colour PPM of @p grid_ptr.
 * @param path_ptr output path
 * @param grid_ptr ASCII grid
 * @param gw   columns
 * @param gh   rows
 */
static void
sWritePpm(const char *path_ptr, char *grid_ptr, int gw, int gh)
{
  FILE *f        = fopen(path_ptr, "wb");
  int   x, y, cw = PRIV_CONFIG_DEFAULT_VERIFY_CELL_W, ch = PRIV_CONFIG_DEFAULT_VERIFY_CELL_H;
  if (!f) return;
  fprintf(f, "P6\n%d %d\n255\n", gw * cw, gh * ch);
  for (y = 0; y < gh * ch; y++) {
    for (x = 0; x < gw * cw; x++) {
      char          gch = grid_ptr[(y / ch) * (gw + 1) + (x / cw)];
      unsigned char r   = 20, g = 20, b = 24;
      if (gch == '-' || gch == '|' || gch == '+') {
        r = 80;
        g = 200;
        b = 220;
      }
      else if (gch == '#') {
        r = 80;
        g = 220;
        b = 120;
      }
      else if (gch == 'R') {
        r = 220;
        g = 180;
        b = 80;
      }
      else if (gch == 'K') {
        r = 180;
        g = 120;
        b = 220;
      }
      else if (gch == 'I') {
        r = 80;
        g = 140;
        b = 220;
      }
      else if (gch == 'E') {
        r = 220;
        g = 200;
        b = 80;
      }
      else if (gch == '.') {
        r = 40;
        g = 40;
        b = 50;
      }
      else if (isalnum((unsigned char)gch) || gch == '_') {
        r = 230;
        g = 230;
        b = 230;
      }
      fputc(r, f);
      fputc(g, f);
      fputc(b, f);
    }
  }
  fclose(f);
  printf("wrote %s\n", path_ptr);
}

/**
 * @brief Allocate and clear an ASCII grid.
 * @param cols width
 * @param rows height
 * @return malloc'd buffer of rows*(cols+1), or NULL
 */
static char *
sMakeGrid(int cols, int rows)
{
  char *grid = malloc((size_t)(rows * (cols + 1)));
  int   y;
  if (!grid) return NULL;
  for (y = 0; y < rows; y++) {
    memset(grid + y * (cols + 1), ' ', (size_t)cols);
    grid[y * (cols + 1) + cols] = '\0';
  }
  return grid;
}

/**
 * @brief Warn about geometry the layout pass should not have produced.
 * @param node_ptr laid-out node
 *
 * Reports non-positive extents, children outside the parent box, and sibling
 * overlap. Two exemptions keep the signal honest: frame children may overlap
 * by design, and stacked children are popups that overlay the tree.
 */
static void
sCheckGeometry(const IrcitUiNode *node_ptr)
{
  int i, j;
  for (i = 0; i < node_ptr->container.n_children; i++) {
    const IrcitUiNode *c = node_ptr->container.children[i];
    if (!(c->common.flags & IRCITUI_F_VISIBLE)) continue;
    if (c->common.w < 1 || c->common.h < 1) {
      sWarn(c->common.id, "non-positive extent after layout");
      continue;
    }
    if (c->common.x < node_ptr->common.x || c->common.y < node_ptr->common.y || c->common.x + c->common.w > node_ptr->common.x + node_ptr->common.w
      || c->common.y + c->common.h > node_ptr->common.y + node_ptr->common.h) sWarn(c->common.id, "extends outside parent box");
    if (node_ptr->common.kind == IRCITUI_FRAME || (c->common.flags & IRCITUI_F_STACKED)) continue;
    for (j = 0; j < i; j++) {
      const IrcitUiNode *o = node_ptr->container.children[j];
      if (!(o->common.flags & IRCITUI_F_VISIBLE) || (o->common.flags & IRCITUI_F_STACKED)) continue;
      if (c->common.x < o->common.x + o->common.w && c->common.x + c->common.w > o->common.x && c->common.y < o->common.
        y + o->common.h && c->common.y + c->common.h > o->common.y) sWarn(c->common.id, "overlaps sibling");
    }
  }
  for (i = 0; i < node_ptr->container.n_children; i++) sCheckGeometry(node_ptr->container.children[i]);
}

/**
 * @brief Layout @p node_ptr at a fake origin and print dump + preview.
 * @param label_ptr section title
 * @param node_ptr     already-cloned tree
 * @param cols  canvas width
 * @param rows  canvas height
 * @param ox    origin x
 * @param oy    origin y
 * @param ppm_ptr   optional PPM path or NULL
 */
static void
sPreviewTree(const char *label_ptr, IrcitUiNode *node_ptr, int cols, int rows, int ox, int oy, const char *ppm_ptr)
{
  char *grid;
  int   y;

  IrcitUiLayout(node_ptr, ox, oy, node_ptr->common.layout_w.mode == IRCITUI_SIZE_EXACT ? node_ptr->common.layout_w.cells : cols,
                node_ptr->common.layout_h.mode == IRCITUI_SIZE_EXACT ? node_ptr->common.layout_h.cells : rows);
  sCheckGeometry(node_ptr);
  printf("\n== %s ==\n", label_ptr);
  sDumpTree(node_ptr, 0);
  grid = sMakeGrid(cols, rows);
  if (!grid) return;
  sPaint(grid, cols, rows, node_ptr, 0);
  printf("preview:\n");
  for (y = 0; y < rows; y++) printf("%s\n", grid + y * (cols + 1));
  if (ppm_ptr) sWritePpm(ppm_ptr, grid, cols, rows);
  free(grid);
}

/**
 * @brief Validate graphs and render previews.
 * @param cols fake terminal width
 * @param rows fake terminal height
 * @return 0 on success, 1 on structural errors
 */
PUBLIC_API int
IrcitUiVerifyLayout(int cols, int rows)
{
  const char *seen[PRIV_CONFIG_DEFAULT_VERIFY_SEEN_SZ];
  int         ns = 0;
  size_t      i;
  IrcitUiNode *    root;
  /* The model the framework is driving, not the generated tree by name: the
     point of verifying is to check what will actually be put on the glass, and
     an application that supplied its own model is exactly the case where those
     two differ. This is why IrcitUiInitialiseFrameworkContext has to run first -
     the caller feeds the framework, then asks it to check itself. */
  IrcitUiDataBindDescriptor *model_ptr = IrcitUiModel();

  sErrs = 0;
  if (!model_ptr->root) {
    sErr("-", "reference tree empty — IrcitUiDataBind failed");
    return 1;
  }
  sCheckIds(model_ptr->root, seen, &ns, PRIV_CONFIG_DEFAULT_VERIFY_SEEN_SZ);
  for (i = 0; i < model_ptr->template_count; i++)
    if (model_ptr->templates[i]) sCheckIds(model_ptr->templates[i], seen, &ns, PRIV_CONFIG_DEFAULT_VERIFY_SEEN_SZ);

  printf("reference nodes: %d  templates: %d\n", ns, (int)model_ptr->template_count);
  if (sErrs) {
    fprintf(stderr, "%d error(s)\n", sErrs);
    return 1;
  }

  IrcitUiScreenReset();
  root = IrcitUiScreen.root;
  sPreviewTree("main", root, cols, rows, 0, 0, "layout-preview.ppm");

  for (i = 0; i < model_ptr->template_count; i++) {
    IrcitUiNode *t;
    char    label[PRIV_CONFIG_DEFAULT_LABEL_SZ];
    if (!model_ptr->templates[i] || !model_ptr->templates[i]->common.id) continue;
    t = IrcitUiClone(model_ptr->templates[i]);
    if (!t) continue;
    snprintf(label, sizeof label, "template %s", t->common.id);
    sPreviewTree(label, t, cols < PRIV_CONFIG_DEFAULT_VERIFY_PREVIEW_COLS ? cols : PRIV_CONFIG_DEFAULT_VERIFY_PREVIEW_COLS, rows < PRIV_CONFIG_DEFAULT_VERIFY_PREVIEW_ROWS ? rows : PRIV_CONFIG_DEFAULT_VERIFY_PREVIEW_ROWS, 0, 0, NULL);
    IrcitUiDeflate(t);
    free(t);
  }

  printf("OK\n");
  return 0;
}
