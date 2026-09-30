/**
 * @file ircit_ui_cluster_position_enum.h
 * @brief Where a message sits in its run of consecutive same-sender messages.
 */
#ifndef IRCITUI_IRCIT_UI_CLUSTER_POSITION_ENUM_H
#define IRCITUI_IRCIT_UI_CLUSTER_POSITION_ENUM_H

/*!
 * @enum IrcitUiClusterPosition
 * @brief Which corners a bubble rounds and whether it carries a tail.
 *
 * COMPUTED, NOT DECLARED. A layout cannot hold this -- it is a property of the
 * messages either side of this one -- so the widget derives it as messages are
 * appended. IrcitUiConversationMessage carries a field of this type, and it is
 * ADVISORY: an application may set it and the widget will overwrite it, because
 * a caller that gets it wrong (a `lone` followed by a `top` from the same
 * sender, which is a shape the algorithm never emits) would otherwise be
 * honoured into a cluster that never closes.
 */
typedef enum {
    IRCITUI_CLUSTER_LONE = 0, ///< Alone: every corner rounded, one tail.
    IRCITUI_CLUSTER_TOP,      ///< First of a run: rounded above, flat below.
    IRCITUI_CLUSTER_MIDDLE,   ///< Interior: flat above and below.
    IRCITUI_CLUSTER_BOTTOM    ///< Last of a run: flat above, rounded and tailed below.
} IrcitUiClusterPosition;

#endif /* IRCITUI_IRCIT_UI_CLUSTER_POSITION_ENUM_H */
