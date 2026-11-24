/*
 * sdl3_main.c - SDL3 host for the port.
 *
 * The decompiled game owns the main loop, exactly like the DOS original: it
 * runs on the main thread and, wherever the original waited (clock, retrace,
 * keyboard), calls into src/platform/pc.c, which calls back here:
 *
 *   host_pump()     SDL events -> BIOS keyboard buffer / mouse driver state,
 *                   game + CD audio top-up (src/platform/audio_sdl.c); window close -> exit.
 *   host_present()  at every emulated vertical retrace: VGA scan-out
 *                   (vga_render) -> 8-bit RGB texture, aspect-correct 4:3
 *                   (letterboxed when the window is not 4:3: full screen).
 *
 * Default action: the game, ist_main() (src/game/istanbul.c), after the
 * set-up istanbul.h asks of the host (pc_reset, the game's folders, dg_reset +
 * bc_farheap_reset, audio, pump/present hooks).  Where the game's files come
 * from is setup.h: DATA and SAVE beside the executable, filled at the first
 * start from the player's CD, disc image, archive or folder.  With
 * --game-dir the program runs in a folder of the DOS game as it is instead
 * (development, tools/verify).
 *
 * Videos: with 4K copies of the game's videos in the folder VIDEO, Ctrl+V
 * switches between them and the originals at any time (hdvideo.h); the keys
 * are the same on Windows and macOS.
 *
 * macOS keyboard (key_down): Command combinations belong to the host
 * (Cmd+Q quit and the rest of SDL's menu bar; Cmd+Return / Cmd+F full screen;
 * Cmd+. = Ctrl-Break, as Mac keyboards have neither Pause nor ScrollLock -
 * Ctrl+F15 / Ctrl+F14, their keys on an extended Apple keyboard, work too);
 * the LEFT Option key is the PC's Alt, the right one keeps typing characters.
 *
 * Options: lale.ini + command line (src/platform/options.c).
 *     Lale64 [<source>] [--edition cd|2005] [--ist-args n]   the game (options.h)
 *     Lale64 ... --frames <n> [--screenshot f.bmp]      quit after n frames (tests)
 *     Lale64 --script s.txt --out <dir> [--seed n] [--step-us n] [--frame-log <dir>]
 *                                           input script / verification mode
 *                                           (docs/VERIFY.md, "input scripts" below)
 *     Lale64 ... --video-toggle-at <n>        the Ctrl+V switch at the n-th frame (tests)
 * Test modes:
 *     Lale64 --cd-music --cd-image <disc.cue> --cd-play 2 [--seconds 15]
 *     Lale64 --vga-test                       VGA/host smoke test (palette, mouse)
 */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <stdlib.h>
#include <string.h>

#include "engine/dgroup.h"
#include "engine/edition.h"
#include "game/cd5737.h"
#include "game/istanbul.h"
#include "platform/about.h"
#include "platform/app_icon.h"
#include "platform/audio_sdl.h"
#include "engine/nkontrol.h"
#include "game/gfl.h"
#include "platform/cdrom.h"
#include "platform/hdvideo.h"
#include "platform/chd.h"
#include "platform/options.h"
#include "platform/pc.h"
#include "platform/setup.h"
#include "rtl/bc_farheap.h"
#include "rtl/bc_stdio.h"

static SDL_Window      *window;
static SDL_Renderer    *renderer;
static SDL_Texture     *texture;
static int              tex_w, tex_h;
static VgaFrame         frame;
static uint32_t         rgba[640 * 480];
static int              frame_w = 320;                  /* mouse x mapping */
static int              frames_left;                    /* --frames: 0 = no limit */
static const char      *screenshot_path;                /* --screenshot */
static int              script_mode;                    /* --script: host input ignored */

#ifdef __APPLE__
#define HOST_APPLE 1
#else
#define HOST_APPLE 0
#endif
/* the PC's Alt key: both Alt keys, on macOS the left Option key only */
#define HOST_KMOD_ALT (HOST_APPLE ? SDL_KMOD_LALT : SDL_KMOD_ALT)

static int  hd_files;                   /* video files in VIDEO: 0 = no 4K switch (see below) */
static void hd_toggle(void);

static void toggle_fullscreen(void)
{
    if (window)
        SDL_SetWindowFullscreen(window, !(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN));
}

/* ======================================================================== */
/* keyboard: SDL -> PC scan code set 1 + BIOS ASCII                          */
/* ======================================================================== */

static uint8_t pc_scancode(SDL_Scancode s)
{
    static const struct { SDL_Scancode s; uint8_t pc; } map[] = {
        { SDL_SCANCODE_ESCAPE, 0x01 }, { SDL_SCANCODE_1, 0x02 }, { SDL_SCANCODE_2, 0x03 },
        { SDL_SCANCODE_3, 0x04 }, { SDL_SCANCODE_4, 0x05 }, { SDL_SCANCODE_5, 0x06 },
        { SDL_SCANCODE_6, 0x07 }, { SDL_SCANCODE_7, 0x08 }, { SDL_SCANCODE_8, 0x09 },
        { SDL_SCANCODE_9, 0x0A }, { SDL_SCANCODE_0, 0x0B }, { SDL_SCANCODE_MINUS, 0x0C },
        { SDL_SCANCODE_EQUALS, 0x0D }, { SDL_SCANCODE_BACKSPACE, 0x0E }, { SDL_SCANCODE_TAB, 0x0F },
        { SDL_SCANCODE_Q, 0x10 }, { SDL_SCANCODE_W, 0x11 }, { SDL_SCANCODE_E, 0x12 },
        { SDL_SCANCODE_R, 0x13 }, { SDL_SCANCODE_T, 0x14 }, { SDL_SCANCODE_Y, 0x15 },
        { SDL_SCANCODE_U, 0x16 }, { SDL_SCANCODE_I, 0x17 }, { SDL_SCANCODE_O, 0x18 },
        { SDL_SCANCODE_P, 0x19 }, { SDL_SCANCODE_LEFTBRACKET, 0x1A }, { SDL_SCANCODE_RIGHTBRACKET, 0x1B },
        { SDL_SCANCODE_RETURN, 0x1C }, { SDL_SCANCODE_KP_ENTER, 0x1C }, { SDL_SCANCODE_A, 0x1E },
        { SDL_SCANCODE_S, 0x1F }, { SDL_SCANCODE_D, 0x20 }, { SDL_SCANCODE_F, 0x21 },
        { SDL_SCANCODE_G, 0x22 }, { SDL_SCANCODE_H, 0x23 }, { SDL_SCANCODE_J, 0x24 },
        { SDL_SCANCODE_K, 0x25 }, { SDL_SCANCODE_L, 0x26 }, { SDL_SCANCODE_SEMICOLON, 0x27 },
        { SDL_SCANCODE_APOSTROPHE, 0x28 }, { SDL_SCANCODE_GRAVE, 0x29 }, { SDL_SCANCODE_BACKSLASH, 0x2B },
        { SDL_SCANCODE_Z, 0x2C }, { SDL_SCANCODE_X, 0x2D }, { SDL_SCANCODE_C, 0x2E },
        { SDL_SCANCODE_V, 0x2F }, { SDL_SCANCODE_B, 0x30 }, { SDL_SCANCODE_N, 0x31 },
        { SDL_SCANCODE_M, 0x32 }, { SDL_SCANCODE_COMMA, 0x33 }, { SDL_SCANCODE_PERIOD, 0x34 },
        { SDL_SCANCODE_SLASH, 0x35 }, { SDL_SCANCODE_KP_MULTIPLY, 0x37 }, { SDL_SCANCODE_SPACE, 0x39 },
        { SDL_SCANCODE_F1, 0x3B }, { SDL_SCANCODE_F2, 0x3C }, { SDL_SCANCODE_F3, 0x3D },
        { SDL_SCANCODE_F4, 0x3E }, { SDL_SCANCODE_F5, 0x3F }, { SDL_SCANCODE_F6, 0x40 },
        { SDL_SCANCODE_F7, 0x41 }, { SDL_SCANCODE_F8, 0x42 }, { SDL_SCANCODE_F9, 0x43 },
        { SDL_SCANCODE_F10, 0x44 }, { SDL_SCANCODE_HOME, 0x47 }, { SDL_SCANCODE_UP, 0x48 },
        { SDL_SCANCODE_PAGEUP, 0x49 }, { SDL_SCANCODE_KP_MINUS, 0x4A }, { SDL_SCANCODE_LEFT, 0x4B },
        { SDL_SCANCODE_KP_5, 0x4C }, { SDL_SCANCODE_RIGHT, 0x4D }, { SDL_SCANCODE_KP_PLUS, 0x4E },
        { SDL_SCANCODE_END, 0x4F }, { SDL_SCANCODE_DOWN, 0x50 }, { SDL_SCANCODE_PAGEDOWN, 0x51 },
        { SDL_SCANCODE_INSERT, 0x52 }, { SDL_SCANCODE_DELETE, 0x53 }, { SDL_SCANCODE_F11, 0x57 },
        { SDL_SCANCODE_F12, 0x58 }, { SDL_SCANCODE_NONUSBACKSLASH, 0x56 },
    };
    for (size_t i = 0; i < sizeof map / sizeof map[0]; i++)
        if (map[i].s == s)
            return map[i].pc;
    return 0;
}

