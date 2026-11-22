/*
 * intrface.h - ALTDUZEY\INTRFACE.L: the game's UI layer - the bottom panel
 * (save/restore of the screen under it), list menus driven by window
 * records of callbacks, message boxes and two-choice dialogs, the voice
 * (speech sample) player and small input waits.
 * Original: IST.EXE seg05DC (Borland C, far cdecl).
 * Implementation: src/engine/intrface.c.
 *
 * Window records.  A window is a 48h-byte record (UI_WIN_SIZE); the game
 * copies the one it wants (9674 + 48h*k, filled by ui_init_tables) to the
 * current record at 962C with F_SCOPY / ui_memcpy, and the menu code calls
 * through its far code pointers.  The port keeps the far pointers in dg[]
 * exactly as the original stores them (runtime segment = flat segment +
 * 0824h) and dispatches through a registry: ui_far_fn(addr) gives the
 * native function registered for that address.  INTRFACE's own functions
 * (05DC:xxxx) are built in; other modules register theirs with
 * ui_register_far_fn(flat seg, off, fn) at start-up (also the routines
 * INTRFACE calls directly through overlay thunks, see UI_EXT_*).  An
 * unregistered address is fatal (the original would jump into nowhere).
 *
 * Current record (962C, offsets +00..+47) and the call shapes:
 *   +00 962C  void (int16_t *count, int16_t n)            open (count may change)
 *   +04 9630  void (int16_t x, int16_t y, int16_t h, int16_t scroll)  draw frame
 *   +08 9634  void (int16_t x, int16_t y, int16_t h)      after the list is drawn
 *   +0C 9638  void (int16_t idx, int16_t x, int16_t row, int16_t h)  item hot zone
 *   +10 963C  void (void)                                 selection moved
 *   +14 9640  void (void)                                 closed
 *   +18 9644  void (const char *s, int16_t x, int16_t row, uint8_t color, int16_t h) item
 *   +1C 9648  void (x, y, w, h, slot, color: int16_t)     fill (ui_save_background)
 *   +20 964C  void (uint32_t arg)                         idle, arg = far value at 9660
 *   +24 9650  int16_t (void)                              cursor shape (ui_call_9650)
 *   +28 9654  (used by the game only)
 *   +2C 9658 panel y, +2E 965A panel height, +30 965C hotspot zone of the
 *   panel, +32 965E x of the button icon in slot 4Eh, +34 9660 idle
 *   argument, +38.. colours, +45 9671 cursor shape, +46 9672 record id /
 *   voice mode, +47 9673 text style.
 *
 * Keyboard.  The original reads keys with kbhit/getch (DOS) and bioskey
 * (INT 16h).  Both work on the pc layer's BIOS buffer (pc_bioskey,
 * pc_kbhit, pc_getch); ui_bioskey/ui_kbhit/ui_getch/ui_flush_keys are thin
 * named wrappers over them.
 *
 * Interactive loops poll: every wait loop of the original spins on the
 * clock or the keyboard; the port calls pc_poll() (directly or through
 * pc_kbhit) once per iteration.
 */
#ifndef LALE_ENGINE_INTRFACE_H
#define LALE_ENGINE_INTRFACE_H

#include <stdint.h>

#include "engine/dgroup.h"

