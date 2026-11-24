/*
 * s3d_menu.c - 3DSYS.L 303F:765C-8523: leaving a district (city map,
 * district videos), the options menu, the save/load menu and the sound
 * fades run from the UI idle callback.
 *
 * Original: IST.EXE ovl303F (T:\CODE\3DSYS.L), Borland C far cdecl.
 */
#include <ctype.h>
#include <dirent.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "dsmi/amp.h"
#include "dsmi/memalloc.h"
#include "engine/fatal.h"
#include "engine/grtext.h"
#include "engine/intrface.h"
#include "engine/memman.h"
#include "engine/ngraphs.h"
#include "engine/nkontrol.h"
#include "engine/systems.h"
#include "engine/vfs.h"
#include "game/gfl.h"
#include "game/q3dview.h"
#include "game/s3d.h"
#include "game/s3d_ext.h"
#include "platform/pc.h"
#include "rtl/bc_rand.h"

/* Borland random(n) as compiled inline: (int)((long)rand() * n / 0x8000) */
static int16_t s3d_random(int16_t n)
{
    int16_t r = bc_rand();
    return (int16_t)(((int32_t)r * n) / 0x8000);
}

/* ampStopModule + free the module (the sequence the original inlines at
   every music switch: 303F:770C ...).  The module far pointer BB75/BB77
   is the native g_music_module (demolar.h). */
static void stop_music(void)
{
    ampStopModule();
    if (g_music_module) {
        ampFreeModule(g_music_module);
        MemFree(g_music_module);
        g_music_module = 0;
    }
}

/* a far char * of the district table (DGROUP pointer; null = no video) */
static const char *district_str(uint16_t field)
{
    uint16_t at = (uint16_t)(S3D_DISTRICT_TAB + 0x1D * g_s3d_district + field);
    if ((DG16(at) | DG16(at + 2)) == 0)
        return 0;
    return DGSTR(DG16(at));
}

/* the district video pair of leave/enter (303F:7808 / 79D0): plays it with
   window record 2 as the current one, then restores the caller's record and
   fades the picture in again */
static void play_district_video(const char *name, const uint8_t *saved_win)
{
    memcpy(DGP(UI_WIN_CUR), DGP(UI_WIN_REC(2)), UI_WIN_SIZE);
    ui_init_voices();
    gfl_play(name);
    memcpy(DGP(UI_WIN_CUR), saved_win, UI_WIN_SIZE);
    g_pal_level = g_pal_level_max;
    gr_fade(500, (int16_t)-g_pal_level, 0);
}

/* chance (percent) of the district video: limit 20 when leaving, 10 when
   entering; t = hours past 13:00 of the day counter 3D68 (wraps at 349) */
static int16_t video_chance(int16_t limit)
{
    int16_t t = g_s3d_day < 0x15D ? (int16_t)(g_s3d_day + 0x0B) : (int16_t)(g_s3d_day - 0x15D);
    return t > limit ? 0x19 : (int16_t)((limit - t) * 0x32 / limit + 0x19);
}

/*
 * s3d_leave_district - "semtten çıkacak mısınız?" (leave the district?);
 * on yes: fade out, maybe the district's exit video, the city map
 * (demo_city_map_travel), maybe the entry video of the new district, then
 * load it.  Sets the cancel-move word (FRPOS BBA8) in both cases.
 *
 * Original: IST.EXE 303F:765C (3DSYS.L), far cdecl.
 * Verified: tests/py/test_s3d_menu.py (yes/no, both video branches).
 */