/* Unicode -> cp857 for the characters a Turkish keyboard produces. */
static int cp857(uint32_t cp)
{
    static const struct { uint16_t u; uint8_t c; } map[] = {
        { 0xC7, 0x80 }, { 0xFC, 0x81 }, { 0xE9, 0x82 }, { 0xE2, 0x83 }, { 0xE4, 0x84 },
        { 0xE0, 0x85 }, { 0xE7, 0x87 }, { 0xEA, 0x88 }, { 0xEB, 0x89 }, { 0xE8, 0x8A },
        { 0xEF, 0x8B }, { 0xEE, 0x8C }, { 0x131, 0x8D }, { 0xC4, 0x8E }, { 0xC9, 0x90 },
        { 0xF4, 0x93 }, { 0xF6, 0x94 }, { 0xF2, 0x95 }, { 0xFB, 0x96 }, { 0xF9, 0x97 },
        { 0x130, 0x98 }, { 0xD6, 0x99 }, { 0xDC, 0x9A }, { 0x15E, 0x9E }, { 0x15F, 0x9F },
        { 0xE1, 0xA0 }, { 0xED, 0xA1 }, { 0xF3, 0xA2 }, { 0xFA, 0xA3 }, { 0xF1, 0xA4 },
        { 0xD1, 0xA5 }, { 0x11E, 0xA6 }, { 0x11F, 0xA7 },
    };
    if (cp >= 0x20 && cp < 0x7F)
        return (int)cp;
    for (size_t i = 0; i < sizeof map / sizeof map[0]; i++)
        if (map[i].u == cp)
            return map[i].c;
    return -1;
}

static uint8_t last_scan;   /* scan code of the key whose text arrives next */

static void key_down(const SDL_KeyboardEvent *k)
{
    uint8_t sc = pc_scancode(k->scancode);
    SDL_Keymod m = k->mod;
    uint8_t shift = (uint8_t)(((m & SDL_KMOD_RSHIFT) ? 1 : 0) | ((m & SDL_KMOD_LSHIFT) ? 2 : 0) |
                              ((m & SDL_KMOD_CTRL) ? 4 : 0) | ((m & HOST_KMOD_ALT) ? 8 : 0));
    pc_set_shift_flags(shift);
    if (!k->down)
        return;                                         /* release: shift flags only */
    /* Ctrl+V: 4K videos <-> original videos - the host's, when there are 4K files */
    if (hd_files && k->key == SDLK_V && (m & SDL_KMOD_CTRL)) {
        if (!k->repeat)
            hd_toggle();
        return;
    }
    if (HOST_APPLE && (m & SDL_KMOD_GUI)) {             /* Command: the host's, never the game's */
        if (k->repeat)
            return;
        if (hd_files && k->key == SDLK_V)
            hd_toggle();                                /* Cmd+V as well, as a Mac user would try */
        else if (k->scancode == SDL_SCANCODE_PERIOD)
            pc_ctrl_break();                            /* Cmd+. : the Mac's break key */
        else if (k->scancode == SDL_SCANCODE_RETURN || k->scancode == SDL_SCANCODE_KP_ENTER ||
                 k->scancode == SDL_SCANCODE_F)
            toggle_fullscreen();
        return;
    }
    /* BIOS INT 09h: Ctrl-Break is Ctrl+Pause on the enhanced keyboard and
       Ctrl+ScrollLock (scan 46h) on every keyboard */
    if ((m & SDL_KMOD_CTRL) && !k->repeat &&
        (k->scancode == SDL_SCANCODE_PAUSE || k->scancode == SDL_SCANCODE_SCROLLLOCK ||
         k->scancode == SDL_SCANCODE_CANCEL ||
         (HOST_APPLE && (k->scancode == SDL_SCANCODE_F15 || k->scancode == SDL_SCANCODE_F14)))) {
        pc_ctrl_break();
        return;
    }
    if (!sc)
        return;
    if (pc_int09(sc))                                   /* the game's INT 09h hook */
        return;
    last_scan = sc;
    if (sc >= 0x3B && sc <= 0x44) {                     /* F1..F10 with modifiers */
        uint8_t s = sc;
        if (m & HOST_KMOD_ALT)       s = (uint8_t)(sc + 0x2D);
        else if (m & SDL_KMOD_CTRL)  s = (uint8_t)(sc + 0x23);
        else if (m & SDL_KMOD_SHIFT) s = (uint8_t)(sc + 0x19);
        pc_key_push((uint16_t)(s << 8));
        return;
    }
    switch (k->scancode) {                              /* keys without SDL text */
    case SDL_SCANCODE_ESCAPE:    pc_key_push(0x011B); return;
    case SDL_SCANCODE_BACKSPACE: pc_key_push(0x0E08); return;
    case SDL_SCANCODE_TAB:       pc_key_push((m & SDL_KMOD_SHIFT) ? 0x0F00 : 0x0F09); return;
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER:  pc_key_push(0x1C0D); return;
    default: break;
    }
    if (sc >= 0x47 && sc <= 0x53 && sc != 0x4A && sc != 0x4E && !(k->scancode >= SDL_SCANCODE_KP_1 && k->scancode <= SDL_SCANCODE_KP_0)) {
        pc_key_push((uint16_t)(sc << 8));               /* cursor block: ASCII 0 */
        return;
    }
    if (m & HOST_KMOD_ALT) {                            /* Alt+key: ASCII 0 */
        pc_key_push((uint16_t)(sc << 8));
        return;
    }
    if ((m & SDL_KMOD_CTRL) && k->key >= 'a' && k->key <= 'z') {
        pc_key_push((uint16_t)((sc << 8) | (k->key - 'a' + 1)));
        return;
    }
    /* printable keys arrive as SDL_EVENT_TEXT_INPUT */
}

