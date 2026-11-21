/*
 * s3d.h - 3DSYS.L: the exploration part of the game (IST.EXE ovl303F,
 * source T:\CODE\3DSYS.L per its fatal-error strings): the main
 * exploration loop, party movement on the district map, the screen and
 * party panel, the game clock and daylight, camp, the character screen,
 * save/load, options and leaving a district.  It drives the first-person
 * renderer (src/game/q3dview.h) and calls the FRPOS script interpreter,
 * the battle system (ISOSYS) and the DEMOLAR screens.
 *
 * Implementation, by original address:
 *   s3d_move.c     303F:0000-0BDA  movement, view redraw
 *   s3d_keys.c     303F:0BDB-1926  location report, key dispatcher
 *   s3d_district.c 303F:1927-2C68  district loading, screen set-up
 *   s3d_panel.c    303F:2C69-3DCF  party panel, UI pictures, clock, daylight
 *                  (+ 303F:75EF s3d_draw_portrait_hl)
 *   s3d_save.c     303F:3DD0-4352  save / load (format: docs/FORMATS.md)
 *   s3d_camp.c     303F:4353-4F39  camp, random phrases, status line
 *   s3d_charscr.c  303F:4F3A-75EE  character screen
 *   s3d_menu.c     303F:765C-8523  leave district, options, save/load menu,
 *                                  sound options
 *   s3d_loop.c     303F:8524-9A48  the exploration loop
 *
 * Calls into other game modules go through src/game/s3d_ext.h.
 */
#ifndef LALE_GAME_S3D_H
#define LALE_GAME_S3D_H

#include <stdint.h>

#include "engine/dgroup.h"
#include "game/q3dview.h"

/* ---- DGROUP (game state owned by 3DSYS) ---------------------------------- */
/* Names that 3DQUICK already defines (q3dview.h): g_s3d_step_dy/dx (B9C4/6),
   g_s3d_map_h/w (B9EA/B), g_s3d_facing (B9F0), g_s3d_y/x (B9F1/3),
   g_s3d_prev_y/x (BB4F/51), g_q3d_opt_flags (3D5E), g_q3d_wall_set(n). */
#define S3D_FILE        0x3F14          /* "T:\CODE\3DSYS.L" (several copies follow) */
#define SURF_REF_OF(n)  ((uint16_t)(0xE29F + 8 * (n)))  /* far MmHandle * of surface n (mm_ref) */
#define g_s3d_view_dirty DG16(0xB9C8)   /* u16  the view must be redrawn */
#define g_s3d_event_flags DGP(0xB9CA)   /* u8[32] 256 event flags (bit n%8 of byte n/8) */
#define g_s3d_district  DGS16(0xBB6B)   /* i16  current district 0..17 */
#define g_s3d_cancel_move DGS16(0xBBA8) /* i16  set by FRPOS (op 16) to cancel a move */

/* ---- DGROUP used mainly by s3d_charscr.c ---- */
/* party stat record p (0..4), 5Ch bytes at 99A6 (gameA.md 2.3); word at
   byte offset off.  +3A health, +3C/+3E/+40 hand slots (0 or inventory
   slot + 22h, i.e. a word index into the record), +46.. 10 inventory
   items (value = 100 * count + item id), +04 gender (1 = erkek), +2C name
   index into the 15-byte table at 0CE2 */
#define g_s3d_stat(p, off)  DGS16((uint16_t)(0x99A6 + 0x5C * (p) + (off)))
#define g_s3d_item(p, i)    DGS16((uint16_t)(0x99EA + 0x5C * (p) + 2 * (i)))  /* i = 1..10 */
#define S3D_NAME_REC(p)     ((uint16_t)(0x9C26 + 0x94 * (p)))   /* char[37] name; +4A race */
#define S3D_ITEM_NAMES      0x0CE2      /* char[15] names, index stat +2C */
#define g_s3d_item_hands(n) DGS16((uint16_t)(0x0E81 + 0x0E * (n)))  /* item table (14-byte
                                           records at 0E7F) +2: 1 one hand, 2 both */
