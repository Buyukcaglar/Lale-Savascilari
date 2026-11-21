/*
 * frpos_util.c - FRPOS.L part 3 (IST.EXE ovl3FD1:46AA-5C25): script access
 * (fetch, skip, strings), event flags, the portrait/icon window, the
 * 6-choice menu, the money panel and the shop panel's slide in/out.
 * See frpos.h.
 *
 * The script file (PCSn) is a MEMMAN block in EMS whose record the far
 * pointer 16E0:E34F points to; every byte is read through EMSLock at the
 * PC 16E0:BB98 (an offset in the file, header included), with the page
 * mapping saved and restored around it (mm_ems_lock on 16E0:DE26).
 */
#include "game/frpos.h"

#include <stdio.h>
#include <string.h>

#include "dsmi/emsheap.h"
#include "engine/blit386.h"
#include "engine/fatal.h"
#include "engine/fileman.h"
#include "engine/grtext.h"
#include "engine/intrface.h"
#include "engine/memman.h"
#include "engine/ngraphs.h"
#include "engine/nkontrol.h"
#include "engine/ographs.h"
#include "engine/systems.h"
#include "engine/vfs.h"
#include "game/q3dview.h"
#include "game/s3d.h"
#include "platform/pc.h"
#include "rtl/bc_farheap.h"

void iso_format_stat_line(void);           /* 4595:7F80, provided by ISOSYS (WP17) */

/* the source-file name strings "T:CODE\FRPOS.L" the original passes to
   mm_ems_lock / o559A_fatal / mm_free (one DGROUP copy per call site) */
#define FILE_AT(o)      DGSTR(o)
#define REF_PCS         0xE34F          /* far MmHandle *: the PCSn script file */
#define REF_RIX         0xE52F          /* far MmHandle *: the .RIX blend table (sys_load_rix) */
#define REF_SURF(n)     ((uint16_t)(0xE29F + 8 * (n)))   /* surface n's MmHandle ref */

/*
 * fr_decode_text - decode script text in place: every byte + 10 until the
 * NUL (the bytes are stored 10 lower: 16h = ' ', 0ACh... = '\n' etc.).
 *
 * Original: IST.EXE ovl3FD1:46AA (FRPOS.L), far cdecl.  strlen() is
 * re-evaluated every step, as in the original (a byte that decodes to 0
 * ends the string early).
 * Verified: tests/py/test_frpos.py (unicorn differential), 2026-11-21.
 */
void fr_decode_text(char *s)
{
    uint16_t i;

    for (i = 0; (uint16_t)strlen(s) > i; i++)
        s[i] = (char)(s[i] + 10);
}

/*
 * fr_find_character - choose a party member by a stat word.
 *   mode == 0: kind == 0 the member with the lowest word `field`,
 *              kind != 0 the one with the highest (first of equals);
 *   mode != 0: kind 0 the first whose word < mode, kind 1 > mode, other
 *              kinds == mode.
 * Returns 1..4, or 5 when nobody matches.
 *
 * Original: IST.EXE ovl3FD1:46E3 (FRPOS.L), far cdecl.  The original
 * compares each member with Borland's min()/max() of the other three
 * (record (i % 4) + 1 etc.); the double evaluation has no side effects.
 * Verified: tests/py/test_frpos.py (unicorn differential), 2026-11-21.
 */
int16_t fr_find_character(int16_t mode, int16_t kind, int16_t field)
{
    int16_t i;

    if (mode == 0) {
        for (i = 1; i <= 4; i++) {
            int16_t a = FR_STAT((int16_t)((i + 1) % 4) + 1, field);
            int16_t b = FR_STAT((int16_t)((i + 2) % 4) + 1, field);
            int16_t c = FR_STAT((int16_t)(i % 4) + 1, field);
            int16_t m;

            if (kind == 0) {
                m = a < b ? a : b;
                m = m > c ? c : m;
                if (!(m < FR_STAT(i, field)))
                    break;
            } else {
                m = a > b ? a : b;
                m = m < c ? c : m;
                if (!(m > FR_STAT(i, field)))
                    break;
            }
        }
        return i;
    }
    for (i = 1; i <= 4; i++) {
        int16_t v = FR_STAT(i, field);

        if (kind == 0 ? v < mode : kind == 1 ? v > mode : v == mode)
            break;
    }
    return i;
}

