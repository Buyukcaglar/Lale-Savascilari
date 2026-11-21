/*
 * iso_turn.c - ISOSYS.L 4595:16EB-2720: speech lines, the talking-face
 * animation, effect damage, the turn order (initiative), status slots,
 * battle sounds and the vecize voices.  See iso.h.
 */
#include "game/iso.h"

#include <stdio.h>
#include <string.h>

#include "engine/blit386.h"
#include "engine/fatal.h"
#include "engine/grtext.h"
#include "engine/intrface.h"
#include "engine/memman.h"
#include "engine/ngraphs.h"
#include "engine/nkontrol.h"
#include "engine/oses.h"
#include "engine/systems.h"
#include "game/chargen.h"
#include "rtl/bc_farheap.h"
#include "rtl/bc_rand.h"

#if __has_include("game/s3d.h")
#include "game/s3d.h"
#else
/* provided by 3dsys (WP16) */
int16_t s3d_advance_clock(int16_t minutes);                                /* 303F:39FD */
#endif

/*
 * iso_show_message - say text s for unit c.  Nothing while D5A2 is set or
 * s is empty.  With D5BA (a chewing gum is being eaten) s becomes "bu
 * ciklet " + flavour D5B0 and the voice 50 + D5B0 is queued (631A).  The
 * speaker's voice id 6320 = c + 40 for the party, else its group's monster
 * type.  In the battle window (9672 == 9792): the text is drawn twice
 * (shadow) above the unit on the work screen, which is then shown, and a
 * queued voice is spoken.  Elsewhere: s is cut to 130 px, "..." appended
 * and shown in a text box, the queued voice spoken (gum only), and the
 * time noted in D5D4.
 *
 * Original: IST.EXE ovl4595:16EB (ISOSYS.L), far cdecl (char far *s, c).
 * The flavour names are F_SCOPY'd (54h bytes, 633F) to the stack first;
 * the port reads them in place.
 * Verified: tests/py/test_iso.py, 2026-11-21.
 */
void iso_show_message(char *s, int16_t c)
{
    int16_t x, y, half, tx, n;

    if (g_iso_quiet != 0 || s[0] == 0)
        return;
    if (g_iso_gum != 0) {
        strcpy(s, DGSTR(0x6528));                              /* "bu ciklet " */
        strcat(s, DGSTR((uint16_t)(0x633F + 12 * g_iso_gum_flavour)));
        g_iso_voice = (int16_t)(g_iso_gum_flavour + 0x32);
    }
    x = iso_unit(c)->x;
    y = iso_unit(c)->y;
    half = (int16_t)(grt_text_width(s, 1) / 2);
    g_iso_speaker = c > 0x18 ? (int16_t)(c + 0x28) : g_iso_group_type(iso_unit(c)->tmpl - 1);
    if (iso_in_battle_window()) {
        tx = (int16_t)((x - g_iso_view_x) * 0x1E + (y - g_iso_view_y) * 0x0E);
        if ((int16_t)(tx + half + 0x21) < 0x140)
            tx = (int16_t)(tx + 0x21 - half - 1);
        else
            tx = (int16_t)(0x140 - half * 2 - 1);
        iso_draw_text(1, tx, (int16_t)((y - g_iso_view_y) * 0x0E + 7), s, 0);
        iso_draw_text(1, tx, (int16_t)((y - g_iso_view_y) * 0x0E + 6), s, DG8(0x9665));
        iso_show_picture(6);
        if (g_iso_voice != 0) {
            iso_speak_vecize(g_iso_voice);
            g_iso_voice = 0;
        }
        return;
    }
    /* ovl4595:18A7 - the first pass clears the terminator itself */
    n = (int16_t)strlen(s);
    while (grt_text_width(s, 1) > 0x82) {
        s[n] = 0;
        n--;
    }
    strcat(s, DGSTR(0x6533));                                  /* "..." */
    iso_draw_textbox(s, 0xD9, DG8(0x9664));
    if (g_iso_gum != 0) {
        iso_speak_vecize(g_iso_voice);
        g_iso_voice = 0;
    }
    g_iso_msg_time = (int16_t)(uint16_t)g_clock_ms;
}

