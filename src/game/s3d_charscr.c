/*
 * s3d_charscr.c - 3DSYS.L: the character / inventory screen
 * (IST.EXE 303F:4F3A-75EE).
 *
 * The screen is composed in surface 6, a copy of picture surface 4Ch
 * (duplicated on entry, freed on exit): the selected member's portrait,
 * the four party portraits (zones 15-18, a click or F1-F4 selects), the 10
 * inventory icons (zones 1-10, from strip 4Bh), the three hand slots
 * (zones 11-13), two bars, the member's texts (iso_draw_text_shadow), the
 * vecize button (zone 20 opens zone 14; key V), the bin (zone 19, F5) and the exit (zone 21, Esc, right+left button = 3).  A left click on
 * an item picks it up (the cursor becomes its icon); a click on a hand
 * slot equips it, on a portrait gives it to that member, on the bin
 * throws it away after a question; the right button on a usable item
 * (ids 21h-2Ah) uses it.  Most item logic lives in ISOSYS (iso_refresh_desc,
 * iso_use_item, iso_cmd_vecize ...).
 *
 * The original is one function with jumps between its parts; the port
 * keeps that shape (labels named after their addresses).
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "engine/dgroup.h"
#include "engine/grtext.h"
#include "engine/intrface.h"
#include "engine/memman.h"
#include "engine/ngraphs.h"
#include "engine/nkontrol.h"
#include "engine/ographs.h"
#include "engine/systems.h"
#include "engine/blit386.h"
#include "platform/pc.h"
#include "game/s3d.h"
#include "game/s3d_ext.h"

#define SURF_REF(n)  ((uint16_t)(0xE29F + 8 * (n)))   /* far MmHandle * of slot n */

/* Borland itoa(v, buf, 10) */
static char *cs_itoa(int16_t v, char *buf)
{
    sprintf(buf, "%d", v);
    return buf;
}

/* the cursor shape returned by the current window record's shape callback
   (far call through 16E0:9650) */
static uint8_t cs_shape_cb(void)
{
    return (uint8_t)((int16_t (*)(void))ui_far_fn(DG32(UI_CB_SHAPE)))();
}

/* inventory value of hand slot h (1..3) of the current member: the hand
   word holds a word index into the stat record (slot + 22h) */
static int16_t cs_hand_item(int16_t h)
{
    int16_t p = g_party_cur;
    return g_s3d_stat(p, 2 * g_s3d_stat(p, 0x3A + 2 * h));
}

/* icon index of an item id: 2Bh -> 22h, ids above 20h shift by 2 */
static int16_t cs_icon(int16_t id)
{
    if (id == 0x2B)
        return 0x22;
    if (id > 0x20)
        return id + 2;
    return id;
}

/* draw hand slot h's icon (or the empty background of picture 4Ch) into
   surface dst */
static void cs_draw_hand(int16_t h, int16_t icon, uint8_t dst)
{
    int16_t x = (int16_t)((h - 1) * 0x11 + 0x29);
    if (icon == 0) {
        sys_blit((uint16_t)x, 0x26, 0x0F, 0x0F, (uint16_t)x, 0x26, 0x4C, dst, sys_rect_copy);
    } else {
        sys_fill_rect((uint16_t)x, 0x26, 0x0F, 0x0F, dst, DG8(0x9667));
        sys_blit((uint16_t)((icon - 1) * 0x0F), 0, 0x0F, 0x0F, (uint16_t)x, 0x26, 0x4B, dst,
                 asm_copy_rect_key);
    }
}

/* 4F72..5C00: one value of the text block (iso_draw_text_shadow(itoa(v)) at
   column col, row row) */
static void cs_text_num(int16_t v, int16_t col, int16_t row, char *buf)
{
    iso_draw_text_shadow(cs_itoa(v, buf), col, row, DG8(0x966B), 9);
}

/* 303F:5ED7 etc.: selected item from inventory slot g_cs_zone */
static void cs_select_item(void)
{
    int16_t v = g_s3d_item(g_party_cur, g_cs_zone);
    g_cs_item_count = (int16_t)(v / 100);
    g_cs_item_id = (int16_t)(v % 100);
}

/* gr_cursor_update(current shape) - the original's idle step */
static void cs_cursor(void)
{
    gr_cursor_update((uint8_t)g_cursor_shape);
}

/* while (buttons held) update the cursor - the original spins on the mouse
   driver; the port polls the emulated PC after each update */
static void cs_wait_release(void)
{
    while (g_mouse_buttons != 0) {
        cs_cursor();
        pc_poll();
    }
}

