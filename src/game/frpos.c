/*
 * frpos.c - FRPOS.L part 1 (IST.EXE ovl3FD1:0000-23B1): the event
 * interpreter fr_run_event with its 33 opcodes, fr_redraw and
 * fr_teach_aphorism.  See frpos.h and docs/FORMATS.md "FRPOS scripts".
 */
#include "game/frpos.h"

#include <stdio.h>
#include <string.h>

#include "dsmi/amp.h"
#include "dsmi/emsheap.h"
#include "dsmi/memalloc.h"
#include "engine/blit386.h"
#include "engine/fatal.h"
#include "engine/grtext.h"
#include "engine/intrface.h"
#include "engine/memman.h"
#include "engine/ngraphs.h"
#include "engine/nkontrol.h"
#include "engine/systems.h"
#include "game/cd5737.h"
#include "game/demolar.h"
#include "game/gfl.h"
#include "game/q3dview.h"
#include "game/s3d.h"
#include "platform/pc.h"
#include "rtl/bc_farheap.h"
#include "rtl/bc_rand.h"

/* ---- provided by ISOSYS (WP17) ---- */
void iso_battle(void);                                      /* 4595:B40A */

#define REF_MAP         0xE33F          /* far MmHandle *: the PCn map file */

/* ampStopModule + free the module: the sequence the original inlines at
   every music switch (BB75/BB77 = the native g_music_module) */
static void stop_music(void)
{
    ampStopModule();
    if (g_music_module) {
        ampFreeModule(g_music_module);
        MemFree(g_music_module);
        g_music_module = 0;
    }
}

/* a big-endian script word (high byte fetched first) */
static uint16_t fetch_word(void)
{
    uint16_t hi = (uint16_t)(fr_fetch_byte() << 8);

    return (uint16_t)(hi + fr_fetch_byte());
}

/* debug trace: printf + getch when 5C0B > 1 */
#define TRACE(...)  do { if (g_fr_debug > 1) { fatal_printf(__VA_ARGS__); ui_getch(); } } while (0)

/* hard-coded shop key "31313129"/"31313130": weapons / general goods */
static void shop_exchange(uint8_t icon, uint16_t text, uint8_t mode)
{
    fr_show_icon(icon, 1);
    ui_message_box(DGSTR(text), 1, 0);
    ui_wait_click_or_key(0x28);
    ui_restore_background();
    fr_item_exchange(mode);
    fr_redraw();
}

/* hard-coded key "31313128": the food shop (hunger of the living members
   reset for random(5)+2 kIvrIk each) */
static void shop_food(void)
{
    uint8_t price, n;
    int16_t i;

    ui_restore_background();
    ui_message_box(DGSTR(0x5CBD), 1, 0);
    ui_wait_click_or_key(0x28);
    ui_restore_background();
    fr_show_icon(0x2A, 1);
    price = (uint8_t)(bc_random(5) + 2);
    if (sprintf(g_ui_msg_buf, DGSTR(0x5CC6), price) >= 0x68)
        o559A_fatal(DGSTR(0x5CF1), 0xA4, NULL, 0x52);
    ui_message_box(g_ui_msg_buf, 1, 0);
    ui_dialog_2198(DGSTR(0x5D00), DGSTR(0x5D05));           /* "evet" / "hayIr" */
    if (g_fr_acc != 0) {
        fr_redraw();
        return;
    }
    n = 0;
    for (i = 1; i <= 4; i++)
        if (FR_STAT(i, FR_STAT_HP) > 0)
            n++;
    price = (uint8_t)(n * price);
    if ((int16_t)price > g_fr_money) {
        ui_message_box(DGSTR(0x5D0B), 1, 0);
        ui_wait_click_or_key(0x3C);
    } else {
        fr_show_icon(0x66, 1);
        ui_wait_click_or_key(0x78);
        fr_show_icon(0x2A, 0);
        ui_message_box(DGSTR(0x5D25), 1, 0);
        ui_wait_click_or_key(0x28);
        g_fr_money = (int16_t)(g_fr_money - price);
        fr_draw_money();
        for (i = 1; i <= 4; i++) {
            FR_STAT(i, FR_STAT_HUNGER) = 0;
            DG8(0x3D6C) &= (uint8_t)~(1u << (i - 1));
        }
    }
    ui_restore_background();
    s3d_redraw_view(1);
    s3d_draw_party_panel();
    fr_redraw();
}

/* hard-coded key "31313127": the doctor ("havalI mehtap"): for BBAB kIvrIk
   a member whose health is -5 or lower gets health 1; anybody else is "not
   dead, pick another" (asked again), Esc / the right button gives up.
   Every way out but "too poor" puts the party back on its previous cell
   (fr_redraw); the 40-byte text buffer leaks on the "too poor" path, as in
   the original.  Whole-game check against the original: play5
   (docs/VERIFY.md), all four ways out. */