/*
 * iso_face_anim_tick - idle callback of the UI (964C = 1694:007A) while a
 * voice plays: every 150 clock units (1.5 s) draw a mouth picture of the
 * talking face: 32x32 (or 30x30) from slot 46h + type at frame 0 or, one
 * time in two, (random(2) + 3) * 32 - so random(2) is called once or twice.
 * The record (far pointer arg = 9660) holds type 1..4, screen x, y, w, h,
 * source x, source y; another type is fatal ("İç hata:", the sprintf'd
 * "tip=%d" is not shown).
 *
 * Original: IST.EXE ovl4595:1935 (ISOSYS.L), far cdecl (int far *rec).
 * Verified: tests/py/test_iso.py, 2026-11-21.
 */
void iso_face_anim_tick(uint32_t arg)
{
    const uint8_t *p = bc_far_ptr(arg);
    int16_t r[7];
    char buf[0x78];
    int16_t i, sx;

    for (i = 0; i < 7; i++)
        r[i] = (int16_t)(p[2 * i] | p[2 * i + 1] << 8);
    if (r[0] < 1 || r[0] > 4) {
        sprintf(buf, DGSTR(0x6537), r[0]);                     /* "tip=%d" */
        o559A_fatal(DGSTR(0x653E), 0x2C8, DGSTR(0x654E), -1);
    }
    if ((uint16_t)((uint16_t)(uint16_t)g_clock_ms - DG16(0xBBC4)) < 0x96)
        return;
    DG16(0xBBC4) = (uint16_t)(uint16_t)g_clock_ms;
    if (gr_cursor_in_rect(r[1], r[2], 0x20, 0x20))
        gr_cursor_hide();
    sx = bc_random(2) != 0 ? 0 : (int16_t)((bc_random(2) + 3) << 5);
    sys_blit((uint16_t)(sx + r[5]), (uint16_t)r[6], (uint16_t)r[3], (uint16_t)r[4],
             (uint16_t)r[1], (uint16_t)r[2], (uint8_t)(r[0] + 0x46), 0, sys_rect_copy);
    gr_cursor_set_shape((uint8_t)g_cursor_shape);
}

/*
 * iso_apply_dot_damage - effect damage d to unit c (only if d > 0 or D59C
 * > 0, and only if c's template has +14h > 0): mark it hit, HP -= d,
 * animation "hit" (D566 = 9, D574 = 4) centred on it; if it dies,
 * animation "death" (D566 = 2, D574 = 2) and D56C = c.
 *
 * Original: IST.EXE ovl4595:1ACE (ISOSYS.L), far cdecl (d, c).
 * Verified: tests/py/test_iso.py, 2026-11-21.
 */
void iso_apply_dot_damage(int16_t d, int16_t c)
{
    if (d <= 0 && g_iso_d59c <= 0)
        return;
    if (iso_utmpl(c)->w[0x14 / 2] <= 0)
        return;
    iso_unit(c)->hit = 1;
    iso_unit(c)->hp = (int16_t)(iso_unit(c)->hp - d);
    g_iso_sfx = 9;
    g_iso_anim_unit = c;
    g_iso_anim_kind = 4;
    iso_center_view(c);
    if (iso_unit(c)->hp < 1) {
        g_iso_sfx = 2;
        g_iso_anim_unit = c;
        g_iso_anim_kind = 2;
        g_iso_defender = c;
    }
}

/*
 * iso_next_turn - check for the end of the battle, then find the next
 * actor.  Side flags 2634/2636 = some unit of side 1/2 alive; if a side is
 * gone: g_iso_result = 1 (no enemy left) or 2, cursor hidden, return -1.
 * Otherwise walk the initiative order (6330, rolled on the first call:
 * 6322): past 28 a new round starts (D54A++, game clock + 1 minute, D550 =
 * D54E = 0, re-roll) - and the index is then incremented once more, so the
 * first unit of the new order is skipped (quirk kept).  Dead units are
 * skipped; stunned ones lose one stun turn and are skipped.  Then AP =
 * template +0Ch (status 15h: AP-3, at least 3; status 1Ch: doubled), the
 * party member 2898 = actor - 24, the view scrolls from its centre to the
 * actor, the party's battle cry (random(4) + 19h female... 1Dh) or the
 * pending sound plays, and the screen is redrawn.  Returns 0.
 *
 * Original: IST.EXE ovl4595:1B6D (ISOSYS.L), far cdecl.
 * Verified: tests/py/test_iso.py (turn sequences, rand stream), 2026-11-21.
 */
