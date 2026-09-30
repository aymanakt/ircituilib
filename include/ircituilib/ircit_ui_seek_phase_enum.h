/**
 * @file ircit_ui_seek_phase_enum.h
 * @brief Which half of a seek gesture a report belongs to.
 *
 * A seek is two operations wearing one gesture, and they are almost never the same cost.
 * DRAGGING arrives once per column the pointer crosses -- the framework asks the terminal
 * for motion on every move (DECSET 1003), so it can arrive hundreds of times for one
 * gesture -- and what a caller should do with it is show the position moving. RELEASED
 * arrives once, when the button comes up, and that is the moment to do whatever is
 * expensive: re-buffer a stream, find the nearest keyframe, commit to the model, write to
 * a database. A caller that treated every DRAGGING report as a commit would do the
 * expensive thing several hundred times per drag; one that ignored DRAGGING would leave
 * the scrubber sitting still under the user's finger.
 *
 * A phase argument on one callback rather than two callbacks, because the two halves cannot
 * be forgotten separately: a host that binds only the motion handler gets a bar that moves
 * and never commits, and nothing in the type would say so. Here, binding the one callback
 * binds both, and the switch on the phase is exhaustive.
 *
 * **THE TWO NAMES ARE THE MOUSE'S; THE TWO MEANINGS ARE PREVIEW AND COMMIT.** An arrow key
 * on a focused bar is a commit with no preview in front of it, and it reports
 * @ref IRCITUI_PHASE_RELEASED -- not because a button came up, but because "this is the
 * value, act on it" is what that phase means. The alternative, a third enumerator for a
 * stepped commit, would let a host tell a drag-release from a keypress; nothing in this
 * tree needs to yet, and a public enumerator is expensive to withdraw once shipped. Adding
 * it later is a one-line change if a consumer turns up that must distinguish them.
 *
 * **The phase is not derived from the mouse state, deliberately.** A caller could in
 * principle ask ncurses whether a button is down and infer this, and it would be wrong for
 * the same reason `sSelNode`'s own comment gives: ncurses folds a press and a release
 * pending in the same read into a lone BUTTON1_CLICKED, so the button state the terminal
 * reports is not a reliable account of the gesture. The engine knows where in the gesture
 * it is because it is the thing tracking it, so it says.
 */
#ifndef IRCITUI_IRCIT_UI_SEEK_PHASE_ENUM_H
#define IRCITUI_IRCIT_UI_SEEK_PHASE_ENUM_H

/*!
 * @enum IrcitUiSeekPhase
 * @brief Whether a report is a preview or a commit.
 */
typedef enum {
    IRCITUI_PHASE_DRAGGING = 0, ///< Preview: the pointer moved, or a gesture began. Fires many times. Do the cheap thing.
    IRCITUI_PHASE_RELEASED      ///< Commit: the button came up, or a key stepped the bar. Fires once per step. Do the expensive thing.
} IrcitUiSeekPhase;

#endif /* IRCITUI_IRCIT_UI_SEEK_PHASE_ENUM_H */