#define g_s3d_vec_kind(n)   DGS16((uint16_t)(0x11FD + 2 * (n)))  /* vecize n: 2 = usable here */
#define g_s3d_vec_target(n) DGS16((uint16_t)(0x1241 + 2 * (n)))  /* 1 = asks for a target */
#define g_s3d_pt_dy     DGS16(0xBB45)   /* i16  portrait row layout: step y */
#define g_s3d_pt_dx     DGS16(0xBB47)   /* i16  step x */
#define g_s3d_pt_y0     DGS16(0xBB49)   /* i16  first portrait y */
#define g_s3d_pt_x0     DGS16(0xBB4B)   /* i16  first portrait x */
#define g_s3d_ba89(i)   DG8((uint16_t)(0xBA89 + 0x11 * (i)))  /* u8  i = 0..3, set to 1 by the
                                           character screen (meaning unknown) */
#define g_cs_desc_prev  DGS16(0xD5E4)   /* = intrface.h g_ui_list_prev: description shown */
#define g_cs_item_count DGS16(0xD5E6)   /* i16  selected item: count (value / 100) */
#define g_cs_zone       DGS16(0xD5E8)   /* i16  zone under the cursor / selected slot */
#define g_cs_item_id    DGS16(0xD5EA)   /* i16  selected item: id (value % 100), remapped
                                           to its icon (2Bh -> 22h, > 20h -> +2) */
#define g_cs_target     DGS16(0xD5EC)   /* i16  drop target zone while an item is held */
#define g_cs_load       DGS16(0xD5EE)   /* i16  load (set by iso_calc_stat_a) */
#define g_cs_weapon     DGS16(0xD5DA)   /* i16  weapon value (set by iso_calc_weapon) */
#define g_cs_vec_caster DGS16(0xD5DE)   /* i16  = iso.h g_iso_actor */
#define g_cs_vec_victim DGS16(0xD5E0)   /* i16  vecize target (combatant 19h..1Ch) */
#define g_cs_vecize     DGS16(0xD5E2)   /* i16  vecize chosen by iso_cmd_vecize, 0 none */
#define g_cs_vec_time   DG16(0xD5D4)    /* u16  low word of the clock after a vecize */
#define g_cs_6320       DGS16(0x6320)   /* i16  ISOSYS face/voice unit (caster + 28h) */
#define g_cs_vec_voice  DGS16(0x631A)   /* i16  = iso.h g_iso_anim_frame: voice to speak */
#define CS_VEC_TEXT     0xA510          /* char[] text of the vecize result */
/* ---- end charscr ---- */

/* ---- DGROUP used mainly by s3d_menu.c / s3d_loop.c ---- */
/* Also used: demolar.h g_music_module (BB75, native), g_music_track (BB4D),
   g_sfx_level (731C), g_fade_bright_d (3D70), g_fade_volume_d (3D72). */
#define S3D_DISTRICT_TAB 0x2E31         /* 18 x 1Dh: far char * name, genitive, videos
                                           day-in +08, night-in +0C, day-out +10,
                                           night-out +14, horizon +18; u8 +1C flag */
#define g_s3d_opt(i)    DG8((uint16_t)(0xBB34 + (i)))  /* u8 options 0..6: animations,
                                           battle speed 0..9, game music, battle music,
                                           sound effects, difficulty, big view */
#define g_s3d_minute    DGS16(0x3D64)   /* i16  calendar (save: 25, 10, 349) */
#define g_s3d_day       DGS16(0x3D68)   /* i16  day counter, wraps at 349 (15Dh) */
#define g_s3d_encounter DGS16(0x3D6A)   /* i16  1 = a random encounter is due */
#define g_s3d_hungry_said DG8(0x3D6C)   /* u8   bit p-1: member p said "ben acıktım" */
#define g_s3d_fade_tempo DGS16(0x3D6E)  /* i16  pending tempo change (sound options) */
#define g_s3d_fade_tempo_step DGS8(0x3D74) /* i8  its step */
#define g_s3d_fade_pal_step DGS8(0x3D75)   /* i8  = demolar.h g_fade_speed */
#define g_s3d_fade_vol_step DGS8(0x3D76)   /* i8  music fade step */
#define g_s3d_automap   DG8(0x3B05)     /* u8   automap shown instead of the view */
#define g_s3d_save_sel  DGS16(0x3F12)   /* i16  last chosen save slot (initially 1) */
#define g_s3d_fade_time DG16(0xBA82)    /* u16  clock (low word) of the last fade step */
#define g_s3d_enc_kind  DGS16(0xBA84)   /* i16  encounter table row: 0 night, 1 day, 2+ fixed */
#define g_s3d_enc_rate  DGS16(0xBA86)   /* i16  encounter chance of the map (header [2]) */
/* enemy groups i = 1..3 at 5BD8 + 6i {active, type, count} (= iso.h
   g_iso_group_type), a check copy at 5BF0 + 6i */
