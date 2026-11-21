/*
 * demolar.c - DEMOLAR.L, decompiled from IST.EXE ovl3C2D (3C2D:0000-3A28).
 * See demolar.h for the overview.
 *
 * Port conventions used throughout:
 *  - kbhit/getch/bioskey -> ui_kbhit/ui_getch/ui_bioskey.
 *  - every busy loop on the 100 Hz clock (g_clock_ms) calls pc_poll() once
 *    per pass, at the loop head; the tests hook the same original
 *    addresses (tests/py/test_demolar.py, POLL points).
 *  - in 3DAh -> pc_in_3da.
 *  - the "stop and free the music" sequence the original inlines at every
 *    site (a source macro) is music_stop_free(); the random channel
 *    panning likewise music_pan_random().
 *  - far code pointers called through DGROUP (964C idle callback, 9650)
 *    go through ui_far_fn.
 *  - o559A_fatal returns in tests (o559A_test_mode); the code after it is
 *    what the original would execute if fatal returned.
 */
#include "game/demolar.h"

#include <stdio.h>
#include <string.h>

#include "dsmi/amp.h"
#include "dsmi/cdi.h"
#include "dsmi/emsheap.h"
#include "dsmi/memalloc.h"
#include "engine/blit386.h"
#include "engine/fatal.h"
#include "engine/fileman.h"
#include "engine/grtext.h"
#include "engine/intrface.h"
#include "engine/memman.h"
#include "engine/ngraphs.h"
#include "engine/nkontrol.h"
#include "engine/nses.h"
#include "engine/ographs.h"
#include "engine/oses.h"
#include "engine/systems.h"
#include "engine/vfs.h"
#include "game/cd5737.h"
#include "game/gfl.h"
#include "platform/pc.h"
#include "rtl/bc_rand.h"

/* ---- callees in other game modules -------------------------------------- */
#if __has_include("game/s3d.h")
#include "game/s3d.h"
#else
/* provided by 3DSYS (WP16) */
uint16_t s3d_save_load_menu(int16_t mode);                    /* 303F:7F2F */
int16_t  s3d_advance_clock(int16_t minutes);                  /* 303F:39FD */
uint16_t s3d_calc_daylight(void);                             /* 303F:3BF3 */
void     s3d_draw_status_line(int16_t a, int16_t b);          /* 303F:4D22 */
#endif
#if __has_include("game/frpos.h")
#include "game/frpos.h"
#else
/* provided by FRPOS (WP15) */
uint8_t fr_flag_test(int16_t n);                              /* 3FD1:4AAC */
void    fr_show_icon(uint8_t icon, uint8_t wait);             /* 3FD1:4B57 */
#endif
#include "game/q3dview.h"   /* q3d_free_view_buffers (2700:0000, thunk 1662:0066) */

AmpModule *g_music_module;          /* 16E0:BB75 */

#define FILE_DEMO   DGSTR(0x4B46)   /* "CODE\DEMOLAR.L" (one copy per site, see the calls) */
#define SURF_REF(n) ((uint16_t)(0xE29F + 8 * (n)))  /* far MmHandle * of surface slot n */
#define SURF(n)     mm_ref(SURF_REF(n))
#define SURF_SET(n) (DG16(SURF_REF(n)) | DG16(SURF_REF(n) + 2))
#define WIN_CUR     0x962C          /* current INTRFACE window record (48h bytes) */
#define RIX_TABLE   0xE52F          /* far MmHandle * of the .RIX blend table */
#define RIX_COPY    0xE56C          /* far MmHandle * of demo_show_picture's copy */

/* The inlined "stop the module and free it" sequence (19F9, 1074, ...). */
static void music_stop_free(void)
{
    if (g_sound_on) {
        ampStopModule();
        if (g_music_module) {
            ampFreeModule(g_music_module);
            MemFree(g_music_module);
            g_music_module = 0;
        }
    }
}

/* The inlined random stereo spread (0B37, 125D, 20AD): channel pairs get
   +-(random(21) + random(4)). */
static void music_pan_random(void)
{
    int16_t n = g_music_module->channelCount;
    for (int16_t i = 0; i < n; i += 2) {
        int16_t r = bc_random(21);
        cdiSetPanning(i, (int16_t)(r + bc_random(4)));
        cdiSetPanning((int16_t)(i + 1), (int16_t)(-r - bc_random(4)));
    }
}

static void win_record_copy(uint16_t dst, uint16_t src)   /* F_SCOPY 48h */
{
    memmove(DGP(dst), DGP(src), 0x48);
}

static void wait_retrace(void)
{
    while ((pc_in_3da() & 8) != 8)
        ;
    while (pc_in_3da() & 8)
        ;
}

/*
 * demo_wait_key_or_delay - wait up to ms milliseconds for a key (returns
 * getch()) or a mouse button (returns 20h); 0 on timeout.  The first two
 * arguments are not used.
 * Original: IST.EXE 3C2D:0000 (DEMOLAR.L), far cdecl.
 * Port: pc_poll() at the loop head (002C).
 * Verified: tests/py/test_demolar.py (test_wait).
 */
int16_t demo_wait_key_or_delay(uint8_t unused1, uint8_t unused2, uint16_t ms)
{
    int16_t t;
    uint32_t deadline = g_clock_ms + ms;
    (void)unused1;
    (void)unused2;
    for (;;) {
        pc_poll();
        if (ui_kbhit())
            return ui_getch();
        if (gr_mouse_read(&t, &t, &t))
            return 0x20;
        if (g_clock_ms >= deadline)
            return 0;
    }
}

/* the 16-bit deadline wait of demo_show_picture (03B0, 0438, 04A8) */
static void wait_clock16(uint16_t until)
{
    for (;;) {
        pc_poll();
        if ((uint16_t)g_clock_ms >= until)
            break;
    }
}

/*
 * demo_show_picture - cross-fade picture a into picture b (same size) on
 * the screen surface at dst_off, through 11 blended intermediate pictures
 * (gr_blend3 with a copy of the .RIX table): step 5 = blend(a, b), 7..10
 * walk towards b, 3..0 towards a, 6 and 4 in between.  Before the fade it
 * waits wait_ms for a key/click: Esc returns 1 at once (nothing drawn),
 * another key makes it return 2 after the fade; else 0.  Each picture
 * stays dur/11 ticks of the 16-bit clock (unsigned compare, so a wrap of
 * the low clock word ends the wait early).  a is not copied when it is
 * the screen record itself.  Conventional pictures are turned into EMS
 * for the duration and back afterwards.
 * Original: IST.EXE 3C2D:0086 (DEMOLAR.L), far cdecl.
 * Port: pc_poll() in the clock waits (see wait_clock16).
 * Verified: tests/py/test_demolar.py (test_show_picture).
 */
