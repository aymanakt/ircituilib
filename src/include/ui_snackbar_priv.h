/**
 * @file ui_snackbar_priv.h
 * @brief The snackbar's framework-side teardown, which no application calls.
 *
 * Not installed and never included from outside the library. The show, dismiss,
 * advance and render calls are public and live in @c ircituilib/ui.h; what is
 * private is the destruction of the one window the overlay owns, because the
 * window's lifetime is the framework context's and not the application's.
 */
#ifndef IRCITUI_UI_SNACKBAR_PRIV_H
#define IRCITUI_UI_SNACKBAR_PRIV_H

/**
 * @brief Release the overlay's window and forget its state.
 *
 * @details Called by @ref IrcitUiDestroyFrameworkContext, and by nothing else.
 *          The application's half is @ref IrcitUiSnackbarDismiss, which slides the
 *          box away without touching the allocation -- so an application that
 *          dismisses a notification and then tears the context down still goes
 *          through here, and a notification that is up when the context dies is
 *          simply released.
 *
 *          Idempotent, and safe on an overlay that was never shown. After it
 *          returns the overlay is hidden and a later @ref IrcitUiSnackbarShow
 *          builds a fresh window.
 */
void IrcitUiSnackbarDestroy(void);

#endif /* IRCITUI_UI_SNACKBAR_PRIV_H */
