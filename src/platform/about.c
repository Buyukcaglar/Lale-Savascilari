/*
 * about.c - texts of the About box (see about.h), shared by every host.
 * Modelled on the Civ64 port's About box (the user's request of 2026-11-21).
 */
#include "platform/about.h"
#include "platform/uitext.h"

#define LALE_VERSION "1.0"     /* the port's version, shown in the box's title */

/* how SDL comes with the program on this host */
#if defined(_WIN32)
#define SDL_SHIPPED_AS T_SDL_AS_WIN
#elif defined(__APPLE__)
#define SDL_SHIPPED_AS T_SDL_AS_MAC
#else
#define SDL_SHIPPED_AS T_SDL_AS_OTHER
#endif

/* Turkish (uitext.h).  The licences are shown in Turkish too; their binding
   English texts are docs/LICENSES.txt, which goes into the packages. */
const char about_title[] = T_ABOUT_TITLE_FMT LALE_VERSION T_ABOUT_TITLE_END;
const char about_line1[] = T_ABOUT_LINE1;
const char about_line2[] = T_ABOUT_LINE2;
const char about_url[]   = "https://github.com/Buyukcaglar/Lale-Savascilari";
const char about_licenses[] = T_LICENSES_1 SDL_SHIPPED_AS T_LICENSES_2;

#if !defined(_WIN32) && !defined(__APPLE__)
/* hosts without an About box (Windows: about_win32.c, macOS: about_macos.m) */
void about_install(SDL_Window *window) { (void)window; }
void about_show(void) {}
#endif
