//
// Created by ayman on 15/09/2026.
//

/**
 * @file ircitui_context.h
 * @brief ircitui_context
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

#ifndef IRCITUILIB_IRCITUI_CONTEXT_H
#define IRCITUILIB_IRCITUI_CONTEXT_H

#include "ircituilib/ircitui_context_type.h"
#include "ircituilib/ircit_ui_tree_type.h"
#include "ircituilib/ircit_ui_dirty_list_type.h"
#include "ircituilib/ircitui_data_bind_descriptor_type.h"
#include "ircituilib/ircitui_node_tree_binder_type.h"
#include <ufsrvrxlib/ufsrv_scheduler/ufsrv_scheduler_type.h>
/* The window registry, by its PUBLIC header. The signature below names
 * IrcitUiWindowRegistry, which is opaque: the public header is enough to
 * declare a pointer to it, and pulling in the driver's private definition here
 * would both break this header for every consumer -- src/include is not on a
 * consumer's search path, so the include simply fails -- and hand the whole
 * world the struct that src/include/ircitui_window_driver_type_priv.h exists to
 * withhold. */
#include "ircituilib/window_driver/ircitui_window_registry.h"

/* This is a C library, but the header has to be usable from C++ translation
 * units: the test harness compiles its test sources as C++ (LINKER_LANGUAGE
 * CXX), and those tests feed the framework the same way an application does.
 * Without these guards a C++ caller emits a mangled name - for
 * IrcitUiInitialiseFrameworkContext, _Z33IrcitUiInitialiseFrameworkContextPFP29IrcitUiDataBindDescriptorvE -
 * against a library that exports the unmangled one, and the link fails with an
 * undefined reference. ui.h carries the same guards for the same reason. */
#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Returns the framework context, holding what the application supplied.
 *
 * @param node_tree_binder The application's half of the contract. It returns the display model the
 *        framework is to drive, carrying the listeners that bind the application's domain to this
 *        framework's data and interaction requirements. An application that has nothing of its own
 *        to supply passes @ref IrcitUiDataBind, which is the model this library generates. The
 *        binder is invoked once, here, and what it returns is what the context holds from then on.
 * @param window_registry The backend the framework is to draw through, as returned by
 *        @ref IrcitUiWindowRegistryCreate and locked with
 *        @ref IrcitUiWindowRegistryRequestDriver. The context **retains** it and does not
 *        own it: whoever created it destroys it, after the context has stopped being
 *        used. Reached afterwards through @ref IrcitUiContextWindowDriver. May be NULL
 *        for a caller that drives no backend — the layout verifier does — and
 *        @ref IrcitUiContextWindowDriver then returns NULL rather than a dereference.
 * @return The process-wide context. Never NULL.
 */
IrcitUiContext *IrcitUiInitialiseFrameworkContext(IrcitUiNodeTreeBinder node_tree_binder, IrcitUiWindowRegistry *window_registry);

/**
 * @brief The scheduler pool the framework context owns.
 *
 * IrcitUiContext is opaque, so this is how a consumer reaches the pool without
 * the private definition. Valid only after @ref IrcitUiInitialiseFrameworkContext
 * has run.
 *
 * @return The pool, or NULL if the context has not been initialised.
 */
UfsrvSchedulerPool *IrcitUiScheduler(void);

/**
 * @brief The display model the context was initialised with.
 *
 * What the binder returned. This is for a subsystem that did not do the
 * initialising and has no handle of its own — the application that supplied the
 * model already holds it.
 *
 * Valid after @ref IrcitUiInitialiseFrameworkContext. When @ref IrcitUiDataBind
 * was the binder it is the same object that returns; the members of the
 * descriptor are the one place the model is read from.
 *
 * @return The model the binder supplied.
 */
IrcitUiDataBindDescriptor *IrcitUiModel(void);

/**
 * @brief The root node of the display model.
 *
 * A convenience over @ref IrcitUiModel for the common case. NULL only when the
 * model the binder supplied has no root.
 *
 * @return The root node, or NULL.
 */
IrcitUiNode *IrcitUiModelRoot(void);

/**
 * @brief The handle for the on-glass tree.
 *
 * Named "tree" rather than "screen" because what is returned is the handle, not
 * a node: its @c root is NULL until @ref IrcitUiScreenReset has cloned the model
 * onto the glass, and every replacement tree a later reset installs is reached
 * through the same handle.
 *
 * @return The screen handle.
 */
IrcitUiTree *IrcitUiScreenTree(void);

const IrcitUiWindowDriver *IrcitUiContextWindowDriver(void);

/**
 * @brief The current frame's change set.
 *
 * The nodes whose paint cache was invalidated since the last pass, in the order
 * they were registered. It is handed to @ref IrcitUiDraw, which consults it to
 * decide whether a node that is otherwise clean must still be refreshed.
 *
 * The list belongs to the framework, not to the caller. It is returned mutable
 * because the framework's own invalidators write through this accessor, and
 * because the handle is the same shape as @ref IrcitUiScreenTree -- a caller who
 * does not own a thing is trusted not to rewrite it. **An application must treat
 * it as read-only**: adding or removing entries changes which nodes the next
 * pass revisits, and clearing it mid-frame loses a repaint.
 *
 * It is reset at the end of every frame, so a pointer taken now is valid only
 * until the next frame completes.
 *
 * @return The frame's change set. Never NULL. See @ref IrcitUiDirtyList.
 */
IrcitUiDirtyList *IrcitUiFrameDirtyList(void);

bool IsOnMainThreadUi(void);

void AssertIsOnMainThreadUi(void);

#ifdef __cplusplus
}
#endif

#endif //IRCITUILIB_IRCITUI_CONTEXT_H
