/*
 * s3d_loop.c - 3DSYS.L 303F:8524-9A48: the exploration main loop.
 *
 * Original: IST.EXE ovl303F (T:\CODE\3DSYS.L), Borland C far cdecl.
 */
#include <stdio.h>
#include <string.h>

#include "dsmi/amp.h"
#include "dsmi/emsheap.h"
#include "dsmi/memalloc.h"
#include "engine/fatal.h"
#include "engine/grtext.h"
#include "engine/intrface.h"
#include "engine/memman.h"
#include "engine/ngraphs.h"
#include "engine/nkontrol.h"
#include "engine/ographs.h"
#include "engine/systems.h"
#include "game/q3dview.h"
#include "game/s3d.h"
#include "game/s3d_ext.h"
#include "rtl/bc_rand.h"

/* stat word of party member p (record 0..4) at byte offset off */
#define STAT(p, off) DGS16((uint16_t)(0x99A6 + 0x5C * (p) + (off)))
#define ST_STRENGTH 0x0C
#define ST_ENDURANCE 0x12
#define ST_FAITH    0x20                /* the hunger reserve */
#define ST_HUNGER   0x22
#define ST_FATIGUE  0x2A
#define ST_HEALTH   0x3A
#define ST_5A       0x5A

/* (int)((long)rand() * n / 0x8000) with n taken as an unsigned word (the
   original zero-extends the multiplier where it is not a constant) */
static int16_t rnd_u(uint16_t n)
{
    int16_t r = bc_rand();
    return (int16_t)(((int32_t)r * (int32_t)n) / 0x8000);
}

/* the cursor-shape callback of the current window record (CALLF [9650]) */
static uint8_t cb_shape(void)
{
    return (uint8_t)((int16_t (*)(void))ui_far_fn(DG32(UI_CB_SHAPE)))();
}

/* the group size of monster type t: (random(hi nibble) + whole byte) & 15
   (303F:8F0B; rand() is called before the table is read) */
static int16_t group_size(int16_t t)
{
    int16_t r = bc_rand();
    int16_t n = (int16_t)((g_s3d_monster(t, 0) & 0xF0) >> 4);
    return (int16_t)(((int16_t)(((int32_t)r * n) / 0x8000) + g_s3d_monster(t, 0)) & 0x0F);
}

/* "zam1/2/3" consistency check of the enemy groups after each step of the
   battle set-up: a difference (memory corruption) switches to text mode
   and dumps both copies (303F:9205, 9310, 947A) */
static void check_groups(uint16_t fmt)
{
    int16_t k, j;
    for (k = 3; k >= 1; k--) {
        if (g_s3d_grp_chk_type(k) == g_s3d_grp_type(k) && g_s3d_grp_chk_count(k) == g_s3d_grp_count(k)
            && g_s3d_grp_chk_on(k) == g_s3d_grp_on(k))
            continue;
        ogr_int10(3);                   /* textmode(C80) */
        for (j = 3; j >= 1; j--)
            fatal_printf(DGSTR(fmt), g_s3d_grp_chk_type(j), g_s3d_grp_type(j), g_s3d_grp_chk_count(j),
                         g_s3d_grp_count(j), g_s3d_grp_chk_on(j), g_s3d_grp_on(j));
        ui_getch();
        k = 0;                          /* the original reuses the outer counter */
    }
}

/* a portrait blit of member m (0..3) from slot src, x offset sx (32 x 32) */
static void portrait_blit(uint16_t sx, int16_t m, uint8_t src)
{
    sys_blit(sx, 0, 0x20, 0x20, (uint16_t)(m * g_s3d_pt_dx + g_s3d_pt_x0),
             (uint16_t)(m * g_s3d_pt_dy + g_s3d_pt_y0), src, 0, sys_rect_copy);
}

