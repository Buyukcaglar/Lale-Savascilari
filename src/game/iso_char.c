/*
 * iso_char.c - ISOSYS.L 4595:6345-8268: party records <-> battle units,
 * the derived combat values of a party member (weapon, armour, load,
 * action points), the character panel at the bottom of the battle screen
 * (name, info, item icons), using an item in battle and the item
 * description line.  See iso.h.
 *
 * Party record p (1..4): 99A6 + 5Ch*p (chargen.h PartyStats, s3d.h
 * g_s3d_stat).  Its hand words +3C (hand 1), +3E (hand 2), +40 (armour)
 * hold 0 or a WORD INDEX into the record (inventory slot + 22h); the item
 * there is 100*count + id.  Item ids < 1Ah are weapons (2Bh is mapped to
 * 1Ah), 1Ah..20h armour (table 10AA), 20h.. consumables.
 *
 * Item table: 14-byte records at 0E7F (s3d.h); the listing reaches its
 * words as base + 0Eh*id with bases 0E79 (damage x100 + to-hit), 0E7D
 * (weight), 0E81 (hands), 0E83 (AP cost of an attack).
 */
#include "game/iso.h"

#include <stdio.h>
#include <string.h>

#include "engine/fatal.h"
#include "engine/grtext.h"
#include "engine/intrface.h"
#include "engine/memman.h"
#include "engine/ngraphs.h"
#include "engine/nkontrol.h"
#include "engine/systems.h"
#include "game/chargen.h"
#include "game/s3d.h"
#include "rtl/bc_rand.h"

#define PW(p, off)      g_s3d_stat(p, off)                      /* party record word */
#define PITEM(p, idx)   DGS16((uint16_t)(0x99A6 + 0x5C * (p) + 2 * (idx)))  /* word idx */
#define TW(g, off)      DGS16((uint16_t)(0x9F18 + 0x2E * (g) + (off)))      /* template word */
#define UW(c, off)      DGS16((uint16_t)(0xA08A + 0x28 * (c) + (off)))      /* unit word */
#define ITEM_W(base, m) DGS16((uint16_t)((base) + 0x0E * (m)))   /* item table, see above */
#define ARMOUR_W(m)     DGS16((uint16_t)(0x10AA + 2 * (m)))      /* m = 1Ah..20h */

#define g_iso_party_i   DGS16(0xD5AC)   /* i16  party loop index (a global in the original) */
#define g_bb53          DGS16(0xBB53)   /* i16  shared loop index (3DSYS's too) */

/* hand slot word +3C/+3E/+40: the item it points to */
static int16_t hand_item(int16_t p, int16_t off)
{
    return PITEM(p, PW(p, off));
}

/* id of an item value for the weapon tables: 2Bh -> 1Ah */
static int16_t weapon_id(int16_t v)
{
    int16_t m = (int16_t)(v % 100);
    return m == 0x2B ? 0x1A : m;
}

/* the hit-points/AP style max(0, a - b), the original's "SUB; JGE" */
static int16_t sub0(int16_t a, int16_t b)
{
    return a < b ? 0 : (int16_t)(a - b);
}

/*
 * iso_party_to_battle - for members p = 1..4 (index in the global D5AC):
 * compute the template (iso_calc_char_stats) and set up unit 24+p: hp,
 * vecize points, facing 1, template p, side 1, to-hit (template +06),
 * dodge (+08), status slots and on-hit vecize from the template.
 *
 * Original: IST.EXE ovl4595:6345 (ISOSYS.L), far cdecl.
 * Verified: tests/py/test_iso_c.py (test_char_stats), 2026-11-21.
 */
void iso_party_to_battle(void)
{
    for (g_iso_party_i = 1; g_iso_party_i <= 4; g_iso_party_i++) {
        int16_t p = g_iso_party_i;
        int16_t c = (int16_t)(p + 24);
        int16_t k;

        iso_calc_char_stats();
        UW(c, 0x00) = PW(p, 0x3A);
        UW(c, 0x02) = PW(p, 0x38);
        UW(c, 0x04) = 1;
        UW(c, 0x06) = p;
        UW(c, 0x0E) = 1;
        UW(c, 0x12) = TW(p, 0x06);
        UW(c, 0x14) = TW(p, 0x08);
        for (k = 0x16; k <= 0x20; k += 2)
            UW(c, k) = TW(p, k);
    }
}