/*
 * fr_flag_test / fr_flag_set / fr_flag_clear - event flag n: bit n % 8 of
 * byte n / 8 of 16E0:B9CA (signed division as in the original).
 *
 * Original: IST.EXE ovl3FD1:4AAC / 4AD9 / 4B04 (FRPOS.L), far cdecl.
 * fr_flag_test returns the masked byte in AL (callers test AL).
 * Verified: tests/py/test_frpos.py (unicorn differential), 2026-11-21.
 */
uint8_t fr_flag_test(int16_t n)
{
    return (uint8_t)(g_fr_flags[(uint16_t)(int16_t)(n / 8)] &
                     (uint8_t)(1u << ((uint16_t)(n % 8) & 0x1F)));
}

void fr_flag_set(int16_t n)
{
    g_fr_flags[(uint16_t)(int16_t)(n / 8)] |= (uint8_t)(1u << ((uint16_t)(n % 8) & 0x1F));
}

/* unreferenced */
void fr_flag_clear(int16_t n)
{
    g_fr_flags[(uint16_t)(int16_t)(n / 8)] &= (uint8_t)~(1u << ((uint16_t)(n % 8) & 0x1F));
}

/*
 * fr_flags_reset - clear all 256 event flags.
 *
 * Original: IST.EXE ovl3FD1:4B31 (FRPOS.L), far cdecl.
 * Verified: tests/py/test_frpos.py (unicorn differential), 2026-11-21.
 */
void fr_flags_reset(void)
{
    int16_t i;

    for (i = 0; i < 0x20; i++)
        g_fr_flags[i] = 0;
}

/*
 * fr_show_icon - show portrait/icon "ikon<n>.cmp" (+ ".pal" if it exists)
 * in the 3D view window, then optionally wait 50 clock units.
 *
 * The picture goes to surface 55h; with a palette, the DAC gets entries
 * 0..7Fh from the .pal, the brightness is bumped and the anti-alias table
 * (.RIX block, 16E0:E52F) is rebuilt.  A 200x200 view gets the picture copied (or,
 * for pictures up to 100x100, the 2x upscaler), otherwise scaled.
 *
 * Original: IST.EXE ovl3FD1:4B57 (FRPOS.L), far cdecl (icon, wait).  The
 * handle of the .pal probe is closed twice (o569A_close on a closed
 * handle just fails), kept.
 * Verified: tests/py/test_frpos.py (unicorn differential), 2026-11-21.
 */
void fr_show_icon(uint8_t icon, uint8_t wait)
{
    char name[0x16];
    char *pal;
    int16_t fd;

    ui_restore_background();
    s3d_redraw_view(1);
    q3d_free_anim_buffer(2);
    sprintf(name, DGSTR(0x6235), icon);                    /* "ikon%d.cmp" */
    fm_load_cmp(1, name, 0x55);
    pal = fm_change_ext(name, DGSTR(0x6240));              /* ".pal" */
    fd = vfs_open(pal, 2);
    if (fd != -1) {
        vfs_close(fd);
        fm_load_pal(pal, 0, 0x80);
        g_pal_level++;
        gr_fade(0, -1, 0);
        mm_ems_lock(g_blit_lock, 0x6D6, FILE_AT(0x6245));
        sys_build_aa_table(EMSLock((int16_t)mm_ref(REF_RIX)->ems, 0, 0x7F80), 0);
        mm_ems_unlock(g_blit_lock);
        DG8(0x5BC2) = 1;                                    /* read by s3d_redraw_view */
        s3d_draw_party_panel();
        vfs_close(fd);
    }
    bc_farfree(pal);
    if (g_q3d_win_w == 200 && g_q3d_win_h == 200) {
        if (gr_cursor_in_zone(0x0E))
            gr_cursor_hide();
        if (g_surf_w(0x55) > 100 && g_surf_h(0x55) > 100) {
            sys_blit(0, 0, g_surf_w(0x55) > 200 ? 200 : g_surf_w(0x55),
                     g_surf_h(0x55) > 200 ? 200 : g_surf_h(0x55), 0, 0, 0x55, 0x41,
                     asm_copy_rect_key);
            q3d_blit_view(0x41, 0x30, 0x34);
        } else {
            MmHandle *dup = mm_duplicate(mm_ref(REF_SURF(0x55)), 0);
            MmHandle *aa;

            mm_ref_set(0xE56C, dup);
            mm_ems_lock(g_blit_lock, 0x6EA, FILE_AT(0x6254));
            aa = mm_ref(REF_RIX);
            asm_upscale_view2x(mm_conv_ptr(mm_ref(0xE56C)),
                               EMSLock((int16_t)aa->ems, 0, (uint16_t)aa->size));
            mm_ems_unlock(g_blit_lock);
            mm_free(mm_ref(0xE56C), 0x6ED, FILE_AT(0x6263));
        }
        gr_call_then_cursor(s3d_draw_portrait_hl);         /* far 1670:0084 */
    } else {
        sys_blit_scaled(0, 0, g_surf_w(0x55), g_surf_h(0x55), 0, 0, g_surf_w(0x41),
                        g_surf_h(0x41), 0x55, 0x41, 0);
        q3d_blit_view(0x41, 0x30, 0x34);
    }
    ctl_free_checked(REF_SURF(0x55), 0x6F6, FILE_AT(0x6272));
    if (wait)
        ui_wait_click_or_key(0x32);
    q3d_wait_retrace(2);                       /* (misnamed) allocates surface 43h */
}

