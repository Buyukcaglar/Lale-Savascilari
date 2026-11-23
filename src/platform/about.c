/*
 * about.c - texts of the About box (see about.h), shared by every host.
 * Modelled on the Civ64 port's About box (the user's request of 2026-11-21).
 */
#include "platform/about.h"

#define LALE_VERSION "1.0"     /* the port's version, shown in the box's title */

/* how SDL comes with the program on this host */
#if defined(_WIN32)
#define SDL_SHIPPED_AS "shipped unchanged as SDL3.dll"
#elif defined(__APPLE__)
#define SDL_SHIPPED_AS "unchanged, inside the application"
#else
#define SDL_SHIPPED_AS "unchanged"
#endif

const char about_title[] = "About \xC4\xB0stanbul Efsaneleri: Lale Sava\xC5\x9F\xC3\xA7\xC4\xB1lar\xC4\xB1 64bit v" LALE_VERSION;
const char about_line1[] = "64bit version by Onur BUYUKCAGLAR";
const char about_line2[] = "with Anthropic Claude AI assistance";
const char about_url[]   = "https://github.com/Buyukcaglar/Lale-Savascilari";

const char about_licenses[] =
    "MIT License\n\n"
    "Copyright (c) 2026 Onur BUYUKCAGLAR\n\n"
    "Permission is hereby granted, free of charge, to any person obtaining a copy of this "
    "software and associated documentation files (the \"Software\"), to deal in the Software "
    "without restriction, including without limitation the rights to use, copy, modify, merge, "
    "publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons "
    "to whom the Software is furnished to do so, subject to the following conditions:\n\n"
    "The above copyright notice and this permission notice shall be included in all copies or "
    "substantial portions of the Software.\n\n"
    "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, "
    "INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR "
    "PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE "
    "FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR "
    "OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER "
    "DEALINGS IN THE SOFTWARE.\n\n"
    "The licence covers this port's own code. \xC4\xB0stanbul Efsaneleri: Lale Sava\xC5\x9F\xC3\xA7\xC4\xB1lar\xC4\xB1, "
    "its program and its data belong to their owners; they are not part of this software, "
    "which needs a copy of the original game to run.\n\n"
    "Third-party software\n\n"
    "This program uses SDL 3 (Simple DirectMedia Layer, https://libsdl.org), "
    SDL_SHIPPED_AS ", under the zlib license:\n\n"
    "Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>\n\n"
    "This software is provided 'as-is', without any express or implied warranty. In no event "
    "will the authors be held liable for any damages arising from the use of this software.\n\n"
    "Permission is granted to anyone to use this software for any purpose, including "
    "commercial applications, and to alter it and redistribute it freely, subject to the "
    "following restrictions:\n\n"
    "1. The origin of this software must not be misrepresented; you must not claim that you "
    "wrote the original software. If you use this software in a product, an acknowledgment "
    "in the product documentation would be appreciated but is not required.\n"
    "2. Altered source versions must be plainly marked as such, and must not be "
    "misrepresented as being the original software.\n"
    "3. This notice may not be removed or altered from any source distribution.\n";

#if !defined(_WIN32) && !defined(__APPLE__)
/* hosts without an About box (Windows: about_win32.c, macOS: about_macos.m) */
void about_install(SDL_Window *window) { (void)window; }
void about_show(void) {}
#endif
