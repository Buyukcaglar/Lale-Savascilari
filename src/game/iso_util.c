/*
 * iso_util.c - ISOSYS.L 4595:0000-0D1A: INTRFACE window callbacks, small
 * helpers (clamp, sign, bit test, text, pictures, grid), the sprite blitter
 * and the battle graphics loader.  See iso.h.
 */
#include "game/iso.h"

#include <stdio.h>
#include <string.h>

#include "engine/blit386.h"
#include "engine/fileman.h"
#include "engine/grtext.h"
#include "engine/intrface.h"
#include "engine/memman.h"
#include "engine/ngraphs.h"
#include "engine/nkontrol.h"
#include "engine/systems.h"
#include "dsmi/emsheap.h"
#include "rtl/bc_farheap.h"
#include "game/chargen.h"

#if __has_include("game/frpos.h")
#include "game/frpos.h"
#else
/* provided by frpos (WP15) */
uint8_t fr_flag_test(int16_t n);                                          /* 3FD1:4AAC */
#endif

#define ISO_FILE(off) DGSTR(off)        /* "T:CODE\ISOSYS.L" copies for the asserts */

/*
 * iso_register_far_fns - make the ISOSYS entry thunks that the original
 * stores as far code pointers (INTRFACE window records, the idle callback
 * of the face animation) or calls through them known to INTRFACE's
 * dispatcher.  Port only; call once at start-up.
 */
void iso_turn_register(void);           /* iso_turn.c: 1694:007A */
void iso_char_register(void);           /* iso_char.c: 1694:0106 */

void iso_register_far_fns(void)
{
    ui_register_far_fn(0x1694, 0x0025, (UiFarFn)iso_clamp_add);
    ui_register_far_fn(0x1694, 0x0043, (UiFarFn)iso_wnd_cb_button);
    ui_register_far_fn(0x1694, 0x0048, (UiFarFn)iso_wnd_cb_set_layout);
    ui_register_far_fn(0x1694, 0x004D, (UiFarFn)iso_wnd_cb_picture6);
    ui_register_far_fn(0x1694, 0x0052, (UiFarFn)iso_draw_text_shadow);
    iso_turn_register();
    iso_char_register();
}

/*
 * iso_free_battle_gfx - free the battle pictures: sprite slots 19..11, the
 * mini-map work screen record E2D7 (slot 7), slot 6, then slots 2 and 1.
 *
 * Original: IST.EXE ovl4595:0000 (ISOSYS.L), far cdecl.
 * Verified: tests/py/test_iso.py (load + free, DGROUP), 2026-11-21.
 */
void iso_free_battle_gfx(void)
{
    int16_t n;
    for (n = 0x13; n >= 0x0B; n--)
        ctl_free_checked((uint16_t)(0xE29F + 8 * n), 0x9B, ISO_FILE(0x642F));
    ctl_free_checked(0xE2D7, 0x9C, ISO_FILE(0x643F));
    ctl_free_checked(0xE2CF, 0x9E, ISO_FILE(0x644F));
    for (n = 2; n >= 1; n--)
        ctl_free_checked((uint16_t)(0xE29F + 8 * n), 0xA0, ISO_FILE(0x645F));
}

/*
 * iso_wnd_cb_picture6 - window callback (moved / closed): put the work
 * screen (slot 6) on the display.
 *
 * Original: IST.EXE ovl4595:008A (ISOSYS.L), far cdecl; thunk 1694:004D.
 * Verified: by inspection (one call).
 */
void iso_wnd_cb_picture6(void)
{
    iso_show_picture(6);
}

/*
 * iso_wnd_cb_set_layout - window callback (list drawn): show slot 6 and set
 * the two hot rectangles of the list's scroll buttons (E725..E733).
 *
 * Original: IST.EXE ovl4595:0097 (ISOSYS.L), far cdecl; thunk 1694:0048.
 * Verified: tests/py/test_iso.py, 2026-11-21.
 */
void iso_wnd_cb_set_layout(int16_t col, int16_t row, int16_t h)
{
    int16_t x = (int16_t)((col - 1) * 8);
    int16_t y = (int16_t)((row + 1) * h);
    iso_show_picture(6);
    g_iso_panel_rect(0) = x;
    g_iso_panel_rect(1) = (int16_t)(y + 8);
    g_iso_panel_rect(2) = (int16_t)(x + 9);
    g_iso_panel_rect(3) = (int16_t)(y + 0x1D);
    g_iso_panel_rect(4) = x;
    g_iso_panel_rect(5) = (int16_t)(y + 0x1E);
    g_iso_panel_rect(6) = (int16_t)(x + 9);
    g_iso_panel_rect(7) = (int16_t)(y + 0x33);
}

