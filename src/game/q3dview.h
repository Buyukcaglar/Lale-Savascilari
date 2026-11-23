/*
 * q3dview.h - the first-person renderer (IST.EXE ovl2700, source file
 * T:\CODE\3DQUICK.L per its fatal-error strings).
 *
 * The view is composed from pre-drawn wall textures ("pieces"): for every
 * one of 37 wall positions visible from the party's cell there is a piece
 * record (Q3dPiece) with the destination rectangle in the view buffer and
 * the source rectangle in the wall texture.  The renderer walks the map
 * cells in front of the party from far to near (five layers) and blits the
 * pieces of the walls it finds.  The turn/step animations (2700:647B ...)
 * scale and slide the rendered view; the rotations use the seg08D0 quads
 * (src/game/q3d.h).
 *
 * Tables.  At start-up main() runs q3d_precalc_table_a(view_w, view_h,
 * g_q3d_pieces) (destination rectangles, x87 arithmetic, see
 * q3dview_tables.c), q3d_precalc_table_b1 (texture rectangles for the
 * 200x200 textures into g_q3d_pieces) and q3d_precalc_table_c1 (the same
 * into g_q3d_pieces_c).  A wall set may come with a .KOR file that
 * replaces the texture rectangles (q3d_load_kor); each of the 8 wall-set
 * slots then owns a farmalloc'd copy of the table.
 *
 * Implementation: q3dview.c (2700:0000-37EE: buffers, tables, .KOR,
 * strips), q3dview_render.c (3932-5F31: the five layers), q3dview_map.c
 * (5F32-647A: automap, piece touch-ups), q3dview_anim.c (647B-93D5).
 */
#ifndef LALE_GAME_Q3DVIEW_H
#define LALE_GAME_Q3DVIEW_H

#include <stdint.h>

#include "engine/dgroup.h"

#pragma pack(push, 1)
/* One wall piece, 12h bytes. */
typedef struct Q3dPiece {
    int16_t  x, y, w, h;        /* +0  destination in the view buffer (table A) */
    int16_t  tx, ty, tw, th;    /* +8  source rectangle in the wall texture (B/C, .KOR) */
    uint16_t flags;             /* +10h bit0 mirrored, bit1 scaled (tw x th -> w x h);
                                   0 = plain colour-key copy of w x h */
} Q3dPiece;
_Static_assert(sizeof(Q3dPiece) == 0x12, "Q3dPiece is 12h bytes");

/* .KOR state of one wall-set slot (8 slots at B3E8, 0Eh bytes each).  3DSYS
   presets version = 1, n = 6, w = h = 200 before calling q3d_load_kor. */
typedef struct Q3dKorSlot {
    int16_t  version;           /* +0  file version, must be 1 */
    uint16_t n;                 /* +2  bytes of header data that follow */
    int16_t  hdr0;              /* +4  first header word (unused by 3DQUICK) */
    int16_t  w, h;              /* +6  view size the .KOR was designed for */
    uint16_t pieces_off;        /* +0Ah far Q3dPiece * (native: q3d_kor_pieces[]) */
    uint16_t pieces_seg;
} Q3dKorSlot;
_Static_assert(sizeof(Q3dKorSlot) == 0x0E, "Q3dKorSlot is 0Eh bytes");

/* Placement of the 4 decoration pictures (surfaces 20h-23h), 0Eh bytes at
   B458, computed by q3d_reset_kor (centred on the front wall piece 23). */
typedef struct Q3dDecor {
    int16_t  w, h;              /* +0  size in the view */
    int16_t  x, y;              /* +4  position (centred on piece 23, bottom aligned) */
    int16_t  src_w, src_h;      /* +8  surface width / 4, surface height */
    int16_t  odd;               /* +0Ch (view_w - w) % 2 */
} Q3dDecor;
_Static_assert(sizeof(Q3dDecor) == 0x0E, "Q3dDecor is 0Eh bytes");
#pragma pack(pop)

#define Q3D_PIECES      37
#define Q3D_KOR_SLOTS   8

/* ---- DGROUP --------------------------------------------------------------- */
#define g_q3d_win_w     DGS16(0x3AFB)   /* i16  size of the view window on screen */
#define g_q3d_win_h     DGS16(0x3AFD)   /* i16  (200x200, or 100 wide = half) */
#define g_q3d_view_w    DGS16(0x3AFF)   /* i16  size of the rendered view buffer */
#define g_q3d_view_h    DGS16(0x3B01)   /* i16  (100x100 low detail, 200x200 high) */
#define g_q3d_view_ox   DGS16(0x3B09)   /* i16  offset added to every piece x */
#define g_q3d_view_oy   DGS16(0x3B0B)   /* i16  offset added to every piece y */
#define g_q3d_opt_flags DG16(0x3D5E)    /* u16  game option/state bits; 20h = the wall
                                           textures are loaded (q3d_free_view_buffers) */
