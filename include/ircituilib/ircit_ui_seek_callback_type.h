/**
 * @file ircit_ui_seek_callback_type.h
 * @brief The signature a seek gesture is reported through.
 *
 * **IT TAKES AN ID WHERE IT COULD TAKE A NODE, and the difference from
 * IrcitUiClickCallback is deliberate rather than an inconsistency.** That one hands the
 * listener the node because a click handler usually wants to act ON the thing clicked --
 * read its text, toggle its mark, walk to its parent. A seek handler does not: the whole
 * model is that the host owns the value and the widget only reports, so the handler's job
 * is to decide a number and hand it back through IrcitUiBarSetValue, and for that it needs
 * the id it bound with, not a pointer it must not keep.
 *
 * The id is also the safer of the two here. A seek fires many times per gesture, from a
 * tracking register the engine clears on release and on deflate, so a listener holding a
 * node across the gesture would be holding one that a popup being closed has already
 * freed. The id is a borrowed literal from the generated model and outlives every clone.
 *
 * **The fraction is a fraction, not a percentage**, spanning [0.0, 1.0], while
 * IrcitUiBarSetValue takes 0-100. That looks like an inconsistency and is not: the report
 * is a POSITION and the setter is a VALUE, and the conversion is where the host's policy
 * belongs -- snapping to a chapter boundary, quantising to 5% steps, refusing a position
 * the media cannot seek to. A report already rounded to an integer percentage would have
 * thrown away the column the user actually hit before the host ever saw it.
 *
 * There is no return value. The tree's other app-facing callback returns a disposition so
 * a listener can veto, and a seek has nothing to veto: the widget never moves itself, so
 * there is no value to decline. What a host would use a veto for -- snapping -- is
 * expressed by calling IrcitUiBarSetValue with the number it wants instead, which is both
 * simpler and works on the motion as well as on the commit.
 */
#ifndef IRCITUI_IRCIT_UI_SEEK_CALLBACK_TYPE_H
#define IRCITUI_IRCIT_UI_SEEK_CALLBACK_TYPE_H

#include "ircituilib/ircit_ui_seek_phase_enum.h"

/**
 * @brief Called when a user seeks a bar, once per motion and once on release.
 *
 * @param node_id_ptr The id the callback was bound to. Borrowed; it is the layout's own
 *                    string and outlives every clone of the node.
 * @param phase       @ref IRCITUI_PHASE_DRAGGING while the pointer moves,
 *                    @ref IRCITUI_PHASE_RELEASED once when it comes up.
 * @param raw_value   Where along the track the pointer is, in [0.0, 1.0]. Already clamped
 *                    to the track, so a pointer outside the bar reads as its nearest end
 *                    rather than as an out-of-range number the caller has to bound.
 * @param user_data   The pointer passed to @ref IrcitUiBindSeek.
 */
typedef void (*IrcitUiSeekCallback)(const char *node_id_ptr, IrcitUiSeekPhase phase,
                                    float raw_value, void *user_data);

#endif /* IRCITUI_IRCIT_UI_SEEK_CALLBACK_TYPE_H */