/*
 * iso_calc_char_stats - fill template p = D5AC (also made the current
 * member 2898) from its party record: +02 strength, +04 damage
 * (hundreds of D5AA + D5A8), +06 to-hit, +08 dodge, +0A armour, +0C AP,
 * +0E/+10 AP cost of attack 1 / 2 (item table 0E83; hand 2 only for
 * a weapon there), +12 vocabulary, +14 cheek,
 * +16..+1E the record's status words +2E..+36, +20 on-hit vecize (+44),
 * +22 for a hand-1 item id > 20 the stun table below.
 *
 * Original: IST.EXE ovl4595:64F5 (ISOSYS.L), far cdecl.  The 5-word
 * table (10,10,15,5,5 at DS:6417) is F_SCOPY'd to bp-0Ch and indexed as
 * bp-36h + 2*id: ids 21..25 hit it, id 26 reads the local at bp-2 (its
 * value is then 6, the end of the status loop), 27.. read the saved BP /
 * return address - the port gives 0 for those (no such items exist).
 * The return value (AX) is not used by the callers.
 * Verified: tests/py/test_iso_c.py (test_char_stats), 2026-11-21.
 */
int16_t iso_calc_char_stats(void)
{
    int16_t p = g_iso_party_i;
    int16_t k, m, v;

    g_party_cur = p;
    TW(p, 0x02) = PW(p, 0x0C);
    iso_calc_weapon();
    TW(p, 0x04) = (int16_t)(g_iso_wpn_dmg / 100 * 100 + g_iso_wpn_base);
    TW(p, 0x06) = g_cs_weapon;
    iso_calc_stat_a();
    TW(p, 0x08) = iso_calc_stat_b();
    TW(p, 0x0A) = iso_calc_stat_c();
    TW(p, 0x0C) = iso_calc_ap_by_load();

    m = weapon_id(hand_item(p, 0x3C));
    TW(p, 0x0E) = PW(p, 0x3C) != 0 ? ITEM_W(0x0E83, m) : 0;

    m = weapon_id(hand_item(p, 0x3E));
    v = hand_item(p, 0x3E);
    TW(p, 0x10) = (v < 0x1A && v != 0) ? ITEM_W(0x0E83, m) : 0;   /* raw value, not the id */

    TW(p, 0x12) = PW(p, 0x0A);
    TW(p, 0x14) = PW(p, 0x16);
    for (k = 1; k <= 5; k++)
        TW(p, 0x14 + 2 * k) = PW(p, 0x2C + 2 * k);
    TW(p, 0x20) = PW(p, 0x44);

    v = hand_item(p, 0x3C);
    if (v % 100 > 0x14) {
        m = (int16_t)(v % 100);
        if (m >= 21 && m <= 25)
            v = DGS16((uint16_t)(0x6417 + 2 * (m - 21)));
        else if (m == 26)
            v = 6;                      /* bp-2: the loop counter after the loop */
        else
            v = 0;                      /* stack garbage in the original */
        TW(p, 0x22) = v;
        return v;
    }
    return v;
}

/*
 * iso_calc_weapon - weapon values of member 2898 for attack D5B6 (2: hand
 * 2, else hand 1): D5AA damage (table 0E79; 200 bare-handed), D5A8 its
 * base (D5AA % 100 + max(0, hitting power / 100) + count), +1 for the
 * race's favourite weapon; D5DA to-hit = agility / 300 + D5A8, minus 2
 * (not below 0) over 500 weight +2A, +1 with status 10, -2 with status
 * 20, minus +2A / 300 (not below 0), 0 with status 21.
 *
 * Original: IST.EXE ovl4595:6828 (ISOSYS.L), far cdecl.
 * Verified: tests/py/test_iso_c.py (test_char_stats), 2026-11-21.
 */
