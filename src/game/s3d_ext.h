/*
 * s3d_ext.h - routines of other game modules that 3DSYS calls (through
 * their overlay thunks 1682 DEMOLAR, 1689 FRPOS, 1694 ISOSYS).  The owning
 * module's header is included when it exists; the prototypes below are
 * repeated anyway ("provided by WPnn", docs/GAME_WAVE.md rule 2) because
 * those headers are still being filled in - they must stay identical to
 * the owners' declarations (a mismatch is a compile error).  The argument
 * lists come from the 3DSYS call sites.
 */
#ifndef LALE_GAME_S3D_EXT_H
#define LALE_GAME_S3D_EXT_H

#include <stdint.h>

/* ---- DEMOLAR.L ovl3C2D, provided by WP13 ---------------------------------- */
#if __has_include("game/demolar.h")
#include "game/demolar.h"
#endif
void    demo_game_over(void);                                   /* 3C2D:19F1 */
void    demo_ending(void);                                      /* 3C2D:1B76 */
void    demo_play_music(int16_t track, int16_t opt_lo, int16_t opt_hi); /* 3C2D:1F32 */
void    demo_credits_exit(void);                                /* 3C2D:217C */
int16_t demo_city_map_travel(void);                             /* 3C2D:2DE2 */
/* far MODULE * at 16E0:BB75: the music module loaded by demo_play_music,
   kept natively by DEMOLAR */
struct AmpModule;
extern struct AmpModule *g_music_module;

/* ---- FRPOS.L ovl3FD1, provided by WP15 ------------------------------------ */
#if __has_include("game/frpos.h")
#include "game/frpos.h"
#endif
uint8_t fr_run_event(int16_t cell_off, uint8_t trigger);         /* 3FD1:0000 */
void    fr_redraw(void);                                        /* 3FD1:20CE */
void    fr_item_exchange(uint8_t mode);                         /* 3FD1:2F21 */
uint8_t fr_flag_test(int16_t n);                                /* 3FD1:4AAC */
void    fr_show_icon(uint8_t icon, uint8_t wait);               /* 3FD1:4B57 */
void    fr_draw_money(void);                                    /* 3FD1:5550 */

/* ---- ISOSYS.L ovl4595, provided by WP17 ----------------------------------- */
#if __has_include("game/iso.h")
#include "game/iso.h"
#endif
void    iso_draw_text_shadow(const char *s, int16_t x, int16_t y, int16_t color,
                             int16_t k);                        /* 4595:019F */
void    iso_clamp_add(int16_t *p, int16_t d, int16_t lo, int16_t hi); /* 4595:0202 */
void    iso_show_picture(int16_t n);                            /* 4595:0BE1 */
int16_t iso_has_status(int16_t c, int16_t id);                  /* 4595:20E0 */
void    iso_speak_vecize(int16_t n);                            /* 4595:235B */
int16_t iso_step_sfx(int16_t tile);                             /* 4595:2C7B */
void    iso_cmd_vecize(void);                                   /* 4595:50BD */
void    iso_party_to_battle(void);                              /* 4595:6345 */
void    iso_calc_weapon(void);                                  /* 4595:6828 */
int16_t iso_calc_stat_a(void);                                  /* 4595:6AC2 */
int16_t iso_calc_stat_b(void);                                  /* 4595:6B79 */
int16_t iso_calc_stat_c(void);                                  /* 4595:6CD6 */
int16_t iso_calc_ap_by_load(void);                              /* 4595:6E4D */
void    iso_use_item(void);                                     /* 4595:7A03 */
void    iso_draw_textbox(const char *s, int16_t a, int16_t b);  /* 4595:7F29 */
void    iso_refresh_desc(void);                                 /* 4595:80D6 */
void    iso_battle_to_party(void);                              /* 4595:8111 */
void    iso_cast_vecize(void);                                  /* 4595:84E9 */
void    iso_battle(void);                                       /* 4595:B40A */

#endif
