/**
 * @file ircitui_observer_token_type_priv.h
 * @brief The concrete observation token binding an observer block to a widget lifecycle.
 *
 * Copyright (C) 2015-2026  unfacd works
 *
 * This file is part of ircituilib source code.
 * Created by ayman on 27/09/2026.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 */

#ifndef IRCITUILIB_IRCITUI_OBSERVER_TOKEN_TYPE_PRIV_H
#define IRCITUILIB_IRCITUI_OBSERVER_TOKEN_TYPE_PRIV_H

#include "ui_priv.h"
#include "ircituilib/livedata/ircitui_livedata_type.h"

/**
 * @brief Concrete structure tracking the observation link and defining an active observer bound to a view lifecycle.
 * It maintains circular references to allow lookups starting from either the token or the parent data layer.
 */
struct IrcitUiObserverToken {
  IrcitUiNode *lifecycle_owner;       /* The UI component context driving this binding */
  IrcitUiObserverBlock block;         /* Heap-copied execution block package */
  IrcitUiLiveDataFilter filter;         /* Optional pre-dispatch evaluation block */
  void *last_delivered_value;          /* Internal tracking cache used for deduplication comparisons */
  IrcitUiLiveData *parent;            /* Back-pointer to the container for manual unobservation */
  IrcitUiObserverToken *next;         /* Next node in the singly-linked observer chain */
};

#endif //IRCITUILIB_IRCITUI_OBSERVER_TOKEN_TYPE_PRIV_H
