/*
 * q3dview.c - 3DQUICK.L part 1 (IST.EXE ovl2700:0000-37EE): view buffers,
 * the start-up piece tables, .KOR coordinate files, piece blits.  See
 * q3dview.h.
 *
 * Floating point.  The table code is x87 arithmetic on floats (FSTP dword
 * between the steps, 64-bit intermediates inside a step) truncated by
 * Borland's F_FTOL after adding 0.5.  It is evaluated with the 80-bit model
 * of x87ext.h, so the tables are bit-exact independently of the host FPU.
 */
#include "game/q3dview.h"

#include <stdio.h>
#include <string.h>

#include "engine/blit386.h"
#include "engine/fatal.h"
#include "engine/fileman.h"
#include "engine/memman.h"
#include "engine/ngraphs.h"
#include "engine/nkontrol.h"
#include "engine/systems.h"
#include "engine/vfs.h"
#include "game/x87ext.h"
#include "rtl/bc_farheap.h"

Q3dPiece *q3d_kor_pieces[Q3D_KOR_SLOTS];
uint8_t  *s3d_map;

#define Q3D_FILE    DGSTR(0x3B0D)      /* "T:\CODE\3DQUICK.L" (three copies) */
#define Q3D_FILE2   DGSTR(0x3B1F)
#define Q3D_FILE3   DGSTR(0x3CBD)
#define K_HALF      0x3B31             /* float 0.5 */

/*
 * q3d_free_view_buffers - free the wall-texture surfaces 44h..16h when
 * option bit 20h says they are loaded (15h, the compass, stays).  Surfaces
 * 24h..3Fh whose texture name (2944 + 13n) is empty only share another
 * slot's record and are just cleared.
 *
 * Original: IST.EXE 2700:0000 (3DQUICK.L), far cdecl.
 * Verified: tests/py/test_q3dview.py (test_small).
 */
void q3d_free_view_buffers(void)
{
    int16_t n;

    if (!(g_q3d_opt_flags & 0x20))
        return;
    for (n = 0x44; n > 0x14; n--) {
        if (n >= 0x24 && n < 0x40 && g_q3d_tex_names[n * 13] == 0) {
            g_surf_mm_seg(n) = 0;
            g_surf_mm_off(n) = 0;
        } else if ((g_surf_mm_off(n) | g_surf_mm_seg(n)) != 0 && n != 0x15) {
            ctl_free_checked((uint16_t)(0xE29F + 8 * n), 0x7B, Q3D_FILE);
        }
    }
    g_q3d_opt_flags &= (uint16_t)~0x20;
}

/* min(-v, 0) / max(-v, 0) as the original computes them (NEG; OR; Jcc) */
static int16_t neg_min0(int16_t v) { return (int16_t)-v <= 0 ? (int16_t)-v : 0; }
static int16_t neg_max0(int16_t v) { return (int16_t)-v >= 0 ? (int16_t)-v : 0; }

/*
 * q3d_blit_view - copy the rendered view from surface `slot` to the screen
 * (slot 0) and draw the compass (surface 15h, 47x40 cells, frame by facing).
 *
 *  - 200x200 window, 200-wide view: copy (view_w-14) x (view_h-8) from
 *    (7, 4) + shake to (5, 4);
 *  - 100-wide window, 200-wide view: scale the view down into the window
 *    at (dx, dy), skipping the shake offset;
 *  - otherwise: copy win_w-4 x win_h-2 to (dx, dy).
 * Then the portrait highlight of 3DSYS sets the cursor shape.
 *
 * Original: IST.EXE 2700:00A8 (3DQUICK.L), far cdecl (u8 slot, dx, dy).
 * Verified: tests/py/test_q3dview.py (test_blit_view).
 */
void q3d_blit_view(uint8_t slot, int16_t dx, int16_t dy)
{
    ui_restore_background();
    if (gr_cursor_in_zone(0x0E))
        gr_cursor_hide();
    if (g_q3d_win_w == 200 && g_q3d_view_w == 200) {
        sys_blit((uint16_t)(neg_min0(g_q3d_shift_x) + 7), (uint16_t)(neg_min0(g_q3d_shift_y) + 4),
                 (uint16_t)(g_q3d_view_w - 14), (uint16_t)(g_q3d_view_h - 8), 5, 4, slot, 0,
                 sys_rect_copy);
    } else if (g_q3d_win_w == 100 && g_q3d_view_w == 200) {
        sys_blit_scaled((uint16_t)neg_max0(g_q3d_shift_x), (uint16_t)neg_max0(g_q3d_shift_y),
                        (uint16_t)(g_q3d_view_w - 8 - neg_max0(g_q3d_shift_x)),
                        (uint16_t)(g_q3d_view_h - 4 - neg_max0(g_q3d_shift_y)),
                        (uint16_t)dx, (uint16_t)dy, (uint16_t)(g_q3d_win_w - 4),
                        (uint16_t)(g_q3d_win_h - 2), slot, 0, 0);
    } else {
        sys_blit(0, 0, (uint16_t)(g_q3d_win_w - 4), (uint16_t)(g_q3d_win_h - 2), (uint16_t)dx,
                 (uint16_t)dy, slot, 0, sys_rect_copy);
    }
    sys_blit((uint16_t)((g_s3d_facing % 2) * 0x2F), (uint16_t)(((g_s3d_facing % 4) / 2) * 0x28),
             0x2F, 0x28, 0x90, 0x9C, 0x15, 0, asm_copy_rect_key);
    gr_call_then_cursor(s3d_draw_portrait_hl);
}

/*
 * q3d_alloc_view_buffers - (misnamed) wrap the facing to 0..3 and set the
 * map step of "forward": north (0,-1), east (1,0), south (0,1), west (-1,0).
 *
 * Original: IST.EXE 2700:0235 (3DQUICK.L), far cdecl.  The default case
 * (fatal, line F5h) cannot happen after the % 4.
 * Verified: tests/py/test_q3dview.py (test_small).
 */
void q3d_alloc_view_buffers(void)
{
    g_s3d_facing = (uint8_t)(g_s3d_facing % 4);
    switch (g_s3d_facing) {
    case 0: g_s3d_step_dx = 0;  g_s3d_step_dy = -1; break;
    case 1: g_s3d_step_dy = 0;  g_s3d_step_dx = 1;  break;
    case 2: g_s3d_step_dx = 0;  g_s3d_step_dy = 1;  break;
    case 3: g_s3d_step_dy = 0;  g_s3d_step_dx = -1; break;
    default:
        o559A_fatal(Q3D_FILE2, 0xF5, NULL, 0x4F);
    }
}