static void text_input(const char *utf8)
{
    /* macOS: Option+key also types a character (Option+C = c-cedilla); with
       the left Option key the game already has its Alt+key word */
    if (HOST_APPLE && (SDL_GetModState() & (SDL_KMOD_LALT | SDL_KMOD_GUI)))
        return;
    while (*utf8) {
        uint32_t cp = SDL_StepUTF8(&utf8, NULL);
        int c = cp857(cp);
        if (c >= 0)
            pc_key_push((uint16_t)((last_scan << 8) | (uint8_t)c));
    }
}

/* ======================================================================== */
/* pump / present                                                           */
/* ======================================================================== */

/* The picture's place in the output (pixels): the largest 4:3 rectangle,
   centred - the whole window while SDL_SetWindowAspectRatio holds, with black
   bars beside it in full screen on a display that is not 4:3. */
static SDL_FRect present_rect(void)
{
    int ow = 0, oh = 0;
    SDL_FRect r = { 0, 0, 4, 3 };
    if (!renderer || !SDL_GetCurrentRenderOutputSize(renderer, &ow, &oh) || ow <= 0 || oh <= 0)
        return r;
    if ((int64_t)ow * 3 > (int64_t)oh * 4) {            /* too wide: bars left and right */
        r.h = (float)oh;
        r.w = (float)oh * 4.0f / 3.0f;
    } else {                                            /* too tall: bars above and below */
        r.w = (float)ow;
        r.h = (float)ow * 3.0f / 4.0f;
    }
    r.x = ((float)ow - r.w) / 2.0f;
    r.y = ((float)oh - r.h) / 2.0f;
    return r;
}

/* ---- the videos in 4K (hdvideo.h) ------------------------------------------------ */

static int          hd_on;              /* the player's choice: 1 = 4K, 0 = original */
static HdVideo     *hd;                 /* decoder of the video the game is playing */
static char         hd_name[16];        /* that video's name, "" between videos */
static int          hd_started;         /* its clock runs (PC_VIDEO_START seen) */
static int          hd_waiting;         /* inside the wait for the first frame */
static SDL_Texture *hd_tex;
static int          hd_tex_w, hd_tex_h;
static int          hd_have;            /* hd_tex holds a frame of this video */
static SDL_FRect    hd_src;             /* the picture inside the frame (side bars cut off) */
static int          hd_bar;             /* narrowest black side bar seen in this video, -1 = none yet */
static Uint64       hd_asked;           /* SDL_GetTicks() when its decoder was started */
static int          hd_frame;           /* the game's frame hd_time() saw last */
static int          hd_seen;            /* how many different frames of the game it saw (statistics) */
static Uint64       hd_frame_ns;        /* SDL_GetTicksNS() when it first saw it */
static const char  *notice_text;        /* "4K Video" / "Orijinal Video" after a switch */
static Uint64       notice_until;       /* SDL_GetTicks() */
static Uint64       last_draw;
static int          present_count;
static int          toggle_at;          /* --video-toggle-at <n>: switch at the n-th frame (tests) */
static int          capture_request;    /* the next draw_output() keeps what it drew */
static SDL_Surface *captured;

/* The video's time in seconds.  It follows the frame the game itself has on
   the screen, not the game's clock: frame n of the original stands for the
   time n / fps, and the 4K picture changes in the same window update as the
   game's.  Within that frame's tenth of a second the host's clock counts
   on, which is what lets a copy with more frames per second show its
   in-between frames; it stops just short of the next frame's time, so the
   4K picture never runs ahead of the game. */
static double hd_time(void)
{
    if (!hd_started)
        return 0.0;
    int n = g_gfl_last_shown, fps = g_gfl_fps ? g_gfl_fps : 10;
    Uint64 now = SDL_GetTicksNS();
    if (n != hd_frame) {
        hd_frame = n;
        hd_frame_ns = now;
        hd_seen++;
    }
    if (n < 0)
        return 0.0;
    double part = (double)(now - hd_frame_ns) / 1e9 * (double)fps;
    return ((double)n + (part > 0.98 ? 0.98 : part)) / (double)fps;
}

static int hd_showing(void) { return hd && hd_on && hd_started && hd_have; }

/* The picture inside the frame.  A frame no wider than 16:10 is all picture.
   A wider one (a 16:9 canvas) is taken to be padded at the sides: the black
   bars are measured on 15 rows, the narrowest seen in this video counts (a
   black frame says nothing), and the picture is the centred 4:3 or 16:10
   part if the bars are that wide. */
static void hd_measure(const uint8_t *pix, int w, int h)
{
    hd_src = (SDL_FRect){ 0.0f, 0.0f, (float)w, (float)h };
    if ((int64_t)w * 100 <= (int64_t)h * 165)
        return;
    int limit = w / 2, bar = limit;
    for (int i = 1; i < 16 && bar > 0; i++) {
        const uint8_t *row = pix + (size_t)(h * i / 16) * (size_t)w * 4;
        int l = 0, r = 0;
        while (l < bar && row[4 * l] < 28 && row[4 * l + 1] < 28 && row[4 * l + 2] < 28)
            l++;
        const uint8_t *end = row + (size_t)(w - 1) * 4;
        while (r < bar && end[-4 * r] < 28 && end[-4 * r + 1] < 28 && end[-4 * r + 2] < 28)
            r++;
        bar = l < r ? l : r;
    }
    if (bar < limit && (hd_bar < 0 || bar < hd_bar))
        hd_bar = bar;
    if (hd_bar < 0)
        return;
    int tol = w / 200 + 2, bar43 = (w - h * 4 / 3) / 2, bar1610 = (w - h * 8 / 5) / 2;
    int cut = hd_bar >= bar43 - tol ? bar43 : hd_bar >= bar1610 - tol ? bar1610 : 0;
    if (cut > 0)
        hd_src = (SDL_FRect){ (float)cut, 0.0f, (float)(w - 2 * cut), (float)h };
}

static void hd_upload(void *ctx, const uint8_t *pix, int w, int h)
{
    (void)ctx;
    if (!hd_tex || w != hd_tex_w || h != hd_tex_h) {
        if (hd_tex)
            SDL_DestroyTexture(hd_tex);
        hd_tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
        if (!hd_tex) {
            SDL_Log("4K video: no %dx%d texture: %s", w, h, SDL_GetError());
            return;
        }
        SDL_SetTextureScaleMode(hd_tex, SDL_SCALEMODE_LINEAR);
        hd_tex_w = w;
        hd_tex_h = h;
    }
    SDL_UpdateTexture(hd_tex, NULL, pix, w * 4);
    hd_measure(pix, w, h);
    if (!hd_have && SDL_getenv("LALE_VIDEO_STATS"))
        SDL_Log("4K video: %s on the screen %u ms after it was asked for, at video time %.2f s", hd_name,
                (unsigned)(SDL_GetTicks() - hd_asked), hd_time());
    hd_have = 1;
}

static void hd_close_current(void)
{
    if (hd)
        hdvideo_close(hd);
    hd = NULL;
    hd_have = 0;
}

static void hd_open_current(double start)
{
    char path[1400];
    if (hd || !hd_name[0] || !hdvideo_find(hd_name, path, sizeof path))
        return;
    hd = hdvideo_open(path, start);
    hd_asked = SDL_GetTicks();
    hd_have = 0;
    hd_bar = -1;
}