uint8_t demo_show_picture(MmHandle *a, MmHandle *b, uint16_t dur, uint8_t c, uint8_t d,
                          uint16_t wait_ms, uint16_t dst_off)
{
    MmHandle *step[11];
    MmHandle *tmp;
    const uint8_t *table;
    uint8_t b_conv = 0, a_conv = 0, res = 0;
    uint16_t div = dur / 11u, until;
    int16_t r;
    int8_t i;

    if (a->type == 0) {
        a_conv = 1;
        mm_change_type(a, 1);
    }
    if (b->type == 0) {
        b_conv = 1;
        mm_change_type(b, 1);
    } else if (a->size != b->size) {
        o559A_fatal(DGSTR(0x4B46), 0x67, 0, 0x68);
    }
    tmp = mm_alloc(a->size, 0);
    mm_ref_set(RIX_COPY, mm_duplicate(mm_ref(RIX_TABLE), 0));

    table = mm_conv_ptr(mm_ref(RIX_COPY));
    gr_blend3(a, b, tmp, table);
    step[5] = mm_duplicate(tmp, 1);
    for (i = 7; i < 11; i++) {
        gr_blend3(b, tmp, tmp, mm_conv_ptr(mm_ref(RIX_COPY)));
        step[i] = mm_duplicate(tmp, 1);
    }
    mm_copy_block(step[5], tmp);
    gr_blend3(step[7], tmp, tmp, mm_conv_ptr(mm_ref(RIX_COPY)));
    step[6] = mm_duplicate(tmp, 1);
    mm_copy_block(step[5], tmp);
    for (i = 3; i >= 0; i--) {
        gr_blend3(a, tmp, tmp, mm_conv_ptr(mm_ref(RIX_COPY)));
        step[i] = mm_duplicate(tmp, 1);
    }
    mm_copy_block(step[5], tmp);
    gr_blend3(step[3], tmp, tmp, mm_conv_ptr(mm_ref(RIX_COPY)));
    step[4] = mm_duplicate(tmp, 1);

    r = demo_wait_key_or_delay(c, d, wait_ms);
    if (r != 0) {
        if (r == 0x1B) {
            res = 1;
            goto done;
        }
        res = 2;
    }
    until = (uint16_t)((uint16_t)g_clock_ms + div);
    if (SURF(0) != a) {
        wait_retrace();
        mm_copy_range(a, SURF(0), 0, dst_off, a->size);
    }
    wait_clock16(until);
    for (i = 0; i < 11; i++) {
        until = (uint16_t)((uint16_t)g_clock_ms + div);
        wait_retrace();
        mm_copy_range(step[i], SURF(0), 0, dst_off, step[i]->size);
        wait_clock16(until);
    }
    until = (uint16_t)((uint16_t)g_clock_ms + div);
    wait_retrace();
    mm_copy_range(b, SURF(0), 0, dst_off, b->size);
    wait_clock16(until);

done:
    for (i = 0; i < 11; i++)
        mm_free(step[i], 0xA0, DGSTR(0x4B55));
    mm_free(tmp, 0xA1, DGSTR(0x4B64));
    mm_free(mm_ref(RIX_COPY), 0xA2, DGSTR(0x4B73));
    if (b_conv)
        mm_change_type(b, 0);
    if (a_conv)
        mm_change_type(a, 0);
    return res;
}

/*
 * demo_intro_menu - the start-up sequence and the main menu.
 *   arg (main: argc-1 the first time, C8h afterwards):
 *     > 2 and != C8h: return arg-2 at once (command-line shortcut);
 *     C8h: straight to the menu screen (acilisb.cmp);
 *     0: INTRO and SILICON videos, the two 640x480 mode-12h pictures
 *        (comp4a/comp4b, istb), the title (acilisa/acilisb with two
 *        cross-fades), then the menu;
 *     1: the videos, then the title (Esc in a video also gets there);
 *     2: the videos, then the plain menu screen (acilisb.cmp).
 *   Plays DEMO.AMF (random panning) when sound is on, CD track 2 when the
 *   CD music is not playing yet.  The menu (4 items into the window
 *   strings A63C..A768) loops until ui_event_loop leaves a valid choice
 *   in D5DC; "eski oyuna devam et" (2) runs the save/load menu and comes
 *   back here when it is cancelled.  Returns D5DC (1..4), or 2 after a
 *   loaded game.
 * Original: IST.EXE 3C2D:054D (DEMOLAR.L), far cdecl (int slot, char arg);
 *   main passes slot 0Bh, so slots 0Bh..0Eh are used.
 * Verified: not verified (long video/fade sequence; parts covered by the
 *   tests of demo_show_picture and demo_wait_key_or_delay).
 */