/* FLD dword v; FADD dword [3B31] (0.5); CALL F_FTOL: round half up, low
   word of the 32-bit result. */
static int16_t round_f(float v)
{
    return (int16_t)ext_ftol(ext_add(ext_from_float(v), ext_from_float(DGF32(K_HALF))));
}

/*
 * q3d_project_a - store the destination rectangle of piece i:
 * tab[i].x..h = (int)(v + 0.5).
 *
 * Original: IST.EXE 2700:02B0 (3DQUICK.L), far cdecl (i, 4 floats, far tab).
 * Verified: tests/py/test_q3dview.py (test_tables).
 */
void q3d_project_a(int16_t i, float x, float y, float w, float h, Q3dPiece *tab)
{
    tab[i].x = round_f(x);
    tab[i].y = round_f(y);
    tab[i].w = round_f(w);
    tab[i].h = round_f(h);
}

/* ---- table A terms ---------------------------------------------------------
   Every argument of q3d_project_a is a float built from the per-pixel
   scales sx = (float)(view_w / 14.0f), sy = (float)(view_h / 14.0f) and the
   constants at 3B35..3BB1 (DGROUP floats K / doubles D):
     T_ZERO        FLDZ
     T_S(s)        FLD s
     T_KS(K, s)    FLD K; FMUL s
     T_SD(s, D)    FLD s; FMUL D          (24 x 53 bits: rounded twice)
     T_KSM(K,s,K2) FLD K; FMUL s; FSUB K2
   each stored with FSTP dword.  The original code is straight-line (one
   block per piece, 2700:0328-0D1C); the port keeps the terms in a table. */
enum { TA_ZERO, TA_S, TA_KS, TA_SD, TA_KSM };
typedef struct TermA {
    uint8_t  op;
    uint8_t  axis;          /* 0 sx, 1 sy */
    uint16_t k, k2;         /* DGROUP offsets of the constants */
} TermA;
#define X 0
#define Y 1
#define T_ZERO              { TA_ZERO, 0, 0, 0 }
#define T_S(s)              { TA_S, s, 0, 0 }
#define T_KS(k, s)          { TA_KS, s, k, 0 }
#define T_SD(s, d)          { TA_SD, s, d, 0 }
#define T_KSM(k, s, k2)     { TA_KSM, s, k, k2 }

typedef struct RowA {
    int16_t i;
    TermA   t[4];           /* x, y, w, h */
} RowA;

/* pieces 7..36 in the original's order (0..6 are the loop below) */
static const RowA k_rows_a[] = {
    {  7, { T_ZERO, T_SD(Y, 0x3B49), T_KS(0x3B39, X), T_SD(Y, 0x3B41) } },   /* call at 2700:040A */
    { 12, { T_KS(0x3B51, X), T_SD(Y, 0x3B49), T_KS(0x3B39, X), T_SD(Y, 0x3B41) } },   /* call at 2700:045B */
    {  8, { T_KS(0x3B39, X), T_SD(Y, 0x3B5D), T_KS(0x3B39, X), T_SD(Y, 0x3B55) } },   /* call at 2700:04AC */
    { 11, { T_KS(0x3B65, X), T_SD(Y, 0x3B5D), T_KS(0x3B39, X), T_SD(Y, 0x3B55) } },   /* call at 2700:04FD */
    {  9, { T_SD(X, 0x3B5D), T_SD(Y, 0x3B5D), T_SD(X, 0x3B69), T_SD(Y, 0x3B55) } },   /* call at 2700:054E */
    { 10, { T_KS(0x3B71, X), T_SD(Y, 0x3B5D), T_SD(X, 0x3B69), T_SD(Y, 0x3B55) } },   /* call at 2700:059F */
    { 13, { T_ZERO, T_SD(Y, 0x3B5D), T_KS(0x3B39, X), T_SD(Y, 0x3B55) } },   /* call at 2700:05EC */
    { 14, { T_KS(0x3B39, X), T_SD(Y, 0x3B5D), T_SD(X, 0x3B55), T_SD(Y, 0x3B55) } },   /* call at 2700:063D */
    { 15, { T_SD(X, 0x3B5D), T_SD(Y, 0x3B5D), T_SD(X, 0x3B55), T_SD(Y, 0x3B55) } },   /* call at 2700:068E */
    { 16, { T_SD(X, 0x3B75), T_SD(Y, 0x3B5D), T_SD(X, 0x3B55), T_SD(Y, 0x3B55) } },   /* call at 2700:06DF */
    { 17, { T_KS(0x3B51, X), T_SD(Y, 0x3B5D), T_KS(0x3B39, X), T_SD(Y, 0x3B55) } },   /* call at 2700:0730 */
    { 18, { T_ZERO, T_SD(Y, 0x3B85), T_KS(0x3B39, X), T_SD(Y, 0x3B7D) } },   /* call at 2700:077D */
    { 21, { T_KS(0x3B51, X), T_SD(Y, 0x3B85), T_KS(0x3B39, X), T_SD(Y, 0x3B7D) } },   /* call at 2700:07CE */
    { 19, { T_KS(0x3B39, X), T_KS(0x3B39, Y), T_SD(X, 0x3B95), T_SD(Y, 0x3B8D) } },   /* call at 2700:081F */
    { 20, { T_SD(X, 0x3B75), T_KS(0x3B39, Y), T_SD(X, 0x3B95), T_SD(Y, 0x3B8D) } },   /* call at 2700:0870 */
    { 22, { T_ZERO, T_KS(0x3B39, Y), T_KS(0x3B39, X), T_SD(Y, 0x3B8D) } },   /* call at 2700:08BD */
    { 23, { T_KS(0x3B39, X), T_KS(0x3B39, Y), T_SD(X, 0x3B8D), T_SD(Y, 0x3B8D) } },   /* call at 2700:090E */
    { 24, { T_KS(0x3B51, X), T_KS(0x3B39, Y), T_KS(0x3B39, X), T_SD(Y, 0x3B8D) } },   /* call at 2700:095F */
    { 25, { T_ZERO, T_ZERO, T_KS(0x3B39, X), T_KS(0x3B35, Y) } },   /* call at 2700:09A8 */
    { 26, { T_KS(0x3B51, X), T_ZERO, T_KS(0x3B39, X), T_KS(0x3B35, Y) } },   /* call at 2700:09F5 */
    { 27, { T_KS(0x3B3D, X), T_KS(0x3B71, Y), T_SD(X, 0x3BA5), T_SD(Y, 0x3B9D) } },   /* call at 2700:0A46 */
    { 28, { T_SD(X, 0x3B5D), T_SD(Y, 0x3B75), T_KS(0x3BAD, X), T_SD(Y, 0x3B95) } },   /* call at 2700:0A97 */
    { 29, { T_KS(0x3B39, X), T_KS(0x3B51, Y), T_KS(0x3B65, X), T_KS(0x3B39, Y) } },   /* call at 2700:0AE8 */
    { 30, { T_KS(0x3B3D, X), T_KS(0x3B3D, Y), T_SD(X, 0x3BA5), T_SD(Y, 0x3B9D) } },   /* call at 2700:0B39 */
    { 31, { T_SD(X, 0x3B5D), T_SD(Y, 0x3B5D), T_KS(0x3BAD, X), T_SD(Y, 0x3B95) } },   /* call at 2700:0B8A */
    { 32, { T_KS(0x3B39, X), T_KS(0x3B39, Y), T_KS(0x3B65, X), T_KS(0x3B39, Y) } },   /* call at 2700:0BDB */
    { 33, { T_KS(0x3B39, X), T_KS(0x3B3D, Y), T_S(X), T_KS(0x3B39, Y) } },   /* call at 2700:0C27 */
    { 34, { T_KS(0x3BAD, X), T_KS(0x3B3D, Y), T_S(X), T_KS(0x3B39, Y) } },   /* call at 2700:0C73 */
    { 35, { T_KSM(0x3B65, X, 0x3B65), T_KS(0x3B3D, Y), T_S(X), T_KS(0x3B39, Y) } },   /* call at 2700:0CC4 */
    { 36, { T_KSM(0x3B51, X, 0x3BB1), T_KS(0x3B3D, Y), T_S(X), T_KS(0x3B39, Y) } },   /* call at 2700:0D15 */
};
#undef X
#undef Y