/* What the game's video player reports (pc_video_event). */
static void host_video_event(int event, const char *name)
{
    switch (event) {
    case PC_VIDEO_OPEN:
        hd_close_current();
        snprintf(hd_name, sizeof hd_name, "%s", name);
        hd_started = 0;
        if (hd_on) {
            /* The first frame is decoded before the game goes on, so that the
               video does not begin as the original and turn sharp a moment
               later.  To the game this is time spent opening the file; its
               timer keeps running (pc_poll), as it did during a disk read. */
            Uint64 t0 = SDL_GetTicks();
            hd_open_current(0.0);
            hd_waiting = 1;
            while (hd && hdvideo_ready(hd) == 0 && SDL_GetTicks() - t0 < 3000) {
                pc_poll();
                SDL_Delay(1);
            }
            hd_waiting = 0;
            if (!hd_on || (hd && hdvideo_ready(hd) < 0))
                hd_close_current();             /* switched off meanwhile, or unplayable */
            SDL_Log("video %s: %s", hd_name, hd ? "4K" : "original (no 4K file)");
        }
        break;
    case PC_VIDEO_START:
        hd_started = 1;
        hd_frame = -2;
        hd_seen = 0;
        break;
    case PC_VIDEO_STOP:
        if (SDL_getenv("LALE_VIDEO_STATS"))
            SDL_Log("video %s: the game showed %d of its %d frames (%s)", hd_name, hd_seen, (int)g_gfl_nframes,
                    hd ? "4K" : "original");
        hd_close_current();
        hd_name[0] = 0;
        hd_started = 0;
        break;
    default:
        break;
    }
}

static void draw_notice(const SDL_FRect *area)
{
    /* letters the size of the game's own 8-pixel text on its 200 lines */
    float s = SDL_floorf(area->h / 200.0f);
    if (s < 2.0f)
        s = 2.0f;
    float tw = (float)strlen(notice_text) * 8.0f * s, pad = 3.0f * s, m = 4.0f * s;
    SDL_FRect box = { area->x + area->w - tw - 2.0f * pad - m, area->y + m, tw + 2.0f * pad, 8.0f * s + 2.0f * pad };
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 170);
    SDL_RenderFillRect(renderer, &box);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_SetRenderScale(renderer, s, s);
    SDL_RenderDebugText(renderer, (box.x + pad) / s, (box.y + pad) / s, notice_text);
    SDL_SetRenderScale(renderer, 1.0f, 1.0f);
}

/* The window: the game's picture, over it the 4K frame while one is shown,
   over that the notice of the last switch for its three seconds. */
static void draw_output(void)
{
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_FRect area = present_rect();
    if (texture)
        SDL_RenderTexture(renderer, texture, NULL, &area);
    if (hd_showing())
        SDL_RenderTexture(renderer, hd_tex, &hd_src, &area);
    if (notice_text) {
        if (SDL_GetTicks() < notice_until)
            draw_notice(&area);
        else
            notice_text = NULL;
    }
    if (capture_request) {
        capture_request = 0;
        captured = SDL_RenderReadPixels(renderer, NULL);
    }
    SDL_RenderPresent(renderer);
    last_draw = SDL_GetTicks();
}

/* Ctrl+V */
static void hd_toggle(void)
{
    hd_on = !hd_on;
    notice_text = hd_on ? "4K Video" : "Orijinal Video";
    notice_until = SDL_GetTicks() + 3000;
    SDL_Log("videos: %s", notice_text);
    if (!hd_waiting) {
        if (hd_on)
            hd_open_current(hd_time());         /* in the middle of a video: from where it is */
        else
            hd_close_current();
    }
    if (renderer)
        draw_output();
}

static void host_pump(void)
{
    static Uint64 last;
    Uint64 now = SDL_GetTicksNS();
    /* SDL_PollEvent is cheap but not free; busy loops call us very often */
    if (now - last < 2 * SDL_NS_PER_MS)
        return;
    last = now;
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_EVENT_QUIT:
            cdrom_close();
            SDL_Quit();
            exit(0);
        case SDL_EVENT_KEY_DOWN:
            if (script_mode) break;
            key_down(&ev.key);
            break;
        case SDL_EVENT_KEY_UP:
            if (script_mode) break;
            key_down(&ev.key);                          /* refresh shift flags only */
            break;
        case SDL_EVENT_TEXT_INPUT:
            if (script_mode) break;
            text_input(ev.text.text);
            break;
        case SDL_EVENT_MOUSE_MOTION:
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            if (script_mode) break;
            float x, y;
            SDL_MouseButtonFlags b = SDL_GetMouseState(&x, &y);
            /* window coordinates -> output pixels -> the picture's 4:3 area
               -> frame pixels (frame_w x tex_h); the driver's virtual screen
               is 640 wide in 320-pixel modes (x is doubled) */
            SDL_FRect a = present_rect();
            float d = SDL_GetWindowPixelDensity(window);
            if (d <= 0.0f)
                d = 1.0f;
            float lx = (x * d - a.x) * (float)frame_w / a.w;
            float ly = (y * d - a.y) * (float)(tex_h > 0 ? tex_h : 200) / a.h;
            float mx = (float)frame_w - 1.0f, my = (float)(tex_h > 0 ? tex_h : 200) - 1.0f;
            lx = lx < 0.0f ? 0.0f : lx > mx ? mx : lx;  /* pointer over the bars */
            ly = ly < 0.0f ? 0.0f : ly > my ? my : ly;
            int16_t vx = (int16_t)(lx * 640.0f / (float)frame_w);
            int16_t vy = (int16_t)(ly);
            pc_mouse_host(vx, vy, (uint16_t)(((b & SDL_BUTTON_LMASK) ? 1 : 0) |
                                             ((b & SDL_BUTTON_RMASK) ? 2 : 0)));
            break;
        }
        default:
            break;
        }
    }
    audio_sdl_pump();
    /* the notice appears and goes on time even while the game draws nothing */
    if (notice_text && renderer && SDL_GetTicks() - last_draw > 100)
        draw_output();
}

static void host_present(void)
{
    vga_render(&pc_vga, &frame);
    int w = frame.width, h = frame.height / (frame.line_repeat ? frame.line_repeat : 1);
    if (w <= 0 || h <= 0 || w > 640 || h > 480)
        return;
    if (w != tex_w || h != tex_h) {
        if (texture)
            SDL_DestroyTexture(texture);
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
        SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
        tex_w = w;
        tex_h = h;
        frame_w = w;
        /* 4:3 output whatever the pixel grid (320x200 has non-square
           pixels): the texture is stretched over present_rect() below */
    }
    uint32_t pal[256];
    for (int i = 0; i < 256; i++) {
        /* 6-bit DAC -> 8 bit the way VGA DACs/DOSBox-X expand it */
        uint32_t r = frame.dac[i][0], g = frame.dac[i][1], b = frame.dac[i][2];
        r = (r << 2) | (r >> 4); g = (g << 2) | (g >> 4); b = (b << 2) | (b >> 4);
        pal[i] = (r << 16) | (g << 8) | b;
    }
    int rep = frame.line_repeat ? frame.line_repeat : 1;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            rgba[y * w + x] = frame.blanked ? 0 : pal[frame.pixels[(y * rep) * frame.width + x]];
    SDL_UpdateTexture(texture, NULL, rgba, w * 4);
    if (toggle_at && ++present_count == toggle_at)
        hd_toggle();
    if (hd_started) {
        double t = hd_time();
        if (hd && hd_on)
            hdvideo_present(hd, t, hd_upload, NULL);
    }
    /* --screenshot of a frame with a 4K picture or a notice: what the window shows */
    if (frames_left == 1 && screenshot_path && *screenshot_path && (hd_showing() || notice_text)) {
        capture_request = 1;
        if (hd_showing())
            SDL_Log("4K video: %s, the game shows frame %d, the window the 4K frame of %.3f s", hd_name,
                    (int)g_gfl_last_shown, hdvideo_shown_pts(hd));
    }
    draw_output();
    if (frames_left > 0 && --frames_left == 0) {
        if (screenshot_path && *screenshot_path) {
            SDL_Surface *shot = captured ? captured
                                         : SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_XRGB8888, rgba, w * 4);
            if (!shot || !SDL_SaveBMP(shot, screenshot_path))
                SDL_Log("screenshot %s: %s", screenshot_path, SDL_GetError());
            else
                SDL_Log("screenshot %s (%dx%d)", screenshot_path, shot->w, shot->h);
            SDL_DestroySurface(shot);
            if (captured) {                     /* and the game's own picture of that moment beside it */
                char own[1100];
                snprintf(own, sizeof own, "%s.game.bmp", screenshot_path);
                shot = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_XRGB8888, rgba, w * 4);
                if (shot)
                    SDL_SaveBMP(shot, own);
                SDL_DestroySurface(shot);
            }
        }
        SDL_Log("--frames reached: quitting");
        audio_sdl_close();
        cdrom_close();
        SDL_Quit();
        exit(0);
    }
}