static void shop_doctor(void)
{
    char *buf;
    uint8_t price;
    uint8_t who;
    uint16_t m;

    fr_show_icon(0x60, 1);
    ui_message_box(DGSTR(0x5D3B), 1, 0);
    ui_wait_click_or_key(0x28);
    ui_restore_background();
    buf = bc_farmalloc(0x28);
    price = g_fr_doctor_price;
    sprintf(buf, DGSTR(0x5D49), price);
    ui_message_box(buf, 1, 0);
    ui_dialog_2198(DGSTR(0x5D56), DGSTR(0x5D5B));
    if (g_fr_acc == 0) {
        if ((int16_t)price > g_fr_money) {
            ui_restore_background();
            ui_message_box(DGSTR(0x5D61), 1, 0);
            ui_wait_click_or_key(0x28);
            ui_restore_background();
            s3d_redraw_view(1);
            return;
        }
        m = 0x5D71;
        for (;;) {
            ui_message_box(DGSTR(m), 1, 0);
            who = ui_poll_input(1);
            if (who == 0 || FR_STAT(who, FR_STAT_HP) <= -5)
                break;
            ui_restore_background();
            m = 0x5D86;
        }
        if (who != 0) {
            s3d_redraw_view(1);
            g_fr_money = (int16_t)(g_fr_money - price);
            ui_restore_background();
            ui_message_box(DGSTR(0x5DA9), 1, 0);
            ui_wait_click_or_key(0x28);
            ui_restore_background();
            ui_message_box(DGSTR(0x5DE2), 1, 0);
            ui_wait_click_or_key(0x28);
            FR_STAT(who, FR_STAT_HP) = 1;
            fr_draw_money();
            s3d_draw_party_panel();
            ui_restore_background();
            ui_message_box(DGSTR(0x5DF5), 0, who);
            ui_wait_click_or_key(0x28);
            fr_show_icon(0x60, 1);
            ui_restore_background();
            ui_message_box(DGSTR(0x5E08), 1, 0);
            ui_wait_click_or_key(0x28);
        }
        s3d_draw_status_line(0xC6, 0xB2);
        ui_restore_background();
        g_fr_cancel_move = 1;
    }
    fr_redraw();
    bc_farfree(buf);
    s3d_redraw_view(1);
}

/* op 8 debug check of the enemy groups against their copy (textmode +
   printf + getch on a mismatch; never happens) */
static void check_groups(uint16_t fmt)
{
    int16_t i, j;

    for (i = 3; i >= 1; i--) {
        if (g_s3d_grp_chk_type(i) == g_s3d_grp_type(i) &&
            g_s3d_grp_chk_count(i) == g_s3d_grp_count(i) &&
            g_s3d_grp_chk_on(i) == g_s3d_grp_on(i))
            continue;
        vga_bios_set_mode(&pc_vga, 3);                      /* textmode(C80) */
        for (j = 3; j >= 1; j--)
            fatal_printf(DGSTR(fmt), g_s3d_grp_chk_type(j), g_s3d_grp_type(j),
                         g_s3d_grp_chk_count(j), g_s3d_grp_count(j), g_s3d_grp_chk_on(j),
                         g_s3d_grp_on(j));
        ui_getch();
    }
}

/*
 * fr_run_event - run the script event of map cell cell_off (offset of the
 * 15-byte cell in s3d_map) for trigger 1 enter / 2 search / 3 leave.
 * Returns 1 when the script teleported the party (op 11), else 0.
 *
 * Event key: cell bytes 11..14 printed "%d" each with a leading 0 below
 * 10 (8 characters; a byte > 20h is fatal).  The event number comes from
 * bytes 13/14 relative to map-file bytes 27h/28h:
 *   (b13 - m27) * 32 - sign(b13 - m27) + b14 - m28   (8-bit)
 * Script file: AmBk header (20 bytes), word count, then per event
 * {u16 len; u8 ?, u8 commands; u16 offsets of the trigger 2/3 sections}
 * - see docs/FORMATS.md.
 *
 * Original: IST.EXE ovl3FD1:0000 (FRPOS.L), far cdecl (int cell, u8 type);
 * returns AL.  Opcode switch: table 3FD1:208C.
 * Verified: tests/py/test_frpos.py (unicorn differential), 2026-11-21.
 */
