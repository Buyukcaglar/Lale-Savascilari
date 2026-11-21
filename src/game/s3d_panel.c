/*
 * s3d_panel.c - 3DSYS.L part 4 (IST.EXE ovl303F:2C69-3DCF and 75EF): the
 * party panel at the right of the exploration screen, the UI pictures,
 * the game clock and day/night.  See s3d.h.
 *
 * Party records (gameA.md 2.3): member p = 1..4 at 99A6 + 5Ch * p; the
 * panel's row i = p - 1.  Panel state per row, 11h bytes at BA88 + 11h*i:
 *   +0  u8  portrait state: FFh dead, 0 unconscious, 1 fine
 *   +1  u8  redraw the portrait
 *   +2  u16 x 3  status icon shown in icon slot 1..3 (BA88 + 2j, overlaps
 *               the two bytes above for j = 0, unused)
 *   +8  u16 health shown, +0Ah max health shown, +0Ch bar 2, +0Eh bar 1
 *   +10h u8  bit 0: weapon-in-use marker drawn
 */
#include "game/s3d.h"

#include <stdio.h>
#include <string.h>

#include "engine/blit386.h"
#include "engine/fileman.h"
#include "engine/grtext.h"
#include "engine/intrface.h"
#include "engine/memman.h"
#include "engine/ngraphs.h"
#include "engine/systems.h"
#include "game/s3d_ext.h"
#include "rtl/bc_rand.h"

#define REC(p, off)     DGS16(0x99A6 + 0x5C * (p) + (off))  /* party record word */
/* panel text colours (UI module DGROUP, not named in intrface.h) */
#define g_ui_col_9669   DG8(0x9669)     /* shadow of the panel numbers / names */
#define g_ui_col_966A   DG8(0x966A)     /* panel numbers / names */
#define PANEL(i, off)   (0xBA88 + 0x11 * (i) + (off))        /* panel state byte offset */

/* random(n) = (long)rand() * n / 8000h */
static int16_t random_n(int16_t n)
{
    return (int16_t)((int32_t)bc_rand() * n / 0x8000);
}

/* bar height of value v: v / (1100 / 24), clamped to 1..1100 (303F:3689) */
static int16_t bar_height(int16_t v)
{
    const int16_t full = 0x44C, rows = 0x18;
    int16_t q = (int16_t)(v / (full / rows));
    if ((q > full ? full : q) < 1)
        return 1;
    return q > full ? full : q;
}

/* one of the two bars (1: x 134h, record +22h; 2: x 12Ch, record +2Ah) */
static void draw_bar(int16_t i, int16_t x, int16_t v)
{
    int16_t n = bar_height(v);
    sys_fill_rect((uint16_t)x, (uint16_t)(i * 0x26 + 9), 7, (uint16_t)(0x18 - n), 0, 0);
    sys_blit(v > 500 ? 0xB5 : 0xAE, 0, 7, (uint16_t)n, (uint16_t)x,
             (uint16_t)(i * 0x26 + 0x21 - n), 0x4E, 0, sys_rect_copy);
}

/* a number ("%3d") on the panel, shadowed */
static void draw_number(int16_t i, int16_t x, int16_t v, uint16_t fmt)
{
    char buf[0x1E];
    sprintf(buf, DGSTR(fmt), (int)v);
    sys_blit(0x10E, 0x1C, 0xE, 6, (uint16_t)x, (uint16_t)(i * 0x26 + 0x1C), 0x50, 0, sys_rect_copy);
    grt_draw_text_alt(0, x, (int16_t)(i * 0x26 + 0x1D), buf, g_ui_col_9669);
    grt_draw_text_alt(0, x, (int16_t)(i * 0x26 + 0x1C), buf, g_ui_col_966A);
}

/*
 * s3d_draw_party_panel - update the party panel: per member the three
 * status icons (from picture 4Bh), the weapon marker, the portrait
 * (picture 47h + i; it jitters by one pixel at random while the party
 * moves), darkened when unconscious / dead, the name, health and maximum
 * health and two bars.  Only what changed is drawn, everything while 3D5E
 * bit 10h is set (whole screen being built).  Also stores the derived
 * value of iso_calc_stat_a (D5EE) in record +5Ah.  Ends with the cursor
 * shape of the window record and fr_draw_money.
 *
 * Original: IST.EXE 303F:2C69 (3DSYS.L), far cdecl.  The text style is a
 * switch on the constant 2 (outlined, grt_draw_text_alt).
 * Verified: tests/py/test_s3d_panel.py (test_party_panel).
 */