static void host_sleep(uint32_t us)
{
    SDL_DelayPrecise((Uint64)us * 1000u);
}


/* Program icon (title bar, taskbar, dock): src/gen/app_icon.c. */
static void set_window_icon(SDL_Window *w)
{
    SDL_Surface *s = SDL_CreateSurfaceFrom(app_icon_size, app_icon_size, SDL_PIXELFORMAT_RGBA32,
                                           (void *)app_icon_rgba, app_icon_size * 4);
    if (s) {
        SDL_SetWindowIcon(w, s);
        SDL_DestroySurface(s);
    }
}

static int host_open_window(void)
{
    if (window)
        return 0;
    /* "İstanbul Efsaneleri - Lale Savaşçıları" in UTF-8 (SDL's encoding), written
     * with escapes so the source's own encoding cannot change it. */
    static const char window_title[] =
        "\xC4\xB0stanbul Efsaneleri - Lale Sava\xC5\x9F\xC3\xA7\xC4\xB1lar\xC4\xB1";
    /* macOS: a Retina window gets its full pixel grid (sharp pixel edges) */
    if (!SDL_CreateWindowAndRenderer(window_title, 960, 720,
                                     SDL_WINDOW_RESIZABLE | (HOST_APPLE ? SDL_WINDOW_HIGH_PIXEL_DENSITY : 0),
                                     &window, &renderer)) {
        SDL_Log("window: %s", SDL_GetError());
        return -1;
    }
    /* 4:3 window area; a window that cannot be 4:3 (full screen) is letterboxed */
    SDL_SetWindowAspectRatio(window, 4.0f / 3.0f, 4.0f / 3.0f);
    set_window_icon(window);
    about_install(window);               /* Windows: system menu, macOS: app menu - About... */
    SDL_SetRenderVSync(renderer, 0);      /* pacing comes from the emulated retrace */
    SDL_StartTextInput(window);
    pc_set_host_pump(host_pump);
    pc_set_host_present(host_present);
    pc_set_host_sleep(host_sleep);
    return 0;
}

/* ======================================================================== */
/* input scripts (verification against DOSBox-X, docs/VERIFY.md)             */
/* ======================================================================== */
/*
 * A script is a text file of timed events.  The SAME file drives the
 * original in DOSBox-X (tools/verify/dbx_replay.py + tools/verify/hook.S),
 * so the delivery rule is defined on what both programs do, not on host
 * time:
 *
 *   At every input read of the game (pc_set_input_hook: kbhit, getch,
 *   bioskey 0/1 - also each wake-up while they block - and INT 33h AX=3),
 *   every event whose time is <= the game's own ms clock DGROUP:E74A
 *   (NKONTROL, +10 per 100 Hz tick) is applied, in file order.
 *
 * Synchronising on the game's input reads and its own clock (instead of
 * host time or frame counts) makes the moment of delivery a game-visible
 * state that both runs reach, whatever their speed: when the game waits for
 * input, both see the event at the first read after the same tick.
 *
 * Read-count sync (+n events): a read is counted at the entry of kbhit,
 * getch, bioskey and the mouse poll (not the wake-ups of a blocking read);
 * +n is due n counted reads after the read that applied the previous
 * event, or at once at a wake-up of a blocking read.  Loops that poll once
 * per pass then make the same number of passes on both sides (chargen calls
 * random(1000) once per pass), whatever their speed.
 *
 *   # comment
 *   <ms> key <hex word>          BIOS key word scan<<8|ASCII (e.g. 1C0D Enter)
 *   <ms> mouse <x> <y> <b>       driver coordinates (x 0..639 in 320-pixel
 *                                modes), set like INT 33h AX=4; buttons b
 *   <ms> buttons <b>
 *   <ms> dump <name>             <out>/<name>.{vga,vram,dg,txt}
 *   <ms> quit
 *   <ms> sync <addr> <val>       nothing; due only when also DGROUP word
 *                                <addr> == <val> (hex): an anchor at a known
 *                                game state for the +n events after it
 *   <ms> syncne <addr> <val>     the same, for DGROUP word <addr> != <val>
 *   <ms> srand <hex>             set the RTL random state (DGROUP:94C4, 32 bit):
 *                                re-aligns rand() after clock-timed calls
 *   <ms> poke <addr> <b> [<b>..] write bytes (hex) to DGROUP:<addr> (hex): resets
 *                                clock-timed state on both sides at a checkpoint
 *   +<n> <event>                 n counted input reads after the previous
 *                                event (read-count sync, below)
 *   @<n> <event>                 n ms of the game clock after the read that
 *                                applied the previous event (clock-relative:
 *                                after an anchor whose clock differs between
 *                                the runs)
 *   dumpevery <from> <to> <step> <prefix>   dump <prefix>_<ms> every step ms
 *   seed <n>                     srand() seed at start-up (time() override)
 *
 * Dump files (identical layout from both sides, tools/verify/vcompare.py):
 *   .vga   829 bytes: misc, seq 0-4, gc 0-8, crtc 0-24, attr 0-20, DAC 768
 *   .vram  chain-4 modes: the 64 KB A000 window as the CPU reads it;
 *          otherwise the 4 planes (4 x 64 KB)
 *   .dg    DGROUP (64 KB)
 *   .txt   clock and event index
 * In script mode time is virtual (pc_set_virtual_time) and keyboard/mouse
 * events of the window are ignored.
 */
enum { EV_KEY = 1, EV_MOUSE = 2, EV_MARK = 3, EV_BUTTONS = 4, EV_SYNC = 5, EV_SYNCNE = 6, EV_SRAND = 7,
       EV_POKE = 8, EV_KBD = 9 };
typedef struct ScriptEv { uint32_t t; int rel, type; int a, b, c; char name[64]; } ScriptEv;
static ScriptEv *sc_ev;
static int       sc_n, sc_next;
static char      sc_out[1024] = "out/verify/port";
static int32_t   sc_seed = -1;
static uint32_t  sc_reads, sc_last_read;     /* counted input reads; read of the last event */
static uint32_t  sc_last_clock;              /* game clock at the read of the last event */
static int       sc_rel;                     /* sc_add: 1 read-count relative (+n), 2 clock
                                                relative (@n) */

static void sc_add(uint32_t t, int type, int a, int b, int c, const char *name)
{
    sc_ev = realloc(sc_ev, (size_t)(sc_n + 1) * sizeof *sc_ev);
    ScriptEv *e = &sc_ev[sc_n++];
    e->t = t; e->rel = sc_rel; e->type = type; e->a = a; e->b = b; e->c = c;
    snprintf(e->name, sizeof e->name, "%s", name ? name : "");
}

