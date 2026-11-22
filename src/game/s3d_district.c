/*
 * s3d_district.c - 3DSYS.L part 3 (IST.EXE ovl303F:1927-2C68): loading a
 * district (map, script, wall sets, floors/ceilings, sky), the
 * exploration screen set-up and the mouse zones.  See s3d.h.
 *
 * Surfaces loaded here: 18h-1Bh wall textures (wall sets of the map
 * header bytes 3..6), 1Ch-1Fh their door textures, 20h-23h decorations,
 * 24h-37h floors (yerN.cmp; empty names reuse an earlier slot), 38h-3Fh
 * ceilings (tavN.cmp), 40h the sky (gotyN.cmp, N = night), 4Fh the
 * district's horizon picture.
 */
#include "game/s3d.h"

#include <stdio.h>
#include <string.h>

#include "dsmi/amp.h"
#include "dsmi/emsheap.h"
#include "engine/blit386.h"
#include "engine/fatal.h"
#include "engine/fileman.h"
#include "engine/intrface.h"
#include "engine/memman.h"
#include "engine/ngraphs.h"
#include "engine/nkontrol.h"
#include "engine/ographs.h"
#include "engine/systems.h"
#include "game/s3d_ext.h"
#include "rtl/bc_rand.h"

/* district table 16E0:2E31, 1Dh bytes per district: far char * name (+0),
   genitive (+4), four GFL names (+8..+14h), horizon picture (+18h) */
#define DISTRICT_PTR(d, field) DG16(0x2E31 + 0x1D * (uint16_t)(d) + (field))
#define DISTRICT_SEG(d, field) DG16(0x2E33 + 0x1D * (uint16_t)(d) + (field))

#define REF_MAP_BANK    0xE33F          /* far MmHandle *: the PCn map bank */
#define REF_SCRIPT_BANK 0xE34F          /* far MmHandle *: the PCSn script bank */
#define g_s3d_map_lock  ((MmEmsLock *)DGP(0xBB3B))  /* EMS mapping of the map */

/* copy surface record `from`'s size and block into surface `to` (shared) */
static void alias_surface(uint16_t to, uint16_t from)
{
    g_surf_w(to) = g_surf_w(from);
    g_surf_h(to) = g_surf_h(from);
    g_surf_mm_seg(to) = g_surf_mm_seg(from);
    g_surf_mm_off(to) = g_surf_mm_off(from);
}

/* rebuild the anti-aliasing table (surface 52h) after a palette change */
static void build_aa(int16_t line, uint16_t file)
{
    mm_ems_lock(g_blit_lock, line, DGSTR(file));
    sys_build_aa_table(EMSLock((int16_t)mm_ref(0xE52F)->ems, 0, 0x7F80), 0);
    mm_ems_unlock(g_blit_lock);
}

/*
 * s3d_load_district - load district `district`: its map bank (PCn, name
 * from the district table) and script bank (pcs<g_s3d_district + 1>), the
 * map size, the start position (when `first`: map header bytes 7/8, the
 * previous cell one step inside, a facing away from the nearest border,
 * BBAB = random(100) + 101, the target point BB88/BB8A of districts 1-12),
 * the wall sets and their pictures, floors, ceilings, the sky for the time
 * of day and the horizon picture.
 *
 * s3d_map first points at the bank's byte 14h (header: width, height,
 * BA86, the 4 wall sets, start x/y); at the end it is re-mapped to byte
 * 2Bh, where cell 0 of the 15-byte cell grid lies (EMSLock maps the
 * block into the page frame; the mapping is saved in the lock context
 * BB3B).
 *
 * Original: IST.EXE 303F:1927 (3DSYS.L), far cdecl (int district, char
 * first).  The map name comes from the parameter, the script name from
 * g_s3d_district (the callers set both).  calc_daylight is called with a
 * stray argument (the slot of the following fm_load_cmp).
 * Verified: tests/py/test_s3d_district.py (test_load_district).
 */
