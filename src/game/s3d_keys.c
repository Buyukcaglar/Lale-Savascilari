/*
 * s3d_keys.c - 3DSYS.L part 2 (IST.EXE ovl303F:0BDB-1926): the location
 * report, the exploration key/mouse dispatcher and saving the map.  See
 * s3d.h.
 */
#include "game/s3d.h"

#include <stdio.h>
#include <string.h>

#include "dsmi/amp.h"
#include "dsmi/emsheap.h"
#include "dsmi/memalloc.h"
#include "engine/fatal.h"
#include "engine/fileman.h"
#include "engine/intrface.h"
#include "engine/memman.h"
#include "engine/ngraphs.h"
#include "engine/nkontrol.h"
#include "engine/systems.h"
#include "engine/vfs.h"
#include "game/s3d_ext.h"
#include "rtl/bc_farheap.h"

/* district table (16E0:2E31, 29 bytes per district): far char * at +4 =
   the district's name in the genitive ("mecidiyekOy'Un") */
#define DISTRICT_GENITIVE(d) DGSTR(DG16(0x2E35 + 0x1D * (uint16_t)(d)))

/*
 * s3d_show_location - the location report ("Şu anda <district>'ın
 * kuzeybatısındayız.").  The map is cut into thirds each way.  With event
 * flag 52h set (the party owns a city map) the district's map picture
 * har<n>.cmp/.pal is shown first (faded in, until a click or 20 s).
 *
 * Original: IST.EXE 303F:0BDB (3DSYS.L), far cdecl.  The text buffer is a
 * 68h-byte copy of 3E74; the name "har?  " at 3F41 gets the district
 * number written into it (itoa at 3F44) and stays so.  The current window
 * record (962C) is saved and restored around everything.
 * Verified: tests/py/test_s3d_district.py (test_show_location).
 */
void s3d_show_location(void)
{
    char text[0x68];
    uint8_t win[UI_WIN_SIZE];
    uint8_t shown = 0;
    uint8_t mouse = 0;
    char *name = DGSTR(0x3F41);                 /* "har?" */
    char *path;
    int16_t fd;
    const char *where;

    memcpy(text, DGSTR(0x3E74), sizeof text);  /* "\n\n Su anda " */
    memcpy(win, DGP(UI_WIN_CUR), UI_WIN_SIZE);
    ui_restore_background();
    sprintf(name + 3, "%d", (int)(int16_t)(g_s3d_district + 1));
    if (fr_flag_test(0x52)) {
        path = fm_change_ext(name, DGSTR(0x3F49));          /* "cmp" */
        fd = vfs_open(path, 2);
        if (fd == -1) {
            bc_farfree(path);
        } else {
            shown = 1;
            memcpy(DGP(UI_WIN_CUR), DGP(UI_WIN_REC(1)), UI_WIN_SIZE);
            vfs_close(fd);
            fm_load_cmp(1, path, 0x57);
            bc_farfree(path);
            gr_fade(0xA0, (int16_t)-g_pal_level, 0);
            path = fm_change_ext(name, DGSTR(0x3F4D));      /* "pal" */
            fm_load_pal(path, 0, 0x100);
            bc_farfree(path);
            mm_ems_lock(g_blit_lock, 0x1AD, DGSTR(0x3F51));
            sys_build_aa_table(EMSLock((int16_t)mm_ref(0xE52F)->ems, 0, 0x7F80), 0);
            mm_ems_unlock(g_blit_lock);
            gr_cursor_hide();
            mouse = g_mouse_present;
            g_mouse_present = 0;
            mm_copy_block(mm_ref(0xE557), mm_ref(0xE29F));  /* picture 57h -> screen */
            ctl_free_checked(0xE557, 0x1B3, DGSTR(0x3F60));
            gr_fade(0xA0, (int16_t)(g_pal_level_max - g_pal_level), 0);
            ui_wait_click_or_key(2000);
        }
    }
    strcat(text, DISTRICT_GENITIVE(g_s3d_district));
    strcat(text, DGSTR(0x3F6F));                            /* "\n\n" + 40 spaces */
    if (g_s3d_x < g_s3d_map_w / 3) {
        if (g_s3d_y < g_s3d_map_h / 3)
            where = DGSTR(0x3F9A);                          /* " kuzeybatIsIndayIz." */
        else if (g_s3d_y > g_s3d_map_h - g_s3d_map_h / 3)
            where = DGSTR(0x3FAE);                          /* " gUneybatIsIndayIz." */
        else
            where = DGSTR(0x3FC2);                          /* " batIsIndayIz." */
    } else if (g_s3d_x > g_s3d_map_w - g_s3d_map_w / 3) {
        if (g_s3d_y < g_s3d_map_h / 3)
            where = DGSTR(0x3FD1);                          /* " kuzeydoGusundayIz." */
        else if (g_s3d_y > g_s3d_map_h - g_s3d_map_h / 3)
            where = DGSTR(0x3FE5);                          /* " gUneydoGusundayIz." */
        else
            where = DGSTR(0x3FF9);                          /* " doGusundayIz." */
    } else {
        if (g_s3d_y < g_s3d_map_h / 3)
            where = DGSTR(0x4008);                          /* " kuzeyindeyiz." */
        else if (g_s3d_y > g_s3d_map_h - g_s3d_map_h / 3)
            where = DGSTR(0x4017);                          /* " gUneyindeyiz." */
        else
            where = DGSTR(0x4026);                          /* " ortasInda bir yerdeyiz." */
    }
    strcat(text, where);
    strcat(text, DGSTR(0x403F));                            /* ".." */
    ui_message_box(text, 1, 0);
    ui_wait_click_or_key(0x960);
    ui_restore_background();
    if (shown) {
        gr_fade(0xA0, (int16_t)-g_pal_level, 0);
        g_mouse_present = mouse;
        s3d_enter_view();
    }
    memcpy(DGP(UI_WIN_CUR), win, UI_WIN_SIZE);
}

