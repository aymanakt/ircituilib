/**
 * @file ircit_ui_snackbar_action_callback_type.h
 * @brief Application hook for a notification's action, fired by its hotkey.
 */
#ifndef IRCITUI_IRCIT_UI_SNACKBAR_ACTION_CALLBACK_TYPE_H
#define IRCITUI_IRCIT_UI_SNACKBAR_ACTION_CALLBACK_TYPE_H

/**
 * @brief What an application does when the user takes a notification's action.
 *
 * Registered by @ref IrcitUiSnackbarShowAction alongside a hotkey and a label, and
 * called from the key path on Thread 0 when that hotkey arrives while the box is
 * up. The notification is dismissed **before** this runs, so a callback is free to
 * raise one of its own -- which is what the Android idiom this mirrors does, and
 * what an "UNDO" that reports its own success does.
 *
 * **A callback means the application has already acted.** The box is an
 * affordance for undoing something the application has already done; by the time
 * this fires, the deletion, the archive or the send has happened. So the work here
 * is the reversal, and a refusal to raise the affordance at all is a refusal the
 * caller must see -- which is why @ref IrcitUiSnackbarShowAction returns a value
 * where @ref IrcitUiSnackbarShow does not.
 *
 * Runs on the engine's thread, inside key dispatch, and is not a place to block:
 * nothing repaints while it runs. It may raise or dismiss a notification, and it
 * may create or destroy nodes.
 *
 * @param user_ptr  The pointer registered alongside this callback; NULL when the
 *                  registration named none.
 *
 * @see IrcitUiSnackbarShowAction
 */
typedef void (*IrcitUiSnackbarActionCallback)(void *user_ptr);

#endif /* IRCITUI_IRCIT_UI_SNACKBAR_ACTION_CALLBACK_TYPE_H */
