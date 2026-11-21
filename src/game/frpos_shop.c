/*
 * frpos_shop.c - FRPOS.L part 2 (IST.EXE ovl3FD1:23B2-46A9): the item
 * exchange screen (treasure / weapon shop / goods shop).  See frpos.h.
 *
 * The panel is surface 53h (320 x 62, slides in over the top of the
 * screen, fr_shop_load_gfx): row 1 (y 1Bh) = the 10 shop / treasure slots
 * (words BB57.., g_fr_shop(i)), row 2 (y 2Ch) = the 10 inventory slots of
 * the chosen member (stat words 23h..2Ch), the four portraits at x B1h,
 * the money / currency line at y 10h and the text line at y 3.  Zones:
 * 1-10 inventory, 11-20 shop, 21 drop, 22-25 members, 26 exit, 27
 * convert currency, 28 sell, 29 the whole panel.
 *
 * Item word = 100 * count + id; prices from the byte table 1041h + id
 * (buy: 2 * t + shop base; sell: 2 * t + 5 - random(10) +- 1, at least 1).
 */
#include "game/frpos.h"

#include <stdio.h>
#include <string.h>

#include "engine/blit386.h"
#include "engine/fatal.h"
#include "engine/grtext.h"
#include "engine/intrface.h"
#include "engine/memman.h"
#include "engine/ngraphs.h"
#include "engine/systems.h"
#include "game/q3dview.h"
#include "game/s3d.h"
#include "rtl/bc_rand.h"

#define g_fr_shop(i)    DGS16((uint16_t)(0xBB55 + 2 * (i)))  /* i16[11] shop row, 1..10 */
#define FR_PRICE(id)    DG8((uint16_t)(0x1041 + (id)))       /* u8 base price of item id */
#define FR_INV(p, s)    FR_STAT(p, 0x22 + (s))               /* inventory slot s = 1..10 */
#define FR_HAND(p, k)   FR_STAT(p, 0x1E + (k))               /* hand slots k = 0..2 */
#define g_fr_panel_y    DGS16(0x5BBE)   /* i16  screen y of the shop panel */
#define g_fr_panel_x    DGS16(0x5BC0)   /* i16  screen x of the shop panel */

/* item id -> icon number (2Bh -> 22h, > 20h -> +2) */
static int16_t icon_of(int16_t id)
{
    if (id == 0x2B)
        return 0x22;
    if (id > 0x20)
        return (int16_t)(id + 2);
    return id;
}

/*
 * fr_get_e5e3 - window-record callback (+...): the current cursor shape.
 *
 * Original: IST.EXE ovl3FD1:23B2 (FRPOS.L), far cdecl; reached through
 * the far pointer 1689:0098 (16E0:9800).
 * Verified: by inspection.
 */
int16_t fr_get_e5e3(void)
{
    return g_cursor_shape;
}

/*
 * fr_shop_draw_item - inventory row of member who (icons from surface 4Bh,
 * 15 x 15; an empty slot gets the icon at x 1E0h), zones 1..10, the four
 * portraits (surfaces 47h..4Ah) with zones 22..25 (dead: strip patterns),
 * and the frame around member who.
 *
 * Original: IST.EXE ovl3FD1:23C0 (FRPOS.L), far cdecl.  Sets the portrait
 * layout 16E0:BB45..BB4B as a side effect.
 * Verified: tests/py/test_frpos_shop.py (unicorn differential), 2026-11-21.
 */