int16_t demo_intro_menu(int16_t slot, uint8_t arg)
{
    uint8_t cnt = 0;
    uint8_t r = 0;
    int16_t i;
    int16_t err;

    if (arg != 0xC8 && arg > 2)
        return (int16_t)(arg - 2);

    g_music_level = 0x40;
    win_record_copy(WIN_CUR, 0x98B4);
    ctl_flush_keys();
    ogr_black_palette();
    if (arg != 0xC8) {
        if (gfl_play(DGSTR(0x4B82)) == 0x1B) {
            g_pal_level = g_pal_level_max;
            gr_fade(0, (int16_t)-g_pal_level, 0);
            arg = 1;
            r = 1;
        } else {
            g_pal_level = g_pal_level_max;
            gr_fade(0, (int16_t)-g_pal_level, 0);
            if (gfl_play(DGSTR(0x4B88)) == 0x1B) {
                g_pal_level = g_pal_level_max;
                gr_fade(0, (int16_t)-g_pal_level, 0);
                arg = 1;
                r = 1;
            }
            g_pal_level = g_pal_level_max;
            gr_fade(0, (int16_t)-g_pal_level, 0);
        }
        if (arg == 1) {
            r = 1;
            goto music;
        }
        if (arg <= 1)
            goto mode12;
    }
    /* 0696: menu screen only */
    ogr_int10(0x13);
    ogr_init();
    ogr_mouse_set_window(0, 0, 0x13F, 0xC7);
    ogr_black_palette();
    r = 1;
    fm_load_cmp(1, DGSTR(0x4B90), slot);
    mm_copy_block(SURF(slot), SURF(0));
    ctl_free_checked(SURF_REF(slot), 0x105, DGSTR(0x4B9C));
    fm_load_pal(DGSTR(0x4BAB), 0, 0x100);
    gr_fade(0x640, (int16_t)(g_pal_level_max - g_pal_level), 0);
    goto menu;

mode12:
    /* 074C: the two 640x480 16-colour pictures */
    ogr_int10(0x12);
    g_surf_w(0) = 0x280;
    g_surf_h(0) = 0xF0;
    SURF(0)->size = 0x25800;
    asm_memset32(gr_target_pal, 0, 0x300);
    gr_set_palette(gr_target_pal);
    fm_load_pal(DGSTR(0x4BB7), 0, 0x100);
    ogr_copy_pal_entries(gr_target_pal);
    fm_load_cmp(1, DGSTR(0x4BC2), slot);
    sys_blit_cb(0, 0, (uint16_t)g_surf_w(slot), (uint16_t)(g_surf_h(slot) * 2), 0, 0,
                (uint8_t)slot, 0, sys_rowcopy_planar);
    ctl_free_checked(SURF_REF(slot), 0x12F, DGSTR(0x4BCD));
    fm_load_cmp(1, DGSTR(0x4BDC), slot);
    sys_blit_cb(0, 0, (uint16_t)g_surf_w(slot), (uint16_t)(g_surf_h(slot) * 2), 0,
                (uint16_t)(g_surf_h(slot) * 2), (uint8_t)slot, 0, sys_rowcopy_planar);
    ctl_free_checked(SURF_REF(slot), 0x133, DGSTR(0x4BE7));
    gr_fade(0x640, (int16_t)(g_pal_level_max - g_pal_level), 0);
    r = (uint8_t)demo_wait_key_or_delay(cnt++, 0x20, 0xBB8);
    gr_fade(0x640, (int16_t)-g_pal_level, 0);
    if (r != 0x1B) {
        ogr_int10(0x12);
        asm_memset32(gr_target_pal, 0, 0x300);
        gr_set_palette(gr_target_pal);
        fm_load_pal(DGSTR(0x4BF6), 0, 0x100);
        ogr_copy_pal_entries(gr_target_pal);
        fm_load_cmp(1, DGSTR(0x4BFF), slot);
        sys_blit_cb(0, 0, 0x280, 0xBE, 0, 0x64, (uint8_t)slot, 0, sys_rowcopy_planar);
        ctl_free_checked(SURF_REF(slot), 0x144, DGSTR(0x4C08));
        o5737_cd_play_track(2, -1);
        gr_fade(0x640, (int16_t)(g_pal_level_max - g_pal_level), 0);
        r = (uint8_t)demo_wait_key_or_delay(cnt++, 0x20, 0xBB8);
        gr_fade(0x640, (int16_t)-g_pal_level, 0);
    }

music:
    /* 0A37 */
    if (!o5737_cd_play())
        o5737_cd_play_track(2, -1);
    for (i = 0; i < 4; i++)
        if (SURF_SET(slot + i))
            o559A_fatal(DGSTR(0x4C17), 0x150, 0, 0x69);
    if (g_sound_on && ampGetModuleStatus() != 1) {
        err = ampLoadModule(DGSTR(0x4C26), 0, &g_music_module);
        if (err != 0) {
            if (err == -2)
                o559A_fatal(DGSTR(0x4C2F), 0x153, DGSTR(0x4C3E), -1);
            else if (err == -1)
                o559A_fatal(DGSTR(0x4C61), 0x154, DGSTR(0x4C70), -1);
        }
    }
    if (g_sound_on && ampGetModuleStatus() != 1) {
        ampPlayModule(g_music_module, 1);
        ampSetTrackVolume(-1, 0x40);
        music_pan_random();
    }

    /* 0BDE: the title */
    sys_fill_slot(0, 0xFA00, 0);
    ogr_int10(0x13);
    ogr_init();
    ogr_mouse_set_window(0, 0, 0x13F, 0xC7);
    ogr_black_palette();
    fm_load_pal(DGSTR(0x4C95), 0, 0x100);
    sys_load_rix(DGSTR(0x4CA1));
    fm_load_cmp(1, DGSTR(0x4CAD), slot);
    mm_copy_block(SURF(slot), SURF(0));
    gr_fade(0x640, (int16_t)(g_pal_level_max - g_pal_level), 0);
    fm_load_cmp(1, DGSTR(0x4CB9), slot + 1);
    r = (uint8_t)demo_wait_key_or_delay(cnt++, 0x20, 1000);
    mm_surface_alloc((uint8_t)(slot + 2), 0x140, 0x32, 1);
    mm_copy_range(SURF(slot), SURF(slot + 2), 0, 0, 0x3E80);
    mm_surface_alloc((uint8_t)(slot + 3), 0x140, 0x32, 1);
    mm_copy_range(SURF(slot + 1), SURF(slot + 3), 0, 0, 0x3E80);
    if (demo_show_picture(SURF(slot + 2), SURF(slot + 3), 1000, 0, 0, 1000, 0) != 1) {
        mm_copy_range(SURF(slot), SURF(slot + 2), 0x3E80, 0, 0x3E80);
        mm_copy_range(SURF(slot + 1), SURF(slot + 3), 0x3E80, 0, 0x3E80);
        demo_show_picture(SURF(slot + 2), SURF(slot + 3), 1000, 0, 0, 1000, 0x3E80);
    }
    /* 0E65 */
    mm_copy_block(SURF(slot + 1), SURF(0));
    ctl_free_checked(SURF_REF(slot + 2), 0x17F, DGSTR(0x4CC5));
    ctl_free_checked(SURF_REF(slot + 3), 0x180, DGSTR(0x4CD4));
    r = (uint8_t)demo_wait_key_or_delay(cnt++, 0x20, 0x1F4);
    ctl_free_checked(SURF_REF(slot), 0x1AE, DGSTR(0x4CE3));
    ctl_free_checked(SURF_REF(slot + 1), 0x1AF, DGSTR(0x4CF2));
    (void)r;

menu:
    /* 0F17 */
    strcpy(DGSTR(0xA63C), DGSTR(0x4D01));
    strcpy(DGSTR(0xA6A0), DGSTR(0x4D16));
    strcpy(DGSTR(0xA704), DGSTR(0x4D2A));
    strcpy(DGSTR(0xA768), DGSTR(0x4D3D));
    mm_surface_alloc(7, 0x78, 0x28, 1);
    sys_fill_slot(7, (uint16_t)SURF(7)->size, 0);
    gr_cursor_update(g_ui_shape);
    do {
        ui_event_loop(4, 0xB, -1, DG8(0x9665), DG8(0x9664), DG8(0x9667), 4, 1);
    } while (g_ui_list_sel == 0 || g_ui_list_sel / 5 != 0);
    ctl_free_checked(SURF_REF(7), 0x1C8, DGSTR(0x4D52));
    if (g_ui_list_sel == 2) {
        if ((uint8_t)s3d_save_load_menu(0) == 0)
            goto menu;
        return 2;
    }
    if (g_ui_list_sel == 1) {
        gr_fade(0x5DC, (int16_t)-g_pal_level, 0);
    } else {
        gr_fade(0x5DC, (int16_t)-g_pal_level, (int16_t)-g_music_level);
        music_stop_free();
        o5737_cd_stop();
    }
    ogr_set_start_addr(0);
    ogr_set_split_line(0xC8);
    ogr_mouse_set_window(0, 0, 0x13F, 0xC7);
    return g_ui_list_sel;
}

/* one slide of demo_story_scene: draw text page t onto the back page,
   cross-fade, play paraNN.snd; returns non-zero when the scene ends. */
static int story_slide(int16_t slot, int16_t *page, int16_t *t, char **texts, uint8_t group0,
                       uint16_t wait_ms, char *sndname)
{
    int16_t pos = 0, line = 0;
    uint8_t r, e;
    mm_copy_block(SURF(slot), SURF(slot + *page + 1));
    grt_decode_turkish_lower(texts[*t]);
    for (;;) {
        int16_t odd = *t & 1;
        int16_t x = group0 ? (int16_t)(((*t ^ 1) & 1) * 0x13F + 1) : (int16_t)((odd ? 0 : 1) * 0x140);
        char *s = texts[*t] + pos;
        pos++;
        if (ui_format_text((uint8_t)(slot + *page + 1), s, x, (int16_t)(odd * 0x6E + line * 0xB + 1),
                           0xFF, &pos, (uint8_t)((odd ? 0 : 1) | 2)))
            break;
        if (line >= 6)
            break;
        line++;
    }
    ctl_flush_keys();
    r = demo_show_picture(SURF(slot + (*page ^ 1) + 1), SURF(slot + *page + 1), 1000, 0, 0,
                          wait_ms, 0);
    if (r != 0 && r != 2)
        return 1;
    e = (uint8_t)oses_load_sample(1, sndname, 0x21, 2, 0, 0);
    if (e != 0) {
        if (group0)
            o559A_fatal(DGSTR(0x55CF), 0x306,
                        e == 1 ? DGSTR(0x55DE) : e == 2 ? DGSTR(0x55F2) : DGSTR(0x5618), -1);
        else
            o559A_fatal(DGSTR(0x5641), 0x320,
                        e == 1 ? DGSTR(0x5650) : e == 2 ? DGSTR(0x5664) : DGSTR(0x568A), -1);
    }
    ui_load_voice(0x21, 0x4E52, 0, 0);
    if (ui_play_voice(0x21, 0))
        return 1;
    *page ^= 1;
    (*t)++;
    sndname[5]++;
    return 0;
}

