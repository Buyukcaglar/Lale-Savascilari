/*
 * iso_spell.c - ISOSYS.L 4595:8269-9667: vecize ("sayings", the spells):
 * parameters, casting, the effect on one target, after-hit reactions.
 * See iso.h.
 *
 * A vecize s = 1..34 has 11 parameter words at 007E + 16h*s (values >= 1000
 * are formulas, iso_eval_param) and its text at 0779 + 25h*s.  The decoded
 * parameters live in D598 (p0) down to D584 (p10):
 *   p0  flags: bit 7 damage (p1, or p2 / p1 by the p3 < p4 test when bit 4),
 *       bit 6 one target only, bit 5 also without a target, bit 4 resisted
 *       when p3 >= p4, bit 2 set unit words, bit 1 add status s for p10 turns
 *   p5/p7, p6/p8  unit word index / value (bit 2)
 *   p9  radius of the area (target cursor, or the caster for vecize 12)
 */
#include "game/iso.h"

#include <string.h>

#include "engine/fatal.h"
#include "engine/intrface.h"
#include "engine/nkontrol.h"
#include "rtl/bc_rand.h"

#define g_iso_vp(k)          DGS16((uint16_t)(0xD598 - 2 * (k)))  /* vecize parameter k = 0..10 */
#define VECIZE_PARAM(s, k)   DGS16((uint16_t)(0x007E + 0x16 * (s) + 2 * (k)))
#define VECIZE_TEXT(s)       DGSTR((uint16_t)(0x0779 + 0x25 * (s)))
#define g_iso_centred        DGS16(0x6326)  /* i16 iso_vecize_effect centred the view */
#define UNIT_W(c, k)         DGS16((uint16_t)(0xA088 + 0x28 * (c) + 2 * (k)))  /* word k-1 of unit c */

/*
 * iso_eval_param - a vecize parameter: v < 1000 is itself; 1001..1019 is a
 * formula of the caster's power P (D5BE), the target's power T (D55C), the
 * target D5E0 and the caster D5DE (r(n) = random(n)):
 *   1001 r(P+1) + r(3)     1008 r(T+6)           1015 r(6) + 8P
 *   1002 T                 1009 caster's side    1016 r(11) + T
 *   1003 P                 1010 to-hit + 1       1017 hp + 25
 *   1004 to-hit - 2, >= 0  1011 dodge + 1        1018 hp + 5
 *   1005 dodge - 2, >= 0   1012 to-hit + 2       1019 r(3)
 *   1006 r(T+9)            1013 r(P+1)
 *   1007 r(P+1) + 1        1014 r(T+11)
 * (to-hit, dodge, hp: of the target).  1000 and > 1019: fatal 66h, then v.
 *
 * Original: IST.EXE ovl4595:8269 (ISOSYS.L), far cdecl (v), jump table
 * 4595:84C3.  random(n) = (long)rand() * n / 8000h, rand() first.
 * Verified: tests/py/test_iso_b.py (every value, rand stream), 2026-11-21.
 */
int16_t iso_eval_param(int16_t v)
{
    int16_t a, b;
    IsoUnit *t = iso_unit(g_iso_target);

    if (v < 1000)
        return v;
    switch ((uint16_t)(v - 1001)) {
    case 0:
        a = bc_random((int16_t)(g_iso_caster_power + 1));
        b = bc_random(3);
        return (int16_t)(a + b);
    case 1:  return g_iso_tgt_power;
    case 2:  return g_iso_caster_power;
    case 3:  return t->to_hit > 2 ? (int16_t)(t->to_hit - 2) : 0;
    case 4:  return t->dodge > 2 ? (int16_t)(t->dodge - 2) : 0;
    case 5:  return bc_random((int16_t)(g_iso_tgt_power + 9));
    case 6:  return (int16_t)(bc_random((int16_t)(g_iso_caster_power + 1)) + 1);
    case 7:  return bc_random((int16_t)(g_iso_tgt_power + 6));
    case 8:  return iso_unit(g_iso_actor)->side;
    case 9:  return (int16_t)(t->to_hit + 1);
    case 10: return (int16_t)(t->dodge + 1);
    case 11: return (int16_t)(t->to_hit + 2);
    case 12: return bc_random((int16_t)(g_iso_caster_power + 1));
    case 13: return bc_random((int16_t)(g_iso_tgt_power + 11));
    case 14:
        a = bc_random(6);
        return (int16_t)(g_iso_caster_power * 8 + a);
    case 15:
        a = bc_random(11);
        return (int16_t)(a + g_iso_tgt_power);
    case 16: return (int16_t)(t->hp + 0x19);
    case 17: return (int16_t)(t->hp + 5);
    case 18: return bc_random(3);
    default:
        o559A_fatal(DGSTR(0x67A4), 0x9A2, NULL, 0x66);
        return v;
    }
}