int16_t iso_next_turn(void)
{
    int16_t c, old_x, old_y, a, b;

    DG16(0x6324) = 1;
    g_iso_alive(1) = 0;
    g_iso_alive(2) = 0;
    for (c = 1; c <= ISO_UNITS; c++)
        if (iso_unit(c)->hp > 0)
            g_iso_alive(iso_unit(c)->side) = 1;
    if (g_iso_alive(1) == 0 || g_iso_alive(2) == 0) {
        g_iso_result = g_iso_alive(2) == 0 ? 1 : 2;
        gr_cursor_hide();
        return -1;
    }
    iso_unit(g_iso_actor)->hit = 0;
    if (g_iso_rolled == 0) {
        g_iso_rolled = 1;
        iso_roll_initiative();
    }
    for (;;) {
        g_iso_turn++;
        if (g_iso_turn > ISO_UNITS) {
            g_iso_turn = 1;
            g_iso_round++;
            s3d_advance_clock(1);
            g_iso_d550 = 0;
            g_iso_d54e = 0;
            iso_roll_initiative();     /* resets 6330 to 0; the loop adds 1 */
            continue;
        }
        g_iso_actor = g_iso_order(g_iso_turn);
        if (iso_unit(g_iso_actor)->hp <= 0)
            continue;
        if (iso_unit(g_iso_actor)->stun > 0) {
            iso_unit(g_iso_actor)->stun--;
            continue;
        }
        break;
    }
    g_iso_anim_unit = g_iso_actor;
    g_iso_anim_kind = 1;
    g_iso_d55a = 0;
    g_iso_ap = iso_utmpl(g_iso_actor)->w[0x0C / 2];
    if (iso_has_status(g_iso_actor, 0x15))
        g_iso_ap = (int16_t)(g_iso_ap - 3) < 3 ? 3 : (int16_t)(g_iso_ap - 3);
    if (iso_has_status(g_iso_actor, 0x1C))
        g_iso_ap = (int16_t)(g_iso_ap * 2);
    if ((int16_t)(g_iso_actor - 0x18) > 0)
        g_party_cur = (int16_t)(g_iso_actor - 0x18);
    g_iso_cursor_on = 1;
    old_x = g_iso_cursor_x;
    g_iso_cursor_x = iso_unit(g_iso_actor)->x;
    old_y = g_iso_cursor_y;
    g_iso_cursor_y = iso_unit(g_iso_actor)->y;
    if (g_iso_view_x != -0x138 && g_iso_view_y != -0x138) {
        a = (int16_t)(g_iso_view_x + 3) < 0x14 ? (int16_t)(g_iso_view_x + 3) : 0x14;
        b = (int16_t)(g_iso_view_y + 3) < 0x14 ? (int16_t)(g_iso_view_y + 3) : 0x14;
        iso_scroll_view_to(g_iso_cursor_x, g_iso_cursor_y, a, b);
    }
    iso_center_view(g_iso_actor);
    ui_wait_voice();
    if (iso_unit(g_iso_actor)->side == 1) {
        if (g_iso_actor > 0x18)
            g_iso_sfx = g_party_stats[g_party_cur].gender == 1
                            ? (int16_t)(bc_random(4) + 0x19)
                            : (int16_t)(bc_random(4) + 0x1D);
        iso_play_sfx(g_iso_sfx);
    }
    iso_redraw();
    ui_wait_frames(10);
    g_iso_cursor_on = 0;
    g_iso_cursor_x = old_x;
    g_iso_cursor_y = old_y;
    ctl_flush_keys();
    return 0;
}

/*
 * iso_roll_initiative - new turn order: 6330 = D54E = D550 = 0; for every
 * unit slot c = 1..28 (dead or empty ones too) order[c] = c and init[c] =
 * max(0, random(6) + T.+0Eh + 10 - T.+08h) with Borland's max() macro: the
 * expression is evaluated again (a second random(6)) when the first value
 * is not negative.  Then a bubble sort into ASCENDING init (swap when
 * init[c+1] < init[c]), repeated until no swap.
 *
 * Original: IST.EXE ovl4595:1E75 (ISOSYS.L), far cdecl.
 * Verified: tests/py/test_iso.py (random states, rand stream), 2026-11-21.
 */