static int script_load(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        SDL_Log("script %s: cannot open", path);
        return -1;
    }
    char line[512];
    int ln = 0;
    while (fgets(line, sizeof line, f)) {
        ln++;
        char *h = strchr(line, '#');
        if (h) *h = 0;
        char w1[64] = "", w2[64] = "", w3[64] = "", w4[64] = "", w5[64] = "";
        int n = sscanf(line, "%63s %63s %63s %63s %63s", w1, w2, w3, w4, w5);
        sc_rel = 0;
        if (n <= 0)
            continue;
        if (strcmp(w1, "seed") == 0 && n >= 2) { sc_seed = (int32_t)strtol(w2, NULL, 0); continue; }
        if (strcmp(w1, "dumpevery") == 0 && n >= 5) {
            uint32_t a = (uint32_t)strtoul(w2, NULL, 0), b = (uint32_t)strtoul(w3, NULL, 0);
            uint32_t st = (uint32_t)strtoul(w4, NULL, 0);
            for (uint32_t t = a; st && t <= b; t += st) {
                char nm[64];
                snprintf(nm, sizeof nm, "%s_%07u", w5, (unsigned)t);
                sc_add(t, EV_MARK, 0, 0, 0, nm);
            }
            continue;
        }
        sc_rel = w1[0] == '+' ? 1 : w1[0] == '@' ? 2 : 0;
        uint32_t t = (uint32_t)strtoul(w1 + (sc_rel != 0), NULL, 0);
        if (strcmp(w2, "key") == 0 && n >= 3)           sc_add(t, EV_KEY, (int)strtol(w3, NULL, 16), 0, 0, NULL);
        else if (strcmp(w2, "mouse") == 0 && n >= 5)    sc_add(t, EV_MOUSE, atoi(w3), atoi(w4), atoi(w5), NULL);
        else if (strcmp(w2, "buttons") == 0 && n >= 3) sc_add(t, EV_BUTTONS, atoi(w3), 0, 0, NULL);
        else if (strcmp(w2, "dump") == 0 && n >= 3)     sc_add(t, EV_MARK, 0, 0, 0, w3);
        else if (strcmp(w2, "quit") == 0)               sc_add(t, EV_MARK, 1, 0, 0, "quit");
        else if (strcmp(w2, "sync") == 0 && n >= 4)
            sc_add(t, EV_SYNC, (int)strtol(w3, NULL, 16), (int)strtol(w4, NULL, 16), 0, NULL);
        else if (strcmp(w2, "srand") == 0 && n >= 3) {
            uint32_t v = (uint32_t)strtoul(w3, NULL, 16);
            sc_add(t, EV_SRAND, (int)(v & 0xFFFF), (int)(v >> 16), 0, NULL);
        }
        else if (strcmp(w2, "poke") == 0 && n >= 4) {
            /* split into byte/word events like dbx_replay.py (hook.S type 8) */
            char *p = strstr(line, "poke") + 4;
            uint16_t addr = (uint16_t)strtoul(p, &p, 16);
            uint8_t data[256];
            int nb = 0;
            for (;;) {
                char *q;
                unsigned long v = strtoul(p, &q, 16);
                if (q == p || nb >= (int)sizeof data) break;
                data[nb++] = (uint8_t)v;
                p = q;
            }
            for (int i = 0; i < nb; i += 2) {
                int len = nb - i >= 2 ? 2 : 1;
                sc_add(i ? 0 : t, EV_POKE, (uint16_t)(addr + i), data[i] | (len > 1 ? data[i + 1] << 8 : 0),
                       len, NULL);
            }
        }
        else if (strcmp(w2, "syncne") == 0 && n >= 4)
            sc_add(t, EV_SYNCNE, (int)strtol(w3, NULL, 16), (int)strtol(w4, NULL, 16), 0, NULL);
        else if (strcmp(w2, "kbd") == 0 && n >= 3) {
            /* scan-code bytes, one event each (dbx_replay.py: hook.S type 9) */
            char *p = strstr(line, "kbd") + 3;
            int i = 0;
            for (;;) {
                char *q;
                unsigned long v = strtoul(p, &q, 16);
                if (q == p) break;
                sc_add(i++ ? 0 : t, EV_KBD, (int)(v & 0xFF), 0, 0, NULL);
                p = q;
            }
        }
        else {
            sc_rel = 0;
            SDL_Log("script %s:%d: cannot parse", path, ln);
            fclose(f);
            return -1;
        }
    }
    sc_rel = 0;
    fclose(f);
    /* events are applied in file order: a later line with an earlier time
       becomes due together with the line before it (as in hook.S) */
    return 0;
}

static void sc_write(const char *name, const char *ext, const void *p, size_t n)
{
    char path[1200];
    snprintf(path, sizeof path, "%s/%s.%s", sc_out, name, ext);
    FILE *f = fopen(path, "wb");
    if (!f) { SDL_Log("dump %s: cannot write", path); return; }
    fwrite(p, 1, n, f);
    fclose(f);
}

static uint8_t vgab[829];
static uint8_t vram[4 * 0x10000];

/* VGA registers + DAC -> vgab, video memory -> vram; returns the vram size */
static size_t sc_capture(void)
{
    uint8_t *q = vgab;
    pc_vga_sync();
    *q++ = pc_vga.misc_output;
    for (int i = 0; i < 5; i++)  *q++ = pc_vga.seq[i];
    for (int i = 0; i < 9; i++)  *q++ = pc_vga.gc[i];
    for (int i = 0; i < 25; i++) *q++ = pc_vga.crtc[i];
    for (int i = 0; i < 21; i++) *q++ = pc_vga.attr[i];
    memcpy(q, pc_vga.dac, 768);
    if (pc_vga.seq[4] & 0x08) {                         /* chain-4: CPU view of A000 */
        uint8_t latch[4];
        memcpy(latch, pc_vga.latch, 4);
        for (uint32_t i = 0; i < 0x10000; i++)
            vram[i] = vga_mem_read8(&pc_vga, 0xA0000u + i);
        memcpy(pc_vga.latch, latch, 4);
        return 0x10000;
    }
    for (int p = 0; p < 4; p++)
        memcpy(vram + p * 0x10000, pc_vga.plane[p], 0x10000);
    return 4 * 0x10000;
}

/* --frame-log <dir>: at the first input read of every 10 ms tick, the video
   state (registers, DAC, video memory) is hashed and every state not seen
   before is written as <dir>/fr_<n>_<clock>.{vga,vram}.  The original is
   sampled less densely (dbx_replay.py); tools/verify/frames_match.py looks
   its samples up in this set, so the comparison does not depend on how
   fast either program draws. */
static char     fl_dir[1024];
static uint64_t fl_seen[1 << 16];
static int      fl_count;

static void frame_log(uint32_t clock)
{
    static uint32_t last = 0xFFFFFFFFu;
    if (clock == last)
        return;
    last = clock;
    size_t n = sc_capture();
    uint64_t h = 0xCBF29CE484222325ull;
    for (size_t i = 0; i < sizeof vgab; i++) { h ^= vgab[i]; h *= 0x100000001B3ull; }
    for (size_t i = 0; i < n; i++)           { h ^= vram[i]; h *= 0x100000001B3ull; }
    if (!h) h = 1;
    size_t k = (size_t)(h & 0xFFFF);
    while (fl_seen[k] && fl_seen[k] != h)
        k = (k + 1) & 0xFFFF;
    if (fl_seen[k] == h || fl_count >= 0xF000)
        return;
    fl_seen[k] = h;
    char name[96], save[1024];
    snprintf(name, sizeof name, "fr_%05d_%07u", fl_count++, (unsigned)clock);
    snprintf(save, sizeof save, "%s", sc_out);
    snprintf(sc_out, sizeof sc_out, "%s", fl_dir);
    sc_write(name, "vga", vgab, sizeof vgab);
    sc_write(name, "vram", vram, n);
    snprintf(sc_out, sizeof sc_out, "%s", save);
}