void s3d_leave_district(void)
{
    uint8_t saved_win[UI_WIN_SIZE];
    int16_t ml = 0;
    uint8_t first = 1;              /* [bp-5]: s3d_load_district's "first" */
    int16_t old;

    memcpy(saved_win, DGP(UI_WIN_CUR), UI_WIN_SIZE);
    ui_restore_background();
    ui_message_box(DGSTR(0x44D6), 1, 0);
    ui_dialog_2198(DGSTR(0x44F2), DGSTR(0x44F7));
    if (g_ui_dialog_result != 0) {              /* hayır */
        fr_redraw();
        s3d_redraw_view(1);
        g_s3d_cancel_move = 1;
        return;
    }
    ui_restore_background();
    ml = g_music_level;
    gr_fade(1000, (int16_t)-g_pal_level, (int16_t)-ml);
    if (ml != 0 && g_sound_on != 0)
        stop_music();
    q3d_free_view_buffers();
    ctl_free_checked(0xE33F, 0x0A5C, DGSTR(0x44FD));    /* the map's block */

    /* 303F:775A exit video */
    if ((int8_t)s3d_random(100) <= video_chance(20)) {
        const char *v = district_str((g_q3d_opt_flags & 1) ? 0x14 : 0x10);
        if (v)
            play_district_video(v, saved_win);
    }
    if (ml != 0) {
        g_music_track = 1;
        demo_play_music(1, 1, 0);
        gr_fade(100, 0, ml);
    }
    old = g_s3d_district;
    g_s3d_district = (uint8_t)demo_city_map_travel();
    if (g_s3d_district == 0) {                  /* stayed: back to the old district */
        g_s3d_district = old;
        fr_redraw();
        first = 0;
    } else {
        g_s3d_district--;
    }
    gr_fade(200, 0, (int16_t)-g_music_level);
    if (g_sound_on != 0)
        stop_music();

    /* 303F:7922 entry video */
    if ((int8_t)s3d_random(100) <= video_chance(10)) {
        const char *v = district_str((g_q3d_opt_flags & 1) ? 0x0C : 0x08);
        if (v)
            play_district_video(v, saved_win);
    }
    ui_init_voices();
    s3d_load_district(g_s3d_district, first);
    s3d_enter_view();
    g_s3d_cancel_move = 1;
    g_s3d_view_dirty = 0;
}

/*
 * s3d_options_menu - the options list (seçenekler): walking/door
 * animation, battle speed 0..9, game music, battle music, sound effects,
 * difficulty, view size.  Each choice toggles (or cycles) its byte at
 * BB34 + n - 1 and applies it; the list is shown again until cancelled.
 * The argument is not used (the original tests it and does nothing).
 *
 * Original: IST.EXE 303F:7A71 (3DSYS.L), far cdecl (char mode).
 * Verified: tests/py/test_s3d_menu.py (every entry, both view sizes).
 */