/* the caster speaks the vecize (text A574) */
static void say_vecize(void)
{
    iso_redraw();
    iso_show_message(DGSTR(0xA574), g_iso_actor);
    iso_speak_vecize(g_iso_spell);
    ui_play_voice(0x21, 8);
}

/* vecize 29 ("line"): hit D5E0 on the cell, random(6) + D5BE damage */
static void line_hit(uint16_t text)
{
    if (g_iso_target <= 0 || g_iso_target == g_iso_actor || iso_has_status(g_iso_target, 0x1F))
        return;
    iso_center_view(g_iso_target);
    g_iso_anim_unit = g_iso_target;
    g_iso_damage = (int16_t)(bc_random(6) + g_iso_caster_power);
    strcpy(DGSTR(0xA510), DGSTR(text));                 /* "" */
    iso_apply_dot_damage(g_iso_damage, g_iso_target);
    iso_vecize_reaction();
    g_iso_d582 = 1;
    iso_redraw();
    ui_wait_frames(10);
    g_iso_sfx = 0;
    if (iso_unit(g_iso_target)->hp < 1)
        iso_set_cell(iso_unit(g_iso_target)->x, iso_unit(g_iso_target)->y, 0);
}

/*
 * iso_cast_vecize - actor D5DE casts vecize D5E2 at D5E0: the parameters
 * are decoded (p0..p10, in order), its text goes to A510.  Unless quiet
 * (D5A2, an on-hit or item vecize):
 *  - vecizes below 12 on a deaf (status 31) or (status 23) target: the
 *    caster says it, the voice plays, and nothing happens (status 23 also
 *    waits 10 frames); the view is centred on the target;
 *  - a one-target vecize (bit 6) without a target and without bit 5 does
 *    nothing at all;
 *  - else the caster says it and pays D5BC vecize points (not enough:
 *    nothing happens).
 * Then: bit 6 -> iso_vecize_effect on D5E0; vecize 29 hits every unit on
 * the line from the cursor back to the caster (random(6) + D5BE each);
 * vecize 34 takes the vecize points of every other living unit whose
 * random(word 10 / 50 + 6) < D5BE (when the target's template word 10 > 0)
 * ("......!!!"); any other: in the battle window every unit in the square
 * of radius p9 around the cursor (vecize 12: around the caster) except the
 * caster and deaf units gets the effect (p3, p4 re-decoded, D55C = its
 * power), outside it (camp, no chewing gum) the party units 25..28.
 *
 * Original: IST.EXE ovl4595:84E9 (ISOSYS.L), far cdecl.  The deaf test of
 * the camp loop has its result ignored; bit 6 is tested twice (no effect).
 * The line's slope divides by the x (or y) distance (idiv word [bp-4] /
 * [bp-6], 3 bytes): with the cursor on the caster that is a division by
 * zero, which the game's INT 0 handler answers with AX = FFFFh (slope -1,
 * nk_idiv_s16); the loop then does not run (distance 0).
 * Verified: tests/py/test_iso_b.py (every vecize, random battles), 2026-11-21.
 */