/* bioskey(1) >> 8 and flush: the scan code of a waiting key, 0 none */
static int16_t cs_key(void)
{
    int16_t k = (int16_t)(ui_bioskey(1) >> 8);
    if (k != 0)
        ui_flush_keys();
    return k;
}

/*
 * s3d_character_screen - the character / inventory screen of the current
 * party member (g_party_cur); returns when the player leaves it.
 *
 * Original: IST.EXE 303F:4F3A (3DSYS.L), far cdecl, no arguments.
 * Verified: tests/py/test_s3d_charscr.py (differential: DGROUP, A000,
 * calls into ISOSYS) for browsing, picking up / moving / equipping /
 * giving / throwing items and leaving; the vecize and item-use paths only
 * up to the (stubbed) ISOSYS calls.
 */
void s3d_character_screen(void)
{
    int16_t i;                  /* bp-02 */
    int16_t icon;               /* bp-04 */
    int16_t k = 9;              /* bp-06 iso_draw_text_shadow last argument */
    int16_t key;                /* bp-08 scan code of the main loop */
    int16_t key2;               /* bp-0A scan code while an item is held */
    int16_t vec_kind;           /* bp-0C */
    int16_t vec_target;         /* bp-0E */
    int16_t saved_cur;          /* bp-10 */
    int16_t hands_main = 0;     /* bp-12 hands of the item in hand slot 1 */
    int16_t hands_off = 0;      /* bp-14 hands of the item in hand slot 2 */
    int16_t mouse_saved = 0;    /* bp-16 g_mouse_present + 1 while the keyboard
                                   holds an item (mouse switched off), 0 none */
    int16_t hand_prev;          /* bp-18 highlighted hand slot (keyboard) */
    int16_t held;               /* bp-1A slot of the held item (keyboard moves) */
    int16_t hand_sel;           /* bp-1C hand slot chosen with the keyboard */
    int16_t key3;               /* bp-1E */
    char    buf[19];            /* bp-3A */
    uint8_t faded = 0;          /* bp-27 */
    char    buf2[8];            /* bp-26 */
    int16_t p;

    g_s3d_pt_x0 = 0x9C;
    g_s3d_pt_y0 = 0x14;
    g_s3d_pt_dx = 0x23;
    g_s3d_pt_dy = 0;
    for (i = 0; i < 4; i++)
        g_s3d_ba89(i) = 1;
    /* surface 6 = a copy of picture 4Ch */
    mm_ref_set(SURF_REF(6), mm_duplicate(mm_ref(SURF_REF(0x4C)), 1));
    g_surf_w(6) = g_surf_w(0x4C);
    g_surf_h(6) = g_surf_h(0x4C);

redraw:                                                     /* 303F:4FD7 */
    gr_zones_clear();
    g_cs_desc_prev = 0;
    ogr_mouse_set_window(0, 0, 0x13F, 0xC7);
    gr_cursor_set_shape(g_ui_shape);
    if (mouse_saved != 0) {
        g_mouse_present = (uint8_t)(mouse_saved - 1);
        mouse_saved = 0;
        gr_cursor_set_shape(cs_shape_cb());
    }
    mm_copy_block(mm_ref(SURF_REF(0x4C)), mm_ref(SURF_REF(6)));
    p = g_party_cur;
    sys_blit(0, 0, 0x20, 0x20, 7, 0x15, (uint8_t)(p + 0x46), 6, sys_rect_copy);
    if (g_s3d_stat(p, 0x3A) <= 0) {
        if (g_s3d_stat(p, 0x3A) <= -5)
            gr_draw_strip(7, 0x15, 0x20, 0x20, 6, 0);
        gr_draw_strip(7, 0x15, 0x20, 0x20, 6, 0xD9);
    }
    /* the party portraits, zones 15-18 */
    for (i = 1; i <= 4; i++) {
        g_zone_x1(14 + i) = (int16_t)((i - 1) * 0x23 + 0x9C);
        g_zone_y1(14 + i) = 0x14;
        g_zone_x2(14 + i) = (int16_t)((i - 1) * 0x23 + 0xBC);
        g_zone_y2(14 + i) = 0x34;
        sys_blit(0, 0, 0x20, 0x20, (uint16_t)((i - 1) * 0x23 + 0x9C), 0x14, (uint8_t)(i + 0x46),
                 6, sys_rect_copy);
        if (g_s3d_stat(i, 0x3A) <= 0) {
            if (g_s3d_stat(i, 0x3A) <= -5)
                gr_draw_strip((uint16_t)(g_s3d_pt_x0 + g_s3d_pt_dx * (i - 1)),
                              (uint16_t)(g_s3d_pt_y0 + g_s3d_pt_dy * (i - 1)), 0x20, 0x20, 6, 0);
            gr_draw_strip((uint16_t)(g_s3d_pt_x0 + g_s3d_pt_dx * (i - 1)),
                          (uint16_t)(g_s3d_pt_y0 + g_s3d_pt_dy * (i - 1)), 0x20, 0x20, 6, 0xD9);
        }
    }
    g_zone_x1(19) = 0x128;                                  /* the bin */
    g_zone_y1(19) = 0x14;
    g_zone_x2(19) = 0x13C;
    g_zone_y2(19) = 0x34;
    /* the inventory, zones 1-10 (only slots holding an item get a zone) */
    for (i = 1; i <= 10; i++) {
        g_cs_item_id = (int16_t)(g_s3d_item(g_party_cur, i) % 100);
        g_cs_item_id = cs_icon(g_cs_item_id);
        if (g_cs_item_id == 0) {
            sys_blit(0x1E0, 0, 0x0F, 0x0F, (uint16_t)((i - 1) * 0x10 + 1), 1, 0x4B, 6,
                     asm_copy_rect_key);
        } else {
            sys_blit((uint16_t)((g_cs_item_id - 1) * 0x0F), 0, 0x0F, 0x0F,
                     (uint16_t)((i - 1) * 0x10 + 1), 1, 0x4B, 6, asm_copy_rect_key);
            g_zone_x1(i) = (int16_t)((i - 1) * 0x10 + 1);
            g_zone_y1(i) = 1;
            g_zone_x2(i) = (int16_t)((i - 1) * 0x10 + 0x0F);
            g_zone_y2(i) = 0x10;
        }
    }
    /* the hand slots, zones 11-13 */
    for (i = 1; i <= 3; i++) {
        icon = cs_icon((int16_t)(cs_hand_item(i) % 100));
        cs_draw_hand(i, icon, 6);
        g_zone_x1(10 + i) = (int16_t)((i - 1) * 0x11 + 0x29);
        g_zone_y1(10 + i) = 0x26;
        g_zone_x2(10 + i) = (int16_t)((i - 1) * 0x11 + 0x38);
        g_zone_y2(10 + i) = 0x35;
    }
    /* a two-handed item in hand 1 also shades hand 2 */
    i = (int16_t)(cs_hand_item(1) % 100);
    if (i == 0x2B)
        i = 0x1A;
    if (g_s3d_item_hands(i) == 2)
        gr_draw_strip(0x3A, 0x26, 0x0F, 0x0F, 6, 0xFD);
    g_zone_x1(20) = 0x8A;                                   /* vecize button */
    g_zone_y1(20) = 0x68;
    g_zone_x2(20) = 0x98;
    g_zone_y2(20) = 0x86;
    g_zone_x1(21) = 0x8A;                                   /* exit */
    g_zone_y1(21) = 0x57;
    g_zone_x2(21) = 0x98;
    g_zone_y2(21) = 0x66;
    p = g_party_cur;
    if (g_s3d_stat(p, 0x2A) > 0)
        sys_fill_rect(0x12, 0x72, (uint16_t)(g_s3d_stat(p, 0x2A) / 10 + 1), 3, 6,
                      g_s3d_stat(p, 0x2A) > 500 ? DG8(0x966D) : DG8(0x966C));
    if (g_s3d_stat(p, 0x22) > 0)
        sys_fill_rect(0x12, 0x80, (uint16_t)(g_s3d_stat(p, 0x22) / 10 + 1), 3, 6,
                      g_s3d_stat(p, 0x22) > 500 ? DG8(0x966D) : DG8(0x966C));
    sys_fill_rect_checked((int16_t)((g_party_cur - 1) * 0x23 + 0x9B), 0x13, 0x21, 0x21, 6,
                          DG8(0x9664));
    k = 9;
    iso_draw_text_shadow(DGSTR(S3D_NAME_REC(g_party_cur)), 1, 5, DG8(0x9669), k);
    iso_draw_text_shadow(DGSTR(g_s3d_stat(g_party_cur, 0x04) == 1 ? 0x444E : 0x4454),
                         6, 1, DG8(0x966B), k);                 /* "erkek" / "dişi" */
    iso_draw_text_shadow(DGSTR(S3D_NAME_REC(g_party_cur) + 0x4A), 6, 2, DG8(0x966B), k);
    cs_text_num(g_s3d_stat(g_party_cur, 0x24), 8, 6, buf);
    cs_text_num(g_s3d_stat(g_party_cur, 0x26), 8, 7, buf);
    cs_itoa(g_s3d_stat(g_party_cur, 0x3A), buf);
    strcat(buf, DGSTR(0x4459));                             /* "/" */
    strcat(buf, cs_itoa(g_s3d_stat(g_party_cur, 0x0C), buf2));
    iso_draw_text_shadow(buf, 8, 8, DG8(0x966B), k);
    cs_itoa(g_s3d_stat(g_party_cur, 0x38), buf);
    strcat(buf, DGSTR(0x445B));                             /* "/" */
    strcat(buf, cs_itoa(g_s3d_stat(g_party_cur, 0x0A), buf2));
    iso_draw_text_shadow(buf, 8, 9, DG8(0x966B), k);
    for (i = 1; i <= 2; i++)
        cs_text_num(g_s3d_stat(g_party_cur, 0x04 + 2 * i), 0x1F, (int16_t)(i + 5), buf);
    cs_text_num(g_s3d_stat(g_party_cur, 0x0E), 0x23, 9, buf);
    cs_text_num(g_s3d_stat(g_party_cur, 0x10), 0x23, 10, buf);
    cs_text_num(g_s3d_stat(g_party_cur, 0x0A), 0x23, 11, buf);
    cs_text_num(g_s3d_stat(g_party_cur, 0x14), 0x23, 12, buf);
    cs_text_num(g_s3d_stat(g_party_cur, 0x12), 0x23, 13, buf);
    cs_text_num(g_s3d_stat(g_party_cur, 0x1A), 0x23, 14, buf);
    cs_text_num(g_s3d_stat(g_party_cur, 0x16), 0x23, 15, buf);
    cs_text_num(g_s3d_stat(g_party_cur, 0x18), 0x23, 16, buf);
    cs_text_num(g_s3d_stat(g_party_cur, 0x1C), 0x23, 17, buf);
    cs_text_num(g_s3d_stat(g_party_cur, 0x1E), 0x23, 18, buf);
    cs_text_num(g_s3d_stat(g_party_cur, 0x20), 0x23, 19, buf);
    cs_text_num(iso_calc_stat_c(), 0x10, 15, buf);
    iso_calc_stat_a();
    cs_text_num(g_cs_load, 0x10, 16, buf);
    iso_calc_weapon();
    cs_text_num(g_cs_weapon, 0x10, 17, buf);
    /* the name of stat +2C (15-byte table), cut to 90 pixels + "." */
    strcpy(buf, DGSTR(S3D_ITEM_NAMES + 0x0F * g_s3d_stat(g_party_cur, 0x2C)));
    i = (int16_t)strlen(buf);
    icon = grt_text_width(buf, 1);          /* bp-08 in the original (reused) */
    if (icon > 0x5A) {
        while (icon > 0x5A) {
            buf[i] = 0;
            i--;
            icon = grt_text_width(buf, 1);
        }
        strcat(buf, DGSTR(0x445D));                         /* "." */
    }
    iso_draw_text_shadow(buf, (int16_t)((0x92 - icon) / 8), 0x12, DG8(0x966A), k);
    cs_text_num(iso_calc_stat_b(), 0x10, 19, buf);
    cs_text_num(iso_calc_ap_by_load(), 0x10, 20, buf);
    iso_show_picture(6);
    if (!faded) {
        gr_fade(100, g_pal_level_max, 0);
        faded = 1;
    }
    cs_wait_release();                                      /* 303F:5E20 */

wait:                                                       /* 303F:5E3D */
    for (;;) {
        key = cs_key();
        cs_cursor();
        if (g_mouse_buttons == 3)
            break;
        g_cs_zone = gr_zone_under_cursor();
        if (g_cs_zone != 0 || key != 0 || g_mouse_buttons > 1)
            break;
    }
    if (key == 1 || (g_cs_zone == 0x15 && g_mouse_buttons == 1) || g_mouse_buttons == 3)
        goto leave;
    if (g_cs_zone < 0x0B && key == 0) {
        cs_select_item();
        iso_refresh_desc();
    }
    if (g_cs_zone == 0 && g_cs_desc_prev != 0) {
        gr_cursor_hide();
        sys_fill_rect(0xA1, 2, 0x9D, 0x0D, 0, 0);
        gr_cursor_set_shape((uint8_t)g_cursor_shape);
        g_cs_desc_prev = 0;
    }
    if (g_cs_zone == 0x14 && g_mouse_buttons == 1 && g_s3d_stat(g_party_cur, 0x3A) > 0) {
        /* the vecize button: mouse confined to the lower left */
        g_zone_x1(14) = 0;
        g_zone_y1(14) = 0x8C;
        g_zone_x2(14) = 0x98;
        g_zone_y2(14) = 0xC8;
        ogr_mouse_set_window(0, 0x8C, 0x98, 0x3B);
        ogr_mouse_set_pos(0x96, 0x8C);
        cs_cursor();
        iso_show_picture(6);
    }
    /* 303F:6049 */
    if (((g_mouse_buttons == 1 || (g_mouse_buttons == 2 && g_cs_zone > 10)) && g_cs_zone != 0)
        || key != 0) {
        g_cs_zone = gr_zone_under_cursor();
        if (((g_cs_zone == 0x0E && g_mouse_buttons == 1) || key == 0x2F)
            && g_s3d_stat(g_party_cur, 0x3A) > 0)
            goto vecize;
    }
    /* 303F:636E: select another member */
    if ((g_cs_zone > 0x0E && g_cs_zone < 0x13 && (int16_t)(g_cs_zone - 0x0E) != g_party_cur
         && g_mouse_buttons == 1)
        || (key >= 0x3B && key <= 0x3E && (int16_t)(key - 0x3A) != g_party_cur)) {
        g_party_cur = key != 0 ? (int16_t)(key - 0x3A) : (int16_t)(g_cs_zone - 0x0E);
        sys_fill_rect_checked((int16_t)((g_party_cur - 1) * 0x23 + 0x9B), 0x13, 0x21, 0x21, 6,
                              DG8(0x9664));
        goto redraw;
    }
    /* 303F:641E: take an item off a hand slot (click, or keys Q/W/E = scan 10h-12h) */
    if (((key < 0x13 && key != 0) || g_mouse_buttons == 1)
        && ((g_cs_zone > 10 && g_cs_zone < 0x0E && g_s3d_stat(g_party_cur, 0x26 + 2 * g_cs_zone) != 0
             && g_mouse_buttons == 1)
            || (key > 0x0F && key < 0x13 && g_s3d_stat(g_party_cur, 0x1C + 2 * key) != 0))) {
        if (key != 0)
            g_cs_zone = (int16_t)(key - 5);
        i = (int16_t)(g_s3d_stat(g_party_cur, 0x26 + 2 * g_cs_zone) - 0x22);
        g_s3d_stat(g_party_cur, 0x26 + 2 * g_cs_zone) = 0;
        if (g_cs_zone == 0x0B)
            hands_main = 0;
        else if (g_cs_zone == 0x0C)
            hands_off = 0;
        gr_cursor_hide();
        sys_fill_rect_checked((int16_t)((i - 1) * 0x10), 0, 0x10, 0x10, 0, DG8(0x9664));
        gr_cursor_set_shape(cs_shape_cb());
        goto redraw;
    }
    /* 303F:6585: pick up an inventory item (left click; keys 17h 'I', 4Bh, 4Dh) */
    if (!((g_cs_zone < 0x0B && g_mouse_buttons == 1) || key == 0x17 || key == 0x4B
          || key == 0x4D))
        goto right_button;
    if (key != 0) {
        /* keyboard: the last occupied slot, mouse switched off */
        g_cs_zone = 0;
        for (i = 1; i <= 10; i++)
            if (g_s3d_item(g_party_cur, i) != 0)
                g_cs_zone = i;
        if (g_cs_zone != 0) {
            gr_cursor_hide();
            mouse_saved = (int16_t)(g_mouse_present + 1);
            g_mouse_present = 0;
        }
    }
    if (g_s3d_item(g_party_cur, g_cs_zone) == 0)
        goto wait;
    held = g_cs_zone;

hold:                                                       /* 303F:6666 */
    cs_select_item();
    gr_cursor_hide();
    sys_fill_rect_checked((int16_t)((g_cs_zone - 1) * 0x10), 0, 0x10, 0x10, 0, DG8(0x9664));
    iso_refresh_desc();
    g_cs_item_id = cs_icon(g_cs_item_id);
    if (g_cs_item_id != 0) {
        gr_cursor_set_shape((uint8_t)(g_cs_item_id - 1));
        gr_wait_buttons_up();
    } else {
        gr_cursor_set_shape(0x2D);
    }
    do {                                                    /* 303F:6762 */
        g_cs_target = gr_zone_under_cursor();
        key2 = cs_key();
        cs_cursor();
    } while (g_mouse_buttons == 0 && key2 == 0);
    if (g_mouse_buttons == 2)
        goto put_back;
    if ((g_cs_target < 0x0B && g_mouse_buttons == 1 && g_cs_target != g_cs_zone
         && g_cs_target != 0)
        || key2 == 0x4B || key2 == 0x4D) {
        /* move the highlight to another slot */
        if (key2 == 0x4D) {
            iso_clamp_add(&held, 1, 1, 10);
            g_cs_target = held;
        } else if (key2 == 0x4B) {
            iso_clamp_add(&held, -1, 1, 10);
            g_cs_target = held;
        }
        gr_cursor_hide();
        sys_fill_rect_checked((int16_t)((g_cs_zone - 1) * 0x10), 0, 0x10, 0x10, 0, 0xA9);
        gr_cursor_set_shape(cs_shape_cb());
        g_cs_zone = g_cs_target;
        gr_cursor_hide();
        sys_fill_rect_checked((int16_t)((g_cs_zone - 1) * 0x10), 0, 0x10, 0x10, 0, 0x1D);
        if (g_s3d_item(g_party_cur, g_cs_zone) != 0) {
            cs_select_item();
            iso_refresh_desc();
            g_cs_item_id = cs_icon(g_cs_item_id);
            gr_cursor_update((uint8_t)(g_cs_item_id - 1));
            gr_wait_buttons_up();
        } else {
            g_cs_item_id = 0;
            g_cs_item_count = 0;
            gr_cursor_update(0x2D);
        }
    }
    /* 303F:69AB */
    if (key2 == 1)
        goto redraw;
    if (key2 == 0x39) {                                     /* Space: use */
        cs_select_item();
        if (g_cs_item_id > 0x20 && g_cs_item_id < 0x2B)
            goto use_item;
    }
    hand_sel = 1;
    hand_prev = 0;
    if (key2 == 0x1C) {
        /* Enter: choose a hand slot with the arrows */
        g_mouse_buttons = 1;
        for (;;) {                                          /* 303F:6A60 */
            key3 = (int16_t)(ui_bioskey(1) >> 8);
            cs_cursor();
            if (key3 != 0)
                ui_flush_keys();
            if (key3 == 0x4D)
                iso_clamp_add(&hand_sel, 1, 1, 3);
            else if (key3 == 0x4B)
                iso_clamp_add(&hand_sel, -1, 1, 3);
            if (hand_prev != hand_sel) {
                gr_cursor_hide();
                if (hand_prev != 0) {
                    i = hand_prev;
                    icon = (int16_t)(cs_hand_item(i) % 100);   /* no icon remap here */
                    gr_cursor_hide();
                    if (icon != 0) {
                        cs_draw_hand(i, icon, 0);
                    } else {
                        cs_draw_hand(i, 0, 0);
                        if (g_s3d_item_hands(cs_hand_item(1) % 100) == 2 && i == 2)
                            gr_draw_strip(0x3A, 0x26, 0x0F, 0x0F, 0, 0xFD);
                    }
                    gr_cursor_set_shape(cs_shape_cb());
                }
                sys_fill_rect_checked((int16_t)((hand_sel - 1) * 0x11 + 0x29), 0x26, 0x0E, 0x0E,
                                      0, DG8(0x9664));
                hand_prev = hand_sel;
            }
            /* NB: tests the main loop's key (bp-08), not key3 */
            if (key >= 0x3B && key <= 0x3E) {
                key2 = key3;
                g_mouse_buttons = 0;
                g_cs_target = 0;
                break;
            }
            g_mouse_buttons = 1;
            key2 = 0;
            g_cs_target = (int16_t)(hand_sel + 10);
            if (key3 == 0x1C || key3 == 1)
                break;
        }
        if (key3 == 1)                                      /* 303F:6C9F */
            g_mouse_buttons = 0;
        i = hand_sel;
        icon = (int16_t)(cs_hand_item(hand_sel) % 100);
        cs_draw_hand(i, icon, 0);
    }
    /* 303F:6D7A: drop on hand slot 1 (zone 11): weapons (< 1Ah) and 22h */
    if (g_cs_target == 0x0B && (g_cs_item_id < 0x1A || g_cs_item_id == 0x22)
        && g_mouse_buttons == 1) {
        g_mouse_buttons = 0;
        i = g_cs_item_id == 0x22 ? 0x1A : g_cs_item_id;
        hands_main = g_cs_item_id != 0 ? g_s3d_item_hands(i) : 0;
        if (g_s3d_stat(g_party_cur, 0x3E) == g_cs_zone + 0x22)
            goto redraw;
        if (g_s3d_stat(g_party_cur, 0x3C) == g_cs_zone + 0x22)
            goto redraw;
        if (hands_main == 2 && g_s3d_stat(g_party_cur, 0x3E) != 0)
            goto redraw;
        g_s3d_stat(g_party_cur, 0x3C) = (int16_t)(g_cs_zone + 0x22);
        goto redraw;
    }
    /* 303F:6EA9: hand slot 2 (zone 12): weapons and 1Ah, 22h */
    if (g_cs_target == 0x0C && g_mouse_buttons == 1
        && (g_cs_item_id < 0x1B || g_cs_item_id == 0x22)) {
        g_mouse_buttons = 0;
        if (g_s3d_stat(g_party_cur, 0x3C) == g_cs_zone + 0x22)
            goto redraw;
        if (g_s3d_stat(g_party_cur, 0x3E) == g_cs_zone + 0x22)
            goto redraw;
        if (g_s3d_stat(g_party_cur, 0x3C) != 0) {
            i = (int16_t)(cs_hand_item(1) % 100);
            if (i == 0x22)
                i = 0x1B;
            hands_main = g_s3d_item_hands(i);
        }
        if (g_cs_item_id < 0x1A || g_cs_item_id == 0x22) {      /* 303F:6FD4 */
            if (g_cs_item_id != 0) {
                i = g_cs_item_id == 0x22 ? 0x1A : g_cs_item_id;
                hands_off = g_s3d_item_hands(i);
            } else {
                hands_off = 0;
            }
            if (cs_hand_item(1) % 100 < 0x1A && hands_off == 1 && hands_main != 2)
                goto set_off_hand;
            hands_off = 0;
            goto put_back;
        }
        if (hands_main == 2)                                /* 303F:7071 */
            goto drop_to_wait;
set_off_hand:                                               /* 303F:707A */
        g_s3d_stat(g_party_cur, 0x3E) = (int16_t)(g_cs_zone + 0x22);
        goto redraw;
    }
    /* 303F:70A4: hand slot 3 (zone 13): ids 1Bh-20h */
    if (g_cs_target == 0x0D && g_mouse_buttons == 1 && g_cs_item_id > 0x1A
        && g_cs_item_id < 0x21) {
        g_mouse_buttons = 0;
        g_s3d_stat(g_party_cur, 0x40) = (int16_t)(g_cs_zone + 0x22);
        goto redraw;
    }
    /* 303F:710B: give to another member (portrait zones 15-18, F1-F4) */
    if ((g_cs_target > 0x0E && g_cs_target < 0x13 && g_mouse_buttons == 1
         && (int16_t)(g_cs_target - 0x0E) != g_party_cur)
        || (key2 >= 0x3B && key2 <= 0x3E)) {
        if (key2 != 0)
            g_cs_target = (int16_t)(key2 - 0x2C);
        if (g_s3d_stat(g_party_cur, 0x3C) == g_cs_zone + 0x22
            || g_s3d_stat(g_party_cur, 0x3E) == g_cs_zone + 0x22
            || g_s3d_stat(g_party_cur, 0x40) == g_cs_zone + 0x22) {
            iso_draw_textbox(DGSTR(0x447E), 0xD9, DG8(0x9664));   /* "eşya kullanımda!" */
            ui_wait_frames(0x32);
            goto clear_buttons;
        }
        /* the target member's first free slot; g_cs_target - 0Eh is the member
           (the original indexes 94E2 + 5Ch * zone) */
        for (i = 1; i <= 10; i++) {
            if (DGS16((uint16_t)(0x94E2 + 0x5C * g_cs_target + 2 * i)) == 0) {
                DGS16((uint16_t)(0x94E2 + 0x5C * g_cs_target + 2 * i)) =
                    g_s3d_item(g_party_cur, g_cs_zone);
                g_s3d_item(g_party_cur, g_cs_zone) = 0;
                break;
            }
        }
        if (i == 0x0B) {
            iso_draw_textbox(DGSTR(0x448F), 0xD9, DG8(0x9664));   /* "boş yer yok!" */
            ui_wait_frames(0x32);
        }
        goto clear_buttons;
    }
    /* 303F:72C7: the bin (zone 19, F5) */
    if ((g_cs_target == 0x13 && g_mouse_buttons == 1) || key2 == 0x3F) {
        if (key2 != 0)
            g_cs_target = (int16_t)(key2 - 0x2C);
        if (g_s3d_stat(g_party_cur, 0x3C) == g_cs_zone + 0x22
            || g_s3d_stat(g_party_cur, 0x3E) == g_cs_zone + 0x22
            || g_s3d_stat(g_party_cur, 0x40) == g_cs_zone + 0x22) {
            iso_draw_textbox(DGSTR(0x449C), 0xD9, DG8(0x9664));   /* "bu eşyayı kullanıyor!" */
            ui_wait_frames(0x32);
            goto redraw;
        }
        cs_cursor();                                        /* 303F:7397 (do-while) */
        pc_poll();
        cs_wait_release();
        iso_draw_textbox(DGSTR(0x44B2), 0xD9, DG8(0x9664));       /* "sol: at   sağ: atma" */
        do {
            key = cs_key();
            cs_cursor();
        } while (g_mouse_buttons != 1 && g_mouse_buttons != 2 && key != 0x4B && key != 0x4D);
        if (g_mouse_buttons == 1 || key == 0x4B)
            goto remove_item;
        goto redraw;
    }
drop_to_wait:                                               /* 303F:743A */
    g_mouse_buttons = 0;
    goto hold;

put_back:                                                   /* 303F:7449 */
    if (g_mouse_present == 0 && mouse_saved != 0 && g_mouse_buttons != 0)
        goto clear_buttons;
    gr_cursor_hide();
    sys_fill_rect_checked((int16_t)((g_cs_zone - 1) * 0x10), 0, 0x10, 0x10, 0, 0xA9);
    gr_cursor_set_shape(0x2D);
    goto wait;

clear_buttons:                                              /* 303F:7469 */
    g_mouse_buttons = 0;
    goto redraw;

right_button:                                               /* 303F:74AC */
    if (g_mouse_buttons != 2)
        goto wait;
    g_cs_zone = gr_zone_under_cursor();
    if (g_cs_zone >= 0x0B)
        goto wait;
    if (gr_zone_under_cursor() <= 0)
        goto wait;
    cs_select_item();
    if (g_cs_item_id <= 0x20 || g_cs_item_id >= 0x2B)
        goto wait;
use_item:                                                   /* 303F:7565 */
    iso_use_item();
remove_item:                                                /* 303F:756A */
    g_s3d_item(g_party_cur, g_cs_zone) = 0;
    goto redraw;

vecize:                                                     /* 303F:60E6 */
    cs_wait_release();
    iso_cmd_vecize();
    ogr_mouse_set_window(0, 0, 0x13F, 0xC7);
    if (g_cs_vecize == 0)
        goto redraw;
    vec_kind = g_s3d_vec_kind(g_cs_vecize);
    vec_target = g_s3d_vec_target(g_cs_vecize);
    if (vec_kind != 2) {
        iso_draw_textbox(DGSTR(0x446E), 0xD9, DG8(0x9664));       /* "savaş vecizesi!" */
        ui_wait_frames(0x1E);
        goto redraw;
    }
    if (vec_target == 1) {
        for (i = 1; i <= 4; i++) {
            g_zone_x1(14 + i) = (int16_t)((i - 1) * 0x23 + 0x9C);
            g_zone_y1(14 + i) = 0x14;
            g_zone_x2(14 + i) = (int16_t)((i - 1) * 0x23 + 0xBC);
            g_zone_y2(14 + i) = 0x34;
        }
        iso_draw_textbox(DGSTR(0x445F), 0xD9, DG8(0x9664));       /* "adam seçin" */
        g_cs_vec_victim = ui_poll_input(0x0F);
        if (g_cs_vec_victim == 0)
            goto redraw;
        g_cs_vec_victim += 0x18;
    } else {
        g_cs_vec_victim = (int16_t)(g_party_cur + 0x18);
    }
    g_cs_vec_caster = (int16_t)(g_party_cur + 0x18);        /* 303F:6221 */
    g_cs_6320 = (int16_t)(g_cs_vec_caster + 0x28);
    saved_cur = g_party_cur;
    iso_party_to_battle();
    iso_cast_vecize();
    iso_battle_to_party();
    ui_wait_frames(0x32);
    if (g_cs_vec_voice != 0) {
        i = (int16_t)strlen(DGSTR(CS_VEC_TEXT));
        while (grt_text_width(DGSTR(CS_VEC_TEXT), 1) > 0x82) {
            DG8((uint16_t)(CS_VEC_TEXT + i)) = 0;
            i--;
        }
        strcat(DGSTR(CS_VEC_TEXT), DGSTR(0x446A));          /* "..." */
        iso_draw_textbox(DGSTR(CS_VEC_TEXT), 0xD9, DG8(0x9664));
        g_cs_6320 = (int16_t)(g_cs_vec_victim + 0x28);
        iso_speak_vecize(g_cs_vec_voice);
        g_cs_vec_voice = 0;
        ui_wait_frames(0x32);
    }
    ui_wait_voice();
    g_cs_vec_time = (uint16_t)g_clock_ms;
    g_party_cur = saved_cur;
    goto redraw;

leave:                                                      /* 303F:7596 */
    if (mouse_saved != 0) {
        g_mouse_present = (uint8_t)(mouse_saved - 1);
        mouse_saved = 0;
        gr_cursor_set_shape(cs_shape_cb());
    }
    ctl_free_checked(SURF_REF(6), 0x0A1D, DGSTR(0x44C7));
    gr_fade(100, (int16_t)-g_pal_level, 0);
}
