/*
 * istanbul.c - ISTANBUL.C, main() of IST.EXE, decompiled from seg0546
 * (0546:0001-0963, the only function of the segment).  See istanbul.h.
 *
 * Port notes:
 *  - puts/printf -> fatal_puts/fatal_printf (the console of the port).
 *  - _OvrInitEms (seg122B, the VROOMM overlay manager, dropped) is modelled
 *    by ist_ovr_init_ems: the same INT 67h calls, so the EMS handle and page
 *    counts the rest of the game sees are the original's.
 *  - time(NULL) -> host time (or ist_time_override in tests).
 *  - getch -> ui_getch.
 */
#include "game/istanbul.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "dsmi/amp.h"
#include "dsmi/memalloc.h"
#include "engine/dgroup.h"
#include "engine/fatal.h"
#include "engine/fileman.h"
#include "engine/intrface.h"
#include "engine/memman.h"
#include "engine/ngraphs.h"
#include "engine/nkontrol.h"
#include "engine/ographs.h"
#include "engine/oses.h"
#include "engine/systems.h"
#include "engine/vfile.h"
#include "engine/vfs.h"
#include "game/cd5737.h"
#include "game/demolar.h"
#include "game/q3dview.h"
#include "platform/pc.h"
#include "rtl/bc_rand.h"
#include "rtl/bc_stdio.h"

#if __has_include("game/s3d.h")
#include "game/s3d.h"
#else
/* provided by 3DSYS (WP16) */
void     s3d_load_district(int16_t district, uint8_t first);  /* 303F:1927 */
void     s3d_enter_view(void);                                /* 303F:2521 */
void     s3d_load_ui_images(void);                            /* 303F:394A */
void     s3d_explore_loop(void);                              /* 303F:8524 */
void     s3d_register_far_fns(void);
#endif
#if __has_include("game/frpos.h")
#include "game/frpos.h"
#else
/* provided by FRPOS (WP15) */
void     fr_flags_reset(void);                                /* 3FD1:4B31 */
void     fr_register_far_fns(void);
#endif
#if __has_include("game/chargen.h")
#include "game/chargen.h"
#else
/* provided by CHARGEN (WP14) */
int16_t  cg_main(void);                                       /* 39E5:1AA2 */
void     cg_load_party(const char *name);                     /* 39E5:1932 */
#endif
#if __has_include("game/iso.h")
#include "game/iso.h"
#else
/* provided by ISOSYS (WP17) */
void     iso_register_far_fns(void);
#endif

int32_t ist_time_override = -1;

/* DGROUP used only here */
#define g_ist_3d5c        DGS16(0x3D5C)   /* i16  cleared at start-up */
#define g_ist_3d6e        DGS16(0x3D6E)   /* i16  cleared with the fade deltas */
#define g_ist_bb34        DG8(0xBB34)     /* u8   0 at start-up, 1 for a new game */
#define g_ist_bb36(i)     DG8(0xBB36 + (i)) /* u8 x3  set to 1 for a new game */
#define g_ist_bb39        DG8(0xBB39)     /* u8 */
#define g_ist_bb3a        DG8(0xBB3A)     /* u8   1 at start-up */
#define g_ist_bb6f(i)     DGS16(0xBB6F + 2 * (i)) /* i16 x3 (BB73 = g_fr_money) set to 10 */
#define g_ist_5bda(i)     DGS16(0x5BDA + 6 * (i)) /* i16 pairs 5BDA/5BDC, i = 1..3, cleared */
#define g_ist_5bdc(i)     DGS16(0x5BDC + 6 * (i))
#define IST_PORTRAIT_REF(i) ((uint16_t)(0xE4D7 + 8 * (i))) /* far MmHandle *, party portraits 47h+i */
#define g_ist_party_race(i) DGS16(0x9A44 + 0x5C * (i))     /* per member, index into the 13-byte
                                                              name table at 269B */
#define IST_FILE          DGSTR(0x33D7)   /* "CODE\ISTANBUL.C" (each site has its own copy) */

/* overlay manager (seg122B) */
#define g_ovr_total_size      DG32(0x8F8E)   /* u32 total size of the overlays (__OvrPrepare) */
#define IST_OVR_TOTAL         0x35D10u       /* IST.EXE's value of it */
#define IST_OVR_LARGEST_PARAS 0x0E82u        /* the overlay manager's own data [11Ah]: the
                                                largest overlay in paragraphs (DOSBox-X) */

static uint32_t ist_time(void)
{
    if (ist_time_override >= 0)
        return (uint32_t)ist_time_override;
    return (uint32_t)time(NULL);
}

