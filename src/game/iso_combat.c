/*
 * iso_combat.c - ISOSYS.L 4595:3F5B-568B: melee attacks, picking a target
 * with the cursor, the attack / vecize commands and the reaction to a
 * vecize.  See iso.h.
 */
#include "game/iso.h"

#include <string.h>

#include "engine/fatal.h"
#include "engine/grtext.h"
#include "engine/intrface.h"
#include "engine/ngraphs.h"
#include "engine/nkontrol.h"
#include "engine/systems.h"
#include "platform/pc.h"
#include "rtl/bc_rand.h"

/* party record words (99A6 + 5Ch*g, chargen.h) and race words (9B7C + 24h*g) */
#define PARTY_W(g, off)  DGS16((uint16_t)(0x99A6 + 0x5C * (g) + (off)))
#define RACE_W(g, off)   DGS16((uint16_t)(0x9B7C + 0x24 * (g) + (off)))

/* recompute the "side alive" flags; 632E = 1 when a side is gone */
static void count_sides(void)
{
    int16_t c;

    g_iso_alive(2) = 0;
    g_iso_alive(1) = 0;
    for (c = 1; c <= ISO_UNITS; c++)
        if (iso_unit(c)->hp >= 1)
            g_iso_alive(iso_unit(c)->side) = 1;
    if (g_iso_alive(1) == 0 || g_iso_alive(2) == 0)
        g_iso_result = 1;
}

/*
 * iso_attack - attacker D56E strikes defender D56C, with each of its two
 * attacks (D5B6 = 1, 2).  Attack k costs template word 6+k AP (minus 100
 * when > 100; the first costs at least 1); it is made when affordable
 * (AP D54C, or always for a free hit D5A4) and the defender lives.  A
 * second attack that is not affordable after a miss (D55A) counts as
 * another miss.
 *  - party vs monster: slayer bonus +1 hit/+2 dmg when the weapon in hand
 *    k (party +3A+2k, < 26) has the monster's group (item word 0E7F+14*i);
 *    race favourite / hated enemy (race +16 / +18) +2/+3, -1/-2;
 *  - chance D5DA = actor's to-hit - D5C2 + bonus; roll random(dodge + 5)
 *    (0 if the defender is stunned): hit when roll <= chance;
 *  - damage: template word 2 = hundreds "random(n/100+1) + n%100", party
 *    members' second attack from the weapon (iso_calc_weapon: D5AA, D5A8);
 *    a stunned defender takes hp + armour; less the armour (word 5);
 *  - a hit marks the defender (+24), may poison (status 15 -> D572) or
 *    drain (status 19 of the attacker -> D56A), and a party member with a
 *    stun weapon (x % 100 = 25) stuns weak monsters (80%: D59A);
 *  - XP 1 per strike; a kill 10, the dead party member's record +06 += 10,
 *    and each living party unit (25..28) gets max(1, value/10), value =
 *    w1 + w9/10 + 5*sign(w16) + w3 + 3*sign(w11) of the victim's template;
 *  - a hard miss (roll - 5 > chance) when nobody is fleeing etc sets D568;
 *  - the defender turns towards the attacker, the attacker shows frame +4,
 *    redraw, the on-hit vecize (+20) is cast, a dead unit leaves its cell.
 * Finally the side-alive flags are recomputed (632E).
 *
 * Original: IST.EXE ovl4595:3F5B (ISOSYS.L), far cdecl.  The Ghidra draft
 * nests the miss path wrongly: every attack made ends at 460E.
 * Verified: tests/py/test_iso.py (scripted duels, rand stream), 2026-11-21.
 */
