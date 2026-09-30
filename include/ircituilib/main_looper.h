/**
 * @file main_looper.h
 * @brief main_looper
 *
 * Copyright (C) 2015-2026  unfacd works
 *
 * This file is part of ircituilib source code.
 * Created by ayman on 15/09/2026.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 */

#ifndef IRCITUILIB_MAIN_LOOPER_H
#define IRCITUILIB_MAIN_LOOPER_H

#include "ircitui_context_type.h"

/* Spelled Looper, not Lopper. The misspelling was in the declaration only and
   the definition followed it; the two are one name again, and it is this one. */
void IrcitUiMainLooper(IrcitUiContext *ircitui_context);

#endif //IRCITUILIB_MAIN_LOOPER_H