#define g_s3d_grp_on(i)    DGS16((uint16_t)(0x5BD8 + 6 * (i)))
#define g_s3d_grp_type(i)  DGS16((uint16_t)(0x5BDA + 6 * (i)))
#define g_s3d_grp_count(i) DGS16((uint16_t)(0x5BDC + 6 * (i)))
#define g_s3d_grp_chk_on(i)    DGS16((uint16_t)(0x5BF0 + 6 * (i)))
#define g_s3d_grp_chk_type(i)  DGS16((uint16_t)(0x5BF2 + 6 * (i)))
#define g_s3d_grp_chk_count(i) DGS16((uint16_t)(0x5BF4 + 6 * (i)))
/* monster types t: 6 bytes at 2894 + 6t: [0] count base (low nibble) and
   random range (high nibble), [1..3] loot words, [4] loot count byte,
   [5] companion type */
#define g_s3d_monster(t, k) DG8((uint16_t)(0x2894 + 6 * (t) + (k)))
void    s3d_register_far_fns_menu(void);   /* s3d_menu.c, called by s3d_register_far_fns */
/* ---- end menu/loop ---- */

/* ---- functions (original address order) ---------------------------------- */
/* s3d_move.c */
uint8_t s3d_try_move(int16_t dx, int16_t dy, uint8_t facing);              /* 303F:0000 */
void    s3d_random_view_effect(void);                                      /* 303F:0715 */
/*      s3d_redraw_view(int16_t force)  - declared in q3dview.h               303F:07B6 */
void    s3d_scan_cells(uint8_t value);                                     /* 303F:0991 */
/* s3d_keys.c */
void    s3d_show_location(void);                                           /* 303F:0BDB */
void    s3d_handle_key(void);                                              /* 303F:0FFC */
void    s3d_load_file_error(const char *name);                             /* 303F:189D */
/* s3d_district.c */
void    s3d_load_district(int16_t district, uint8_t first);                /* 303F:1927 */
void    s3d_debug_buffers(uint8_t n);                                      /* 303F:23ED */
void    s3d_enter_view(void);                                              /* 303F:2521 */
void    s3d_redraw_screen(void);                                           /* 303F:276C */
void    s3d_draw_portrait(void);                                           /* 303F:27F7 */
/* s3d_panel.c */
void    s3d_draw_party_panel(void);                                        /* 303F:2C69 */
void    s3d_stub_392D(void);                                               /* 303F:392D */
void    s3d_load_ui_images(void);                                          /* 303F:394A */
int16_t s3d_advance_clock(int16_t minutes);                                /* 303F:39FD */
uint16_t s3d_calc_daylight(void);                                          /* 303F:3BF3 */
void    s3d_apply_daylight(void);                                          /* 303F:3CCE */
/*      s3d_draw_portrait_hl(void)  - declared in q3dview.h                   303F:75EF */
/* s3d_save.c */
void    s3d_write_save(const char *name, const char *desc);                /* 303F:3DD0 */
void    s3d_read_save(const char *name);                                   /* 303F:3F95 */
/* s3d_camp.c */
void    s3d_camp(void);                                                    /* 303F:4353 */
char   *s3d_make_random_phrase(int16_t n);                                 /* 303F:4A27 */
void    s3d_draw_status_line(int16_t a, int16_t b);                        /* 303F:4D22 */
/* s3d_charscr.c */
void    s3d_character_screen(void);                                        /* 303F:4F3A */
/* s3d_menu.c */
void    s3d_leave_district(void);                                          /* 303F:765C */
void    s3d_options_menu(uint8_t mode);                                    /* 303F:7A71 */
uint16_t s3d_save_load_menu(int16_t mode);                                 /* 303F:7F2F */
void    s3d_apply_sound_options(void);                                     /* 303F:837A */
/* s3d_loop.c */
void    s3d_explore_loop(void);                                            /* 303F:8524 */

/* Registers the far code addresses of 3DSYS routines that are stored in
   window records or called through overlay thunks (intrface.h). */
void    s3d_register_far_fns(void);

#endif