/* ---- DGROUP ------------------------------------------------------------- */
#define UI_WIN_SIZE          0x48
#define UI_WIN_CUR           0x962C         /* current window record */
#define UI_WIN_REC(k)        ((uint16_t)(0x9674 + UI_WIN_SIZE * (k)))  /* k = 0..8 */
#define UI_CB_OPEN           0x962C         /* far code pointers of the current record */
#define UI_CB_FRAME          0x9630
#define UI_CB_LIST_DONE      0x9634
#define UI_CB_ITEM_ZONE      0x9638
#define UI_CB_MOVED          0x963C
#define UI_CB_CLOSE          0x9640
#define UI_CB_ITEM           0x9644
#define UI_CB_FILL           0x9648
#define UI_CB_IDLE           0x964C
#define UI_CB_SHAPE          0x9650
#define g_ui_panel_y         DGS16(0x9658)  /* i16  y of the bottom panel */
#define g_ui_panel_h         DGS16(0x965A)  /* i16  its height */
#define g_ui_panel_zone      DG16(0x965C)   /* u16  hotspot rectangle of the panel */
#define g_ui_icon_x          DG16(0x965E)   /* u16  x of the 12x32 button icon in slot 4Eh */
#define g_ui_idle_arg        DG32(0x9660)   /* u32  far value handed to the idle callback */
#define g_ui_col_initial     DG8(0x9665)    /* u8   colour of a message's first letter */
#define g_ui_col_text        DG8(0x9666)    /* u8   text colour */
#define g_ui_col_shadow      DG8(0x9667)    /* u8   list item shadow colour */
#define g_ui_col_outline     DG8(0x9668)    /* u8   outline colour (ui_format_text flag 2) */
#define g_ui_col_text2       DG8(0x966C)    /* u8   message text colour, mode 0 */
#define g_ui_shape           DG8(0x9671)    /* u8   cursor shape over the panel */
#define g_ui_voice_mode      DG8(0x9672)    /* u8   which voices ui_init_voices keeps (0,1,3) */
#define g_ui_text_style      DG8(0x9673)    /* u8   2 = outlined message text */
#define g_ui_list_key        DGS16(0xD5D6)  /* i16  scan code peeked by ui_event_loop */
#define g_ui_list_top        DGS16(0xD5D8)  /* i16  first visible entry (1-based) */
#define g_ui_list_sel        DGS16(0xD5DC)  /* i16  selected entry (0 = cancelled) */
#define g_ui_list_prev       DGS16(0xD5E4)  /* i16  previously highlighted entry */
#define UI_LIST_ENTRY(n)     ((uint16_t)(0xA5D8 + 0x64 * (n)))  /* char[100] entries */
#define g_voice_sample_end   DG32(0x73CC)   /* u32  length of the playing voice sample */
#define g_voice_end_time     DG32(0x73D1)   /* u32  clock when the voice's wait ends, -1 none */
#define g_voice_len          DG16(0x73D5)   /* u16  that wait in clock units */
#define UI_VOICE_SILENCE     0xDF1E         /* SAMPLEINFO of sound slot 0Ah (bosch.pur, the empty
                                               sample) set on the voice channel when done:
                                               05DC:1E3C PUSH DF1Eh */
#define g_ui_frame_ticks     DG8(0xBB35)    /* u8   ui_wait_frames: n*(n+1) clock units/frame */
#define g_ui_msg_buf         DGSTR(0xBACC)  /* char[] sprintf buffer of the voice errors */
#define g_ui_dialog_result   DGS16(0xBB9E)  /* i16  ui_dialog_2198: 100 waiting, 0 / 1 */
#define g_ui_msg_lines       DG8(0xBBAA)    /* u8   message lines on the panel (max 6) */
/* sound table record of slot n seen from INTRFACE: {char name[13]; u16 rate;
   u16 flags} at 22DB + 11h*n (flags = oses.h g_snd_table_flags(n)) */
#define g_snd_name(n)        DGSTR((uint16_t)(0x22DB + (n) * 0x11))
#define g_snd_rate(n)        DG16((uint16_t)(0x22E8 + (n) * 0x11))
#define g_party_cur          DGS16(0x2898)  /* i16  current party member */
#define g_party_word(p, i)   DGS16((uint16_t)(0x99D2 + 0x5C * (p) + 2 * (i)))  /* i = 1..5:
                                               condition ids shown by ui_setup_hotspots */
#define g_party_hp(p)        DGS16((uint16_t)(0x99E0 + 0x5C * (p)))  /* p = 1..4 */
#define UI_COND_NAMES        0x0361         /* char[1Fh] condition names, index v % 100 */

/* ---- far code pointer registry ------------------------------------------ */
typedef void (*UiFarFn)(void);
#define UI_LOAD_SEG          0x0824u        /* runtime = flat segment + this */
#define UI_FAR(seg, off)     ((uint32_t)((uint16_t)((seg) + UI_LOAD_SEG)) << 16 | (uint16_t)(off))
/* Routines of other modules INTRFACE calls through their overlay thunks: */
#define UI_EXT_DRAW_TEXT_SHADOW 0x1694, 0x0052 /* void (const char *s, int16_t x, int16_t y,
                                                  int16_t color, int16_t k)  iso_draw_text_shadow */
#define UI_EXT_CLAMP_ADD     0x1694, 0x0025 /* void (int16_t *p, int16_t d, int16_t lo,
                                               int16_t hi)                 iso_clamp_add */
