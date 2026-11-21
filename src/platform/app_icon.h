/*
 * app_icon.h - the program icon as raw pixels (generated: tools/genicon.py
 * -> src/gen/app_icon.c), set on the window at start-up.  The Windows
 * executable's file icon comes from src/platform/lale.rc (src/gen/lale.ico).
 */
#ifndef LALE_PLATFORM_APP_ICON_H
#define LALE_PLATFORM_APP_ICON_H

extern const int           app_icon_size;     /* width = height, pixels */
extern const unsigned char app_icon_rgba[];   /* size*size RGBA, row-major, top row first */

#endif