/* 303F:86C2-8C8E: hunger, every 10 minutes of game time, member p */
static void hunger_tick(int16_t p, uint16_t *t0p)
{
    int16_t inc, r, t, end;
    uint16_t t0, t1;
    char buf[0x24];                     /* [bp-2Eh] */

    if (STAT(p, ST_HEALTH) <= 0)
        return;
    end = (int16_t)(STAT(p, ST_ENDURANCE) / 100);
    inc = (int16_t)(5 - end) < 1 ? 1 : (int16_t)(5 - end);
    t = (int16_t)(STAT(p, ST_HUNGER) + inc);
    STAT(p, ST_HUNGER) = t > 1000 ? 1000 : t;
    r = (int16_t)(STAT(p, ST_5A) / 2 - end);
    if (r <= 1)
        r = 1;
    if (iso_has_status((int16_t)(p + 0x18), 0x14)) {
        t = (int16_t)(STAT(p, ST_FATIGUE) + (int16_t)(r * 3));
        STAT(p, ST_FATIGUE) = t > 1000 ? 1000 : t;
    }
    if (STAT(p, ST_HUNGER) > 500) {
        t = (int16_t)(STAT(p, ST_FATIGUE) + r);
        STAT(p, ST_FATIGUE) = t > 1000 ? 1000 : t;
    }
    if (STAT(p, ST_HUNGER) < 1000)
        return;
    if (STAT(p, ST_FAITH) > 0) {
        t = (int16_t)(STAT(p, ST_FAITH) - inc);
        STAT(p, ST_FAITH) = t < 0 ? 0 : t;
    }
    if (STAT(p, ST_FAITH) > 0) {
        if (g_s3d_hungry_said & (1 << (p - 1)))
            return;
        ui_message_box(DGSTR(0x4773), 0, p);            /* "ben acıktım !" */
        g_s3d_hungry_said |= (uint8_t)(1 << (p - 1));
        ui_restore_background();
        return;
    }

    /* 303F:8A0B starved */
    ui_message_box(DGSTR(0x479C), 0, p);                /* "aah!! acıktım demiştim..." */
    ui_restore_background();
    STAT(p, ST_HEALTH) = -6;
    if (STAT(p, ST_STRENGTH) != 0) {
        sprintf(buf, DGSTR(0x47BD), STAT(p, ST_HEALTH));
        sys_blit(0x10E, 0x1C, 0x0E, 6, 0xF2, (uint16_t)(p * 0x26 + 0x1C), 0x50, 0, sys_rect_copy);
        /* switch on a constant 2 (303F:8A92): case 1 used grt_draw_text */
        grt_draw_text_alt(0, 0xF2, (int16_t)((p - 1) * 0x26 + 0x1D), buf, DG8(0x9669));
        grt_draw_text_alt(0, 0xF2, (int16_t)((p - 1) * 0x26 + 0x1C), buf, DG8(0x966A));
    }
    /* NB: this is the main loop's blink timer [bp-0Ah] */
    t0 = *t0p = (uint16_t)g_clock_ms;
    t1 = (uint16_t)g_clock_ms;
    if (gr_cursor_in_zone((uint8_t)p))
        gr_cursor_hide();
    /* the portrait flickers out for 1.5 s (or until a key / mouse button) */
    while (!ui_kbhit() && g_mouse_buttons == 0 && (uint16_t)((uint16_t)g_clock_ms - t0) < 1500) {
        if ((uint16_t)((uint16_t)g_clock_ms - t1) <= 50)
            continue;
        t1 = (uint16_t)g_clock_ms;
        if (rnd_u((uint16_t)(5 - (uint16_t)((uint16_t)g_clock_ms - t0) / 300)) != 0)
            portrait_blit(0, (int16_t)(p - 1), (uint8_t)(p + 0x46));
        else
            sys_fill_rect((uint16_t)((p - 1) * g_s3d_pt_dx + g_s3d_pt_x0),
                          (uint16_t)((p - 1) * g_s3d_pt_dy + g_s3d_pt_y0), 0x20, 0x20, 0, 0);
    }
    gr_cursor_set_shape(cb_shape());
    s3d_draw_party_panel();
    ui_restore_background();
}

