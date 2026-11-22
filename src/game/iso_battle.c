/*
 * iso_battle.c - ISOSYS.L 4595:B04D-D7CB: the item icons of the panel and
 * iso_battle, the entry point of a battle (called by 3DSYS 303F:8524 and
 * FRPOS 3FD1:0DF6).  See iso.h.
 */
#include "game/iso.h"

#include <stdio.h>
#include <string.h>

#include "dsmi/amp.h"
#include "dsmi/emsheap.h"
#include "dsmi/memalloc.h"
#include "engine/blit386.h"
#include "engine/fileman.h"
#include "engine/grtext.h"
#include "engine/intrface.h"
#include "engine/memman.h"
#include "engine/ngraphs.h"
#include "engine/nkontrol.h"
#include "engine/ographs.h"
#include "engine/systems.h"
#include "game/demolar.h"
#include "game/q3dview.h"
#include "game/s3d.h"
#include "game/s3d_ext.h"
#include "rtl/bc_rand.h"

/* item value -> icon number in ITEMBANK (slot 4Bh, 15x15 icons):
   2Bh -> 22h, ids above 20h move up by 2 */
static int16_t icon_of(int16_t v)
{
    int16_t m = (int16_t)(v % 100);
    if (m == 0x2B)
        m = 0x22;
    else if (m > 0x20)
        m = (int16_t)(m + 2);
    return m;
}

/*
 * iso_draw_item_icon - inventory slot k (1..10, five per row from
 * (ECh, A5h), 17 px apart) of member p on the work screen (slot 6): the
 * item's icon (id in D5EA) on a black 16x16 square, or the empty-slot
 * picture (icon x 1E0h).  With `zone` also hot zone k (cleared when the
 * slot is empty).
 *
 * Original: IST.EXE ovl4595:B04D (ISOSYS.L), far cdecl (k, p, zone).
 * Verified: tests/py/test_iso_c.py (test_panel), 2026-11-21.
 */
void iso_draw_item_icon(int16_t k, int16_t p, int16_t zone)
{
    int16_t col = (int16_t)((k - 1) % 5), row = (int16_t)((k - 1) / 5);
    uint16_t x = (uint16_t)(col * 0x11 + 0xEC), y = (uint16_t)(row * 0x11 + 0xA5);

    g_cs_item_id = icon_of(g_s3d_item(p, k));
    if (g_cs_item_id != 0) {
        sys_fill_rect((uint16_t)(col * 0x11 + 0xEB), (uint16_t)(row * 0x11 + 0xA4), 0x10, 0x10,
                      6, 0);
        sys_blit((uint16_t)((g_cs_item_id - 1) * 0x0F), 0, 0x0F, 0x0F, x, y, 0x4B, 6,
                 asm_copy_rect_key);
        if (zone == 0)
            return;
        g_zone_x1(k) = (int16_t)x;
        g_zone_y1(k) = (int16_t)y;
        g_zone_x2(k) = (int16_t)(col * 0x11 + 0xFB);
        g_zone_y2(k) = (int16_t)(row * 0x11 + 0xB4);
    } else {
        sys_blit(0x1E0, 0, 0x0F, 0x0F, x, y, 0x4B, 6, asm_copy_rect_key);
        if (zone != 0) {
            g_zone_y2(k) = 0;
            g_zone_x2(k) = 0;
            g_zone_y1(k) = 0;
            g_zone_x1(k) = 0;
        }
    }
}

/*
 * iso_draw_portrait - hand slot k (1 hand 1, 2 hand 2, 3 armour; at
 * (3Bh + 17*(k-1), A5h)) of member p: the item's icon on black, or the
 * empty slot restored from the background (slot 1).  Hand 2 is crossed
 * out (gr_draw_strip colour FDh) while hand 1 holds a two-handed weapon
 * (item table +2 == 2).  With `zone`, hot zone 10+k.  k == 4 clears a
 * 14x14 square at (6Eh, A5h) on the screen; k 0 or > 4 does nothing.
 *
 * Original: IST.EXE ovl4595:B22F (ISOSYS.L), far cdecl (k, p, zone).
 * Verified: tests/py/test_iso_c.py (test_panel), 2026-11-21.
 */
void iso_draw_portrait(int16_t k, int16_t p, int16_t zone)
{
    uint16_t x = (uint16_t)((k - 1) * 0x11 + 0x3B);
    int16_t m, w;

    if (k >= 4 || k == 0) {
        if (k == 4)
            sys_fill_rect_checked((int16_t)x, 0xA5, 0x0E, 0x0E, 0, 0);
        return;
    }
    m = icon_of(DGS16((uint16_t)(0x99A6 + 0x5C * p + 2 * g_s3d_stat(p, 0x3A + 2 * k))));
    if (m != 0) {
        sys_fill_rect((uint16_t)((k - 1) * 0x11 + 0x3A), 0xA4, 0x10, 0x10, 6, 0);
        sys_blit((uint16_t)((m - 1) * 0x0F), 0, 0x0F, 0x0F, x, 0xA5, 0x4B, 6, asm_copy_rect_key);
    } else {
        sys_blit(x, 0xA5, 0x0F, 0x0F, x, 0xA5, 1, 6, sys_rect_copy);
    }
    w = (int16_t)(DGS16((uint16_t)(0x99A6 + 0x5C * p + 2 * g_s3d_stat(p, 0x3C))) % 100);
    if (w == 0x2B)
        w = 0x1A;
    if (k == 2 && g_s3d_item_hands(w) == 2)
        gr_draw_strip(x, 0xA5, 0x0F, 0x0F, 6, 0xFD);
    if (zone != 0) {
        g_zone_x1(10 + k) = (int16_t)x;
        g_zone_y1(10 + k) = 0xA5;
        g_zone_x2(10 + k) = (int16_t)((k - 1) * 0x11 + 0x4A);
        g_zone_y2(10 + k) = 0xB4;
    }
}