void iso_attack(void)
{
    int16_t ga, gd, cost, ok, armour, wpn, dmg, roll, grp, value, c, k, save_actor, save_target;

    if (g_iso_attacker <= 0 || g_iso_defender <= 0)
        return;
    g_iso_d572 = 0;
    g_iso_d56a = 0;
    ga = iso_unit(g_iso_attacker)->tmpl;
    gd = iso_unit(g_iso_defender)->tmpl;
    g_iso_d59c = 0;
    roll = 0;       /* bp-0E: kept from the first strike for a missed second one */
    for (g_iso_strike = 1; g_iso_strike <= 2; g_iso_strike++) {
        cost = iso_tmpl(ga)->w[6 + g_iso_strike];
        if (cost > 100)
            cost = (int16_t)(cost - 100);
        if (g_iso_strike == 1 && cost == 0)
            cost = 1;
        ok = g_iso_free_hit != 0 ? 1 : g_iso_ap >= cost;
        if (g_iso_strike == 2 && !ok && g_iso_d55a == 1)
            goto missed;
        if (!ok || cost <= 0 || iso_unit(g_iso_defender)->hp <= 0)
            continue;
        g_iso_anim_unit = g_iso_defender;
        g_iso_anim_kind = 3;
        g_iso_damage = 0;
        g_iso_dmg_bonus = 0;
        g_iso_hit_bonus = 0;
        if (ga < 5 && ga != 0 && gd > 4) {
            grp = g_iso_group_type((g_iso_defender - 1) / 8 + 4);   /* 5BE0 + 6*((c-1)/8) */
            wpn = PARTY_W(ga, 0x3A + 2 * g_iso_strike);
            if (wpn < 0x1A && wpn > 0) {
                if (wpn == 0x2B)
                    wpn = 0x1A;
                if (grp == DGS16((uint16_t)(0x0E7F + 0x0E * wpn))) {
                    g_iso_hit_bonus++;
                    g_iso_dmg_bonus = (int16_t)(g_iso_dmg_bonus + 2);
                }
            }
            if (RACE_W(ga, 0x16) == grp) {
                g_iso_hit_bonus = (int16_t)(g_iso_hit_bonus + 2);
                g_iso_dmg_bonus = (int16_t)(g_iso_dmg_bonus + 3);
            }
            if (RACE_W(ga, 0x18) == grp) {
                g_iso_hit_bonus--;
                g_iso_dmg_bonus = (int16_t)(g_iso_dmg_bonus - 2);
            }
        }
        g_iso_def_dodge = iso_unit(g_iso_defender)->dodge;
        armour = iso_tmpl(gd)->w[5];
        g_iso_chance = (int16_t)(iso_unit(g_iso_actor)->to_hit - g_iso_d5c2 + g_iso_hit_bonus);
        k = (int16_t)(iso_tmpl(ga)->w[2] / 100);
        dmg = (int16_t)(bc_random((int16_t)(k + 1)) + iso_tmpl(ga)->w[2] - k * 100 +
                        g_iso_dmg_bonus);
        if (iso_unit(g_iso_defender)->stun > 0)
            dmg = (int16_t)(iso_unit(g_iso_defender)->hp + armour);
        if (g_iso_strike == 2) {
            iso_redraw();
            if (g_iso_attacker > 0x18) {
                iso_calc_weapon();
                k = (int16_t)(g_iso_wpn_dmg / 100);
                dmg = (int16_t)(bc_random((int16_t)(k + 1)) + g_iso_wpn_base + g_iso_dmg_bonus);
            }
        }
        if (g_iso_free_hit == 0)
            g_iso_ap = (int16_t)(g_iso_ap - cost) < 0 ? 0 : (int16_t)(g_iso_ap - cost);
        roll = bc_random((int16_t)(g_iso_def_dodge + 5));
        iso_unit(g_iso_attacker)->target = g_iso_defender;
        if (iso_unit(g_iso_defender)->stun > 0)
            roll = 0;
        if (roll > g_iso_chance) {
            if ((g_iso_strike == 2 && g_iso_d55a == 1) || iso_tmpl(ga)->w[8] == 0)
                goto missed;
            goto miss_sound;
        }
        g_iso_damage = (int16_t)(dmg - armour) < 0 ? 0 : (int16_t)(dmg - armour);
        g_iso_d55a = 0;
        if (g_iso_damage > 0) {
            iso_unit(g_iso_defender)->hit = 1;
            if (iso_has_status(g_iso_defender, 0x0F) && g_iso_d5c2 == 0)
                g_iso_d572 = 1;
            if (iso_has_status(g_iso_attacker, 0x13) && g_iso_d5c2 == 0)
                g_iso_d56a = 1;
            if (g_iso_attacker > 0x18 &&
                PARTY_W(ga, 2 * PARTY_W(ga, 0x3C)) % 100 == 0x19 &&
                iso_tmpl(gd)->w[9] < 10 && iso_tmpl(gd)->w[10] < 0 &&
                bc_random(100) + 1 < 0x50) {
                g_iso_stunned = 1;
                g_iso_sfx = 0;
            }
            g_iso_d582 = 1;
            g_iso_sfx = 3;
            g_iso_d59c = (int16_t)(g_iso_d59c + g_iso_damage);
        } else {
            g_iso_sfx = 4;
        }
        iso_unit(g_iso_defender)->hp = (int16_t)(iso_unit(g_iso_defender)->hp - g_iso_damage);
        iso_give_xp(g_iso_attacker, 1);
        if (iso_unit(g_iso_defender)->hp < 1) {
            IsoTmpl *v = iso_tmpl(iso_unit(g_iso_defender)->tmpl);
            value = (int16_t)(v->w[1] + v->w[9] / 10 + iso_sign(v->w[16]) * 5 + v->w[3] +
                              iso_sign(v->w[11]) * 3);
            g_iso_anim_kind = 2;
            g_iso_sfx = 2;
            iso_give_xp(g_iso_attacker, 10);
            if (iso_unit(g_iso_defender)->tmpl < 5) {
                PARTY_W(iso_unit(g_iso_defender)->tmpl, 6) += 10;
                iso_xp_progress(iso_unit(g_iso_defender)->tmpl);
            }
            for (c = 0x19; c <= 0x1C; c++)
                if (iso_unit(c)->hp > 0)
                    iso_give_xp(c, value / 10 < 1 ? 1 : (int16_t)(value / 10));
        }
        goto after;
missed:
        if (g_iso_free_hit == 0)
            g_iso_ap = 0;
        if ((int16_t)(roll - 5) > g_iso_chance && g_iso_flee == 0 && g_iso_d5b4 == 0 &&
            g_iso_d5b2 == 1 && g_iso_d5c2 == 0)
            g_iso_flee = 1;
miss_sound:
        g_iso_sfx = 5;
        g_iso_d55a = 1;
after:
        if (g_iso_d5b4 == 0) {
            iso_unit(g_iso_defender)->facing = (int16_t)((iso_unit(g_iso_attacker)->facing + 2) % 4);
            if (iso_unit(g_iso_defender)->facing == 0)
                iso_unit(g_iso_defender)->facing = 4;
        }
        iso_unit(g_iso_attacker)->facing = (int16_t)(iso_unit(g_iso_attacker)->facing + 4);
        iso_redraw();
        if (g_iso_damage > 0 && iso_unit(g_iso_attacker)->on_hit != 0 &&
            iso_unit(g_iso_defender)->hp > 0) {
            save_actor = g_iso_actor;
            save_target = g_iso_target;
            g_iso_quiet = 1;
            g_iso_actor = g_iso_attacker;
            g_iso_target = g_iso_defender;
            g_iso_spell = iso_unit(g_iso_attacker)->on_hit;
            iso_cast_vecize();
            if (g_iso_anim_kind != 6 && iso_unit(g_iso_defender)->hp < 1) {
                iso_redraw();
                iso_play_sfx(g_iso_sfx);
                ui_wait_frames(10);
            }
            g_iso_target = save_target;
            g_iso_actor = save_actor;
            g_iso_anim_unit = g_iso_actor;
            g_iso_anim_kind = 1;
            g_iso_sfx = 0;
        }
        g_iso_quiet = 0;
        if (iso_unit(g_iso_defender)->hp < 1)
            iso_set_cell(iso_unit(g_iso_defender)->x, iso_unit(g_iso_defender)->y, 0);
        ui_wait_frames(10);
        g_iso_anim_unit = g_iso_attacker;
        g_iso_anim_kind = 1;
        iso_unit(g_iso_attacker)->facing = (int16_t)(iso_unit(g_iso_attacker)->facing - 4);
    }
    count_sides();
}

