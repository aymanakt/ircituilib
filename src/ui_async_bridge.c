/**
 * @file ui_async_bridge.c
 * @brief The bridge from a ufsrvrxlib future's resolution onto Thread 0.
 *
 * Copyright (C) 2015-2026  unfacd works
 *
 * This file is part of ircituilib source code.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * See ui_async_bridge.h for what the bridge is for. This file is the producer
 * half only: it turns a resolved future into an envelope and posts it. The
 * consumer half is the MAIN_MSG_FUTURE_READY case in main_looper.c, which runs
 * on Thread 0 and is the only place the tree is touched.
 */
#include <stdlib.h>
#include <string.h>

/* The project's headers first, and that order is load-bearing: ui_async_bridge.h
   reaches ufsrvrxlib, which brings uflib's PUBLIC_API, and ircituilib_defs.h
   defines the same macro empty - reached the other way round it is a
   -Wmacro-redefined error under -Werror.

   ui_priv.h is here for the consumer half: raising a window needs the node and
   widget vocabulary the producer never touches. */
#include "ui_priv.h"
#include "ui_log.h"
#include "ui_event_dispatcher_priv.h"
#include "ui_async_bridge.h"

/* The string a failed resolution carries. Named rather than inlined so a caller
   that wants to recognise it compares against one spelling. */
static const char *const sAsyncFailureText = "ERROR_FETCH_FAILED";

void
IrcitUiPostFutureContinuation(UfsrvFutureResult *result_ptr, void *context_ptr)
{
  IrcitUiBridgeContext *bridge = context_ptr;
  MainLoopMessage       envelope = {0};

  if (!bridge) return;

  envelope.type        = (bridge->kind == IRCITUI_BRIDGE_IMAGE) ? MAIN_MSG_FUTURE_IMAGE : MAIN_MSG_FUTURE_READY;
  envelope.target_node = bridge->target_node;

  // The value is borrowed, so this reads it and copies. Nothing here frees it, and nothing here writes through it.
  if (result_ptr) envelope.payload.async_work.status_code = result_ptr->error;

  if (result_ptr && result_ptr->error == 0 && result_ptr->value) {
    /* Bounded copy into the envelope's inline buffer: the message crosses a
       thread boundary by value, so the string has to travel inside it rather
       than behind a pointer the receiving thread would have to chase. */
    strncpy(envelope.status_message, (const char *)result_ptr->value, sizeof(envelope.status_message) - 1);
  } else {
    /* A failure is reported by ID, with no node pointer at all.
     *
     * Thread 0 will raise an alert naming what failed, and it must be able to do
     * that without dereferencing the widget: the result can arrive after the
     * widget is gone, and a freed pointer is indistinguishable from a live one
     * on the receiving side. The id was captured while the node was certainly
     * alive, and it outlives the node. Clearing target_node is what makes that
     * guarantee mechanical rather than a convention. */
    envelope.target_node = NULL;
    strncpy(envelope.status_message, bridge->target_id ? bridge->target_id : sAsyncFailureText,
            sizeof(envelope.status_message) - 1);
  }

  /* The post owns the wake-up and the saturation policy. Copying the loop's own
     push and pipe-write here instead would give the pipeline a second producer
     that could drift from the first - and the dispatcher's is the one that drops
     rather than spins when the ring is full. */
  IrcitUiDispatcherPostMessage(bridge->dispatcher, &envelope);

  // A terminal continuation runs exactly once, so this is the single reader.
  free(bridge);
}

void
IrcitUiPostBarProgress(IrcitUiEventDispatcher *disp_ptr, const char *node_id_ptr, int pct)
{
  MainLoopMessage envelope = {0};

  if (!disp_ptr || !node_id_ptr || !node_id_ptr[0]) return;

  /* Clamped HERE as well as in the setter, because this is the last point at which a
     worker's arithmetic can be seen: the envelope's status_code is an int and a caller
     that computed 103 from a rounded fraction means "finished", so clamping here is the
     same decision the setter makes rather than a second policy. */
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;

  envelope.type                     = MAIN_MSG_FUTURE_PROGRESS;
  /* NO NODE POINTER, deliberately -- and this is the one thing about this function that
     is not obvious.
   *
   * ui_async_bridge.h documents that a worker's result can outlive the widget it was
   * fetched for, and that nothing on the receiving side can tell a freed node from a live
   * one. A terminal future only has to survive that hazard once, so the failure path
   * carries the id and the success path carries the pointer, which is safe because the
   * context that holds it is freed by the continuation. A progress report has no such
   * one-shot lifetime: it is posted at line rate while a run is in flight, and the panel
   * it belongs to can be closed at any point in the middle -- so EVERY report carries the
   * id, and Thread 0 resolves it. A report for a node that is gone resolves to NULL and is
   * dropped, which is the correct outcome and needs no cancellation protocol to achieve. */
  envelope.target_node              = NULL;
  envelope.payload.async_work.status_code = pct;
  strncpy(envelope.status_message, node_id_ptr, sizeof(envelope.status_message) - 1);

  IrcitUiDispatcherPostMessage(disp_ptr, &envelope);
}

