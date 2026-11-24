/*
 * ui.c - the port's dialogs where the system has none of the port's own
 * (see ui.h): everything but Windows (ui_win32.c).  The message is SDL's -
 * on macOS an NSAlert, which follows the system's appearance.  The progress
 * is ui_macos.m on macOS; elsewhere it is drawn here, into the game's
 * window, with SDL's built-in font - which has no ğ, ı, İ, ş, so those four
 * letters lose their marks there and nowhere else.
 */
#ifndef _WIN32

#include <stdio.h>
#include <string.h>

#include "platform/ui.h"
#include "platform/uitext.h"

void ui_init(void) {}

int ui_message(SDL_Window *parent, int error, const char *text, const char *const *buttons, int nbuttons,
               int def, int esc)
{
    static const char *const ok[] = { T_OK };
    SDL_MessageBoxButtonData b[4];
    SDL_MessageBoxData box;
    int hit = esc;
    if (!buttons || nbuttons < 1) {
        buttons = ok;
        nbuttons = 1;
        def = esc = hit = 0;
    }
    if (nbuttons > 4)
        nbuttons = 4;
    for (int i = 0; i < nbuttons; i++) {
        b[i].flags = (i == def ? SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT : 0u)
                   | (i == esc ? SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT : 0u);
        b[i].buttonID = i;
        b[i].text = buttons[i];
    }
    memset(&box, 0, sizeof box);
    box.flags = (error ? SDL_MESSAGEBOX_ERROR : SDL_MESSAGEBOX_INFORMATION) | SDL_MESSAGEBOX_BUTTONS_LEFT_TO_RIGHT;
    box.window = parent;
    box.title = T_TITLE;
    box.message = text;
    box.numbuttons = nbuttons;
    box.buttons = b;
    if (!SDL_ShowMessageBox(&box, &hit) || hit < 0)
        return esc;
    return hit;
}

#ifndef __APPLE__

/* UTF-8 for SDL's built-in font (ASCII and Latin-1): the Turkish letters
   outside Latin-1 without their marks, the ellipsis as three dots. */
static void plain(char *out, size_t cap, const char *s)
{
    size_t n = 0;
    while (*s && n + 4 < cap) {
        const unsigned char a = (unsigned char)s[0], b = (unsigned char)s[1];
        char c = 0;
        if (a == 0xC4 && (b == 0x9E || b == 0x9F)) c = b == 0x9E ? 'G' : 'g';
        else if (a == 0xC4 && (b == 0xB0 || b == 0xB1)) c = b == 0xB0 ? 'I' : 'i';
        else if (a == 0xC5 && (b == 0x9E || b == 0x9F)) c = b == 0x9E ? 'S' : 's';
        if (c) {
            out[n++] = c;
            s += 2;
        } else if (a == 0xE2 && b == 0x80 && (unsigned char)s[2] == 0xA6) {
            memcpy(out + n, "...", 3);
            n += 3;
            s += 3;
        } else
            out[n++] = *s++;
    }
    out[n] = 0;
}

static void text_at(SDL_Renderer *ren, float x, float y, float scale, const char *s)
{
    char line[400];
    plain(line, sizeof line, s);
    SDL_SetRenderScale(ren, scale, scale);
    SDL_RenderDebugText(ren, x / scale, y / scale, line);
    SDL_SetRenderScale(ren, 1.0f, 1.0f);
}

static int progress_quit;

int ui_progress(SDL_Window *parent, const char *what, const char *doing, double fraction)
{
    static Uint64 last;
    SDL_Renderer *ren = parent ? SDL_GetRenderer(parent) : NULL;
    SDL_Event ev;
    Uint64 now = SDL_GetTicks();
    while (SDL_PollEvent(&ev))
        if (ev.type == SDL_EVENT_QUIT || (ev.type == SDL_EVENT_KEY_DOWN && ev.key.scancode == SDL_SCANCODE_ESCAPE))
            progress_quit = 1;
    if (progress_quit)
        return 1;
    if (now - last < 40 || !ren)
        return 0;
    last = now;

    /* the system's light or dark colours */
    int dark = SDL_GetSystemTheme() != SDL_SYSTEM_THEME_LIGHT;
    Uint8 bg = dark ? 32 : 243, fg = dark ? 255 : 0, dim = dark ? 170 : 96;
    int w = 0, h = 0;
    char line[32] = "";
    SDL_GetCurrentRenderOutputSize(ren, &w, &h);
    float s = (float)(w / 420 > 0 ? w / 420 : 1), x = (float)w * 0.1f, bw = (float)w * 0.8f;
    float y = (float)h * 0.4f, bh = 6.0f * s;
    SDL_SetRenderDrawColor(ren, bg, bg, bg, 255);
    SDL_RenderClear(ren);
    SDL_SetRenderDrawColor(ren, fg, fg, fg, 255);
    text_at(ren, x, y - 28.0f * s, s, what);
    SDL_SetRenderDrawColor(ren, dim, dim, dim, 255);
    text_at(ren, x, y - 14.0f * s, s, doing);
    SDL_FRect track = { x, y, bw, bh }, bar = track;
    SDL_SetRenderDrawColor(ren, dark ? 70 : 214, dark ? 70 : 214, dark ? 70 : 214, 255);
    SDL_RenderFillRect(ren, &track);
    if (fraction >= 0.0) {
        bar.w = bw * (float)(fraction > 1.0 ? 1.0 : fraction);
        snprintf(line, sizeof line, "%%%d", (int)(fraction * 100.0));
    } else {                                            /* length unknown: a block going round */
        bar.x = x + bw * 0.85f * ((float)(now % 2000) / 2000.0f);
        bar.w = bw * 0.15f;
    }
    SDL_SetRenderDrawColor(ren, dark ? 96 : 0, dark ? 205 : 103, dark ? 255 : 192, 255);
    SDL_RenderFillRect(ren, &bar);
    SDL_SetRenderDrawColor(ren, fg, fg, fg, 255);
    text_at(ren, x, y + bh + 6.0f * s, s, line);
    SDL_SetRenderDrawColor(ren, dim, dim, dim, 255);
    text_at(ren, x, (float)h - 16.0f * s, s, "Esc: " T_CANCEL);
    SDL_RenderPresent(ren);
    return 0;
}

void ui_progress_close(void) { progress_quit = 0; }

#endif /* !__APPLE__ */
#endif /* !_WIN32 */