void iso_calc_weapon(void)
{
    int16_t p = g_party_cur;
    int16_t hand = g_iso_strike == 2 ? PW(p, 0x3E) : PW(p, 0x3C);
    int16_t v = PITEM(p, hand);
    int16_t m = weapon_id(v);
    int16_t hp;

    g_iso_wpn_dmg = v != 0 ? ITEM_W(0x0E79, m) : 200;
    hp = (int16_t)(PW(p, 0x10) / 100);
    if (hp < 0)
        hp = 0;
    g_iso_wpn_base = (int16_t)(g_iso_wpn_dmg - g_iso_wpn_dmg / 100 * 100 + hp + v / 100);
    if (hand_item(p, 0x3C) % 100 == PW(p, 0x2C))
        g_iso_wpn_base++;
    g_cs_weapon = (int16_t)(PW(p, 0x14) / 300 + g_iso_wpn_base);
    if (PW(p, 0x2A) > 500)
        g_cs_weapon = sub0(g_cs_weapon, 2);
    if (iso_has_status((int16_t)(p + 0x18), 10))
        g_cs_weapon++;
    if (iso_has_status((int16_t)(p + 0x18), 0x14))
        g_cs_weapon = sub0(g_cs_weapon, 2);
    g_cs_weapon = sub0(g_cs_weapon, (int16_t)(PW(p, 0x2A) / 300));
    if (iso_has_status((int16_t)(p + 0x18), 0x15))
        g_cs_weapon = 0;
}

/*
 * iso_calc_stat_a - load D5EE of member 2898: the weight of the ten
 * inventory items (weapons: table 0E7D; armour 1Ah..20h: table 10AA;
 * anything else 1), stored in record +5A.  Returns nothing useful.
 *
 * Original: IST.EXE ovl4595:6AC2 (ISOSYS.L), far cdecl.
 * Verified: tests/py/test_iso_c.py (test_char_stats), 2026-11-21.
 */
int16_t iso_calc_stat_a(void)
{
    int16_t p = g_party_cur;
    int16_t k, m;

    g_cs_load = 0;
    for (k = 1; k <= 10; k++) {
        m = (int16_t)(g_s3d_item(p, k) % 100);
        if (m < 0x1A || m == 0x2B) {
            if (m == 0x2B)
                m = 0x1A;
            g_cs_load += ITEM_W(0x0E7D, m);
        } else if (m > 0x19 && m < 0x21) {
            g_cs_load += ARMOUR_W(m);
        } else if (m != 0) {
            g_cs_load++;
        }
    }
    PW(p, 0x5A) = g_cs_load;
    return g_cs_load;
}

/*
 * iso_calc_stat_b - dodge of member 2898: max(0, agility / 50 + 7 -
 * load), +1 status 10, +2 status 11, +3 for race 8 with armour id 1Bh,
 * +1 status 28, -2 (not below 0) status 20, 0 with status 21.
 *
 * Original: IST.EXE ovl4595:6B79 (ISOSYS.L), far cdecl.
 * Verified: tests/py/test_iso_c.py (test_char_stats), 2026-11-21.
 */
int16_t iso_calc_stat_b(void)
{
    int16_t p = g_party_cur;
    int16_t c = (int16_t)(p + 0x18);
    int16_t r = sub0((int16_t)(PW(p, 0x14) / 50 + 7), g_cs_load);

    if (iso_has_status(c, 10))
        r++;
    if (iso_has_status(c, 11))
        r += 2;
    if (hand_item(p, 0x40) % 100 == 0x1B && PW(p, 0x02) == 8)
        r += 3;
    if (iso_has_status(c, 0x1C))
        r++;
    if (iso_has_status(c, 0x14))
        r = sub0(r, 2);
    if (iso_has_status(c, 0x15))
        r = 0;
    return r;
}

/*
 * iso_calc_stat_c - armour of member 2898: armour item (+40) id D5EA and
 * count D5E6: table 10AA[id] + count; a hand-2 shield (id 1Ah) adds its
 * count + 1; plus endurance / 300.
 *
 * Original: IST.EXE ovl4595:6CD6 (ISOSYS.L), far cdecl.
 * Verified: tests/py/test_iso_c.py (test_char_stats), 2026-11-21.
 */
int16_t iso_calc_stat_c(void)
{
    int16_t p = g_party_cur;
    int16_t r = 0, v;

    g_cs_item_id = (int16_t)(hand_item(p, 0x40) % 100);
    g_cs_item_count = (int16_t)(hand_item(p, 0x40) / 100);
    if (g_cs_item_id != 0)
        r = (int16_t)(ARMOUR_W(g_cs_item_id) + g_cs_item_count);
    v = hand_item(p, 0x3E);
    if ((int16_t)(v - v / 100 * 100) == 0x1A)
        r = (int16_t)(r + v / 100 + 1);
    return (int16_t)(r + PW(p, 0x12) / 300);
}