uint8_t fr_run_event(int16_t cell_off, uint8_t trigger)
{
    char key[0x0C];
    int16_t running = 1;
    uint8_t ret = 0;
    uint8_t b, ev, v27, v28, b13, b14;
    int8_t sign;
    int16_t i;
    uint8_t op;

    g_fr_pc = 0;
    if (cell_off < 0 || (int16_t)((int16_t)(g_s3d_map_w * g_s3d_map_h) * 15) <= cell_off)
        return 0;
    for (i = 0; i < 4; i++) {
        b = s3d_map[(int32_t)(int16_t)(cell_off + i + 11)];
        if (b > 0x20) {
            sprintf(g_ui_msg_buf, DGSTR(0x5C53), DGS16(0x86F8), DGS16(0x86FA), DGS16(0x86FC),
                    DGS16(0x86FE));
            o559A_fatal(DGSTR(0x5C64), 0x6C, NULL, 0x51);
        }
        if (b < 10)
            key[g_fr_pc++] = '0';
        sprintf(key + g_fr_pc, "%d", b);                    /* itoa(b, .., 10) */
        g_fr_pc = (uint16_t)(g_fr_pc + strlen(key + g_fr_pc));
    }
    b13 = s3d_map[(int32_t)(int16_t)(cell_off + 13)];
    b14 = s3d_map[(int32_t)(int16_t)(cell_off + 14)];
    mm_ems_lock(g_blit_lock, 0x74, DGSTR(0x5C73));
    v27 = *EMSLock((int16_t)mm_ref(REF_MAP)->ems, 0x27, 1);
    v28 = *EMSLock((int16_t)mm_ref(REF_MAP)->ems, 0x28, 1);
    mm_ems_unlock(g_blit_lock);
    sign = b13 < v27 ? -1 : b13 == v27 ? 0 : 1;
    ev = (uint8_t)(((uint8_t)(b13 - v27) << 5) - sign + b14 - v28);

    if (strcmp(key, DGSTR(0x5C82)) == 0)                    /* "00000000" */
        return 0;
    if (strcmp(key, DGSTR(0x5C8B)) == 0) {                  /* "31313129" weapons */
        if (trigger == 2)
            shop_exchange(0x2B, 0x5C94, 1);
        return 0;
    }
    if (strcmp(key, DGSTR(0x5C9C)) == 0) {                  /* "31313130" goods */
        if (trigger == 2)
            shop_exchange(0x2C, 0x5CA5, 2);
        return 0;
    }
    if (strcmp(key, DGSTR(0x5CB4)) == 0) {                  /* "31313128" food */
        if (trigger == 2)
            shop_food();
        return 0;
    }
    if (strcmp(key, DGSTR(0x5D32)) == 0) {                  /* "31313127" doctor */
        if (trigger == 2)
            shop_doctor();
        return 0;
    }
    if (strcmp(key, DGSTR(0x5E37)) == 0) {                  /* "31313131" */
        if (trigger == 2)
            g_q3d_opt_flags |= 4;
        return 0;
    }

    g_q3d_opt_flags &= (uint16_t)~4;
    g_fr_pc = 0x14;
    if ((int16_t)fetch_word() <= (int16_t)ev) {
        if (sprintf(g_ui_msg_buf, DGSTR(0x5E40), key, ev) >= 0x68)
            o559A_fatal(DGSTR(0x5E56), 0x11C, NULL, 0x53);
        o559A_fatal(DGSTR(0x5E65), 0x11E, NULL, 0x54);
    }
    {
        uint16_t len = fetch_word();

        while (ev-- > 0) {
            g_fr_pc = (uint16_t)(g_fr_pc + len);
            len = fetch_word();
        }
    }
    g_fr_count = (uint8_t)fetch_word();          /* (hi << 8) + lo, stored to a byte */
    if (trigger > 1) {
        uint16_t off;

        g_fr_pc = (uint16_t)(g_fr_pc + (trigger - 2) * 2);
        off = fetch_word();
        g_fr_pc = (uint16_t)(g_fr_pc + off - 2 * trigger);
    } else {
        g_fr_pc = (uint16_t)(g_fr_pc + 6);
    }
    {
        uint8_t h = fr_fetch_byte();

        g_fr_sect_flags = fr_fetch_byte();
        if ((int16_t)(h - 13) != (int16_t)(trigger - 1)) {
            size_t l;

            fatal_printf(DGSTR(0x5E74), h, trigger);
            ui_getch();
            l = strlen(g_ui_msg_buf);
            sprintf(g_ui_msg_buf + l, "%d", (int16_t)g_fr_pc);   /* itoa(BB98, ..) */
            DG8((uint16_t)(0xBACD + strlen(g_ui_msg_buf))) = 0;
            if (strlen(g_ui_msg_buf) >= 0x68)
                o559A_fatal(DGSTR(0x5E8A), 0x142, NULL, 0x55);
            ui_message_box(g_ui_msg_buf, 1, 0);
            ui_wait_click_or_key(0x28);
            o559A_fatal(DGSTR(0x5E99), 0x144, NULL, 0x56);
        }
    }

    for (;;) {
        uint8_t c = g_fr_count--;

        if (!c || !running)
            break;
        op = fr_fetch_byte();
        if (g_fr_debug) {
            fatal_printf(DGSTR(0x5EA8), op);                /* "Komut no:%d" */
            ui_getch();
        }
        switch (op) {
        case 1: {                                   /* if: flag / accumulator */
            uint8_t t = fr_fetch_byte(), a, n, f;

            if (t == 1) {
                a = fr_fetch_byte();
                n = fr_fetch_byte();
                f = fr_flag_test(a);
                TRACE(DGSTR(0x5EB6), a, f ? DGSTR(0x5EC9) : DGSTR(0x5ECF));
                if (!f)
                    fr_skip_commands(n);
            } else if (t == 2) {
                a = fr_fetch_byte();
                n = fr_fetch_byte();
                TRACE(DGSTR(0x5ED7), g_fr_acc, a);
                if (a != g_fr_acc)
                    fr_skip_commands(n);
            } else {
                o559A_fatal(DGSTR(0x5EEB), 0x162, NULL, 0x57);
            }
            break;
        }
        case 2: {                                   /* message */
            char *s;
            uint8_t lines;

            ui_restore_background();
            g_ui_msg_lines = 0;
            s = fr_fetch_string(fr_fetch_byte());
            lines = ui_message_box(s, 1, 0);
            if (fr_fetch_byte() != 2 + 3)
                ui_wait_click_or_key((int16_t)(lines * 100 + 0x8C));
            g_fr_pc--;
            bc_farfree(s);
            break;
        }
        case 3:                                     /* icon */
            fr_show_icon(fr_fetch_byte(), 1);
            g_fr_view_force = 1;
            break;
        case 4:                                     /* end */
            ui_restore_background();
            running = 0;
            break;
        case 5: {                                   /* yes/no dialog */
            uint8_t l1 = fr_fetch_byte(), l2 = fr_fetch_byte();
            char *s1 = fr_fetch_string(l1);
            char *s2 = fr_fetch_string(l2);

            ui_dialog_2198(s1, s2);
            bc_farfree(s2);
            bc_farfree(s1);
            break;
        }
        case 6:                                     /* goto (forward) */
            fr_skip_commands(fr_fetch_byte());
            break;
        case 7: {                                   /* set flag */
            uint8_t f = fr_fetch_byte();

            fr_flag_set(f);
            TRACE(DGSTR(0x5EFA), f);
            break;
        }
        case 8:                                     /* battle */
            fr_fetch_byte();
            for (i = 1; i <= 3; i++) {
                g_s3d_grp_count(i) = 0;
                g_s3d_grp_type(i) = 0;
            }
            DGS16(0x5BE0) = fr_fetch_byte();
            DGS16(0x5BE2) = fr_fetch_byte();
            DGS16(0x5BE6) = fr_fetch_byte();
            DGS16(0x5BE8) = fr_fetch_byte();
            DGS16(0x5BEC) = fr_fetch_byte();
            DGS16(0x5BEE) = fr_fetch_byte();
            if (!DGS16(0x5BE2) && !DGS16(0x5BE0) && !DGS16(0x5BE8) && !DGS16(0x5BE6) &&
                !DGS16(0x5BEE) && !DGS16(0x5BEC)) {
                for (i = 1; i <= 3; i++) {
                    mm_ems_lock(g_blit_lock, 0x1A7, DGSTR(0x5F04));
                    g_s3d_grp_type(i) = *EMSLock((int16_t)mm_ref(REF_MAP)->ems,
                                                 (int32_t)(bc_random(8) + 0x1C), 1);
                    mm_ems_unlock(g_blit_lock);
                    g_s3d_grp_count(i) = (int16_t)(bc_random(8) + 1);
                }
            }
            for (i = 3; i >= 1; i--) {
                g_s3d_grp_chk_type(i) = g_s3d_grp_type(i);
                g_s3d_grp_chk_count(i) = g_s3d_grp_count(i);
                g_s3d_grp_chk_on(i) = g_s3d_grp_on(i);
            }
            ui_wait_click_or_key(0x28);
            q3d_free_view_buffers();
            check_groups(0x5F13);
            gr_fade(1000, (int16_t)-g_pal_level, (int16_t)-g_music_level);
            if (g_sound_on)
                stop_music();
            check_groups(0x5F5A);
            iso_battle();
            s3d_load_district(g_s3d_district, 0);
            s3d_enter_view();
            g_fr_view_force = 0;
            for (i = 1; i < 0x14; i++) {
                int16_t j;

                for (j = 1; j < 0x1D; j++)
                    DGS16((uint16_t)(0xA088 + 0x28 * j + 2 * i)) = 0;
                for (j = 1; j < 8; j++)
                    DGS16((uint16_t)(0x9F18 + 0x2E * j + 2 * i)) = 0;
            }
            for (i = 1; i < 4; i++) {
                g_s3d_grp_count(i) = 0;
                g_s3d_grp_type(i) = 0;
                g_s3d_grp_on(i) = 0;
                g_s3d_grp_chk_count(i) = 0;
                g_s3d_grp_chk_type(i) = 0;
                g_s3d_grp_chk_on(i) = 0;
            }
            for (i = 1; i < 5; i++)
                if (FR_STAT(i, FR_STAT_HP) > 0)
                    break;
            if (DGS16(0x632E) == 2 || i == 5) {             /* lost / all dead */
                demo_game_over();
                g_fr_view_force = 0;
                g_fr_cancel_move = 1;
                running = 0;
            }
            break;
        case 9:                                     /* item exchange (treasure) */
            g_fr_xchg_a = fr_fetch_byte();
            g_fr_xchg_b = fr_fetch_byte();
            g_fr_xchg_c = fr_fetch_byte();
            g_fr_xchg_w1 = (int16_t)fetch_word();
            g_fr_xchg_w2 = (int16_t)fetch_word();
            g_fr_xchg_w3 = (int16_t)fetch_word();
            fr_item_exchange(0);
            break;
        case 10: {                                  /* one of two messages */
            uint8_t l1, l2, lines;
            char *s1, *s2;

            ui_restore_background();
            l1 = fr_fetch_byte();
            s1 = fr_fetch_string(l1);
            l2 = fr_fetch_byte();
            s2 = fr_fetch_string(l2);
            lines = ui_message_box(bc_random(2) ? s1 : s2, 1, 0);
            if (fr_fetch_byte() != 5)
                ui_wait_click_or_key((int16_t)(lines * 100 + 100));
            g_fr_pc--;
            bc_farfree(s2);
            bc_farfree(s1);
            break;
        }
        case 11: {                                  /* teleport */
            uint8_t d = fr_fetch_byte(), f, lvl;
            uint16_t pos = fetch_word();

            f = (uint8_t)(fr_fetch_byte() % 4);
            ui_restore_background();
            if (d < 100) {
                if ((int16_t)(d - 1) != g_s3d_district) {
                    lvl = (uint8_t)g_music_level;
                    gr_fade(500, 0, (int16_t)-g_music_level);
                    if (DG8(0xBB36) && g_sound_on)
                        stop_music();
                    g_s3d_district = (int16_t)(d - 1);
                    s3d_load_district(g_s3d_district, 1);
                    if (g_sound_on && DG8(0xBB36))
                        demo_play_music(g_music_track, 1, 0);
                    gr_fade(500, 0, lvl);
                }
                g_s3d_y = (int16_t)(pos / g_s3d_map_w);
                g_s3d_x = (int16_t)(pos % g_s3d_map_w - 1);
                g_s3d_facing = (uint8_t)(f % 4);
                q3d_alloc_view_buffers();
            } else if (d == 100) {
                s3d_leave_district();
            }
            s3d_apply_daylight();
            s3d_redraw_screen();
            g_fr_view_force = 1;
            g_fr_cancel_move = 1;
            running = 0;
            ret = 1;
            break;
        }
        case 12: {                                  /* scale a stat by percent */
            uint8_t who = fr_fetch_byte(), f = fr_fetch_byte(), p = fr_fetch_byte();

            if (f == FR_STAT_HP && p < 100 && g_sound_on)
                ui_load_voice(9, (uint32_t)DG16(0x2381), 0x1F4, 0);
            if (who == 0) {
                for (who = 1; who < 5; who++) {
                    if (f == FR_STAT_HP)
                        FR_STAT(who, f) += 5;
                    FR_STAT(who, f) = (int16_t)((int16_t)(FR_STAT(who, f) * p) / 100);
                    if (f == FR_STAT_HP)
                        FR_STAT(who, f) -= 5;
                }
            } else if (who == 5) {
                who = (uint8_t)g_fr_who;
                if (f == FR_STAT_HP)
                    FR_STAT(who, f) += 5;
                FR_STAT(who, f) = (int16_t)((int16_t)(FR_STAT(who, f) * p) / 100);
                if (f == FR_STAT_HP)
                    FR_STAT(who, f) -= 5;
                g_s3d_ba89(who) = 1;
            }
            s3d_draw_party_panel();
            break;
        }
        case 13: case 14: case 15:                  /* unused, 1 operand */
            fr_fetch_byte();
            break;
        case 16:                                    /* stop, cancel the move */
            g_fr_cancel_move = 1;
            running = 0;
            break;
        case 17: {                                  /* a member speaks */
            uint8_t c;
            char *s;

            ui_restore_background();
            c = fr_fetch_byte();
            TRACE(DGSTR(0x5FA1), c, g_fr_who);
            s = fr_fetch_string(fr_fetch_byte());
            if (c == 0)
                c = (uint8_t)(bc_random(4) + 1);
            if (c == 5) {
                if (g_fr_who <= 4)
                    c = (uint8_t)g_fr_who;
                else
                    o559A_fatal(DGSTR(0x5FB7), 0x26F, NULL, 0x58);
            }
            ui_message_box(s, 0, c);
            bc_farfree(s);
            break;
        }
        case 18:                                    /* ACC = !(3D5E & 1) */
            g_fr_acc = (g_q3d_opt_flags & 1) ? 0 : 1;
            break;
        case 19: {                                  /* play a GFL film */
            uint8_t f = fr_fetch_byte();

            if (DGS16(0x3D5C) == 1) {
                for (;;) {
                    uint8_t ok = 0, k;

                    ui_restore_background();
                    for (k = 0; k < 3; k++)
                        ok |= (uint8_t)o5737_cd_vtoc_ok();
                    if (ok) {
                        ui_restore_background();
                        break;
                    }
                    ui_wait_click_or_key((int16_t)(ui_message_box(DGSTR(0x5FC6), 1, 0) * 0x46 +
                                                   0x8C));
                    gr_cursor_hide();
                    k = (uint8_t)ui_getch();
                    gr_cursor_set_shape((uint8_t)g_cursor_shape);
                    if (k == 0x1B) {
                        ui_restore_background();
                        ui_wait_click_or_key(
                            (int16_t)(ui_message_box(DGSTR(0x6042), 1, 0) * 0x46 + 0x8C));
                        ui_restore_background();
                        goto next;
                    }
                }
            }
            q3d_free_view_buffers();
            if (f != 2) {
                gr_fade(1000, (int16_t)-g_pal_level, (int16_t)-g_music_level);
                if (g_sound_on)
                    stop_music();
            } else {
                gr_fade(1000, (int16_t)-g_pal_level, 0);
            }
            memcpy(DGP(0x962C), DGP(0x986C), 0x48);
            ui_init_voices();
            memcpy(DGP(0x962C), DGP(0x9824), 0x48);
            gfl_play(DGSTR((uint16_t)(0x2D6E + 13 * f)));
            g_pal_level = 0x40;
            gr_fade(1000, (int16_t)-g_pal_level, 0);
            ui_init_voices();
            s3d_load_district(g_s3d_district, 0);
            s3d_enter_view();
            g_fr_view_force = 0;
            break;
        }
        case 20:                                    /* ACC = random(n) + 1 */
            fatal_printf(DGSTR(0x6074));
            ui_getch();
            g_fr_acc = (int16_t)(bc_random(fr_fetch_byte()) + 1);
            break;
        case 21: {                                  /* menu of 6 */
            char *items[6];
            int16_t k;

            for (k = 0; k < 6; k++)
                items[k] = fr_fetch_string(fr_fetch_byte());
            g_fr_acc = fr_menu(items);
            for (k = 5; k >= 0; k--)
                bc_farfree(items[k]);
            break;
        }
        case 22: {                                  /* select a member by stat */
            uint8_t b1 = fr_fetch_byte(), b2 = fr_fetch_byte(), b3 = fr_fetch_byte();
            int16_t r = fr_find_character(b3, b2, b1);

            if (r < 5)
                g_fr_who = r;
            else
                running = 0;
            break;
        }
        case 23: {                                  /* select lowest/highest */
            uint8_t b1 = fr_fetch_byte(), b2 = fr_fetch_byte();

            g_fr_who = fr_find_character(0, b1, b2);
            TRACE(DGSTR(0x6097), b1, b2, g_fr_who);
            break;
        }
        case 24:                                    /* the player picks a member */
            ui_restore_background();
            ui_message_box(DGSTR(0x60AD), 1, 0);
            for (;;) {
                g_fr_who = ui_poll_input(1);
                if (g_fr_who == 0)
                    continue;                       /* cancelled: ask again */
                if (FR_STAT(g_fr_who, FR_STAT_HP) > 0)
                    break;
                ui_restore_background();
                ui_message_box(DGSTR(0x60C1), 1, 0);
            }
            break;
        case 25: {                                  /* music */
            uint8_t lvl = 0;

            ui_restore_background();
            if (g_sound_on && g_music_module) {
                lvl = (uint8_t)g_music_level;
                gr_fade(1000, 0, (int16_t)-g_music_level);
                if (g_sound_on)
                    stop_music();
            }
            g_music_track = fr_fetch_byte();
            if ((g_music_track == 1 && DG8(0xBB36) == 0) ||
                (DG8(0xBB36) == 0 && DG8(0xBB38) == 0)) {
                if (g_sound_on)
                    gr_fade(0, 0, lvl);
                break;
            }
            demo_play_music(g_music_track, 1, 0);
            if (g_music_track == 1) {
                uint8_t s = DG8(0x3D6D);

                g_q3d_opt_flags = 0x20;
                gr_fade(100, g_fade_bright_d, (int16_t)-g_music_level);
                g_s3d_fade_tempo = 0;
                g_fade_volume_d = 0;
                g_fade_bright_d = 0;
                s3d_apply_daylight();
                g_fade_volume_d = (int16_t)(g_fade_volume_d + g_music_level_max - 0x1E);
                gr_fade(100, 0, g_fade_volume_d);
                g_sfx_level = (int16_t)(g_sfx_level_max + g_s3d_fade_tempo);
                if (g_sound_on)
                    ampSetTempo((int32_t)(int16_t)(g_sfx_level << 8));
                g_fade_volume_d = 0;
                g_s3d_fade_tempo = 0;
                g_fade_bright_d = 0;
                DG8(0x3D6D) = s;
            } else {
                gr_fade(500, 0,
                        (int16_t)(g_music_level_max + (g_music_track == 14 ? 0x14 : 0) - 0x1E));
            }
            break;
        }
        case 26: {                                  /* pay */
            uint8_t m = fr_fetch_byte();

            if (m == 0xFF) {
                g_fr_money = 0;
                DGS16(0xBB71) = 0;
                DGS16(0xBB6F) = 0;
                fr_draw_money();
            }
            if ((int16_t)m <= g_fr_money) {
                g_fr_acc = 1;
                g_fr_money = (int16_t)(g_fr_money - m);
                fr_draw_money();
            } else {
                g_fr_acc = 0;
            }
            break;
        }
        case 27: {                                  /* story scene (40 unused texts) */
            int16_t k;

            gr_fade(1000, (int16_t)-g_pal_level, (int16_t)-g_music_level);
            ui_restore_background();
            if (g_sound_on)
                stop_music();
            q3d_free_view_buffers();
            ctl_free_checked(REF_MAP, 0x32F, DGSTR(0x60E1));
            demo_story_scene(0x0B);
            s3d_load_district(g_s3d_district, 0);
            s3d_enter_view();
            for (k = 0; k < 0x28; k++) {
                uint8_t len = fr_fetch_byte();      /* PC++ before the add */

                g_fr_pc = (uint16_t)(g_fr_pc + len);
            }
            break;
        }
        case 28:                                    /* teach an aphorism */
            fr_teach_aphorism();
            break;
        case 29: {                                  /* remove an item from the party */
            uint16_t w;
            int16_t c, s, k;

            fr_fetch_byte();
            w = fetch_word();
            for (c = 1; c < 5; c++)
                for (s = 1; s < 0x0B; s++)
                    if (FR_STAT(c, 0x22 + s) == (int16_t)w) {
                        FR_STAT(c, 0x22 + s) = 0;
                        for (k = 1; k < 4; k++)
                            if (FR_STAT(c, 0x1D + k) == s + 0x22)
                                FR_STAT(c, 0x1D + k) = 0;
                    }
            break;
        }
        case 30: {                                  /* password */
            uint8_t l = fr_fetch_byte(), n = fr_fetch_byte(), y;
            char *s = grt_decode_turkish_lower(fr_fetch_string(l));
            char *buf;
            uint16_t k;

            for (k = 0; k < strlen(s); k++)
                grt_strip_accent_inplace(&s[k]);
            buf = bc_farmalloc(0x32);
            buf[0] = 0;                             /* sprintf(buf, "") (60F0) */
            if (g_ui_msg_lines < 4)
                y = (uint8_t)(g_ui_panel_y + (g_ui_msg_lines + 1) * 8);
            else
                y = (uint8_t)(g_ui_panel_y + g_ui_msg_lines * 8);
            if (mm_ref(0xE357) == NULL)
                ui_save_background();
            if (mm_ref(0xE557) != NULL)
                o559A_fatal(DGSTR(0x60F1), 0x397, NULL, 0x5C);
            mm_change_type(mm_ref(0xE357), 1);
            g_surf_w(0x57) = (int16_t)(l * 14 + 15);
            g_surf_h(0x57) = 12;
            mm_ref_set(0xE557, mm_alloc((uint32_t)(int32_t)(int16_t)(g_surf_w(0x57) *
                                                                     g_surf_h(0x57)), 0));
            sys_blit(3, y, (uint16_t)g_surf_w(0x57), (uint16_t)g_surf_h(0x57), 0, 0, 0, 0x57,
                     sys_rect_copy);
            grt_input_line(buf, 1, g_ui_col_initial, 0, (int16_t)(l * 14), 4, y, 0, 1, 0, 0x57);
            ctl_free_checked(0xE557, 0x39E, DGSTR(0x6100));
            mm_change_type(mm_ref(0xE357), 0);
            for (k = 0; k < strlen(buf); k++)
                grt_strip_accent_inplace(&buf[k]);
            if (strcmp(buf, s) != 0)
                fr_skip_commands(n);
            bc_farfree(s);
            bc_farfree(buf);
            break;
        }
        case 31:                                    /* wait */
            fatal_printf(DGSTR(0x610F));
            ui_getch();
            ui_wait_click_or_key((int16_t)(fr_fetch_byte() * 0x32));
            break;
        case 32:                                    /* redraw, stop */
            fr_redraw();
            running = 0;
            break;
        case 33:                                    /* leave the district */
            s3d_leave_district();
            ui_restore_background();
            running = 0;
            break;
        default:
            if (sprintf(g_ui_msg_buf, DGSTR(0x6130), op) >= 0x68)
                o559A_fatal(DGSTR(0x6133), 0x3BE, NULL, 0x5D);
            break;
        }
next:;
    }
    ui_restore_background();
    s3d_redraw_view(g_fr_view_force);
    return ret;
}

