/*
 * s3d_camp.c - 3DSYS.L part 6 (IST.EXE ovl303F:4353-4F39): camping, the
 * "monsters attack" phrase and the date / time line of the panel.  See
 * s3d.h.
 */
#include "game/s3d.h"

#include <stdio.h>
#include <string.h>

#include "engine/fatal.h"
#include "engine/grtext.h"
#include "engine/intrface.h"
#include "engine/ngraphs.h"
#include "engine/nkontrol.h"
#include "engine/systems.h"
#include "game/s3d_ext.h"
#include "platform/pc.h"
#include "rtl/bc_rand.h"

#define REC(p, off)     DGS16(0x99A6 + 0x5C * (p) + (off))  /* party record word */
#define g_s3d_phrase    0xB9F8          /* char[0x4B] phrase built by s3d_make_random_phrase */
#define g_s3d_status_text 0xBA42        /* char[0x40] status line as last drawn */
#define g_ui_col_966B   DG8(0x966B)     /* camp progress bar colour */

static int16_t random_n(int16_t n)
{
    return (int16_t)((int32_t)bc_rand() * n / 0x8000);
}

/* the district's "no random encounters" event flag (district table +1Ch)
   is not set */
static int encounters_allowed(void)
{
    uint8_t flag = DG8(0x2E4D + 0x1D * g_s3d_district);
    return !(g_q3d_opt_flags & 4) && (flag == 0 || !fr_flag_test(flag));
}

static void message(uint16_t text, int16_t wait)
{
    ui_message_box(DGSTR(text), 1, 0);
    ui_wait_click_or_key(wait);
}

/*
 * s3d_camp - rest (key K / camp button).  Refused on a dangerous cell
 * (3D5E bit 3) and when nobody needs it.  The rest takes 15 minutes per
 * "hour" needed: the largest of (max - current) of record +0Ch/+3Ah and
 * (max - current) / 30 + 1 of +0Ah/+38h over the living members.  Each
 * minute advances the clock and fills the progress bar; every 15 minutes
 * everybody heals (+38h by up to 30, +3Ah by 1 from -5 up, +22h up and +2Ah
 * down by an amount from the level word +12h) and the panel is redrawn.
 * A key Esc or a click stops it; with probability rate/(rate + 7) - unless
 * encounters are off - an encounter interrupts it after random(minutes)
 * + 1 minutes (3D6A = 1, encounter table 2).  A long rest (> 30 hours)
 * may also end with an encounter (random(6) < 2).
 *
 * Original: IST.EXE 303F:4353 (3DSYS.L), far cdecl.  The 50 ms wait per
 * minute spins on the clock calling gr_cursor_update; the port calls
 * pc_poll() at its test (303F:4903).
 * Verified: tests/py/test_s3d_camp.py.
 */