/* 303F:8E5B-97FE: a random encounter - pick the groups, announce them,
   fight (iso_battle), reload the district, loot */
static void random_encounter(void)
{
    int16_t k;

    g_s3d_encounter = 0;
    k = 0;
    while (k == 0) {
        if (g_s3d_enc_kind == 2 && rnd_u(2) != 0)
            g_s3d_enc_kind = 1;
        if (g_s3d_enc_kind < 2) {
            int16_t h;
            uint8_t *p;
            mm_ems_lock(g_blit_lock, 0x0C57, DGSTR(0x47C1));
            h = (int16_t)mm_ref(0xE33F)->ems;   /* the map's PCn block */
            p = EMSLock(h, (int32_t)(int16_t)(rnd_u(4) + g_s3d_enc_kind * 4 + 0x1D), 1);
            g_s3d_grp_type(1) = *p;
            mm_ems_unlock(g_blit_lock);
            g_s3d_grp_count(1) = group_size(g_s3d_grp_type(1));
            g_s3d_grp_on(1) = 1;
            g_s3d_grp_type(2) = g_s3d_monster(g_s3d_grp_type(1), 5);
            if (g_s3d_grp_type(2) != 0 && rnd_u(3) != 0) {
                g_s3d_grp_count(2) = group_size(g_s3d_grp_type(2));
                g_s3d_grp_on(2) = 1;
                goto third;
            }
        } else {
            g_s3d_grp_type(1) = 0x1F;
            g_s3d_grp_count(1) = group_size(0x1F);
        }
        g_s3d_grp_count(2) = 0;
        g_s3d_grp_type(2) = 0;
        g_s3d_grp_on(2) = 0;
third:
        g_s3d_grp_count(3) = 0;
        g_s3d_grp_type(3) = 0;
        g_s3d_grp_on(3) = 0;
        for (k = 3; k >= 1 && g_s3d_grp_type(k) == 0; k--)
            ;
    }

    fr_show_icon((uint8_t)g_s3d_grp_type(1), 1);
    strcpy(DGSTR(0xBACC), s3d_make_random_phrase(1));
    if (strlen(DGSTR(0xBACC)) >= 0x68)
        o559A_fatal(DGSTR(0x47D0), 0x0C6E, 0, 0x4E);
    for (k = 3; k >= 1; k--) {
        g_s3d_grp_chk_type(k) = g_s3d_grp_type(k);
        g_s3d_grp_chk_count(k) = g_s3d_grp_count(k);
        g_s3d_grp_chk_on(k) = g_s3d_grp_on(k);
    }
    ui_message_box(DGSTR(0xBACC), 1, 0);
    ui_wait_click_or_key(0x3C);
    ui_restore_background();
    check_groups(0x47DF);
    q3d_free_view_buffers();
    check_groups(0x4827);
    gr_fade(1000, (int16_t)-g_pal_level, (int16_t)-g_music_level);
    if (g_sound_on != 0) {
        ampStopModule();
        if (g_music_module) {
            ampFreeModule(g_music_module);
            MemFree(g_music_module);
            g_music_module = 0;
        }
    }
    check_groups(0x486F);

    g_iso_result = 0;
    iso_battle();
    s3d_load_district(g_s3d_district, 0);
    s3d_enter_view();
    g_s3d_view_dirty = 0;
    if (g_iso_result == 2) {
        demo_game_over();
        g_s3d_view_dirty = 0;
    } else {
        int16_t j;
        g_fr_xchg_c = 0;
        g_fr_xchg_b = 0;
        g_fr_xchg_a = 0;
        for (k = 1; k <= 3; k++) {
            int16_t t;
            if (g_s3d_grp_on(k) == 0 || g_s3d_grp_type(k) >= 100)
                continue;
            t = g_s3d_grp_type(k);
            g_fr_xchg_w1 = g_s3d_monster(t, 1);
            g_fr_xchg_w2 = g_s3d_monster(t, 2);
            g_fr_xchg_w3 = g_s3d_monster(t, 3);
            {
                /* rand() first, then the table (303F:9691) */
                int16_t r = bc_rand();
                int16_t n = (int16_t)((g_s3d_monster(g_s3d_grp_type(k), 4) & 0xF0) >> 4);
                g_fr_xchg_c = (int16_t)(((int16_t)(((int32_t)r * n) / 0x8000)
                                         + g_s3d_monster(g_s3d_grp_type(k), 4)) & 0x0F);
            }
            if (rnd_u(2) != 0)
                DGS16((uint16_t)(0xBB55 + 2 * k)) = 0;  /* BB57/59/5B: drop that loot */
        }
        if (g_fr_xchg_c != 0 || g_fr_xchg_w1 != 0 || g_fr_xchg_w2 != 0 || g_fr_xchg_w3 != 0)
            fr_item_exchange(0);
        /* clear words 1..19 of the battle units 1..28 and templates 1..7 */
        for (k = 1; k < 0x14; k++) {
            for (j = 1; j < 0x1D; j++)
                DGS16((uint16_t)(0xA088 + j * 0x28 + k * 2)) = 0;
            for (j = 1; j < 8; j++)
                DGS16((uint16_t)(0x9F18 + j * 0x2E + k * 2)) = 0;
        }
    }
    for (k = 1; k < 4; k++) {
        g_s3d_grp_count(k) = 0;
        g_s3d_grp_type(k) = 0;
    }
}

