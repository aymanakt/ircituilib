//
// Created by ayman on 17/09/2026.
//

/**
 * @file ui_engine_priv.h
 * @brief ui_engine_priv
 *
 * Copyright (C) 2015-2026  unfacd works
 *
 * This file is part of ircituilib source code.
 * Created by ayman on 17/09/2026.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 */

#ifndef IRCITUILIB_UI_ENGINE_PRIV_H
#define IRCITUILIB_UI_ENGINE_PRIV_H

#include <stddef.h>
#include "ircituilib/ircit_ui_node_type.h"

// The edit field's fast path: mutate the field's own buffer and invalidate it, so the frame
// pass paints it in this same pass. Called from IrcitUiMainLooper's intercept, which consumes
// the key and skips the ring -- which is the round trip a printable key must not pay.
int IrcitUiExecuteFastPathTyping(IrcitUiNode *node_ptr, int ch);

/* The engine's UTF-8 writer, for the units that paint text the engine does not:
   the conversation window (ui_conversation.c) and the edit field
   (ircitui_edit_text.c). It is defined in ui_engine.c, so it is named for
   external linkage -- the s prefix means file-static in this tree, and a
   declaration in a header is the one place it cannot mean that. */
void IrcitUiAddUtf8(WINDOW *win_ptr, int y, int x, const char *str_ptr, int maxc);

/* The engine's text metrics, exposed to ircitui_edit_text.c.

   These five walked the buffer for the editor, the predictive painter and the
   pointer-to-caret mapping, and that code has moved to its own unit. They stayed
   here rather than moving with it because they are not the field's: sPaintSelText
   measures a TEXT_VIEW with them, the bars measure their own glyphs, and
   IrcitUiWrapW answers about a node with no state at all. A second copy in the
   edit unit would be two implementations of one column arithmetic, which is the
   drift the comments at each site are written against.
    */
size_t IrcitUiUtf8Prev(const char *str_ptr, size_t i);
size_t IrcitUiUtf8Next(const char *str_ptr, size_t i);
void   IrcitUiUtf8Step(const char *str_ptr, size_t p, size_t *k_out_ptr, int *ww_out_ptr);
int    IrcitUiColForIndex(const char *str_ptr, size_t i);
size_t IrcitUiIndexForCol(const char *str_ptr, int col);

#endif //IRCITUILIB_UI_ENGINE_PRIV_H