/*
 * ist_ovr_prepare - port: the DGROUP effect of the overlay manager's
 * start-up (Borland __OvrPrepare, seg122B 0054-00E7, a c0 #pragma startup
 * routine): it reads the FBOV headers of IST.EXE and stores the total
 * size of the overlays at 16E0:8F8E (u32), which _OvrInitEms uses to
 * size the EMS cache.  IST.EXE's overlays total 35D10h bytes (DOSBox-X at
 * main(): build/oracle/main_mem.bin).  The host calls this with dg_reset().
 */
void ist_ovr_prepare(void)
{
    g_ovr_total_size = IST_OVR_TOTAL;
}

/*
 * ist_ovr_init_ems - the EMS part of Borland's _OvrInitEms(handle, first,
 * pages) as IST.EXE's seg122B does it: EMMXXXX0 present, status (40h),
 * version >= 3.2 (46h), page frame (41h), free pages (42h); for handle 0
 * the cache is `pages` pages, but at most the free pages above `first` and
 * at most the overlays' total size (8F8E) in 16 KB pages (and at least 4
 * pages or room for the largest overlay); then allocate them (43h) and
 * save/restore the page map (47h/48h).  Returns 0 when the overlay cache
 * is in EMS, else -1.
 * Original: IST.EXE 122B:08EF (+0D8F, 0E41), Borland RTL.
 * Port: model; the overlays themselves are normal code.
 * Verified: against DOSBox-X (tools/dosbox/capture_startup.py): after the
 *   start-up the original's EMS handle 1 has 14 pages (0x38000 bytes) =
 *   ceil(35D10h / 4000h), the port's the same (tests/py/test_istanbul.py
 *   test_startup_oracle).
 */
int16_t ist_ovr_init_ems(uint16_t handle, uint16_t first, uint16_t pages)
{
    PcEmsRegs r;
    uint16_t avail, need;
    if (!pc_ems_detect())
        return -1;
    memset(&r, 0, sizeof r);
    r.ax = 0x4000;
    pc_int67(&r);
    if (r.ax >> 8)
        return -1;
    r.ax = 0x4600;
    pc_int67(&r);
    if ((r.ax >> 8) || (uint8_t)r.ax < 0x32)
        return -1;
    r.ax = 0x4100;
    pc_int67(&r);
    if (r.ax >> 8)
        return -1;
    r.ax = 0x4200;
    pc_int67(&r);
    if (r.ax >> 8)
        return -1;
    if (handle == 0) {
        avail = r.bx;                               /* free pages (0D8F returns BX) */
        if (avail == 0 || first >= avail)
            return -1;
        avail = (uint16_t)(avail - first);
        if (pages == 0 || pages >= avail)
            pages = avail;
        need = (uint16_t)((g_ovr_total_size + 0x3FFFu) / 0x4000u);
        if (pages > need)
            pages = need;
        if (pages < 4 && (uint16_t)(pages * 0x400u) < IST_OVR_LARGEST_PARAS)
            return -1;
    r.ax = 0x4300;
        r.bx = pages;
        pc_int67(&r);
        if (r.ax >> 8)
            return -1;
        handle = r.dx;
    }
    r.ax = 0x4700;
    r.dx = handle;
    pc_int67(&r);
    if (r.ax >> 8)
        return -1;
    r.ax = 0x4800;
    r.dx = handle;
    pc_int67(&r);
    return (r.ax >> 8) ? -1 : 0;
}

/* the portraits of the members that have none yet (0675 / 07FD) */
static void ist_portraits(const char *prefix, char *buf)
{
    for (int16_t i = 0; i < 4; i++)
        if (!(DG16(IST_PORTRAIT_REF(i)) | DG16(IST_PORTRAIT_REF(i) + 2)))
            fm_load_cmp(1, strcat(strcpy(buf, prefix),
                                  DGSTR((uint16_t)(g_ist_party_race(i) * 13 + 0x269B))),
                        (int16_t)(i + 0x47));
}

/*
 * ist_main - see istanbul.h.
 * Original: IST.EXE 0546:0001 (ISTANBUL.C), far cdecl main(argc, argv).
 * Verified: start-up up to s3d_load_ui_images (the first call into another
 *   game module after fr_flags_reset, both stubbed) by
 *   tests/py/test_istanbul.py: heap layout, DGROUP, EMS; the menu loop
 *   is not verified (needs the game modules).
 */