void s3d_draw_party_panel(void)
{
    int16_t i, j, v;

    g_s3d_pt_x0 = 0xC5;               /* portrait position: x, y, step x, step y */
    g_s3d_pt_y0 = 2;
    g_s3d_pt_dx = 0;
    g_s3d_pt_dy = 0x26;
    s3d_draw_portrait();                /* mouse zones */
    gr_cursor_hide();
    for (i = 0; i < 4; i++) {
        /* status icons: record word (+3Ah + 2j) is the index of a status word */
        for (j = 1; j < 4; j++) {
            v = (int16_t)(REC(i + 1, 2 * REC(i + 1, 0x3A + 2 * j)) % 100);
            if (v == 0x2B)
                v = 0x22;
            else if (v > 0x20)
                v += 2;
            if (DGS16(PANEL(i, 2 * j)) == v && !(g_q3d_opt_flags & 0x10))
                continue;
            if (j == 2)
                DG8(PANEL(i, 0x10)) &= 0xFE;
            if (v != 0) {
                sys_fill_rect((uint16_t)((j - 1) * 0x11 + 0xE7), (uint16_t)(i * 0x26 + 2), 0xF, 0xF,
                              0, g_ui_col_shadow);
                sys_blit((uint16_t)((v - 1) * 0xF), 0, 0xF, 0xF, (uint16_t)((j - 1) * 0x11 + 0xE7),
                         (uint16_t)(i * 0x26 + 2), 0x4B, 0, asm_copy_rect_key);
                DGS16(PANEL(i, 2 * j)) = v;
            } else {
                sys_blit((uint16_t)((j - 1) * 0x11 + 0xE7), (uint16_t)(i * 0x26 + 2), 0xF, 0xF,
                         (uint16_t)((j - 1) * 0x11 + 0xE7), (uint16_t)(i * 0x26 + 2), 0x50, 0,
                         sys_rect_copy);
                DGS16(PANEL(i, 2 * j)) = 0;
            }
        }
        /* weapon marker: item table word E81 + 0Eh * item == 2 */
        v = (int16_t)(REC(i + 1, 2 * REC(i + 1, 0x3C)) % 100);
        if (v == 0x2B)
            v = 0x1A;
        if (DGS16(0xE81 + 0xE * v) == 2) {
            if (!(DG8(PANEL(i, 0x10)) & 1) && DG8(0x5BC2) == 0) {
                gr_draw_strip(0xF8, (uint16_t)(i * 0x26 + 2), 0xF, 0xF, 0, 0xFD);
                DG8(PANEL(i, 0x10)) |= 1;
            }
        } else {
            DG8(PANEL(i, 0x10)) &= 0xFE;
        }
    }
    for (i = 0; i < 4; i++) {
        int16_t hp = REC(i + 1, 0x3A);
        int16_t jx, jy;                 /* [bp-8], [bp-0Ah] portrait jitter */
        uint8_t *state = DGP(PANEL(i, 0));
        uint8_t *redraw = DGP(PANEL(i, 1));

        if (hp < -5 && *state != 0xFF) {
            *state = 0xFF;
            *redraw = 1;
        }
        if (hp > -6 && hp <= 0 && *state != 0) {
            *state = 0;
            *redraw = 1;
        }
        if (hp > 0 && *state != 1) {
            *state = 1;
            *redraw = 1;
        }
        if (g_q3d_opt_flags & 0x10)
            *redraw = 1;
        if (*state == 1 && (g_s3d_x != g_s3d_prev_x || g_s3d_y != g_s3d_prev_y)
            && random_n(3) == 0) {
            jx = (int16_t)((int32_t)bc_rand() * 2 / 0x8000);
            jy = (int16_t)((int32_t)bc_rand() * 2 / 0x8000);
            *redraw = 1;
        } else {
            jy = 0;
            jx = 0;
        }
        if (*redraw != 0 && !(DG8(0x5BC2) != 0 && *state != 1)) {
            int16_t ax = (int16_t)(jx < 0 ? -jx : jx);
            sys_blit((uint16_t)(jx >= 0 ? jx : 0), (uint16_t)(jy >= 0 ? jy : 0),
                     (uint16_t)(0x20 - (ax < 0 ? 0 : ax)), 0x1F,
                     (uint16_t)(g_s3d_pt_x0 + g_s3d_pt_dx * i - (jx < 0 ? jx : 0)),
                     (uint16_t)(g_s3d_pt_y0 + g_s3d_pt_dy * i - (jy < 0 ? jy : 0) + 1),
                     (uint8_t)(i + 0x47), 0, sys_rect_copy);
        } else if (*redraw != 0) {
            *state = 1;
        }
        if (*state == 0xFF && *redraw != 0) {
            if (DG8(0x5BC2) == 0) {
                gr_draw_strip((uint16_t)(g_s3d_pt_x0 + g_s3d_pt_dx * i),
                              (uint16_t)(g_s3d_pt_y0 + g_s3d_pt_dy * i + 1), 0x20, 0x1F, 0, 0xFD);
                gr_draw_strip((uint16_t)(g_s3d_pt_x0 + g_s3d_pt_dx * i),
                              (uint16_t)(g_s3d_pt_y0 + g_s3d_pt_dy * i + 1), 0x20, 0x1F, 0, 0xD9);
            } else {
                *state = 1;
            }
        }
        if (*state == 0 && *redraw == 1) {
            if (DG8(0x5BC2) == 0)
                gr_draw_strip((uint16_t)(g_s3d_pt_x0 + g_s3d_pt_dx * i),
                              (uint16_t)(g_s3d_pt_y0 + g_s3d_pt_dy * i + 1), 0x20, 0x1F, 0, 0xD9);
            else
                *state = 1;
        }
        if (*state != 0xFF) {
            g_party_cur = (int16_t)(i + 1);
            iso_calc_stat_a();
            REC(g_party_cur, 0x5A) = DGS16(0xD5EE);
            if (g_q3d_opt_flags & 0x10) {
                grt_draw_text_alt(0, 0xE7, (int16_t)(i * 0x26 + 0x15), DGSTR(0x9CBA + 0x94 * i),
                                  g_ui_col_9669);
                grt_draw_text_alt(0, 0xE7, (int16_t)(i * 0x26 + 0x14), DGSTR(0x9CBA + 0x94 * i),
                                  g_ui_col_966A);
            }
            if (REC(i + 1, 0x3A) != DGS16(PANEL(i, 8)) || (g_q3d_opt_flags & 0x10)) {
                draw_number(i, 0xF2, REC(i + 1, 0x3A), 0x4264);
                DGS16(PANEL(i, 8)) = REC(i + 1, 0x3A);
            }
            if (REC(i + 1, 0x38) != DGS16(PANEL(i, 0xA)) || (g_q3d_opt_flags & 0x10)) {
                draw_number(i, 0x10E, REC(i + 1, 0x38), 0x4268);
                DGS16(PANEL(i, 0xA)) = REC(i + 1, 0x38);
            }
            if (REC(i + 1, 0x22) != DGS16(PANEL(i, 0xE)) || (g_q3d_opt_flags & 0x10)) {
                draw_bar(i, 0x134, REC(i + 1, 0x22));
                DGS16(PANEL(i, 0xE)) = REC(i + 1, 0x22);
            }
            if (REC(i + 1, 0x2A) != DGS16(PANEL(i, 0xC)) || (g_q3d_opt_flags & 0x10)) {
                draw_bar(i, 0x12C, REC(i + 1, 0x2A));
                DGS16(PANEL(i, 0xC)) = REC(i + 1, 0x2A);
            }
        }
        *redraw = 0;
    }
    gr_cursor_set_shape((uint8_t)((int16_t (*)(void))ui_far_fn(DG32(UI_CB_SHAPE)))());
    fr_draw_money();
}