void iso_roll_initiative(void)
{
    int16_t c, swapped, t;

    g_iso_turn = 0;
    g_iso_d54e = 0;
    g_iso_d550 = 0;
    for (c = 1; c <= ISO_UNITS; c++) {
        IsoTmpl *T;
        g_iso_order(c) = c;
        T = iso_utmpl(c);
        if ((int16_t)(bc_random(6) + T->w[0x0E / 2] + 10 - T->w[0x08 / 2]) < 0)
            g_iso_init(c) = 0;
        else
            g_iso_init(c) = (int16_t)(bc_random(6) + iso_utmpl(c)->w[0x0E / 2] + 10 -
                                      iso_utmpl(c)->w[0x08 / 2]);
    }
    do {
        swapped = 0;
        for (c = 1; c <= 0x1B; c++)
            if (g_iso_init(c + 1) < g_iso_init(c)) {
                t = g_iso_init(c);
                g_iso_init(c) = g_iso_init(c + 1);
                g_iso_init(c + 1) = t;
                t = g_iso_order(c);
                g_iso_order(c) = g_iso_order(c + 1);
                g_iso_order(c + 1) = t;
                swapped = 1;
            }
    } while (swapped);
}

/*
 * iso_add_status - give unit c status `id` for `dur` (stored as
 * dur*60*100 + id, 16-bit).  Nothing if it already has it.  Else the first
 * free slot; with all five used the slots are shifted down by one word
 * starting AT +14h: dodge (+14h) = slot 1, slot 1 = slot 2, slot 2 = slot
 * 3, slot 3 = slot 4, and slot 5 is overwritten (slot 4 stays twice) -
 * original bug, kept.
 *
 * Original: IST.EXE ovl4595:1FFC (ISOSYS.L), far cdecl (c, id, dur).
 * Verified: tests/py/test_iso.py (random inputs), 2026-11-21.
 */
void iso_add_status(int16_t c, int16_t id, int16_t dur)
{
    int16_t k, slot;
    int16_t *w = (int16_t *)iso_unit(c);          /* +16h + 2(k-1) = w[0Ah + k] */
    dur = (int16_t)(dur * 0x3C);
    for (k = 1; k <= 5; k++)
        if ((uint16_t)w[0x0A + k] % 100 == (uint16_t)id)
            return;
    for (k = 1; k <= 5; k++)
        if (w[0x0A + k] == 0)
            break;
    if (k == 6) {
        for (k = 1; k <= 4; k++)
            w[0x09 + k] = w[0x0A + k];
        slot = 5;
    } else {
        slot = k;
    }
    w[0x0A + slot] = (int16_t)(id + dur * 100);
}

/*
 * iso_has_status - 1 if one of unit c's five status slots holds `id`
 * (slot % 100, unsigned), else 0.
 *
 * Original: IST.EXE ovl4595:20E0 (ISOSYS.L), far cdecl (c, id).
 * Verified: tests/py/test_iso.py (random inputs), 2026-11-21.
 */
int16_t iso_has_status(int16_t c, int16_t id)
{
    int16_t k;
    for (k = 1; k <= 5; k++)
        if ((uint16_t)iso_unit(c)->status[k - 1] % 100 == (uint16_t)id)
            return 1;
    return 0;
}

/*
 * iso_play_sfx - play battle sound n (sound table 22DB + 11h*n) in the
 * battle window, when sound is on (BB38 and 73D0).  Sounds with flag 20h
 * are voiced: their rate is scaled by the speaker's voice class (the
 * defender for n = 2, else the actor: unit + 40 for the party, else the
 * group's monster type); for n = 2 a few monster types (14..17, 30, 31)
 * use sound 0Dh instead.  D566 = 0 afterwards (not outside the battle
 * window).
 *
 * Original: IST.EXE ovl4595:214E (ISOSYS.L), far cdecl (n).
 * Verified: tests/py/test_iso.py, 2026-11-21.
 */