/*
 * s3d_explore_loop - the exploration main loop; never returns (the game
 * ends inside demo_game_over / demo_ending / the menus).  Per pass:
 *  - keys / mouse -> s3d_handle_key; after a move: one minute of game time,
 *    the status line, leaving the map edge -> s3d_leave_district, the
 *    footstep sound, every 10 minutes hunger;
 *  - the view (or the automap);
 *  - on a new cell: FRPOS event type 2 (repeated while it returns 1), the
 *    random encounter roll (unless the district's flag is set);
 *  - party dead -> demo_game_over; event flag 9Ch -> demo_ending;
 *  - a due encounter: random_encounter;
 *  - the UI idle callback, and the portrait blink animation.
 *
 * Original: IST.EXE 303F:8524 (3DSYS.L), far cdecl.
 * Verified: tests/py/test_s3d_loop.py (scripted passes, left through the
 * idle callback).
 */
void s3d_explore_loop(void)
{
    int16_t k, p;
    uint16_t t0;                    /* [bp-0Ah] clock (low word) */
    uint8_t blink_m = 0;            /* [bp-0Fh] member that blinks */
    uint8_t blink_t = 0;            /* [bp-0Eh] extra delay (random(500)) */
    uint8_t blink = 0;              /* [bp-0Dh] a blink is due */

    gr_cursor_set_shape(g_ui_shape);
    t0 = (uint16_t)g_clock_ms;
    ctl_flush_keys();
    for (;;) {
        gr_call_then_cursor(s3d_draw_portrait_hl);
        if (ui_kbhit() || g_mouse_buttons == 1 || g_s3d_x != g_s3d_prev_x || g_s3d_y != g_s3d_prev_y) {
            s3d_handle_key();
            if (g_s3d_x != g_s3d_prev_x || g_s3d_y != g_s3d_prev_y) {
                s3d_advance_clock(1);
                s3d_draw_status_line(0xC6, 0xB2);
                if (g_s3d_x < 0 || g_s3d_map_w <= g_s3d_x || g_s3d_y < 0 || g_s3d_map_h <= g_s3d_y) {
                    s3d_leave_district();
                    g_s3d_view_dirty = 0;
                }
                if (g_s3d_opt(4) != 0) {            /* footsteps */
                    int16_t off = (int16_t)((int16_t)(g_s3d_y * g_s3d_map_w + g_s3d_x) * 15 + 9);
                    int16_t s = iso_step_sfx(s3d_map[off]);
                    ui_load_voice(s, g_snd_rate(s), 0, 0x40);
                }
                if (g_s3d_minute % 10 == 0) {
                    for (p = 1; p <= 4; p++)
                        hunger_tick(p, &t0);
                    s3d_draw_party_panel();
                }
                s3d_apply_daylight();
                s3d_redraw_screen();
            }
            /* 303F:8CA3 */
            if (g_s3d_automap == 0) {
                s3d_redraw_view(g_s3d_view_dirty);
                g_s3d_view_dirty = 0;
            } else {
                q3d_draw_automap();
            }
            if (g_s3d_x != g_s3d_prev_x || g_s3d_y != g_s3d_prev_y) {
                uint8_t f;
                s3d_draw_party_panel();
                g_q3d_opt_flags &= ~4;
                while (fr_run_event((int16_t)((int16_t)(g_s3d_y * g_s3d_map_w + g_s3d_x) * 15), 2)) {
                    g_q3d_opt_flags &= ~4;
                    s3d_draw_party_panel();
                }
                f = DG8((uint16_t)(S3D_DISTRICT_TAB + 0x1C + 0x1D * g_s3d_district));
                if (f == 0 || !fr_flag_test(DG8((uint16_t)(S3D_DISTRICT_TAB + 0x1C + 0x1D * g_s3d_district)))) {
                    if (g_q3d_opt_flags & 8) {
                        if (rnd_u(200) + 1 <= g_s3d_enc_rate)
                            g_s3d_encounter = 1;
                        g_s3d_enc_kind = 0;
                    } else {
                        if (rnd_u(300) + 1 <= g_s3d_enc_rate)
                            g_s3d_encounter = 1;
                        g_s3d_enc_kind = 1;
                    }
                }
                g_s3d_prev_x = g_s3d_x;
                g_s3d_prev_y = g_s3d_y;
                g_q3d_shake_set = 0;
            }
            /* 303F:8E05 */
            for (k = 1; k < 5 && STAT(k, ST_HEALTH) <= 0; k++)
                ;
            if (k == 5) {
                demo_game_over();
                g_s3d_view_dirty = 0;
            }
            if (fr_flag_test(0x9C))
                demo_ending();
            if (g_s3d_encounter != 0)
                random_encounter();
        }

        /* 303F:97FF idle callback, portrait blink */
        ((void (*)(uint32_t))ui_far_fn(DG32(UI_CB_IDLE)))(g_ui_idle_arg);
        if ((uint16_t)(blink_t + 500) < (uint16_t)((uint16_t)g_clock_ms - t0) && blink) {
            blink = 0;
            t0 = (uint16_t)g_clock_ms;
            if (gr_cursor_in_zone((uint8_t)(blink_m + 1)))
                gr_cursor_hide();
            portrait_blit(0, blink_m, (uint8_t)(blink_m + 0x47));
            gr_cursor_set_shape(cb_shape());
        }
        if ((uint16_t)((uint16_t)g_clock_ms - t0) <= 1000 || blink)
            continue;
        t0 = (uint16_t)g_clock_ms;
        if (rnd_u(20) != 0)
            continue;
        blink = 1;
        blink_t = (uint8_t)rnd_u(500);
        blink_m = (uint8_t)rnd_u(4);
        if (STAT(blink_m + 1, ST_HEALTH) <= 0) {
            for (k = blink_m; blink_m + 4 > k; k++)
                if (STAT(k % 4 + 1, ST_HEALTH) > 0)
                    break;
            if (blink_m + 4 == k)
                demo_game_over();
            blink_m = (uint8_t)(k % 4);
        }
        if (gr_cursor_in_zone((uint8_t)(blink_m + 1)))
            gr_cursor_hide();
        {
            /* argument order: the source x (random) is computed last */
            int16_t m = blink_m;
            uint16_t sx = (uint16_t)((rnd_u(3) + 1) << 5);
            portrait_blit(sx, m, (uint8_t)(m + 0x47));
        }
        gr_cursor_set_shape(cb_shape());
    }
}