void iso_cast_vecize(void)
{
    int16_t k, cx, cy, dx, dy, ax, ay, slope, step, i, acc, c, r;

    g_iso_centred = 0;
    g_iso_d59c = 0;
    for (k = 0; k <= 10; k++)
        g_iso_vp(k) = iso_eval_param(VECIZE_PARAM(g_iso_spell, k));
    strcpy(DGSTR(0xA510), VECIZE_TEXT(g_iso_spell));
    if (g_iso_quiet == 0) {
        g_iso_d582 = 1;
        if (g_iso_spell < 12 && iso_has_status(g_iso_target, 0x1F)) {
            say_vecize();
            iso_center_view(g_iso_target);
            return;
        }
        if (g_iso_spell < 12 && iso_has_status(g_iso_target, 0x17)) {
            say_vecize();
            ui_wait_frames(10);
            iso_center_view(g_iso_target);
            return;
        }
        if (iso_bit_test(6, (uint16_t)g_iso_vp(0)) && g_iso_target == 0 &&
            !iso_bit_test(5, (uint16_t)g_iso_vp(0)))
            return;
        say_vecize();
        if (DG32(0xE12B) != 0)
            ctl_free_checked(0xE12B, 0x9D8, DGSTR(0x67B4));
        if (iso_unit(g_iso_actor)->spell < g_iso_d5bc)
            return;
        iso_unit(g_iso_actor)->spell = (int16_t)(iso_unit(g_iso_actor)->spell - g_iso_d5bc);
    }
    if (iso_bit_test(6, (uint16_t)g_iso_vp(0))) {
        iso_vecize_effect();
        return;
    }
    if (g_iso_spell != 0x1D && g_iso_spell != 0x22) {
        if (iso_in_battle_window()) {
            if (g_iso_spell == 12) {
                cx = iso_unit(g_iso_actor)->x;
                cy = iso_unit(g_iso_actor)->y;
            } else {
                cx = g_iso_cursor_x;
                cy = g_iso_cursor_y;
            }
            for (dy = (int16_t)(g_iso_vp(9) * -1); dy <= g_iso_vp(9); dy++)
                for (dx = (int16_t)(g_iso_vp(9) * -1); dx <= g_iso_vp(9); dx++) {
                    int16_t x = (int16_t)(dx + cx), y = (int16_t)(dy + cy);

                    g_iso_target = g_iso_occ(x, y);
                    if (x < 0 || x > 0x1B || y < 0 || y > 0x1B)
                        g_iso_target = 0;
                    if (g_iso_target <= 0 || g_iso_target == g_iso_actor ||
                        iso_has_status(g_iso_target, 0x1F))
                        continue;
                    g_iso_anim_kind = 0;
                    g_iso_vp(3) = iso_eval_param(VECIZE_PARAM(g_iso_spell, 3));
                    g_iso_vp(4) = iso_eval_param(VECIZE_PARAM(g_iso_spell, 4));
                    strcpy(DGSTR(0xA510), VECIZE_TEXT(g_iso_spell));
                    g_iso_tgt_power = (int16_t)(iso_utmpl(g_iso_target)->w[10] / 50);
                    if (iso_unit(g_iso_target)->x < g_iso_view_x ||
                        iso_unit(g_iso_target)->y < g_iso_view_y ||
                        iso_unit(g_iso_target)->x > g_iso_view_x + 7 ||
                        iso_unit(g_iso_target)->y > g_iso_view_y + 7)
                        iso_center_view(g_iso_target);
                    iso_vecize_effect();
                    iso_redraw();
                    if (iso_unit(g_iso_target)->hp < 1)
                        iso_set_cell(iso_unit(g_iso_target)->x, iso_unit(g_iso_target)->y, 0);
                }
            g_iso_anim_kind = 1;
            g_iso_damage = 0;
            g_iso_d59c = 0;
        } else if (g_iso_gum == 0) {
            for (g_iso_target = 0x19; g_iso_target <= 0x1C; g_iso_target++) {
                (void)iso_has_status(g_iso_target, 0x1F);       /* result unused */
                iso_vecize_effect();
            }
        }
        return;
    }
    if (g_iso_spell == 0x1D) {
        ax = iso_unit(g_iso_actor)->x;
        ay = iso_unit(g_iso_actor)->y;
        dx = (int16_t)(g_iso_cursor_x - ax);
        dy = (int16_t)(g_iso_cursor_y - ay);
        /* abs() as cwd/xor/sub: abs(-32768) stays negative */
        if ((int16_t)(dx < 0 ? -dx : dx) >= (int16_t)(dy < 0 ? -dy : dy)) {
            slope = nk_idiv_s16((int16_t)(dy * 100), dx, NULL);   /* 4595:8AA0 */
            step = dx > 0 ? 1 : -1;
            i = dx;
            acc = (int16_t)(dy * 100);
            while (i != 0) {
                g_iso_target = g_iso_occ(ax + i, acc / 100 + ay);
                line_hit(0x67C4);
                i = (int16_t)(i - step);
                acc = (int16_t)(acc - (int16_t)(slope * step));
            }
        } else {
            slope = nk_idiv_s16((int16_t)(dx * 100), dy, NULL);   /* 4595:8BF3 */
            step = dy > 0 ? 1 : -1;
            acc = (int16_t)(dx * 100);
            i = dy;
            while (i != 0) {
                g_iso_target = g_iso_occ(acc / 100 + ax, ay + i);
                line_hit(0x67C5);
                acc = (int16_t)(acc - (int16_t)(slope * step));
                i = (int16_t)(i - step);
            }
        }
        return;
    }
    /* vecize 34 */
    for (c = 1; c <= 0x1C; c++) {
        if (c == g_iso_actor || iso_unit(c)->hp <= 0)
            continue;
        r = bc_random((int16_t)(iso_utmpl(c)->w[10] / 50 + 6));
        if (r < g_iso_caster_power && !iso_has_status(c, 0x1F) &&
            iso_utmpl(g_iso_target)->w[10] > 0) {
            g_iso_anim_unit = c;
            iso_unit(c)->spell = 0;
            g_iso_anim_kind = 5;
            iso_center_view(c);
            strcpy(DGSTR(0xA510), DGSTR(0x67C6));        /* "......!!!" */
            g_iso_d582 = 1;
            iso_redraw();
            ui_wait_frames(10);
            g_iso_sfx = 0;
        }
    }
}