int ist_main(int argc, char **argv)
{
    char buf[0x64];
    int16_t r, i;
    uint8_t menuarg = (uint8_t)(argc - 1);
    uint8_t sound;
    uint32_t ems;
    (void)argv;

    /* port: far code entries of the game modules for INTRFACE */
    demo_register_far_fns();
    s3d_register_far_fns();
    fr_register_far_fns();
    iso_register_far_fns();

    o559A_load_trdos_font();
    fatal_puts(DGSTR(0x338E));
    if (vfs_init(0x28))
        o559A_fatal(DGSTR(0x33D7), 0x29, 0, 0x6D);
    else
        fatal_puts(DGSTR(0x33E7));
    if (ist_ovr_init_ems(0, 0, 0x12))
        o559A_fatal(DGSTR(0x33FE), 0x2C, DGSTR(0x340E), -1);
    else
        fatal_puts(DGSTR(0x3456));
    ctl_dds_timer_init();
    fatal_puts(DGSTR(0x346F));
    r = oses_init(1);
    if (r != 0 && r == 1) {
        fatal_printf(DGSTR(0x348F));
        if (!ui_ask_evet_hayir())
            o559A_fatal(DGSTR(0x3521), 0x38, DGSTR(0x3531), -1);
        fatal_puts(DGSTR(0x3565));
    }
    sound = r == 0xEE ? 1 : 0;
    fatal_printf(DGSTR(0x3566), sound ? DGSTR(0x3571) : DGSTR(0x3575));
    fatal_printf(DGSTR(0x3579), (int)g_ses_cfg_tail);
    ems = 0x240000;
    if (g_ses_cfg_tail == 0)
        ems += 0x200000;
    else if (g_ses_cfg_tail == 1)
        ems += 0x100000;
    g_sound_on = sound;
    r = mm_init(0x240000, ems);
    g_sound_on = 0;
    fatal_printf(DGSTR(0x3591), (long)mm_coreleft(1));
    if (r != 1) {
        switch (r) {
        case -1:
            o559A_fatal(DGSTR(0x35A8), 0x50, DGSTR(0x35B8), -1);
            /* fall through */
        case -2:
            sprintf(DGSTR(0xBACC), DGSTR(0x363D), 0x240000UL);
            o559A_fatal(DGSTR(0x36A1), 0x53, DGSTR(0xBACC), -1);
            /* fall through */
        case -3:
            fatal_printf(DGSTR(0x36B1),
                         (long)(int32_t)((0x3D090u - (sound ? 0u : 0xEA60u)) - mm_coreleft(0)));
            if (!ui_ask_evet_hayir())
                o559A_fatal(DGSTR(0x37BF), 0x5A, DGSTR(0x37CF), -1);
            fatal_puts(DGSTR(0x380E));
            break;
        default:
            o559A_fatal(DGSTR(0x380F), 0x5D, 0, 0x4F);
            break;
        }
    }
    o559A_clear_tables();
    bc_srand((uint16_t)ist_time());
    ctl_install();
    r = oses_init(0);
    if (r == 0xFF)
        r = 0;
    if (r != 0 && r == 3)
        o559A_fatal(DGSTR(0x381F), 0x6E, DGSTR(0x382F), -1);
    ctl_timer_install();
    g_ist_3d5c = 0;
    ui_init_tables();
    buf[0] = 0;
    strcat(buf, DGSTR(0x3841));
    if ((g_archive_slot(0) = vfs_mount_archive(DGSTR(0x384A), 0)) == -1 ||
        (g_archive_slot(1) = vfs_mount_archive(DGSTR(0x3854), 0)) == -1 ||
        (g_archive_slot(2) = vfs_mount_archive(DGSTR(0x385F), 0)) == -1 ||
        (g_archive_slot(3) = vfs_mount_archive(DGSTR(0x3869), 0)) == -1 ||
        (g_archive_slot(4) = vfs_mount_archive(DGSTR(0x3872), 0)) == -1 ||
        (g_archive_slot(5) = vfs_mount_archive(DGSTR(0x387B), 0)) == -1 ||
        (g_archive_slot(6) = vfs_mount_archive(DGSTR(0x3885), 0)) == -1 ||
        (g_archive_slot(7) = vfs_mount_archive(buf, 0)) == -1)
        o559A_fatal(DGSTR(0x388C), 0xAE, DGSTR(0x389C), -1);
    bc_flushall();
    bc_setvbuf(vfile_files[g_archive_slot(7)], 0, 1, 0x800);
    fatal_puts(DGSTR(0x392E));
    fm_load_cmp(1, DGSTR(0x394B), 3);
    fm_load_cmp(1, DGSTR(0x3956), 4);
    fm_load_cmp(1, DGSTR(0x3960), 5);
    fm_load_cmp(1, DGSTR(0x396A), 0x54);
    fatal_puts(DGSTR(0x3975));
    sys_load_rix(DGSTR(0x398F));
    fatal_puts(DGSTR(0x399B));
    fr_flags_reset();
    fatal_puts(DGSTR(0x39BA));
    g_q3d_win_w = 0xC8;
    g_q3d_win_h = 0xC8;
    g_q3d_view_w = 0xC8;
    g_q3d_view_h = 0xC8;
    g_q3d_anim_speed = 0;
    g_ist_bb3a = 1;
    g_ist_bb34 = 0;
    g_fade_bright_d = 0;
    g_fade_volume_d = 0;
    g_ist_3d6e = 0;
    q3d_precalc_table_a(g_q3d_view_w, g_q3d_view_h, g_q3d_pieces);
    q3d_precalc_table_b1();
    q3d_precalc_table_c1();
    q3d_init_tables(g_q3d_pieces);
    fatal_puts(DGSTR(0x39E8));
    s3d_load_ui_images();
    fatal_puts(DGSTR(0x3A0A));
    ogr_mouse_init();
    if (g_mouse_present) {
        fatal_puts(DGSTR(0x3A29));
    } else {
        fatal_printf(DGSTR(0x3A39));
        ui_getch();
        fatal_puts(DGSTR(0x3A82));
    }
    ogr_init();

    for (;;) {
        const char *name, *prefix;
        int16_t m = demo_intro_menu(0xB, menuarg);
        switch (m - 1) {
        case 0:                                     /* yeni oyuna başla */
            if (!cg_main())
                goto next;
            gr_fade(1000, 0, (int16_t)-g_music_level);
            if (g_sound_on && g_sound_on) {
                ampStopModule();
                if (g_music_module) {
                    ampFreeModule(g_music_module);
                    MemFree(g_music_module);
                    g_music_module = 0;
                }
            }
            o5737_cd_stop();
            name = DGSTR(0x3A90);
            goto new_game;
        case 2:                                     /* çabuk başla */
            name = DGSTR(0x3A83);
        new_game:
            cg_load_party(name);
            demo_load_sounds();
            for (i = 1; i < 4; i++) {
                g_ist_5bdc(i) = 0;
                g_ist_5bda(i) = 0;
            }
            ist_portraits(DGSTR(0x3A98), buf);
            g_s3d_district = 0;
            ui_init_voices();
            s3d_load_district(g_s3d_district, 1);
            g_ist_bb6f(0) = g_ist_bb6f(1) = g_ist_bb6f(2) = 10;
            g_sfx_level = g_sfx_level_max;
            g_music_track = 1;
            g_ist_bb34 = 1;
            DG8(0xBB35) = 5;
            g_ist_bb36(2) = g_ist_bb36(1) = g_ist_bb36(0) = 1;
            g_ist_bb39 = 0;
        enter:
            s3d_enter_view();
            s3d_explore_loop();
            goto next;
        case 1:                                     /* eski oyun (loaded) */
            ctl_flush_keys();
            s3d_explore_loop();
            o559A_fatal(DGSTR(0x3A9B), 0x112, DGSTR(0x3AAB), -1);
            goto next;
        case 3:                                     /* dos'a çık */
            o5737_cd_stop();
            demo_credits_exit();
            goto next;
        case 4:                                     /* argv shortcut: grp.dat */
            cg_load_party(DGSTR(0x3AB5));
            for (i = 1; i < 4; i++) {
                g_ist_5bdc(i) = 0;
                g_ist_5bda(i) = 0;
            }
            prefix = DGSTR(0x3ABD);
            ist_portraits(prefix, buf);
            g_s3d_district = 0;
            ui_init_voices();
            s3d_load_district(g_s3d_district, 1);
            g_ist_bb6f(0) = g_ist_bb6f(1) = g_ist_bb6f(2) = 10;
            g_s3d_facing = 2;
            g_sfx_level = g_sfx_level_max;
            g_music_track = 1;
            DG8(0xBB35) = 5;
            g_ist_bb36(2) = g_ist_bb36(1) = g_ist_bb36(0) = 1;
            g_ist_bb39 = 0;
            o5737_cd_stop();
            goto enter;
        case 5:
            demo_credits_exit();
            /* fall through */
        default:
            sprintf(buf, DGSTR(0x3AC0), (int)g_ui_list_sel);
            o559A_fatal(DGSTR(0x3AE7), 0x134, buf, -1);
            break;
        }
    next:
        menuarg = 0xC8;
        if (o559A_test_mode && o559A_fatal_count)
            return -1;                  /* port: tests only (fatal returned) */
    }
}
