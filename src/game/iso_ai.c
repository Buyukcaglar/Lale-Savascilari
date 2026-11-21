/*
 * iso_ai.c - ISOSYS.L 4595:9668-B04C: the monsters' turn, experience and
 * growth.  See iso.h.
 *
 */
#include "game/iso.h"

#include <stdio.h>
#include <string.h>

#include "engine/fatal.h"
#include "engine/intrface.h"
#include "engine/nkontrol.h"
#include "rtl/bc_rand.h"

/*
 * iso_give_xp - unit c gets n experience points: a party unit (template
 * < 5) adds n to its record word +08 and grows (iso_xp_progress).
 *
 * Original: IST.EXE ovl4595:AA60 (ISOSYS.L), far cdecl (c, n).
 * Verified: tests/py/test_iso.py, 2026-11-21.
 */
void iso_give_xp(int16_t c, int16_t n)
{
    if (iso_unit(c)->tmpl >= 5)
        return;
    DGS16((uint16_t)(0x99AE + 0x5C * iso_unit(c)->tmpl)) += n;
    iso_xp_progress(iso_unit(c)->tmpl);
}

/* growth rules: when counter (record +06 "practice" or +08 "experience")
   has grown more than `over` since the member's mark (9902 + 22h*p + ..),
   stat dst grows by max(0, sign(race word src)) + 1 and the mark moves */
typedef struct {
    uint16_t cnt, over, mark, src, dst;
} Growth;

static const Growth growth[] = {
    { 0x99AE, 999,   0x9908, 0x9B7E, 0x99B2 },   /* max(0, race word) + 1, not its sign */
    { 0x99AC, 0xBD,  0x990A, 0x9B78, 0x99B4 },
    { 0x99AE, 99,    0x990C, 0x9B7A, 0x99B6 },
    { 0x99AC, 0xD1,  0x990E, 0x9B82, 0x99B8 },
    { 0x99AE, 99,    0x9910, 0x9B80, 0x99BA },
    { 0x99AC, 199,   0x9914, 0x9B88, 0x99BE },
    { 0x99AC, 0x76B, 0x9916, 0x9B84, 0x99C0 },   /* += race word - 3 */
    { 0x99AC, 0xEA,  0x9918, 0x9B8A, 0x99C2 },
    { 0x99AC, 1999,  0x991A, 0x9B8C, 0x99C4 },
    { 0x99AC, 199,   0x991C, 0x9B8E, 0x99C6 },
};

/*
 * iso_xp_progress - growth of party member p (1..4) from its counters
 * (no levels): first the new experience (+08 above mark 9902) is added to
 * +06, then the table above, in order.
 *
 * Original: IST.EXE ovl4595:AAAE (ISOSYS.L), far cdecl (p).  The
 * original's max(0, iso_sign(x)) calls iso_sign twice (macro); it has no
 * side effects.
 * Verified: tests/py/test_iso.py, 2026-11-21.
 */
void iso_xp_progress(int16_t p)
{
    unsigned i;
    int16_t d, s;

    d = (int16_t)(DGS16((uint16_t)(0x99AE + 0x5C * p)) - DGS16((uint16_t)(0x9902 + 0x22 * p)));
    if (d > 0) {
        DGS16((uint16_t)(0x99AC + 0x5C * p)) += d;
        DGS16((uint16_t)(0x9902 + 0x22 * p)) = DGS16((uint16_t)(0x99AE + 0x5C * p));
    }
    for (i = 0; i < sizeof growth / sizeof growth[0]; i++) {
        const Growth *g = &growth[i];
        int16_t cnt = DGS16((uint16_t)(g->cnt + 0x5C * p));

        if ((int16_t)(cnt - DGS16((uint16_t)(g->mark + 0x22 * p))) <= (int16_t)g->over)
            continue;
        s = DGS16((uint16_t)(g->src + 0x24 * p));
        if (i == 6)
            s = (int16_t)(s - 3);
        else if (i == 0)
            s = (int16_t)((s < 0 ? 0 : s) + 1);
        else
            s = (int16_t)((iso_sign(s) < 0 ? 0 : iso_sign(s)) + 1);
        DGS16((uint16_t)(g->dst + 0x5C * p)) += s;
        DGS16((uint16_t)(g->mark + 0x22 * p)) = cnt;
    }
}