void s3d_load_district(int16_t district, uint8_t first)
{
    char name[14];
    int16_t i;

    if (g_q3d_opt_flags & 0x20)
        q3d_free_view_buffers();
    sys_load_rix(DGSTR(0x413A));                            /* "xd.rix" */
    if (mm_ref(REF_MAP_BANK) != NULL)
        ctl_free_checked(REF_MAP_BANK, 0x478, DGSTR(0x4141));
    mm_ref_set(REF_MAP_BANK,
               fm_load_file(1, DGSTR(DISTRICT_PTR(district, 0)), NULL));
    s3d_map = EMSLock((int16_t)mm_ref(REF_MAP_BANK)->ems, 0x14,
                      (uint16_t)(mm_ref(REF_MAP_BANK)->size - 0x14));
    mm_ems_lock(g_s3d_map_lock, 0x47E, DGSTR(0x4150));
    mm_ems_unlock(g_s3d_map_lock);
    sprintf(name, DGSTR(0x415F), (int)(int16_t)(g_s3d_district + 1));   /* "pcs%d" */
    if (mm_ref(REF_SCRIPT_BANK) != NULL)
        ctl_free_checked(REF_SCRIPT_BANK, 0x488, DGSTR(0x4165));
    mm_ref_set(REF_SCRIPT_BANK, fm_load_file(1, name, NULL));
    g_s3d_map_w = s3d_map[0];
    g_s3d_map_h = s3d_map[1];
    DG16(0xBA86) = s3d_map[2];
    if (first) {
        g_s3d_x = (int16_t)(s3d_map[7] - 1);
        g_s3d_y = (int16_t)(s3d_map[8] - 1);
        g_s3d_prev_x = (int16_t)(g_s3d_map_w >= g_s3d_x + 1 ? g_s3d_x + 1 : g_s3d_x - 1);
        g_s3d_prev_y = (int16_t)(g_s3d_map_h >= g_s3d_y + 1 ? g_s3d_y + 1 : g_s3d_y - 1);
        if (g_s3d_y < 3)
            g_s3d_facing = 2;
        else if (g_s3d_x < 3)
            g_s3d_facing = 1;
        else if (g_s3d_map_h - 3 >= g_s3d_y)
            g_s3d_facing = 3;
        else
            g_s3d_facing = 0;
        DG8(0xBBAC) = 0;
        DG8(0xBBB7) = 0;
        DG8(0xBBAB) = (uint8_t)((int32_t)bc_rand() * 100 / 0x8000 + 0x65);
        if (g_s3d_district - 1 < 12) {
            /* BB88 holds the index first, then the y midpoint */
            DGS16(0xBB88) = g_s3d_district - 1 > 0 ? g_s3d_district - 1 : 0;
            DGS16(0xBB8A) = (int16_t)(DGS16(0x4A86 + 8 * DG16(0xBB88))
                                      + DGS16(0x4A8A + 8 * DG16(0xBB88))) >> 1;
            DGS16(0xBB88) = (int16_t)(DGS16(0x4A88 + 8 * DG16(0xBB88))
                                      + DGS16(0x4A8C + 8 * DG16(0xBB88))) >> 1;
        }
    }
    /* wall sets: slots 0-3 walls (surfaces 18h-1Bh), 4-7 doors (1Ch-1Fh) */
    for (i = 0; i < 4; i++) {
        g_q3d_wall_set(i) = s3d_map[i + 3];
        g_q3d_kor[i].version = 1;
        g_q3d_kor[i].n = 6;
        g_q3d_kor[i].hdr0 = 0;
        g_q3d_kor[i].h = 200;
        g_q3d_kor[i].w = 200;
        q3d_load_kor(DGSTR(0x2978 + 13 * g_q3d_wall_set(i)), i);       /* duvarN.cmp */
        if (mm_ref(SURF_REF_OF(0x18 + i)) != NULL)
            ctl_free_checked(SURF_REF_OF(0x18 + i), 0x4B5, DGSTR(0x4174));
        if (s3d_map[i + 3] != 0)
            fm_load_cmp(1, DGSTR(0x2978 + 13 * g_q3d_wall_set(i)), (int16_t)(i + 0x18));
    }
    for (i = 0; i < 4; i++) {
        g_q3d_kor[i + 4].version = 1;
        g_q3d_kor[i + 4].n = 6;
        g_q3d_kor[i + 4].hdr0 = 0xEB;
        g_q3d_kor[i + 4].h = 200;
        g_q3d_kor[i + 4].w = 200;
        q3d_load_kor(DGSTR(0x29FA + 13 * g_q3d_wall_set(i)), (int16_t)(i + 4)); /* kapiN.cmp */
        if (mm_ref(SURF_REF_OF(0x1C + i)) != NULL)
            ctl_free_checked(SURF_REF_OF(0x1C + i), 0x4C3, DGSTR(0x4183));
        if (s3d_map[i + 3] != 0)
            fm_load_cmp(1, DGSTR(0x29FA + 13 * g_q3d_wall_set(i)), (int16_t)(i + 0x1C));
    }
    for (i = 0; i < 4; i++) {
        if (mm_ref(SURF_REF_OF(0x20 + i)) != NULL)
            ctl_free_checked(SURF_REF_OF(0x20 + i), 0x4D0, DGSTR(0x4192));
        if (g_q3d_wall_set(i) != 0)
            fm_load_cmp(1, DGSTR(0x2A7C + 13 * g_q3d_wall_set(i)), (int16_t)(i + 0x20)); /* kapianN */
    }
    q3d_reset_kor();
    /* floors 24h-37h: an empty name shares the picture 5 or 10 slots back */
    for (i = 0; i < 0x14; i++) {
        if (mm_ref(SURF_REF_OF(0x24 + i)) != NULL)
            ctl_free_checked(SURF_REF_OF(0x24 + i), 0x4D9, DGSTR(0x41A1));
        if (DG8(0x2B18 + 13 * i) != 0)
            fm_load_cmp(1, DGSTR(0x2B18 + 13 * i), (int16_t)(i + 0x24));
        else
            alias_surface((uint16_t)(0x24 + i), (uint16_t)(0x24 + i - (i / 5 <= 1 ? 1 : 2) * 5));
    }
    /* ceilings 38h-3Fh: an empty name shares the picture 2 or 4 slots back */
    for (i = 0; i < 8; i++) {
        if (mm_ref(SURF_REF_OF(0x38 + i)) != NULL)
            ctl_free_checked(SURF_REF_OF(0x38 + i), 0x4E6, DGSTR(0x41B0));
        if (DG8(0x2C1C + 13 * i) != 0)
            fm_load_cmp(1, DGSTR(0x2C1C + 13 * i), (int16_t)(i + 0x38));
        else
            alias_surface((uint16_t)(0x38 + i), (uint16_t)(0x38 + i - (i / 2 <= 1 ? 1 : 2) * 2));
    }
    /* the sky: goty0 (day) / goty1 (night) */
    if (mm_ref(SURF_REF_OF(0x40)) != NULL)
        ctl_free_checked(SURF_REF_OF(0x40), 0x4F1, DGSTR(0x41BF));
    fm_load_cmp(1, DGSTR(0x2AFE + 13 * ((s3d_calc_daylight() & 1) ? 1 : 0)), 0x40);
    if (s3d_calc_daylight() & 1) {
        if (DG8(0x5BC2) != 0) {
            asm_memset32(gr_target_pal, 0, 0x180);
            build_aa(0x4F6, 0x41CE);
            DG8(0x5BC2) = 0;
        }
        gr_draw_strip(0, 0, (uint16_t)g_surf_w(0x40), (uint16_t)g_surf_h(0x40), 0x40, 0xFD);
        DG8(0x3D6D) |= 2;
    } else {
        DG8(0x3D6D) &= 0xFD;
    }
    if (DISTRICT_PTR(g_s3d_district, 0x18) != 0 || DISTRICT_SEG(g_s3d_district, 0x18) != 0) {
        if (mm_ref(SURF_REF_OF(0x4F)) != NULL)
            ctl_free_checked(SURF_REF_OF(0x4F), 0x500, DGSTR(0x41DD));
        fm_load_cmp(1, DGSTR(DISTRICT_PTR(g_s3d_district, 0x18)), 0x4F);  /* ufukN.cmp */
    }
    q3d_wait_retrace(3);
    s3d_map = EMSLock((int16_t)mm_ref(REF_MAP_BANK)->ems, 0x2B,
                      (uint16_t)(mm_ref(REF_MAP_BANK)->size - 0x2B));
    mm_ems_lock(g_s3d_map_lock, 0x50C, DGSTR(0x41EC));
    mm_ems_unlock(g_s3d_map_lock);
    g_q3d_opt_flags |= 0x20;
}