/*
 * iso_wnd_cb_button - window callback (draw frame): the scroll-button
 * picture (slot 7) left of the list; with `hl` (low byte) also the 12x32
 * button icon from slot 4Eh.
 *
 * Original: IST.EXE ovl4595:0136 (ISOSYS.L), far cdecl; thunk 1694:0043.
 * Verified: tests/py/test_iso.py, 2026-11-21.
 */
void iso_wnd_cb_button(int16_t col, int16_t row, int16_t h, int16_t hl)
{
    iso_blit_icon(7, (int16_t)((col - 1) * 8 - 8), (int16_t)((row + 1) * h + 3));
    if ((uint8_t)hl != 0)
        sys_blit(g_ui_icon_x, 0, 0x0C, 0x20, (uint16_t)((col - 1) * 8 - 4),
                 (uint16_t)((row + 1) * h + 0x0F), 0x4E, 6, asm_copy_rect_key);
}

/*
 * iso_draw_text_shadow - text at text column `col`, row `row` (line height
 * h): first in colour 0 one pixel lower, then in `color`.  Font 2 (the
 * main font) when h == 7, else font 1.
 *
 * Original: IST.EXE ovl4595:019F (ISOSYS.L), far cdecl; thunk 1694:0052.
 * Verified: tests/py/test_iso.py, 2026-11-21.
 */
void iso_draw_text_shadow(const char *s, int16_t col, int16_t row, int16_t color, int16_t h)
{
    int16_t font = h == 7 ? 2 : 1;
    iso_draw_text(font, (int16_t)(col << 3), (int16_t)(row * h + 9), s, 0);
    iso_draw_text(font, (int16_t)(col << 3), (int16_t)(row * h + 8), s, (uint8_t)color);
}

/*
 * iso_clamp_add - *p += d, then WRAP into [lo, hi]: below lo gives hi,
 * above hi gives lo (a cyclic menu index, not a clamp).
 *
 * Original: IST.EXE ovl4595:0202 (ISOSYS.L), far cdecl; thunk 1694:0025.
 * Verified: tests/py/test_iso.py (random inputs), 2026-11-21.
 */
void iso_clamp_add(int16_t *p, int16_t d, int16_t lo, int16_t hi)
{
    *p = (int16_t)(*p + d);
    if (*p < lo)
        *p = hi;
    if (*p > hi)
        *p = lo;
}

/*
 * iso_sign - 1, -1 or 0.
 *
 * Original: IST.EXE ovl4595:0238 (ISOSYS.L), far cdecl.
 * Verified: tests/py/test_iso.py (exhaustive), 2026-11-21.
 */
int16_t iso_sign(int16_t x)
{
    if (x > 0)
        return 1;
    if (x < 0)
        return -1;
    return 0;
}

/*
 * iso_bit_test - -1 if bit n of mask is set (the loop doubles 1 n times;
 * unsigned compare, so n >= 16 gives bit 0 -> 0 after the overflow), else 0.
 *
 * Original: IST.EXE ovl4595:0255 (ISOSYS.L), far cdecl.
 * Verified: tests/py/test_iso.py (random inputs), 2026-11-21.
 */
int16_t iso_bit_test(uint16_t n, uint16_t mask)
{
    uint16_t bit = 1, i;
    /* n = FFFFh would never end (i wraps); the original hangs too */
    for (i = 1; i <= n; i++) {
        bit = (uint16_t)(bit * 2);
        if (i == 0xFFFF)
            break;
    }
    return (mask & bit) ? -1 : 0;
}

/*
 * iso_blit_icon - copy the whole of surface `slot` to the work screen
 * (slot 6) at x, y, colour key 0... (asm_copy_rect: no key).
 *
 * Original: IST.EXE ovl4595:028E (ISOSYS.L), far cdecl.
 * Verified: tests/py/test_iso.py, 2026-11-21.
 */
void iso_blit_icon(uint16_t slot, int16_t x, int16_t y)
{
    sys_blit(0, 0, (uint16_t)g_surf_w(slot), (uint16_t)g_surf_h(slot), (uint16_t)x,
             (uint16_t)y, (uint8_t)slot, 6, sys_rect_copy);
}

/*
 * iso_draw_bar - the bar picture n (50 px cells): n < 14 from slot 2
 * (43x35), else picture n-13 of slot 18 (the walls, 50x60), cut at the top
 * of the screen (y < 0) or at its right edge (E2CB = width of slot 6).
 *
 * Original: IST.EXE ovl4595:02D1 (ISOSYS.L), far cdecl.
 * Verified: tests/py/test_iso.py, 2026-11-21.
 */