static float term_a(const TermA *t, float sx, float sy)
{
    Ext s = ext_from_float(t->axis ? sy : sx);
    switch (t->op) {
    case TA_S:   return ext_to_float(s);
    case TA_KS:  return ext_to_float(ext_mul(ext_from_float(DGF32(t->k)), s));
    case TA_SD:  return ext_to_float(ext_mul(s, ext_from_double(DGF64(t->k))));
    case TA_KSM: return ext_to_float(ext_sub(ext_mul(ext_from_float(DGF32(t->k)), s),
                                             ext_from_float(DGF32(t->k2))));
    default:     return 0.0f;                                   /* FLDZ */
    }
}

/*
 * q3d_precalc_table_a - destination rectangles of the 37 pieces for a view
 * of view_w x view_h pixels, designed on a 14 x 14 grid: sx = view_w / 14,
 * sy = view_h / 14 (floats), e.g. pieces 0..6 (the far row) =
 * (2i*sx, 6*sy, 2*sx, 2*sy).
 *
 * Original: IST.EXE 2700:0328 (3DQUICK.L), far cdecl (view_w, view_h,
 * far tab); main passes (g_q3d_view_w, g_q3d_view_h, g_q3d_pieces).
 * Verified: tests/py/test_q3dview.py (test_tables: whole table vs the
 * original in Unicorn's x87 for every view size 1..400 and random ones).
 */
void q3d_precalc_table_a(int16_t view_w, int16_t view_h, Q3dPiece *tab)
{
    float sx = ext_to_float(ext_div(ext_from_i32(view_w), ext_from_float(DGF32(0x3B35))));
    float sy = ext_to_float(ext_div(ext_from_i32(view_h), ext_from_float(DGF32(0x3B35))));
    int16_t i;
    size_t r;

    for (i = 0; i < 7; i++) {               /* 2700:035F: FILD (i << 1); FMUL sx */
        Ext fx = ext_from_float(sx), fy = ext_from_float(sy);
        q3d_project_a(i,
                      ext_to_float(ext_mul(ext_from_i32((int16_t)(i << 1)), fx)),
                      ext_to_float(ext_mul(ext_from_float(DGF32(0x3B3D)), fy)),
                      ext_to_float(ext_mul(ext_from_float(DGF32(0x3B39)), fx)),
                      ext_to_float(ext_mul(ext_from_float(DGF32(0x3B39)), fy)), tab);
    }
    for (r = 0; r < sizeof k_rows_a / sizeof k_rows_a[0]; r++) {
        const RowA *a = &k_rows_a[r];
        q3d_project_a(a->i, term_a(&a->t[0], sx, sy), term_a(&a->t[1], sx, sy),
                      term_a(&a->t[2], sx, sy), term_a(&a->t[3], sx, sy), tab);
    }
}

/*
 * q3d_project_b - store the texture rectangle and flags of piece i:
 * tab[i].tx..th = (int)(v + 0.5), tab[i].flags = flags.
 *
 * Original: IST.EXE 2700:0D1D (3DQUICK.L), far cdecl (far tab, i, 4 floats,
 * flags).
 * Verified: tests/py/test_q3dview.py (test_tables).
 */
void q3d_project_b(Q3dPiece *tab, int16_t i, float tx, float ty, float tw, float th,
                   uint16_t flags)
{
    tab[i].tx = round_f(tx);
    tab[i].ty = round_f(ty);
    tab[i].tw = round_f(tw);
    tab[i].th = round_f(th);
    tab[i].flags = flags;
}

/* ---- tables B/C --------------------------------------------------------------
   Texture rectangles for one texture size (base x base pixels): float
   constants from DGROUP (FZ = FLDZ, F1 = FLD1).  Flags: the literal bits,
   | VF = the view-size bit 2 (scaled) unless the view is base x base. */
enum { FZ = 0, F1 = 1 };
#define VF 0x8000                   /* marker: OR in the view-size flag */
typedef struct RowB {
    int16_t  first, last;           /* pieces first..last get the same values */
    uint16_t flags;
    uint16_t k[4];                  /* tx, ty, tw, th */
} RowB;

