/**
 * @file ircit_ui_event_type.h
 * @brief The event capsule handed to a listener, opaque to callers.
 */
#ifndef IRCITUI_IRCIT_UI_EVENT_TYPE_H
#define IRCITUI_IRCIT_UI_EVENT_TYPE_H

#include "ircituilib/ircit_ui_event_type_enum.h"

/*!
 * @struct IrcitUiEvent
 * @brief What happened, and to which node.
 *
 * Opaque by design: the members live in the library's private header, so a
 * listener receives a pointer and reads it through the accessors rather than
 * reaching inside. The struct is named, rather than left anonymous, so that a
 * listener can take @c struct @c IrcitUiEvent * without a second typedef in
 * scope.
 */
typedef struct IrcitUiEvent IrcitUiEvent;

#endif /* IRCITUI_IRCIT_UI_EVENT_TYPE_H */