void iso_play_sfx(int16_t n)
{
    int16_t who, c;
    if (!iso_in_battle_window())
        return;
    if (n != 0 && DG8(0xBB38) != 0 && g_sound_on != 0) {
        if (g_snd_table_flags(n) & 0x20) {
            c = n == 2 ? g_iso_defender : g_iso_actor;
            who = c > 0x18 ? (int16_t)(c + 0x28) : g_iso_group_type(iso_unit(c)->tmpl - 1);
            if (n == 2)
                switch (who) {             /* jump table 4595:228F */
                case 14: case 15: case 16: case 17: case 30: case 31:
                    n = 0x0D;
                    break;
                default:
                    break;
                }
            ui_load_voice(n, iso_sfx_rate(who, g_snd_rate(n)), 0x1F4, 0);
        } else {
            ui_load_voice(n, g_snd_rate(n), 0x1F4, 0);
        }
    }
    g_iso_sfx = 0;
}

/*
 * iso_start_face_anim - set up the talking-face animation: a 14-byte
 * record (mm_farmalloc) at 9660 with the seven words, and the idle
 * callback 964C = 1694:007A (iso_face_anim_tick).
 *
 * Original: IST.EXE ovl4595:22B3 (ISOSYS.L), far cdecl (7 words).
 * Verified: tests/py/test_iso.py (through iso_speak_vecize), 2026-11-21.
 */
void iso_start_face_anim(int16_t type, int16_t x, int16_t y, int16_t w, int16_t h,
                         int16_t sx, int16_t sy)
{
    int16_t v[7];
    uint8_t *p = mm_farmalloc(0x0E);
    int i;
    g_ui_idle_arg = bc_far_addr(p);
    ui_far_set(UI_CB_IDLE, 0x1694, 0x007A);
    v[0] = type; v[1] = x; v[2] = y; v[3] = w; v[4] = h; v[5] = sx; v[6] = sy;
    for (i = 0; i < 7; i++) {
        p[2 * i] = (uint8_t)v[i];
        p[2 * i + 1] = (uint8_t)((uint16_t)v[i] >> 8);
    }
}

/*
 * iso_speak_vecize - speak voice n of the speaker 6320 (a vecize, or with
 * 631A set an answer, see iso_voice_filename) with the talking face; with
 * 631A = 0 and D5BA = 1 nothing.  The sample is loaded into voice slot
 * 21h (stored reversed, flags 2, unless 631A is not 1Dh, below 2Ch and
 * D5BC > D556: then 1), played at sfx_rate(speaker, 44100) and waited for;
 * with sound off the wait is 20 frames.  Load errors are fatal.  6320 =
 * FFh afterwards.
 *
 * Original: IST.EXE ovl4595:235B (ISOSYS.L), far cdecl (n).
 * Verified: tests/py/test_iso.py, 2026-11-21.
 */
void iso_speak_vecize(int16_t n)
{
    char name[0x0E];
    uint8_t started = 0, flags = 2, err;
    uint16_t rate;

    if (g_iso_voice == 0 && g_iso_gum == 1)
        return;
    iso_voice_filename(g_iso_speaker, name, n);
    if (g_iso_voice != 0x1D && g_iso_voice < 0x2C && g_iso_d5bc > g_iso_d556)
        flags = 1;
    rate = iso_sfx_rate(g_iso_speaker, 0xAC44);
    ui_wait_voice();
    if (DG32(0xE12B) != 0)
        ctl_free_checked(0xE12B, 0x39F, DGSTR(0x6575));
    if (g_sound_on != 0 && DG8(0xBB38) != 0) {
        err = (uint8_t)oses_load_sample(1, name, 0x21, flags, 0, 0);
        if (err != 0)
            o559A_fatal(DGSTR(0x6585), 0x3A3,
                        err == 1 ? DGSTR(0x6595) : err == 2 ? DGSTR(0x65A9) : DGSTR(0x65CF),
                        -1);
    }
    if (g_sound_on == 0 || DG8(0xBB38) == 0)
        ui_wait_frames(0x14);
    else
        ui_load_voice(0x21, rate, 0x1F4, 0);
    if (iso_in_battle_window()) {
        if (g_iso_speaker > 0x28 && g_iso_voice != 0x1A) {
            iso_start_face_anim((int16_t)(g_iso_speaker - 0x40), 0x19, 0xA5, 0x20, 0x20, 0, 0);
            started = 1;
        }
    } else if (g_iso_voice != 0) {
        if (g_iso_voice != 0x1A) {
            iso_start_face_anim((int16_t)(g_iso_target - 0x18),
                                (int16_t)((g_iso_target - 0x19) * 0x23 + 0x9D), 0x15, 0x1E,
                                0x1E, 1, 1);
            started = 1;
        }
    } else if (g_iso_speaker > 0x28) {
        iso_start_face_anim((int16_t)(g_iso_speaker - 0x40), 7, 0x15, 0x20, 0x20, 0, 0);
        started = 1;
    }
    ui_play_voice(0x21, 8);
    if (started) {
        ui_far_set(UI_CB_IDLE, 0x05DC, 0x00CD);                /* ui_nop */
        bc_farfree(bc_far_ptr(g_ui_idle_arg));
    }
    g_iso_speaker = 0xFF;
}

