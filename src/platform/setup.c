/*
 * setup.c - see setup.h.
 */
#include "setup.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/edition.h"
#include "platform/cdrom.h"
#include "platform/chd.h"
#include "platform/drive.h"
#include "platform/install.h"

/* "İstanbul Efsaneleri - Lale Savaşçıları" */
static const char title[] = "\xC4\xB0stanbul Efsaneleri - Lale Sava\xC5\x9F\xC3\xA7\xC4\xB1lar\xC4\xB1";

static SDL_Window   *win;
static SDL_Renderer *ren;
static int           dialogs;
static const char   *progress_source = "";

static void show_error(const char *msg)
{
    SDL_Log("%s", msg);
    if (dialogs)
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, msg, win);
}

/* ---- folders ---------------------------------------------------------------------- */

static void strip_slash(char *p)
{
    size_t n = strlen(p);
    while (n > 1 && (p[n - 1] == '/' || p[n - 1] == '\\') && p[n - 2] != ':')
        p[--n] = 0;
}

/* The folder of the executable; macOS: the folder that holds the .app. */
static void exe_dir(char *out, size_t size)
{
    const char *base = SDL_GetBasePath();
    snprintf(out, size, "%s", base ? base : ".");
    char *app = strstr(out, ".app/");
    if (app) {
        *app = 0;
        char *slash = strrchr(out, '/');
        if (slash)
            *slash = 0;
    }
    strip_slash(out);
}

static int writable(const char *dir)
{
    char path[1200];
    snprintf(path, sizeof path, "%s/SAVE", dir);
    if (install_mkdir(dir) != 0 || install_mkdir(path) != 0)
        return 0;
    snprintf(path, sizeof path, "%s/SAVE/_write.tmp", dir);
    FILE *f = fopen(path, "wb");
    if (!f)
        return 0;
    fclose(f);
    remove(path);
    return 1;
}

static int is_dir(const char *path)
{
    SDL_PathInfo info;
    return SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_DIRECTORY;
}

/* ---- progress ----------------------------------------------------------------------- */

static void text_at(float x, float y, float scale, const char *s)
{
    SDL_SetRenderScale(ren, scale, scale);
    SDL_RenderDebugText(ren, x / scale, y / scale, s);
    SDL_SetRenderScale(ren, 1.0f, 1.0f);
}

static int progress(void *ctx, const char *text, uint64_t done, uint64_t total)
{
    static Uint64 last;
    static const char *last_text;
    static int quit;
    SDL_Event ev;
    Uint64 now = SDL_GetTicks();
    (void)ctx;
    while (SDL_PollEvent(&ev))
        if (ev.type == SDL_EVENT_QUIT || (ev.type == SDL_EVENT_KEY_DOWN && ev.key.scancode == SDL_SCANCODE_ESCAPE))
            quit = 1;
    if (quit)
        return 1;
    if (text != last_text) {
        SDL_Log("%s", text);
        last_text = text;
        last = 0;
    }
    if (now - last < 40 || !ren)
        return 0;
    last = now;

    int w = 0, h = 0;
    char line[160];
    SDL_GetCurrentRenderOutputSize(ren, &w, &h);
    float s = (float)(w / 420 > 0 ? w / 420 : 1), x = (float)w * 0.1f, bw = (float)w * 0.8f;
    float y = (float)h * 0.36f, bh = 14.0f * s;
    SDL_SetRenderDrawColor(ren, 12, 12, 20, 255);
    SDL_RenderClear(ren);
    SDL_SetRenderDrawColor(ren, 150, 150, 160, 255);
    text_at(x, y - 26.0f * s, s, progress_source);
    SDL_SetRenderDrawColor(ren, 235, 235, 235, 255);
    text_at(x, y - 13.0f * s, s, text);
    SDL_FRect frame = { x, y, bw, bh };
    SDL_RenderRect(ren, &frame);
    if (total) {
        float f = (float)((double)done / (double)total);
        SDL_FRect bar = { x + 2.0f * s, y + 2.0f * s, (bw - 4.0f * s) * (f > 1.0f ? 1.0f : f), bh - 4.0f * s };
        SDL_SetRenderDrawColor(ren, 200, 60, 50, 255);
        SDL_RenderFillRect(ren, &bar);
        snprintf(line, sizeof line, "%d %%", (int)(f * 100.0f));
    } else {                                            /* length unknown: a block going round */
        float f = (float)(now % 2000) / 2000.0f;
        SDL_FRect bar = { x + 2.0f * s + (bw - 4.0f * s) * 0.85f * f, y + 2.0f * s, (bw - 4.0f * s) * 0.15f, bh - 4.0f * s };
        SDL_SetRenderDrawColor(ren, 200, 60, 50, 255);
        SDL_RenderFillRect(ren, &bar);
        line[0] = 0;
    }
    SDL_SetRenderDrawColor(ren, 235, 235, 235, 255);
    text_at(x, y + bh + 6.0f * s, s, line);
    SDL_SetRenderDrawColor(ren, 110, 110, 120, 255);
    text_at(x, (float)h - 16.0f * s, s, "Esc or closing the window cancels");
    SDL_RenderPresent(ren);
    return 0;
}