/* sys_fill_slot_rect lines of a pressed panel button */
typedef struct {
    int16_t x1, y1, x2, y2, color;
} IsoLine;

/* the button shown pressed: lines, cursor back, wait until the mouse
   buttons are released (port: pc_poll while a button is down) */
static void press_button(const IsoLine *l, int n)
{
    int i;

    gr_cursor_hide();
    for (i = 0; i < n; i++)
        sys_fill_slot_rect(l[i].x1, l[i].y1, l[i].x2, l[i].y2, (uint8_t)l[i].color, 0);
    gr_cursor_set_shape((uint8_t)g_cursor_shape);
    for (;;) {
        gr_cursor_update((uint8_t)g_cursor_shape);
        if (g_mouse_buttons == 0)
            break;
        pc_poll();
    }
}

static const IsoLine btn_prev[4] = {     /* zone 20h / key 's': previous enemy */
    { 0xF7, 0x92, 0x11B, 0x92, 0xF8 }, { 0xF7, 0x92, 0x104, 0x9F, 0xF8 },
    { 0x105, 0xA0, 0x12A, 0xA0, 6 },   { 0x11C, 0x92, 0x12A, 0xA0, 6 },
};
static const IsoLine btn_next[3] = {     /* zone 21h / key 'o': next enemy */
    { 0x11E, 0x92, 0x140, 0x92, 0xF8 }, { 0x11E, 0x92, 0x12B, 0x9F, 0xF8 },
    { 0x12C, 0xA0, 0x140, 0xA0, 6 },
};
static const IsoLine btn_ok_1f[4] = {    /* zone 1Fh: pick */
    { 0xD0, 0x92, 0xF4, 0x92, 0xF8 }, { 0xD0, 0x92, 0xDD, 0x9F, 0xF8 },
    { 0xDE, 0xA0, 0x103, 0xA0, 6 },   { 0xF5, 0x92, 0x103, 0xA0, 6 },
};
static const IsoLine btn_ok_22[4] = {    /* zone 22h: pick */
    { 0x17, 0x59, 0x22, 0x59, 0xF8 }, { 0x17, 0x59, 0x21, 0x63, 0xF8 },
    { 0x23, 0x64, 0x2D, 0x64, 6 },    { 0x23, 0x59, 0x2E, 0x64, 6 },
};