static const RowB k_rows_b0[] = {   /* 2700:0DA8 (base 100) */
    {  0,  6, 2     , { 0x3BB9, FZ    , 0x3BB5, 0x3BB5 } },   /* call at 2700:0E19 */
    {  7,  7, 0 | VF, { 0x3BC9, 0x3BC5, 0x3BC1, 0x3BBD } },   /* call at 2700:0E66 */
    { 12, 12, 1 | VF, { 0x3BC9, 0x3BC5, 0x3BC1, 0x3BBD } },   /* call at 2700:0EAE */
    {  8,  8, 0 | VF, { 0x3BD5, 0x3BD1, 0x3B35, 0x3BCD } },   /* call at 2700:0EF2 */
    { 11, 11, 1 | VF, { 0x3BD5, 0x3BD1, 0x3B35, 0x3BCD } },   /* call at 2700:0F3A */
    {  9,  9, 0 | VF, { 0x3BD9, 0x3BD9, 0x3B3D, 0x3BCD } },   /* call at 2700:0F7E */
    { 10, 10, 1 | VF, { 0x3BD9, 0x3BD9, 0x3B3D, 0x3BCD } },   /* call at 2700:0FC6 */
    { 13, 13, 2 | VF, { 0x3BC5, FZ    , 0x3BDD, 0x3BB5 } },   /* call at 2700:100E */
    { 17, 17, 2 | VF, { 0x3BB9, FZ    , 0x3BDD, 0x3BB5 } },   /* call at 2700:1056 */
    { 14, 16, 2 | VF, { 0x3BB9, FZ    , 0x3BB5, 0x3BB5 } },   /* call at 2700:10A6 */
    { 18, 18, 0 | VF, { 0x3BB9, 0x3BC5, 0x3B35, 0x3BE1 } },   /* call at 2700:10F3 */
    { 21, 21, 1 | VF, { 0x3BB9, 0x3BC5, 0x3B35, 0x3BE1 } },   /* call at 2700:113B */
    { 19, 19, 0 | VF, { 0x3B35, 0x3BE5, 0x3BCD, 0x3BB5 } },   /* call at 2700:117F */
    { 20, 20, 1 | VF, { 0x3B35, 0x3BE5, 0x3BCD, 0x3BB5 } },   /* call at 2700:11C7 */
    { 22, 22, 0 | VF, { 0x3BE9, FZ    , 0x3B35, 0x3BB5 } },   /* call at 2700:120B */
    { 23, 23, 0 | VF, { 0x3BB9, FZ    , 0x3BB5, 0x3BB5 } },   /* call at 2700:124F */
    { 24, 24, 0 | VF, { 0x3BB9, FZ    , 0x3B35, 0x3BB5 } },   /* call at 2700:1293 */
    { 25, 25, 0 | VF, { FZ    , FZ    , 0x3B35, 0x3BED } },   /* call at 2700:12D7 */
    { 26, 26, 1 | VF, { FZ    , FZ    , 0x3B35, 0x3BED } },   /* call at 2700:131F */
    { 27, 27, 0 | VF, { 0x3BDD, FZ    , 0x3B35, 0x3BF1 } },   /* call at 2700:1363 */
    { 28, 28, 0 | VF, { 0x3BD9, 0x3BF1, 0x3BCD, 0x3BCD } },   /* call at 2700:13A7 */
    { 29, 29, 0 | VF, { 0x3B35, 0x3BF5, 0x3BB5, 0x3B35 } },   /* call at 2700:13EB */
    { 30, 30, 0 | VF, { 0x3BDD, 0x3BDD, 0x3B35, 0x3BF1 } },   /* call at 2700:142F */
    { 31, 31, 0 | VF, { 0x3BD9, 0x3BD9, 0x3BCD, 0x3BCD } },   /* call at 2700:1473 */
    { 32, 32, 0 | VF, { 0x3B35, 0x3B35, 0x3BB5, 0x3B35 } },   /* call at 2700:14B7 */
    { 33, 33, 0 | VF, { 0x3BE9, 0x3BF9, 0x3BB1, 0x3BB1 } },   /* call at 2700:14FB */
    { 36, 36, 1 | VF, { 0x3BE9, 0x3BF9, 0x3BB1, 0x3BB1 } },   /* call at 2700:1543 */
    { 34, 34, 0 | VF, { 0x3C01, 0x3BFD, 0x3B65, 0x3BB1 } },   /* call at 2700:1587 */
    { 35, 35, 1 | VF, { 0x3C01, 0x3BFD, 0x3B65, 0x3BB1 } },   /* call at 2700:15CF */
};

static const RowB k_rows_b1[] = {   /* 2700:15D7 (base 200) */
    {  0,  6, 2     , { 0x3C09, FZ    , 0x3C05, 0x3C05 } },   /* call at 2700:1648 */
    {  7,  7, 0 | VF, { 0x3C15, 0x3C11, 0x3C0D, 0x3BD9 } },   /* call at 2700:1695 */
    { 12, 12, 1 | VF, { 0x3C15, 0x3C11, 0x3C0D, 0x3BD9 } },   /* call at 2700:16DD */
    {  8,  8, 0 | VF, { 0x3C25, 0x3C21, 0x3C1D, 0x3C19 } },   /* call at 2700:1721 */
    { 11, 11, 1 | VF, { 0x3C25, 0x3C21, 0x3C1D, 0x3C19 } },   /* call at 2700:1769 */
    {  9,  9, 0 | VF, { 0x3C2D, 0x3C2D, 0x3B51, 0x3C29 } },   /* call at 2700:17AD */
    { 10, 10, 1 | VF, { 0x3C2D, 0x3C2D, 0x3B51, 0x3C29 } },   /* call at 2700:17F5 */
    { 13, 13, 2 | VF, { 0x3C11, FZ    , 0x3C31, 0x3C05 } },   /* call at 2700:183D */
    { 17, 17, 2 | VF, { 0x3C09, FZ    , 0x3C31, 0x3C05 } },   /* call at 2700:1885 */
    { 14, 16, 2 | VF, { 0x3C09, FZ    , 0x3C05, 0x3C05 } },   /* call at 2700:18D5 */
    { 18, 18, 0 | VF, { 0x3C09, 0x3C05, 0x3C1D, 0x3C35 } },   /* call at 2700:1922 */
    { 21, 21, 1 | VF, { 0x3C09, 0x3C05, 0x3C1D, 0x3C35 } },   /* call at 2700:196A */
    { 19, 19, 0 | VF, { 0x3C1D, 0x3C39, 0x3C29, 0x3C05 } },   /* call at 2700:19AE */
    { 20, 20, 1 | VF, { 0x3C1D, 0x3C39, 0x3C29, 0x3C05 } },   /* call at 2700:19F6 */
    { 22, 22, 0 | VF, { 0x3C3D, FZ    , 0x3C1D, 0x3C05 } },   /* call at 2700:1A3A */
    { 23, 23, 0 | VF, { 0x3C09, FZ    , 0x3C05, 0x3C05 } },   /* call at 2700:1A7E */
    { 24, 24, 0 | VF, { 0x3C09, FZ    , 0x3C1D, 0x3C05 } },   /* call at 2700:1AC2 */
    { 25, 25, 0 | VF, { FZ    , FZ    , 0x3C1D, 0x3C41 } },   /* call at 2700:1B06 */
    { 26, 26, 1 | VF, { FZ    , FZ    , 0x3C1D, 0x3C41 } },   /* call at 2700:1B4E */
    { 27, 27, 0 | VF, { 0x3C31, F1    , 0x3C1D, 0x3B65 } },   /* call at 2700:1B92 */
    { 28, 28, 0 | VF, { 0x3C2D, 0x3C45, 0x3C29, 0x3C29 } },   /* call at 2700:1BD6 */
    { 29, 29, 0 | VF, { 0x3C1D, 0x3C49, 0x3C05, 0x3C1D } },   /* call at 2700:1C1A */
    { 30, 30, 0 | VF, { 0x3BC9, 0x3BC9, 0x3C4D, 0x3C45 } },   /* call at 2700:1C5E */
    { 31, 31, 0 | VF, { 0x3C2D, 0x3C2D, 0x3C29, 0x3C29 } },   /* call at 2700:1CA2 */
    { 32, 32, 0 | VF, { 0x3C1D, 0x3C1D, 0x3C05, 0x3C1D } },   /* call at 2700:1CE6 */
    { 33, 33, 0 | VF, { 0x3C55, 0x3C51, 0x3BF5, 0x3C39 } },   /* call at 2700:1D2A */
    { 36, 36, 1 | VF, { 0x3C55, 0x3C51, 0x3BF5, 0x3C39 } },   /* call at 2700:1D72 */
    { 34, 34, 0 | VF, { 0x3C65, 0x3C61, 0x3C5D, 0x3C59 } },   /* call at 2700:1DB6 */
    { 35, 35, 1 | VF, { 0x3C65, 0x3C61, 0x3C5D, 0x3C59 } },   /* call at 2700:1DFE */
};