/*
 * demo_story_scene - the story slideshow (FRPOS calls it): 8 pictures
 * sbd00.cmp, sbd01.cmp... (the digit at byte 4 of the DGROUP name is
 * incremented, and so is byte 5 of para00.snd per slide - both stay
 * changed, the original never resets them), counts[g] slides each, the
 * page texts (DGROUP 4AE6) laid out alternately left/right with
 * ui_format_text and read aloud (voice slot 21h).  A key in a cross-fade
 * other than "any key" (Esc), or a key during the voice, ends the scene.
 * Music DEMO.AMF and CD track 3.  Slots slot..slot+2.
 * Original: IST.EXE 3C2D:1108 (DEMOLAR.L), far cdecl.
 * Verified: not verified (needs the voice/sample playback in the
 *   differential harness).
 */
void demo_story_scene(int16_t slot)
{
    char *texts[10];
    uint8_t counts[8];
    uint8_t saved[0x48];
    char *picname = DGSTR(DEMO_STORY_PIC);
    char *sndname = DGSTR(DEMO_STORY_SND);
    int16_t page, t = 0, k, g = 0;
    uint16_t wait_ms;
    int16_t i;

    for (i = 0; i < 10; i++)
        texts[i] = DGSTR(DG16(DEMO_STORY_TEXTS + 4 * i));
    memcpy(counts, DGP(DEMO_STORY_COUNTS), 8);
    memcpy(saved, DGP(WIN_CUR), 0x48);
    win_record_copy(WIN_CUR, 0x9674);
    ctl_flush_keys();
    sys_load_rix(DGSTR(0x5571));
    for (i = 0; i < 3; i++)
        if (SURF_SET(slot + i))
            o559A_fatal(DGSTR(0x557B), 0x2DA, 0, 0x6A);
    if (g_sound_on && ampLoadModule(DGSTR(0x558A), 0, &g_music_module) != 0)
        o559A_fatal(DGSTR(0x5593), 0x2DC, DGSTR(0x55A2), -1);
    ogr_clear_screen();
    ogr_black_palette();
    fm_load_pal(DGSTR(0x55C5), 0, 0x100);
    g_music_level = 0x40;
    if (g_sound_on) {
        ampPlayModule(g_music_module, 1);
        ampSetTrackVolume(-1, 0x40);
        music_pan_random();
    }
    o5737_cd_play_track(3, -1);
    fm_load_cmp(1, picname, slot);
    mm_copy_range(SURF(slot), SURF(0), 0, 0, SURF(slot)->size);
    gr_fade(0xC80, (int16_t)(g_pal_level_max - g_pal_level), 0);
    mm_surface_alloc((uint8_t)(slot + 1), g_surf_w(slot), g_surf_h(slot), 1);
    mm_surface_alloc((uint8_t)(slot + 2), g_surf_w(slot), g_surf_h(slot), 1);
    mm_copy_block(SURF(slot), SURF(slot + 2));
    page = 1;
    wait_ms = 0;
    for (k = 0; counts[g] > k; k++) {
        if (story_slide(slot, &page, &t, texts, 1, wait_ms, sndname))
            goto end;
        wait_ms = 0;
    }
    ctl_free_checked(SURF_REF(slot), 0x30E, DGSTR(0x5632));
    picname[4]++;
    for (g = 1; g < 8; g++) {
        fm_load_cmp(1, picname, slot);
        for (k = 0; counts[g] > k; k++)
            if (story_slide(slot, &page, &t, texts, 0, wait_ms, sndname))
                goto end;
        if (g < 7)
            ctl_free_checked(SURF_REF(slot), 0x327, DGSTR(0x56A4));
        picname[4]++;
    }
    ctl_free_checked(SURF_REF(slot + 2), 0x32A, DGSTR(0x56B3));
    mm_change_type(SURF(slot), 0);
    demo_show_picture(SURF(slot + (page ^ 1) + 1), SURF(slot), 2000, 0, 0, wait_ms, 0);
end:
    if (SURF_SET(slot + 2))
        ctl_free_checked(SURF_REF(slot + 2), 0x330, DGSTR(0x56C2));
    ctl_free_checked(SURF_REF(slot + 1), 0x331, DGSTR(0x56D1));
    ctl_free_checked(SURF_REF(slot), 0x332, DGSTR(0x56E0));
    gr_fade(0x5DC, (int16_t)-g_pal_level, (int16_t)-g_music_level);
    music_stop_free();
    sys_load_rix(DGSTR(0x56EF));
    ctl_flush_keys();
    memcpy(DGP(WIN_CUR), saved, 0x48);
    o5737_cd_stop();
}

/*
 * demo_game_over - all party members dead: stop the music, show icon 3Ah,
 * play module 10 ("TheEnd.amf") to its end with the laughing message,
 * then ask "eski bir oyuna devam edecek misiniz?" (evet/hayır) until a
 * save is loaded; "hayır" fades out and leaves through the credits.
 * Original: IST.EXE 3C2D:19F1 (DEMOLAR.L), far cdecl.
 * Port: pc_poll() in the "while the module plays" loop (1A85).
 * Verified: not verified (3DSYS save/load menu not available yet).
 */
void demo_game_over(void)
{
    music_stop_free();
    fr_show_icon(0x3A, 0);
    q3d_free_view_buffers();
    demo_play_music(10, 0, 0);
    ui_restore_background();
    ui_message_box(DGSTR(0x56F6), 1, 0);
    do {
        pc_poll();
        gr_cursor_update((uint8_t)g_cursor_shape);
    } while (ampGetModuleStatus() == 1);
    do {
        ui_restore_background();
        ui_message_box(DGSTR(0x5740), 1, 0);
        ui_dialog_2198(DGSTR(0x5766), DGSTR(0x576B));
        ui_restore_background();
        if (g_ui_dialog_result != 0) {
            gr_fade(1000, (int16_t)-g_pal_level, (int16_t)-g_music_level);
            music_stop_free();
            q3d_free_view_buffers();
            demo_credits_exit();
        }
    } while ((uint8_t)s3d_save_load_menu(0) == 0);
}

/*
 * demo_ending - the end of the game (only when FRPOS flag 9Ch is set):
 * the SON video, soon.cmp / devam.cmp with palette step fades and a
 * cross-fade, then "kayıt yapılacak dosyayı seçin" and the save menu
 * (mode 1) until something is saved, then the credits.
 * Original: IST.EXE 3C2D:1B76 (DEMOLAR.L), far cdecl.
 * Verified: not verified (needs FRPOS/3DSYS).
 */
