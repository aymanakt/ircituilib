/**
 * @file ircit_ui_event_type_enum.h
 * @brief Event kinds carried by @ref IrcitUiEvent.
 */
#ifndef IRCITUI_IRCIT_UI_EVENT_TYPE_ENUM_H
#define IRCITUI_IRCIT_UI_EVENT_TYPE_ENUM_H

/*!
 * @enum IrcitUiEventType
 * @brief What happened, stored in @ref IrcitUiEvent::type.
 *
 * One listener set serves input, layout and lifecycle, so the tag is what tells
 * a listener which payload fields of @ref IrcitUiEvent are meaningful: the
 * coordinate pair for a click tag, the key code for a key tag, the delta for a
 * scroll tag. A listener handed a tag it does not handle returns 0, the Event
 * Consumption Contract's "not resolved" value, and the framework's default
 * handling proceeds.
 */
typedef enum {
    IRCITUI_EVENT_NONE = 0,          ///< Zero value. Never dispatched.
    IRCITUI_EVENT_INPUT_KEY,         ///< A key or key sequence reached a focused node.
    IRCITUI_EVENT_INPUT_CLICK,       ///< A hit-test resolved a mouse press to a node.
    IRCITUI_EVENT_INPUT_SCROLL,      ///< A wheel motion matched a scrollable node.
    IRCITUI_EVENT_LAYOUT_RESIZE,     ///< A coalesced `KEY_RESIZE` frame.
    IRCITUI_EVENT_LIFECYCLE_INFLATE, ///< A node was instantiated from a blueprint.
    IRCITUI_EVENT_LIFECYCLE_DEFLATE, ///< A node is about to be stripped and freed.
    IRCITUI_EVENT_LIFECYCLE_FOCUS    ///< A node gained or lost input focus.
} IrcitUiEventType;

#endif /* IRCITUI_IRCIT_UI_EVENT_TYPE_ENUM_H */