/*
 * fr_redraw - put the party back on its previous cell (BB4F/BB51) and
 * redraw the view.
 *
 * Original: IST.EXE ovl3FD1:20CE (FRPOS.L), far cdecl.
 * Verified: tests/py/test_frpos.py (unicorn differential), 2026-11-21.
 */
void fr_redraw(void)
{
    g_s3d_x = g_s3d_prev_x;
    g_s3d_y = g_s3d_prev_y;
    s3d_redraw_view(1);
    s3d_apply_daylight();
}

/*
 * fr_teach_aphorism - script op 28: the wisest member (highest word 5)
 * is offered up to six random aphorisms (table 361h, 31 bytes each) of
 * his level (level table 0C88h); the chosen one is marked known ('1' in
 * the name record 9C94h + 94h * who) for a living member the player picks.
 * A pick already known by the wise member (flag 195Bh + 23h * who) is
 * drawn again; the level count k may make this loop forever (k <= 2), as
 * in the original.
 *
 * Original: IST.EXE ovl3FD1:2104 (FRPOS.L), far cdecl.  Byte counters:
 * the duplicate scan j = i-1 .. 0 ends when j wraps to 0FFh.
 * Verified: tests/py/test_frpos.py (unicorn differential), 2026-11-21.
 */
void fr_teach_aphorism(void)
{
    uint8_t who = (uint8_t)fr_find_character(0, 1, 5);
    int16_t lvl = FR_STAT(who, 5);
    int16_t k = 1;
    uint8_t picks[6];
    uint8_t i, j;
    int16_t chosen;

    while (k < 0x23 && DGS16((uint16_t)(0x0C88 + 2 * k)) <= lvl)
        k++;
    k--;
    for (i = 0; i < 6; i++) {
        picks[i] = (uint8_t)(bc_random((int16_t)(k - 1)) + 2);
        for (j = (uint8_t)(i - 1); j <= i; j--)
            if (picks[i] == picks[j]) {
                i--;                                /* duplicate: pick again */
                break;
            }
        if (j == 0xFF && DG8((uint16_t)(0x195B + who * 0x23 + picks[i])) == '1')
            i--;                                    /* already known: again */
    }
    for (i = 0; i < 6; i++)
        strcpy(DGSTR(UI_LIST_ENTRY(i + 1)), DGSTR((uint16_t)(0x361 + 0x1F * picks[i])));
    for (;;) {
        ui_event_loop(6, 2, -1, g_ui_col_initial, DG8(0x9664), DG8(0x9666), 4, 1);
        if (g_ui_list_sel != 0)
            break;
        ui_restore_background();
        ui_message_box(DGSTR(0x6142), 1, 0);
        ui_dialog_2198(DGSTR(0x6167), DGSTR(0x6177));
        if (g_fr_acc == 0)
            break;
    }
    gr_zones_clear();
    s3d_draw_party_panel();
    if (g_ui_list_sel == 0) {
        ui_restore_background();
        ui_message_box(DGSTR(0x619A), 1, 0);
        ui_wait_click_or_key(0x8C);
        ui_restore_background();
        return;
    }
    chosen = picks[g_ui_list_sel - 1];
    for (;;) {
        uint8_t p;

        ui_restore_background();
        ui_message_box(DGSTR(0x618A), 1, 0);
        p = ui_poll_input(1);
        if (p == 0) {
            ui_restore_background();
            ui_message_box(DGSTR(0x619A), 1, 0);
            ui_wait_click_or_key(0x8C);
            break;
        }
        if (FR_STAT(p, FR_STAT_HP) <= 0) {
            ui_restore_background();
            ui_message_box(FR_STAT(p, FR_STAT_HP) <= -5 ? DGSTR(0x61BE) : DGSTR(0x61D1), 1, 0);
            ui_wait_click_or_key(0x8C);
            continue;
        }
        DG8((uint16_t)(0x9C94 + p * 0x94 + chosen)) = '1';
        break;
    }
    ui_restore_background();
}
