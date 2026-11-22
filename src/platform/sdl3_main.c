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
 *                   (vga_render) -> 8-bit RGB texture, aspect-correct 4:3.
 *
 * Default action: the game, ist_main() (src/game/istanbul.c), after the
 * set-up istanbul.h asks of the host (pc_reset, game folder, dg_reset +
 * bc_farheap_reset, CD music / audio, pump/present hooks).  The game folder
 * holds the original files (3D.CKD, SES.CFG ...): --game-dir / game_dir, else
 * the working directory if it has them, else ./orig.
 *
 * Options: lale.ini + command line (src/platform/options.c).
 *     Lale64 [--game-dir <dir>] [--ist-args n]          the game
 *     Lale64 ... --frames <n> [--screenshot f.bmp]      quit after n frames (tests)
 *     Lale64 --script s.txt --out <dir> [--seed n] [--step-us n] [--frame-log <dir>]
 *                                           input script / verification mode
 *                                           (docs/VERIFY.md, "input scripts" below)
 * Test modes:
 *     Lale64 --cd-music --cd-image <disc.cue> --cd-play 2 [--seconds 15]
 *     Lale64 --vga-test                       VGA/host smoke test (palette, mouse)
 */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <stdlib.h>
#include <string.h>

#include "engine/dgroup.h"
#include "game/cd5737.h"
#include "game/istanbul.h"
#include "platform/about.h"
#include "platform/app_icon.h"
#include "platform/audio_sdl.h"
#include "platform/cdrom.h"
#include "platform/options.h"
#include "platform/pc.h"
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
                              ((m & SDL_KMOD_CTRL) ? 4 : 0) | ((m & SDL_KMOD_ALT) ? 8 : 0));
    pc_set_shift_flags(shift);
    if (!k->down)
        return;                                         /* release: shift flags only */
    /* BIOS INT 09h: Ctrl-Break is Ctrl+Pause on the enhanced keyboard and
       Ctrl+ScrollLock (scan 46h) on every keyboard */
    if ((m & SDL_KMOD_CTRL) && !k->repeat &&
        (k->scancode == SDL_SCANCODE_PAUSE || k->scancode == SDL_SCANCODE_SCROLLLOCK ||
         k->scancode == SDL_SCANCODE_CANCEL)) {
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
        if (m & SDL_KMOD_ALT)        s = (uint8_t)(sc + 0x2D);
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
    if (m & SDL_KMOD_ALT) {                             /* Alt+key: ASCII 0 */
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
            float lx, ly;
            SDL_RenderCoordinatesFromWindow(renderer, x, y, &lx, &ly);
            /* logical presentation is frame_w x frame_h; the driver's virtual
               screen is 640 wide in 320-pixel modes (x is doubled) */
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
        /* 4:3 output whatever the pixel grid (320x200 has non-square pixels) */
        SDL_SetRenderLogicalPresentation(renderer, w, h, SDL_LOGICAL_PRESENTATION_STRETCH);
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
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderTexture(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);
    if (frames_left > 0 && --frames_left == 0) {
        if (screenshot_path && *screenshot_path) {
            SDL_Surface *shot = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_XRGB8888, rgba, w * 4);
            if (!shot || !SDL_SaveBMP(shot, screenshot_path))
                SDL_Log("screenshot %s: %s", screenshot_path, SDL_GetError());
            else
                SDL_Log("screenshot %s (%dx%d)", screenshot_path, w, h);
            SDL_DestroySurface(shot);
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
    /* "İstanbul Efsaneleri - Lale Savaşçıları" in UTF-8 (SDL's encoding), written
     * with escapes so the source's own encoding cannot change it. */
    static const char window_title[] =
        "\xC4\xB0stanbul Efsaneleri - Lale Sava\xC5\x9F\xC3\xA7\xC4\xB1lar\xC4\xB1";
    if (!SDL_CreateWindowAndRenderer(window_title, 960, 720,
                                     SDL_WINDOW_RESIZABLE, &window, &renderer)) {
        SDL_Log("window: %s", SDL_GetError());
        return -1;
    }
    /* 640x480 window area with 4:3 aspect; letterboxed when resized */
    SDL_SetWindowAspectRatio(window, 4.0f / 3.0f, 4.0f / 3.0f);
    set_window_icon(window);
    about_install(window);               /* system menu: About... */
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
       EV_POKE = 8 };
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

static int has_game_files(const char *dir)
{
    char path[1100];
    snprintf(path, sizeof path, "%s/3D.CKD", dir);
    SDL_PathInfo info;
    return SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_FILE;
}

static int run_game(const LaleOptions *o)
{
    const char *dir = o->game_dir;
    if (!*dir)
        dir = has_game_files(".") ? "." : "orig";
    if (!has_game_files(dir)) {
        SDL_Log("no game files (3D.CKD) in '%s': use --game-dir <folder of the original game>", dir);
        return 1;
    }
    SDL_Log("game folder: %s", dir);
    pc_set_game_dir(dir);
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
        if (sc_seed >= 0)
            ist_time_override = sc_seed;
        SDL_Log("script %s: %d events, out %s, seed %d, step %u us", script_path, sc_n, sc_out,
                (int)sc_seed, (unsigned)step_us);
    }

    int rc = 0;
    if (opt.cd_music) {
        if (cd5737_port_enable(opt.cd_image) == 0) {
            SDL_Log("CD music on: %s (%d tracks, drive %c:)", opt.cd_image,
                    cdrom_track_count(), 'A' + CDROM_DRIVE_INDEX);
        } else {
            SDL_Log("CD music off: cannot use image '%s': %s", opt.cd_image, cdrom_error());
        }
    }
    audio_sdl_open(cd5737_active());
    if (opt.cd_play_track) {
        if (!cd5737_active()) {
            SDL_Log("--cd-play needs --cd-music and a usable --cd-image");
            rc = 1;
        } else {
            rc = cd_play_test(&opt);
        }
    } else if (vgat) {
        frames_left = opt.frames;
        screenshot_path = opt.screenshot;
        rc = host_open_window() ? 1 : vga_test();
    } else {
        frames_left = opt.frames;
        screenshot_path = opt.screenshot;
        rc = run_game(&opt);
    }
    audio_sdl_close();
    cdrom_close();
    SDL_Quit();
    return rc;
}