/*
 * s3d_debug_buffers - make surface n's picture the "shown picture" 57h: a
 * conventional copy (mm_duplicate type 0) and its size.  An empty slot or
 * one that is not in EMS (type 1) is fatal ("Buf no:%d").
 *
 * Original: IST.EXE 303F:23ED (3DSYS.L), far cdecl (unsigned char n).
 * After a fatal the original goes on reading the record (the port's
 * fatal does not return in the game; the tests' fatal returns, so a NULL
 * record is not dereferenced here).
 * Unreferenced in the shipped game (its thunk 1670:0070 is never called).
 * Verified: tests/py/test_s3d_gap.py (test_debug_buffers: EMS and
 * conventional surfaces, empty slots up to the second fatal).
 */
void s3d_debug_buffers(uint8_t n)
{
    if (mm_ref(SURF_REF_OF(n)) == NULL) {
        if (sprintf(g_q3d_errbuf, DGSTR(0x41FB), (int)n) >= 0x68)          /* "Buf no:%d" */
            o559A_fatal(DGSTR(0x4205), 0x515, NULL, 0x47);
        o559A_fatal(DGSTR(0x4214), 0x517, NULL, 0x48);
    }
    if (mm_ref(SURF_REF_OF(n)) == NULL || mm_ref(SURF_REF_OF(n))->type != 1) {
        if (sprintf(g_q3d_errbuf, DGSTR(0x4223), (int)n) >= 0x68)
            o559A_fatal(DGSTR(0x422D), 0x51C, NULL, 0x49);
        o559A_fatal(DGSTR(0x423C), 0x51E, NULL, 0x4A);
        if (mm_ref(SURF_REF_OF(n)) == NULL)
            return;
    }
    mm_ref_set(SURF_REF_OF(0x57), mm_duplicate(mm_ref(SURF_REF_OF(n)), 0));
    g_surf_w(0x57) = g_surf_w(n);
    g_surf_h(0x57) = g_surf_h(n);
}

