/*
 * hdvideo.c - see hdvideo.h.
 */
#include "hdvideo.h"

#include <SDL3/SDL.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- the files ------------------------------------------------------------------- */

#define MAX_FILES 256

static char file_dir[1100];
static char file_name[MAX_FILES][96];
static int  file_count;

static int video_ext(const char *name)
{
    const char *dot = strrchr(name, '.');
    return dot && (!SDL_strcasecmp(dot, ".mp4") || !SDL_strcasecmp(dot, ".m4v") || !SDL_strcasecmp(dot, ".mov"));
}

static SDL_EnumerationResult SDLCALL scan_cb(void *userdata, const char *dirname, const char *fname)
{
    (void)userdata; (void)dirname;
    if (file_count < MAX_FILES && video_ext(fname) && strlen(fname) < sizeof file_name[0])
        snprintf(file_name[file_count++], sizeof file_name[0], "%s", fname);
    return SDL_ENUM_CONTINUE;
}

int hdvideo_scan(const char *dir)
{
    file_count = 0;
    if (!dir || !*dir || !hdreader_available())
        return 0;
    snprintf(file_dir, sizeof file_dir, "%s", dir);
    SDL_EnumerateDirectory(dir, scan_cb, NULL);
    return file_count;
}

int hdvideo_find(const char *name, char *out, size_t size)
{
    size_t n = strlen(name);
    int best = -1;
    for (int i = 0; i < file_count; i++) {
        const char *f = file_name[i];
        if (SDL_strncasecmp(f, name, n) != 0)
            continue;
        if (f[n] == '.' && !strchr(f + n + 1, '.')) {   /* <NAME>.mp4 itself */
            best = i;
            break;
        }
        if (strchr("_- .(", f[n]) && (best < 0 || strlen(f) < strlen(file_name[best])))
            best = i;
    }
    if (best < 0)
        return 0;
    snprintf(out, size, "%s/%s", file_dir, file_name[best]);
    return 1;
}

/* ---- the decoder thread ------------------------------------------------------------ */

#define QUEUE 3

typedef struct Frame {
    uint8_t *pix;
    double   pts;
    int      valid;
} Frame;

struct HdVideo {
    SDL_Thread    *thread;
    SDL_Mutex     *lock;
    SDL_Condition *cond;
    char           path[1300];
    double         start;       /* frames before this time are dropped */
    double         want;        /* the time the consumer asked for last */
    int            w, h;
    int            state;       /* 0 opening, 1 decoding, 2 at the end, -1 failed */
    int            quit;
    int            shown;       /* a frame has been handed over */
    double         shown_pts;   /* its time */
    Frame          q[QUEUE];
};

static int free_slot(HdVideo *v)
{
    for (int i = 0; i < QUEUE; i++)
        if (!v->q[i].valid)
            return i;
    return -1;
}

static int SDLCALL decode_thread(void *arg)
{
    HdVideo *v = arg;
    char err[512] = "";
    int w = 0, h = 0, stats = SDL_getenv("LALE_VIDEO_STATS") != NULL;
    Uint64 t_open = SDL_GetTicksNS();
    HdReader *r = hdreader_open(v->path, &w, &h, err, sizeof err);
    if (stats)
        SDL_Log("4K video: opened in %.0f ms", (double)(SDL_GetTicksNS() - t_open) / 1e6);
    if (r && (w < 16 || h < 16 || w > 8192 || h > 4352)) {
        snprintf(err, sizeof err, "picture size %dx%d", w, h);
        hdreader_close(r);
        r = NULL;
    }
    SDL_LockMutex(v->lock);
    if (r) {
        v->w = w;
        v->h = h;
        for (int i = 0; i < QUEUE; i++)
            if (!(v->q[i].pix = malloc((size_t)w * (size_t)h * 4))) {
                snprintf(err, sizeof err, "out of memory");
                hdreader_close(r);
                r = NULL;
                break;
            }
    }
    if (!r) {
        SDL_Log("4K video: cannot play %s: %s", v->path, err);
        v->state = -1;
        SDL_BroadcastCondition(v->cond);
        SDL_UnlockMutex(v->lock);
        return 0;
    }
    SDL_UnlockMutex(v->lock);
    if (v->start > 0.05)
        hdreader_seek(r, v->start);
    Uint64 t0 = SDL_GetTicksNS();
    unsigned frames = 0;

    for (;;) {
        int slot;
        double pts = 0.0, want;
        SDL_LockMutex(v->lock);
        while (!v->quit && (slot = free_slot(v)) < 0)
            SDL_WaitCondition(v->cond, v->lock);
        want = v->want;
        if (v->quit) {
            SDL_UnlockMutex(v->lock);
            break;
        }
        SDL_UnlockMutex(v->lock);

        Uint64 t_read = SDL_GetTicksNS();
        int rc = hdreader_read(r, v->q[slot].pix, &pts);
        if (stats && frames < 4)
            SDL_Log("4K video: frame %u (%.3f s) read in %.0f ms", frames, pts, (double)(SDL_GetTicksNS() - t_read) / 1e6);
        frames++;
        /* Frames before the start (the decoder began at a key frame) and
           frames the game is well past are not kept: decoding runs on at
           full speed until it has caught up.  A frame less than a quarter
           second late is still shown, so a decoder that is only just too
           slow shows what it can instead of nothing. */
        if (rc == 1 && (pts < v->start - 0.0005 || pts < want - 0.25))
            continue;
        SDL_LockMutex(v->lock);
        if (rc == 1) {
            v->q[slot].pts = pts;
            v->q[slot].valid = 1;
            v->state = 1;
        } else {
            v->state = rc == 0 ? 2 : -1;
            if (rc < 0)
                SDL_Log("4K video: decoding %s stopped", v->path);
        }
        SDL_BroadcastCondition(v->cond);
        while (rc != 1 && !v->quit)             /* nothing more to do: wait for the end */
            SDL_WaitCondition(v->cond, v->lock);
        SDL_UnlockMutex(v->lock);
        if (rc != 1)
            break;
    }
    hdreader_close(r);
    if (stats)                                  /* how fast this machine decodes the file */
        SDL_Log("4K video: %s: %dx%d, %u frames decoded in %.2f s", v->path, w, h, frames,
                (double)(SDL_GetTicksNS() - t0) / 1e9);
    return 0;
}