/*
 * s3d_stub_392D - put the party at cell (9, 9).  Unreferenced debug
 * helper.
 *
 * Original: IST.EXE 303F:392D (3DSYS.L), far cdecl.
 * Verified: by inspection (two stores).
 */
void s3d_stub_392D(void)
{
    g_s3d_x = 9;
    g_s3d_y = 9;
}

/*
 * s3d_load_ui_images - load the exploration interface pictures (called by
 * main): pcmainth.cmp (50h; its compass part 32h,46h copied into a new
 * 94x80 surface 15h), trade (53h), merdim (51h), viewbuyu (4Dh), viewpc
 * (4Ch), itembank (4Bh), disdunya (4Eh).
 *
 * Original: IST.EXE 303F:394A (3DSYS.L), far cdecl.  The blit size is
 * read from slot 15h's record (E343/E345) after the allocation.
 * Verified: tests/py/test_s3d_panel.py (test_load_ui_images).
 */
void s3d_load_ui_images(void)
{
    fm_load_cmp(1, DGSTR(0x426C), 0x50);
    mm_surface_alloc(0x15, 0x5E, 0x50, 1);
    sys_blit(0x32, 0x46, (uint16_t)g_surf_w(0x15), (uint16_t)g_surf_h(0x15), 0, 0, 0x50, 0x15,
             sys_rect_copy);
    fm_load_cmp(1, DGSTR(0x4279), 0x53);
    fm_load_cmp(1, DGSTR(0x4283), 0x51);
    fm_load_cmp(1, DGSTR(0x428E), 0x4D);
    fm_load_cmp(1, DGSTR(0x429B), 0x4C);
    fm_load_cmp(1, DGSTR(0x42A6), 0x4B);
    fm_load_cmp(1, DGSTR(0x42B3), 0x4E);
}