/*
 * iso_calc_ap_by_load - action points: max(3, 10 - max(0, load - 5)).
 *
 * Original: IST.EXE ovl4595:6E4D (ISOSYS.L), far cdecl.
 * Verified: tests/py/test_iso_c.py (test_char_stats), 2026-11-21.
 */
int16_t iso_calc_ap_by_load(void)
{
    int16_t over = sub0(g_cs_load, 5);
    return (int16_t)(10 - over) < 3 ? 3 : (int16_t)(10 - over);
}

/*
 * iso_draw_char_frame - mark inventory slot k (1..10, 5 per row) of the
 * panel: colour != 0 draws a 15x15 frame of that colour behind the icon
 * (the icon's 13x13 inside is restored from the screen, slot 0); colour 0
 * redraws the icon (iso_draw_item_icon).
 *
 * Original: IST.EXE ovl4595:6E95 (ISOSYS.L), far cdecl (color, k).
 * Verified: tests/py/test_iso_c.py (test_panel), 2026-11-21.
 */
void iso_draw_char_frame(int16_t color, int16_t k)
{
    int16_t col = (int16_t)((k - 1) % 5), row = (int16_t)((k - 1) / 5);

    gr_cursor_hide();
    if (color == 0)
        iso_draw_item_icon(k, g_party_cur, 0);
    else
        sys_fill_rect((uint16_t)(col * 0x11 + 0xEC), (uint16_t)(row * 0x11 + 0xA5), 0x0F, 0x0F,
                      6, (uint8_t)color);
    sys_blit((uint16_t)(col * 0x11 + 0xED), (uint16_t)(row * 0x11 + 0xA6), 0x0D, 0x0D,
             (uint16_t)(col * 0x11 + 0xED), (uint16_t)(row * 0x11 + 0xA6), 0, 6, sys_rect_copy);
    gr_cursor_set_shape((uint8_t)g_cursor_shape);
}

/*
 * iso_item_effect_a..h - small item effects on member 2898 / the actor
 * D5DE (unreferenced in the listing: they were reached only through a
 * table that no longer exists).
 *   a: health +3 (not above strength), also the actor's hp
 *   b: vecize points +50 (not above vocabulary), also the actor's
 *   maxhp3: strength +3 (record and template)
 *   c: vocabulary +50 (record and template +12)
 *   d: +1C courage +50     e: cheek +50 (record, template +14)
 *   f: +0E intelligence +50   g: +12 endurance +50   h: +14 agility +50
 *
 * Original: IST.EXE ovl4595:6F5F..7233 (ISOSYS.L), far cdecl.
 * Verified: tests/py/test_iso_c.py (test_item_effects), 2026-11-21.
 */
void iso_item_effect_a(void)    /* unreferenced */
{
    int16_t p = g_party_cur;
    PW(p, 0x3A) = PW(p, 0x0C) < (int16_t)(PW(p, 0x3A) + 3) ? PW(p, 0x0C) : (int16_t)(PW(p, 0x3A) + 3);
    iso_unit(g_iso_actor)->hp = PW(p, 0x0C) < (int16_t)(iso_unit(g_iso_actor)->hp + 3)
                              ? PW(p, 0x0C) : (int16_t)(iso_unit(g_iso_actor)->hp + 3);
}

void iso_item_effect_b(void)    /* unreferenced */
{
    int16_t p = g_party_cur;
    PW(p, 0x38) = PW(p, 0x0A) < (int16_t)(PW(p, 0x38) + 50) ? PW(p, 0x0A) : (int16_t)(PW(p, 0x38) + 50);
    iso_unit(g_iso_actor)->spell = PW(p, 0x0A) < (int16_t)(iso_unit(g_iso_actor)->spell + 50)
                                 ? PW(p, 0x0A) : (int16_t)(iso_unit(g_iso_actor)->spell + 50);
}

void iso_item_effect_maxhp3(void)  /* unreferenced */
{
    PW(g_party_cur, 0x0C) += 3;
    TW(g_party_cur, 0x02) += 3;
}

void iso_item_effect_c(void)    /* unreferenced */
{
    PW(g_party_cur, 0x0A) += 50;
    TW(g_party_cur, 0x12) += 50;
}

void iso_item_effect_d(void)    /* unreferenced */
{
    PW(g_party_cur, 0x1C) += 50;
}

void iso_item_effect_e(void)    /* unreferenced */
{
    PW(g_party_cur, 0x16) += 50;
    TW(g_party_cur, 0x14) += 50;
}