void demo_ending(void)
{
    const int16_t sb = 0xB, sc = 0xC, sd = 0xD;
    uint8_t esc = 0;

    if (!fr_flag_test(0x9C))
        return;
    gr_fade(1000, (int16_t)-g_pal_level, (int16_t)-g_music_level);
    music_stop_free();
    q3d_free_view_buffers();
    win_record_copy(WIN_CUR, 0x9704);
    ui_init_voices();
    g_pal_level = g_pal_level_max;
    if (gfl_play(DGSTR(0x5771)) == 0x1B) {
        gr_fade(1000, (int16_t)-g_pal_level, 0);
        esc = 1;
    }
    win_record_copy(WIN_CUR, 0x986C);
    demo_play_music(g_music_track, 1, 0);
    gr_fade(1000, 0, g_music_level_max);
    sys_load_rix(DGSTR(0x5775));
    fm_load_cmp(1, DGSTR(0x577F), sb);
    fm_load_cmp(1, DGSTR(0x5788), sc);
    gr_cursor_hide();
    if (esc)
        ogr_black_palette();
    fm_load_pal(DGSTR(0x5792), 0, 0x100);
    if (esc)
        ogr_black_palette();
    sys_blit(0, 0, (uint16_t)g_surf_w(sb), (uint16_t)g_surf_h(sb), 0, 0, (uint8_t)sb, 0,
             sys_rect_copy);
    mm_surface_alloc((uint8_t)sd, g_surf_w(sc), g_surf_h(sc), 1);
    sys_blit(0, 0x3E, (uint16_t)g_surf_w(sc), (uint16_t)g_surf_h(sc), 0, 0, (uint8_t)sb,
             (uint8_t)sd, sys_rect_copy);
    if (esc)
        gr_fade(0x1F4, g_pal_level_max, 0);
    else
        memcpy(gr_work_pal, gr_target_pal, 0x300);
    ui_wait_click_or_key(1000);
    ogr_palette_step_fade(1, DGSTR(0x579B), 0x14);
    demo_show_picture(SURF(sd), SURF(sc), 1000, 0, 0, 0, 0x4D80);
    ogr_palette_step_fade(1, DGSTR(0x57A5), 1);
    ctl_free_checked(SURF_REF(sb), 0x39B, DGSTR(0x57AF));
    ctl_free_checked(SURF_REF(sc), 0x39C, DGSTR(0x57BE));
    ctl_free_checked(SURF_REF(sd), 0x39D, DGSTR(0x57CD));
    ui_wait_click_or_key(0x64);
    gr_cursor_set_shape((uint8_t)((int16_t (*)(void))ui_far_fn(DG32(0x9650)))());
    ctl_free_checked(0xE4D7, 0x3A0, DGSTR(0x57DC));
    ui_message_box(DGSTR(0x57EB), 1, 0);
    ui_wait_click_or_key(0x8C);
    ui_restore_background();
    while ((uint8_t)s3d_save_load_menu(1) == 0)
        ;
    ogr_palette_step_fade(7, 0, 5);
    demo_credits_exit();
}

/*
 * demo_play_music - load and play module g_music_names(track) with the
 * random channel spread; the last channel (the sound effects) goes to the
 * middle (panning 100 = surround).  When the music level is up it is faded
 * down first and the volume restored after.  Quirk kept: the old module is
 * stopped only when g_music_module is NULL (the test is inverted), so a
 * playing module is replaced without being freed.  When there is no memory
 * for the module, demo_free_music turns conventional surfaces into EMS and
 * it tries again.
 * Original: IST.EXE 3C2D:1F32 (DEMOLAR.L), far cdecl (int track, long options).
 * Verified: tests/py/test_demolar.py (test_play_music).
 */
void demo_play_music(int16_t track, int16_t opt_lo, int16_t opt_hi)
{
    uint8_t fade = 0;
    int16_t r;

    if (!g_sound_on)
        return;
    mm_ems_lock(g_blit_lock, 0x3C4, DGSTR(0x580C));
    if (g_music_level != 0)
        fade = 1;
    if (fade)
        gr_fade(0x64, 0, (int16_t)-g_music_level);
    if (g_music_module == 0 && g_sound_on) {
        ampStopModule();
        if (g_music_module) {           /* never true here */
            ampFreeModule(g_music_module);
            MemFree(g_music_module);
            g_music_module = 0;
        }
    }
    if (fade)
        gr_fade(0, 0, g_music_level_max);
    g_music_track = track;
    do {
        r = ampLoadModule(g_music_names(g_music_track), 0, &g_music_module);
        if (r != 0) {
            if (r == -1) {
                if (!demo_free_music())
                    o559A_fatal(DGSTR(0x581B), 0x3CE, DGSTR(0x582A), -1);
            } else {
                o559A_fatal(DGSTR(0x5843), 0x3D0, DGSTR(0x5852), -1);
            }
        }
    } while (r != 0);
    ampPlayModule(g_music_module, (int32_t)((uint32_t)(uint16_t)opt_lo | (uint32_t)(uint16_t)opt_hi << 16));
    music_pan_random();
    cdiSetPanning((int16_t)(g_snd_channels - 1), 100);
    mm_ems_unlock(g_blit_lock);
}

/* credits: the line in BACC drawn 5 times as a shadow/outline + once in
   its colour (24C9 / 25C5) */
static void credits_draw(uint8_t style, uint8_t lineh, uint8_t col)
{
    int16_t y = (int16_t)(g_surf_h(0xC) - lineh);
    char *s = DGSTR(0xBACC);
    if (style == 1) {
        grt_draw_text(0xC, 4, y, s, 0xFA, 5);
        grt_draw_text(0xC, 5, (int16_t)(y - 1), s, 0xFA, 5);
        grt_draw_text(0xC, 6, (int16_t)(y + 1), s, 0, 5);
        grt_draw_text(0xC, 5, (int16_t)(y + 1), s, 0, 5);
        grt_draw_text(0xC, 6, y, s, 0, 5);
        grt_draw_text(0xC, 5, y, s, col, 5);
    } else if (style == 2) {
        grt_draw_text_alt(0xC, 4, y, s, 0xFA);
        grt_draw_text_alt(0xC, 5, (int16_t)(y - 1), s, 0xFA);
        grt_draw_text_alt(0xC, 6, (int16_t)(y + 1), s, 0);
        grt_draw_text_alt(0xC, 5, (int16_t)(y + 1), s, 0);
        grt_draw_text_alt(0xC, 6, y, s, 0);
        grt_draw_text_alt(0xC, 5, y, s, col);
    }
}

/*
 * demo_credits_exit - the closing credits: THANX.DOC scrolls up in a
 * 225x200 window over grup.cmp with music 11, fading in (the fade runs in
 * the INTRFACE idle callback through g_fade_* = s3d_apply_sound_options).
 * Lines starting with '-' are headings (colour D5h), an upper-case first
 * letter means big font lower-cased (3Ch), else the small font (FFh);
 * lines shorter than 2 chars are 20 rows high.  A key or click fades out
 * and ends; a click on the "SON" button (108h,9Fh 35hx11h) instead plays
 * the "son" video and exits.  The way out is always o559A_fatal(-1) with
 * "İSTANBUL EFSANELERİ I - Lale Savaşçıları, sorunsuz bitti!" - the
 * game's normal exit.
 * Original: IST.EXE 3C2D:217C (DEMOLAR.L), far cdecl.
 * Port: pc_poll() at the head of the frame wait (276F).
 * Verified: tests/py/test_demolar.py (test_credits, sampled frames).
 */