#define UI_EXT_SOUND_OPTIONS 0x1670, 0x009D /* void (uint32_t arg)  s3d_apply_sound_options */
#define UI_EXT_PARTY_PANEL   0x1670, 0x003E /* void (void)          s3d_draw_party_panel */
#define UI_EXT_GAME_OVER     0x1682, 0x002F /* void (void)          demo_game_over */

void    ui_register_far_fn(uint16_t flat_seg, uint16_t off, UiFarFn fn);
UiFarFn ui_far_fn(uint32_t far_addr);     /* runtime seg:off; fatal if unknown */
void    ui_far_set(uint16_t at, uint16_t flat_seg, uint16_t off);  /* store a far code ptr */

/* ---- keyboard (see above) ------------------------------------------------- */
int16_t ui_bioskey(int16_t cmd);
int16_t ui_kbhit(void);
int16_t ui_getch(void);
void    ui_flush_keys(void);
void    ui_bioskey_reset(void);

/* ---- functions (original address order) ------------------------------------ */
void    ui_text_measure_0004(const char *s, int16_t col, int16_t row, uint8_t color,
                             int16_t h);                                   /* 05DC:0004 */
void    ui_nop(void);                                                      /* 05DC:00CD */
int16_t ui_return_zero(void);                                              /* 05DC:00D2 */
int16_t ui_get_9671(void);                                                 /* 05DC:00D9 */
void    ui_button_press_anim(int16_t *count, int16_t n);                   /* 05DC:00E9 */
void    ui_button_release_anim(int16_t *count, int16_t n);                 /* 05DC:0167 */
void    ui_draw_panel_027B(int16_t x, int16_t y, int16_t h, int16_t icon); /* 05DC:027B */
void    ui_draw_panel_0308(int16_t x, int16_t y, int16_t h, int16_t icon); /* 05DC:0308 */
void    ui_setup_hotspots(void);                                           /* 05DC:041C */
void    ui_rect_op_0568(int16_t n, int16_t col, int16_t row, int16_t h);   /* 05DC:0568 */
void    ui_rect_op_05E4(int16_t n, int16_t col, int16_t row, int16_t h);   /* 05DC:05E4 */
void    ui_update_hover(int16_t x, int16_t y, int16_t h);                  /* 05DC:066B */
void    ui_unhighlight(void);                                              /* 05DC:0831 */
void    ui_free_panel(void);                                               /* 05DC:0881 */
void    ui_memcpy(uint8_t *dst, const uint8_t *src);                       /* 05DC:08DB */
void    ui_cd_music(uint32_t arg);                                         /* 05DC:08F6 */
void    ui_init_tables(void);                                              /* 05DC:0940 */
void    ui_event_loop(int16_t n, int16_t x, int16_t y, int16_t col_item, int16_t col_sel,
                      int16_t col_done, int16_t count, int16_t style);     /* 05DC:1133 */
void    ui_init_voices(void);                                              /* 05DC:178E */
void    ui_load_voice(int16_t slot, uint32_t freq, uint16_t len, uint16_t flags); /* 05DC:192D */
uint8_t ui_play_voice(int16_t slot, uint16_t flags);                       /* 05DC:1B6A */
int16_t ui_wait_voice(void);                                               /* 05DC:1D1B */
void    ui_wait_frames(int16_t n);                                         /* 05DC:1E5E */
void    ui_restore_background(void);                                       /* 05DC:1EB9 */
void    ui_save_background(void);                                          /* 05DC:1F6A */
uint8_t ui_poll_input(uint8_t base);                                       /* 05DC:2083 */
void    ui_dialog_2198(char *a, char *b);                                  /* 05DC:2198 */
uint8_t ui_format_text(uint8_t dst, char *s, int16_t x, int16_t y, uint8_t color,
                       int16_t *pos, uint8_t flags);                       /* 05DC:25EC */
uint8_t ui_message_box(char *s, int16_t mode, int16_t who);                /* 05DC:2948 */
int16_t ui_call_9650(void);                                                /* 05DC:2D9A */
void    ui_wait_click_or_key(int16_t n);                                   /* 05DC:2DCA */
uint8_t ui_ask_evet_hayir(void);                                           /* 05DC:2EEC */

#endif