/* ---- iso_battle ------------------------------------------------------------ */
#define UW(c, off)      DGS16((uint16_t)(0xA08A + 0x28 * (c) + (off)))      /* unit word */
#define TW(g, off)      DGS16((uint16_t)(0x9F18 + 0x2E * (g) + (off)))      /* template word */
#define MON_W(t, j)     DGS16((uint16_t)(0x1287 + 0x2E * (t) + 2 * (j)))    /* monster table */
#define PITEM(p, idx)   DGS16((uint16_t)(0x99A6 + 0x5C * (p) + 2 * (idx)))  /* record word idx */
#define g_bb53          DGS16(0xBB53)   /* i16  shared loop index */
#define g_iso_632a      DGS16(0x632A)
#define g_iso_632c      DGS16(0x632C)
#define g_iso_d5dc      DGS16(0xD5DC)
#define g_iso_d5d8      DGS16(0xD5D8)
#define MSG_BUF         0xA574          /* char[] line said by the actor */

/* the eight move keys (keypad scan codes) of the arrow zones 21..28 */
static const uint16_t arrow_keys[8] = { 0x47, 0x48, 0x49, 0x4B, 0x4D, 0x4F, 0x50, 0x51 };

/* turn the actor towards the target cursor (D57A, D578): D55E = 2 west,
   4 east, 1 north, 3 south (x first) */
static void face_cursor(void)
{
    if (iso_unit(g_iso_actor)->x > g_iso_cursor_x)
        g_iso_facing = 2;
    else if (iso_unit(g_iso_actor)->x < g_iso_cursor_x)
        g_iso_facing = 4;
    else if (iso_unit(g_iso_actor)->y > g_iso_cursor_y)
        g_iso_facing = 1;
    else
        g_iso_facing = 3;
    iso_unit(g_iso_actor)->facing = g_iso_facing;
}

/* the item of inventory slot k of member 2898 into D5E6 (count) / D5EA (id) */
static void select_item(int16_t k)
{
    g_cs_item_count = (int16_t)(g_s3d_item(g_party_cur, k) / 100);
    g_cs_item_id = (int16_t)(g_s3d_item(g_party_cur, k) % 100);
}

/* the item cursor and its description line under the panel: D5EA becomes
   the icon number; with `always` the cursor changes even for id 0 */
static void show_item(int16_t always)
{
    if (g_cs_item_id == 0x2B)
        g_cs_item_id = 0x22;
    else if (g_cs_item_id > 0x20)
        g_cs_item_id = (int16_t)(g_cs_item_id + 2);
    if (always || g_cs_item_id != 0)
        gr_cursor_update((uint8_t)(g_cs_item_id - 1));
    sys_fill_rect(0x18, 0x9D, 0x55, 6, 6, DG8(0x9666));
    iso_draw_text(2, 0x18, 0x9E, DGSTR(0xA510), 0);
    iso_draw_text(2, 0x18, 0x9D, DGSTR(0xA510), DG8(0x9665));
    iso_show_picture(6);
}

/* press-down look of a panel button: four lines in `slot` */
static void button_lines(const int16_t r[4][5], uint8_t slot)
{
    int i;
    for (i = 0; i < 4; i++)
        sys_fill_slot_rect(r[i][0], r[i][1], r[i][2], r[i][3], (uint8_t)r[i][4], slot);
}

static void wait_release(void)
{
    do
        gr_cursor_update((uint8_t)g_cursor_shape);
    while (g_mouse_buttons != 0);
}

/* the actor says the queued voice 631A: speaker 6320 = its group's monster
   type (monsters) or actor + 40 (party) */
static void say_voice(void)
{
    if (g_iso_actor <= 0x18)
        g_iso_speaker = DGS16((uint16_t)(0x5BC2 + 6 * iso_unit(g_iso_actor)->tmpl));
    else
        g_iso_speaker = (int16_t)(g_iso_actor + 0x28);
    iso_speak_vecize(g_iso_voice);
    g_iso_voice = 0;
    ui_wait_frames(10);
    ui_play_voice(0x21, 8);
}