void demo_credits_exit(void)
{
    uint8_t style = 1, lineh = 0xF, col = 0xFF, frac = 0, step = 0x10;
    int16_t state = 0, speed = 1;
    uint16_t y = 0, delay = 0x32, t0;
    int16_t i, mt;
    int16_t fd;
    int32_t pos;

    fd = vfs_open(DGSTR(0x586C), 2);
    if (fd == -1)
        o559A_fatal(DGSTR(0x5876), 0x3EA, DGSTR(0x5885), -1);
    if (g_pal_level != 0)
        gr_fade(0x64, (int16_t)-g_pal_level, (int16_t)-g_music_level);
    fm_load_pal(DGSTR(0x58B0), 0, 0x100);
    fm_load_cmp(1, DGSTR(0x58B9), 0xB);
    gr_cursor_hide();
    mm_copy_block(SURF(9), SURF(0));
    gr_cursor_set_shape(0x33);
    demo_play_music(0xB, 1, 0);
    gr_fade(0, 0, (int16_t)(g_music_level_max - 0x1E));
    mm_surface_alloc(0xC, 0xE1, 0xC8, 1);
    mm_surface_alloc(0xD, 0xE1, 0xC8, 1);
    ctl_flush_keys();
    sys_fill_slot(0xC, 0xAFC8, 0xFE);
    g_fade_bright_d = g_pal_level_max;
    g_fade_speed = 1;
    g_fade_volume_d = 0;
    for (;;) {
        /* 22DD */
        t0 = (uint16_t)g_clock_ms;
        if (state == 0) {
            uint16_t s = (uint16_t)frac + step;
            frac = (uint8_t)s;
            if (s > 0xFF)
                g_fade_speed++;
            if (g_fade_bright_d == 0)
                state = 5;
        } else if (state == 2) {
            uint8_t old = frac;
            frac = (uint8_t)(frac - step);
            if (old < step)
                g_fade_speed--;
            if (g_fade_bright_d == 0)
                break;
        }
        while (y / lineh != 0) {
            /* 2361: next line of THANX.DOC */
            pos = vfs_tell(fd);
            vfs_read(fd, DGP(0xBACC), 0x67);
            for (i = 0; i < 0x67; i++)
                if (DG8(0xBACC + i) == '\n') {
                    DG8(0xBACC + i) = 0;
                    break;
                }
            sys_fill_rect(0, (uint16_t)(g_surf_h(0xC) - y), (uint16_t)g_surf_w(0xC), lineh, 0xC,
                          0xFE);
            y = (uint16_t)(y - lineh);
            if (DG8(0xBACC) == '-') {
                DG8(0xBACC) = ' ';
                style = 1;
                lineh = 0xF;
                col = 0xD5;
            } else if (grt_is_upper(DG8(0xBACC))) {
                style = 1;
                lineh = 0xF;
                grt_tolower_inplace(DGSTR(0xBACC));
                col = 0x3C;
            } else {
                style = 2;
                lineh = 0xB;
                col = 0xFF;
            }
            if (strlen(DGSTR(0xBACC)) < 2)
                lineh = 0x14;
            vfs_seek(fd, pos + i + 1, 0);
            grt_decode_turkish_lower(DGSTR(0xBACC));
            if (strlen(DGSTR(0xBACC)) > 0x68)
                o559A_fatal(DGSTR(0x58C2), 0x429, 0, 0x6B);
            credits_draw(style, lineh, col);
        }
        /* 26C7: scroll */
        y = (uint16_t)(y + speed);
        mm_copy_range(SURF(0xC), SURF(0xC), (uint32_t)(int32_t)(int16_t)(speed * g_surf_w(0xC)), 0,
                      (uint32_t)((int32_t)(int16_t)(g_surf_h(0xC) - speed) * (int32_t)g_surf_w(0xC)));
        sys_blit(0, 0, (uint16_t)g_surf_w(0xC), (uint16_t)g_surf_h(0xC), 0, 0, 0xB, 0xD,
                 sys_rect_copy);
        sys_blit_0615(0xC, 0xD, 0xAFC8, 0xFE, mm_copy_transparent);
        do {
            pc_poll();
            ((void (*)(uint32_t))ui_far_fn(DG32(0x964C)))(DG32(0x9660));
        } while ((uint16_t)((uint16_t)g_clock_ms - t0) < delay);
        gr_cursor_hide();
        sys_blit(0, 0xF, (uint16_t)g_surf_w(0xD), (uint16_t)(g_surf_h(0xD) - 0x1E), 0, 0xF, 0xD, 0,
                 sys_rect_copy);
        gr_cursor_set_shape((uint8_t)g_cursor_shape);
        if (!ui_kbhit() && !gr_mouse_read(&mt, &mt, &mt))
            continue;
        if (state == 2)
            continue;
        if (gr_cursor_in_rect(0x108, 0x9F, 0x35, 0x11)) {
            gr_fade(0x1F4, 0, (int16_t)-g_music_level);
            music_stop_free();
            win_record_copy(WIN_CUR, 0x9704);
            ui_init_voices();
            g_mouse_present = 0;
            if (!gfl_play(DGSTR(0x58D1)))
                ui_wait_click_or_key(0x8C);
            o559A_fatal(DGSTR(0x58D5), 0x44E, DGSTR(0x58E4), -1);
        }
        g_fade_bright_d = (int16_t)-g_pal_level;
        g_fade_volume_d = (int16_t)-g_music_level;
        state = 2;
    }
    /* 2926 */
    vfs_close(fd);
    ctl_free_checked(SURF_REF(0xB), 0x456, DGSTR(0x58EF));
    ctl_free_checked(SURF_REF(0xC), 0x457, DGSTR(0x58FE));
    ctl_free_checked(SURF_REF(0xD), 0x458, DGSTR(0x590D));
    if (ui_kbhit())
        ui_getch();
    o559A_fatal(DGSTR(0x591C), 0x45C, DGSTR(0x592B), -1);
}

/* the screen buffer as demo_palette_effect fetches it per pixel */
static uint8_t *screen_buf(void)
{
    MmHandle *s = SURF(0);
    if (s->type == 0)
        return mm_conv_ptr(s);
    return EMSLock((int16_t)s->ems, 0, s->size > 0xFFFFu ? 0xFFFFu : (uint16_t)s->size);
}

/*
 * demo_palette_effect - draw the outline of mouse zone `zone` (ngraphs
 * zones E5F5) dotted (every 2nd pixel, colour FFh) on the screen, the
 * rows yoff..yoff+199 of it, shifted up by yoff.  (The name comes from
 * the inventory; it draws no palette effect.)  Side columns are drawn in a
 * do-while from y1, so a zone with y2 < y1 still gets one row.
 * Original: IST.EXE 3C2D:2992 (DEMOLAR.L), far cdecl (char zone, int yoff).
 * Verified: tests/py/test_demolar.py (test_zone_outline).
 */
void demo_palette_effect(uint8_t zone, int16_t yoff)
{
    int16_t v;

    if (g_zone_y1(zone) >= yoff && g_zone_y1(zone) < (int16_t)(yoff + 200))
        for (v = g_zone_x1(zone); g_zone_x2(zone) >= v; v += 2)
            sys_putpixel((uint16_t)v, (uint16_t)(g_zone_y1(zone) - yoff), 0xFF, screen_buf(),
                         (uint16_t)g_surf_w(0));
    v = g_zone_y1(zone);
    do {
        if (v >= yoff && (int16_t)(yoff + 200) > v) {
            sys_putpixel((uint16_t)g_zone_x1(zone), (uint16_t)(v - yoff), 0xFF, screen_buf(),
                         (uint16_t)g_surf_w(0));
            sys_putpixel((uint16_t)g_zone_x2(zone), (uint16_t)(v - yoff), 0xFF, screen_buf(),
                         (uint16_t)g_surf_w(0));
        }
        v += 2;
    } while (g_zone_y2(zone) >= v);
    if (g_zone_y2(zone) >= yoff && g_zone_y2(zone) < (int16_t)(yoff + 200))
        for (v = g_zone_x1(zone); g_zone_x2(zone) >= v; v += 2)
            sys_putpixel((uint16_t)v, (uint16_t)(g_zone_y2(zone) - yoff), 0xFF, screen_buf(),
                         (uint16_t)g_surf_w(0));
}

