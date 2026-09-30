/**
 * @file ui_theme.c
 * @brief Theme objects: base style, ordered rules, and the resolver that
 *        applies them to a node tree.
 */
#include "ui_priv.h"
#include "ircituilib/config_defs.h"
#include <stdlib.h>
#include <string.h>

typedef struct IrcitUiTheme
{
  IrcitUiStyleOverride base;
  IrcitUiThemeRule *   rules;
  size_t          n, cap;
} IrcitUiTheme;

static IrcitUiTheme *sTheme;

PUBLIC_API IrcitUiTheme *
IrcitUiThemeCreate(void)
{
  return calloc(1, sizeof(IrcitUiTheme));
}

PUBLIC_API void
IrcitUiThemeDestroy(IrcitUiTheme *theme_ptr)
{
  if (!theme_ptr) return;
  free(theme_ptr->rules);
  if (sTheme == theme_ptr) sTheme = NULL;
  free(theme_ptr);
}

PUBLIC_API int
IrcitUiThemeSetBase(IrcitUiTheme *theme_ptr, IrcitUiStyleOverride s)
{
  if (!theme_ptr) return 0;
  theme_ptr->base = s;
  return 1;
}

PUBLIC_API int
IrcitUiThemeAddRule(IrcitUiTheme *theme_ptr, IrcitUiThemeRule r)
{
  IrcitUiThemeRule *p;
  if (!theme_ptr) return 0;
  if (theme_ptr->n == theme_ptr->cap) {
    size_t c = theme_ptr->cap ? theme_ptr->cap * 2 : PRIV_CONFIG_DEFAULT_THEME_RULE_CAP;
    p        = realloc(theme_ptr->rules, c * sizeof(*p));
    if (!p)return 0;
    theme_ptr->rules = p;
    theme_ptr->cap   = c;
  }
  theme_ptr->rules[theme_ptr->n++] = r;
  return 1;
}

static int
sMatch(IrcitUiNode *node_ptr, const IrcitUiThemeRule *rule_ptr)
{
  switch (rule_ptr->selector) {
  case IRCITUI_THEME_ALL: return 1;
  case IRCITUI_THEME_KIND: return node_ptr->common.kind == rule_ptr->kind;
  case IRCITUI_THEME_ID: return node_ptr->common.id && rule_ptr->value && !strcmp(node_ptr->common.id, rule_ptr->value);
  case IRCITUI_THEME_CLASS: return node_ptr->common.theme_class && rule_ptr->value && !strcmp(node_ptr->common.theme_class, rule_ptr->value);
  }
  return 0;
}

static int
sSpec(const IrcitUiThemeRule *rule_ptr)
{
  return rule_ptr->selector == IRCITUI_THEME_ALL ? 0 : rule_ptr->selector == IRCITUI_THEME_KIND ? 1 : rule_ptr->selector == IRCITUI_THEME_CLASS ? 2 : 3;
}

static void
sApplyNode(IrcitUiTheme *theme_ptr, IrcitUiNode *node_ptr)
{
  IrcitUiStyleOverride s    = theme_ptr->base;
  int             best = -1;
  for (size_t pass = 0; pass < theme_ptr->n; pass++) {
    if (sMatch(node_ptr, &theme_ptr->rules[pass]) && sSpec(&theme_ptr->rules[pass]) >= best) {
      IrcitUiStyleOverride x = theme_ptr->rules[pass].style;
      if (x.set_fg)s.fg = x.fg, s.set_fg = 1;
      if (x.set_bg)s.bg = x.bg, s.set_bg = 1;
      if (x.set_bdr)s.bdr = x.bdr, s.set_bdr = 1;
      best = sSpec(&theme_ptr->rules[pass]);
    }
  }
  if (s.set_fg)node_ptr->common.fg = s.fg;
  if (s.set_bg)node_ptr->common.bg = s.bg;
  if (s.set_bdr)node_ptr->common.bdr = s.bdr;
  /* Only when a colour actually moved, so re-applying the same theme costs
     nothing and does not invalidate a tree that is already correct. A node with
     no window is skipped by IrcitUiInvalidate itself, which is what keeps a
     theme applied to the reference model - as the theme tests do - from
     registering static nodes in the frame's change set. */
  if (s.set_fg || s.set_bg || s.set_bdr) IrcitUiInvalidate(node_ptr);
  for (int i = 0; i < node_ptr->container.n_children; i++)sApplyNode(theme_ptr, node_ptr->container.children[i]);
}

PUBLIC_API int
IrcitUiThemeApply(IrcitUiTheme *theme_ptr, IrcitUiNode *root_ptr)
{
  if (!theme_ptr || !root_ptr)return 0;
  sApplyNode(theme_ptr, root_ptr);
  return 1;
}

PUBLIC_API void
IrcitUiThemeSetCurrent(IrcitUiTheme *theme_ptr)
{
  if (sTheme && sTheme != theme_ptr) IrcitUiThemeDestroy(sTheme);
  sTheme = theme_ptr;
}

PUBLIC_API IrcitUiTheme *
IrcitUiThemeCurrent(void)
{
  return sTheme;
}
PUBLIC_API int
IrcitUiThemeApplyCurrent(IrcitUiNode *root_ptr)
{
  return sTheme ? IrcitUiThemeApply(sTheme, root_ptr) : 0;
}
PUBLIC_API void
IrcitUiSetThemeClass(IrcitUiNode *node_ptr, const char *class_name_ptr)
{
  if (node_ptr)node_ptr->common.theme_class = class_name_ptr;
}
PUBLIC_API const char *
IrcitUiThemeClass(const IrcitUiNode *node_ptr)
{
  return node_ptr ? node_ptr->common.theme_class : NULL;
}