/*
 * iso_battle - a whole battle.  The caller (3DSYS, FRPOS) sets the
 * encounter: g_iso_encounter(i, 0/1) for groups i = 1..3 (monster type,
 * +100 for a group that fights on the party's side; count), the options
 * BB34.. (BB37 battle music, BB38 effects, BB39 difficulty) and the
 * party's map position / facing (B9F3, B9F1, B9F0).
 *
 * Set-up: the battle window record (974C) becomes the current one (the
 * old one is saved on the stack and restored at the end), palbat.pal and
 * the tint table, battle music (random(2): track 0Ch or 10h) and its
 * volume, the graphics, the battlefield (iso_build_battlefield), the
 * party's units (iso_party_to_battle) and the monster templates 5..7
 * (hp = monster word 1 + difficulty * word 0; to-hit +1), units 1..24 (8
 * per group).  Each living unit gets a random free cell (random(9),
 * random(8)) in the 9x9 block of its side: the party's block D554/D552;
 * the enemy's the next block in the facing direction when the party's map
 * cell has no wall that way (cell bytes 0..7: wall picture < 8 and kind
 * not 1 or 3), else the same block.  The party faces the map direction,
 * the enemies the opposite one.
 *
 * Main loop: when iso_next_turn has started a turn (6324): monsters (side
 * 2) play (iso_ai_turn); a unit of neither side dies.  Otherwise input:
 * bioskey(1) scan code (D5D6; Ctrl-Break: flush + getch) or a click on a
 * hot zone; keypad / arrow zones 21..28 move (iso_move_step, the stun
 * weapon's effect D59A: random(2)+2 turns, the free attack D568 at the
 * end of the turn), 'v' / zone 30 vecize, Enter / zones 31, 34 attack
 * (when the AP cover the weapon's cost), Tab / zone 29 ends the turn; on
 * a party turn the panel: Q W E / zones 11..13 put down what the hand
 * slot holds, a right click on an inventory slot uses a consumable, 'i' /
 * a left click opens the inventory loop (arrows, Space uses, Enter then
 * chooses hand 1 / hand 2 / armour / throw away, Esc leaves).
 *
 * End (iso_next_turn returns -1, or 632E == 1 at a key read): with an
 * allied group of type 4, event flag 20 and the hp of the UNIT numbered
 * like that group (original quirk) < 0: BB9E = 1; graphics freed, unit
 * positions / sides / stun cleared, the party records updated
 * (iso_battle_to_party), fade out, the battle music stopped and freed,
 * the music track restored.
 *
 * Original: IST.EXE ovl4595:B40A (ISOSYS.L), far cdecl.  The original's
 * 'I' test compares the lower-cased word with FF8Dh, which the byte from
 * grt_tolower_inplace never gives (only 'i' opens the inventory).
 * bp-0Ah / bp-0Ch (hand kinds) keep their value between uses; their first
 * value is stack garbage in the original, 0 in the port.
 * Verified: tests/py/test_iso_c.py (scripted battles), 2026-11-21.
 */