/* INSTALL_FILES / INSTALL_DISC, 0 cancelled, -1 failed (message in err) */
static int import(const GameSetup *g, const char *source, char *err, size_t errlen)
{
    const char *leaf = source + strlen(source);
    while (leaf > source && leaf[-1] != '/' && leaf[-1] != '\\')
        leaf--;
    progress_source = *leaf ? leaf : source;
    SDL_Log("importing %s", source);
    int rc = install_import(g->base, source, progress, NULL, err, errlen);
    if (rc < 0)
        SDL_Log("import of %s failed: %s", source, err);
    return rc;
}

/* ---- asking the player ----------------------------------------------------------------- */

static int  pick_done;
static char pick_path[1024];

static void SDLCALL pick_cb(void *userdata, const char *const *list, int filter)
{
    (void)userdata; (void)filter;
    pick_path[0] = 0;
    if (list && list[0])
        snprintf(pick_path, sizeof pick_path, "%s", list[0]);
    pick_done = 1;
}

/* 1 with a path in out, 0 when the player quits */
static int ask_source(const char *text, char *out, size_t size)
{
    static const SDL_MessageBoxButtonData buttons[] = {
        { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Choose File\xE2\x80\xA6" },
        { 0, 2, "Choose Folder\xE2\x80\xA6" },
        { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Quit" },
    };
    static const SDL_DialogFileFilter filters[] = {
        { "Disc images and archives of the game", "cue;iso;chd;zip;7z;rar" },
        { "All files", "*" },
    };
    for (;;) {
        SDL_MessageBoxData box = { SDL_MESSAGEBOX_INFORMATION, win, title, text, 3, buttons, NULL };
        int hit = 0;
        if (!SDL_ShowMessageBox(&box, &hit) || hit == 0)
            return 0;
        pick_done = 0;
        if (hit == 1)
            SDL_ShowOpenFileDialog(pick_cb, NULL, win, filters, 2, NULL, false);
        else
            SDL_ShowOpenFolderDialog(pick_cb, NULL, win, NULL, false);
        while (!pick_done) {
            SDL_PumpEvents();
            SDL_Delay(10);
        }
        if (pick_path[0]) {
            snprintf(out, size, "%s", pick_path);
            return 1;
        }
    }
}

/* ---- the whole thing ---------------------------------------------------------------------- */

int setup_game(const LaleOptions *o, SDL_Window *window, SDL_Renderer *renderer, int interactive,
               GameSetup *g)
{
    static char found[24][1024];
    char err[1024] = "", msg[2400], exe[1024], chd[1200], cwd[8] = ".";
    int imported = INSTALL_NONE;
    win = window;
    ren = renderer;
    dialogs = interactive;
    memset(g, 0, sizeof *g);

    exe_dir(exe, sizeof exe);
    if (*o->base_dir) {
        snprintf(g->base, sizeof g->base, "%s", o->base_dir);
        strip_slash(g->base);
    } else if (writable(exe)) {
        snprintf(g->base, sizeof g->base, "%s", exe);
    } else {
        char *pref = SDL_GetPrefPath(NULL, "Lale64");
        snprintf(g->base, sizeof g->base, "%s", pref ? pref : ".");
        SDL_free(pref);
        strip_slash(g->base);
    }
    snprintf(g->data, sizeof g->data, "%s/DATA", g->base);
    snprintf(g->save, sizeof g->save, "%s/SAVE", g->base);
    if (install_mkdir(g->base) != 0 || install_mkdir(g->data) != 0 || install_mkdir(g->save) != 0) {
        snprintf(msg, sizeof msg, "Cannot create the folders DATA and SAVE in\n%s", g->base);
        show_error(msg);
        return -1;
    }
    SDL_Log("folders: %s (DATA, SAVE)", g->base);
    snprintf(g->video, sizeof g->video, "%s/VIDEO", exe);
    if (!is_dir(g->video))
        snprintf(g->video, sizeof g->video, "%s/VIDEO", g->base);
    if (!is_dir(g->video))
        g->video[0] = 0;

    /* 1. what the player named */
    if (*o->source) {
        imported = import(g, o->source, err, sizeof err);
        if (imported == 0)
            return 1;
        if (imported < 0) {
            snprintf(msg, sizeof msg, "Cannot use\n%s\n\n%s", o->source, err);
            show_error(msg);
            return -1;
        }
    }
    /* 2. nothing installed: look around */
    if (!install_have_chd(g->data, NULL, 0) && !install_have_files(g->data)) {
        int n = install_find_sources(exe, found, 8);
        if (strcmp(exe, g->base) != 0)
            n += install_find_sources(g->base, found + n, 8);
        n += install_find_sources(cwd, found + n, 6);
        if (n < 24 && drive_find_game("ISTANBUL", found[n], sizeof found[n]))
            n++;
        for (int i = 0; i < n && imported <= 0; i++) {
            imported = import(g, found[i], err, sizeof err);
            if (imported == 0)
                return 1;
        }
        /* 3. ask */
        const char *text =
            "This program needs your copy of the original game. It takes either\n\n"
            "  the game CD: put it in the drive and start again, or choose an image of it\n"
            "  (.cue with its .bin files, .iso, .chd), or\n\n"
            "  the files of the 2005 release: a .zip, .7z or .rar archive, or the folder\n"
            "  they were unpacked to.\n\n"
            "The game is copied to the folder DATA beside this program; saved games go to SAVE.";
        while (imported <= 0) {
            char path[1024];
            if (!interactive) {
                SDL_Log("no game installed in %s: start with the path of the game CD image (.cue, .iso, .chd), "
                        "of an archive of the 2005 release (.zip, .7z, .rar) or of its folder", g->data);
                return -1;
            }
            if (!ask_source(text, path, sizeof path))
                return 1;
            imported = import(g, path, err, sizeof err);
            if (imported == 0)
                return 1;
            if (imported < 0) {
                snprintf(msg, sizeof msg, "Cannot use\n%s\n\n%s\n\nChoose the game CD's image or the files of the 2005 release.",
                         path, err);
                text = msg;
            }
        }
    }

    /* 4. edition */
    int have_chd = install_have_chd(g->data, chd, sizeof chd), have_files = install_have_files(g->data);
    int want = !strcmp(o->edition, "cd") || o->cd_music ? EDITION_CD : !strcmp(o->edition, "2005") ? EDITION_2005 : -1;
    int audio = 0;
    if (want < 0 && imported == INSTALL_FILES)
        want = EDITION_2005;                            /* just brought in: that one, this time */
    if (have_chd && (want != EDITION_2005 || !have_files)) {
        if (chd_mount(chd, err, sizeof err) != 0) {
            snprintf(msg, sizeof msg, "Cannot use %s\n\n%s\n\nDelete the file and give the program the game CD again.", chd, err);
            if (!have_files || want == EDITION_CD) {
                show_error(msg);
                return -1;
            }
            SDL_Log("%s", msg);
        } else {
            g->disc = 1;
            for (int t = 1; t <= CDROM_MAX_TRACKS; t++)
                if (cdrom_track(t) && cdrom_track(t)->audio)
                    audio = 1;
        }
    }
    if (want == EDITION_CD && !(g->disc && audio)) {
        show_error("The CD edition needs the game CD with its audio tracks (DATA/LALE.CHD).\n\n"
                   "Start the program with the CD in the drive or with its image (.cue), "
                   "or set edition = 2005.");
        return -1;
    }
    g->edition = want >= 0 ? want : g->disc && audio ? EDITION_CD : EDITION_2005;
    if (g->disc && g->edition == EDITION_2005 && have_files) {
        cdrom_close();                                  /* the files in DATA are all it needs */
        g->disc = 0;
    }
    /* 5. */
    install_default_sescfg(g->data, g->save);
    SDL_Log("edition: %s, game data: %s", g->edition == EDITION_CD ? "1996 CD" : "2005",
            g->disc ? chd : g->data);
    return 0;
}