static const RowB k_rows_c0[] = {   /* 2700:2413 (base 100; = b0) */
    {  0,  6, 2     , { 0x3BB9, FZ    , 0x3BB5, 0x3BB5 } },   /* call at 2700:2484 */
    {  7,  7, 0 | VF, { 0x3BC9, 0x3BC5, 0x3BC1, 0x3BBD } },   /* call at 2700:24D1 */
    { 12, 12, 1 | VF, { 0x3BC9, 0x3BC5, 0x3BC1, 0x3BBD } },   /* call at 2700:2519 */
    {  8,  8, 0 | VF, { 0x3BD5, 0x3BD1, 0x3B35, 0x3BCD } },   /* call at 2700:255D */
    { 11, 11, 1 | VF, { 0x3BD5, 0x3BD1, 0x3B35, 0x3BCD } },   /* call at 2700:25A5 */
    {  9,  9, 0 | VF, { 0x3BD9, 0x3BD9, 0x3B3D, 0x3BCD } },   /* call at 2700:25E9 */
    { 10, 10, 1 | VF, { 0x3BD9, 0x3BD9, 0x3B3D, 0x3BCD } },   /* call at 2700:2631 */
    { 13, 13, 2 | VF, { 0x3BC5, FZ    , 0x3BDD, 0x3BB5 } },   /* call at 2700:2679 */
    { 17, 17, 2 | VF, { 0x3BB9, FZ    , 0x3BDD, 0x3BB5 } },   /* call at 2700:26C1 */
    { 14, 16, 2 | VF, { 0x3BB9, FZ    , 0x3BB5, 0x3BB5 } },   /* call at 2700:2711 */
    { 18, 18, 0 | VF, { 0x3BB9, 0x3BC5, 0x3B35, 0x3BE1 } },   /* call at 2700:275E */
    { 21, 21, 1 | VF, { 0x3BB9, 0x3BC5, 0x3B35, 0x3BE1 } },   /* call at 2700:27A6 */
    { 19, 19, 0 | VF, { 0x3B35, 0x3BE5, 0x3BCD, 0x3BB5 } },   /* call at 2700:27EA */
    { 20, 20, 1 | VF, { 0x3B35, 0x3BE5, 0x3BCD, 0x3BB5 } },   /* call at 2700:2832 */
    { 22, 22, 0 | VF, { 0x3BE9, FZ    , 0x3B35, 0x3BB5 } },   /* call at 2700:2876 */
    { 23, 23, 0 | VF, { 0x3BB9, FZ    , 0x3BB5, 0x3BB5 } },   /* call at 2700:28BA */
    { 24, 24, 0 | VF, { 0x3BB9, FZ    , 0x3B35, 0x3BB5 } },   /* call at 2700:28FE */
    { 25, 25, 0 | VF, { FZ    , FZ    , 0x3B35, 0x3BED } },   /* call at 2700:2942 */
    { 26, 26, 1 | VF, { FZ    , FZ    , 0x3B35, 0x3BED } },   /* call at 2700:298A */
    { 27, 27, 0 | VF, { 0x3BDD, FZ    , 0x3B35, 0x3BF1 } },   /* call at 2700:29CE */
    { 28, 28, 0 | VF, { 0x3BD9, 0x3BF1, 0x3BCD, 0x3BCD } },   /* call at 2700:2A12 */
    { 29, 29, 0 | VF, { 0x3B35, 0x3BF5, 0x3BB5, 0x3B35 } },   /* call at 2700:2A56 */
    { 30, 30, 0 | VF, { 0x3BDD, 0x3BDD, 0x3B35, 0x3BF1 } },   /* call at 2700:2A9A */
    { 31, 31, 0 | VF, { 0x3BD9, 0x3BD9, 0x3BCD, 0x3BCD } },   /* call at 2700:2ADE */
    { 32, 32, 0 | VF, { 0x3B35, 0x3B35, 0x3BB5, 0x3B35 } },   /* call at 2700:2B22 */
    { 33, 33, 0 | VF, { 0x3BE9, 0x3BF9, 0x3BB1, 0x3BB1 } },   /* call at 2700:2B66 */
    { 36, 36, 1 | VF, { 0x3BE9, 0x3BF9, 0x3BB1, 0x3BB1 } },   /* call at 2700:2BAE */
    { 34, 34, 0 | VF, { 0x3C01, 0x3BFD, 0x3B65, 0x3BB1 } },   /* call at 2700:2BF2 */
    { 35, 35, 1 | VF, { 0x3C01, 0x3BFD, 0x3B65, 0x3BB1 } },   /* call at 2700:2C3A */
};

