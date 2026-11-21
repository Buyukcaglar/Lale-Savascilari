/*
 * frpos.h - FRPOS.L: the map-cell event / shop script interpreter
 * (IST.EXE ovl3FD1, source file T:\CODE\FRPOS.L per its fatal-error strings).
 *
 * Every district has a map PCn and a script bank PCSn (FRPOS.CKD, both AMOS
 * banks; format: docs/FORMATS.md "FRPOS event scripts").  3DSYS loads them
 * (s3d_load_district): the map cells are at s3d_map (16E0:B9EC, see
 * q3dview.h), the whole map file is the MEMMAN block whose record 16E0:E33F
 * points to, the whole script file the block of 16E0:E34F (both EMS).
 *
 * How scripts run.  3DSYS calls fr_run_event(cell_off, trigger) for a map
 * cell: trigger 1 = the party enters the cell, 3 = it leaves it
 * (s3d_try_move), 2 = "search/use" on the cell (s3d_explore_loop).  Bytes
 * 11..14 of the cell form the event key; "00000000" = no event, the keys
 * "31313127".."31313131" are hard-coded shops (doctor, food, weapons,
 * general goods, a state flag), anything else selects an event of the
 * district's script.  The event's section for the trigger is interpreted
 * command by command (fr_fetch_byte reads at PC 16E0:BB98 through EMS).
 * fr_run_event returns 1 when the script teleported the party (op 11).
 * A script may set g_fr_cancel_move (BBA8) to stop the move that triggered
 * it (3DSYS checks it after the call).
 *
 * Implementation: frpos.c (3FD1:0000-20CD fr_run_event with the opcode
 * switch, fr_redraw, fr_teach_aphorism), frpos_shop.c (23B2-46A9: the
 * item exchange / shop screen), frpos_util.c (46AA-5C25: script access,
 * flags, icons, menu, money panel, shop graphics).
 */
#ifndef LALE_GAME_FRPOS_H
#define LALE_GAME_FRPOS_H

#include <stdint.h>

#include "engine/dgroup.h"
#include "game/chargen.h"

/* ---- DGROUP ------------------------------------------------------------- */
/* Party stat records (chargen.h PARTY_STATS_OFF, 5Ch bytes, index 0..4):
   word i at +2i, as the scripts address them (op 12/22/23). */
#define FR_STAT(p, i)        DGS16((uint16_t)(PARTY_STATS_OFF + 0x5C * (p) + 2 * (i)))
#define FR_STAT_HP           0x1D          /* word index of +3Ah: health */
#define FR_STAT_HUNGER       0x11          /* word index of +22h: hunger counter */
#define g_fr_flags           DGP(0xB9CA)   /* u8[32] 256 event flags (saved) */
#define g_fr_view_force      DGS16(0xB9C8) /* i16  argument of the final s3d_redraw_view:
                                              1 = redraw the view after the script */
#define g_fr_sect_flags      DG8(0xBB92)   /* u8   second byte of the running section
                                              (read, never used by FRPOS) */
#define g_fr_count           DG8(0xBB93)   /* u8   commands left in the section */
#define g_fr_pc              DG16(0xBB98)  /* u16  script PC: offset in the PCS file
                                              (incl. its 20-byte AmBk header); also the
                                              key-building index at the start of
                                              fr_run_event */
#define g_fr_who             DGS16(0xBB9C) /* i16  selected party member (ops 22-24) */
#define g_fr_acc             DGS16(0xBB9E) /* i16  accumulator (= g_ui_dialog_result) */
#define g_fr_xchg_a          DGS16(0xBBA2) /* i16  op 9 operands: item exchange set-up */
#define g_fr_xchg_b          DGS16(0xBBA4)
#define g_fr_xchg_c          DGS16(0xBBA6)
#define g_fr_cancel_move     DGS16(0xBBA8) /* i16  1 = cancel the move (3DSYS clears it) */
#define g_fr_xchg_w1         DGS16(0xBB57) /* i16  op 9 word operands */
#define g_fr_xchg_w2         DGS16(0xBB59)
#define g_fr_xchg_w3         DGS16(0xBB5B)
#define g_fr_debug          DG8(0x5C0B)   /* u8   debug traces (printf + getch) when > 0 /
                                              > 1; 0 in the shipped game */
#define g_fr_doctor_price    DG8(0xBBAB)   /* u8   doctor's fee (random(100)+101 on district
                                              load) */
#define g_fr_money           DGS16(0xBB73) /* i16  party money */

/* ---- functions (original address order) ---------------------------------- */
uint8_t fr_run_event(int16_t cell_off, uint8_t trigger);                  /* 3FD1:0000 */
void    fr_redraw(void);                                                  /* 3FD1:20CE */
void    fr_teach_aphorism(void);                                          /* 3FD1:2104 */
int16_t fr_get_e5e3(void);                                                /* 3FD1:23B2 */
void    fr_shop_draw_item(int16_t idx);                                   /* 3FD1:23C0 */
void    fr_shop_draw_text(void);                                          /* 3FD1:27C4 */
int16_t fr_shop_draw_cursor(void);                                        /* 3FD1:296A */
void    fr_shop_buy(int16_t *sel, int16_t who, uint8_t mode, int16_t base); /* 3FD1:2B1A */
void    fr_shop_draw_prices(uint8_t mode);                                /* 3FD1:2D6F */
void    fr_item_exchange(uint8_t mode);                                   /* 3FD1:2F21 */
void    fr_decode_text(char *s);                                          /* 3FD1:46AA */
int16_t fr_find_character(int16_t mode, int16_t a, int16_t b);            /* 3FD1:46E3 */
uint8_t fr_flag_test(int16_t n);                                          /* 3FD1:4AAC */
void    fr_flag_set(int16_t n);                                           /* 3FD1:4AD9 */
void    fr_flag_clear(int16_t n);                                         /* 3FD1:4B04 */
void    fr_flags_reset(void);                                             /* 3FD1:4B31 */
void    fr_show_icon(uint8_t icon, uint8_t wait);                         /* 3FD1:4B57 */
void    fr_stub_4E42(int16_t x, int16_t y, int16_t w, int16_t h, int16_t slot,
                     int16_t color);                                      /* 3FD1:4E42 */
uint8_t fr_fetch_byte(void);                                              /* 3FD1:4E63 */
void    fr_skip_commands(int16_t n);                                      /* 3FD1:4ECB */
char   *fr_fetch_string(int16_t n);                                       /* 3FD1:509D */
void    fr_item_name(uint8_t mode, int16_t price);                        /* 3FD1:513F */
void    fr_draw_text_line(char *s);                                       /* 3FD1:51C7 */
uint8_t fr_menu(char **items);                                            /* 3FD1:5285 */
void    fr_draw_money(void);                                              /* 3FD1:5550 */
void    fr_shop_load_gfx(void);                                           /* 3FD1:57E8 */
void    fr_shop_free_gfx(void);                                           /* 3FD1:5A7F */

/* Port: register the FRPOS routines that INTRFACE's window records hold as
   far code pointers (1689:003E fill = fr_stub_4E42, 1689:0098 cursor shape =
   fr_get_e5e3).  Call once at start-up, after ui_init_tables is possible. */
void    fr_register_far_fns(void);

#endif