/*
 * iso_pick_target - the target cursor (D57C = 1): it starts on the actor's
 * last target if that lives, else on the actor.  Keys (bioskey(1) peeked,
 * scan code -> D5D6, the keyboard flushed when there was one) and panel
 * zones:
 *  - keypad 47h..51h, or the compass zones 21..28 with the left button:
 *    move the cursor (iso_move_step);
 *  - zone 20h / 's', zone 21h / 'o': previous / next living enemy (side 2)
 *    from the unit under the cursor, numbers 1..24 wrapping (iso_clamp_add);
 *    the search does not end while no enemy lives and 632E = 0;
 *  - Esc (scan 1) or the right button: D5E0 = -1;
 *  - Enter (scan 1Ch) or zone 1Fh / 22h: D5E0 = D536 = the unit under the
 *    cursor (0 none), also the actor's last target.
 * Picking the actor itself gives -1.  Afterwards the cursor is off and the
 * view centred on the actor.
 *
 * Original: IST.EXE ovl4595:482D (ISOSYS.L), far cdecl.  The redraw after
 * choosing an enemy is followed by a second one (the loop head).
 * Port: the button-release waits call pc_poll() while a button is down.
 * Verified: tests/py/test_iso_b.py (scripted keys), 2026-11-21.
 */