void s3d_options_menu(uint8_t mode)
{
    int16_t i, ml, pl;
    (void)mode;

    do {
        sprintf(DGSTR(UI_LIST_ENTRY(1)), DGSTR(g_s3d_opt(6) ? 0x459F : 0x45B3),
                DGSTR(g_s3d_opt(0) ? 0x45C9 : 0x45CD));
        /* battle speed: the names come from a local copy of the 10 far
           pointers at 3EEA (F_SCOPY at entry; the table is constant) */
        sprintf(DGSTR(UI_LIST_ENTRY(2)), DGSTR(0x45D1), DGSTR(DG16(0x3EEA + 4 * g_s3d_opt(1))));
        sprintf(DGSTR(UI_LIST_ENTRY(3)), DGSTR(0x45E1), DGSTR(g_s3d_opt(2) ? 0x45F4 : 0x45F9));
        sprintf(DGSTR(UI_LIST_ENTRY(4)), DGSTR(0x4600), DGSTR(g_s3d_opt(3) ? 0x4613 : 0x4618));
        sprintf(DGSTR(UI_LIST_ENTRY(5)), DGSTR(0x461F), DGSTR(g_s3d_opt(4) ? 0x4634 : 0x4639));
        sprintf(DGSTR(UI_LIST_ENTRY(6)), DGSTR(0x4640), DGSTR(g_s3d_opt(5) ? 0x4656 : 0x465B));
        sprintf(DGSTR(UI_LIST_ENTRY(7)), DGSTR(0x4661), DGSTR(g_s3d_opt(6) ? 0x466F : 0x4675));
        for (i = 1; i < 8; i++)
            grt_decode_turkish_lower(DGSTR(UI_LIST_ENTRY(i)));
        ui_event_loop(7, 3, -1, g_ui_col_text, g_ui_col_initial, 0, 7, 2);

        if (g_ui_list_sel != 0)
            DG8(0xBB33 + g_ui_list_sel)++;
        /* NB: with nothing chosen (0) this masks BB33 itself (303F:7C72) */
        if (g_ui_list_sel - 1 == 1)
            DG8(0xBB33 + g_ui_list_sel) %= 10;
        else
            DG8(0xBB33 + g_ui_list_sel) &= 1;

        switch (g_ui_list_sel - 1) {
        case 0:                                 /* animations */
            if (g_s3d_opt(6) == 0) {
                g_q3d_anim_speed = g_s3d_opt(0) ? 9 : 0;
                g_q3d_anim_turn = 1;
            } else {
                g_q3d_anim_speed = 0;
                g_q3d_anim_turn = g_s3d_opt(0) ? 1 : 0;
            }
            break;
        case 2:                                 /* game music */
            if (g_sound_on == 0)
                break;
            ml = g_music_level;
            gr_fade(300, 0, (int16_t)-g_music_level);
            if (g_music_track == 1) {
                if (g_s3d_opt(2))
                    demo_play_music(g_music_track, 1, 0);
                else if (g_sound_on != 0)
                    stop_music();
            } else if (g_s3d_opt(2) || g_s3d_opt(4)) {
                demo_play_music(g_music_track, 1, 0);
            } else if (g_sound_on != 0) {
                stop_music();
            }
            gr_fade(300, 0, ml);
            break;
        case 6:                                 /* view size */
            if (g_s3d_opt(6) != 0) {
                g_q3d_win_w = 200;
                g_q3d_win_h = 200;
                g_q3d_anim_speed = 0;
                g_q3d_anim_turn = 1;
                g_s3d_opt(0) = 1;
                s3d_draw_party_panel();
                s3d_redraw_view(1);
                break;
            }
            g_q3d_win_w = 100;
            g_q3d_win_h = 100;
            gr_cursor_hide();
            for (i = 0; i < 4; i++)
                g_s3d_ba89(i) = 1;
            pl = g_pal_level;
            gr_fade(100, (int16_t)-g_pal_level, 0);
            mm_copy_block(mm_ref(0xE51F), mm_ref(0xE29F));      /* slot 50h -> screen */
            gr_cursor_set_shape((uint8_t)((int16_t (*)(void))ui_far_fn(DG32(UI_CB_SHAPE)))());
            g_q3d_opt_flags |= 0x10;
            s3d_draw_status_line(0xC6, 0xB2);
            s3d_draw_party_panel();
            fr_draw_money();
            g_q3d_opt_flags &= ~0x10;
            gr_call_then_cursor(s3d_draw_portrait_hl);
            s3d_redraw_view(1);
            gr_fade(100, pl, 0);
            g_q3d_anim_speed = g_s3d_opt(0) ? 9 : 0;
            break;
        }
    } while (g_ui_list_sel != 0);
}

/* ---- _dos_findfirst / _dos_findnext (0000:112F / 1162) ------------------
   Borland's struct find_t is 2Bh bytes; the menu keeps 20 of them on the
   stack and copies each into the next before _dos_findnext.  The port
   lists the game directory itself: names matching "*.SAV" (8.3, any
   case), upper-cased as DOSBox reports them, in sorted order (the order
   DOSBox-X gives for a host folder on NTFS). */
#define FIND_T_SIZE 0x2B
#define FIND_NAME   0x1E

static char  find_names[64][13];
static int   find_count, find_pos;

static int name_cmp(const void *a, const void *b) { return strcmp(a, b); }

static int16_t find_fill(uint8_t *ff)
{
    if (find_pos >= find_count)
        return 0x12;                            /* no more files */
    memset(ff, 0, FIND_T_SIZE);
    ff[0x15] = 0x20;                            /* attrib: archive */
    strcpy((char *)ff + FIND_NAME, find_names[find_pos++]);
    return 0;
}

static int16_t s3d_findfirst(const char *pattern, uint8_t *ff)
{
    (void)pattern;                              /* always "*.sav" (467B) */
    find_pos = 0;
    find_count = pc_list_files("SAV", find_names, 64);
    qsort(find_names, (size_t)find_count, sizeof find_names[0], name_cmp);
    return find_fill(ff);
}

static int16_t s3d_findnext(uint8_t *ff) { return find_fill(ff); }