/* one timed-status list (5 words, word 5 is the last): count down by
   minutes * 100 and drop the entries that run out */
static void tick_list(uint16_t base, int16_t minutes)
{
    int16_t s, k, v;
    for (s = 1; s <= 5; s++) {
        v = (int16_t)(DGS16(base + 2 * s) - (int16_t)(minutes * 100));
        if (v < 0)
            v = 0;
        if (v != 0) {
            DGS16(base + 2 * s) = v;
        } else {
            for (k = (int16_t)(s + 1); k <= 5; k++)
                DGS16(base + 2 * k - 2) = DGS16(base + 2 * k);
            DGS16(base + 10) = 0;
        }
    }
}

/*
 * s3d_advance_clock - let `minutes` of game time pass: minute 3D64, hour
 * 3D66, day 3D68 (mod 360), and count down the timed statuses of the four
 * members (record words +2Eh.. at 99D2 + 5Ch*p, and the battle copies at
 * A45E + 28h*p).
 *
 * Original: IST.EXE 303F:39FD (3DSYS.L), far cdecl (int minutes).  It has
 * no return value (AX is left at the runtime data segment by the last ES
 * load; no caller uses it); the port returns 0.
 * Verified: tests/py/test_s3d_panel.py (test_advance_clock).
 */
int16_t s3d_advance_clock(int16_t minutes)
{
    int16_t p;

    DGS16(0x3D64) += minutes;
    DGS16(0x3D66) += (int16_t)(DGS16(0x3D64) / 60);
    DGS16(0x3D68) += (int16_t)(DGS16(0x3D66) / 24);
    DGS16(0x3D64) = (int16_t)(DGS16(0x3D64) % 60);
    DGS16(0x3D66) = (int16_t)(DGS16(0x3D66) % 24);
    DGS16(0x3D68) = (int16_t)(DGS16(0x3D68) % 360);
    /* the original interleaves the two lists entry by entry; they are
       disjoint, so list by list is the same */
    for (p = 1; p <= 4; p++) {
        tick_list((uint16_t)(0x99D2 + 0x5C * p), minutes);
        tick_list((uint16_t)(0xA45E + 0x28 * p), minutes);
    }
    return 0;
}

/*
 * s3d_calc_daylight - the state bits 3D5E as they should be now: bit 0
 * night (hour < 7 or > 17), bit 1 the party's cell has a roof (cell byte
 * 8), bit 3 cell byte 10 set; bit 4 cleared.
 *
 * Original: IST.EXE 303F:3BF3 (3DSYS.L), far cdecl, returns AX.
 * Verified: tests/py/test_s3d_panel.py (test_daylight).
 */
uint16_t s3d_calc_daylight(void)
{
    uint16_t f = g_q3d_opt_flags;
    int16_t off = (int16_t)((int16_t)(g_s3d_x + (int16_t)(g_s3d_y * g_s3d_map_w)) * 15);

    if (DGS16(0x3D66) < 7 || DGS16(0x3D66) > 0x11)
        f |= 1;
    else
        f &= 0xFFFE;
    if (s3d_map[(int32_t)(int16_t)(off + 8)] != 0)
        f |= 2;
    else
        f &= 0xFFFD;
    if (s3d_map[(int32_t)(int16_t)(off + 0xA)] != 0)
        f |= 8;
    else
        f &= 0xFFF7;
    return f & 0xFFEF;
}