/* voice (answer) number of vecize s's effect, jump table 4595:9452 */
static const uint8_t effect_voice[32] = {
    0,
    0x28, 0x28, 0x0B, 0x0C, 0x0D, 0x0E, 0x28, 0x00, 0x0F, 0x10,
    0x11, 0x28, 0x29, 0x12, 0x13, 0x00, 0x00, 0x14, 0x15, 0x16,
    0x17, 0x18, 0x00, 0x19, 0x00, 0x2A, 0x28, 0x1A, 0x28, 0x2B,
    0x1B,
};

/*
 * iso_vecize_effect - vecize D5E2 on unit D5E0 (D576 = it; the view is
 * centred on the first target, 6326):
 *  - damage (p0 bit 7): D570 = p1, or with bit 4 p1 when p3 < p4 and the
 *    target's word 10 >= 0, else p2 when non-zero, else p1 when non-zero:
 *    iso_vecize_reaction, then iso_apply_dot_damage; no damage: the miss
 *    animation (D574 = 6) unless word 10 < 0.  Vecize 30 then writes -5
 *    into 9112 + 5Ch*c and 9ACA + 2Eh*c (party record +0C and template
 *    word 1 of member c - 24);
 *  - otherwise resisted (bit 4 and p3 >= p4, or word 10 < 0): D574 = 6
 *    (not when word 10 < 0); else
 *      bit 2: unit word p5 = p7 (and p6 = p8 when p6);
 *      vecize 16: status slots and stun cleared, a charmed unit (side 1
 *        below 25 / side 2 above) changes side, "ohh!";
 *      vecize 17: random(7): 2 -> random(D5BE+1) + random(3) damage;
 *        3 -> the target joins the caster's side ("geliyorum
 *        babacığım!", charmer +10 = caster); 4 -> to-hit - 2 ("ben gariban
 *        bir antenim!"); else nothing (D574 = 6); (a template word 10 <= 0
 *        always gives nothing);
 *    then bit 1 adds status D5E2 for p10, D574 = 5, sound 8 and the voice
 *    of the vecize (631A, table above).
 *
 * Original: IST.EXE ovl4595:8E4F (ISOSYS.L), far cdecl.  Bit 7 is tested
 * twice when clear (no effect).
 * Verified: tests/py/test_iso_b.py (every vecize, random battles), 2026-11-21.
 */