void iso_pick_target(void)
{
    static const int16_t scan[8] = { 0x47, 0x48, 0x49, 0x4B, 0x4D, 0x4F, 0x50, 0x51 };
    int16_t zone, dir, n, start;
    uint16_t key;
    char ch;

    g_iso_cursor_on = 1;
    if (iso_unit(iso_unit(g_iso_actor)->target)->hp > 0) {
        start = iso_unit(g_iso_actor)->target;
        g_iso_cursor_x = iso_unit(start)->x;
        g_iso_cursor_y = iso_unit(start)->y;
    } else {
        g_iso_cursor_x = iso_unit(g_iso_actor)->x;
        g_iso_cursor_y = iso_unit(g_iso_actor)->y;
        start = g_iso_actor;
    }
    g_iso_target = 0;
    iso_center_view(start);
redraw:
    iso_redraw();
    for (;;) {
        key = (uint16_t)ui_bioskey(1);
        g_ui_list_key = (int16_t)((int16_t)key >> 8);
        ch = (char)(key & 0xFF);
        grt_tolower_inplace(&ch);          /* on the low byte of the int */
        iso_dos_break((int16_t)key);       /* Ctrl-Break: INT 23h at the flush's kbhit(), iso.h */
        if (g_ui_list_key != 0)
            ui_flush_keys();
        gr_cursor_update((uint8_t)g_cursor_shape);
        if (g_ui_list_key == 0 && (gr_zone_under_cursor() == 0 || g_mouse_buttons == 0))
            continue;
        zone = gr_zone_under_cursor();
        if (g_ui_list_key == 0x47 || g_ui_list_key == 0x48 || g_ui_list_key == 0x49 ||
            g_ui_list_key == 0x4B || g_ui_list_key == 0x4D || g_ui_list_key == 0x4F ||
            g_ui_list_key == 0x50 || g_ui_list_key == 0x51 ||
            (zone > 0x14 && zone < 0x1D && g_mouse_buttons == 1)) {
            if (zone > 0x14 && zone < 0x1D && g_mouse_buttons != 0)
                g_ui_list_key = scan[zone - 0x15];
            iso_move_step();
            goto redraw;
        }
        if (((zone == 0x20 || zone == 0x21) && g_mouse_buttons == 1) ||
            (uint8_t)ch == 's' || (uint8_t)ch == 'o') {
            if (zone == 0x20 || (uint8_t)ch == 's') {
                g_ui_list_key = 0x1F;
                press_button(btn_prev, 4);
            } else if (zone == 0x21 || (uint8_t)ch == 'o') {
                g_ui_list_key = 0x18;
                press_button(btn_next, 3);
            }
            dir = (int16_t)(iso_sign((int16_t)(0x1E - g_ui_list_key)) * -1);
            n = g_iso_occ(g_iso_cursor_x, g_iso_cursor_y);
            do
                iso_clamp_add(&n, dir, 1, 0x18);
            while (!(iso_unit(n)->hp > 0 && iso_unit(n)->side == 2) && g_iso_result == 0);
            g_iso_cursor_x = iso_unit(n)->x;
            g_iso_cursor_y = iso_unit(n)->y;
            iso_center_view(n);
            iso_redraw();
            goto redraw;
        }
        if (g_ui_list_key == 1 || g_mouse_buttons == 2) {
            g_iso_target = -1;
            break;
        }
        if (g_ui_list_key == 0x1C ||
            ((zone == 0x1F || zone == 0x22) && g_mouse_buttons == 1)) {
            if ((zone == 0x1F || zone == 0x22) && g_mouse_buttons == 1)
                press_button(zone == 0x1F ? btn_ok_1f : btn_ok_22, 4);
            g_iso_near = g_iso_occ(g_iso_cursor_x, g_iso_cursor_y);
            g_iso_target = g_iso_near;
            iso_unit(g_iso_actor)->target = g_iso_near;
            break;
        }
    }
    if (g_iso_target == g_iso_actor)
        g_iso_target = -1;
    g_iso_cursor_on = 0;
    iso_center_view(g_iso_actor);
}

/* a refusal: voice 631A, the text (copied to A574) said by the actor,
   picture 6, wait for the voice and free it */
static void refuse(uint16_t text, int16_t voice, int16_t mark, int16_t line, uint16_t file)
{
    iso_redraw();
    g_iso_voice = voice;
    strcpy(DGSTR(0xA574), DGSTR(text));
    iso_show_message(DGSTR(0xA574), g_iso_actor);
    if (mark)
        g_iso_d582 = 1;
    iso_show_picture(6);
    ui_wait_frames(10);
    ui_wait_voice();
    if (DG32(0xE12B) != 0)
        ctl_free_checked(0xE12B, line, DGSTR(file));
}