void iso_battle(void)
{
    static const int16_t vec_lines[4][5] = {
        { 0xA9, 0x92, 0xCD, 0x92, 0xF8 }, { 0xA9, 0x92, 0xB6, 0x9F, 0xF8 },
        { 0xB7, 0xA0, 0xDC, 0xA0, 6 }, { 0xCE, 0x92, 0xDC, 0xA0, 6 } };
    static const int16_t atk_lines1[4][5] = {
        { 0xD0, 0x92, 0xF4, 0x92, 0xF8 }, { 0xD0, 0x92, 0xDD, 0x9F, 0xF8 },
        { 0xDE, 0xA0, 0x103, 0xA0, 6 }, { 0xF5, 0x92, 0x103, 0xA0, 6 } };
    static const int16_t atk_lines2[4][5] = {
        { 0x17, 0x59, 0x22, 0x59, 0xF8 }, { 0x17, 0x59, 0x21, 0x63, 0xF8 },
        { 0x23, 0x64, 0x2D, 0x64, 6 }, { 0x23, 0x59, 0x2E, 0x64, 6 } };
    static const int16_t end_lines[4][5] = {
        { 0x82, 0x92, 0xA6, 0x92, 0xF8 }, { 0x82, 0x92, 0x8F, 0x9F, 0xF8 },
        { 0x90, 0xA0, 0xB5, 0xA0, 6 }, { 0xA7, 0x92, 0xB5, 0xA0, 6 } };
    uint8_t saved_win[UI_WIN_SIZE];      /* bp-6Ch */
    int16_t i, j;                        /* bp-2, bp-4 */
    int16_t zone = 0, buttons = 0;       /* bp-6, bp-8 */
    int16_t kind1 = 0, kind2 = 0;        /* bp-0Ah, bp-0Ch: hands of an item */
    int16_t allied;                      /* bp-0Eh */
    int16_t kbd_mouse = 0;               /* bp-10h: 7324 + 1 while the keyboard
                                            drives the panel */
    int16_t facing;                      /* bp-14h */
    int16_t edx, edy;                    /* bp-18h, bp-16h: enemy block offset */
    int16_t bx, by;                      /* bp-1Ah, bp-1Ch */
    int16_t mouse;                       /* bp-1Eh */
    int16_t music = 0;                   /* bp-20h (set only with sound) */
    int16_t ch = 0;                      /* bp-22h: lower-cased ASCII */
    int16_t key2 = 0;                    /* bp-24h: scan code in the inventory */
    uint8_t cell[8];
    int16_t t;

    g_iso_rolled = 0;
    memcpy(saved_win, DGP(UI_WIN_CUR), UI_WIN_SIZE);
    memcpy(DGP(UI_WIN_CUR), DGP(0x974C), UI_WIN_SIZE);
    g_iso_view_y = (int16_t)0xFEC8;
    g_iso_view_x = (int16_t)0xFEC8;
    DG16(0x3D5E) = 0;
    g_s3d_fade_tempo = 0;
    g_fade_volume_d = 0;
    g_fade_bright_d = 0;
    s3d_apply_daylight();
    fm_load_pal(DGSTR(0x6880), 0, 0x100);                       /* "palbat.pal" */
    mm_ems_lock(g_blit_lock, 0xC21, DGSTR(0x688B));
    sys_build_aa_table(EMSLock((int16_t)mm_ref(G_RIX_TABLE_REF)->ems, 0, 0x7F80), 0);
    mm_ems_unlock(g_blit_lock);
    if (g_pal_level == 0) {
        g_fade_bright_d += g_pal_level_max;
        g_fade_volume_d = g_music_level_max;
    }
    if (g_sound_on != 0)
        music = g_music_track;
    if (g_sound_on != 0 && g_s3d_opt(3) != 0) {
        demo_play_music(bc_random(2) != 0 ? 0x0C : 0x10, 1, 0);
        if (g_s3d_opt(4) != 0) {
            if (g_music_track == 0x0C)
                g_fade_volume_d -= 0x1E;
            else
                g_fade_volume_d -= 0x19;
        }
        g_music_level += g_fade_volume_d;
        ampSetTrackVolume(-1, (int32_t)g_music_level);
    }
    mouse = g_mouse_present;
    g_iso_632a = 1;
    g_iso_632c = 2;
    g_iso_round = 0;
    g_iso_monsters = 0;
    g_iso_d5a6 = 0;
    for (i = 1; i <= 3; i++)
        g_iso_monsters += g_iso_encounter(i, 1);
    ui_init_voices();
    iso_load_battle_gfx();
    iso_build_battlefield();
    gr_zones_clear();
    iso_party_to_battle();

    /* monster templates 5..7 */
    allied = 0;
    for (i = 1; i <= 3; i++) {
        if (g_iso_encounter(i, 0) > 100) {
            g_iso_encounter(i, 0) -= 100;
            allied = i;
        }
        if (g_iso_encounter(i, 0) > 0) {
            t = g_iso_encounter(i, 0);
            TW(i + 4, 0x02) = (int16_t)(MON_W(t, 1) + g_s3d_opt(5) * MON_W(t, 0));
            for (j = 2; j <= 0x13; j++)
                TW(i + 4, 2 * j) = MON_W(t, j);
        } else {
            g_iso_encounter(i, 1) = 0;
            for (j = 1; j <= 0x13; j++)
                TW(i + 4, 2 * j) = 0;
        }
        TW(i + 4, 0x06)++;
    }
    /* monster units: 8 per group */
    for (i = 1; i <= 3; i++) {
        if (g_iso_encounter(i, 1) == 0)
            continue;
        for (j = (int16_t)((i - 1) * 8 + 1); j <= (i - 1) * 8 + g_iso_encounter(i, 1); j++) {
            int16_t k;
            UW(j, 0x00) = TW(i + 4, 0x02);
            UW(j, 0x02) = TW(i + 4, 0x12);
            UW(j, 0x04) = 1;
            UW(j, 0x06) = (int16_t)(i + 4);
            UW(j, 0x0E) = allied == i ? 1 : 2;
            UW(j, 0x12) = TW(i + 4, 0x06);
            UW(j, 0x14) = TW(i + 4, 0x08);
            for (k = 0x16; k <= 0x20; k += 2)
                UW(j, k) = TW(i + 4, k);
        }
    }
    for (g_iso_cell_x = 0; g_iso_cell_x < 0x1C; g_iso_cell_x++)
        for (g_iso_cell_y = 0; g_iso_cell_y < 0x1C; g_iso_cell_y++)
            g_iso_occ(g_iso_cell_x, g_iso_cell_y) = 0;

    /* facing: map 0 north, 1 east, 2 south, 3 west -> unit 1, 4, 3, 2 */
    g_s3d_facing = (uint8_t)(g_s3d_facing % 4);
    facing = (int16_t)(g_s3d_facing + 1);
    if (facing == 2)
        facing = 4;
    else if (facing == 4)
        facing = 2;
    mm_ems_unlock((MmEmsLock *)DGP(0xBB3B));
    /* the party's map cell, bytes 0..7; the original reads it through the
       huge pointer B9EC plus the 16-bit offset, sign-extended (F_PADD) */
    for (i = 0; i < 8; i++)
        cell[i] = s3d_map[(int16_t)((int16_t)(g_s3d_map_w * g_s3d_y + g_s3d_x) * 15 + i)];
    DGS16(0xD53E) = cell[0];
    DGS16(0xD546) = cell[1];
    DGS16(0xD53C) = cell[2];
    DGS16(0xD544) = cell[3];
    DGS16(0xD53A) = cell[4];
    DGS16(0xD542) = cell[5];
    DGS16(0xD538) = cell[6];
    DGS16(0xD540) = cell[7];
    edy = 0;
    if (g_s3d_facing == 0 && DGS16(0xD546) != 1 && DGS16(0xD53E) < 8 && DGS16(0xD546) != 3) {
        edx = 0;
        edy = -1;
    } else if (g_s3d_facing == 1 && DGS16(0xD544) != 1 && DGS16(0xD53C) < 8
               && DGS16(0xD544) != 3) {
        edx = 1;
    } else if (g_s3d_facing == 2 && DGS16(0xD542) != 1 && DGS16(0xD53A) < 8
               && DGS16(0xD542) != 3) {
        edx = 0;
        edy = 1;
    } else if (g_s3d_facing == 3 && DGS16(0xD540) != 1 && DGS16(0xD538) < 8
               && DGS16(0xD540) != 3) {
        edx = -1;
    } else {
        edx = 0;
    }
    /* placement: until a free cell that does not block a step */
    for (j = 1; j <= 0x1C; j++) {
        if (UW(j, 0x00) <= 0)
            continue;
        do {
            if (UW(j, 0x0E) == 1) {
                bx = g_iso_party_bx;
                by = g_iso_party_by;
                UW(j, 0x04) = facing;
            } else {
                bx = (int16_t)(g_iso_party_bx + edx);
                by = (int16_t)(g_iso_party_by + edy);
                UW(j, 0x04) = (int16_t)((facing + 1) % 4 + 1);
            }
            g_iso_cell_x = bc_random(9);
            g_iso_cell_y = bc_random(8);
            g_iso_cell_x = (int16_t)(g_iso_cell_x + bx * 9);
            g_iso_cell_y = (int16_t)(g_iso_cell_y + by * 9);
        } while (g_iso_occ(g_iso_cell_x, g_iso_cell_y) != 0
                 || g_iso_terrain(g_iso_cell_x, g_iso_cell_y) >= 10
                 || g_iso_terrain(g_iso_cell_x, g_iso_cell_y) == 8
                 || g_iso_terrain(g_iso_cell_x, g_iso_cell_y) == 6);
        UW(j, 0x08) = g_iso_cell_x;
        UW(j, 0x0A) = g_iso_cell_y;
        g_iso_occ(UW(j, 0x08), UW(j, 0x0A)) = j;
    }
    ogr_mouse_set_pos(0, 0);
    ogr_mouse_set_window(0, 0, 0x13F, 0xC7);
    g_cursor_shape = 0x2D;
    gr_cursor_set_shape((uint8_t)g_cursor_shape);
    g_iso_result = 0;
    iso_next_turn();
    gr_fade(500, g_fade_bright_d, 0);
    g_fade_volume_d = 0;
    g_fade_bright_d = 0;

    for (;;) {                                                   /* BF32 */
        if (g_iso_new_turn != 0) {
            g_iso_new_turn = 0;
            if (iso_unit(g_iso_actor)->side == 2) {
                gr_cursor_hide();
                g_mouse_present = 0;
                iso_ai_turn();
                if (iso_next_turn() == -1 || g_iso_result == 1)
                    goto end;
                continue;
            }
            if (iso_unit(g_iso_actor)->side != 1) {
                iso_unit(g_iso_actor)->hp = 0;
                g_iso_occ(iso_unit(g_iso_actor)->x, iso_unit(g_iso_actor)->y) = 0;
            }
            g_mouse_present = (uint8_t)mouse;
            gr_cursor_set_shape((uint8_t)g_cursor_shape);
        }
        for (;;) {                       /* BFF5: wait for a key or a click on a zone */
            char c8;
            g_iso_key = (uint16_t)ui_bioskey(1);
            c8 = (char)(g_iso_key & 0xFF);
            grt_tolower_inplace(&c8);
            ch = (uint8_t)c8;
            if (g_iso_key == 0xFFFF) {                           /* Ctrl-Break */
                iso_dos_break((int16_t)g_iso_key);   /* INT 23h at the flush's kbhit(): iso.h */
                ctl_flush_keys();
                /* C022: INT 1Bh (the break flag again) - never reached under DOS */
                ui_getch();
            }
            g_iso_key >>= 8;
            if (g_iso_key != 0)
                ctl_flush_keys();
            if (g_iso_result == 1)
                goto end;
            zone = gr_zone_under_cursor();
            buttons = g_mouse_buttons;
            gr_cursor_update((uint8_t)g_cursor_shape);
            if (g_iso_key != 0 || (zone > 0 && buttons > 0))
                break;
        }
        if (g_iso_key == 0 && (g_mouse_buttons == 0 || gr_zone_under_cursor() == 0))
            continue;
        zone = gr_zone_under_cursor();
        buttons = g_mouse_buttons;

        /* C0B5: move */
        {
            int moving = zone > 0x14 && zone < 0x1D && buttons == 1;
            for (i = 0; i < 8 && !moving; i++)
                moving = g_iso_key == arrow_keys[i];
            if (moving) {
                if (zone > 0x14 && zone < 0x1D && buttons == 1)
                    g_iso_key = arrow_keys[zone - 0x15];
                iso_move_step();
                iso_after_hit();
                if (g_iso_stunned == 1 && iso_unit(g_iso_defender)->hp > 0) {
                    iso_unit(g_iso_defender)->stun = (int16_t)(bc_random(2) + 2);
                    g_iso_anim_kind = 5;
                    g_iso_anim_unit = g_iso_defender;
                    g_iso_sfx = 8;
                    iso_redraw();
                    iso_play_sfx(g_iso_sfx);
                    ui_wait_frames(10);
                    g_iso_sfx = 0;
                    g_iso_anim_kind = 1;
                    g_iso_anim_unit = g_iso_actor;
                    iso_redraw();
                    iso_play_sfx(g_iso_sfx);
                    ui_wait_frames(10);
                }
                g_iso_stunned = 0;
                if (g_iso_ap == 0) {
                    if (g_iso_flee == 1 && iso_unit(g_iso_defender)->hp > 0) {
                        int16_t d = g_iso_defender;           /* free attack back */
                        g_iso_defender = g_iso_attacker;
                        g_iso_attacker = d;
                        g_iso_free_hit = 1;
                        iso_attack();
                        g_iso_free_hit = 0;
                        g_iso_flee = 0;
                    }
                    iso_redraw();
                    ui_wait_frames(10);
                    if (iso_next_turn() == -1)
                        goto end;
                }
                if (g_iso_actor <= 0x18 && iso_unit(g_iso_actor)->side == 1) {
                    iso_redraw();
                    ui_wait_frames(10);
                }
            }
        }

        /* C294: vecize */
        if ((ch == 'v' || (buttons == 1 && zone == 0x1E)) && iso_unit(g_iso_actor)->spell != 0
            && iso_unit(g_iso_actor)->hit == 0) {
            button_lines(vec_lines, 6);
            iso_show_picture(6);
            wait_release();
            g_iso_d5dc = 0;
            g_iso_d5d8 = 0;
            g_cs_desc_prev = 0;
            g_iso_sfx = 0;
            g_iso_los_dist = 0;
            g_iso_los_clear = 0;
            iso_cmd_vecize();
            g_iso_target = -1;
            if (g_iso_spell_arg > 0 && g_iso_spell != 0) {
                iso_pick_target();
                iso_trace_line(g_iso_actor, g_iso_cursor_x, g_iso_cursor_y);
            }
            g_iso_key = 0;
            ctl_flush_keys();
            if (g_iso_los_dist > g_iso_spell_arg) {
                iso_redraw();
                g_iso_d582 = 1;
                strcpy(DGSTR(MSG_BUF), DGSTR(0x689B));        /* "desem de duyamaz ki!" */
                iso_show_message(DGSTR(MSG_BUF), g_iso_actor);
                g_iso_voice = 0x21;
                say_voice();
            } else if (g_iso_los_clear > 0 && g_iso_target != -1
                       && !(g_iso_target == 0 && g_iso_spell != 0x1D
                            && iso_bit_test(6, (uint16_t)iso_eval_param(
                                   DGS16((uint16_t)(0x7E + 0x16 * g_iso_spell)))))) {
                face_cursor();
                g_iso_resist = (int16_t)(TW(iso_unit(g_iso_target)->tmpl, 0x14) / 50);
                if (iso_has_status(g_iso_target, 0x0E))
                    g_iso_resist++;
                iso_cast_vecize();
                iso_redraw();
                ui_wait_frames(10);
                if (iso_unit(g_iso_target)->hp < 1)
                    iso_set_cell(iso_unit(g_iso_target)->x, iso_unit(g_iso_target)->y, 0);
                iso_center_view(g_iso_actor);
                g_iso_anim_unit = g_iso_actor;
                g_iso_anim_kind = 1;
                g_iso_ap = 0;
            } else if (g_iso_spell_arg == 0 && g_iso_spell > 0) {     /* C5B0: on itself */
                g_iso_target = g_iso_actor;
                iso_cast_vecize();
                iso_redraw();
                ui_wait_frames(10);
                g_iso_anim_unit = g_iso_actor;
                g_iso_anim_kind = 1;
                g_iso_ap = 0;
            } else if (g_iso_target == 0) {
                iso_redraw();
                g_iso_d582 = 1;
                strcpy(DGSTR(MSG_BUF), DGSTR(0x68B0));
                iso_show_message(DGSTR(MSG_BUF), g_iso_actor);
                g_iso_voice = 0x20;
                say_voice();
            } else if (g_iso_target == -1) {
                g_iso_target = g_iso_actor;
            }
            if (g_iso_ap == 0 && iso_next_turn() == -1)                 /* C645 */
                goto end;
            iso_redraw();
            ui_wait_frames(10);
        }

        /* C664: attack */
        if ((g_iso_key == 0x1C || (buttons == 1 && (zone == 0x1F || zone == 0x22)))
            && TW(iso_unit(g_iso_actor)->tmpl, 0x0E) % 100 <= g_iso_ap) {
            gr_cursor_hide();
            button_lines(zone == 0x1F ? atk_lines1 : atk_lines2, 0);
            gr_cursor_set_shape((uint8_t)g_cursor_shape);
            wait_release();
            ctl_flush_keys();
            iso_pick_target();
            face_cursor();
            iso_cmd_attack();
            if (g_iso_ap == 0 && iso_next_turn() == -1)
                goto end;
            iso_redraw();
            ui_wait_frames(10);
        }

        /* C83A: end the turn */
        if ((g_iso_key == 0x0F || (zone == 0x1D && buttons == 1)) && g_iso_cursor_on == 0) {
            gr_cursor_hide();
            button_lines(end_lines, 0);
            gr_cursor_set_shape((uint8_t)g_cursor_shape);
            wait_release();
            if (iso_next_turn() == -1)
                goto end;
            g_iso_d582 = 1;
            iso_redraw();
            ui_wait_frames(10);
        }

        /* C90F: the party member's panel */
        if (g_iso_actor <= 0x18 || iso_unit(g_iso_actor)->side != 1)
            continue;
        if ((zone > 10 && zone < 0x0E && g_s3d_stat(g_party_cur, 0x26 + 2 * zone) != 0
             && buttons == 1)
            || (g_iso_key > 0x0F && g_iso_key < 0x13
                && PITEM(g_party_cur, g_iso_key + 0x0E) != 0)) {
            /* put down what the hand slot holds (zones / Q W E 11..13) */
            g_iso_panel_used = 1;
            if (g_iso_key != 0)
                zone = (int16_t)(g_iso_key - 5);
            g_bb53 = (int16_t)(g_s3d_stat(g_party_cur, 0x26 + 2 * zone) - 0x22);
            g_s3d_stat(g_party_cur, 0x26 + 2 * zone) = 0;
            if (zone == 0x0B)
                kind1 = 0;
            else if (zone == 0x0C)
                kind2 = 0;
            iso_draw_char_frame(DG8(0x9664), g_bb53);
            gr_cursor_update(0x2D);
            gr_wait_buttons_up();
        }
        if ((buttons != 0 && zone < 0x0F) || ch == 0x69 || ch == (int16_t)0xFF8D) {
            if (g_iso_key != 0) {
                kbd_mouse = (int16_t)(g_mouse_present + 1);
                gr_cursor_hide();
                g_mouse_present = 0;
            }
            if (zone < 0x0B && buttons == 2) {               /* right click: use */
                g_iso_panel_used = 1;
                select_item(zone);
                if (g_cs_item_id > 0x20 && g_cs_item_id < 0x2C) {
                    iso_use_item();
                    g_s3d_item(g_party_cur, zone) = 0;
                    iso_draw_char_info();
                    gr_cursor_update(0x2D);
                }
            }
        }
        if (!((zone < 0x0B && buttons == 1) || ch == 0x69 || ch == (int16_t)0xFF8D))
            goto panel_done;

        /* CB41: the inventory */
        g_iso_panel_used = 1;
        if (ch == 0x69 || ch == (int16_t)0xFF8D) {
            for (g_bb53 = 1; g_bb53 <= 10; g_bb53++)
                if (g_s3d_item(g_party_cur, g_bb53) != 0) {
                    zone = g_bb53;
                    break;
                }
        }
        if (g_s3d_item(g_party_cur, zone) == 0)
            goto panel_done;
        select_item(zone);
        iso_format_stat_line();
        iso_draw_char_frame(DG8(0x9664), zone);
        show_item(1);
        for (;;) {                                               /* CCBB */
            if (g_iso_key == 0)
                g_cs_target = gr_zone_under_cursor();
            gr_cursor_update((uint8_t)g_cursor_shape);
            if (ch == 0x69 || ch == (int16_t)0xFF8D) {
                for (;;) {                       /* CCE9: keyboard: choose the slot */
                    do {
                        key2 = (int16_t)(ui_bioskey(1) >> 8);
                        gr_cursor_update((uint8_t)g_cursor_shape);
                    } while (key2 == 0);
                    iso_dos_break(key2);         /* FFFFh >> 8 (SAR): Ctrl-Break, iso.h */
                    ctl_flush_keys();
                    iso_draw_char_frame(0, zone);
                    if (key2 == 0x4D)
                        iso_clamp_add(&zone, 1, 1, 10);
                    else if (key2 == 0x4B)
                        iso_clamp_add(&zone, -1, 1, 10);
                    else if (key2 == 0x48 || key2 == 0x50)
                        zone = (int16_t)((zone + 4) % 10 + 1);
                    else if (key2 == 1) {
                        zone = 0;
                        iso_draw_char_info();
                        goto inv_done;
                    }
                    if (key2 != 0) {
                        select_item(zone);
                        if (key2 == 0x39 && g_cs_item_id > 0x20 && g_cs_item_id < 0x2C) {
                            iso_use_item();
                            g_s3d_item(g_party_cur, zone) = 0;
                            zone = 0;
                            iso_draw_char_info();
                            goto inv_done;
                        }
                        iso_format_stat_line();
                        iso_draw_char_frame(DG8(0x9664), zone);
                        show_item(0);
                    }
                    if (key2 == 0x1C)
                        break;
                }
                /* CEB6: keyboard: choose where it goes (11..14) */
                g_cs_target = 0x0B;
                for (g_bb53 = 1; g_bb53 <= 10; g_bb53++)
                    iso_draw_item_icon(g_bb53, g_party_cur, 1);
                sys_fill_rect_checked((int16_t)((g_cs_target - 0x0B) * 0x11 + 0x3B), 0xA5, 0x0E,
                                      0x0E, 6, DG8(0x9664));
                iso_show_picture(6);
                for (;;) {                                       /* CF35 */
                    do {
                        key2 = (int16_t)(ui_bioskey(1) >> 8);
                        gr_cursor_update((uint8_t)g_cursor_shape);
                    } while (key2 == 0);
                    iso_dos_break(key2);         /* Ctrl-Break, iso.h */
                    ctl_flush_keys();
                    for (g_bb53 = 1; g_bb53 <= 3; g_bb53++)
                        iso_draw_portrait(g_bb53, g_party_cur, 1);
                    if (key2 == 0x4D)
                        { int16_t tg = g_cs_target; iso_clamp_add(&tg, 1, 0x0B, 0x0E); g_cs_target = tg; }
                    else if (key2 == 0x4B)
                        { int16_t tg = g_cs_target; iso_clamp_add(&tg, -1, 0x0B, 0x0E); g_cs_target = tg; }
                    else if (key2 == 1) {
                        zone = 0;
                        goto inv_done;
                    }
                    if (key2 != 0) {
                        for (g_bb53 = 1; g_bb53 <= 3; g_bb53++)
                            iso_draw_portrait(g_bb53, g_party_cur, 1);
                        iso_show_picture(6);
                        sys_fill_rect_checked((int16_t)((g_cs_target - 0x0B) * 0x11 + 0x3B),
                                              0xA5, 0x0E, 0x0E, 0, DG8(0x9664));
                    }
                    if (key2 == 0x1C)
                        break;
                }
                if (g_cs_target != 0)
                    iso_draw_portrait((int16_t)(g_cs_target - 0x0B), g_party_cur, 0);
                select_item(zone);
            }
            /* D0C9: mouse: another inventory slot clicked */
            if (g_cs_target < 0x0B && g_mouse_buttons == 1 && g_cs_target != zone
                && g_cs_target != 0) {
                iso_draw_char_frame(0, zone);
                zone = g_cs_target;
                iso_draw_char_frame(DG8(0x9664), zone);
                select_item(zone);
                iso_format_stat_line();
                show_item(1);
            }
            /* D1F4: hand 1 */
            if (g_cs_target == 0x0B && (g_cs_item_id < 0x1A || g_cs_item_id == 0x2B)
                && (g_mouse_buttons == 1 || key2 == 0x1C) && g_cs_item_id != 0) {
                t = g_cs_item_id == 0x2B ? 0x1A : g_cs_item_id;
                kind1 = g_s3d_item_hands(t);
                if (g_s3d_stat(g_party_cur, 0x3E) == zone + 0x22
                    || g_s3d_stat(g_party_cur, 0x3C) == zone + 0x22
                    || (kind1 == 2 && g_s3d_stat(g_party_cur, 0x3E) != 0))
                    goto inv_done;
                g_s3d_stat(g_party_cur, 0x3C) = (int16_t)(zone + 0x22);
                goto inv_changed;
            }
            /* D2DB: hand 2 */
            if (g_cs_target == 0x0C && (g_mouse_buttons == 1 || key2 == 0x1C)
                && (g_cs_item_id < 0x1B || g_cs_item_id == 0x22) && g_cs_item_id != 0) {
                int16_t h1;
                if (g_s3d_stat(g_party_cur, 0x3C) == zone + 0x22
                    || g_s3d_stat(g_party_cur, 0x3E) == zone + 0x22)
                    goto inv_done;
                h1 = (int16_t)(PITEM(g_party_cur, g_s3d_stat(g_party_cur, 0x3C)) % 100);
                if (h1 == 0x2B)
                    h1 = 0x1A;
                if (g_s3d_stat(g_party_cur, 0x3C) != 0)
                    kind1 = g_s3d_item_hands(h1);
                if (g_cs_item_id < 0x1A || g_cs_item_id == 0x22) {
                    t = g_cs_item_id == 0x22 ? 0x1A : g_cs_item_id;
                    kind2 = g_s3d_item_hands(t);
                    if (PITEM(g_party_cur, g_s3d_stat(g_party_cur, 0x3C)) % 100 >= 0x1A
                        || kind2 != 1 || kind1 != 1)
                        goto inv_done;
                } else if (kind1 == 2) {
                    goto inv_next;
                }
                g_s3d_stat(g_party_cur, 0x3E) = (int16_t)(zone + 0x22);
                goto inv_changed;
            }
            /* D489: armour */
            if (g_cs_target == 0x0D && (g_mouse_buttons == 1 || key2 == 0x1C)
                && g_cs_item_id > 0x1A && g_cs_item_id < 0x21) {
                g_s3d_stat(g_party_cur, 0x40) = (int16_t)(zone + 0x22);
                goto inv_changed;
            }
            /* D4D1: throw away (zone 14) what is not in a hand */
            if (g_cs_target == 0x0E && (g_mouse_buttons == 1 || key2 == 0x1C)
                && g_s3d_stat(g_party_cur, 0x3C) != zone + 0x22
                && g_s3d_stat(g_party_cur, 0x3E) != zone + 0x22
                && g_s3d_stat(g_party_cur, 0x40) != zone + 0x22) {
                g_s3d_item(g_party_cur, zone) = 0;
                g_zone_y2(zone) = 0;
                g_zone_x2(zone) = 0;
                g_zone_y1(zone) = 0;
                g_zone_x1(zone) = 0;
                goto inv_changed;
            }
inv_next:                                                        /* D5C6 */
            key2 = 0;
            if (g_mouse_buttons == 2)
                goto inv_done;
            continue;
inv_changed:                                                     /* D5B8 */
            iso_draw_char_info();
            gr_cursor_update(0x2D);
            goto inv_done;
        }
inv_done:                                                        /* D5DB */
        iso_draw_char_info();
        if (kbd_mouse != 0) {
            g_mouse_present = (uint8_t)(kbd_mouse - 1);
            kbd_mouse = 0;
        }
        gr_cursor_update(0x2D);
        gr_wait_buttons_up();
panel_done:                                                      /* D605 */
        if (g_iso_panel_used == 0) {
            iso_redraw();
            ui_wait_frames(10);
        }
        g_iso_panel_used = 0;
        iso_redraw();
    }

end:                                                             /* D62A */
    if (allied != 0 && g_iso_encounter(allied, 0) == 4 && fr_flag_test(0x14) != 0
        && iso_unit(allied)->hp < 0)
        g_fr_acc = 1;
    iso_free_battle_gfx();
    g_mouse_present = (uint8_t)mouse;
    for (i = 1; i <= 0x1C; i++) {
        UW(i, 0x08) = 0;
        UW(i, 0x0A) = 0;
        UW(i, 0x0C) = 0;
        UW(i, 0x0E) = 0;
        UW(i, 0x10) = 0;
        UW(i, 0x22) = 0;
    }
    iso_battle_to_party();
    gr_fade(1000, (int16_t)-g_pal_level, (int16_t)-g_music_level);
    if (g_sound_on != 0 && g_s3d_opt(3) != 0) {
        if (g_sound_on != 0) {
            ampStopModule();
            if (g_music_module != NULL) {
                ampFreeModule(g_music_module);
                MemFree(g_music_module);
                g_music_module = NULL;
            }
        }
        g_music_track = music;
    }
    memcpy(DGP(UI_WIN_CUR), saved_win, UI_WIN_SIZE);
}