void iso_vecize_effect(void)
{
    int16_t k, r, a, b, w10;
    IsoUnit *u;

    g_iso_d582 = 1;
    g_iso_anim_unit = g_iso_target;
    if (g_iso_centred == 0) {
        iso_center_view(g_iso_target);
        g_iso_centred = 1;
    }
    u = iso_unit(g_iso_target);
    if (iso_bit_test(7, (uint16_t)g_iso_vp(0))) {
        w10 = iso_utmpl(g_iso_target)->w[10];
        if (iso_bit_test(4, (uint16_t)g_iso_vp(0)) && g_iso_vp(3) < g_iso_vp(4) && w10 >= 0)
            g_iso_damage = g_iso_vp(1);
        else if (iso_bit_test(4, (uint16_t)g_iso_vp(0)) && g_iso_vp(2) != 0)
            g_iso_damage = g_iso_vp(2);
        else if (g_iso_vp(1) != 0)
            g_iso_damage = g_iso_vp(1);
        else {
            if (iso_utmpl(g_iso_target)->w[10] >= 0)
                g_iso_anim_kind = 6;
            goto vecize30;
        }
        iso_vecize_reaction();
        iso_apply_dot_damage(g_iso_damage, g_iso_target);
        g_iso_d582 = 1;
vecize30:
        if (g_iso_spell == 0x1E) {
            DGS16((uint16_t)(0x9112 + 0x5C * g_iso_target)) = -5;
            DGS16((uint16_t)(0x9ACA + 0x2E * g_iso_target)) = -5;
        }
        return;
    }
    if ((iso_bit_test(4, (uint16_t)g_iso_vp(0)) && g_iso_vp(3) >= g_iso_vp(4)) ||
        iso_utmpl(g_iso_target)->w[10] < 0) {
        if (iso_utmpl(g_iso_target)->w[10] >= 0)
            g_iso_anim_kind = 6;
        return;
    }
    if (iso_bit_test(2, (uint16_t)g_iso_vp(0))) {
        UNIT_W(g_iso_target, g_iso_vp(5)) = g_iso_vp(7);
        if (g_iso_vp(6) != 0)
            UNIT_W(g_iso_target, g_iso_vp(6)) = g_iso_vp(8);
    } else if (g_iso_spell == 0x10) {
        for (k = 0; k < 5; k++)
            u->status[k] = 0;
        u->stun = 0;
        if ((g_iso_target < 0x19 && u->side == 1) || (g_iso_target > 0x18 && u->side == 2))
            iso_clamp_add(&u->side, 1, 1, 2);
        strcpy(DGSTR(0xA510), DGSTR(0x67D0));             /* "ohh!" */
        g_iso_voice = 0x14;
    } else if (g_iso_spell == 0x11) {
        g_iso_anim_unit = g_iso_target;
        iso_center_view(g_iso_target);
        r = bc_random(7);
        if (r == 2 && iso_utmpl(g_iso_target)->w[10] > 0) {
            a = bc_random((int16_t)(g_iso_caster_power + 1));
            b = bc_random(3);
            g_iso_damage = (int16_t)(a + b);
            strcpy(DGSTR(0xA510), DGSTR(0x67D5));         /* "" */
            g_iso_sfx = 0;
            iso_apply_dot_damage(g_iso_damage, g_iso_target);
            iso_vecize_reaction();
            g_iso_d582 = 1;
        } else if (r == 3 && iso_utmpl(g_iso_target)->w[10] > 0) {
            u->side = iso_unit(g_iso_actor)->side;
            strcpy(DGSTR(0xA510), DGSTR(0x67D6));         /* "geliyorum babacığım!" */
            g_iso_voice = 0x0F;
            g_iso_anim_kind = 5;
            u->f10 = g_iso_actor;
        } else if (r == 4 && iso_utmpl(g_iso_target)->w[10] > 0) {
            u->to_hit = u->to_hit > 2 ? (int16_t)(u->to_hit - 2) : 0;
            strcpy(DGSTR(0xA510), DGSTR(0x67EB));         /* "ben gariban bir antenim!" */
            g_iso_voice = 0x0B;
            g_iso_anim_kind = 5;
        } else {
            if (iso_utmpl(g_iso_target)->w[10] >= 0) {
                g_iso_damage = 0;
                g_iso_d59c = 0;
                g_iso_anim_kind = 6;
                g_iso_sfx = 0;
                strcpy(DGSTR(0xA510), DGSTR(0x6804));     /* "" */
            }
            goto status;
        }
        iso_redraw();
        ui_wait_frames(10);
    }
status:
    if (iso_bit_test(1, (uint16_t)g_iso_vp(0)))
        iso_add_status(g_iso_target, g_iso_spell, g_iso_vp(10));
    g_iso_anim_kind = 5;
    g_iso_sfx = 8;
    g_iso_voice = (uint16_t)(g_iso_spell - 1) < 0x1F ? effect_voice[g_iso_spell] : 0;
}