void iso_item_effect_f(void)    /* unreferenced */
{
    PW(g_party_cur, 0x0E) += 50;
}

void iso_item_effect_g(void)    /* unreferenced */
{
    PW(g_party_cur, 0x12) += 50;
}

void iso_item_effect_h(void)    /* unreferenced */
{
    PW(g_party_cur, 0x14) += 50;
}

/*
 * iso_init_panel_layout - the hot zones of the battle screen's panel:
 * 21..28 the eight direction arrows, 29..33 the five command buttons
 * (39 px apart from x 90h), 34 the vecize button.
 *
 * Original: IST.EXE ovl4595:7250 (ISOSYS.L), far cdecl; thunk 1694:0106.
 * Verified: tests/py/test_iso_c.py (test_panel), 2026-11-21.
 */
void iso_init_panel_layout(void)
{
    static const int16_t arrows[8][4] = {
        { 0x02, 0x4F, 0x0C, 0x56 }, { 0x11, 0x4F, 0x1B, 0x56 }, { 0x1F, 0x4E, 0x28, 0x55 },
        { 0x0E, 0x5B, 0x19, 0x63 }, { 0x2B, 0x5A, 0x34, 0x63 }, { 0x1A, 0x67, 0x25, 0x6E },
        { 0x29, 0x67, 0x34, 0x6E }, { 0x38, 0x67, 0x41, 0x6E },
    };
    int16_t i;

    for (i = 0; i < 8; i++) {
        g_zone_x1(21 + i) = arrows[i][0];
        g_zone_y1(21 + i) = arrows[i][1];
        g_zone_x2(21 + i) = arrows[i][2];
        g_zone_y2(21 + i) = arrows[i][3];
    }
    g_zone_x1(34) = 0x1D;
    g_zone_y1(34) = 0x5B;
    g_zone_x2(34) = 0x28;
    g_zone_y2(34) = 0x63;
    for (i = 0; i < 5; i++) {
        g_zone_x1(29 + i) = (int16_t)(i * 0x27 + 0x90);
        g_zone_y1(29 + i) = 0x93;
        g_zone_x2(29 + i) = (int16_t)(i * 0x27 + 0xA8);
        g_zone_y2(29 + i) = 0x9F;
    }
}

/*
 * iso_draw_name - the actor's name (monsters only, D5DE < 24): the name
 * of its group type (1EB3 + 13h*type), cut to 65 px with "." appended,
 * drawn at (3Ch, C0h) in colour 0 and at (3Ch, BFh) in colour 9664.
 *
 * Original: IST.EXE ovl4595:7470 (ISOSYS.L), far cdecl.  The cut loop
 * first writes the NUL already at strlen (no change), then removes one
 * character per pass; fatal 65h if the name reaches 40 bytes.
 * Verified: tests/py/test_iso_c.py (test_panel), 2026-11-21.
 */
void iso_draw_name(void)
{
    char buf[40];
    int16_t len, w;

    if (g_iso_actor >= 0x18)
        return;
    strcpy(buf, DGSTR((uint16_t)(0x1EB3 + 0x13 * DGS16((uint16_t)(0x5BC2 + 6 * iso_unit(g_iso_actor)->tmpl)))));
    len = (int16_t)strlen(buf);
    w = grt_text_width(buf, 2);
    if (w > 0x41) {
        while (w > 0x41) {
            buf[len] = 0;
            len--;
            w = grt_text_width(buf, 2);
        }
        strcat(buf, DGSTR(0x6746));                    /* "." */
    }
    if (strlen(buf) >= 0x28)
        o559A_fatal(DGSTR(0x6748), 0x8D0, NULL, 0x65);
    iso_draw_text(2, 0x3C, 0xC0, buf, 0);
    iso_draw_text(2, 0x3C, 0xBF, buf, DG8(0x9664));
}

/*
 * iso_draw_char_info - the panel of the actor (party units only,
 * D5DE > 24; D5DE - 24 becomes the current member 2898 and D5AC, its
 * template is recomputed first): portrait (picture 46h + p), the ten
 * inventory icons with their zones 1..10, the three hand slots with
 * zones 11..13, zone 14 the portrait, then "hp/strength", "vecize/
 * vocabulary", dodge, armour, the to-hit of hand 1 ("%d" at C1h), the
 * AP left, the AP cost of hand 1 % 100, "gender race" and the name.
 *
 * Original: IST.EXE ovl4595:7574 (ISOSYS.L), far cdecl.  The original
 * computes a colour (1Fh party / 1Dh else) for the last line and never
 * uses it (the name is drawn in 9665).
 * Verified: tests/py/test_iso_c.py (test_panel), 2026-11-21.
 */