void s3d_camp(void)
{
    const int16_t bar_w = 0x13F;
    int16_t need = 0, stop, i, p, v, d;
    uint16_t t0;
    int16_t dummy;

    if (g_q3d_opt_flags & 8) {
        message(0x4345, 0x14);                  /* "burada kamp yapmak Cok tehlikeli." */
        ui_restore_background();
        return;
    }
    for (p = 1; p < 5; p++) {
        if (DG8(0xBA77 + 0x11 * p) == 0xFF)     /* panel: dead */
            continue;
        if (REC(p, 0xC) - REC(p, 0x3A) > need)
            need = (int16_t)(REC(p, 0xC) - REC(p, 0x3A));
        if ((REC(p, 0xA) - REC(p, 0x38)) / 30 > need)
            need = (int16_t)((REC(p, 0xA) - REC(p, 0x38)) / 30 + 1);
    }
    if (need == 0) {
        message(0x4367, 0x28);                  /* "yorgun deGiliz ki..." */
        ui_restore_background();
        return;
    }
    fr_show_icon(0x4F, 1);
    need = (int16_t)(need * 15);
    if (random_n((int16_t)(g_s3d_enc_rate + 7)) < g_s3d_enc_rate && encounters_allowed())
        stop = (int16_t)(random_n(need) + 1);
    else
        stop = (int16_t)(need + 1);
    ui_message_box(DGSTR(0x437C), 1, 0);        /* "kamp yapIyoruz..." */
    for (i = 0; i < need; i++) {
        t0 = (uint16_t)g_clock_ms;
        s3d_advance_clock(1);
        gr_cursor_hide();
        sys_fill_rect(0, 0xC6, (uint16_t)((uint32_t)(int32_t)((i + 1) * bar_w) / (uint32_t)(int32_t)need),
                      2, 0, g_ui_col_966B);
        gr_cursor_set_shape((uint8_t)((int16_t (*)(void))ui_far_fn(DG32(UI_CB_SHAPE)))());
        if (pc_kbhit() && (pc_bioskey(0) >> 8) == 1)
            break;
        if (gr_mouse_read(&dummy, &dummy, &dummy) != 0) {
            gr_wait_buttons_up();
            break;
        }
        if (i >= stop) {
            ui_restore_background();
            message(0x438E, 0x14);              /* "nooluyor yaahuu!!" */
            g_s3d_encounter = 1;
            g_s3d_enc_kind = 2;
            break;
        }
        if (i % 15 == 0) {
            for (p = 1; p < 5; p++) {
                if (REC(p, 0x38) < REC(p, 0xA)) {
                    v = (int16_t)(REC(p, 0xA) - REC(p, 0x38));
                    REC(p, 0x38) += v >= 30 ? 30 : v;
                }
                if (REC(p, 0x3A) < REC(p, 0xC) && REC(p, 0x3A) >= -5)
                    REC(p, 0x3A)++;
                if (REC(p, 0x22) != 0) {
                    d = (int16_t)(5 - REC(p, 0x12) / 100);
                    if (d < 1)
                        d = 1;
                    v = (int16_t)(REC(p, 0x22) + d);
                    REC(p, 0x22) = v < 1 ? 1 : v;
                }
                if (REC(p, 0x2A) != 0) {
                    d = (int16_t)(REC(p, 0x5A) / 2 - REC(p, 0x12) / 100);
                    if (d < 1)
                        d = 1;
                    v = (int16_t)(REC(p, 0x2A) - d);
                    REC(p, 0x2A) = v < 1 ? 1 : v;
                }
            }
            s3d_draw_party_panel();
        }
        for (;;) {
            pc_poll();                          /* 303F:4903 loop test */
            if ((uint16_t)((uint16_t)g_clock_ms - t0) >= 0x32)
                break;
            gr_cursor_update((uint8_t)g_cursor_shape);
        }
    }
    ui_restore_background();
    s3d_apply_daylight();
    s3d_redraw_view(1);
    s3d_redraw_screen();
    if (g_s3d_encounter == 0) {
        uint8_t r = ui_message_box(DGSTR(i == need ? 0x43A0 : 0x43BF), 1, 0);
        ui_wait_click_or_key((int16_t)(r * 0x46));
        s3d_redraw_view(1);
        ui_restore_background();
    }
    if (need / 4 > 30 && random_n(6) < 2 && g_s3d_encounter == 0 && encounters_allowed()) {
        message(0x43D2, 0x14);                  /* "yaa tam da gidiyoduk yaa" */
        g_s3d_encounter = 1;
        g_s3d_enc_kind = 2;
        ui_restore_background();
    }
    s3d_draw_status_line(0xC6, 0xB2);
}

/*
 * s3d_make_random_phrase - "<monsters> <verb> !" for an encounter, from
 * enemy group n and the next one (n % 3 + 1): "bir " for one, Turkish
 * plural suffixes (-ları / -kleri / -ler / -lar) for more, "ve " between
 * the two, line breaks when wider than 300 / 200 pixels, then "bastI" for
 * monster type 1Fh or one of five verbs at 3D77 (decoded in place by
 * grt_decode_turkish_lower).  Returns the DGROUP buffer B9F8.
 *
 * Original: IST.EXE 303F:4A27 (3DSYS.L), far cdecl (int n), returns
 * DX:AX.  The verb test reads group type 5BD4 + 6n with n as advanced by
 * the loop (so the group before the last one used).  Fatal 4Bh when longer
 * than 74 characters.
 * Verified: tests/py/test_s3d_camp.py (test_random_phrase).
 */