/* "Cit mod'da seyv edemezsiniz!" etc.: message, 3 s, gone */
static void short_message(uint16_t text)
{
    ui_message_box(DGSTR(text), 1, 0);
    ui_wait_click_or_key(300);
    ui_restore_background();
}

/*
 * s3d_handle_key - one command of the exploration screen, from the mouse
 * (button 1 on a panel zone) or the keyboard (if a key is waiting).
 *
 * Mouse zones: 1-4 portraits (as F1-F4), 5 location report, 6-9 the arrows
 * (as the cursor keys), 10 options, 11 save/load, 12 camp.
 * Extended keys (scan code, switch table 303F:186F):
 *   F1-F4    character screen of member 1-4
 *   Up       step forward         Down  step back
 *   Left     turn left            Right turn right
 *   PgUp     low detail (100x100 view, 9 animation steps)
 *   PgDn     high detail (200x200 view, no animations)
 * then Ctrl+G (scan 22h with Ctrl held) toggles into the cheat mode
 * ("Cit mod", 3E72: 2 -> 3, the party gets cheat values).  ASCII keys
 * (table 303F:182F): Esc quit, 8/7/9 forward/turn left/turn right, 2 or 5
 * back, 4/6 side steps, K camp, N location, S save/load, T options.
 * The result for the main loop is g_s3d_view_dirty: the s3d_redraw_view
 * mode of the move made (0 = nothing to animate).
 *
 * Original: IST.EXE 303F:0FFC (3DSYS.L), far cdecl.
 * Verified: tests/py/test_s3d_keys.py (test_handle_key).
 */