static const RowB k_rows_c1[] = {   /* 2700:2C42 (base 200; piece 27 differs from b1) */
    {  0,  6, 2     , { 0x3C09, FZ    , 0x3C05, 0x3C05 } },   /* call at 2700:2CB3 */
    {  7,  7, 0 | VF, { 0x3C15, 0x3C11, 0x3C0D, 0x3BD9 } },   /* call at 2700:2D00 */
    { 12, 12, 1 | VF, { 0x3C15, 0x3C11, 0x3C0D, 0x3BD9 } },   /* call at 2700:2D48 */
    {  8,  8, 0 | VF, { 0x3C25, 0x3C21, 0x3C1D, 0x3C19 } },   /* call at 2700:2D8C */
    { 11, 11, 1 | VF, { 0x3C25, 0x3C21, 0x3C1D, 0x3C19 } },   /* call at 2700:2DD4 */
    {  9,  9, 0 | VF, { 0x3C2D, 0x3C2D, 0x3B51, 0x3C29 } },   /* call at 2700:2E18 */
    { 10, 10, 1 | VF, { 0x3C2D, 0x3C2D, 0x3B51, 0x3C29 } },   /* call at 2700:2E60 */
    { 13, 13, 2 | VF, { 0x3C11, FZ    , 0x3C31, 0x3C05 } },   /* call at 2700:2EA8 */
    { 17, 17, 2 | VF, { 0x3C09, FZ    , 0x3C31, 0x3C05 } },   /* call at 2700:2EF0 */
    { 14, 16, 2 | VF, { 0x3C09, FZ    , 0x3C05, 0x3C05 } },   /* call at 2700:2F40 */
    { 18, 18, 0 | VF, { 0x3C09, 0x3C05, 0x3C1D, 0x3C35 } },   /* call at 2700:2F8D */
    { 21, 21, 1 | VF, { 0x3C09, 0x3C05, 0x3C1D, 0x3C35 } },   /* call at 2700:2FD5 */
    { 19, 19, 0 | VF, { 0x3C1D, 0x3C39, 0x3C29, 0x3C05 } },   /* call at 2700:3019 */
    { 20, 20, 1 | VF, { 0x3C1D, 0x3C39, 0x3C29, 0x3C05 } },   /* call at 2700:3061 */
    { 22, 22, 0 | VF, { 0x3C3D, FZ    , 0x3C1D, 0x3C05 } },   /* call at 2700:30A5 */
    { 23, 23, 0 | VF, { 0x3C09, FZ    , 0x3C05, 0x3C05 } },   /* call at 2700:30E9 */
    { 24, 24, 0 | VF, { 0x3C09, FZ    , 0x3C1D, 0x3C05 } },   /* call at 2700:312D */
    { 25, 25, 0 | VF, { FZ    , FZ    , 0x3C1D, 0x3C41 } },   /* call at 2700:3171 */
    { 26, 26, 1 | VF, { FZ    , FZ    , 0x3C1D, 0x3C41 } },   /* call at 2700:31B9 */
    { 27, 27, 0 | VF, { 0x3BC9, F1    , 0x3CCF, 0x3B65 } },   /* call at 2700:31FD */
    { 28, 28, 0 | VF, { 0x3C2D, 0x3C45, 0x3C29, 0x3C29 } },   /* call at 2700:3241 */
    { 29, 29, 0 | VF, { 0x3C1D, 0x3C49, 0x3C05, 0x3C1D } },   /* call at 2700:3285 */
    { 30, 30, 0 | VF, { 0x3BC9, 0x3BC9, 0x3C4D, 0x3C45 } },   /* call at 2700:32C9 */
    { 31, 31, 0 | VF, { 0x3C2D, 0x3C2D, 0x3C29, 0x3C29 } },   /* call at 2700:330D */
    { 32, 32, 0 | VF, { 0x3C1D, 0x3C1D, 0x3C05, 0x3C1D } },   /* call at 2700:3351 */
    { 33, 33, 0 | VF, { 0x3C55, 0x3C51, 0x3BF5, 0x3C39 } },   /* call at 2700:3395 */
    { 36, 36, 1 | VF, { 0x3C55, 0x3C51, 0x3BF5, 0x3C39 } },   /* call at 2700:33DD */
    { 34, 34, 0 | VF, { 0x3C65, 0x3C61, 0x3C5D, 0x3C59 } },   /* call at 2700:3421 */
    { 35, 35, 1 | VF, { 0x3C65, 0x3C61, 0x3C5D, 0x3C59 } },   /* call at 2700:3469 */
};

static float const_f(uint16_t k)
{
    return k == FZ ? 0.0f : k == F1 ? 1.0f : DGF32(k);
}

static void precalc_tex(Q3dPiece *tab, int16_t base, const RowB *rows, size_t n)
{
    uint16_t vf = 0;
    size_t r;
    int16_t i;

    if (base != g_q3d_view_w || base != g_q3d_view_h)
        vf = 2;
    for (r = 0; r < n; r++)
        for (i = rows[r].first; i <= rows[r].last; i++)
            q3d_project_b(tab, i, const_f(rows[r].k[0]), const_f(rows[r].k[1]),
                          const_f(rows[r].k[2]), const_f(rows[r].k[3]),
                          (uint16_t)((rows[r].flags & ~VF) | ((rows[r].flags & VF) ? vf : 0)));
}

/*
 * q3d_precalc_table_b0 - texture rectangles of 100x100 wall textures into
 * g_q3d_pieces.  (unreferenced)
 *
 * Original: IST.EXE 2700:0DA8 (3DQUICK.L), far cdecl.
 * Verified: tests/py/test_q3dview.py (test_tables).
 */
void q3d_precalc_table_b0(void)
{
    precalc_tex(g_q3d_pieces, 100, k_rows_b0, sizeof k_rows_b0 / sizeof k_rows_b0[0]);
}

/*
 * q3d_precalc_table_b1 - texture rectangles of the 200x200 wall textures
 * into g_q3d_pieces (called by main).
 *
 * Original: IST.EXE 2700:15D7 (3DQUICK.L), far cdecl.
 * Verified: tests/py/test_q3dview.py (test_tables).
 */
void q3d_precalc_table_b1(void)
{
    precalc_tex(g_q3d_pieces, 200, k_rows_b1, sizeof k_rows_b1 / sizeof k_rows_b1[0]);
}