/*
 * s3d_save_load_menu - list the save files (*.sav, their 60-byte
 * descriptions) and save (mode != 0: plus a "[bosch]" free slot; asks for
 * a new description) or load (mode 0) the chosen one.  Returns 1 when a
 * game was saved or loaded.
 *
 * Original: IST.EXE 303F:7F2F (3DSYS.L), far cdecl (int mode); returns AL.
 * Verified: tests/py/test_s3d_menu.py (load, save, cancel, no files).
 */
uint16_t s3d_save_load_menu(int16_t mode)
{
    /* [bp-3CAh], never initialised: s3d_write_save writes all 60 bytes,
       also those after the NUL, so the save file holds the stack's old
       contents.  This frame lies ~1 KB below the exploration loop's, a
       region only this menu reaches: on a fresh stack the bytes are 0
       (DOSBox-X play-through, docs/VERIFY.md), later they are what the
       previous call left there.  Modelled as a static buffer (0 at start,
       kept between calls); the native stack held host garbage. */
    static uint8_t desc[60];
    uint8_t ff[20][FIND_T_SIZE];                /* [bp-38Eh] */
    int16_t k = 0, rc, h, sel;
    char *name;
    uint16_t i;

    rc = s3d_findfirst(DGSTR(0x467B), ff[0]);
    while (rc == 0 && k < 20) {
        h = vfs_open((char *)ff[k] + FIND_NAME, 2);
        if (h == -1)
            o559A_fatal(DGSTR(0x4681), 0x0B24, DGSTR(0x4690), -1);
        vfs_read(h, DGSTR(UI_LIST_ENTRY(k + 1)), 0x3C);
        vfs_close(h);
        k++;
        if (k < 20) {
            memcpy(ff[k], ff[k - 1], FIND_T_SIZE);
            rc = s3d_findnext(ff[k]);
        } else {
            /* 303F:7FD3: the original copies into ff[20] (the slot past
               the array) and calls findnext on it; the loop ends anyway */
            rc = 0;
        }
    }
    if (mode != 0 && k != 20) {
        sprintf(DGSTR(UI_LIST_ENTRY(k + 1)), "%s", DGSTR(0x46AD));     /* "[bosch]" */
        ff[k][FIND_NAME] = 0;
    } else {
        k--;
    }

    if (k >= 0) {
        g_ui_list_sel = g_s3d_save_sel;
        ui_event_loop((int16_t)(k + 1), 2, -1, g_ui_col_initial, DG8(0x9664), g_ui_col_text,
                      (int16_t)((uint16_t)g_ui_panel_h / 0x0B), 1);
        if (g_ui_list_sel != 0)
            g_s3d_save_sel = g_ui_list_sel;
        ctl_flush_keys();
        ui_far_fn(DG32(0x9654))();
    } else if (mode == 0) {
        ui_message_box(DGSTR(0x46B5), 1, 0);
        ui_wait_click_or_key(0x28);
        ui_restore_background();
        return 0;
    }

    if (g_ui_list_sel == 0)
        return 0;
    sel = g_ui_list_sel;
    name = (char *)ff[sel - 1] + FIND_NAME;
    if (*name == 0)
        sprintf(name, DGSTR(0x46CC), (uint16_t)sel);       /* sakla%03u.sav */
    if (mode == 0) {
        s3d_read_save(name);
        return 1;
    }

    ui_message_box(DGSTR(0x46DA), 1, 0);
    strcpy((char *)desc, DGSTR(UI_LIST_ENTRY(sel)));
    if (mm_ref(0xE557) != 0)                    /* slot 57h must be free */
        o559A_fatal(DGSTR(0x46ED), 0x0B45, 0, 0x4D);
    mm_change_type(mm_ref(0xE357), 1);
    mm_surface_alloc(0x57, 0x13B, 0x0C, 0);
    sys_blit(5, 0xA8, (uint16_t)g_surf_w(0x57), (uint16_t)g_surf_h(0x57), 0, 0, 0, 0x57,
             sys_rect_copy);
    if (grt_input_line((char *)desc, 1, g_ui_col_initial, 0, 0x12C, 0x0A, 0xA8, 0, 5, 0,
                       0x57) != 0) {
        ctl_free_checked(0xE557, 0x0B4B, DGSTR(0x46FC));
        mm_change_type(mm_ref(0xE357), 0);
        s3d_write_save(name, (char *)desc);
        ui_restore_background();
        for (i = 0; strlen(name) > i; i++)
            grt_tolower_inplace(name + i);
        sprintf((char *)desc, DGSTR(0x470B), name);
        ui_message_box((char *)desc, 1, 0);
        ui_wait_click_or_key(0x50);
        ui_restore_background();
        return 1;
    }
    /* cancelled: NB slot 17h keeps type 1 (no mm_change_type back) */
    ctl_free_checked(0xE557, 0x0B58, DGSTR(0x473B));
    ui_restore_background();
    ui_message_box(DGSTR(0x474A), 1, 0);
    ui_wait_click_or_key(0x50);
    ui_restore_background();
    return 0;
}