void iso_draw_char_info(void)
{
    /* the original's buffer is bp-1Ah, 20 bytes: a 36-character name or
       race overruns it into the locals; the port's is large enough */
    char buf[96];
    int16_t p, k, apcost;

    iso_init_panel_layout();
    g_party_cur = (int16_t)(g_iso_actor - 0x18);
    g_iso_party_i = g_party_cur;
    iso_calc_char_stats();
    if (g_iso_actor <= 0x18)
        return;
    p = g_party_cur;
    sys_blit(0, 0, 0x20, 0x20, 0x19, 0xA5, (uint8_t)(p + 0x46), 6, sys_rect_copy);
    for (k = 1; k <= 10; k++)
        iso_draw_item_icon(k, p, 1);
    for (k = 1; k <= 3; k++)
        iso_draw_portrait(k, p, 1);
    g_zone_x1(14) = 0x6E;
    g_zone_y1(14) = 0xA5;
    g_zone_x2(14) = 0x7D;
    g_zone_y2(14) = 0xB4;
    sys_fill_rect(0xAE, 0xA4, 0x2B, 0x22, 6, 0);
    sprintf(buf, DGSTR(0x6758), UW(p + 24, 0x00), TW(UW(p + 24, 0x06), 0x02));
    iso_draw_text(2, 0xAE, 0xA5, buf, DG8(0x9664));
    sprintf(buf, DGSTR(0x675E), UW(p + 24, 0x02), TW(UW(p + 24, 0x06), 0x12));
    iso_draw_text(2, 0xAE, 0xAC, buf, DG8(0x9664));
    sprintf(buf, DGSTR(0x6764), TW(UW(p + 24, 0x06), 0x08));
    iso_draw_text(2, 0xAE, 0xB3, buf, DG8(0x9664));
    sprintf(buf, DGSTR(0x6767), TW(UW(p + 24, 0x06), 0x0A));
    iso_draw_text(2, 0xAE, 0xBA, buf, DG8(0x9664));
    sprintf(buf, DGSTR(0x676A), UW(p + 24, 0x12));
    iso_draw_text(2, 0xAE, 0xC1, buf, DG8(0x9664));
    sys_fill_rect(0xDE, 0xAB, 9, 6, 6, 0);
    sprintf(buf, DGSTR(0x676D), g_iso_ap);
    iso_draw_text(2, 0xDE, 0xAB, buf, DG8(0x9664));
    if (hand_item(p, 0x3C) != 0)
        apcost = ITEM_W(0x0E83, weapon_id(hand_item(p, 0x3C)));
    else
        apcost = 0;
    sys_fill_rect(0xDE, 0xBD, 9, 6, 6, 0);
    sprintf(buf, DGSTR(0x6770), apcost % 100);
    iso_draw_text(2, 0xDE, 0xBD, buf, DG8(0x9664));
    strcpy(g_cg_gender_text, PW(p, 0x04) == 1 ? DGSTR(0x6773) : DGSTR(0x6779));  /* erkek / dişi */
    sprintf(buf, DGSTR(0x677E), g_cg_gender_text, g_party_names[p].race_name);
    iso_draw_text(2, 0x3D, 0xB8, buf, 0);
    iso_draw_text(2, 0x3C, 0xB7, buf, DG8(0x9665));
    sprintf(buf, DGSTR(0x6784), g_party_names[p].name);
    iso_draw_text(2, 0x3D, 0xC0, buf, 0);
    iso_draw_text(2, 0x3C, 0xBF, buf, DG8(0x9665));
}

/*
 * iso_use_item - use consumable D5EA (count D5E6) as member 2898 /
 * actor D5DE.  Ids below 2Ah (the test is signed: D5EA - 20h < 10) use
 * the 10-byte record 1047 + 10*id: {cap, rec word, template word, unit
 * word (index from A088), amount}: with a cap word the three values grow
 * by amount but not above record word cap; without one each grows by
 * max(0, amount - value / 20) (the template's divisor is the RECORD word
 * of the template index - original quirk).  Ids from 2Ah are chewing
 * gum: random(7) picks a flavour D5B0 and its vecize (table 6421) which
 * is cast on the actor itself (outside the battle window as unit 24+p,
 * the party copied to units and back), with the "bu ciklet ..." line.
 *
 * Original: IST.EXE ovl4595:7A03 (ISOSYS.L), far cdecl.
 * Verified: tests/py/test_iso_c.py (test_use_item, table branch);
 *   the gum branch only via the battle tests (iso_cast_vecize stubbed).
 */