/*
 * s3d_enter_view - set up the exploration screen: palette pal3d.pal (from
 * black), the interface picture 50h on the screen, the district music
 * (unless already playing, the battle was lost, or BB4D == 1 without
 * music option BB36), the party panel, the 3D view, the status line and
 * money, daylight; then fade in (music tempo / volume by the daylight
 * values 3D6E/3D72) and set the cursor from the window's shape callback.
 *
 * Original: IST.EXE 303F:2521 (3DSYS.L), far cdecl.
 * Verified: tests/py/test_s3d_district.py (test_enter_view).
 */
void s3d_enter_view(void)
{
    gr_cursor_hide();
    memcpy(DGP(UI_WIN_CUR), DGP(UI_WIN_REC(6)), UI_WIN_SIZE);
    ogr_black_palette();
    fm_load_pal(DGSTR(0x424B), 0, 0x100);                   /* "pal3d.pal" */
    build_aa(0x534, 0x4255);
    DG8(0x5BC2) = 0;
    g_q3d_opt_flags = 0x20;
    g_pal_level = 1;
    gr_fade(1, (int16_t)-g_pal_level, 0);
    g_music_level = 1;
    gr_fade(1, 0, (int16_t)-g_music_level);
    ogr_set_start_addr(0);
    ogr_set_split_line(200);
    ogr_mouse_set_window(0, 0, 0x13F, 199);
    mm_copy_block(mm_ref(0xE51F), mm_ref(0xE29F));          /* interface 50h -> screen */
    if (g_sound_on && g_music_module == NULL && DGS16(0x632E) != 2
        && (DG8(0xBB36) != 0 || DGS16(0xBB4D) != 1))
        demo_play_music(DGS16(0xBB4D), 1, 0);
    g_q3d_opt_flags |= 0x10;
    s3d_draw_party_panel();
    g_q3d_anim_mode = 0;
    q3d_alloc_view_buffers();
    DGS16(0x3D6E) = 0;
    DGS16(0x3D72) = 0;
    DGS16(0x3D70) = 0;
    s3d_redraw_screen();
    s3d_redraw_view(1);
    s3d_draw_status_line(0xC6, 0xB2);
    fr_draw_money();
    s3d_apply_daylight();
    g_q3d_opt_flags &= 0xFFEF;
    if (DGS16(0xBB4D) == 1) {
        DGS16(0x731C) = (int16_t)(DGS16(0x7322) + DGS16(0x3D6E));
        ampSetTempo((int32_t)(int16_t)(DGS16(0x731C) << 8));
    } else {
        DGS16(0x3D72) = (int16_t)(g_music_level_max - 0x1E);
        DGS16(0x3D6E) = 0;
    }
    if (g_pal_level != 0)
        DGS16(0x3D70) = 0;
    if (g_music_level != 0)
        DGS16(0x3D72) = 0;
    gr_fade(500, DGS16(0x3D70), DGS16(0x3D72));
    DGS16(0x3D72) = 0;
    DGS16(0x3D70) = 0;
    gr_cursor_set_shape((uint8_t)((int16_t (*)(void))ui_far_fn(DG32(UI_CB_SHAPE)))());
}

/*
 * s3d_redraw_screen - follow a day/night change (3D6D bit 1): load the
 * matching sky into 40h and redraw the view; at night the sky is darkened
 * (gr_draw_strip colour FDh) unless the palette is still being rebuilt
 * (5BC2, then 3EDC is not updated).  While 3D5E bit 10h is set (screen
 * being built) only the state 3EDC is updated.
 *
 * Original: IST.EXE 303F:276C (3DSYS.L), far cdecl.
 * Compared only inside s3d_enter_view (test_s3d_district.py test_enter_view).
 */