/*
 * demo_stub_2D3E - dominant channel of target palette entry idx: 0 red,
 * 1 green, 2 blue (strictly greater than both others), else 4.
 * Original: IST.EXE 3C2D:2D3E (DEMOLAR.L), far cdecl (unsigned char idx).
 * Verified: tests/py/test_demolar.py (test_stub_2D3E).
 */
uint8_t demo_stub_2D3E(uint8_t idx)
{
    uint8_t r = gr_target_pal[idx * 3];
    uint8_t g = gr_target_pal[idx * 3 + 1];
    uint8_t b = gr_target_pal[idx * 3 + 2];
    if (r > g && r > b)
        return 0;
    if (g > r && g > b)
        return 1;
    if (b > g && b > r)
        return 2;
    return 4;
}

static void map_status_text(const char *s1, const char *s2)
{
    /* the original switches on the constant 2 (1 = grt_draw_text variant,
       never taken) */
    grt_draw_text_alt(0, 0, 0xBE, s1, 0);
    grt_draw_text_alt(0, 0, 0xBD, s2, 0xFF);
}

static void map_clear_status(void)
{
    sys_fill_rect(0, 0xBD, 0xC8, 0xA, 0, 0);
}

/*
 * demo_city_map_travel - the Istanbul map (isthara/istharb.cmp, 320x350
 * in slot 6, scrolled to the marker): the arrow keys move the marker one
 * pixel (not onto colours 8..0Fh / 1Ah..1Dh), each step advances the
 * game clock 3 minutes and follows day/night with the palette.  Over one
 * of the 12 district rectangles (DGROUP 4A86) its name is shown; Enter
 * asks "%s girecek miyiz? ...(e/h)".  Returns the district (2..13), 0Eh
 * for the sewers of Kadıköy ("s/k" when FRPOS flags 22h/45h differ), or
 * the last district shown when 'e' was pressed.  Moving right past x C6h
 * needs FRPOS flag 10h (the bridge), else "trafik tıkalı".
 * Quirks: the marker's sprite position g_map_x is left as drawn, only
 * g_map_y is rewritten on exit; dir > 3 is impossible (switch default
 * would jump to the Enter handling).
 * Original: IST.EXE 3C2D:2DE2 (DEMOLAR.L), far cdecl.
 * Verified: not verified (needs 3DSYS: clock, daylight, status line).
 */
int16_t demo_city_map_travel(void)
{
    char *names[12];
    char buf[0x64];
    uint8_t dir = 0, fadeon = 0, sel = 0;
    int16_t x, y, scroll, px, py, i, sx, sy, w, h;
    uint16_t day, key;
    uint8_t saved_mouse;
    uint8_t *p;

    for (i = 0; i < 12; i++)
        names[i] = DGSTR(DG16(DEMO_DISTRICTS + 4 * i));
    x = (int16_t)(g_map_x + 5);
    y = (int16_t)(g_map_y + 5);
    scroll = y <= 0x5D ? 0 : (y < 0xF7 ? (int16_t)(y - 0x5D) : 0x9A);
    (void)scroll;
    px = x;
    py = y;
    for (i = 0; i < 12; i++)
        grt_decode_turkish_lower(names[i]);
    gr_cursor_hide();
    saved_mouse = g_mouse_present;
    g_mouse_present = 0;
    sys_fill_slot(0, 0xFA00, 0);
    ogr_palette_step_fade(1, (g_q3d_opt_flags & 1) ? DGSTR(0x59C9) : DGSTR(0x59D5), 0x14);
    mm_surface_alloc(6, 0x140, 0x15E, 1);
    g_q3d_opt_flags |= 0x10;
    s3d_draw_status_line(0xD2, 0xBD);
    g_q3d_opt_flags &= 0xFFEF;
    fm_load_cmp(1, DGSTR(0x59E0), 0x57);
    mm_copy_range(SURF(0x57), SURF(6), 0, 0, 0xFA00);
    ctl_free_checked(SURF_REF(0x57), 0x4CC, DGSTR(0x59EC));
    fm_load_cmp(1, DGSTR(0x59FB), 0x57);
    mm_copy_range(SURF(0x57), SURF(6), 0, 0xFA00, 0xBB80);
    ctl_free_checked(SURF_REF(0x57), 0x4CF, DGSTR(0x5A07));

redraw:
    /* 2FA7 */
    mm_ems_lock(g_blit_lock, 0x4D3, DGSTR(0x5A16));
    p = EMSLock((int16_t)SURF(6)->ems, (int32_t)y * 0x140 + x, 1);
    if ((*p >= 8 && *p < 0x10) || (*p >= 0x1A && *p < 0x1E)) {
        x = px;
        y = py;
        mm_ems_unlock(g_blit_lock);
        goto keys;
    }
    mm_ems_unlock(g_blit_lock);
    scroll = y <= 0x5D ? 0 : (y < 0xF7 ? (int16_t)(y - 0x5D) : 0x9A);
    mm_copy_range(SURF(6), SURF(0), (uint32_t)((int32_t)scroll * 0x140), 0, 0xE880);
    switch (dir) {
    case 0: g_map_x = (int16_t)(x - 4); g_map_y = (int16_t)(y - scroll - 5); sx = 0;    break;
    case 1: g_map_x = (int16_t)(x - 5); g_map_y = (int16_t)(y - scroll - 5); sx = 0xA;  break;
    case 2: g_map_x = (int16_t)(x - 5); g_map_y = (int16_t)(y - scroll - 5); sx = 0x14; break;
    case 3: g_map_x = (int16_t)(x - 5); g_map_y = (int16_t)(y - scroll - 4); sx = 0x1E; break;
    default: goto enter;
    }
    sy = 0x154;
    w = h = 0xA;
    if (g_map_x < 0) {
        sx = (int16_t)(sx - g_map_x);
        w = (int16_t)(w + g_map_x);
        g_map_x = 0;
    }
    if (g_map_y < 0) {
        sy = (int16_t)(sy - g_map_y);
        h = (int16_t)(h + g_map_y);
        g_map_y = 0;
    }
    sys_blit((uint16_t)sx, (uint16_t)sy, (uint16_t)w, (uint16_t)h, (uint16_t)g_map_x,
             (uint16_t)g_map_y, 6, 0, asm_copy_rect_key);
    px = x;
    py = y;
    s3d_advance_clock(3);
    day = s3d_calc_daylight();
    if (day & 1) {
        if (~g_q3d_opt_flags & 1) {
            while (fadeon)
                fadeon = ogr_palette_step_fade(0x21, 0, 0);
            fadeon = ogr_palette_step_fade(0x21, DGSTR(0x5A25), 0x14);
        }
    } else if (g_q3d_opt_flags & 1) {
        while (fadeon)
            fadeon = ogr_palette_step_fade(0x21, 0, 0);
        fadeon = ogr_palette_step_fade(0x21, DGSTR(0x5A31), 0x14);
    }
    g_q3d_opt_flags = day;
    s3d_draw_status_line(0xD2, 0xBD);
    sel = 0;
    for (i = 0; i < 12; i++) {
        uint16_t r = (uint16_t)(DEMO_MAP_RECTS + 8 * i);
        if (DGS16(r) < x && DGS16(r + 4) > x && DGS16(r + 2) < y && DGS16(r + 6) > y)
            sel = (uint8_t)(i + 2);
    }
    if (sel) {
        if (sprintf(buf, DGSTR(0x5A3C), names[sel - 1]) >= 0x64)
            o559A_fatal(DGSTR(0x5A3F), 0x508, 0, 0x6C);
        map_status_text(buf, buf);
    } else {
        map_clear_status();
    }
    if (fadeon)
        fadeon = ogr_palette_step_fade(0x21, 0, 0);

keys:
    /* 338B */
    key = (uint16_t)ui_bioskey(0);
    switch ((int16_t)key >> 8) {
    case 0x48:                                       /* up */
        dir = 1;
        if (y > 0) {
            y--;
            goto redraw;
        }
        break;
    case 0x4B:                                       /* left */
        dir = 2;
        if (x > 0) {
            x--;
            goto redraw;
        }
        break;
    case 0x4D:                                       /* right */
        dir = 0;
        if (x >= 0x136)
            break;
        if (x <= 0xC6 || fr_flag_test(0x10)) {
            x++;
            goto redraw;
        }
        map_clear_status();
        map_status_text(DGSTR(0x5A86), DGSTR(0x5AA2));
        break;
    case 0x50:                                       /* down */
        dir = 3;
        if (y >= 0x153)
            break;
        y++;
        goto redraw;
    default:
        break;
    }
    /* 34A1 */
    if ((key & 0xFF) != 0xD)
        goto keys;
    if (sel == 0)
        goto keys;
enter:
    /* 34BD */
    map_clear_status();
    if (sprintf(buf, DGSTR(0x5ABE), names[sel - 1]) >= 0x64)
        o559A_fatal(DGSTR(0x5ADA), 0x531, 0, 0x6C);
    map_status_text(buf, buf);
    for (;;) {
        char kb[2];
        key = (uint16_t)ui_bioskey(0) & 0xFF;
        kb[0] = (char)key;
        kb[1] = 0;
        grt_tolower_inplace(kb);
        key = (uint16_t)((uint8_t)kb[0] | (uint8_t)kb[1] << 8);
        if (key == 'e') {
            if ((uint8_t)(fr_flag_test(0x22) ^ fr_flag_test(0x45)) != 0 && sel == 5) {
                map_clear_status();
                map_status_text(DGSTR(0x5B33), DGSTR(0x5B58));
                for (;;) {
                    key = (uint16_t)ui_bioskey(0) & 0xFF;
                    kb[0] = (char)key;
                    kb[1] = 0;
                    grt_tolower_inplace(kb);
                    key = (uint16_t)((uint8_t)kb[0] | (uint8_t)kb[1] << 8);
                    if (key == 's')
                        break;
                    if (key == 'k') {
                        sel = 0xE;
                        break;
                    }
                }
            }
            break;
        }
        if (key == 'h') {
            map_clear_status();
            goto keys;
        }
    }
    /* 36A7 */
    ctl_free_checked(SURF_REF(6), 0x547, DGSTR(0x5B7D));
    gr_fade(0x64, (int16_t)-g_pal_level, 0);
    ctl_flush_keys();
    g_map_y = (int16_t)(y - 5);
    g_mouse_present = saved_mouse;
    gr_cursor_set_shape((uint8_t)g_cursor_shape);
    return sel;
}