void fr_shop_draw_item(int16_t who)
{
    int16_t i, v;

    g_s3d_pt_x0 = 0xB1;
    g_s3d_pt_y0 = 0x1B;
    g_s3d_pt_dx = 0x22;
    g_s3d_pt_dy = 0;
    for (i = 1; i <= 10; i++) {
        v = icon_of((int16_t)(FR_INV(who, i) % 100));
        g_zone_x1(i) = (int16_t)((i - 1) * 0x11 + 7);
        g_zone_y1(i) = (int16_t)(g_fr_panel_y + 0x2C);
        if (v != 0) {
            sys_fill_rect((uint16_t)g_zone_x1(i), (uint16_t)(g_zone_y1(i) - g_fr_panel_y), 0x0F,
                          0x0F, 0x53, 0);
            sys_blit((uint16_t)((v - 1) * 0x0F), 0, 0x0F, 0x0F, (uint16_t)g_zone_x1(i),
                     (uint16_t)(g_zone_y1(i) - g_fr_panel_y), 0x4B, 0x53, asm_copy_rect_key);
        } else {
            sys_blit(0x1E0, 0, 0x0F, 0x0F, (uint16_t)((i - 1) * 0x11 + 7), 0x2C, 0x4B, 0x53,
                     asm_copy_rect_key);
        }
        g_zone_x1(i) = (int16_t)(g_zone_x1(i) + g_fr_panel_x);
        g_zone_y1(i) = (int16_t)(g_zone_y1(i) % g_surf_h(0));
        g_zone_x2(i) = (int16_t)(g_zone_x1(i) + 0x0F);
        g_zone_y2(i) = (int16_t)(g_zone_y1(i) + 0x0F);
    }
    for (i = 1; i <= 4; i++) {
        int16_t x = (int16_t)(g_s3d_pt_x0 + (i - 1) * g_s3d_pt_dx);
        int16_t y = (int16_t)(g_s3d_pt_y0 + (i - 1) * g_s3d_pt_dy);

        g_zone_x1(0x15 + i) = (int16_t)(x + g_fr_panel_x);
        g_zone_y1(0x15 + i) = (int16_t)((int16_t)(g_s3d_pt_y0 + g_fr_panel_y +
                                                  (i - 1) * g_s3d_pt_dy) % g_surf_h(0));
        g_zone_x2(0x15 + i) = (int16_t)(g_zone_x1(0x15 + i) + 0x20);
        g_zone_y2(0x15 + i) = (int16_t)(g_zone_y1(0x15 + i) + 0x20);
        sys_blit(0, 0, 0x20, 0x20, (uint16_t)x, (uint16_t)y, (uint8_t)(0x46 + i), 0x53,
                 sys_rect_copy);
        if (FR_STAT(i, FR_STAT_HP) < -5) {
            gr_draw_strip((uint16_t)x, (uint16_t)y, 0x20, 0x20, 0x53, 0xFD);
            gr_draw_strip((uint16_t)x, (uint16_t)y, 0x20, 0x20, 0x53, 0xD9);
        } else if (FR_STAT(i, FR_STAT_HP) <= 0) {
            gr_draw_strip((uint16_t)x, (uint16_t)y, 0x20, 0x20, 0x53, 0xD9);
        }
    }
    sys_fill_rect_checked((int16_t)((who - 1) * 0x22 + 0xB1), 0x1B, 0x1F, 0x1F, 0x53, 0xDC);
}

/*
 * fr_shop_draw_text - copy the panel picture (surface 53h, 4D80h bytes =
 * 320 x 62) to the screen at the panel position, hiding the cursor if it
 * is over the panel zone.
 *
 * Original: IST.EXE ovl3FD1:27C4 (FRPOS.L), far cdecl.
 * Verified: tests/py/test_frpos_shop.py (unicorn differential), 2026-11-21.
 */
void fr_shop_draw_text(void)
{
    uint8_t z = (uint8_t)g_ui_panel_zone;

    if (gr_cursor_in_rect(g_zone_x1(z), g_zone_y1(z), (int16_t)(g_zone_x2(z) - g_zone_x1(z)),
                          (int16_t)(g_zone_y2(z) - g_zone_y1(z))))
        gr_cursor_hide();
    mm_copy_range(mm_ref(0xE537), mm_surface(0), 0,
                  (uint32_t)((int32_t)(int16_t)(g_fr_panel_y % g_surf_h(0)) * 0x140 +
                             g_fr_panel_x),
                  0x4D80);
    if (gr_cursor_in_rect(g_zone_x1(z), g_zone_y1(z), (int16_t)(g_zone_x2(z) - g_zone_x1(z)),
                          (int16_t)(g_zone_y2(z) - g_zone_y1(z))))
        gr_cursor_set_shape((uint8_t)g_cursor_shape);
}

/*
 * fr_shop_draw_cursor - the shop row (zones 11..20): icons of g_fr_shop(i).
 * Returns 1 when any slot holds an item.
 *
 * Original: IST.EXE ovl3FD1:296A (FRPOS.L), far cdecl.
 * Verified: tests/py/test_frpos_shop.py (unicorn differential), 2026-11-21.
 */