/*
 * s3d_apply_daylight - follow a change of s3d_calc_daylight: adjust the
 * pending palette / music changes 3D70 / 3D72 and the music tempo offset
 * 3D6E for entering / leaving a roofed cell (bit 1) and for night / day
 * (bit 0, also 3D6D bit 1 = night sky), then store the new bits.  While
 * the screen is being built (bit 10h) the full levels are added.
 *
 * Original: IST.EXE 303F:3CCE (3DSYS.L), far cdecl.
 * Verified: tests/py/test_s3d_panel.py (test_daylight).
 */
void s3d_apply_daylight(void)
{
    uint16_t f = s3d_calc_daylight();

    if (g_q3d_opt_flags == f)
        return;
    if ((f ^ g_q3d_opt_flags) & 2) {
        if (f & 2) {                    /* under a roof */
            DGS16(0x3D6E) += 0x1E;
            DGS16(0x3D72) += 10;
            if (g_q3d_opt_flags & 1)
                DGS16(0x3D70) += 5;
            else
                DGS16(0x3D70) -= 0x14;
        } else {
            DGS16(0x3D6E) -= 0x1E;
            DGS16(0x3D72) -= 5;
            if (g_q3d_opt_flags & 1) {
                DGS16(0x3D70) -= 5;
                DGS16(0x3D72) -= 5;
            } else {
                DGS16(0x3D70) += 0x14;
            }
        }
        g_q3d_opt_flags = (uint16_t)((g_q3d_opt_flags & 0xFFFD) | (f & 2));
    }
    if ((f ^ g_q3d_opt_flags) & 1) {
        if (f & 1) {                    /* night falls */
            if (!(g_q3d_opt_flags & 2)) {
                DGS16(0x3D72) -= 0x14;
                DGS16(0x3D70) -= 0x19;
            }
            DG8(0x3D6D) |= 2;
        } else {
            if (!(g_q3d_opt_flags & 2)) {
                DGS16(0x3D72) += 0x14;
                DGS16(0x3D70) += 0x19;
            }
            DG8(0x3D6D) &= 0xFD;
        }
        g_q3d_opt_flags = (uint16_t)((g_q3d_opt_flags & 0xFFFE) | (f & 1));
    }
    if (g_q3d_opt_flags & 0x10) {
        DGS16(0x3D70) += g_pal_level_max;
        DGS16(0x3D72) += (int16_t)(g_music_level_max - 0x1E);
    }
    g_q3d_opt_flags = f;
}

/*
 * s3d_draw_portrait_hl - the cursor-shape callback of the exploration
 * screen (window record UI_CB_SHAPE): re-creates the mouse zones when the
 * view window changed size since they were set (zone 6's left edge is 30h
 * for the 100-wide view, 0 otherwise), then returns the cursor shape for
 * the zone under the cursor: 2Eh-31h over the view thirds / lower half
 * (zones 6-9), the panel shape g_ui_shape elsewhere.
 *
 * Original: IST.EXE 303F:75EF (3DSYS.L), far cdecl, returns AX; reached
 * through the far pointer 1670:0084.
 * Verified: tests/py/test_s3d_panel.py (test_portrait_hl).
 */
int16_t s3d_draw_portrait_hl(void)
{
    if ((g_q3d_win_w == 100 ? 0x30 : 0) != g_zone_x1(6))
        s3d_draw_portrait();
    switch (gr_zone_under_cursor()) {
    case 6: return 0x2E;
    case 7: return 0x2F;
    case 8: return 0x30;
    case 9: return 0x31;
    default: return g_ui_shape;
    }
}

/*
 * s3d_register_far_fns - register the 3DSYS routines that are reached
 * through far code pointers (window records 9844/9848/984C, INTRFACE's
 * direct overlay-thunk calls UI_EXT_*): 1670:003E s3d_draw_party_panel,
 * 1670:0084 s3d_draw_portrait_hl, 1670:00A2 s3d_draw_portrait, and the
 * menu ones (s3d_register_far_fns_menu).  Port-only, called by main.
 */
void s3d_register_far_fns(void)
{
    ui_register_far_fn(UI_EXT_PARTY_PANEL, (UiFarFn)s3d_draw_party_panel);
    ui_register_far_fn(0x1670, 0x0084, (UiFarFn)s3d_draw_portrait_hl);
    ui_register_far_fn(0x1670, 0x00A2, (UiFarFn)s3d_draw_portrait);
    s3d_register_far_fns_menu();
}