#define g_q3d_tex_names DGSTR(0x2944)   /* char[][13]: name of the texture of surface n
                                           at 2944 + 13n (n = 24h..3Fh: wall sets) */
#define g_q3d_shift_y   DGS16(0xB3E4)   /* i16  view shake: source offset (negative
                                           values move the picture) */
#define g_q3d_shift_x   DGS16(0xB3E6)   /* i16 */
#define g_q3d_kor       ((Q3dKorSlot *)DGP(0xB3E8))   /* Q3D_KOR_SLOTS */
#define g_q3d_decor     ((Q3dDecor *)DGP(0xB458))     /* 4 */
#define g_q3d_pieces_c  ((Q3dPiece *)DGP(0xB490))     /* Q3D_PIECES, table C */
#define g_q3d_pieces    ((Q3dPiece *)DGP(0xB72A))     /* Q3D_PIECES, tables A + B */
/* NB: absolute offsets B72A..B9C3 in the listing are fields of g_q3d_pieces:
   B72A + 12h * i + field (e.g. B8C8 = piece 23 .x, B912 = piece 27 .y). */
#define g_s3d_step_dy   DGS16(0xB9C4)   /* i16  map step for "forward" (set from the */
#define g_s3d_step_dx   DGS16(0xB9C6)   /*      facing by q3d_alloc_view_buffers) */
#define g_s3d_map_h     DG8(0xB9EA)     /* u8   map height in cells (map byte 1) */
#define g_s3d_map_w     DG8(0xB9EB)     /* u8   map width in cells (map byte 0) */
#define g_s3d_facing    DG8(0xB9F0)     /* u8   party facing 0..3 (0 north, 1 east ...) */
#define g_s3d_y         DGS16(0xB9F1)   /* i16  party cell y */
#define g_s3d_x         DGS16(0xB9F3)   /* i16  party cell x */
#define g_q3d_errbuf    DGSTR(0xBACC)   /* char[] fatal-message buffer (shared) */

/* far Q3dPiece * of each wall-set slot (16E0:B3F2 + 0Eh * n in the original;
   the 4 bytes in dg[] stay unused).  NULL until 3DSYS loads the slot; may
   point at g_q3d_pieces (no .KOR) or at a farmalloc'd copy. */
extern Q3dPiece *q3d_kor_pieces[Q3D_KOR_SLOTS];

/* far pointer at 16E0:B9EC: the map cells (district file FRPOS PCn, an
   AMOS bank: byte 14h = width, 15h = height, cells from byte 2Bh, where
   s3d_load_district locks the EMS block for the cells).  Cell
   (x, y) is at (y * map_w + x) * 15; bytes
   [2*side] = wall type (1-based KOR slot), [2*side+1] = wall kind.  The
   original adds offsets with F_PADD (huge pointer, sign-extended int16). */
extern uint8_t *s3d_map;

/* ---- callees in modules that are not ported yet (3DSYS, INTRFACE) -------- */
#if __has_include("engine/intrface.h")
#include "engine/intrface.h"
#else
void    ui_restore_background(void);                                 /* 05DC:1EB9 */
void    ui_load_voice(int16_t n);                                    /* 05DC:192D */
#endif
int16_t s3d_draw_portrait_hl(void);                                  /* 303F:75EF */
void    s3d_redraw_view(int16_t force);                              /* 303F:07B6 */

/* ---- 2700:0000-37EE (q3dview.c) -------------------------------------------- */
void    q3d_free_view_buffers(void);                                 /* 2700:0000 */
void    q3d_blit_view(uint8_t slot, int16_t dx, int16_t dy);         /* 2700:00A8 */
void    q3d_alloc_view_buffers(void);                                /* 2700:0235 */
void    q3d_project_a(int16_t i, float x, float y, float w, float h,
                      Q3dPiece *tab);                                /* 2700:02B0 */
void    q3d_precalc_table_a(int16_t view_w, int16_t view_h, Q3dPiece *tab); /* 2700:0328 */
void    q3d_project_b(Q3dPiece *tab, int16_t i, float tx, float ty, float tw, float th,
                      uint16_t flags);                               /* 2700:0D1D */
void    q3d_precalc_table_b0(void);                                  /* 2700:0DA8 */
void    q3d_precalc_table_b1(void);                                  /* 2700:15D7 */
int16_t q3d_load_kor(const char *name, int16_t slot);                /* 2700:1E06 */
void    q3d_precalc_table_c0(void);                                  /* 2700:2413 */
void    q3d_precalc_table_c1(void);                                  /* 2700:2C42 */
void    q3d_draw_strip_a(int16_t i, uint8_t src_slot, uint8_t dst_slot,
                         const Q3dPiece *tab);                       /* 2700:3471 */
