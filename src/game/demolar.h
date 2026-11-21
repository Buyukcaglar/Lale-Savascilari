/*
 * demolar.h - DEMOLAR.L: intro, main menu, story slideshow, game over,
 * ending, credits ("thanx.doc" scroller), the city map and the music
 * helpers (IST.EXE ovl3C2D, 3C2D:0000-3A28, source T:\CODE\DEMOLAR.L per
 * its fatal-error strings).
 *
 * Entry thunks (stub segment 1682, build/ist_map.json):
 *   1682:0020 demo_story_scene      1682:0039 demo_credits_exit
 *   1682:0025 demo_load_sounds      1682:003E demo_city_map_travel
 *   1682:002A demo_intro_menu       1682:0043 demo_play_music
 *   1682:002F demo_game_over        1682:0048 demo_free_music
 *   1682:0034 demo_ending           1682:004D demo_wait_key_or_delay
 *   1682:0052 demo_show_picture     1682:0057 demo_palette_effect
 *   1682:005C demo_stub_2D3E
 * demo_register_far_fns() registers the void(void) ones (0025, 002F,
 * 0034, 0039) with ui_register_far_fn (INTRFACE calls 1682:002F).
 *
 * The game's music module (DGROUP 16E0:BB75, a far MODULE *) is shared by
 * 3DSYS, FRPOS, ISOSYS, main and this module; in the port it is the native
 * pointer g_music_module below.  dg[BB75..BB78] is not kept up to date.
 */
#ifndef LALE_GAME_DEMOLAR_H
#define LALE_GAME_DEMOLAR_H

#include <stdint.h>

#include "dsmi/amp.h"
#include "engine/dgroup.h"
#include "engine/memman.h"

/* ---- DGROUP ------------------------------------------------------------- */
extern AmpModule *g_music_module;          /* 16E0:BB75 far MODULE * (native)      */
#define g_music_track      DGS16(0xBB4D)   /* i16  index into g_music_names         */
#define g_music_names(i)   DGSTR(0x2C91 + 13 * (i))  /* char[13] AMF file names     */
#define g_map_x            DGS16(0xBB8A)   /* i16  city map marker x (top-left)     */
#define g_map_y            DGS16(0xBB88)   /* i16  city map marker y (top-left)     */
#define g_sfx_level        DGS16(0x731C)   /* i16  third fader (set to max by main) */
#define g_sfx_level_max    DGS16(0x7322)   /* i16                                   */
#define g_fade_bright_d    DGS16(0x3D70)   /* i16  pending palette fade (s3d_apply_sound_options) */
#define g_fade_volume_d    DGS16(0x3D72)   /* i16  pending music fade                */
#define g_fade_speed       DG8(0x3D75)     /* u8   fade step size                    */
#define DEMO_STORY_TEXTS   0x4AE6          /* 10 far char * (story pages)           */
#define DEMO_STORY_COUNTS  0x4B0E          /* u8[8] slides per picture             */
#define DEMO_DISTRICTS     0x4B16          /* 12 far char * (district names)        */
#define DEMO_MAP_RECTS     0x4A86          /* 12 x {i16 x1, y1, x2, y2}             */
#define DEMO_STORY_PIC     0x555C          /* "sbd00.cmp", byte +4 counts up        */
#define DEMO_STORY_SND     0x5566          /* "para00.snd", byte +5 counts up       */

/* ---- functions (original address order) --------------------------------- */
int16_t demo_wait_key_or_delay(uint8_t unused1, uint8_t unused2, uint16_t ms);  /* 3C2D:0000 */
uint8_t demo_show_picture(MmHandle *a, MmHandle *b, uint16_t dur, uint8_t c, uint8_t d,
                          uint16_t wait_ms, uint16_t dst_off);                  /* 3C2D:0086 */
int16_t demo_intro_menu(int16_t slot, uint8_t arg);                              /* 3C2D:054D */
void    demo_story_scene(int16_t slot);                                          /* 3C2D:1108 */
void    demo_game_over(void);                                                    /* 3C2D:19F1 */
void    demo_ending(void);                                                       /* 3C2D:1B76 */
/* options is a long in the original: opt_lo | opt_hi << 16 */
void    demo_play_music(int16_t track, int16_t opt_lo, int16_t opt_hi);          /* 3C2D:1F32 */
void    demo_credits_exit(void);                                                 /* 3C2D:217C */
void    demo_palette_effect(uint8_t zone, int16_t yoff);                         /* 3C2D:2992 */
uint8_t demo_stub_2D3E(uint8_t idx);                                             /* 3C2D:2D3E */
int16_t demo_city_map_travel(void);                                              /* 3C2D:2DE2 */
uint8_t demo_free_music(void);                                                   /* 3C2D:371E */
void    demo_load_sounds(void);                                                  /* 3C2D:3790 */

/* port: register the 1682:xxxx thunks with ui_register_far_fn */
void    demo_register_far_fns(void);

#endif