HdVideo *hdvideo_open(const char *path, double start)
{
    HdVideo *v = calloc(1, sizeof *v);
    if (!v)
        return NULL;
    snprintf(v->path, sizeof v->path, "%s", path);
    v->start = start;
    v->want = start;
    v->lock = SDL_CreateMutex();
    v->cond = SDL_CreateCondition();
    v->thread = SDL_CreateThread(decode_thread, "hdvideo", v);
    if (!v->lock || !v->cond || !v->thread) {
        v->state = -1;
        if (!v->thread) {
            hdvideo_close(v);
            return NULL;
        }
    }
    return v;
}

void hdvideo_close(HdVideo *v)
{
    if (!v)
        return;
    if (v->thread) {
        SDL_LockMutex(v->lock);
        v->quit = 1;
        SDL_BroadcastCondition(v->cond);
        SDL_UnlockMutex(v->lock);
        SDL_WaitThread(v->thread, NULL);
    }
    for (int i = 0; i < QUEUE; i++)
        free(v->q[i].pix);
    SDL_DestroyCondition(v->cond);
    SDL_DestroyMutex(v->lock);
    free(v);
}

int hdvideo_ready(HdVideo *v)
{
    int r = 0;
    SDL_LockMutex(v->lock);
    if (v->state < 0)
        r = -1;
    else
        for (int i = 0; i < QUEUE; i++)
            if (v->q[i].valid)
                r = 1;
    if (!r && v->state == 2)
        r = -1;                                 /* ended without a frame */
    SDL_UnlockMutex(v->lock);
    return r;
}

int hdvideo_present(HdVideo *v, double t, HdUploadFn upload, void *ctx)
{
    int best = -1, first = -1, done = 0;
    SDL_LockMutex(v->lock);
    v->want = t;
    for (int i = 0; i < QUEUE; i++) {
        if (!v->q[i].valid)
            continue;
        if (first < 0 || v->q[i].pts < v->q[first].pts)
            first = i;
        if (v->q[i].pts <= t + 0.0005 && (best < 0 || v->q[i].pts > v->q[best].pts))
            best = i;
    }
    if (best < 0 && !v->shown)
        best = first;                           /* nothing shown yet: the first frame there is */
    if (best >= 0) {
        upload(ctx, v->q[best].pix, v->w, v->h);
        v->shown_pts = v->q[best].pts;
        for (int i = 0; i < QUEUE; i++)         /* it and everything older is done with */
            if (v->q[i].valid && v->q[i].pts <= v->q[best].pts)
                v->q[i].valid = 0;
        v->shown = 1;
        done = 1;
        SDL_BroadcastCondition(v->cond);
    }
    SDL_UnlockMutex(v->lock);
    return done;
}

double hdvideo_shown_pts(const HdVideo *v) { return v->shown ? v->shown_pts : -1.0; }

/* ---- systems without a decoder ------------------------------------------------------- */

#if !defined(_WIN32) && !defined(__APPLE__)
int hdreader_available(void) { return 0; }

HdReader *hdreader_open(const char *path, int *w, int *h, char *err, size_t errlen)
{
    (void)path; (void)w; (void)h;
    snprintf(err, errlen, "no video decoder in this build");
    return NULL;
}

int  hdreader_seek(HdReader *r, double seconds) { (void)r; (void)seconds; return -1; }
int  hdreader_read(HdReader *r, uint8_t *bgrx, double *pts) { (void)r; (void)bgrx; (void)pts; return -1; }
void hdreader_close(HdReader *r) { (void)r; }
#endif