void iso_use_item(void)
{
    int16_t id = g_cs_item_id;
    int16_t p = g_party_cur;

    if ((int16_t)(id - 0x20) < 10) {
        int16_t cap = DGS16((uint16_t)(0x1047 + 10 * id));
        int16_t rw = DGS16((uint16_t)(0x1049 + 10 * id));
        int16_t tw = DGS16((uint16_t)(0x104B + 10 * id));
        int16_t uw = DGS16((uint16_t)(0x104D + 10 * id));
        int16_t amt = DGS16((uint16_t)(0x104F + 10 * id));
#define TWI(i)  DGS16((uint16_t)(0x9F18 + 0x2E * p + 2 * (i)))
#define UWI(i)  DGS16((uint16_t)(0xA088 + 0x28 * g_iso_actor + 2 * (i)))
        if (cap != 0) {
            int16_t lim = PITEM(p, cap);
            PITEM(p, rw) = lim < (int16_t)(PITEM(p, rw) + amt) ? PITEM(p, cap)
                                                               : (int16_t)(PITEM(p, rw) + amt);
            lim = PITEM(p, cap);
            TWI(tw) = lim < (int16_t)(TWI(tw) + amt) ? PITEM(p, cap) : (int16_t)(TWI(tw) + amt);
            lim = PITEM(p, cap);
            UWI(uw) = lim < (int16_t)(UWI(uw) + amt) ? PITEM(p, cap) : (int16_t)(UWI(uw) + amt);
        } else {
            PITEM(p, rw) += sub0(amt, (int16_t)(PITEM(p, rw) / 20));
            TWI(tw) += sub0(amt, (int16_t)(PITEM(p, tw) / 20));
            UWI(uw) += sub0(amt, (int16_t)(UWI(uw) / 20));
        }
#undef TWI
#undef UWI
    } else {
        int16_t saved = 0;          /* bp-0Ch: only set (and used) outside the window */

        g_iso_gum_flavour = bc_random(7);
        g_cs_vecize = DGS16((uint16_t)(0x6421 + 2 * g_iso_gum_flavour));  /* F_SCOPY'd table */
        g_iso_target = g_iso_actor;
        DG8(0xA574) = 0;
        strcpy(DGSTR(0xA510), DGSTR(0x6787));
        g_iso_gum = 1;
        if (!iso_in_battle_window()) {
            saved = g_party_cur;
            g_iso_actor = (int16_t)(g_party_cur + 0x18);
            g_iso_target = g_iso_actor;
            iso_party_to_battle();
        }
        iso_unit(g_iso_actor)->spell += g_iso_d5bc;
        iso_cast_vecize();
        if (!iso_in_battle_window()) {
            iso_battle_to_party();
            g_party_cur = saved;
        }
        strcpy(DGSTR(0xA510), DGSTR(0x6789));
        g_iso_anim_kind = 5;
        iso_redraw();
        if (!iso_in_battle_window())
            iso_show_message(DGSTR(0xA510), g_iso_actor);
        g_iso_gum = 0;
        g_iso_anim_unit = g_iso_actor;
        g_iso_anim_kind = 0;
        g_iso_sfx = 0;
        ui_wait_voice();
        if (DG32(0xE12B) != 0)
            ctl_free_checked(0xE12B, 0x93A, DGSTR(0x678B));
        iso_redraw();
    }
}

/*
 * iso_draw_textbox - the one-line text box at the top of the screen
 * (A1h, 2, 157x13, colour 9667 on the screen slot 0): s in colour
 * `shadow` one pixel lower, then in `color`, font 5.
 *
 * Original: IST.EXE ovl4595:7F29 (ISOSYS.L), far cdecl (char far *s,
 * shadow, color).
 * Verified: tests/py/test_iso_c.py (test_panel), 2026-11-21.
 */
void iso_draw_textbox(const char *s, int16_t shadow, int16_t color)
{
    sys_fill_rect(0xA1, 2, 0x9D, 0x0D, 0, DG8(0x9667));
    grt_draw_text(0, 0xA1, 4, s, (uint8_t)shadow, 5);
    grt_draw_text(0, 0xA1, 3, s, (uint8_t)color, 5);
}