/* one fade step of a (step byte, word counter) pair: the step's sign is
   forced to the counter's sign (303F:83AF), the counter moves towards 0 */
static void fade_sign(int16_t count, int8_t *step)
{
    if (count > 0) {
        if (*step < 0)
            *step = (int8_t)-*step;
    } else if (*step > 0) {
        *step = (int8_t)-*step;
    }
}

static int16_t fade_count(int16_t count, int8_t step)
{
    count = (int16_t)(count - step);
    if (step > 0)
        return count > 0 ? count : 0;
    return count < 0 ? count : 0;
}

/*
 * s3d_apply_sound_options - timed fades, at most once per 100 ms: the
 * brightness (3D70 counter, 3D75 step) and music volume (3D72, 3D76; only
 * with music 1) are faded by gr_fade, the tempo (3D6E, 3D74) is moved by
 * ampSetTempo.  Run as the UI idle callback (window record +20h).
 *
 * Original: IST.EXE 303F:837A (3DSYS.L), far cdecl (the idle callback's
 * far argument is ignored).
 * Verified: tests/py/test_s3d_menu.py (random counters/steps/clock).
 */
void s3d_apply_sound_options(void)
{
    if ((uint16_t)((uint16_t)g_clock_ms - g_s3d_fade_time) < 100)
        return;
    g_s3d_fade_time = (uint16_t)g_clock_ms;
    if (g_fade_volume_d != 0 || g_fade_bright_d != 0) {
        fade_sign(g_fade_volume_d, &g_s3d_fade_vol_step);
        fade_sign(g_fade_bright_d, &g_s3d_fade_pal_step);
        if (g_music_track != 1)
            g_fade_volume_d = 0;
        gr_fade(0, g_fade_bright_d ? g_s3d_fade_pal_step : 0,
                g_fade_volume_d ? g_s3d_fade_vol_step : 0);
        if (g_fade_volume_d != 0)
            g_fade_volume_d = fade_count(g_fade_volume_d, g_s3d_fade_vol_step);
        if (g_fade_bright_d != 0)
            g_fade_bright_d = fade_count(g_fade_bright_d, g_s3d_fade_pal_step);
    }
    if (g_s3d_fade_tempo == 0)
        return;
    fade_sign(g_s3d_fade_tempo, &g_s3d_fade_tempo_step);
    if (g_sound_on != 0 && g_music_track == 1)
        ampSetTempo((int32_t)(int16_t)((uint16_t)(g_sfx_level + g_s3d_fade_tempo) << 8));
    g_s3d_fade_tempo = fade_count(g_s3d_fade_tempo, g_s3d_fade_tempo_step);
    g_sfx_level = (int16_t)(g_sfx_level + g_s3d_fade_tempo_step);
}

/* the idle-callback shape: void far (*)(void far *arg) */
static void apply_sound_options_idle(uint32_t arg)
{
    (void)arg;
    s3d_apply_sound_options();
}

/* far code addresses of this file's routines (overlay thunks 1670:xxxx)
   that INTRFACE stores in window records / calls directly */
void s3d_register_far_fns_menu(void)
{
    ui_register_far_fn(0x1670, 0x009D, (UiFarFn)apply_sound_options_idle);
    ui_register_far_fn(0x303F, 0x837A, (UiFarFn)apply_sound_options_idle);
}
