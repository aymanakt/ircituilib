/**
 * @file ircitui_livedata_type.h
 * @brief The opaque LiveData handles and the block signatures that observe them.
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

#ifndef IRCITUILIB_IRCITUI_LIVEDATA_TYPE_H
#define IRCITUILIB_IRCITUI_LIVEDATA_TYPE_H

#include <stdbool.h>

/**
 * @brief Opaque handle to the LiveData container structure.
 * Members are intentionally hidden from the host application to enforce
 * data hiding and allow structural evolution without breaking binary compatibility.
 */
typedef struct IrcitUiLiveData IrcitUiLiveData;

/**
 * @brief Opaque handle to an internal tracking observer handle.
 */
typedef struct IrcitUiObserverToken IrcitUiObserverToken;

/**
 * @brief Reactive observation update signature expressed using Clang blocks.
 * Visible to the Host App consumer so they can dispatch inline definitions.
 */
typedef void (^IrcitUiObserverBlock)(const void *new_value);
typedef void * (^IrcitUiMapBlock)(const void *source_value);

/**
 * @brief Evaluation predicate signature for custom state filtering.
 * @param current_value The last known state payload processed by the observer.
 * @param proposed_value The new state payload arriving from the data stream.
 * @return True if the update should reach the UI thread; False if it should be dropped.
 */
typedef bool (^IrcitUiLiveDataFilter)(const void *current_value, const void *proposed_value);

#endif //IRCITUILIB_IRCITUI_LIVEDATA_TYPE_H