void s3d_redraw_screen(void)
{
    uint8_t night = (uint8_t)(DG8(0x3D6D) >> 1);

    if (!(g_q3d_opt_flags & 0x10)) {
        if (night == DG8(0x3EDC))
            return;
        fm_load_cmp(1, DGSTR(0x2AFE + 13 * night), 0x40);  /* gotyN.cmp */
        s3d_redraw_view(1);
        if (night != 0) {
            if (DG8(0x5BC2) != 0)
                return;
            gr_draw_strip(0, 0, (uint16_t)g_surf_w(0x40), (uint16_t)g_surf_h(0x40), 0x40, 0xFD);
        }
    }
    DG8(0x3EDC) = night;
}

/*
 * s3d_draw_portrait - (misnamed: sets up the mouse zones of the
 * exploration screen) zones 1-4 the portraits (32x32 at BB4B/BB49 + i *
 * BB47/BB45), 5 the compass, 6-9 the left/forward/right thirds and the
 * lower half of the view, 10-12 the three buttons, the panel zone (965C)
 * and 14 the whole view.
 *
 * Original: IST.EXE 303F:27F7 (3DSYS.L), far cdecl (Ghidra splits it at
 * 280A).  Divisions are signed (IDIV).
 * Called by s3d_draw_party_panel (303F:2C92) and s3d_draw_portrait_hl
 * (303F:7616); INTRFACE stores its thunk 1670:00A2 as a window callback
 * (05DC:0A52).
 * Verified: tests/py/test_s3d_gap.py (test_zones).
 */
void s3d_draw_portrait(void)
{
    int16_t i;
    int16_t w = g_q3d_win_w, h = g_q3d_win_h;
    int16_t ox, oy;

    gr_zones_clear();
    for (i = 0; i < 4; i++) {
        g_zone_x1(1 + i) = (int16_t)(DGS16(0xBB4B) + i * DGS16(0xBB47));
        g_zone_y1(1 + i) = (int16_t)(DGS16(0xBB49) + i * DGS16(0xBB45));
        g_zone_x2(1 + i) = (int16_t)(g_zone_x1(1 + i) + 0x20);
        g_zone_y2(1 + i) = (int16_t)(g_zone_y1(1 + i) + 0x20);
    }
    g_zone_x1(5) = 0x90;
    g_zone_y1(5) = 0x9C;
    g_zone_x2(5) = 0xBE;
    g_zone_y2(5) = 0xC3;
    ox = (int16_t)(w == 100 ? 0x30 : 0);
    oy = (int16_t)(h == 100 ? 0x34 : 0);
    g_zone_x1(6) = ox;
    g_zone_y1(6) = oy;
    g_zone_x2(6) = (int16_t)(g_zone_x1(6) + w / 3);
    g_zone_y2(6) = (int16_t)(g_zone_y1(6) + h / 2);
    g_zone_x1(7) = (int16_t)(w / 3 + ox);
    g_zone_y1(7) = oy;
    g_zone_x2(7) = (int16_t)(g_zone_x1(7) + w / 3);
    g_zone_y2(7) = (int16_t)(g_zone_y1(7) + h / 2);
    g_zone_x1(8) = (int16_t)((int16_t)(w << 1) / 3 + ox);
    g_zone_y1(8) = oy;
    g_zone_x2(8) = (int16_t)(g_zone_x1(8) + w / 3);
    g_zone_y2(8) = (int16_t)(g_zone_y1(8) + h / 2);
    g_zone_x1(9) = ox;
    g_zone_y1(9) = (int16_t)(h / 2 + oy);
    g_zone_x2(9) = (int16_t)(g_zone_x1(9) + w);
    g_zone_y2(9) = (int16_t)(g_zone_y1(9) + h / 2);
    for (i = 0; i < 3; i++) {
        g_zone_x1(10 + i) = (int16_t)(i * 0x12 + 0x109);
        g_zone_y1(10 + i) = 0x98;
        g_zone_x2(10 + i) = (int16_t)(g_zone_x1(10 + i) + 0xE);
        g_zone_y2(10 + i) = (int16_t)(g_zone_y1(10 + i) + 0xE);
    }
    g_zone_x1(g_ui_panel_zone) = 0;
    g_zone_y1(g_ui_panel_zone) = 0x85;
    g_zone_x2(g_ui_panel_zone) = 0x140;
    g_zone_y2(g_ui_panel_zone) = 200;
    g_zone_x1(14) = ox;
    g_zone_y1(14) = oy;
    g_zone_x2(14) = (int16_t)(g_zone_x1(14) + w);
    g_zone_y2(14) = (int16_t)(g_zone_y1(14) + h);
}