/*
 * iso_cmd_attack - the ranged attack command of a unit whose first attack
 * costs >= 100 (template word 7 = 100 + AP): with a target D5E0 > 0 the
 * line of sight to the cursor is traced (iso_trace_line); when it is clear
 * and in range (template word 17 >= D564 steps), or the target is next to
 * the actor (D564 = 0), and the AP suffice: the actor shows its attack
 * frame, sound 8, the missile flies (iso_scroll_view_dir with the icon of
 * the current party member's weapon in hand: record word 2*(+3C) % 100)
 * and iso_attack runs with D5C2 = 1.  Otherwise one of three lines: no
 * line of sight (voice 1Eh), too far (1Fh), not enough AP (20h).
 *
 * Original: IST.EXE ovl4595:4DB8 (ISOSYS.L), far cdecl.
 * Verified: tests/py/test_iso_b.py, 2026-11-21.
 */
void iso_cmd_attack(void)
{
    int16_t cost;

    if (g_iso_target < 1 || iso_utmpl(g_iso_actor)->w[7] < 100)
        return;
    iso_trace_line(g_iso_actor, g_iso_cursor_x, g_iso_cursor_y);
    cost = iso_utmpl(g_iso_actor)->w[7];
    if (cost > 100)
        cost = (int16_t)(cost - 100);
    if ((g_iso_los_clear > 0 && iso_utmpl(g_iso_actor)->w[17] >= g_iso_los_dist &&
         g_iso_ap >= cost) ||
        (g_iso_los_dist == 0 && g_iso_ap >= cost)) {
        g_iso_attacker = g_iso_actor;
        g_iso_defender = g_iso_target;
        iso_center_view(g_iso_attacker);
        iso_unit(g_iso_attacker)->facing = (int16_t)(iso_unit(g_iso_attacker)->facing + 4);
        iso_redraw();
        iso_play_sfx(8);
        ui_wait_frames(10);
        iso_scroll_view_dir(iso_unit(g_iso_defender)->x, iso_unit(g_iso_defender)->y,
                            iso_unit(g_iso_attacker)->x, iso_unit(g_iso_attacker)->y,
                            (int16_t)(PARTY_W(g_party_cur, 2 * PARTY_W(g_party_cur, 0x3C)) % 100));
        iso_center_view(g_iso_defender);
        iso_unit(g_iso_attacker)->facing = (int16_t)(iso_unit(g_iso_attacker)->facing - 4);
        g_iso_d5c2 = 1;
        iso_attack();
        g_iso_d5c2 = 0;
        iso_center_view(g_iso_actor);
        return;
    }
    if (g_iso_los_clear == 0)
        refuse(0x665D, 0x1E, 0, 0x6CB, 0x666B);
    else if (iso_utmpl(g_iso_actor)->w[17] < g_iso_los_dist)
        refuse(0x667B, 0x1F, 1, 0x6D7, 0x6690);
    else
        refuse(0x66A0, 0x20, 0, 0x6DF, 0x66BB);
}

/* vecize (spell) i = 1..34: name (31 bytes at 0361 + 1Fh*i), level
   (0C86 + 2*i); a caster knows the vecizes whose character i (1-based) of
   its 34/35-character knowledge string is '1' */
#define ISO_VECIZE_NAME(i)   DGSTR((uint16_t)(0x0361 + 0x1F * (i)))
#define ISO_VECIZE_LEVEL(i)  DGS16((uint16_t)(0x0C86 + 2 * (i)))
#define ISO_LIST_ENTRY(k)    DGSTR((uint16_t)(0xA5D8 + 100 * (k)))   /* ui_event_loop list */

/* the original's test: strncpy(buf = " \0", A50F + i, 1); atol(buf) == 1 */
static int knows(uint16_t i)
{
    return DG8((uint16_t)(0xA50F + i)) == '1';
}

/* strrev() (Borland: in place) */
static void str_reverse(char *s)
{
    size_t i, n = strlen(s);

    for (i = 0; i < n / 2; i++) {
        char t = s[i];
        s[i] = s[n - 1 - i];
        s[n - 1 - i] = t;
    }
}

