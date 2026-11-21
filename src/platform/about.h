/*
 * about.h - the host's About box (the user's request of 2026-11-21: "like
 * the one in the Civ64 project").  Not part of the original game: a port
 * addition of the host, like the window icon.
 *
 * Content (about.c, portable, shared by every host):
 *   title    "About İstanbul Efsaneleri: Lale Savaşçıları 64bit vX.Y"
 *   lines    "64bit version by Onur BUYUKCAGLAR"
 *            "with Anthropic Claude AI assistance"
 *   link     the project's repository (opens in the default browser)
 *   licences the port's MIT licence + the note that the original game is
 *            not part of it, then the third-party notices (SDL3, zlib licence)
 *
 * Windows (about_win32.c): "About…" in the window's system menu (title-bar
 * icon / Alt+Space), just above Close; a native modal dialog that follows the
 * system's light/dark app theme.  The game is paused while it is open (its
 * emulated clock does not run and does not catch up afterwards).
 * macOS: the app menu's "About" item - to be written with the macOS host.
 */
#ifndef LALE_PLATFORM_ABOUT_H
#define LALE_PLATFORM_ABOUT_H

#include <SDL3/SDL.h>

/* UTF-8 texts of the box (about.c) */
extern const char about_title[];      /* window title of the box */
extern const char about_line1[];
extern const char about_line2[];
extern const char about_url[];        /* the repository link */
extern const char about_licenses[];   /* the scrolling licence text, '\n' line ends */

/* Adds the About entry to the window's menu (Windows: system menu).  No-op
   on hosts without an implementation yet. */
void about_install(SDL_Window *window);

/* Shows the box modally (also callable directly, e.g. from a key). */
void about_show(void);

#endif
