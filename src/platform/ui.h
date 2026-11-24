/*
 * ui.h - the port's own dialogs: a message with buttons, and the progress
 * of the import.  Not part of the original game (port code of the host).
 *
 * They are the system's: its fonts, its light or dark colours, its buttons
 * (the user's request of 2026-11-25: every dialog and window conforms to the
 * OS theme).  All text is Turkish (uitext.h).
 *
 *   Windows  ui_win32.c - windows of the port's own, drawn the way Windows
 *            11 draws its dialogs, light or dark with the system's app mode
 *            and re-themed when it changes.  SDL's message box is not used
 *            there: it stays light in dark mode.  ui_init() also lets the
 *            parts Windows draws itself - the file and folder pickers, the
 *            window's menu - follow the dark mode.
 *   macOS    ui_macos.m - the message is SDL's (an NSAlert, which follows
 *            the system's appearance); the progress is an NSPanel.
 *   others   ui.c - SDL's message box; the progress is drawn into the
 *            game's window with SDL's built-in font.
 */
#ifndef LALE_PLATFORM_UI_H
#define LALE_PLATFORM_UI_H

#include <SDL3/SDL.h>

/* Once, before the first window. */
void ui_init(void);

/* A modal message over `parent` (may be NULL) with 1 to 4 buttons, shown left
   to right.  `def` is the button Return presses, `esc` the one Esc and
   closing the window stand for.  Returns the index of the button chosen. */
int ui_message(SDL_Window *parent, int error, const char *text,
               const char *const *buttons, int nbuttons, int def, int esc);

/* The progress of a long job: `what` (a file name ...), `doing` (what is
   being done to it), `fraction` 0..1, or negative when the length is not
   known.  The first call opens the window.  Returns 1 once the player has
   asked to cancel (the button, Esc, closing the window). */
int ui_progress(SDL_Window *parent, const char *what, const char *doing, double fraction);
void ui_progress_close(void);

#ifdef _WIN32
/* ui_win32.c, for the About box and the game's window: 1 when the system's
   app mode is dark; the test picture of a window (LALE_UI_SHOT). */
int  ui_win32_dark(void);
void ui_win32_shot(void *hwnd);
#endif

#endif