/*
 * iso_cmd_vecize - choose a vecize ("saying", the spells) to cast.  In the
 * battle window the caster is the actor (2898 = its template, D556 = its
 * vecize points +02); a monster's knowledge string is 195C + 23h * its
 * group type (34 chars), a party member's the record's 9C95 + 94h*p (35);
 * outside the battle (the camp) the caster is party member 2898 with
 * D556 = record word +38.  The known vecizes are listed (A5D8 + 100*k:
 * name + "!"; written backwards when the level D5BC is above D556):
 *  - none: "Bu adam vecize okuyamaz!" (battle, waits for a key) or
 *    "vecize bilmiyor!" (2000 ms); D5E2 = 0;
 *  - else ui_event_loop; the k-th known vecize -> D5E2 (BB53 the scan;
 *    0 when cancelled), its name -> A574, its level -> D5BC, and its
 *    strength D562 from the caster's template word 9 / 50 (D5BE).
 * In the battle window the screen is redrawn at the end.
 *
 * Original: IST.EXE ovl4595:50BD (ISOSYS.L), far cdecl.  The knowledge
 * test is strncpy(" ", s + i - 1, 1) + atol() == 1, i.e. the character '1'.
 * Port: the 2000 ms wait calls pc_poll().
 * Verified: tests/py/test_iso_b.py, 2026-11-21.
 */
void iso_cmd_vecize(void)
{
    int16_t n, i, p;

    if (iso_in_battle_window()) {
        g_party_cur = iso_unit(g_iso_actor)->tmpl;
        g_iso_d556 = iso_unit(g_iso_actor)->spell;
        if (g_iso_actor <= 0x18)
            strncpy(DGSTR(0xA510), DGSTR((uint16_t)(0x195C + 0x23 * DGS16((uint16_t)(0x5BC2 + 6 * g_party_cur)))),
                    0x22);
        else
            strncpy(DGSTR(0xA510), DGSTR((uint16_t)(0x9C95 + 0x94 * g_party_cur)), 0x23);
    } else {
        g_iso_d556 = PARTY_W(g_party_cur, 0x38);
        strncpy(DGSTR(0xA510), DGSTR((uint16_t)(0x9C95 + 0x94 * g_party_cur)), 0x23);
    }
    n = 0;
    for (i = 1; i <= 0x22; i++) {
        if (!knows((uint16_t)i))
            continue;
        n++;
        strcpy(ISO_LIST_ENTRY(n), ISO_VECIZE_NAME(i));
        g_iso_d5bc = ISO_VECIZE_LEVEL(i);
        if (g_iso_d5bc > g_iso_d556)
            str_reverse(ISO_LIST_ENTRY(n));     /* strcpy(s, strrev(s)) */
        strcat(ISO_LIST_ENTRY(n), DGSTR(0x66CB));   /* "!" */
    }
    if (n == 0) {
        if (iso_in_battle_window()) {
            iso_blit_icon(7, 0x20, 0x5B);
            iso_draw_text_shadow(DGSTR(0x66CD), 7, 9, DG8(0x9665), 0x0B);  /* "Bu adam vecize okuyamaz!" */
            iso_show_picture(6);
            ui_getch();
            iso_redraw();
        } else {
            iso_draw_textbox(DGSTR(0x66E6), 0xD9, DG8(0x9664));         /* "vecize bilmiyor!" */
            g_iso_msg_time = (int16_t)(uint16_t)g_clock_ms;
            do {
                pc_poll();
                gr_cursor_update((uint8_t)g_cursor_shape);
            } while ((uint16_t)((uint16_t)g_clock_ms - (uint16_t)g_iso_msg_time) < 2000);
        }
        g_iso_spell = 0;
        return;
    }
    if (iso_in_battle_window()) {
        ui_event_loop(n, 5, 7, 0x86, DG8(0x9664), 0xD9, n > 4 ? 4 : n, 1);
        iso_init_panel_layout();
    } else {
        ui_event_loop(n, 3, 0x13, DG8(0x966B), DG8(0x9666), DG8(0x966A), n > 6 ? 6 : n, 2);
    }
    i = 0;
    DGS16(0xBB53) = 0;
    g_iso_spell = 0;
    if (g_ui_list_sel > 0) {
        do {
            do
                DGS16(0xBB53)++;
            while (!knows(DG16(0xBB53)));
            i++;
        } while (i != g_ui_list_sel);
        g_iso_spell = DGS16(0xBB53);
        strcpy(DGSTR(0xA574), ISO_LIST_ENTRY(g_ui_list_sel));
        g_iso_d5bc = ISO_VECIZE_LEVEL(g_iso_spell);
        p = g_iso_caster_power = (int16_t)(iso_utmpl(g_iso_actor)->w[9] / 50);
        switch (g_iso_spell) {
        case 1: case 2: case 10: case 11: case 15: case 18: case 19: case 31:
            g_iso_spell_arg = 2;
            break;
        case 3: case 4: case 5: case 6: case 14: case 16: case 26: case 28:
            g_iso_spell_arg = (int16_t)(p / 3 + 4);
            break;
        case 7:
            g_iso_spell_arg = (int16_t)(p * 10);
            break;
        case 8: case 9: case 17: case 22: case 24: case 30: case 32: case 33:
            g_iso_spell_arg = (int16_t)(p + 10);
            break;
        case 12: case 23: case 25: case 34:
            g_iso_spell_arg = 0;
            break;
        case 13: case 20: case 21:
            g_iso_spell_arg = (int16_t)(p / 3 + 8);
            break;
        case 27:
            g_iso_spell_arg = (int16_t)(p / 3 * 3 + 5);
            break;
        case 29:
            g_iso_spell_arg = (int16_t)(p + 5);
            break;
        default:
            o559A_fatal(DGSTR(0x66F7), 0x736, NULL, 100);
            break;
        }
    }
    if (iso_in_battle_window()) {
        iso_redraw();
        ui_wait_frames(10);
    }
}