/*
 * iso_format_stat_line - description of item D5EA (count D5E6) into
 * A510: its name (weapons 0CE2 + 15*id, armour 0ECE + 16*id, else
 * 0F0C + 15*id), "+count " if any and " (weight)" (D5EE = weight).
 *
 * Original: IST.EXE ovl4595:7F80 (ISOSYS.L), far cdecl.  itoa(.., 10)
 * = "%d".
 * Verified: tests/py/test_iso_c.py (test_panel), 2026-11-21.
 */
void iso_format_stat_line(void)
{
    char num[8];
    int16_t id = g_cs_item_id;

    if ((id < 0x1A || id == 0x2B) && id != 0) {
        int16_t m = id == 0x2B ? 0x1A : id;
        strcpy(DGSTR(0xA510), DGSTR((uint16_t)(0x0CE2 + 0x0F * m)));
        g_cs_load = ITEM_W(0x0E7D, m);
    } else if (id > 0x19 && id < 0x21) {
        strcpy(DGSTR(0xA510), DGSTR((uint16_t)(0x0ECE + 0x10 * id)));
        g_cs_load = ARMOUR_W(id);
    } else {
        strcpy(DGSTR(0xA510), DGSTR((uint16_t)(0x0F0C + 0x0F * id)));
        g_cs_load = 1;
    }
    if (g_cs_item_count != 0) {
        strcat(DGSTR(0xA510), DGSTR(0x679B));            /* "+" */
        sprintf(num, "%d", g_cs_item_count);
        strcat(DGSTR(0xA510), num);
        strcat(DGSTR(0xA510), DGSTR(0x679D));            /* " " */
    }
    if (g_cs_item_id != 0) {
        strcat(DGSTR(0xA510), DGSTR(0x679F));            /* " (" */
        sprintf(num, "%d", g_cs_load);
        strcat(DGSTR(0xA510), num);
        strcat(DGSTR(0xA510), DGSTR(0x67A2));            /* ")" */
    }
}

/*
 * iso_refresh_desc - when an item is selected (D5EA) and the zone D5E8
 * differs from the one described (D5E4): describe it in the text box.
 *
 * Original: IST.EXE ovl4595:80D6 (ISOSYS.L), far cdecl (unreferenced in
 * the listing).
 * Verified: tests/py/test_iso_c.py (test_panel), 2026-11-21.
 */
void iso_refresh_desc(void)
{
    if (g_cs_item_id != 0 && g_cs_desc_prev != g_cs_zone) {
        g_cs_desc_prev = g_cs_zone;
        iso_format_stat_line();
        iso_draw_textbox(DGSTR(0xA510), 0xD9, DG8(0x9664));
    }
}

/*
 * iso_battle_to_party - copy back to the records p = 1..4 (D5AC; also
 * 2898): strength, vocabulary, cheek (template +02, +12, +14), the
 * status words (unit 24+p +16..+1E, loop index the global BB53),
 * vecize points, hp, on-hit vecize (+20 -> record +44).
 *
 * Original: IST.EXE ovl4595:8111 (ISOSYS.L), far cdecl.
 * Verified: tests/py/test_iso_c.py (test_char_stats), 2026-11-21.
 */
void iso_battle_to_party(void)
{
    for (g_iso_party_i = 1; g_iso_party_i <= 4; g_iso_party_i++) {
        int16_t p = g_iso_party_i;
        int16_t c = (int16_t)(p + 24);

        g_party_cur = p;
        PW(p, 0x0C) = TW(p, 0x02);
        PW(p, 0x0A) = TW(p, 0x12);
        PW(p, 0x16) = TW(p, 0x14);
        for (g_bb53 = 1; g_bb53 <= 5; g_bb53++)
            PW(p, 0x2C + 2 * g_bb53) = UW(c, 0x14 + 2 * g_bb53);
        PW(p, 0x38) = UW(c, 0x02);
        PW(p, 0x3A) = UW(c, 0x00);
        PW(p, 0x44) = UW(c, 0x20);
    }
}

/* port: the far code pointer the original stores for this file's code */
void iso_char_register(void)
{
    ui_register_far_fn(0x1694, 0x0106, (UiFarFn)iso_init_panel_layout);
}