void iso_draw_bar(int16_t x, int16_t y, int16_t n)
{
    uint16_t w;
    if (n < 0x0E) {
        sys_blit((uint16_t)((n - 1) * 0x32), 0, 0x2B, 0x23, (uint16_t)x, (uint16_t)y, 2, 6,
                 asm_copy_rect_key);
        return;
    }
    n = (int16_t)(n - 0x0D);
    if (y < 0) {
        sys_blit((uint16_t)((n - 1) * 0x32), (uint16_t)-y, 0x32, (uint16_t)(0x3C + y),
                 (uint16_t)x, 0, ISO_SURF_WALLS, 6, asm_copy_rect_key);
        return;
    }
    /* ovl4595:0322 - unsigned compare, so a bar left of x = -50 .. right
       edge gives a huge difference -> 50 */
    w = (uint16_t)((uint16_t)(g_surf_w(6) + 0x32) - (uint16_t)(x + 0x32));
    if (w > 0x32)
        w = 0x32;
    sys_blit((uint16_t)((n - 1) * 0x32), 0, w, 0x3C, (uint16_t)x, (uint16_t)y, ISO_SURF_WALLS,
             6, asm_copy_rect_key);
}

/*
 * iso_draw_sprite - draw frame `frame` of the sprite in slot `slot`
 * (11..17) with its anchor at (*x, *y) on the work screen.  The frame's
 * box comes from the .KOR table (slot 19 / E337: 6 frames x 6 words per
 * sprite: x1, y1, x2, y2, anchor x, anchor y).  Frames 1..8 = 8 facings:
 * 4 and 8 are 2 and 5 mirrored.  On return *x, *y = the sprite's centre
 * minus 13 (where the HP bar goes); frame >= 100 only computes that.
 *
 * Original: IST.EXE ovl4595:0387 (ISOSYS.L), far cdecl (int far *x,
 * int far *y, slot, frame).  Called by CHARGEN too.
 * Verified: tests/py/test_iso.py, 2026-11-21.
 */
void iso_draw_sprite(int16_t *x, int16_t *y, int16_t slot, int16_t frame)
{
    uint8_t mirror = 0;
    uint8_t f;
    int16_t px, py, x1, y1, w, h, off;
    const uint8_t *kor;
    MmHandle *rec;
    uint16_t at;

    mm_ems_lock(g_blit_lock, 0x104, ISO_FILE(0x646F));
    px = *x;
    py = *y;
    f = (uint8_t)((frame >= 100 ? (uint8_t)(frame - 100) : (uint8_t)frame) - 1);
    if (f == 3) {
        f = 1;
        mirror = 1;
    } else if (f > 3 && f < 7) {
        f--;
    } else if (f == 7) {
        f = 4;
        mirror = 1;
    }
    rec = mm_ref(G_ISO_KOR_REF);
    at = (uint16_t)(((slot - 0x0B) * 6 + f) * 0x0C);
    if (rec->type == 1)
        kor = EMSLock((int16_t)rec->ems, (int32_t)at, 0x0C);
    else
        kor = mm_conv_ptr(rec) + at;
    x1 = (int16_t)(kor[0] | kor[1] << 8);
    w = (int16_t)(kor[4] | kor[5] << 8);              /* x2 */
    if (mirror)
        off = (int16_t)(w + 4 - (int16_t)(kor[8] | kor[9] << 8));
    else
        off = (int16_t)((int16_t)(kor[8] | kor[9] << 8) + 4 - x1);
    px = (int16_t)(px - off);
    w = (int16_t)(w - (x1 - 1));
    y1 = (int16_t)(kor[2] | kor[3] << 8);
    h = (int16_t)(kor[6] | kor[7] << 8);              /* y2 */
    py = (int16_t)(py - ((int16_t)(kor[10] | kor[11] << 8) + 8 - y1));
    mm_ems_unlock(g_blit_lock);
    h = (int16_t)(h - (y1 - 1));
    *y = (int16_t)(py + h / 2 - 0x0D);
    *x = (int16_t)(px + w / 2 - 0x0D);
    if (py < 0) {
        y1 = (int16_t)(y1 - py);
        h = (int16_t)(h + py);
        py = 0;
        *y = 0;
    }
    if (frame < 100)
        sys_blit((uint16_t)x1, (uint16_t)y1, (uint16_t)w, (uint16_t)h, (uint16_t)px,
                 (uint16_t)py, (uint8_t)slot, 6,
                 mirror ? asm_copy_rect_key_mirror : asm_copy_rect_key);
}