/*
 * iso_after_hit - after an attack: a draining attacker (D56A, not stunned)
 * takes the damage dealt (D59C) from the living defender once more ("bi
 * korum bir de yer kor!", voice 2Eh); a poisonous defender (D572, not
 * stunned, alive) deals the damage (the drained amount when there was
 * one) back to the attacker (voice 2Dh).  A unit killed leaves its cell
 * and the actor's AP go to 0; the actor's turn animation follows.
 *
 * Original: IST.EXE ovl4595:9490 (ISOSYS.L), far cdecl.
 * Verified: tests/py/test_iso_b.py, 2026-11-21.
 */
void iso_after_hit(void)
{
    int16_t drained = 0, def;

    if (g_iso_d56a == 1 && iso_unit(g_iso_attacker)->stun == 0 &&
        iso_unit(g_iso_defender)->hp > 0) {
        drained = g_iso_d59c;
        iso_apply_dot_damage(g_iso_d59c, g_iso_defender);
        iso_redraw();
        g_iso_voice = 0x2E;
        iso_show_message(DGSTR(0x6805), g_iso_attacker);  /* "bi korum bir de yer kor!" */
        g_iso_d582 = 1;
        iso_play_sfx(g_iso_sfx);
        ui_wait_frames(10);
        if (iso_unit(g_iso_defender)->hp < 1) {
            iso_set_cell(iso_unit(g_iso_defender)->x, iso_unit(g_iso_defender)->y, 0);
            g_iso_ap = 0;
        }
        g_iso_anim_unit = g_iso_actor;
        g_iso_anim_kind = 1;
        iso_redraw();
        ui_wait_frames(10);
        g_iso_d56a = 0;
    }
    if (g_iso_d572 == 1 && iso_unit(g_iso_defender)->stun == 0 &&
        iso_unit(g_iso_defender)->hp > 0) {
        def = g_iso_defender;
        if (drained > 0)
            g_iso_d59c = drained;
        iso_apply_dot_damage(g_iso_d59c, g_iso_attacker);
        iso_redraw();
        g_iso_voice = 0x2D;
        iso_show_message(DGSTR(0x681E), def);
        g_iso_d582 = 1;
        iso_play_sfx(g_iso_sfx);
        ui_wait_frames(10);
        if (iso_unit(g_iso_attacker)->hp < 1) {
            iso_set_cell(iso_unit(g_iso_attacker)->x, iso_unit(g_iso_attacker)->y, 0);
            g_iso_ap = 0;
        }
        g_iso_anim_unit = g_iso_actor;
        g_iso_anim_kind = 1;
        iso_redraw();
        ui_wait_frames(10);
        g_iso_d572 = 0;
    }
}
