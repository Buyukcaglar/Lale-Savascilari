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
#include "platform/ui.h"
#include "platform/uitext.h"

static SDL_Window   *win;
static SDL_Renderer *ren;
static int           dialogs;
static const char   *progress_source = "";

static void show_error(const char *msg)
{
    SDL_Log("%s", msg);
    if (dialogs)
        ui_message(win, 1, msg, NULL, 0, 0, 0);
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

static int progress(void *ctx, const char *text, uint64_t done, uint64_t total)
{
    (void)ctx;
    if (!dialogs)
        return 0;
    return ui_progress(win, progress_source, text, total ? (double)done / (double)total : -1.0);
}

/* The import's message ends in ": <source>" when it is about the source as a
   whole; the dialog names the source above it already. */
static void drop_source(char *err, const char *source)
{
    size_t n = strlen(err), k = strlen(source);
    if (n > k + 2 && strcmp(err + n - k, source) == 0 && err[n - k - 2] == ':')
        err[n - k - 2] = 0;
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
    ui_progress_close();
    if (rc < 0) {
        SDL_Log("import of %s failed: %s", source, err);
        drop_source(err, source);
    }
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

/* 1 with a path in out, 0 when the player quits.  `error`: the text says
   what was wrong with the last choice. */
static int ask_source(const char *text, int error, char *out, size_t size)
{
    static const char *const buttons[] = { T_CHOOSE_FILE, T_CHOOSE_FOLDER, T_QUIT };
    static const SDL_DialogFileFilter filters[] = {
        { T_FILTER_GAME, "cue;iso;chd;zip;7z;rar" },
        { T_FILTER_ALL, "*" },
    };
    for (;;) {
        int hit = ui_message(win, error, text, buttons, 3, 0, 2);
        if (hit != 0 && hit != 1)
            return 0;
        pick_done = 0;
        /* the system's picker; its title and buttons in Turkish where it lets
           them be set (the rest is the system's, in its own language) */
        SDL_PropertiesID p = SDL_CreateProperties();
        SDL_SetPointerProperty(p, SDL_PROP_FILE_DIALOG_WINDOW_POINTER, win);
        SDL_SetStringProperty(p, SDL_PROP_FILE_DIALOG_TITLE_STRING, hit == 0 ? T_PICK_FILE_TITLE : T_PICK_FOLDER_TITLE);
        SDL_SetStringProperty(p, SDL_PROP_FILE_DIALOG_ACCEPT_STRING, T_PICK_ACCEPT);
        SDL_SetStringProperty(p, SDL_PROP_FILE_DIALOG_CANCEL_STRING, T_CANCEL);
        if (hit == 0) {
            SDL_SetPointerProperty(p, SDL_PROP_FILE_DIALOG_FILTERS_POINTER, (void *)filters);
            SDL_SetNumberProperty(p, SDL_PROP_FILE_DIALOG_NFILTERS_NUMBER, 2);
        }
        SDL_ShowFileDialogWithProperties(hit == 0 ? SDL_FILEDIALOG_OPENFILE : SDL_FILEDIALOG_OPENFOLDER, pick_cb, NULL, p);
        SDL_DestroyProperties(p);
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
    dialogs = interactive || SDL_getenv("LALE_UI_SHOT") != NULL;      /* (ui_win32.c's test hook) */
    if (ren) {                                          /* the game's window stays black behind the dialogs */
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        SDL_RenderPresent(ren);
    }
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
        snprintf(msg, sizeof msg, T_NO_FOLDERS_FMT, g->base);
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
            snprintf(msg, sizeof msg, T_CANNOT_USE_FMT, o->source, err);
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
        const char *text = T_ASK_SOURCE;
        while (imported <= 0) {
            char path[1024];
            if (!dialogs) {
                SDL_Log("no game installed in %s: start with the path of the game CD image (.cue, .iso, .chd), "
                        "of an archive of the 2005 release (.zip, .7z, .rar) or of its folder", g->data);
                return -1;
            }
            if (!ask_source(text, text == msg, path, sizeof path))
                return 1;
            imported = import(g, path, err, sizeof err);
            if (imported == 0)
                return 1;
            if (imported < 0) {
                snprintf(msg, sizeof msg, T_CANNOT_USE_PICK_FMT, path, err);
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
            snprintf(msg, sizeof msg, T_BAD_CHD_FMT, chd, err);
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
        show_error(T_CD_EDITION_NEEDS_DISC);
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