/*
 * q3d_load_kor - load NAME.KOR for wall-set slot `slot` (0-7): a per-set
 * replacement of the texture rectangles.  File: i16 version (1), u16 n,
 * n header bytes (3DSYS presets {0, 200, 200}: the view size the file was
 * made for), then 37 records of 7 words {tx, ty, tw, th, dx, dy, flags}.
 * The slot gets a farmalloc'd copy of g_q3d_pieces in which each piece
 * takes the record's texture rectangle, its size, the offset (dx, dy)
 * added to its position and the record flags; the piece is clipped to the
 * view.  When the view size differs from the header's, sizes and offsets
 * are scaled by the piece's table ratio w/tw, h/th (x87, rounded with
 * +0.5 and F_FTOL) and the piece is flagged scaled.
 *
 * Returns 0 if the file does not exist (slot -> g_q3d_pieces), else 1.  A
 * file too short for the records also leaves the slot at g_q3d_pieces; a
 * wrong version is fatal (line 1F7h).
 *
 * Quirks kept: the unscaled branch clips the height against the view
 * WIDTH using x and w (2700:23B0), and adds instead of subtracting; the
 * scaled branch adjusts tw/th on the right/bottom clip by c * ratio (not
 * c / ratio).  The fatal message formats the file name after it was freed
 * (as the original, whose free-list links overwrite its first bytes).
 *
 * Original: IST.EXE 2700:1E06 (3DQUICK.L), far cdecl (far name, slot).
 * Verified: tests/py/test_q3dview.py (test_load_kor: every .KOR of the 3D
 * archive, both view sizes, plus synthetic records).
 */
int16_t q3d_load_kor(const char *name, int16_t slot)
{
    Q3dKorSlot *k = &g_q3d_kor[slot];
    uint8_t *kp = (uint8_t *)k;
    uint8_t recs[0x206];            /* [bp-21Ah]: 37 x 14-byte records */
    uint8_t flag = 0;               /* [bp-1] */
    char *path;
    int16_t h, i;

    if (q3d_kor_pieces[slot] != NULL && q3d_kor_pieces[slot] != g_q3d_pieces &&
        q3d_kor_pieces[slot] != g_q3d_pieces_c)
        bc_farfree(q3d_kor_pieces[slot]);
    q3d_kor_pieces[slot] = mm_farmalloc(Q3D_PIECES * sizeof(Q3dPiece));
    path = fm_change_ext(name, DGSTR(0x3C69));                /* ".kor" */
    h = vfs_open(path, 2);
    if (h == -1) {
        bc_farfree(path);
        bc_farfree(q3d_kor_pieces[slot]);
        q3d_kor_pieces[slot] = g_q3d_pieces;
        vfs_close(h);
        return 0;
    }
    bc_farfree(path);
    memcpy(q3d_kor_pieces[slot], g_q3d_pieces, Q3D_PIECES * sizeof(Q3dPiece));
    vfs_read(h, kp, 2);                                       /* version */
    if (k->version == 1) {
        vfs_read(h, kp + 2, 2);                               /* n */
        vfs_read(h, kp + 4, (int32_t)k->n);                   /* header (n bytes, unchecked) */
        if ((uint32_t)(fm_filelength(h) - 4) > (uint32_t)k->n) {
            vfs_read(h, recs, 0x206);
        } else {
            vfs_close(h);
            bc_farfree(q3d_kor_pieces[slot]);
            q3d_kor_pieces[slot] = g_q3d_pieces;
            return 1;
        }
    } else {
        /* "Ic hata!\nKordinat dosyasi versiyonu desteklenmiyor,\ndosya %s dogru mu?" */
        if (sprintf(g_q3d_errbuf, DGSTR(0x3C6E), path) > 0x68)
            strcpy(g_q3d_errbuf, DGSTR(0x3CB5));             /* " ML(?!)" */
        o559A_fatal(Q3D_FILE3, 0x1F7, g_q3d_errbuf, -1);
        memset(recs, 0, sizeof recs);   /* never reached in the game (fatal exits) */
    }
    if (k->w != g_q3d_view_w || k->h != g_q3d_view_h)
        flag |= 2;

    for (i = 0; i < Q3D_PIECES; i++) {
        const uint8_t *r = recs + i * 14;
        int16_t kr[7];                  /* [bp-228h]: tx, ty, tw, th, dx, dy, flags */
        Q3dPiece p = q3d_kor_pieces[slot][i];   /* [bp-23Ah] */
        int16_t c;
        int j;

        for (j = 0; j < 7; j++)
            kr[j] = (int16_t)(r[2 * j] | r[2 * j + 1] << 8);
        p.tx = kr[0];
        p.ty = kr[1];
        p.tw = kr[2];
        p.th = kr[3];
        p.flags = (uint16_t)(kr[6] | flag);
        if (p.flags & 2) {
            /* x: ratio = (float)(w / tw) of the default table */
            float ratio = ext_to_float(ext_div(ext_from_i32(g_q3d_pieces[i].w),
                                               ext_from_i32(g_q3d_pieces[i].tw)));
            Ext half = ext_from_float(DGF32(K_HALF)), er = ext_from_float(ratio);
            p.w = (int16_t)ext_ftol(ext_add(ext_mul(ext_from_i32((uint16_t)kr[2]), er), half));
            p.x = (int16_t)(p.x + ext_ftol(ext_add(ext_mul(ext_from_i32(kr[4]), er), half)));
            c = p.x;
            if (c < 0) {
                p.x = 0;
                p.w = (int16_t)(p.w + c);
                c = (int16_t)ext_ftol(ext_add(ext_div(ext_from_i32(c), er), half));
                p.tx = (int16_t)(p.tx - c);
                p.tw = (int16_t)(p.tw + c);
            }
            c = (int16_t)(g_q3d_view_w - (int16_t)(p.x + p.w));
            if (c < 0) {
                p.w = (int16_t)(p.w + c);
                p.tw = (int16_t)(p.tw + ext_ftol(ext_add(ext_mul(ext_from_i32(c), er), half)));
            }
            /* y */
            ratio = ext_to_float(ext_div(ext_from_i32(g_q3d_pieces[i].h),
                                         ext_from_i32(g_q3d_pieces[i].th)));
            er = ext_from_float(ratio);
            p.h = (int16_t)ext_ftol(ext_add(ext_mul(ext_from_i32((uint16_t)kr[3]), er), half));
            p.y = (int16_t)(p.y + ext_ftol(ext_add(ext_mul(ext_from_i32(kr[5]), er), half)));
            c = p.y;
            if (c < 0) {
                p.y = 0;
                p.h = (int16_t)(p.h + c);
                c = (int16_t)ext_ftol(ext_add(ext_div(ext_from_i32(c), er), half));
                p.ty = (int16_t)(p.ty - c);
                p.th = (int16_t)(p.th + c);
            }
            c = (int16_t)(g_q3d_view_h - (int16_t)(p.y + p.h));
            if (c < 0) {
                p.h = (int16_t)(p.h + c);
                p.th = (int16_t)(p.th + ext_ftol(ext_add(ext_mul(ext_from_i32(c), er), half)));
            }
        } else {
            p.w = kr[2];
            p.x = (int16_t)(p.x + kr[4]);
            c = p.x;
            if (c < 0) {
                p.x = 0;
                p.tx = (int16_t)(p.tx - c);
                p.w = (int16_t)(p.w + c);
            }
            c = (int16_t)(g_q3d_view_w - (int16_t)(p.x + p.w));
            if (c < 0)
                p.w = (int16_t)(p.w + c);
            p.h = kr[3];
            p.y = (int16_t)(p.y + kr[5]);
            c = p.y;
            if (c < 0) {
                p.y = 0;
                p.ty = (int16_t)(p.ty - c);
                p.h = (int16_t)(p.h + c);
            }
            c = (int16_t)(g_q3d_view_w - (int16_t)(p.x + p.w));    /* sic: x, w, view_w */
            if (c < 0)
                p.h = (int16_t)(p.h - c);
        }
        q3d_kor_pieces[slot][i] = p;
    }
    vfs_close(h);
    return 1;
}