#define g_iso_ai_clock       DGS16(0xD534)  /* i16 clock (low word) of the AI's decision */
#define g_iso_ai_count       DGS16(0xD5D6)  /* i16 iso_ai_turn: neighbour counter (also the list key) */
#define g_iso_bb53           DGS16(0xBB53)  /* i16 loop counter shared with other modules */
#define g_iso_clock_lo       DGS16(0xE74A)  /* i16 100 Hz clock, low word */
#define VECIZE_COST(s)       DGS16((uint16_t)(0x0C88 + 2 * (s)))   /* vecize points of vecize s */
#define AREA_VECIZE(i)       DGS16((uint16_t)(0x0CCC + 2 * (i)))   /* i = 1, 3 .. 9: 13, 17, 22, 24, 27 */
#define VECIZE_NAME(s)       DGSTR((uint16_t)(0x0361 + 0x1F * (s)))
#define KNOWS(i)             DG8((uint16_t)(0xA510 + (i)))         /* '1': vecize i known (copy) */

static int16_t abs16(int16_t v) { return v < 0 ? (int16_t)-v : v; }  /* cwd/xor/sub */

/* unit c faces unit t: west 2, east 4, north 1, else south 3 */
static void face(int16_t c, int16_t t)
{
    if (iso_unit(t)->x < iso_unit(c)->x)
        iso_unit(c)->facing = 2;
    else if (iso_unit(t)->x > iso_unit(c)->x)
        iso_unit(c)->facing = 4;
    else if (iso_unit(t)->y < iso_unit(c)->y)
        iso_unit(c)->facing = 1;
    else
        iso_unit(c)->facing = 3;
}

/* the unit on cell (x, y) of a scan: 0 outside 1..27 (column / row 0
   counts as outside) */
static int16_t scan_cell(int16_t x, int16_t y)
{
    if (x > 0x1B || x < 1 || y > 0x1B || y < 1)
        return 0;
    return g_iso_occ(x, y);
}

/*
 * iso_ai_turn - the turn of computer unit D5DE (a monster, side 2):
 *  1. at the start of a round (D54E) the monsters may surrender: score =
 *     (others' hp - monsters' hp) + (others' vecize points / 10 - those of
 *     the LAST monster with template word 10 >= 0 / 10) + 10 (D54E) - 10
 *     (D550) + 20 (D558 / living monsters >= 2); when random(150) +
 *     random(150) + 2 <= score, the actor's template word 10 > 0 and its
 *     group type (5BE0 + 6*((c-1)/8)) < 30 and not 19..23: "teslim,
 *     teslim!" (voice 1Dh), the actor leaves (hp 0), the alive flags 2632..
 *     are recounted, no party or no enemy left -> 632E = 1.
 *  2. (while AP > 0) the target: the old one; the score for changing it:
 *     100 when dead or an ally, near*5 next to it (near = party units
 *     around the actor), else 70 / 90 with party neighbours (line clear /
 *     not), 5 / 50 without; +5 when the actor was hit.  random(100)+1 <=
 *     score: with neighbours and random(100)+1 < 70 + 10*near, a neighbour
 *     that targets the actor (80%) or the random(near)+1-th neighbour
 *     numbered above the actor; else a random living party unit.  A dead
 *     or allied target becomes the first living party unit.
 *  3. vecize or attack: the vecize chance grows with the vecize points sp
 *     against 50*to-hit + 30 (none when the actor was hit).  Vecize: a
 *     random known one among those before the first it cannot pay,
 *     within its range (iso_cmd_vecize's) of the line of sight; an area
 *     vecize only when random(100)+1 <= the party's hp - the others' hp +
 *     vecize point difference / 10 + 10 per party unit - 5 per other cell
 *     of the square.  It faces the target and casts (iso_cast_vecize, name
 *     in A574); a party target gains vecize experience (record +0E += 1 +
 *     max(0, sign(class word 2))); AP 0.  Attack: next to the target
 *     iso_attack and iso_after_hit; a ranged monster (template word 7 >
 *     100) shoots when the target is within word 17 (else the turn ends),
 *     the line is clear and random(100)+1 >= 6.
 *  4. otherwise it walks towards the target: up to 3 steps (straight, then
 *     one direction to the left, then two to the right, wrapping 1..8);
 *     a step onto a free cell (D5B8) decides again (2.).
 *
 * Original: IST.EXE ovl4595:9668 (ISOSYS.L), far cdecl.  Quirks kept:
 *  - both neighbour choices in 2. test the unit of the scan's LAST cell
 *    (x+1, y+1) nine times instead of each neighbour (the scan's variable
 *    is reused), so that unit may be counted several times, others never;
 *  - the area test uses the random index of the chosen vecize as the
 *    square's radius and counts empty cells (unit 0) with the monsters;
 *  - a vecize out of 1..34 (cannot happen) is fatal "Böyle bir büyü
 *    bulunmamaktadır: %d" and then uses the previous range (the
 *    original's uninitialised stack word; here 0);
 *  - with the target on the actor's cell the first direction is not set
 *    (stack word kept from the previous walk; here 0 at first);
 *  - D558 / (living monsters) divides by zero when none lives: the
 *    game's INT 0 handler gives -1 (no bonus).  The word divisions
 *    (idiv word [bp-xx], 3 bytes each) go through nk_idiv_s16, which also
 *    covers 50*to-hit + 30 wrapping to 0 (to-hit 6553).
 * Verified: tests/py/test_iso_b.py (seeded turns, random battles), 2026-11-21.
 */