void    q3d_draw_strip_b(int16_t i, uint8_t src_slot, uint8_t dst_slot,
                         const Q3dPiece *tab);                       /* 2700:35FF */
void    q3d_reset_kor(void);                                         /* 2700:37EE */

/* ---- 2700:3932-5F31 render (q3dview_render.c) ------------------------------ */
#define g_q3d_ceiling_here DG8(0x3AF8)  /* u8   the party's cell has a ceiling piece */
#define g_q3d_wall_bits DG16(0x3AF9)    /* u16  walls next to the party (kind byte != 0):
                                           1/2 distance 2 left/right, 4/8 distance 1,
                                           10h/20h/40h own cell left/right/front */
#define g_q3d_msg       DGSTR(0xB394)   /* char[] debug text (sprintf by draw_cell_wall) */
#define g_q3d_wall_set(n) DGS16(0x9F10 + 2 * (n)) /* i16 wall-set type of KOR slot n
                                           (map header byte n+3, set by 3DSYS); n = -1
                                           reads 9F0E, never written (0) */
#define Q3D_SURF_HORIZON 0x4F           /* surface: horizon picture of the far layer */
void    q3d_render_view(uint8_t slot);                              /* 2700:3932 */
void    q3d_draw_cell_wall(int16_t piece, int16_t cell_off, uint8_t side,
                           uint8_t dst_slot);                        /* 2700:3969 */
void    q3d_render_layer_far(uint8_t slot);                          /* 2700:3CDD */
void    q3d_render_layer_mid(uint8_t slot);                          /* 2700:43C9 */
void    q3d_render_layer_near(uint8_t slot);                         /* 2700:4F55 (to 56DF) */
void    q3d_render_layer_side(uint8_t slot);                         /* 2700:56E0 */
void    q3d_render_layer_front(uint8_t slot);                        /* 2700:58E2 */
void    q3d_debug_dump(void);                                        /* 2700:5E7F */
/* ---- end render ---- */

/* ---- 2700:5F32-647A automap, piece adjustments (q3dview_map.c) ------------- */
void    q3d_draw_automap(void);                                      /* 2700:5F32 */
void    q3d_init_a(Q3dPiece *tab);                                   /* 2700:624A */
void    q3d_init_tables(Q3dPiece *tab);                              /* 2700:62EC */
/* ---- end map ---- */

/* ---- 2700:647B-93D5 animations, shading (q3dview_anim.c) ------------------- */
#define g_q3d_shake_set DGS16(0x3B03)   /* i16  view shake chosen for this cell (3DSYS
                                           clears it on a move) */
#define g_q3d_anim_mode DG8(0x3B06)     /* u8   step forward: != 0 = 43h/44h already hold
                                           the new view's layers; turns: 1 / 2 = where
                                           the view sits in the interface picture 51h */
#define g_q3d_anim_keep DG8(0x3B07)     /* u8   step forward: keep the centre of 42h
                                           (cleared after the animation) */
#define g_q3d_anim_turn DG8(0x3B08)     /* u8   turn_a animation enabled */
#define g_q3d_anim_speed DGS16(0x6D74)  /* i16  animation steps; 0 = no animations */
#define g_q3d_rot_plane DGS16(0x6D76)   /* i16  rotations: projection plane z (+ bulge) */
#define g_q3d_rot_focal DGS16(0x6D78)   /* i16  rotations: eye distance */
#define g_q3d_sound     DG8(0xBB38)     /* u8   sound effects on (door / turn voices) */
#define g_s3d_prev_y    DGS16(0xBB4F)   /* i16  party cell before the last move (3DSYS) */
#define g_s3d_prev_x    DGS16(0xBB51)   /* i16 */
void    q3d_anim_step_forward(void);                                 /* 2700:647B */
void    q3d_anim_step_back(void);                                    /* 2700:69E2 */
void    q3d_anim_turn_a(void);                                       /* 2700:6F18 */
void    q3d_anim_turn_b(void);                                       /* 2700:73D8 */
void    q3d_anim_door(int16_t n);                                    /* 2700:798C */
void    q3d_anim_dispatch(uint8_t mode);                             /* 2700:80D0 */
int16_t q3d_anim_rotate_left(void);                                  /* 2700:8197 */
int16_t q3d_anim_rotate_right(void);                                 /* 2700:8896 */
void    q3d_wait_retrace(uint8_t mask);   /* (misnamed) allocate 41h+n  2700:8FD4 */
void    q3d_free_anim_buffer(uint8_t mask);                          /* 2700:9024 */
void    q3d_shade_view(float a, uint8_t src, uint8_t dst); /* (misnamed) sky 2700:9078 */
/* ---- end anim ---- */

#endif