int16_t fr_shop_draw_cursor(void)
{
    int16_t any = 0, i, v;

    for (i = 1; i <= 10; i++) {
        v = icon_of((int16_t)(g_fr_shop(i) % 100));
        g_zone_x1(10 + i) = (int16_t)((i - 1) * 0x11 + 7);
        g_zone_y1(10 + i) = (int16_t)(g_fr_panel_y + 0x1B);
        if (v != 0) {
            sys_fill_rect((uint16_t)g_zone_x1(10 + i),
                          (uint16_t)(g_zone_y1(10 + i) - g_fr_panel_y), 0x0F, 0x0F, 0x53, 0);
            any = 1;
            sys_blit((uint16_t)((v - 1) * 0x0F), 0, 0x0F, 0x0F, (uint16_t)g_zone_x1(10 + i),
                     (uint16_t)(g_zone_y1(10 + i) - g_fr_panel_y), 0x4B, 0x53,
                     asm_copy_rect_key);
        } else {
            sys_blit(0x1E0, 0, 0x0F, 0x0F, (uint16_t)((i - 1) * 0x11 + 7), 0x1B, 0x4B, 0x53,
                     asm_copy_rect_key);
        }
        g_zone_x1(10 + i) = (int16_t)(g_zone_x1(10 + i) + g_fr_panel_x);
        g_zone_y1(10 + i) = (int16_t)(g_zone_y1(10 + i) % g_surf_h(0));
        g_zone_x2(10 + i) = (int16_t)(g_zone_x1(10 + i) + 0x0F);
        g_zone_y2(10 + i) = (int16_t)(g_zone_y1(10 + i) + 0x0F);
    }
    return any;
}

/*
 * fr_shop_buy - exchange shop slot sel[1] with inventory slot sel[2] of
 * member who.  In a shop (mode != 0) only buying a non-empty shop slot
 * happens: price = 2 * t(shop item) + base - (trade-in value of the
 * inventory item: 2 * t + 5 - random(10) - 1, at least 1; 0 for an empty
 * slot); a hand slot pointing at the inventory slot is cleared.  Then both
 * selections are reset and the rows redrawn.
 *
 * Original: IST.EXE ovl3FD1:2B1A (FRPOS.L), far cdecl (int far *sel, who,
 * u8 mode, base).  sel points at the caller's overlapping {sel, hold}
 * locals (see fr_item_exchange).  The trade-in random(10) is drawn twice
 * when the first result is >= 1, as in the original.
 * Verified: tests/py/test_frpos_shop.py (unicorn differential), 2026-11-21.
 */
void fr_shop_buy(int16_t *sel, int16_t who, uint8_t mode, int16_t base)
{
    int16_t t;

    if (mode && g_fr_shop(sel[1]) != 0) {
        int16_t iv = (int16_t)(FR_INV(who, sel[2]) % 100);
        int16_t price, k;

        if (iv != 0) {
            if ((int16_t)(FR_PRICE(iv) * 2 + 5 - bc_random(10) + (iv / 100) * 100 - 1) < 1)
                iv = 1;
            else
                iv = (int16_t)(FR_PRICE(iv) * 2 + 5 - bc_random(10) + (iv / 100) * 100 - 1);
        }
        price = (int16_t)(FR_PRICE(g_fr_shop(sel[1]) % 100) * 2 + base - iv);
        if (g_fr_money < price) {
            fr_draw_text_line(DGSTR(0x61ED));
            fr_shop_draw_text();
            ui_wait_click_or_key(0x3C);
            goto done;
        }
        g_fr_money = (int16_t)(g_fr_money - price);
        fr_shop_draw_prices(mode);
        for (k = 0; k < 3; k++)
            if (FR_HAND(who, k) == sel[2] + 0x22)
                FR_HAND(who, k) = 0;
    } else if (mode) {
        goto done;
    }
    t = FR_INV(who, sel[2]);
    FR_INV(who, sel[2]) = g_fr_shop(sel[1]);
    g_fr_shop(sel[1]) = t;
done:
    fr_shop_draw_cursor();
    sel[2] = 0;
    sel[1] = 0;
    fr_shop_draw_item(who);
    fr_shop_draw_text();
    gr_cursor_update(g_ui_shape);
}