/*
 * iso_vecize_reaction - a target with status 25 (it answers back) that
 * was hit by a vecize (D570 != 0) says "bana mı dedin ?" (voice 22h).
 * When random(caster's template word 10 / 50) < target's word 9 / 50 and
 * the caster's word 10 > 0, the caster backs down ("yok abicim, ne
 * haddimize", 23h) and becomes the target itself (D5E0 = D5DE); else it
 * insists ("sana dedim, n'olcek ?", 24h) and D570 doubles.  D5C0 = 1.
 *
 * Original: IST.EXE ovl4595:552C (ISOSYS.L), far cdecl.  The random
 * number is (long)rand() * n / 8000h (Borland random(n)) with the
 * template word computed after rand().
 * Verified: tests/py/test_iso_b.py, 2026-11-21.
 */
void iso_vecize_reaction(void)
{
    int16_t r;

    if (!iso_has_status(g_iso_target, 0x19) || g_iso_damage == 0)
        return;
    iso_redraw();
    g_iso_voice = 0x22;
    g_iso_d582 = 1;
    iso_show_message(DGSTR(0x6707), g_iso_target);       /* "bana mı dedin ?" */
    ui_wait_frames(10);
    r = bc_random((int16_t)(iso_utmpl(g_iso_actor)->w[10] / 50));
    if (r < iso_utmpl(g_iso_target)->w[9] / 50 && iso_utmpl(g_iso_actor)->w[10] > 0) {
        iso_center_view(g_iso_actor);
        iso_redraw();
        g_iso_d582 = 1;
        g_iso_voice = 0x23;
        iso_show_message(DGSTR(0x6717), g_iso_actor);    /* "yok abicim, ne haddimize" */
        ui_wait_frames(10);
        iso_center_view(g_iso_actor);
        g_iso_target = g_iso_actor;
    } else {
        iso_center_view(g_iso_actor);
        iso_redraw();
        g_iso_voice = 0x24;
        g_iso_d582 = 1;
        iso_show_message(DGSTR(0x6730), g_iso_actor);    /* "sana dedim, n'olcek ?" */
        ui_wait_frames(10);
        g_iso_damage = (int16_t)(g_iso_damage * 2);
        iso_center_view(g_iso_target);
    }
    g_iso_d5c0 = 1;
}