/*
 * q3d_precalc_table_c0 - texture rectangles of 100x100 wall textures into
 * g_q3d_pieces_c.  (unreferenced)
 *
 * Original: IST.EXE 2700:2413 (3DQUICK.L), far cdecl.
 * Verified: tests/py/test_q3dview.py (test_tables).
 */
void q3d_precalc_table_c0(void)
{
    precalc_tex(g_q3d_pieces_c, 100, k_rows_c0, sizeof k_rows_c0 / sizeof k_rows_c0[0]);
}

/*
 * q3d_precalc_table_c1 - texture rectangles of the 200x200 textures into
 * g_q3d_pieces_c (called by main).
 *
 * Original: IST.EXE 2700:2C42 (3DQUICK.L), far cdecl.
 * Verified: tests/py/test_q3dview.py (test_tables).
 */
void q3d_precalc_table_c1(void)
{
    precalc_tex(g_q3d_pieces_c, 200, k_rows_c1, sizeof k_rows_c1 / sizeof k_rows_c1[0]);
}

/* the blits of q3d_draw_strip_a/b: flags 0 plain, 1 mirrored, 2 scaled,
   3 scaled + mirrored; other values draw nothing */
static void draw_piece(const Q3dPiece *p, int16_t tx, uint8_t src_slot, uint8_t dst_slot)
{
    uint16_t dx = (uint16_t)(p->x + g_q3d_view_ox), dy = (uint16_t)(p->y + g_q3d_view_oy);

    if (p->flags == 0)
        sys_blit((uint16_t)tx, (uint16_t)p->ty, (uint16_t)p->w, (uint16_t)p->h, dx, dy,
                 src_slot, dst_slot, asm_copy_rect_key);
    else if ((p->flags & 2) && (p->flags & 1))
        sys_blit_scaled((uint16_t)tx, (uint16_t)p->ty, (uint16_t)p->tw, (uint16_t)p->th, dx, dy,
                        (uint16_t)p->w, (uint16_t)p->h, src_slot, dst_slot, 1);
    else if (p->flags & 1)
        sys_blit((uint16_t)tx, (uint16_t)p->ty, (uint16_t)p->w, (uint16_t)p->h, dx, dy,
                 src_slot, dst_slot, asm_copy_rect_key_mirror);
    else if (p->flags & 2)
        sys_blit_scaled((uint16_t)tx, (uint16_t)p->ty, (uint16_t)p->tw, (uint16_t)p->th, dx, dy,
                        (uint16_t)p->w, (uint16_t)p->h, src_slot, dst_slot, 0);
}

/*
 * q3d_draw_strip_a - draw piece i of `tab` from texture surface src_slot
 * into dst_slot (nothing when src_slot is 0).
 *
 * Original: IST.EXE 2700:3471 (3DQUICK.L), far cdecl (i, u8 src, u8 dst,
 * far tab).
 * Verified: tests/py/test_q3dview.py (test_strips).
 */
void q3d_draw_strip_a(int16_t i, uint8_t src_slot, uint8_t dst_slot, const Q3dPiece *tab)
{
    if (src_slot == 0)
        return;
    draw_piece(&tab[i], tab[i].tx, src_slot, dst_slot);
}

/*
 * q3d_draw_strip_b - as q3d_draw_strip_a, but the side pieces 9, 10, 19,
 * 20, 25, 26 take their texture 0EBh pixels further right when facing
 * north or west (the textures hold both shadings side by side).
 *
 * Original: IST.EXE 2700:35FF (3DQUICK.L), far cdecl (i, u8 src, u8 dst,
 * far tab).
 * Verified: tests/py/test_q3dview.py (test_strips).
 */
void q3d_draw_strip_b(int16_t i, uint8_t src_slot, uint8_t dst_slot, const Q3dPiece *tab)
{
    int16_t off = 0;

    if ((i == 9 || i == 10 || i == 0x13 || i == 0x14 || i == 0x19 || i == 0x1A) &&
        (g_s3d_facing % 4 == 0 || g_s3d_facing % 4 == 3))
        off = 0xEB;
    if (src_slot == 0)
        return;
    draw_piece(&tab[i], (int16_t)(tab[i].tx + off), src_slot, dst_slot);
}

/*
 * q3d_reset_kor - (misnamed) place the 4 decoration pictures (surfaces
 * 20h-23h, 4 frames side by side): size (surface w/4, h) scaled by
 * view/view (the original's (int)(view_w * w) / view_w, 16-bit product),
 * centred horizontally on the front wall piece 23 and standing on its
 * bottom.
 *
 * Original: IST.EXE 2700:37EE (3DQUICK.L), far cdecl.
 * Verified: tests/py/test_q3dview.py (test_small).
 */
void q3d_reset_kor(void)
{
    int16_t vw = g_q3d_view_w, vh = g_q3d_view_h;   /* [bp-4], [bp-6] */
    int16_t k;

    for (k = 0; k < 4; k++) {
        Q3dDecor *d = &g_q3d_decor[k];
        d->src_w = (int16_t)(g_surf_w(0x20 + k) / 4);
        d->src_h = g_surf_h(0x20 + k);
        d->w = (int16_t)((int16_t)(g_q3d_view_w * d->src_w) / vw);   /* IMUL; CWD; IDIV */
        d->h = (int16_t)((int16_t)(g_q3d_view_h * d->src_h) / vh);
        d->x = (int16_t)(g_q3d_pieces[23].x + (int16_t)(g_q3d_pieces[23].w - d->w) / 2);
        d->odd = (int16_t)((int16_t)(g_q3d_view_w - d->w) % 2);
        d->y = (int16_t)(g_q3d_pieces[23].y + g_q3d_pieces[23].h - d->h);
    }
}