/*
 * fr_shop_draw_prices - the currency line: in a shop with the money panel
 * on the screen (5C0A = 0) just fr_draw_money; otherwise three fields
 * "%d %s" of the money words BB73/BB71/BB6F (mode != 0) or of the treasure
 * found BBA6/BBA4/BBA2 (mode 0, empty when 0) with the currency names
 * (16E0:5BC3, 7 bytes each).
 *
 * Original: IST.EXE ovl3FD1:2D6F (FRPOS.L), far cdecl.
 * Verified: tests/py/test_frpos_shop.py (unicorn differential), 2026-11-21.
 */
void fr_shop_draw_prices(uint8_t mode)
{
    char buf[0x10];
    int16_t i;

    if (mode && !DG8(0x5C0A)) {
        fr_draw_money();
        return;
    }
    for (i = 1; i <= 3; i++) {
        if (mode)
            sprintf(buf, DGSTR(0x61FD), DGS16((uint16_t)(0xBB6D + 2 * (4 - i))),
                    DGSTR((uint16_t)(0x5BC3 + 7 * (3 - i))));
        else if (DGS16((uint16_t)(0xBBA0 + 2 * (4 - i))) != 0)
            sprintf(buf, DGSTR(0x6203), DGS16((uint16_t)(0xBBA0 + 2 * (4 - i))),
                    DGSTR((uint16_t)(0x5BC3 + 7 * (3 - i))));
        else
            buf[0] = 0;
        if (strlen(buf) > 0x0E)
            o559A_fatal(DGSTR(0x6209), 0x4B1, NULL, 0x5E);
        grt_decode_turkish_lower(buf);
        sys_fill_rect((uint16_t)((i - 1) * 0x5A + 0x2A), 0x10, 0x57, 9, 0x53, 0);
        grt_draw_text(0x53, (int16_t)((i - 1) * 0x5A + 0x2C), 0x0F, buf, g_ui_col_text, 5);
        grt_draw_text(0x53, (int16_t)((i - 1) * 0x5A + 0x2B), 0x0E, buf, g_ui_col_initial, 5);
    }
}

/* the selection frame of slot sel in row pane (toggle, 14 x 14) */
static void frame(int16_t pane, int16_t sel)
{
    sys_fill_rect_checked((int16_t)((sel - 1) * 0x11 + 7), (int16_t)((pane - 1) * 0x11 + 0x1B),
                          0x0E, 0x0E, 0x53, 0xDC);
}

/* item word -> D5E6 (count) / D5EA (id) */
static void select_item(int16_t w)
{
    g_cs_item_count = (int16_t)(w / 100);
    g_cs_item_id = (int16_t)(w % 100);
}

/* sell the inventory item in slot s of member who for 2t + 5 - random(10)
   + 100 * (id / 100) + d (at least 1); clears it and hand slots on it */
static void sell(int16_t who, int16_t *slot, int16_t d)
{
    int16_t id = (int16_t)(FR_INV(who, *slot) % 100);
    int16_t p, k;

    p = (int16_t)(FR_PRICE(id) * 2 + 5 - bc_random(10) +
                  (FR_INV(who, *slot) / 100) * 100 + d);
    if (p < 1) {
        p = 1;
    } else {
        id = (int16_t)(FR_INV(who, *slot) % 100);
        p = (int16_t)(FR_PRICE(id) * 2 + 5 - bc_random(10) +
                      (FR_INV(who, *slot) / 100) * 100 + d);
    }
    g_fr_money = (int16_t)(g_fr_money + p);
    for (k = 0; k < 3; k++)
        if (FR_HAND(who, k) == *slot + 0x22)
            FR_HAND(who, k) = 0;
    FR_INV(who, *slot) = 0;
    *slot = 0;
}

/* change the other currencies into kIvrIk: 10 + random(9) per BB6F unit
   (once), 5 + random(2) per BB71 unit */
static void convert(void)
{
    if (DGS16(0xBB6F) != 0)
        g_fr_money = (int16_t)(g_fr_money + DGS16(0xBB6F) * 10 + bc_random(9));
    if (DGS16(0xBB71) != 0)
        g_fr_money = (int16_t)(g_fr_money + DGS16(0xBB71) * 5 + bc_random(2));
    DGS16(0xBB71) = 0;
    DGS16(0xBB6F) = 0;
}