/*
 * demo_free_music - make room for a module: the first conventional surface
 * among slots 44h..41h (searching down) is moved to EMS.  Returns 1 if
 * one was moved.
 * Original: IST.EXE 3C2D:371E (DEMOLAR.L), far cdecl.
 * Verified: tests/py/test_demolar.py (test_free_music).
 */
uint8_t demo_free_music(void)
{
    uint8_t done = 0;
    for (int16_t i = 0x44; i >= 0x41; i--)
        if (SURF_SET(i) && SURF(i)->type == 0) {
            mm_change_type(SURF(i), 1);
            done = 1;
            break;
        }
    return done;
}

/*
 * demo_load_sounds - the new-game intro: buun.pur into sample slot 2Fh
 * (played at once when sound is on), the videos seq1..seq5 (Esc skips the
 * rest), the sample faded out over 1 s (cdiSetVolume steps) after seq3,
 * then stopped; finally the screen faded to black and the surface
 * E26D (slot -6?, sample buffer) freed when allocated.
 * Original: IST.EXE 3C2D:3790 (DEMOLAR.L), far cdecl.
 * Port: the volume ramp loop is a busy loop on the clock: pc_poll() at its
 * head (38F3 / 3930).
 * Verified: not verified (video playback; see gfl tests).
 */
void demo_load_sounds(void)
{
    int16_t e;
    uint32_t until;
    int32_t v;

    e = oses_load_sample(1, DGSTR(0x5B8C), 0x2F, 0, 0, 0x46E3);
    ampSetTrackVolume(-1, 0x40);
    g_music_level = g_music_level_max;
    if (g_sound_on && e == 0)
        snd_play_sample(0x2F, 0x2134, 0x20, 0x64, 0);
    if (gfl_play(DGSTR(0x5B95)) == 0x1B)
        goto out;
    g_pal_level = g_pal_level_max;
    gr_fade(0, (int16_t)-g_pal_level, 0);
    if (gfl_play(DGSTR(0x5B9A)) == 0x1B)
        goto out;
    g_pal_level = g_pal_level_max;
    gr_fade(0, (int16_t)-g_pal_level, 0);
    if (gfl_play(DGSTR(0x5B9F)) == 0x1B)
        goto out;
    g_pal_level = g_pal_level_max;
    gr_fade(0, (int16_t)-g_pal_level, 0);
    if (g_sound_on && e == 0) {
        until = g_clock_ms + 1000;
        v = 0x20;
        for (;;) {
            pc_poll();
            if (!(v > 0))
                break;
            v = (int32_t)(((until - g_clock_ms) << 5) / 1000u);
            cdiSetVolume(0, (uint32_t)v);
        }
        cdiStopChannel(0);
    }
    if (gfl_play(DGSTR(0x5BA4)) == 0x1B)
        goto out;
    g_pal_level = g_pal_level_max;
    gr_fade(0, (int16_t)-g_pal_level, 0);
    if (gfl_play(DGSTR(0x5BA9)) == 0x1B)
        goto out;
    g_pal_level = g_pal_level_max;
    gr_fade(0, (int16_t)-g_pal_level, 0);
out:
    g_pal_level = g_pal_level_max;
    gr_fade(1000, (int16_t)-g_pal_level, 0);
    if (g_sound_on && e == 0)
        cdiStopChannel(0);
    if (DG16(0xE26D) | DG16(0xE26F))
        ctl_free_checked(0xE26D, 0x58D, DGSTR(0x5BAE));
}

/* ---- port: overlay thunk registry ---------------------------------------- */
void demo_register_far_fns(void)
{
    /* INTRFACE calls 1682:002F (game over); the other void(void) entries
       are registered for callers that go through the registry too. */
    ui_register_far_fn(0x1682, 0x002F, demo_game_over);
    ui_register_far_fn(0x1682, 0x0034, demo_ending);
    ui_register_far_fn(0x1682, 0x0039, demo_credits_exit);
    ui_register_far_fn(0x1682, 0x0025, demo_load_sounds);
}