static void sc_dump(const ScriptEv *e, int idx)
{
    size_t vn = sc_capture();
    sc_write(e->name, "vga", vgab, sizeof vgab);
    sc_write(e->name, "vram", vram, vn);
    sc_write(e->name, "dg", dg, 0x10000);
    char txt[200];
    int n = snprintf(txt, sizeof txt, "clock %u\nevent %d\nscript_time %u\nreads %u\n",
                     (unsigned)DG32(0xE74A), idx, (unsigned)e->t, (unsigned)sc_reads);
    sc_write(e->name, "txt", txt, (size_t)n);
}

/* `kbd` events: the part of the BIOS INT 09h the verification scripts use,
   after the game's INT 09h hook has seen the byte (as on the PC, where the
   hook reads port 60h first): E0h prefix, Ctrl (1Dh) and Alt (38h) make /
   break -> BDA 0040:0017 bits 2/3, Ctrl+ScrollLock (46h, also E0 46h =
   Ctrl+Pause) -> Ctrl-Break.  Ctrl+Alt+Del that the game does not swallow
   would reboot: not modelled (logged); other keys likewise. */
static void sc_kbd(uint8_t b)
{
    static int e0;
    uint8_t f = pc_shift_flags();
    if (pc_int09(b)) {
        e0 = 0;
        return;
    }
    if (b == 0xE0) {
        e0 = 1;
        return;
    }
    switch (b & 0x7F) {
    case 0x1D: f = (uint8_t)((b & 0x80) ? f & ~0x04 : f | 0x04); pc_set_shift_flags(f); break;
    case 0x38: f = (uint8_t)((b & 0x80) ? f & ~0x08 : f | 0x08); pc_set_shift_flags(f); break;
    case 0x46:
        if (!(b & 0x80) && (f & 0x04))
            pc_ctrl_break();
        break;
    default:
        if (!(b & 0x80))
            SDL_Log("script: kbd %s%02X not modelled", e0 ? "E0 " : "", b);
        break;
    }
    e0 = 0;
}

/* INT 23h about to be issued: the `int23` checkpoint (dbx_replay.py: the
   hook's INT 23h wrapper) */
static void sc_int23(void)
{
    ScriptEv e;
    memset(&e, 0, sizeof e);
    snprintf(e.name, sizeof e.name, "int23");
    sc_dump(&e, 0xFFFE);
}

static int sc_due(const ScriptEv *e, uint32_t clock, int wake)
{
    if (e->type == EV_SYNC && DG16((uint16_t)e->a) != (uint16_t)e->b)
        return 0;                       /* sync: also wait for the DGROUP word */
    if (e->type == EV_SYNCNE && DG16((uint16_t)e->a) == (uint16_t)e->b)
        return 0;
    if (!e->rel)
        return e->t <= clock;
    if (e->rel == 2)                    /* @n: n ms after the previous event */
        return clock - sc_last_clock >= e->t;
    /* +n: n counted reads after the previous event; a blocking read that
       waits for a key takes the next relative event at once */
    return wake || sc_reads - sc_last_read >= e->t;
}

static void script_input_hook(int wake)
{
    static int busy;
    if (busy)
        return;
    busy = 1;
    if (!wake)
        sc_reads++;
    uint32_t clock = DG32(0xE74A);
    if (fl_dir[0])
        frame_log(clock);
    while (sc_next < sc_n && sc_due(&sc_ev[sc_next], clock, wake)) {
        const ScriptEv *e = &sc_ev[sc_next++];
        sc_last_read = sc_reads;
        sc_last_clock = clock;
        switch (e->type) {
        case EV_KEY:
            pc_key_push((uint16_t)e->a);
            break;
        case EV_MOUSE: {
            /* like INT 33h AX=4 (clamped to the driver range), buttons from now on */
            PcMouseRegs r = { 4, 0, (uint16_t)e->a, (uint16_t)e->b };
            pc_int33(&r);
            r.ax = 3;
            pc_int33(&r);            /* the nested hook call returns at once (busy) */
            pc_mouse_host((int16_t)r.cx, (int16_t)r.dx, (uint16_t)e->c);
            break;
        }
        case EV_BUTTONS: {
            PcMouseRegs r = { 3, 0, 0, 0 };
            pc_int33(&r);
            pc_mouse_host((int16_t)r.cx, (int16_t)r.dx, (uint16_t)e->a);
            break;
        }
        case EV_SRAND:                  /* the RTL's random state (srand) */
            DG32(0x94C4) = (uint32_t)(uint16_t)e->a | (uint32_t)(uint16_t)e->b << 16;
            break;
        case EV_POKE:                   /* DGROUP bytes (clock-timed state at a checkpoint) */
            if (e->c == 1) DG8((uint16_t)e->a) = (uint8_t)e->b;
            else           DG16((uint16_t)e->a) = (uint16_t)e->b;
            break;
        case EV_KBD:
            sc_kbd((uint8_t)e->a);
            break;
        case EV_SYNC:                   /* only an anchor for the +n events after it */
        case EV_SYNCNE:
            break;
        case EV_MARK:
            if (e->a == 1) {
                SDL_Log("script: quit at clock %u", (unsigned)clock);
                audio_sdl_close();
                cdrom_close();
                SDL_Quit();
                exit(0);
            }
            sc_dump(e, sc_next - 1);
            break;
        }
    }
    busy = 0;
}

/* ======================================================================== */
/* test modes                                                               */
/* ======================================================================== */

static int cd_play_test(const LaleOptions *o)
{
    const CdromTrack *t = cdrom_track(o->cd_play_track);
    if (!t || !t->audio) {
        SDL_Log("track %d is not an audio track", o->cd_play_track);
        return 1;
    }
    int16_t ok = o5737_cd_play_track((uint8_t)o->cd_play_track, -1);
    SDL_Log("play_track(%d) -> %d: start %u, length %u sectors (%.2f s)", o->cd_play_track, ok,
            (unsigned)cd5737_start(), (unsigned)cd5737_length(), cd5737_length() / 75.0);
    Uint64 t0 = SDL_GetTicks(), next_log = 0;
    while (SDL_GetTicks() - t0 < (Uint64)o->cd_play_seconds * 1000) {
        pc_poll();                                /* emulated time drives the CD audio clock */
        audio_sdl_pump();
        /* ui_cd_music: loop the track.  The poll reports 'not playing' until
           the drive position has moved past the start, so (like the menus,
           which only check while idle) give a fresh play a moment first. */
        static Uint64 started;
        if (!started)
            started = SDL_GetTicks();
        if (SDL_GetTicks() - started > 500 && o5737_cd_play() == 0) {
            SDL_Log("track ended -> play_track(0) = %d", o5737_cd_play_track(0, -1));
            started = SDL_GetTicks();
        }
        if (SDL_GetTicks() - t0 >= next_log) {
            uint32_t p = cdrom_position();
            SDL_Log("  pos LBA %u (track time %.1f s)", (unsigned)p, (p - cd5737_start()) / 75.0);
            next_log += 2000;
        }
        SDL_Delay(10);
    }
    o5737_cd_stop();
    return 0;
}

/* Mode 13h colour bars + a mouse-driven box; Esc quits.  Exercises the
   whole host path: pc_poll -> pump, retrace -> present, keyboard, mouse. */