/*
 * iso_sfx_rate - sample rate for voice class of `who` (byte table 1E8D:
 * 0/2 6000, 1 7500, 3 7000, 4 6500, 5 4500, 6 20000, 7 8500, 9 44000,
 * other 22000 Hz; who > 40 (party) 7500) times base / 7500, unsigned long;
 * FFFFh if that is FFFFh or more.
 *
 * Original: IST.EXE ovl4595:255B (ISOSYS.L), far cdecl (who, u32 base).
 * Verified: tests/py/test_iso.py (exhaustive over who), 2026-11-21.
 */
uint16_t iso_sfx_rate(int16_t who, uint32_t base)
{
    uint16_t r;
    uint32_t v;
    if (who > 0x28) {
        r = 7500;
    } else {
        switch (DG8((uint16_t)(0x1E8D + who))) {       /* jump table 4595:260F */
        case 0: r = 6000; break;
        case 1: r = 7500; break;
        case 2: r = 6000; break;
        case 3: r = 7000; break;
        case 4: r = 6500; break;
        case 5: r = 4500; break;
        case 6: r = 20000; break;
        case 7: r = 8500; break;
        case 9: r = 44000; break;
        default: r = 22000; break;     /* 8 and > 9 */
        }
    }
    v = (uint32_t)(r * base) / 7500u;
    return v >= 0xFFFF ? 0xFFFF : (uint16_t)v;
}

/*
 * iso_voice_filename - sample name of voice n for speaker `who`: vecize
 * sayings "vecize%.2d.pur" (male) / "fecize%.2d.pur" (female), with 631A
 * set the answers "cevap%.2d.pur" / "fevap%.2d.pur".  Female: the party
 * member's gender (who > 40: stats of member who - 64) is not 1, or the
 * monster's voice class (1E8D) is 2, 3, 6 or 9.
 *
 * Original: IST.EXE ovl4595:2623 (ISOSYS.L), far cdecl (who, char far
 * *buf, n).
 * Verified: tests/py/test_iso.py (exhaustive over who), 2026-11-21.
 */
void iso_voice_filename(int16_t who, char *buf, int16_t n)
{
    int female;
    if (who > 0x28) {
        /* 82AA + 5Ch*who = g_party_stats[who - 64].gender */
        female = DGS16((uint16_t)(0x82AA + 0x5C * who)) != 1;
    } else {
        switch (DG8((uint16_t)(0x1E8D + who))) {       /* tables 4595:2711 / 2701 */
        case 2: case 3: case 6: case 9: female = 1; break;
        default: female = 0; break;
        }
    }
    if (g_iso_voice == 0)
        sprintf(buf, female ? DGSTR(who > 0x28 ? 0x65F8 : 0x6616)
                            : DGSTR(who > 0x28 ? 0x65E9 : 0x6607), n);
    else
        sprintf(buf, female ? DGSTR(who > 0x28 ? 0x6633 : 0x664F)
                            : DGSTR(who > 0x28 ? 0x6625 : 0x6641), n);
}

static void face_anim_far(uint32_t arg) { iso_face_anim_tick(arg); }

void iso_turn_register(void)
{
    ui_register_far_fn(0x1694, 0x007A, (UiFarFn)face_anim_far);
}