void s3d_handle_key(void)
{
    uint8_t opt_arg = 1;                        /* [bp-7] options from the panel */
    uint8_t level = 0;                          /* [bp-8] palette level / options arg */
    uint8_t ascii = 0;                          /* [bp-9] look the key up as ASCII */
    uint16_t key;
    int16_t zone;
    int16_t scan;

    if (g_mouse_buttons == 1) {
        zone = gr_zone_under_cursor();
        if (zone > 0 && zone < 5) {
            key = (uint16_t)((zone + 0x3A) << 8);
            gr_wait_buttons_up();
        } else if (zone == 5) {
            key = 0;
            gr_wait_buttons_up();
            s3d_show_location();
        } else if (zone >= 6 && zone <= 9) {
            static const uint16_t arrows[4] = { 0x4B00, 0x4800, 0x4D00, 0x5000 };
            key = arrows[zone - 6];
            gr_wait_buttons_up();
        } else if (zone == 10) {
            key = 0;
            gr_wait_buttons_up();
            s3d_options_menu(opt_arg);
        } else if (zone == 11) {
            key = 0;
            gr_wait_buttons_up();
            if (DGS16(0x3E72) == 3)
                short_message(0x4042);          /* "Cit mod'da seyv edemezsiniz!" */
            else
                s3d_save_load_menu(1);
        } else if (zone == 12) {
            key = 0;
            gr_wait_buttons_up();
            s3d_camp();
        } else {
            key = 0;
        }
    } else {
        if (!ui_kbhit())
            return;
        key = (uint16_t)ui_bioskey(0);
        ctl_flush_keys();
    }

    scan = (int16_t)key >> 8;
    if (key & 0xFF) {
        ascii = 1;
    } else {
        switch ((uint16_t)(scan - 0x3B)) {
        case 0x00: case 0x01: case 0x02: case 0x03:         /* F1-F4 (303F:1215) */
            g_party_cur = (int16_t)(scan - 0x3A);
            q3d_free_anim_buffer(0x0F);
            level = (uint8_t)g_pal_level;
            gr_fade(100, (int16_t)-g_pal_level, 0);
            memcpy(DGP(UI_WIN_CUR), DGP(UI_WIN_REC(4)), UI_WIN_SIZE);
            s3d_character_screen();
            memcpy(DGP(UI_WIN_CUR), DGP(UI_WIN_REC(6)), UI_WIN_SIZE);
            q3d_wait_retrace(3);
            gr_cursor_hide();
            g_cursor_shape = g_ui_shape;
            mm_copy_block(mm_ref(0xE51F), mm_ref(0xE29F));  /* interface 50h -> screen */
            g_q3d_opt_flags |= 0x10;
            s3d_draw_party_panel();
            g_q3d_anim_mode = 0;
            q3d_alloc_view_buffers();
            s3d_redraw_view(1);
            s3d_draw_status_line(0xC6, 0xB2);
            fr_draw_money();
            g_q3d_opt_flags &= 0xFFEF;
            gr_fade(100, level, 0);
            g_s3d_view_dirty = 0;
            break;
        case 0x0D:                                          /* Up (303F:114D) */
            if (s3d_try_move(g_s3d_step_dx, g_s3d_step_dy, g_s3d_facing))
                g_s3d_view_dirty = 2;
            break;
        case 0x0E: {                                        /* PgUp (303F:130B) */
            int16_t lv;
            g_q3d_win_w = 100;
            g_q3d_win_h = 100;
            DG8(0xBB3A) = 0;
            g_q3d_anim_speed = 9;
            DG8(0xBB34) = 1;
            g_q3d_anim_turn = 1;
            gr_cursor_hide();
            for (int16_t i = 0; i < 4; i++)
                DG8(0xBA89 + 0x11 * i) = 1;
            lv = g_pal_level;
            gr_fade(100, (int16_t)-g_pal_level, 0);
            mm_copy_block(mm_ref(0xE51F), mm_ref(0xE29F));
            g_q3d_opt_flags |= 0x10;
            s3d_draw_status_line(0xC6, 0xB2);
            s3d_draw_party_panel();
            fr_draw_money();
            g_q3d_opt_flags &= 0xFFEF;
            gr_call_then_cursor(s3d_draw_portrait_hl);      /* 1670:0084 */
            s3d_redraw_view(1);
            gr_fade(100, lv, 0);
            g_s3d_view_dirty = 0;
            break;
        }
        case 0x10:                                          /* Left (303F:118B) */
            g_s3d_facing += 3;
            q3d_alloc_view_buffers();
            g_s3d_view_dirty = 5;
            break;
        case 0x12:                                          /* Right (303F:11B3) */
            g_s3d_facing++;
            q3d_alloc_view_buffers();
            g_s3d_view_dirty = 4;
            break;
        case 0x15:                                          /* Down (303F:11D1) */
            if (s3d_try_move((int16_t)-g_s3d_step_dx, (int16_t)-g_s3d_step_dy,
                             (uint8_t)(g_s3d_facing + 2)))
                g_s3d_view_dirty = 3;
            break;
        case 0x16:                                          /* PgDn (303F:1408) */
            g_q3d_win_w = 200;
            g_q3d_win_h = 200;
            DG8(0xBB3A) = 1;
            g_q3d_anim_speed = 0;
            DG8(0xBB34) = 1;
            g_q3d_anim_turn = 1;
            s3d_draw_party_panel();
            g_s3d_view_dirty = 1;
            break;
        default:                                            /* 303F:1454 */
            g_s3d_view_dirty = 0;
            ascii = 1;
            break;
        }
    }

    /* 303F:1468 - Ctrl held: Ctrl+G is the cheat mode, nothing else */
    if ((ui_bioskey(2) & 0x0F) == 4 && ascii) {
        ascii = 0;
        if (scan == 0x22) {
            if (DGS16(0x3E72) == 2) {
                DGS16(0x3E72) = 3;
                fm_load_cmp(1, DGSTR(0x405F), 0x47);        /* "CHEAT.CMP" */
                DG8(0xBA89) = 1;
                DGS16(0x9A0C) = 1000;
                DGS16(0x9A3A) = 1000;
                DGS16(0x9A0E) = 100;
                DGS16(0x9A3C) = 100;
                DGS16(0x9A06) = 1;
                memcpy(DGP(0x9D29), DGP(0x4069), 0x23);     /* aphorisms "111..." */
                strcpy(DGSTR(0x9CBA), DGSTR(0x408F));       /* "bay dum" */
                strcpy(DGSTR(0x9D04), DGSTR(0x4097));       /* "insanüstü" */
                sys_blit(0xE6, 0x12, 0x41, 9, 0xE6, 0x12, 0x50, 0, sys_rect_copy);
                s3d_draw_party_panel();
                short_message(0x40A1);          /* "Cit mod'a hoS geldiniz!" */
            } else if (DGS16(0x3E72) == 3) {
                short_message(0x40B9);          /* "Cit mod'dan CIkamazsInIz!" */
            }
        } else {
            ascii = 1;
        }
        g_s3d_view_dirty = 0;
    }

    if (!ascii)
        return;
    switch (key & 0xFF) {                       /* table 303F:182F */
    case 0x1B:                                  /* Esc: quit the game (303F:15C3) */
        g_s3d_view_dirty = 0;
        ui_message_box(DGSTR(0x40D3), 1, 0);    /* "oyundan cidden CIkIcan mI ?" */
        ui_dialog_2198(DGSTR(0x40F0), DGSTR(0x40F6));       /* "evvet" / "hayyIr" */
        if (g_ui_dialog_result != 0)
            return;
        if (g_sound_on) {
            gr_fade(500, (int16_t)-g_pal_level, (int16_t)-g_music_level);
            if (g_sound_on) {
                ampStopModule();
                if (g_music_module != NULL) {
                    ampFreeModule(g_music_module);
                    MemFree(g_music_module);
                    g_music_module = NULL;
                }
            }
        } else {
            gr_fade(1000, (int16_t)-g_pal_level, 0);
        }
        q3d_free_view_buffers();
        demo_credits_exit();
        return;
    case 'T': case 't':                         /* 303F:169A */
        s3d_options_menu(level);
        break;
    case 'N': case 'n':
        s3d_show_location();
        break;
    case 'K': case 'k':
        s3d_camp();
        break;
    case 'S': case 's':
        if (DGS16(0x3E72) == 3)
            short_message(0x40FD);              /* "Cit mod'da seyv edemezsiniz!" */
        else
            s3d_save_load_menu(1);
        break;
    case '7':                                   /* turn left (303F:16EC) */
        g_s3d_facing += 3;
        q3d_alloc_view_buffers();
        g_s3d_view_dirty = 5;
        return;
    case '8':                                   /* forward (303F:1714) */
        if (s3d_try_move(g_s3d_step_dx, g_s3d_step_dy, g_s3d_facing))
            g_s3d_view_dirty = 2;
        return;
    case '9':                                   /* turn right (303F:1752) */
        g_s3d_facing++;
        q3d_alloc_view_buffers();
        g_s3d_view_dirty = 4;
        return;
    case '4':                                   /* step left (303F:1770) */
        if (s3d_try_move(g_s3d_step_dy, (int16_t)-g_s3d_step_dx, (uint8_t)(g_s3d_facing + 3)))
            g_s3d_view_dirty = 1;
        return;
    case '6':                                   /* step right (303F:17A5) */
        if (s3d_try_move((int16_t)-g_s3d_step_dy, g_s3d_step_dx, (uint8_t)(g_s3d_facing + 1)))
            g_s3d_view_dirty = 1;
        return;
    case '2': case '5':                         /* back (303F:17E2) */
        if (s3d_try_move((int16_t)-g_s3d_step_dx, (int16_t)-g_s3d_step_dy,
                         (uint8_t)(g_s3d_facing + 2)))
            g_s3d_view_dirty = 3;
        return;
    default:
        break;
    }
    g_s3d_view_dirty = 0;                       /* 303F:1821 */
}

/*
 * s3d_load_file_error - (misnamed: saves the map) write the district
 * map's bank back to file `name`: map_w * map_h * 15 bytes (16-bit
 * product) starting 2Bh bytes before the cells.  A file that cannot be
 * created is fatal ("Dosya açılamadı!").  Unreferenced debug helper.
 *
 * Original: IST.EXE 303F:189D (3DSYS.L), far cdecl (char far *name).
 * Not compared yet (no test_save_map in tests/py/test_s3d_keys.py).
 */
void s3d_load_file_error(const char *name)
{
    int16_t fd = vfs_open(name, 4);

    if (fd == -1)
        o559A_fatal(DGSTR(0x411A), 0x457, DGSTR(0x4129), -1);
    mm_ems_unlock(g_blit_lock);
    vfs_write(fd, s3d_map - 0x2B,
              (int32_t)(uint16_t)((uint16_t)(g_s3d_map_w * g_s3d_map_h) * 15));
    vfs_close(fd);
}