static int vga_test(void)
{
    vga_bios_set_mode(&pc_vga, 0x13);
    for (int i = 0; i < 64000; i++)
        pc_a000[i] = (uint8_t)((i % 320) * 256 / 320);
    PcMouseRegs r = { 0, 0, 0, 0 };
    pc_int33(&r);
    for (;;) {
        pc_wait_retrace();
        r.ax = 3;
        pc_int33(&r);
        int mx = r.cx / 2, my = r.dx;
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++)
                if (my + y < 200 && mx + x < 320)
                    pc_a000[(my + y) * 320 + mx + x] = r.bx ? 4 : 15;
        while (pc_kbhit()) {
            int16_t c = pc_getch();
            SDL_Log("key %d", c);
            if (c == 27)
                return 0;
        }
    }
}

/* ======================================================================== */
/* the game                                                                 */
/* ======================================================================== */

#ifdef __APPLE__
/* ~/Library/Application Support/Lale64/lale.ini: the options file of the app
   (read before ./lale.ini). */
static const char *mac_ini_path(void)
{
    static char path[1100];
    if (!path[0]) {
        char *pref = SDL_GetPrefPath(NULL, "Lale64");
        if (pref) {
            snprintf(path, sizeof path, "%slale.ini", pref);
            SDL_free(pref);
        }
    }
    return path;
}
#endif

static GameSetup game_setup;

/* The game's files, its edition and its CD drive (setup.h).  0 = ready. */
static int prepare_game(const LaleOptions *o)
{
    if (*o->game_dir) {
        /* development and the test tools: the DOS game's folder as it is */
        int want_cd = o->cd_music || !strcmp(o->edition, "cd");
        char path[1100], err[512] = "";
        SDL_PathInfo info;
        snprintf(path, sizeof path, "%s/3D.CKD", o->game_dir);
        if (!want_cd && !(SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_FILE)) {
            SDL_Log("no game files (3D.CKD) in '%s'", o->game_dir);
            return 1;
        }
        pc_set_game_dir(o->game_dir);
        if (want_cd) {
            size_t n = strlen(o->cd_image);
            int is_chd = n > 4 && SDL_strcasecmp(o->cd_image + n - 4, ".chd") == 0;
            if (is_chd ? chd_mount(o->cd_image, err, sizeof err) : cdrom_open(o->cd_image)) {
                SDL_Log("edition cd: cannot use the image '%s': %s", o->cd_image, is_chd ? err : cdrom_error());
                return 1;
            }
            mscdex_set_present(1);
            pc_use_disc_files(1);
            g_edition = EDITION_CD;
        }
        SDL_Log("game folder: %s, edition: %s", o->game_dir, want_cd ? "1996 CD" : "2005");
        return 0;
    }
    if (host_open_window())
        return 1;
    if (setup_game(o, window, renderer, !script_mode && !frames_left, &game_setup) != 0)
        return 1;
    pc_set_dirs(game_setup.data, game_setup.save);
    if (!script_mode && (hd_files = hdvideo_scan(game_setup.video)) > 0) {
        hd_on = strcmp(o->video, "original") != 0;
        pc_set_host_video(host_video_event);
        SDL_Log("4K videos: %d files in %s; Ctrl+V switches (now: %s)", hd_files, game_setup.video,
                hd_on ? "4K" : "original");
    }
    if (game_setup.disc) {
        pc_use_disc_files(1);
        mscdex_set_present(game_setup.edition == EDITION_CD);
    }
    g_edition = (int16_t)game_setup.edition;
    return 0;
}

static int run_game(const LaleOptions *o)
{
    if (host_open_window())
        return 1;
    /* IST.EXE's command line: only the number of arguments counts */
    static char arg0[] = "IST.EXE", argx[] = "x";
    char *iargv[8] = { arg0 };
    int iargc = 1 + (o->ist_args < 0 ? 0 : o->ist_args > 6 ? 6 : o->ist_args);
    for (int i = 1; i < iargc; i++)
        iargv[i] = argx;
    return ist_main(iargc, iargv);          /* never returns: o559A_fatal exits */
}

/* ======================================================================== */

int main(int argc, char **argv)
{
    LaleOptions opt;
    options_defaults(&opt);
#ifdef __APPLE__
    options_load_ini(&opt, mac_ini_path());             /* the app's own, then ./lale.ini */
#endif
    options_load_ini(&opt, "lale.ini");
    int vgat = 0;
    for (int i = 1; i < argc; i++)
        if (strcmp(argv[i], "--vga-test") == 0) {
            vgat = 1;
            memmove(&argv[i], &argv[i + 1], (size_t)(argc - i) * sizeof *argv);
            argc--;
            break;
        }
    /* input-script mode switches (host only, not lale.ini options) */
    const char *script_path = NULL;
    uint32_t step_us = 100;
    int32_t cmd_seed = -1;
    for (int i = 1; i + 1 < argc;) {
        const char *k = argv[i], *v = argv[i + 1];
        if (strcmp(k, "--script") == 0)       script_path = v;
        else if (strcmp(k, "--out") == 0)     snprintf(sc_out, sizeof sc_out, "%s", v);
        else if (strcmp(k, "--seed") == 0)    cmd_seed = (int32_t)strtol(v, NULL, 0);
        else if (strcmp(k, "--step-us") == 0) step_us = (uint32_t)strtoul(v, NULL, 0);
        else if (strcmp(k, "--frame-log") == 0) snprintf(fl_dir, sizeof fl_dir, "%s", v);
        else if (strcmp(k, "--video-toggle-at") == 0) toggle_at = atoi(v);
        else { i++; continue; }
        memmove(&argv[i], &argv[i + 2], (size_t)(argc - i - 1) * sizeof *argv);
        argc -= 2;
    }
    if (options_parse_args(&opt, argc, argv) != 0)
        return 2;

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    pc_reset();
    /* c0 start-up: DGROUP from the image, the far heap as it is at main() */
    dg_reset();
    bc_farheap_reset();
    bc_setupio();                       /* #pragma startup of the RTL */
    ist_ovr_prepare();                  /* ... and of the overlay manager */
    if (script_path) {
        if (script_load(script_path) != 0)
            return 2;
        if (cmd_seed >= 0)
            sc_seed = cmd_seed;               /* --seed overrides the script */
        SDL_CreateDirectory(sc_out);
        if (fl_dir[0])
            SDL_CreateDirectory(fl_dir);
        script_mode = 1;
        pc_set_virtual_time(1, step_us);
        pc_set_input_hook(script_input_hook);
        pc_set_int23_observer(sc_int23);
        if (sc_seed >= 0)
            ist_time_override = sc_seed;
        SDL_Log("script %s: %d events, out %s, seed %d, step %u us", script_path, sc_n, sc_out,
                (int)sc_seed, (unsigned)step_us);
    }

    int rc = 0;
    frames_left = opt.frames;
    screenshot_path = opt.screenshot;
    if (!opt.cd_play_track && !vgat) {
        if (prepare_game(&opt) != 0) {
            cdrom_close();
            SDL_Quit();
            return 1;
        }
    } else if (opt.cd_music) {
        if (cd5737_port_enable(opt.cd_image) == 0) {
            SDL_Log("CD music on: %s (%d tracks, drive %c:)", opt.cd_image,
                    cdrom_track_count(), 'A' + CDROM_DRIVE_INDEX);
        } else {
            SDL_Log("CD music off: cannot use image '%s': %s", opt.cd_image, cdrom_error());
        }
    }
    audio_sdl_open(cd5737_active() || g_edition == EDITION_CD);
    if (opt.cd_play_track) {
        if (!cd5737_active()) {
            SDL_Log("--cd-play needs --cd-music and a usable --cd-image");
            rc = 1;
        } else {
            rc = cd_play_test(&opt);
        }
    } else if (vgat) {
        rc = host_open_window() ? 1 : vga_test();
    } else {
        rc = run_game(&opt);
    }
    audio_sdl_close();
    cdrom_close();
    SDL_Quit();
    return rc;
}