/* fm_load_file into the .KOR block at byte `at`: the original points the
   block's record at block + at for the call (ovl4595:0637) and restores it
   afterwards; the record's DGROUP bytes (the far pointer) are not kept by
   the port (memman.h), so only the native pointer is moved. */
static void load_kor_at(const char *name, uint8_t *base, uint16_t at)
{
    MmHandle *rec = mm_ref(G_ISO_KOR_REF);
    mm_set_conv_ptr(rec, base + at, 4);
    fm_load_file(0, name, rec);
}

/*
 * iso_load_battle_gfx - load the battle pictures: anakes (slot 1), yericon
 * (2), tabletur (7); the work screen (6, 320x200 EMS); the party sprites
 * (slots 11..14, portrait picture names; "karafa" for all when event flag
 * 8Dh is set and 8Fh is not) and the three monster groups' sprites (15..17,
 * short names 2185 + 9*type, "<name>.cmp" twice when the name is short),
 * each with its .KOR frame table into the 1F8h-byte block of slot 19; the
 * walls (slot 18, 300x60): duvarica, duvaricb, and duvaric%d for the party
 * slots 2..5 (their wall kind and mini-map colour are set by the number);
 * finally the panel layout.
 *
 * Original: IST.EXE ovl4595:0570 (ISOSYS.L), far cdecl.
 * Verified: tests/py/test_iso.py (DGROUP, surfaces), 2026-11-21.
 */
void iso_load_battle_gfx(void)
{
    char name[0x1A];
    uint8_t *base;
    char *alt;
    MmHandle *rec;
    int16_t k, pic = 0;   /* bp-4: never set when all four are 0 (stack garbage) */

    memcpy(name, DGP(G_ISO_NAMEBUF), 0x0D);            /* F_SCOPY, CX = 0Dh */
    fm_load_cmp(1, DGSTR(0x647F), 1);                  /* anakes.cmp */
    fm_load_cmp(1, DGSTR(0x648A), 2);                  /* yericon.cmp */
    fm_load_cmp(1, DGSTR(0x6496), 7);                  /* tabletur.cmp */
    mm_surface_alloc(6, 0x140, 200, 2);
    rec = mm_alloc(0x1F8, 0);
    mm_ref_set(G_ISO_KOR_REF, rec);
    base = mm_conv_ptr(rec);
    for (k = 0; k < 4; k++) {
        if (fr_flag_test(0x8D) && !fr_flag_test(0x8F)) {
            fm_load_cmp(1, DGSTR(0x64A3), (int16_t)(k + 0x0B));      /* karafa.cmp */
            load_kor_at(DGSTR(0x64AE), base, (uint16_t)(k * 0x48)); /* karafa.kor */
        } else {
            strcpy(name, g_portrait_name(g_party_stats[k + 1].portrait));
            fm_load_cmp(1, name, (int16_t)(k + 0x0B));
            alt = fm_change_ext(name, DGSTR(0x64B9));                 /* ".kor" */
            load_kor_at(alt, base, (uint16_t)(k * 0x48));
            bc_farfree(alt);
        }
    }
    for (k = 4; k <= 6; k++) {
        const char *shortname = DGSTR((uint16_t)(0x2185 + (g_iso_group_type(k) % 100) * 9));
        strcat(strcpy(name, shortname), DGSTR(0x64BD));              /* ".cmp" */
        if (strlen(name) < 5)
            strcat(strcpy(name, DGSTR(0x2185)), DGSTR(0x64C3));      /* "bosch.cmp" */
        fm_load_cmp(1, name, (int16_t)(k + 0x0B));
        alt = fm_change_ext(name, DGSTR(0x64C9));
        load_kor_at(alt, base, (uint16_t)(k * 0x48));
        bc_farfree(alt);
    }
    mm_set_conv_ptr(rec, base, 4);
    mm_change_type(rec, 1);

    /* slot 18: the walls, 300x60 (record E32F built by hand) */
    mm_ref_set(0xE32F, mm_alloc(0x4650, 0));
    g_surf_w(ISO_SURF_WALLS) = 300;
    g_surf_h(ISO_SURF_WALLS) = 0x3C;
    sprintf(name, DGSTR(0x64CD), 0);                                 /* duvarica.cmp */
    fm_load_cmp(0, name, 0x57);
    sys_blit(0, 0, (uint16_t)g_surf_w(0x57), (uint16_t)g_surf_h(0x57), 0, 0, 0x57,
             ISO_SURF_WALLS, sys_rect_copy);
    ctl_free_checked(0xE557, 0x1AA, ISO_FILE(0x64DB));
    sprintf(name, DGSTR(0x64EB), 0);                                 /* duvaricb.cmp */
    fm_load_cmp(0, name, 0x57);
    sys_blit(0, 0, (uint16_t)g_surf_w(0x57), (uint16_t)g_surf_h(0x57), 0x32, 0, 0x57,
             ISO_SURF_WALLS, sys_rect_copy);
    ctl_free_checked(0xE557, 0x1AB, ISO_FILE(0x64F9));
    for (k = 2; k < 6; k++)
        if (g_iso_party_pic(k) != 0) {
            pic = g_iso_party_pic(k);
            break;
        }
    for (k = 2; k < 6; k++) {
        sprintf(name, DGSTR(0x6509), g_iso_party_pic(k) == 0 ? pic : g_iso_party_pic(k));
        switch (g_iso_party_pic(k)) {      /* jump table 4595:0B70 */
        case 1: g_iso_wall_kind(k) = 2; g_iso_wall_color(k) = 0xB7; break;
        case 2: g_iso_wall_kind(k) = 3; g_iso_wall_color(k) = 0xF8; break;
        case 3: g_iso_wall_kind(k) = 3; g_iso_wall_color(k) = 0xF3; break;
        case 4: g_iso_wall_kind(k) = 1; g_iso_wall_color(k) = 0x93; break;
        case 5: g_iso_wall_kind(k) = 5; g_iso_wall_color(k) = 0xEE; break;
        case 6: g_iso_wall_kind(k) = 9; g_iso_wall_color(k) = 0x94; break;
        case 7: g_iso_wall_kind(k) = 4; g_iso_wall_color(k) = 0xAF; break;
        case 8: g_iso_wall_kind(k) = 6; g_iso_wall_color(k) = 0x88; break;
        default: break;
        }
        fm_load_cmp(1, name, 0x57);
        sys_blit(0, 0, (uint16_t)g_surf_w(0x57), (uint16_t)g_surf_h(0x57),
                 (uint16_t)(k * 0x32), 0, 0x57, ISO_SURF_WALLS, sys_rect_copy);
        ctl_free_checked(0xE557, 0x1BE, ISO_FILE(0x6518));
    }
    mm_change_type(mm_ref(0xE32F), 1);
    iso_init_panel_layout();
}