void
IrcitUiPostBarBuffered(IrcitUiEventDispatcher *disp_ptr, const char *node_id_ptr, int pct)
{
  MainLoopMessage envelope = {0};

  if (!disp_ptr || !node_id_ptr || !node_id_ptr[0]) return;

  // Clamped here as well as in the setter, for the reason the sibling above gives: this is the last point at which a worker's arithmetic can be seen.
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;

  /* ITS OWN DISCRIMINANT rather than reusing MAIN_MSG_FUTURE_PROGRESS with a flag, and the
     reason is the one the tag's own comment in the dispatcher header gives for the family
     being separate tags at all: Thread 0 acts on the two differently. A progress report goes
     to IrcitUiBarSetValue and this one to IrcitUiBarSetBuffered, so a shared tag would mean
     the delivery path had to be told which setter to reach -- and a field added to the
     envelope to tell it would be a field every other producer has to leave alone. */
  envelope.type                     = MAIN_MSG_FUTURE_BUFFERED;
  /* NO NODE POINTER, for the identical reason the sibling documents at length: this is
     posted at line rate while a run is in flight and the panel can be closed in the middle
     of one, so the id is the only durable handle and the lookup on Thread 0 is what makes
     the report safe rather than merely conventional. */
  envelope.target_node              = NULL;
  envelope.payload.async_work.status_code = pct;
  strncpy(envelope.status_message, node_id_ptr, sizeof(envelope.status_message) - 1);

  IrcitUiDispatcherPostMessage(disp_ptr, &envelope);
}

void
IrcitUiBridgeShowImageResult(const char *path_ptr)
{
  IrcitUiWidgetDef win_def, img_def;
  IrcitUiNode *    win_ptr;
  IrcitUiNode *    img_ptr;
  int              w, h, x, y;

  if (!path_ptr || !path_ptr[0]) return;
  if (!IrcitUiScreen.root) return;

  /* The window is a CONTAINER, and the image is its child.
   *
   * IrcitUiCreateFloatingWindow cannot make an image_view: it rewrites any kind
   * that IrcitUiIsContainer rejects into IRCITUI_LINEAR, because it exists to
   * raise popup panels. Handing it a view produced a LINEAR window that drew an
   * empty box - the kind was 10, so IrcitUiImagePrepare returned early and the
   * image was never loaded at all. A container with the view inside it is what
   * the API actually supports. */
  memset(&win_def, 0, sizeof(win_def));
  win_def.id          = "async_download_image";
  win_def.kind        = IRCITUI_LINEAR;
  win_def.text        = "";
  win_def.width_mode  = IRCITUI_SIZE_WRAP;
  win_def.height_mode = IRCITUI_SIZE_WRAP;
  win_def.flags       = IRCITUI_F_VISIBLE | IRCITUI_F_ENABLED | IRCITUI_F_BORDER | IRCITUI_F_DRAGGABLE;
  win_def.fg          = 7;
  win_def.bg          = 0;
  win_def.bdr         = 2;

  memset(&img_def, 0, sizeof(img_def));
  img_def.id          = "async_download_pixels";
  img_def.kind        = IRCITUI_IMAGE_VIEW;
  img_def.text        = path_ptr;
  /* WRAP so the view measures itself from the decoded PNG, which it does in
     IrcitUiImagePrepare, reached from IrcitUiRealize. Naming a size here would
     only be overwritten by the scaling widget's own answer. */
  img_def.width_mode  = IRCITUI_SIZE_WRAP;
  img_def.height_mode = IRCITUI_SIZE_WRAP;
  img_def.flags       = IRCITUI_F_VISIBLE | IRCITUI_F_ENABLED;

  win_ptr = IrcitUiCreateFloatingWindow(&win_def);
  if (!win_ptr) {
    IRCITUI_LOG_WARN("downloaded image could not be shown: no screen root, or out of memory");
    return;
  }

  img_ptr = IrcitUiAddChild(win_ptr, &img_def);

  /* Measure BEFORE realising, and this is the whole trick.
   *
   * IrcitUiRealize reaches IrcitUiImagePrepare for a child only after that child
   * has a window, and a child gets a window only if it fits inside its parent -
   * whose size is not known until the image has been measured. Realising first
   * clipped the view away, so the prepare never ran: the child came back w=0 h=0
   * and nothing was ever decoded. The prepare needs no window, so calling it
   * here breaks the circle: decode, learn the cell size, then size the parent to
   * fit and realise once. The scaling widget caps the result against the
   * terminal, so a 233x242 source arrives as something that fits a 30-row
   * screen. */
  IrcitUiImagePrepare(img_ptr);

  /* Guard on the DECODED IMAGE, not on the size.
   *
   * sImageCellSize falls back to a default cell size when there is no image, so
   * common.w is non-zero even for a file that would not decode - which is how an
   * empty box got raised for a JPEG that this loader cannot read. The decoded
   * image is what interaction_defaults.user holds once sEnsureLoaded has run,
   * and it is NULL exactly when nothing was decoded. */
  if (img_ptr && img_ptr->common.interaction_defaults.user) {
    w = img_ptr->common.w + 2; /* the window's border */
    h = img_ptr->common.h + 2;
  } else {
    // Say so in words rather than raising an empty box.
    IRCITUI_LOG_WARN("downloaded image could not be decoded (PNG only): %s", path_ptr);
    IrcitUiPopStacked(win_ptr);
    return;
  }

  if (w > COLS) w = COLS;
  if (h > LINES) h = LINES;
  x = (COLS - w) / 2;
  y = (LINES - h) / 2;
  if (x < 0) x = 0;
  if (y < 0) y = 0;

  IrcitUiLayout(win_ptr, x, y, w, h);
  IrcitUiRealize(win_ptr);

  IrcitUiSetFocus(win_ptr);
  IrcitUiRequestLayout(win_ptr);

  IRCITUI_LOG_INFO("downloaded image shown: %s (%dx%d cells)", path_ptr, w, h);
}