/*
 * fr_stub_4E42 - window-record fill callback (+1Ch): gr_draw_strip with
 * the record's arguments.
 *
 * Original: IST.EXE ovl3FD1:4E42 (FRPOS.L), far cdecl; reached through
 * the far pointer 1689:003E that ui_init_tables stores at 16E0:9840.
 * Verified: by inspection (a plain forwarder).
 */
void fr_stub_4E42(int16_t x, int16_t y, int16_t w, int16_t h, int16_t slot, int16_t color)
{
    gr_draw_strip((uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h, (uint8_t)slot,
                  (uint8_t)color);
}

/*
 * fr_fetch_byte - the script byte at PC (16E0:BB98), PC++.
 *
 * Original: IST.EXE ovl3FD1:4E63 (FRPOS.L), far cdecl.  EMSLock gets the
 * sign-extended PC as offset and (low word of the block size - PC) as
 * length.
 * Verified: tests/py/test_frpos.py (unicorn differential), 2026-11-21.
 */
uint8_t fr_fetch_byte(void)
{
    MmHandle *h;
    uint16_t pc;
    uint8_t b;

    mm_ems_lock(g_blit_lock, 0x706, FILE_AT(0x6281));
    h = mm_ref(REF_PCS);
    pc = g_fr_pc++;
    b = *EMSLock((int16_t)h->ems, (int32_t)(int16_t)pc, (uint16_t)((uint16_t)h->size - pc));
    mm_ems_unlock(g_blit_lock);
    return b;
}

/*
 * fr_skip_commands - skip the next n commands (forward jump): the command
 * counter (BB93) and BB92 drop by n, then each command's opcode is read
 * and PC advanced over its operands (ops 2/5/10/17/21/27/30 carry
 * length-prefixed strings).  An unknown opcode is fatal ("counter=%d,
 * komut=%d").
 *
 * Original: IST.EXE ovl3FD1:4ECB (FRPOS.L), far cdecl; operand table =
 * switch @4F38 (table 3FD1:505B).  Debug traces when 16E0:5C0B > 1.
 * Verified: tests/py/test_frpos.py (unicorn differential), 2026-11-21.
 */
/* PC += the length byte at PC (+ extra); the byte is fetched (PC++) first */
static void skip_text(uint16_t extra)
{
    uint8_t len = fr_fetch_byte();

    g_fr_pc = (uint16_t)(g_fr_pc + len + extra);
}

void fr_skip_commands(int16_t n)
{
    int16_t i, k;
    uint8_t op;

    g_fr_count = (uint8_t)(g_fr_count - (uint8_t)n);
    g_fr_sect_flags = (uint8_t)(g_fr_sect_flags - (uint8_t)n);
    if (g_fr_debug > 1)
        fatal_printf(DGSTR(0x6290), n);                    /* "Atla:%d=> " */
    for (i = 0; i < n; i++) {
        op = fr_fetch_byte();
        if (g_fr_debug > 1) {
            fatal_printf(DGSTR(0x629B), op);               /* ".%d." */
            ui_getch();
        }
        switch (op) {
        case 2:                                     /* [len] text */
            skip_text(0);
            break;
        case 5: {                                   /* [l1][l2] text1 text2 */
            uint16_t a = fr_fetch_byte();
            uint16_t b = fr_fetch_byte();
            g_fr_pc += (uint16_t)(a + b);
            break;
        }
        case 10:                                    /* [l1] text1 [l2] text2 */
            skip_text(0);
            skip_text(0);
            break;
        case 17:                                    /* who [len] text */
            g_fr_pc++;
            skip_text(0);
            break;
        case 21:                                    /* 6 x [len] text */
            for (k = 0; k < 6; k++)
                skip_text(0);
            break;
        case 30:                                    /* [len] skip text */
            skip_text(1);
            break;
        case 4: case 16: case 18: case 24: case 28: case 32: case 33:
            break;
        case 3: case 6: case 7: case 13: case 14: case 15: case 19: case 20:
        case 25: case 26: case 31:
            g_fr_pc++;
            break;
        case 8:
            g_fr_pc += 7;
            break;
        case 9:
            g_fr_pc += 9;
            break;
        case 11:
            g_fr_pc += 4;
            break;
        case 1: case 12: case 22: case 29:
            g_fr_pc += 3;
            break;
        case 23:
            g_fr_pc += 2;
            break;
        case 27:                                    /* 40 x [len] text */
            for (k = 0; k < 0x28; k++)
                skip_text(0);
            break;
        default:                                    /* "counter=%d, komut=%d" */
            if (sprintf(g_ui_msg_buf, DGSTR(0x62A0), (int16_t)g_fr_pc, op) >= 0x68)
                o559A_fatal(FILE_AT(0x62B5), 0x760, NULL, 0x61);
            o559A_fatal(FILE_AT(0x62C4), 0x762, NULL, 0x62);
            break;
        }
    }
}

/*
 * fr_fetch_string - n script bytes into a new farmalloc'd NUL-terminated
 * buffer, decoded (+10); PC += n.
 *
 * Original: IST.EXE ovl3FD1:509D (FRPOS.L), far cdecl; returns DX:AX.
 * Verified: tests/py/test_frpos.py (unicorn differential), 2026-11-21.
 */
char *fr_fetch_string(int16_t n)
{
    char *buf = bc_farmalloc((uint32_t)(int32_t)(int16_t)(n + 1));
    MmHandle *h;

    mm_ems_lock(g_blit_lock, 0x76D, FILE_AT(0x62D3));
    h = mm_ref(REF_PCS);
    mm_memcpy(EMSLock((int16_t)h->ems, (int32_t)(int16_t)g_fr_pc,
                      (uint16_t)((uint16_t)h->size - g_fr_pc)), buf, (uint16_t)n);
    mm_ems_unlock(g_blit_lock);
    buf[(uint16_t)n] = 0;
    fr_decode_text(buf);
    g_fr_pc += (uint16_t)n;
    return buf;
}

/*
 * fr_item_name - shop screen: the description of the item in D5E6/D5EA
 * (count/id) on the text line; with mode != 0 followed by " <price>
 * kIvrIk".  No item (id 0): an empty line.
 *
 * Original: IST.EXE ovl3FD1:513F (FRPOS.L), far cdecl (mode, price).
 * iso_format_stat_line (4595:7F80) builds the text in 16E0:A510.
 * Verified: tests/py/test_frpos_shop.py (unicorn differential), 2026-11-21.
 */
void fr_item_name(uint8_t mode, int16_t price)
{
    char num[8];

    if (g_cs_item_id == 0) {
        fr_draw_text_line(DGSTR(0x62EC));                  /* "" */
        return;
    }
    iso_format_stat_line();
    if (mode) {
        strcat(DGSTR(0xA510), DGSTR(0x62E2));              /* " " */
        sprintf(num, "%d", price);                         /* itoa(price, num, 10) */
        strcat(DGSTR(0xA510), num);
        strcat(DGSTR(0xA510), DGSTR(0x62E4));              /* " kIvrIk" */
    }
    grt_decode_turkish_lower(DGSTR(0xA510));
    fr_draw_text_line(DGSTR(0xA510));
}

/*
 * fr_draw_text_line - the shop panel's text line (surface 53h): clear
 * 0DFh x 0Bh at (29h, 3), draw s with a shadow.
 *
 * Original: IST.EXE ovl3FD1:51C7 (FRPOS.L), far cdecl.  A constant switch
 * (1) picks grt_draw_text over grt_draw_text_alt.  s is decoded in place
 * (grt_decode_turkish_lower).
 * Verified: tests/py/test_frpos_shop.py (unicorn differential), 2026-11-21.
 */
void fr_draw_text_line(char *s)
{
    sys_fill_rect(0x29, 3, 0xDF, 0x0B, 0x53, 0);
    grt_decode_turkish_lower(s);
    grt_draw_text(0x53, 0x2A, 3, s, g_ui_col_text, 5);
    grt_draw_text(0x53, 0x29, 2, s, g_ui_col_initial, 5);
}

/*
 * fr_menu - up to six choices (script op 21) drawn on the screen at
 * (8, 94h + 8i) with zones 6..11; the player clicks one or types its
 * first letter.  Returns 1..6 (also left in ACC).
 *
 * Original: IST.EXE ovl3FD1:5285 (FRPOS.L), far cdecl (char far *items[6]).
 * Waits on the mouse/keyboard in a loop (the port polls through
 * gr_cursor_update / ui_kbhit).
 * Verified: tests/py/test_frpos.py (unicorn differential), 2026-11-21.
 */
uint8_t fr_menu(char **items)
{
    char first[6];
    uint8_t i;
    int16_t z, k;

    ui_restore_background();
    if (gr_cursor_in_zone((uint8_t)g_ui_panel_zone))
        gr_cursor_hide();
    for (i = 0; i < 6; i++) {
        grt_draw_text(0, 8, (int16_t)(i * 8 + 0x95), items[i], 0xFE, 5);
        grt_draw_text(0, 8, (int16_t)(i * 8 + 0x94), items[i], 0xF5, 5);
        sprintf(first, DGSTR(0x62EE), (int16_t)items[i][0]);   /* "%c" */
        grt_draw_text(0, 8, (int16_t)(i * 8 + 0x94), first, g_ui_col_initial, 5);
        g_zone_x1(6 + i) = 5;
        g_zone_y1(6 + i) = (int16_t)(i * 8 + 0x94);
        g_zone_x2(6 + i) = (int16_t)(g_zone_x1(6 + i) + grt_text_width(items[i], 1) + 6);
        g_zone_y2(6 + i) = (int16_t)(g_zone_y1(6 + i) + 8);
    }
    gr_cursor_set_shape((uint8_t)g_cursor_shape);
    g_fr_acc = 100;
    while (g_fr_acc == 100) {
        if (g_cursor_visible)
            gr_cursor_update((uint8_t)g_cursor_shape);
        if (g_mouse_buttons == 1) {
            z = gr_zone_under_cursor();
            if (z > 5 && z < 12)
                g_fr_acc = (int16_t)(z - 5);
            gr_wait_buttons_up();
        }
        if (ui_kbhit()) {
            char c;

            k = (int16_t)(ui_getch() & 0xFF);
            c = (char)k;
            grt_tolower_inplace(&c);           /* on the low byte of the int */
            k = (int16_t)((k & 0xFF00) | (uint8_t)c);
            for (i = 0; i < 6; i++)
                if ((int16_t)items[i][0] == k)
                    g_fr_acc = (int16_t)(i + 1);
        }
    }
    if (gr_cursor_in_zone((uint8_t)g_ui_panel_zone))
        gr_cursor_hide();
    grt_draw_text(0, 8, (int16_t)((g_fr_acc - 1) * 8 + 0x94), items[g_fr_acc - 1],
                  g_ui_col_initial, 5);
    gr_cursor_set_shape((uint8_t)g_cursor_shape);
    ui_wait_click_or_key(5);
    ui_restore_background();
    return (uint8_t)g_fr_acc;
}

/*
 * fr_draw_money - the three money counters (words BB73 / BB71 / BB6F,
 * names from 5C4A) "%s%5d" on the screen, or on the saved panel picture
 * (surface 17h, y - panel y) while a dialog panel is up.  Only changed
 * values are redrawn unless 3D5E bit 4 forces it; the last drawn values
 * are kept in BB8C..BB91.
 *
 * Original: IST.EXE ovl3FD1:5550 (FRPOS.L), far cdecl.  Positions and
 * names are local copies of 16E0:5C3E (3 x {x, y}) and 5C4A (3 x char[3]).
 * A constant switch (2) selects grt_draw_text_alt.  dst stays 17h for the
 * later counters once set (it is not reset per counter).
 * Verified: tests/py/test_frpos.py (unicorn differential), 2026-11-21.
 */
void fr_draw_money(void)
{
    int16_t pos[4][2];
    char names[4][3];
    char buf[0x0C];
    uint8_t dst = 0;
    int16_t i;

    memcpy(&pos[1][0], DGP(0x5C3E), 0x0C);
    memcpy(&names[1][0], DGP(0x5C4A), 9);
    gr_cursor_hide();
    for (i = 1; i <= 3; i++) {
        int16_t v = DGS16((uint16_t)(0xBB6D + 2 * (4 - i)));

        if (v == DGS16((uint16_t)(0xBB8C + 2 * (3 - i))) && !(g_q3d_opt_flags & 0x10))
            continue;
        sprintf(buf, DGSTR(0x62F1), names[i], v);           /* "%s%5d" */
        if (strlen(buf) > 0x0A)
            o559A_fatal(FILE_AT(0x62F7), 0x7E3, NULL, 0x63);
        if (mm_ref(REF_SURF(0x17)) != NULL && g_ui_voice_mode != DG8(0x9822)) {
            dst = 0x17;
            pos[i][1] = (int16_t)(pos[i][1] - g_ui_panel_y);
        }
        sys_fill_rect((uint16_t)pos[i][0], (uint16_t)pos[i][1], 0x1B, 6, dst, 0);
        grt_draw_text_alt(dst, pos[i][0], (int16_t)(pos[i][1] + 1), buf, DG8(0x9669));
        grt_draw_text_alt(dst, pos[i][0], pos[i][1], buf, g_ui_col_initial);
        DGS16((uint16_t)(0xBB8C + 2 * (3 - i))) = v;
    }
    gr_cursor_set_shape((uint8_t)g_cursor_shape);
}

/*
 * fr_shop_load_gfx - bring up the shop panel (surface 53h, 320 x 62):
 * the screen strip under it is saved to a new conventional surface 17h.
 * With 16E0:5C0A set the panel is copied to the bottom (y = screen h -
 * 62); otherwise it slides in from the top with the VGA split screen
 * (start address / line compare), s = elapsed^2 / 20 lines, the clock
 * read from 16E0:E74A.  Then the mouse is confined to the panel.
 *
 * Original: IST.EXE ovl3FD1:57E8 (FRPOS.L), far cdecl.  The slide loop
 * spins on the 100 Hz clock; the port polls (pc_poll) once per pass.
 * Verified: tests/py/test_frpos_shop.py (unicorn differential), 2026-11-21.
 */
void fr_shop_load_gfx(void)
{
    uint16_t w = (uint16_t)g_surf_w(0), h = (uint16_t)g_surf_h(0);
    uint16_t n = 0x3E;
    uint16_t s, done, e, t0;

    ui_restore_background();
    gr_cursor_hide();
    g_surf_w(0x17) = (int16_t)w;
    g_surf_h(0x17) = (int16_t)n;
    mm_ref_set(REF_SURF(0x17),
               mm_alloc((uint32_t)(uint16_t)(g_surf_w(0x17) * g_surf_h(0x17)), 0));
    if (DG8(0x5C0A)) {
        DGS16(0x5BC0) = 0;
        DGS16(0x5BBE) = (int16_t)(h - n);
        mm_copy_range(mm_surface(0), mm_ref(REF_SURF(0x17)),
                      (uint32_t)((int32_t)DGS16(0x5BBE) * 0x140), 0, (uint32_t)w * 0x3E);
        mm_copy_range(mm_ref(REF_SURF(0x53)), mm_surface(0), 0,
                      (uint32_t)((int32_t)DGS16(0x5BBE) * 0x140), (uint32_t)w * 0x3E);
    } else {
        DGS16(0x5BBE) = 0;
        mm_copy_range(mm_surface(0), mm_ref(REF_SURF(0x17)), 0, 0, (uint32_t)w * n);
        sys_fill_rect(0, 0, (uint16_t)DGS16(0x5BC0), 1, 0, 0);
        s = 0;
        t0 = (uint16_t)g_clock_ms;
        done = 0;
        while (done < n) {
            if (s > n)
                s = n;
            ogr_set_start_addr((uint32_t)((int32_t)(int16_t)s * 0x50));
            ogr_set_split_line((uint16_t)(200 - s));
            mm_copy_range(mm_ref(REF_SURF(0x53)), mm_surface(0), 0,
                          (uint32_t)(int32_t)DGS16(0x5BC0), (uint16_t)(s * 0x140));
            done = s;
            pc_poll();                         /* the original spins on the clock ISR */
            e = (uint16_t)(((uint16_t)g_clock_ms - t0) / 10);
            s = (uint16_t)((uint16_t)(e * e) / 20);
        }
    }
    ogr_mouse_set_window(DGS16(0x5BC0), (int16_t)((uint16_t)DGS16(0x5BBE) % h),
                         (int16_t)(w - 1), (int16_t)(0x3D % h - 1));
    ogr_mouse_set_pos((int16_t)(DGS16(0x5BC0) + 10), 0x14);
    gr_cursor_set_shape((uint8_t)g_cursor_shape);
}

/*
 * fr_shop_free_gfx - take the shop panel down: restore the saved strip
 * (surface 17h) - at once, or sliding out (s = 62 - elapsed^2 / 20) -
 * reset the split line and mouse window, free surface 17h.  5BBE = 86h
 * after the slide.
 *
 * Original: IST.EXE ovl3FD1:5A7F (FRPOS.L), far cdecl.  Polls like
 * fr_shop_load_gfx.
 * Verified: tests/py/test_frpos_shop.py (unicorn differential), 2026-11-21.
 */
void fr_shop_free_gfx(void)
{
    uint16_t w = (uint16_t)g_surf_w(0), h = (uint16_t)g_surf_h(0);
    int16_t n = 0x3E;
    int16_t s, prev;
    uint16_t t0, e;

    DGS16(0x5BC0) = 0;
    gr_cursor_hide();
    if (DG8(0x5C0A)) {
        mm_copy_range(mm_ref(REF_SURF(0x17)), mm_surface(0), 0,
                      (uint32_t)((int32_t)DGS16(0x5BBE) * 0x140), (uint32_t)w * 0x3E);
    } else {
        s = n;
        t0 = (uint16_t)g_clock_ms;
        prev = n;
        while (prev > 0) {
            if (s < 0)
                s = 0;
            ogr_set_start_addr((uint32_t)((int32_t)s * 0x50));
            ogr_set_split_line((uint16_t)(200 - s));
            mm_copy_range(mm_ref(REF_SURF(0x17)), mm_surface(0), (uint16_t)(s * 0x140),
                          (uint16_t)(s * 0x140), (uint16_t)((prev - s) * 0x140));
            prev = s;
            pc_poll();                         /* the original spins on the clock ISR */
            e = (uint16_t)(((uint16_t)g_clock_ms - t0) / 10);
            s = (int16_t)(n - (int16_t)(e * e) / 20);
        }
        DGS16(0x5BBE) = 0x86;
    }
    ogr_set_split_line(200);
    ogr_mouse_set_window(0, 0, (int16_t)(w - 1), (int16_t)(h - 1));
    ctl_free_checked(REF_SURF(0x17), 0x841, FILE_AT(0x6306));
    gr_cursor_set_shape((uint8_t)g_cursor_shape);
}

/* port: far code addresses of FRPOS routines held in window records */
void fr_register_far_fns(void)
{
    ui_register_far_fn(0x1689, 0x003E, (UiFarFn)fr_stub_4E42);
    ui_register_far_fn(0x1689, 0x0098, (UiFarFn)fr_get_e5e3);
}