/*
 * fr_item_exchange - the item exchange screen.  mode 0: treasure (script
 * op 9: currencies BBA2/4/6 added to the money, items in BB57..), 1: the
 * weapon shop, 2: the goods shop (a random stock kept per district in
 * BBB8.. / BBAD.., whose byte 0 is the price base, random on first visit).
 *
 * Keys: F1-F4 member, Left/Right move the selection, Enter select /
 * exchange, I select the inventory, S sell, B convert currency, Esc
 * leave; F5 drops the selected inventory item.  Mouse: the zones listed
 * at the top of this file; right button cancels the selection.
 *
 * Original: IST.EXE ovl3FD1:2F21 (FRPOS.L), far cdecl (u8 mode).  The
 * locals sel[] (bp-8, indexed by the row 1..2) and hold[] (bp-6) overlap:
 * hold[r] = sel[r + 1]; "hold[other]" (row ^ 1) thus reads sel[2] for row
 * 1 and sel[1] for row 2, i.e. "the other row has a selection".  The loop
 * variable of the sell handlers (bp-2) is hold[2], unused otherwise.  The
 * idle callback (16E0:964C) runs every pass.
 * Verified: tests/py/test_frpos_shop.py (unicorn differential), 2026-11-21.
 */
void fr_item_exchange(uint8_t mode)
{
    char title[0x32];
    int16_t loc[4];                 /* bp-8 .. bp-2: sel[r] = loc[r] */
    int16_t *sel = loc;
    int16_t div = 0xCD;
    int16_t pane, who = 1, last, z, base = 0, i;
    int16_t running;
    uint16_t key;

    memcpy(title, DGP(0x5C0C), 0x32);
    q3d_free_anim_buffer(0x0F);
    ui_restore_background();
    memcpy(DGP(0x962C), DGP(0x97DC), 0x48);
    mm_change_type(mm_ref(0xE537), 0);
    switch (mode) {
    case 0:
        sprintf(title, "%s", DGSTR(0x6218));                     /* "hazine bulduk" */
        DGS16(0xBB6F) = (int16_t)(DGS16(0xBB6F) + g_fr_xchg_a);
        DGS16(0xBB71) = (int16_t)(DGS16(0xBB71) + g_fr_xchg_b);
        g_fr_money = (int16_t)(g_fr_money + g_fr_xchg_c);
        base = 0;
        fr_shop_draw_prices(mode);
        break;
    case 1:
        if (DG8(0xBBB7) == 0) {
            for (i = 1; bc_random(8) + 3 >= i; i++) {
                g_fr_shop(i) = (int16_t)(bc_random(0x1F) + 1);
                DG8((uint16_t)(0xBBB7 + i)) = (uint8_t)g_fr_shop(i);
            }
            for (; i <= 10; i++) {
                DG8((uint16_t)(0xBBB7 + i)) = 0;
                g_fr_shop(i) = 0;
            }
            DG8(0xBBB7) = (uint8_t)(bc_random(10) + 1);
        } else {
            for (i = 1; i <= 10; i++)
                g_fr_shop(i) = DG8((uint16_t)(0xBBB7 + i));
        }
        g_fr_xchg_c = g_fr_xchg_b = g_fr_xchg_a = 0;
        base = DG8(0xBBB7);
        break;
    case 2:
        if (DG8(0xBBAC) == 0) {
            for (i = 1; bc_random(8) + 3 >= i; i++) {
                g_fr_shop(i) = (int16_t)(bc_random(10) + 0x21);
                DG8((uint16_t)(0xBBAC + i)) = (uint8_t)g_fr_shop(i);
            }
            for (; i <= 10; i++) {
                DG8((uint16_t)(0xBBAC + i)) = 0;
                g_fr_shop(i) = 0;
            }
            DG8(0xBBAC) = (uint8_t)(bc_random(10) + 0x21);
        } else {
            for (i = 1; i <= 10; i++)
                g_fr_shop(i) = DG8((uint16_t)(0xBBAC + i));
        }
        g_fr_xchg_c = g_fr_xchg_b = g_fr_xchg_a = 0;
        base = DG8(0xBBAC);
        break;
    default:
        o559A_fatal(DGSTR(0x6226), 0x4FA, NULL, 0x5F);
        break;
    }
    fr_shop_draw_cursor();
    pane = 1;
    sel[pane] = 0;
    loc[pane + 1] = 0;                                     /* hold[pane] */
    g_fr_shop(0) = 0;
    if (sel[pane])
        frame(pane, sel[pane]);
    who = 1;
    fr_shop_draw_item(who);
    last = gr_zone_under_cursor();
    fr_draw_text_line(title);
    fr_shop_draw_prices(mode);
    fr_shop_load_gfx();
    fr_shop_draw_cursor();
    fr_shop_draw_item(who);
    g_zone_x1(0x15) = (int16_t)(g_fr_panel_x + 7);
    g_zone_y1(0x15) = (int16_t)((g_fr_panel_y + 3) % div);
    g_zone_x2(0x15) = (int16_t)(g_zone_x1(0x15) + 0x1F);
    g_zone_y2(0x15) = (int16_t)(g_zone_y1(0x15) + 0x15);
    g_zone_x1(0x1A) = (int16_t)(g_fr_panel_x + 0x12E);
    g_zone_y1(0x1A) = (int16_t)((g_fr_panel_y + 3) % div);
    g_zone_x2(0x1A) = (int16_t)(g_zone_x1(0x1A) + 8);
    g_zone_y2(0x1A) = (int16_t)(g_zone_y1(0x1A) + 0x0A);
    g_zone_x1(0x1B) = (int16_t)(g_fr_panel_x + 0x11B);
    g_zone_y1(0x1B) = (int16_t)((g_fr_panel_y + 3) % div);
    g_zone_x2(0x1B) = (int16_t)(g_zone_x1(0x1B) + 0x0F);
    g_zone_y2(0x1B) = (int16_t)(g_zone_y1(0x1B) + 0x0A);
    g_zone_x1(0x1C) = (int16_t)(g_fr_panel_x + 0x10E);
    g_zone_y1(0x1C) = (int16_t)((g_fr_panel_y + 3) % div);
    g_zone_x2(0x1C) = (int16_t)(g_zone_x1(0x1C) + 0x0C);
    g_zone_y2(0x1C) = (int16_t)(g_zone_y1(0x1C) + 0x0A);
    g_zone_x1(0x1D) = g_fr_panel_x;
    g_zone_y1(0x1D) = (int16_t)(g_fr_panel_y % div);
    g_zone_x2(0x1D) = (int16_t)(g_zone_x1(0x1D) + 0x140);
    g_zone_y2(0x1D) = (int16_t)(g_zone_y1(0x1D) + 0x3E);
    running = 1;
    if (sel[pane]) {
        int16_t s = (int16_t)(g_fr_shop(1) % 100);

        gr_cursor_update((uint8_t)(s < 0x20 ? s - 1 : s + 1));
    } else {
        gr_cursor_update(g_ui_shape);
    }

    while (running) {
        ((void (*)(uint32_t))ui_far_fn(DG32(UI_CB_IDLE)))(g_ui_idle_arg);
        if (g_cursor_visible) {
            gr_cursor_update((uint8_t)g_cursor_shape);
            z = gr_zone_under_cursor();
            if (z > 0 && z < 0x15 && last != z) {
                last = z;
                if (z < 0x0B) {
                    select_item(FR_INV(who, z));
                    fr_item_name(0, 0);
                } else {
                    select_item(g_fr_shop(z - 10));
                    fr_item_name(mode, (int16_t)(FR_PRICE(g_cs_item_id) * 2 + base));
                }
                fr_shop_draw_text();
            }
        }
        if (ui_kbhit()) {
            key = (uint16_t)ui_bioskey(0);
            if ((key & 0xFF) == 0) {
                switch (key >> 8) {
                case 0x3B: case 0x3C: case 0x3D: case 0x3E:      /* F1-F4: member */
                    who = (int16_t)((key >> 8) - 0x3A);
                    fr_shop_draw_item(who);
                    if (pane == 2) {
                        if (sel[pane])
                            frame(pane, sel[pane]);
                        select_item(FR_INV(who, sel[pane]));
                        fr_item_name(0, 0);
                    }
                    fr_shop_draw_text();
                    gr_cursor_update(g_ui_shape);
                    break;
                case 0x3F:                                      /* F5: drop */
                    if (pane == 2 && sel[pane] && FR_INV(who, loc[2]) % 100 != 0) {
                        FR_INV(who, loc[2]) = 0;
                        fr_shop_draw_item(who);
                        frame(pane, sel[pane]);
                        sys_fill_rect_checked(7, 3, 0x1F, 0x15, 0x53, 0xDC);
                        fr_shop_draw_text();
                        ui_wait_click_or_key(0x0A);
                        sys_fill_rect_checked(7, 3, 0x1F, 0x15, 0x53, 0);
                        fr_shop_draw_item(who);
                        fr_shop_draw_text();
                    }
                    break;
                case 0x4D:                                      /* Right */
                    if (sel[pane] < 10) {
                        sel[pane]++;
                        goto moved;
                    }
                    break;
                case 0x4B:                                      /* Left */
                    if (sel[pane] > 1) {
                        sel[pane]--;
                        goto moved;
                    }
                    break;
                }
            } else {
                switch (key & 0xFF) {
                case 0x0D:                                      /* Enter */
                    if (sel[pane] == 0)
                        break;
                    if (loc[((pane - 1) ^ 1) + 1] != 0) {
                        fr_shop_buy(loc, who, mode, base);
                        pane = (int16_t)(((pane - 1) ^ 1) + 1);
                        break;
                    }
                    frame(pane, sel[pane]);
                    pane = (int16_t)(((pane - 1) ^ 1) + 1);
                    sel[pane] = 1;
                    frame(pane, sel[pane]);
                    select_item(FR_INV(who, sel[pane]));
                    fr_item_name(0, 0);
                    fr_shop_draw_text();
                    break;
                case 0x1B:                                      /* Esc */
                    running = 0;
                    break;
                case 'B': case 'b':                             /* convert */
                    if (mode) {
                        convert();
                        fr_shop_draw_prices(mode);
                        fr_shop_draw_text();
                    }
                    break;
                case 'S': case 's':                             /* sell */
                    if (mode && FR_INV(who, loc[2]) != 0 && pane == 2) {
                        sell(who, &loc[2], -1);
                        loc[3] = 3;
                        fr_shop_draw_item(who);
                        fr_shop_draw_prices(mode);
                        fr_shop_draw_text();
                    }
                    break;
                case 'I': case 'i':                             /* inventory */
                    if (sel[pane] == 0) {
                        pane = 1;
                        sel[pane] = 1;
                        goto moved;
                    }
                    break;
                }
            }
            goto keys_done;
moved:
            if (pane == 1) {
                fr_shop_draw_cursor();
                frame(pane, sel[pane]);
                select_item(g_fr_shop(sel[pane]));
                fr_item_name(mode, (int16_t)(FR_PRICE(g_cs_item_id) * 2 + base));
            } else {
                fr_shop_draw_item(who);
                frame(pane, sel[pane]);
                select_item(FR_INV(who, sel[pane]));
                fr_item_name(0, 0);
            }
            fr_shop_draw_text();
            if (loc[((pane - 1) ^ 1) + 1] == 0) {
                if (g_cs_item_id != 0) {
                    g_cs_item_id = icon_of(g_cs_item_id);
                    gr_cursor_update((uint8_t)(g_cs_item_id - 1));
                } else {
                    gr_cursor_update(g_ui_shape);
                }
            }
        }
keys_done:
        if (g_mouse_buttons == 3) {
            running = 0;
            continue;
        }
        if (g_mouse_buttons == 1) {
            z = gr_zone_under_cursor();
            if (z > 0 && z <= 10) {                              /* inventory slot */
                pane = 2;
                if (loc[((pane - 1) ^ 1) + 1] == 0) {
                    fr_shop_draw_item(who);
                    g_cs_item_id = (int16_t)(FR_INV(who, z) % 100);
                    if (g_cs_item_id != 0) {
                        g_cs_item_id = icon_of(g_cs_item_id);
                        sel[pane] = z;
                        g_cursor_shape = (int16_t)(g_cs_item_id - 1);
                    } else {
                        g_cursor_shape = g_ui_shape;
                    }
                } else {
                    sel[pane] = z;
                    fr_shop_buy(loc, who, mode, base);
                    pane = (int16_t)(((pane - 1) ^ 1) + 1);
                }
                select_item(FR_INV(who, sel[pane]));
                fr_item_name(0, 0);
                fr_shop_draw_text();
            } else if (z > 10 && z < 0x15) {                     /* shop slot */
                pane = 1;
                if (loc[((pane - 1) ^ 1) + 1] == 0) {
                    fr_shop_draw_item(who);
                    g_cs_item_id = (int16_t)(g_fr_shop(z - 10) % 100);
                    if (g_cs_item_id != 0) {
                        g_cs_item_id = icon_of(g_cs_item_id);
                        sel[pane] = (int16_t)(z - 10);
                        g_cursor_shape = (int16_t)(g_cs_item_id - 1);
                    } else {
                        g_cursor_shape = g_ui_shape;
                    }
                } else {
                    sel[pane] = (int16_t)(z - 10);
                    fr_shop_buy(loc, who, mode, base);
                    pane = (int16_t)(((pane - 1) ^ 1) + 1);
                }
                select_item(g_fr_shop(sel[pane]));
                fr_item_name(mode, (int16_t)(FR_PRICE(g_cs_item_id) * 2 + base));
                fr_shop_draw_text();
            } else if (z == 0x15) {                              /* drop */
                if (pane == 2 && FR_INV(who, loc[2]) % 100 != 0 && sel[pane] != 0) {
                    FR_INV(who, loc[2]) = 0;
                    fr_shop_draw_item(who);
                    frame(pane, sel[pane]);
                    sys_fill_rect_checked(7, 3, 0x1F, 0x15, 0x53, 0xDC);
                    fr_shop_draw_text();
                    ui_wait_click_or_key(0x0A);
                    sys_fill_rect_checked(7, 3, 0x1F, 0x15, 0x53, 0);
                    fr_shop_draw_text();
                }
            } else if (z > 0x15 && z < 0x1A) {                   /* member */
                who = (int16_t)(z - 0x15);
                fr_shop_draw_item(who);
                if (pane == 2) {
                    select_item(FR_INV(who, sel[pane]));
                    fr_item_name(0, 0);
                }
                gr_cursor_update(g_ui_shape);
                fr_shop_draw_text();
            } else if (z == 0x1A) {                              /* exit */
                running = 0;
            } else if (z == 0x1B && mode) {                      /* convert */
                convert();
                fr_shop_draw_prices(mode);
                fr_shop_draw_text();
            } else if (z == 0x1C) {                              /* sell */
                if (g_ui_shape != g_cursor_shape && mode && FR_INV(who, loc[2]) != 0 &&
                    pane == 2) {
                    sell(who, &loc[2], 1);
                    loc[3] = 3;
                    fr_shop_draw_item(who);
                    gr_cursor_update(g_ui_shape);
                    fr_shop_draw_prices(mode);
                    fr_shop_draw_text();
                }
            }
            gr_wait_buttons_up();
        } else if (g_mouse_buttons == 2) {
            gr_cursor_update(g_ui_shape);
            if (pane == 1)
                fr_shop_draw_cursor();
            else
                fr_shop_draw_item(who);
            fr_shop_draw_text();
            sel[pane] = 0;
        }
    }
    gr_cursor_update(g_ui_shape);
    for (i = 1; i < 0x0B; i++) {
        if (mode == 1)
            DG8((uint16_t)(0xBBB7 + i)) = (uint8_t)g_fr_shop(i);
        else if (mode == 2)
            DG8((uint16_t)(0xBBAC + i)) = (uint8_t)g_fr_shop(i);
        g_fr_shop(i) = 0;
    }
    g_fr_xchg_c = g_fr_xchg_b = g_fr_xchg_a = 0;
    fr_shop_free_gfx();
    sys_fill_rect(0x29, 3, 0x78, 0x0B, 0x53, 0);
    for (i = 1; i <= 3; i++)
        sys_fill_rect((uint16_t)((i - 1) * 0x5A + 0x2A), 0x10, 0x57, 9, 0x53, 0);
    mm_change_type(mm_ref(0xE537), 1);
    q3d_wait_retrace(3);
    memcpy(DGP(0x962C), DGP(0x9824), 0x48);
    s3d_draw_party_panel();
    s3d_draw_status_line(0xC6, 0xB2);
    s3d_redraw_view(1);
}