void iso_ai_turn(void)
{
    int16_t m_hp = 0, o_hp = 0, m_sp = 0, o_sp = 0, m_alive = 0, score = 0;
    int16_t c, ax0, ay0, tgt, near, any, nb = 0, sp, thr, div, use, count, pick = 0;
    int16_t range = 0, go, area = 0, n1, n2, steps, dir = 0, k, r, a, b;
    int16_t actor = g_iso_actor;
    IsoUnit *u;

    if (g_iso_d54e != 0) {
        for (c = 1; c <= ISO_UNITS; c++) {
            u = iso_unit(c);
            if (u->side == 2) {
                m_hp = (int16_t)(m_hp + (u->hp < 0 ? 0 : u->hp));
                if (iso_utmpl(c)->w[10] > -1)
                    m_sp = u->spell;
                if (u->hp > 0)
                    m_alive++;
            } else {
                o_hp = (int16_t)(o_hp + (u->hp < 0 ? 0 : u->hp));
                o_sp = (int16_t)(o_sp + u->spell);
            }
        }
        score = (int16_t)(score + (o_hp - m_hp));
        score = (int16_t)(score + (o_sp / 10 - m_sp / 10));
        if (g_iso_d54e != 0)
            score = (int16_t)(score + 10);
        if (g_iso_d550 != 0)
            score = (int16_t)(score - 10);
        if (nk_idiv_s16(g_iso_monsters, m_alive, NULL) >= 2)    /* 4595:97AD */
            score = (int16_t)(score + 20);
        a = bc_random(0x96);
        b = bc_random(0x96);
        if ((int16_t)(a + b + 2) <= score && iso_utmpl(actor)->w[10] > 0) {
            k = g_iso_group_type((actor - 1) / 8 + 4);
            if (k < 0x1E && (k < 0x13 || k > 0x17)) {
                iso_redraw();
                g_iso_d582 = 1;
                g_iso_voice = 0x1D;
                iso_show_message(DGSTR(0x6834), actor);          /* "teslim, teslim!" */
                ui_wait_frames(10);
                iso_set_cell(iso_unit(actor)->x, iso_unit(actor)->y, 0);
                iso_unit(actor)->hp = 0;
                g_iso_alive(2) = 0;
                g_iso_alive(1) = 0;
                for (g_iso_bb53 = 1; g_iso_bb53 <= ISO_UNITS; g_iso_bb53++)
                    if (iso_unit(g_iso_bb53)->hp >= 1)
                        g_iso_alive(iso_unit(g_iso_bb53)->side) = 1;
                if (g_iso_alive(1) == 0 || g_iso_alive(2) == 0)
                    g_iso_result = 1;
                return;
            }
        }
    }

decide:
    if (g_iso_ap < 1)
        return;
    score = 0;
    ax0 = iso_unit(actor)->x;
    ay0 = iso_unit(actor)->y;
    tgt = iso_unit(actor)->target;
    near = 0;
    iso_trace_line(actor, iso_unit(tgt)->x, iso_unit(tgt)->y);
    for (g_iso_cell_y = -1; g_iso_cell_y <= 1; g_iso_cell_y++)
        for (g_iso_cell_x = -1; g_iso_cell_x <= 1; g_iso_cell_x++) {
            nb = scan_cell((int16_t)(ax0 + g_iso_cell_x), (int16_t)(ay0 + g_iso_cell_y));
            if (nb > 0 && nb != actor && iso_unit(nb)->side == 1)
                near++;
        }
    if (iso_unit(tgt)->hp < 1 || iso_unit(tgt)->side == 2)
        score = 100;
    else if (abs16((int16_t)(ax0 - iso_unit(tgt)->x)) < 2 &&
             abs16((int16_t)(ay0 - iso_unit(tgt)->y)) < 2)
        score = (int16_t)(near * 5);
    else if (near > 0)
        score = g_iso_los_clear != 0 ? 0x46 : 0x5A;
    else if (near == 0)
        score = g_iso_los_clear != 0 ? 5 : 0x32;
    if (iso_unit(actor)->hit != 0)
        score = (int16_t)(score + 5);

    any = 0;
    for (c = 1; c <= ISO_UNITS; c++)
        if (iso_unit(c)->side == 1 && iso_unit(c)->hp > 0) {
            any = 1;
            break;
        }
    if (any == 1 && bc_random(100) + 1 <= score) {
        if (near != 0 && bc_random(100) + 1 < near * 10 + 0x46) {
            /* nb: still the unit of the scan's last cell */
            for (g_iso_cell_y = -1; g_iso_cell_y <= 1; g_iso_cell_y++)
                for (g_iso_cell_x = -1; g_iso_cell_x <= 1; g_iso_cell_x++)
                    if (iso_unit(nb)->target == actor && bc_random(100) + 1 < 0x50) {
                        tgt = nb;
                        goto chosen;
                    }
            pick = near > 1 ? (int16_t)(bc_random(near) + 1) : 1;
            g_iso_ai_count = 0;
            for (g_iso_cell_y = -1; g_iso_cell_y <= 1; g_iso_cell_y++)
                for (g_iso_cell_x = -1; g_iso_cell_x <= 1; g_iso_cell_x++)
                    if (nb > 0 && nb > actor && iso_unit(nb)->side == 1 &&
                        ++g_iso_ai_count == pick) {
                        tgt = nb;
                        goto chosen;
                    }
            goto keep;
        }
        n1 = 0;
        n2 = 0;
        for (c = 1; c <= ISO_UNITS; c++)
            if (iso_unit(c)->side == 1 && iso_unit(c)->hp > 0)
                n2++;
        n2 = (int16_t)(bc_random(n2) + 1);
        for (c = 1; c <= ISO_UNITS; c++) {
            if (iso_unit(c)->side == 1 && iso_unit(c)->hp > 0)
                n1++;
            if (n1 == n2) {
                tgt = c;
                goto chosen;
            }
        }
    }
keep:
    if (iso_unit(tgt)->hp < 1 || iso_unit(tgt)->side == 2)
        for (c = 1; c <= ISO_UNITS; c++)
            if (iso_unit(c)->side == 1 && iso_unit(c)->hp > 0) {
                tgt = c;
                break;
            }
chosen:
    g_iso_ai_clock = g_iso_clock_lo;
    iso_unit(actor)->target = tgt;
    sp = iso_unit(actor)->spell;
    thr = (int16_t)(iso_unit(actor)->to_hit * 50 + 0x1E);
    if (sp > thr) {
        div = (int16_t)(nk_idiv_s16(sp, thr, NULL) * 2 + 1);       /* 9E80 */
        r = bc_random((int16_t)(sp + 1));
        use = r > nk_idiv_s16(thr, div, NULL);                      /* 9EAE */
    } else if (sp != 0) {
        div = (int16_t)(nk_idiv_s16(thr, sp, NULL) * 2 + 1);       /* 9ECB */
        r = bc_random((int16_t)(thr + 1));
        use = r <= nk_idiv_s16(sp, div, NULL);                      /* 9EF9 */
    } else {
        use = 0;
    }
    if (iso_unit(actor)->hit != 0)
        use = 0;
    count = 0;
    g_iso_d556 = iso_unit(actor)->spell;
    iso_trace_line(actor, iso_unit(tgt)->x, iso_unit(tgt)->y);
    g_iso_caster_power = (int16_t)(iso_utmpl(actor)->w[9] / 50);
    g_iso_target = tgt;
    g_iso_d5bc = 0;
    if (use == 1 && g_iso_los_clear == 0)
        goto walk;
    if (use != 1)
        goto attack;

    /* ---- vecize ---- */
    if (actor > 0x18)       /* a party unit: its knowledge string */
        strncpy(DGSTR(0xA510), DGSTR((uint16_t)(0x8EB4 + 0x94 * actor)), 0x22);
    else
        strncpy(DGSTR(0xA510),
                DGSTR((uint16_t)(0x195B + 0x23 * g_iso_group_type(iso_unit(actor)->tmpl - 1))), 0x23);
    for (c = 1; c <= 0x22; c++)
        if (KNOWS(c) == '1') {
            g_iso_d5bc = VECIZE_COST(c);
            if (g_iso_d5bc > g_iso_d556)
                break;
            count++;
        }
    if (count == 0)
        goto walk;
    pick = count > 1 ? (int16_t)(bc_random(count) + 1) : 1;
    count = 0;
    for (g_iso_spell = 1; g_iso_spell <= 0x22; g_iso_spell++)
        if (KNOWS(g_iso_spell) == '1' && ++count == pick)
            break;
    switch (g_iso_spell) {          /* range, jump table 4595:AA1C */
    case 1: case 2: case 10: case 11: case 15: case 18: case 19: case 31:
        range = 2; break;
    case 3: case 4: case 5: case 6: case 14: case 16: case 26: case 28:
        range = (int16_t)(g_iso_caster_power / 3 + 4); break;
    case 7:
        range = (int16_t)(g_iso_caster_power * 10); break;
    case 8: case 9: case 17: case 22: case 24: case 30: case 32: case 33:
        range = (int16_t)(g_iso_caster_power + 10); break;
    case 12: case 23: case 25: case 34:
        range = 0; break;
    case 13: case 20: case 21:
        range = (int16_t)(g_iso_caster_power / 3 + 8); break;
    case 27:
        range = (int16_t)(g_iso_caster_power / 3 * 3 + 5); break;
    case 29:
        range = (int16_t)(g_iso_caster_power + 5); break;
    default:
        sprintf(DGSTR(0xBACC), DGSTR(0x6844), (int)g_iso_spell);
        o559A_fatal(DGSTR(0x6870), 0xB4C, DGSTR(0xBACC), -1);
        break;
    }
    go = 1;
    if (range < g_iso_los_dist)
        goto walk;
    for (g_iso_bb53 = 1; g_iso_bb53 <= 10; g_iso_bb53 += 2) {
        area = AREA_VECIZE(g_iso_bb53);
        if (area == g_iso_spell)
            break;
    }
    if (area == g_iso_spell) {
        g_iso_cell_x = iso_unit(actor)->x;
        g_iso_cell_y = iso_unit(actor)->y;
        o_hp = m_hp = o_sp = m_sp = n1 = n2 = 0;
        for (g_iso_bb53 = (int16_t)(g_iso_cell_y - pick); g_iso_bb53 <= g_iso_cell_y + pick;
             g_iso_bb53++)
            for (c = (int16_t)(g_iso_cell_x - pick); c <= g_iso_cell_x + pick; c++) {
                g_iso_near = scan_cell(c, g_iso_bb53);
                u = iso_unit(g_iso_near);
                if (u->side == 1) {
                    o_hp = (int16_t)(o_hp + u->hp);
                    o_sp = (int16_t)(o_sp + u->spell);
                    n1++;
                } else {
                    m_hp = (int16_t)(m_hp + u->hp);
                    m_sp = (int16_t)(m_sp + u->spell);
                    n2++;
                }
            }
        r = (int16_t)(bc_random(100) + 1);
        go = r > (int16_t)(o_hp - m_hp + (int16_t)(o_sp - m_sp) / 10 + n1 * 10 - n2 * 5) ? 0 : 1;
    }
    if (go == 0)
        goto walk;
    face(actor, tgt);
    g_iso_target = tgt;
    g_iso_d5bc = VECIZE_COST(g_iso_spell);
    g_iso_tgt_power = (int16_t)(iso_utmpl(g_iso_target)->w[10] / 50);
    if (iso_has_status(g_iso_target, 0xE))
        g_iso_tgt_power++;
    strcpy(DGSTR(0xA574), VECIZE_NAME(g_iso_spell));
    iso_cast_vecize();
    iso_center_view(g_iso_target);
    iso_redraw();
    ui_wait_frames(10);
    if (iso_unit(g_iso_target)->tmpl < 5) {
        /* party record +0E += 1 + max(0, sign(class word 2)) (iso_sign twice) */
        k = iso_sign(DGS16((uint16_t)(0x9B80 + 0x24 * iso_unit(g_iso_target)->tmpl)));
        if (k < 0)
            k = 0;
        else
            k = iso_sign(DGS16((uint16_t)(0x9B80 + 0x24 * iso_unit(g_iso_target)->tmpl)));
        DGS16((uint16_t)(0x99BA + 0x5C * iso_unit(g_iso_target)->tmpl)) += (int16_t)(k + 1);
        iso_xp_progress(iso_unit(g_iso_target)->tmpl);
    }
    if (iso_unit(g_iso_target)->hp < 1)
        iso_set_cell(iso_unit(g_iso_target)->x, iso_unit(g_iso_target)->y, 0);
    iso_center_view(actor);
    g_iso_anim_unit = actor;
    g_iso_anim_kind = 1;
    g_iso_ap = 0;
    return;

attack:
    face(actor, tgt);
    if (abs16((int16_t)(ax0 - iso_unit(tgt)->x)) < 2 && abs16((int16_t)(ay0 - iso_unit(tgt)->y)) < 2) {
        g_iso_attacker = actor;
        g_iso_defender = tgt;
        iso_attack();
        iso_redraw();
        ui_wait_frames(10);
        iso_after_hit();
        return;
    }
    if (iso_utmpl(actor)->w[7] <= 100)
        goto walk;
    iso_trace_line(actor, iso_unit(tgt)->x, iso_unit(tgt)->y);
    if (iso_utmpl(actor)->w[17] < g_iso_los_dist)
        return;
    if (bc_random(100) + 1 < 6 || g_iso_los_clear == 0)
        goto walk;
    g_iso_attacker = actor;
    g_iso_defender = tgt;
    iso_center_view(g_iso_attacker);
    iso_unit(g_iso_attacker)->facing = (int16_t)(iso_unit(g_iso_attacker)->facing + 4);
    iso_redraw();
    iso_play_sfx(8);
    ui_wait_frames(10);
    iso_center_view(g_iso_defender);
    iso_unit(g_iso_attacker)->facing = (int16_t)(iso_unit(g_iso_attacker)->facing - 4);
    g_iso_d5c2 = 1;
    iso_attack();
    g_iso_d5c2 = 0;
    iso_center_view(actor);
    return;

walk:
    if (g_iso_ap == 0)
        return;
    g_iso_ai_count = 0;
    steps = 0;
    g_iso_cell_x = iso_sign((int16_t)(iso_unit(tgt)->x - iso_unit(actor)->x));
    g_iso_cell_y = iso_sign((int16_t)(iso_unit(tgt)->y - iso_unit(actor)->y));
    for (;;) {
        if (steps == 0) {
            static const int8_t sx[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
            static const int8_t sy[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };
            for (k = 0; k < 8; k++)
                if (g_iso_cell_x == sx[k] && g_iso_cell_y == sy[k])
                    dir = (int16_t)(k + 1);
        } else {
            iso_clamp_add(&dir, steps == 1 ? -1 : 2, 1, 8);
        }
        steps++;
        switch (dir) {              /* jump table 4595:AA0C: step and facing */
        case 1: g_iso_cell_x = 0;  g_iso_cell_y = -1; g_iso_facing = 1; break;
        case 2: g_iso_cell_x = 1;  g_iso_cell_y = -1; g_iso_facing = 4; break;
        case 3: g_iso_cell_x = 1;  g_iso_cell_y = 0;  g_iso_facing = 4; break;
        case 4: g_iso_cell_x = 1;  g_iso_cell_y = 1;  g_iso_facing = 4; break;
        case 5: g_iso_cell_x = 0;  g_iso_cell_y = 1;  g_iso_facing = 3; break;
        case 6: g_iso_cell_x = -1; g_iso_cell_y = 1;  g_iso_facing = 2; break;
        case 7: g_iso_cell_x = -1; g_iso_cell_y = 0;  g_iso_facing = 2; break;
        case 8: g_iso_cell_x = -1; g_iso_cell_y = -1; g_iso_facing = 2; break;
        default: break;
        }
        iso_move_step();
        iso_redraw();
        ui_wait_frames(10);
        iso_after_hit();
        if (g_iso_d5b8 != 0)
            goto decide;
        if (steps >= 3)
            return;
    }
}