/*
 * iso_draw_text - text on the work screen (slot 6); negative coordinates
 * become 0.  Font 1: grt_draw_text with big capitals from slot 5; else
 * grt_draw_text_alt.
 *
 * Original: IST.EXE ovl4595:0B80 (ISOSYS.L), far cdecl.
 * Verified: tests/py/test_iso.py, 2026-11-21.
 */
void iso_draw_text(int16_t font, int16_t x, int16_t y, const char *s, uint8_t color)
{
    x = x < 0 ? 0 : x;
    y = y < 0 ? 0 : y;
    if (font == 1)
        grt_draw_text(6, x, y, s, color, 5);
    else
        grt_draw_text_alt(6, x, y, s, color);
}

/*
 * iso_show_picture - hide the cursor, copy surface `slot` to the screen
 * (slot 0), show the cursor again (shape E5E3).
 *
 * Original: IST.EXE ovl4595:0BE1 (ISOSYS.L), far cdecl.
 * Verified: tests/py/test_iso.py, 2026-11-21.
 */
void iso_show_picture(int16_t slot)
{
    gr_cursor_hide();
    mm_copy_block(mm_surface(slot), mm_surface(0));
    gr_cursor_set_shape((uint8_t)g_cursor_shape);
}

/*
 * iso_set_cell - occupancy grid: occ[x][y] = v.
 *
 * Original: IST.EXE ovl4595:0C27 (ISOSYS.L), far cdecl.
 * Verified: by inspection (one store).
 */
void iso_set_cell(int16_t x, int16_t y, int16_t v)
{
    g_iso_occ(x, y) = v;
}

/*
 * iso_center_view - view origin = unit c's cell - 3, clamped to 0..20.
 * (min() then max(): the macros evaluate the expression again; no side
 * effects here.)
 *
 * Original: IST.EXE ovl4595:0C40 (ISOSYS.L), far cdecl.
 * Verified: tests/py/test_iso.py, 2026-11-21.
 */
void iso_center_view(int16_t c)
{
    int16_t v;
    v = (int16_t)(iso_unit(c)->x - 3);
    g_iso_view_x = v < 0 ? 0 : (v < 0x14 ? v : 0x14);
    v = (int16_t)(iso_unit(c)->y - 3);
    g_iso_view_y = v < 0 ? 0 : (v < 0x14 ? v : 0x14);
}