char *s3d_make_random_phrase(int16_t n)
{
    char *s = DGSTR(g_s3d_phrase);
    char *verb;
    int16_t k;

    s[0] = 0;
    for (k = 0; k < 2; k++) {
        if (g_s3d_grp_count(n) == 0)
            break;
        if (k == 1)
            strcat(s, DGSTR(0x43EB));           /* "ve " */
        if (g_s3d_grp_count(n) == 1)
            strcat(s, DGSTR(0x43EF));           /* "bir " */
        if (grt_text_width(s, 1) + grt_text_width(DGSTR(0x1EB3 + 0x13 * g_s3d_grp_type(n)), 1) > 300)
            strcat(s, DGSTR(0x43F4));           /* "\n" */
        strcat(s, DGSTR(0x1EB3 + 0x13 * (g_s3d_grp_type(n) % 100)));
        if (g_s3d_grp_count(n) > 1) {
            char *end = s + strlen(s) - 2;
            char *end3 = s + strlen(s) - 3;
            if (strncmp(end, DGSTR(0x43F6), 2) == 0) {          /* "sı" */
                strcpy(end, DGSTR(0x43F9));                      /* "ları" */
            } else if (strncmp(end, DGSTR(0x43FE), 2) == 0) {   /* "çü" */
                strcpy(end, DGSTR(0x4401));                      /* "kleri" */
            } else if (strchr(end3, 'e') || strchr(end3, 'i') || strchr(end3, (char)0x94)
                       || strchr(end3, (char)0x81)) {           /* front vowels e i ö ü */
                strcat(s, DGSTR(0x4407));                        /* "ler" */
            } else {
                strcat(s, DGSTR(0x440B));                        /* "lar" */
            }
        }
        strcat(s, DGSTR(0x440F));               /* " " */
        n = (int16_t)(n % 3 + 1);
    }
    if (grt_text_width(s, 1) > 200)
        strcat(s, DGSTR(0x4411));               /* "\n" */
    if (DGS16(0x5BD4 + 6 * n) == 0x1F)
        verb = DGSTR(0x4413);                   /* "bastI" */
    else
        verb = DGSTR(0x3D77 + 0x11 * random_n(5));
    strcat(s, grt_decode_turkish_lower(verb));
    strcat(s, DGSTR(0x4419));                   /* " !" */
    if (strlen(s) > 0x4A)
        o559A_fatal(DGSTR(0x441C), 0x7EA, NULL, 0x4B);
    return s;
}

/*
 * s3d_draw_status_line - draw "<day> <month> <weekday>, hh:mm" at (x, y)
 * in a 110x6 box, redrawing only from the first character that differs
 * from the text drawn last time (BA42; everything while the screen is
 * being built, 3D5E bit 10h).
 *
 * Original: IST.EXE 303F:4D22 (3DSYS.L), far cdecl (int x, int y).  The
 * text style is a local constant 2 (outlined).  Fatal 4Ch when longer than
 * 63 characters.
 * Verified: tests/py/test_s3d_camp.py (test_status_line).
 */
void s3d_draw_status_line(int16_t x, int16_t y)
{
    const int16_t w = 0x6E, h = 6;
    const uint8_t style = 2;
    char buf[0x41];
    char *last = DGSTR(g_s3d_status_text);
    int16_t i, off;

    if (g_q3d_opt_flags & 0x10)
        last[0] = 0;
    sprintf(buf, DGSTR(0x442B), (int)(g_s3d_day % 30 + 1), DGSTR(0x3DCC + 8 * (g_s3d_day / 30)),
            DGSTR(0x3E2C + 10 * (g_s3d_day % 7)), (int)DGS16(0x3D66), (int)g_s3d_minute);
    grt_decode_turkish_lower(buf);
    if (last[0] == 0) {
        i = 0;
        off = 0;
    } else {
        for (i = 0; (uint16_t)i < strlen(buf) && buf[i] == last[i]; i++)
            ;
        strncpy(last, buf, (size_t)i);
        last[i] = 0;
        off = (int16_t)(grt_text_width(last, style) + 1);
    }
    if (strlen(buf) > 0x3F)
        o559A_fatal(DGSTR(0x443F), 0x808, NULL, 0x4C);
    if (gr_cursor_in_zone((uint8_t)g_ui_panel_zone))
        gr_cursor_hide();
    sys_fill_rect((uint16_t)(x + off), (uint16_t)y, (uint16_t)(w - off), (uint16_t)h, 0, 0);
    grt_draw_text_alt(0, (int16_t)(x + off), (int16_t)(y + 1), buf + i, 0);
    grt_draw_text_alt(0, (int16_t)(x + off), y, buf + i, g_ui_col_initial);
    gr_cursor_set_shape((uint8_t)((int16_t (*)(void))ui_far_fn(DG32(UI_CB_SHAPE)))());
    strcpy(last, buf);
}
