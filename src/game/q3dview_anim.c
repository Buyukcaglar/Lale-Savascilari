/*
 * q3dview_anim.c - 3DQUICK.L part 3 (IST.EXE ovl2700:647B-93D5): the step,
 * turn, door and rotation animations of the first-person view and the sky
 * background.  See q3dview.h.
 *
 * Work surfaces: 40h is the sky panorama (4 x 160 wide, 114 rows), 41h the
 * frame being composed, 42h..44h layers of the rendered view (allocated
 * view_w x view_h, EMS, by q3d_wait_retrace and freed by
 * q3d_free_anim_buffer; both take a bit mask, bit n = surface 41h + n).
 * Every frame goes to the screen through q3d_blit_view(41h, 30h, 34h).
 *
 * Timing: the animations run for g_q3d_anim_speed steps and pick the step
 * from the 100 Hz clock (30 steps per second, step = 30 * ms / 1000 + 1),
 * low 16 bits of g_clock_ms only.
 *
 * Arithmetic: the original multiplies ints with 16-bit IMUL followed by CWD
 * (the high half of the product is dropped), so every product is wrapped to
 * int16 here (M16); long products/quotients are Borland's LXMUL@/LDIV@.
 */
#include "game/q3dview.h"

#include <stdint.h>

#include "dsmi/cdi.h"
#include "engine/blit386.h"
#include "engine/fatal.h"
#include "engine/memman.h"
#include "engine/nkontrol.h"
#include "engine/nses.h"
#include "engine/oses.h"
#include "engine/systems.h"
#include "game/q3d.h"
#include "game/x87ext.h"
#include "platform/pc.h"
#include "rtl/bc_rand.h"

#define ANIM_FILE       DGSTR(0x3D41)   /* "T:\CODE\3DQUICK.L" */
#define ANIM_FILE2      DGSTR(0x3D15)   /* (another copy) */
#define K_160           0x3D53          /* float 160.0 */
#define K_3             0x3D57          /* float 3.0 */
#define K_4             0x3BAD          /* float 4.0 */

/* (int16) a * b as IMUL r16 + CWD */
static int16_t m16(int16_t a, int16_t b) { return (int16_t)(a * b); }
/* LXMUL@ + LDIV@, low word of the quotient */
static int16_t muldiv(int16_t a, int16_t b, int16_t c)
{
    return (int16_t)(((int32_t)a * (int32_t)b) / (int32_t)c);
}
/* ((int32) frames of 30 Hz since t0) + 1 */
static int16_t anim_tick(uint16_t t0)
{
    return (int16_t)((int32_t)((uint32_t)(uint16_t)((uint16_t)g_clock_ms - t0) * 0x1E) / 1000 + 1);
}
static float dg_float(uint16_t off) { return DGF32(off); }
static int16_t max0(int16_t v) { return v < 0 ? 0 : v; }
static int16_t min0(int16_t v) { return v > 0 ? 0 : v; }

/* whole view in bytes (IMUL r16) */
#define VIEW_BYTES      ((uint16_t)m16(g_q3d_view_h, g_q3d_view_w))

/*
 * q3d_wait_retrace - (misnamed) allocate the animation work surfaces: for
 * every bit n of mask (n = 0..3) surface 41h + n is allocated view_w x
 * view_h, type 1 (EMS), unless it already exists.
 *
 * Original: IST.EXE 2700:8FD4 (3DQUICK.L), far cdecl (u8 mask).
 * Verified: tests/py/test_q3dview_anim.py (test_buffers).
 */
void q3d_wait_retrace(uint8_t mask)
{
    int16_t n;

    for (n = 0; n < 4; n++, mask >>= 1) {
        if ((mask & 1) && (g_surf_mm_off(0x41 + n) | g_surf_mm_seg(0x41 + n)) == 0)
            mm_surface_alloc((uint8_t)(0x41 + n), g_q3d_view_w, g_q3d_view_h, 1);
    }
}

/*
 * q3d_free_anim_buffer - free the work surfaces 41h + n of the bits of mask
 * that are allocated (line 931h).
 *
 * Original: IST.EXE 2700:9024 (3DQUICK.L), far cdecl (u8 mask).
 * Verified: tests/py/test_q3dview_anim.py (test_buffers).
 */
void q3d_free_anim_buffer(uint8_t mask)
{
    int16_t n;

    for (n = 0; n < 4; n++, mask >>= 1) {
        if ((mask & 1) && (g_surf_mm_off(0x41 + n) | g_surf_mm_seg(0x41 + n)) != 0)
            ctl_free_checked((uint16_t)(0xE29F + 8 * (0x41 + n)), 0x931, ANIM_FILE);
    }
}

/*
 * q3d_shade_view - (misnamed) draw the sky: copy the 114 rows of the sky
 * panorama src (4 x 160 columns, one quarter per facing) seen at angle a
 * (0.0 <= a < 4.0, in facings) into dst.  At a > 3 the picture wraps
 * around: [a*160, 640) then [0, (4-a)*160).
 *
 * At view 200x200 the copy is 1:1 and moved by the view shake, which is
 * chosen once (random(2) each, the first time the party stands on a new
 * cell: g_q3d_shake_set clear and the cell != g_s3d_prev_x/y).  At other
 * view sizes the panorama is scaled to view_w columns x
 * (u16)(114 * view_w) / 200 + 2 rows (sic: view_w).
 *
 * x87: ftol(a * 160) and ftol((4 - a) * 160), float operands, 64-bit
 * precision intermediate, truncation; a NaN goes the no-wrap way.
 *
 * Original: IST.EXE 2700:9078 (3DQUICK.L), far cdecl (float a, u8 src,
 * u8 dst).
 * Verified: tests/py/test_q3dview_anim.py (test_shade_view).
 */
void q3d_shade_view(float a, uint8_t src, uint8_t dst)
{
    int16_t x0, x1;
    uint16_t dh;

    x0 = (int16_t)ext_ftol(ext_mul(ext_from_float(a), ext_from_float(dg_float(K_160))));
    if (g_q3d_view_w == 200 && g_q3d_view_h == 200) {
        if ((g_s3d_prev_x != g_s3d_x || g_s3d_prev_y != g_s3d_y) && g_q3d_shake_set == 0) {
            g_q3d_shift_x = bc_random(2);
            g_q3d_shift_y = bc_random(2);
            g_q3d_shake_set = 1;
        }
        if (!(a > dg_float(K_3))) {
            sys_blit((uint16_t)(x0 + max0(g_q3d_shift_x)), (uint16_t)max0(g_q3d_shift_y),
                     (uint16_t)(g_q3d_view_w - max0(g_q3d_shift_x)),
                     (uint16_t)(0x72 - max0(g_q3d_shift_y)),
                     (uint16_t)-min0(g_q3d_shift_x), (uint16_t)-min0(g_q3d_shift_y),
                     src, dst, sys_rect_copy);
            return;
        }
        x1 = (int16_t)ext_ftol(ext_mul(ext_sub(ext_from_float(dg_float(K_4)), ext_from_float(a)),
                                       ext_from_float(dg_float(K_160))));
        sys_blit((uint16_t)(x0 + max0(g_q3d_shift_x)), (uint16_t)max0(g_q3d_shift_y),
                 (uint16_t)(x1 - max0(g_q3d_shift_x)), (uint16_t)(0x72 - max0(g_q3d_shift_y)),
                 (uint16_t)-min0(g_q3d_shift_x), (uint16_t)-min0(g_q3d_shift_y),
                 src, dst, sys_rect_copy);
        sys_blit((uint16_t)max0(g_q3d_shift_x), (uint16_t)max0(g_q3d_shift_y),
                 (uint16_t)(g_q3d_view_w - x1 - max0(g_q3d_shift_x)),
                 (uint16_t)(0x72 - max0(g_q3d_shift_y)),
                 (uint16_t)(x1 - min0(g_q3d_shift_x)), (uint16_t)-min0(g_q3d_shift_y),
                 src, dst, sys_rect_copy);
        return;
    }
    dh = (uint16_t)((uint16_t)((int32_t)0x72 * g_q3d_view_w) / 200u + 2);
    if (!(a > dg_float(K_3))) {
        sys_blit_scaled((uint16_t)x0, 0, 200, 0x72, 0, 0, (uint16_t)g_q3d_view_w, dh, src, dst, 0);
        return;
    }
    x1 = (int16_t)ext_ftol(ext_mul(ext_sub(ext_from_float(dg_float(K_4)), ext_from_float(a)),
                                   ext_from_float(dg_float(K_160))));
    sys_blit_scaled((uint16_t)x0, 0, (uint16_t)x1, 0x72, 0, 0,
                    (uint16_t)(m16(x1, g_q3d_view_w) / 200), dh, src, dst, 0);
    sys_blit_scaled(0, 0, (uint16_t)(200 - x1), 0x72, (uint16_t)(m16(x1, g_q3d_view_w) / 200), 0,
                    (uint16_t)(m16((int16_t)(200 - x1), g_q3d_view_w) / 200), dh, src, dst, 0);
}

/* the sky of the current facing into surface 41h */
static void anim_sky(void)
{
    q3d_shade_view((float)(int16_t)(g_s3d_facing % 4), 0x40, 0x41);
}

/* Steps of a step animation (g_q3d_wall_bits): all of g_q3d_anim_speed
   with a left wall (own cell or distance 1) and a right wall (distance 1
   or 2), or a wall in front of the party; half of them otherwise. */
static int16_t anim_steps(void)
{
    uint16_t f = g_q3d_wall_bits;

    if (((f & 0x10) || (f & 4)) && ((f & 2) || (f & 8)))
        return g_q3d_anim_speed;
    if (f & 0x40)
        return g_q3d_anim_speed;
    return (int16_t)(g_q3d_anim_speed / 2);
}

/* The rows [top, view_h - bot) of the rendered layers: far/mid into 44h,
   near into `near`, then the side walls and the front piece column into
   `near` after the keyed copies of what is there into 44h. */
static void anim_render_layers(uint8_t near, int16_t top, int16_t bot)
{
    Q3dPiece *p = g_q3d_pieces;
    int16_t h = (int16_t)(g_q3d_view_h - top - bot);
    int16_t x, w;

    q3d_render_layer_far(0x44);
    q3d_render_layer_mid(0x44);
    q3d_render_layer_near(near);
    sys_blit(0, (uint16_t)top, (uint16_t)g_q3d_view_w, (uint16_t)h, 0, (uint16_t)top, near, 0x44,
             asm_copy_rect_key);
    sys_fill_rect(0, (uint16_t)top, (uint16_t)g_q3d_view_w, (uint16_t)h, near, 0xFE);
    q3d_render_layer_side(near);
    x = (int16_t)(p[19].x + m16(p[19].w, 3) / 4);
    w = (int16_t)(p[15].w + (int16_t)(p[19].w << 1) / 4);
    sys_blit((uint16_t)x, (uint16_t)top, (uint16_t)w, (uint16_t)h, (uint16_t)x, (uint16_t)top,
             near, 0x44, asm_copy_rect_key);
    sys_fill_rect((uint16_t)x, (uint16_t)top, (uint16_t)w, (uint16_t)h, near, 0xFE);
    q3d_render_layer_front(near);
}

static int16_t anim_top(void)
{
    return g_q3d_ceiling_here ? (int16_t)(g_q3d_view_h / 3 - 3) : (int16_t)(g_q3d_view_h / 3 + 2);
}

/*
 * q3d_anim_step_forward - the "step forward" zoom.  The new view (already
 * rendered by the caller unless g_q3d_anim_mode) is split into the far
 * part (44h) and the near walls (43h); the old frame in 42h (its centre
 * cleared unless g_q3d_anim_keep) grows out of the view while the new
 * parts zoom in from the centre: per step i the old frame is scaled up by
 * 2*i*c/100 pixels, the near part shrinks onto its border k and the far
 * part onto g (k and g follow the perspective; g starts at view_w / 5 and
 * is updated from the previous step).  Speed 0: just redraw.
 *
 * Original: IST.EXE 2700:647B (3DQUICK.L), far cdecl.
 * Verified: tests/py/test_q3dview_anim.py (test_step).
 */
void q3d_anim_step_forward(void)
{
    int16_t i, steps, a, b, c = 0, d = 0, g, k, m, e2, top, bot, n;
    uint16_t t0;

    if (g_q3d_anim_speed == 0) {
        s3d_redraw_view(1);
        return;
    }
    top = anim_top();
    bot = (int16_t)(g_q3d_view_h / 3 - 3);
    q3d_free_anim_buffer(1);
    q3d_wait_retrace(0x0E);
    if (g_q3d_anim_mode == 0) {
        sys_fill_slot(0x43, VIEW_BYTES, 0xFE);
        sys_fill_slot(0x44, VIEW_BYTES, 0xFE);
        anim_render_layers(0x43, top, bot);
    }
    q3d_wait_retrace(1);
    steps = anim_steps();
    a = (int16_t)(g_q3d_view_w / 3 + 1);
    b = (int16_t)(g_q3d_view_h / 10);
    if (steps != 0)
        c = (int16_t)(m16(a, 100) / steps);
    if (steps != 0)
        d = (int16_t)(m16(b, 100) / steps);
    g = (int16_t)(g_q3d_view_w / 5);
    if (g_q3d_anim_keep == 0)
        sys_fill_rect((uint16_t)a, (uint16_t)a, (uint16_t)(g_q3d_view_w - (a << 1) - 1),
                      (uint16_t)(g_q3d_view_h - (a << 1) - 1), 0x42, 0xFE);
    i = 1;
    t0 = (uint16_t)g_clock_ms;
    for (n = 0; i < steps; n++) {
        ctl_flush_keys();
        sys_fill_rect(0, 0, (uint16_t)g_q3d_view_w, (uint16_t)g_q3d_view_h, 0x41, 0xFE);
        anim_sky();
        e2 = (int16_t)(g_q3d_view_w - m16((int16_t)(i << 1), c) / 100);
        k = muldiv((int16_t)(a - m16(i, c) / 100), g_q3d_view_w,
                   (int16_t)(g_q3d_view_w - m16((int16_t)(i << 1), c) / 100));
        m = muldiv((int16_t)(b - m16(i, d) / 100), (int16_t)(g_q3d_view_w - (g << 1)),
                   (int16_t)(g_q3d_view_w - (g << 1) - m16((int16_t)(i << 1), d) / 100));
        g = muldiv((int16_t)(k - m), g_q3d_view_w, (int16_t)(g_q3d_view_w - (m << 1)));
        sys_blit_scaled((uint16_t)g, (uint16_t)g, (uint16_t)(g_q3d_view_w - (g << 1)),
                        (uint16_t)(g_q3d_view_h - (g << 1)), (uint16_t)k, (uint16_t)k,
                        (uint16_t)(g_q3d_view_w - (k << 1)), (uint16_t)(g_q3d_view_h - (k << 1)),
                        0x44, 0x41, 0);
        sys_blit_scaled(0, 0, (uint16_t)g_q3d_view_w, (uint16_t)g_q3d_view_h, (uint16_t)k,
                        (uint16_t)k, (uint16_t)(g_q3d_view_w - (k << 1)),
                        (uint16_t)(g_q3d_view_h - (k << 1)), 0x43, 0x41, 0);
        sys_blit_scaled((uint16_t)(m16(i, c) / 100), (uint16_t)(m16(i, c) / 100), (uint16_t)e2,
                        (uint16_t)e2, 0, 0, (uint16_t)g_q3d_view_w, (uint16_t)g_q3d_view_h,
                        0x42, 0x41, 0);
        q3d_blit_view(0x41, 0x30, 0x34);
        i = anim_tick(t0);
    }
    (void)n;
    q3d_wait_retrace(3);
    sys_blit_0615(0x44, 0x42, VIEW_BYTES, 0, sys_span_memcpy);
    sys_blit_0615(0x43, 0x42, VIEW_BYTES, 0xFE, mm_copy_transparent);
    sys_fill_slot(0x41, VIEW_BYTES, 0xFE);
    anim_sky();
    sys_blit_0615(0x42, 0x41, VIEW_BYTES, 0xFE, mm_copy_transparent);
    q3d_blit_view(0x41, 0x30, 0x34);
    q3d_free_anim_buffer(0x0C);
    g_q3d_anim_keep = 0;
}

/*
 * q3d_anim_step_back - the "step back" zoom: the current view (rendered
 * into 43h) shrinks into the centre while the view of the cell behind
 * (rendered with the position moved by one step for the duration, far part
 * 44h, near walls 42h) zooms down from outside; the step goes from
 * steps - 1 down to 0.  The cell is restored before the loop (the caller
 * moves the party).  Speed 0: just redraw.
 *
 * Original: IST.EXE 2700:69E2 (3DQUICK.L), far cdecl.
 * Verified: tests/py/test_q3dview_anim.py (test_step).
 */
void q3d_anim_step_back(void)
{
    int16_t i, steps, a, b, c = 0, d = 0, g, k, m, e2, top, bot;
    uint16_t t0;

    if (g_q3d_anim_speed == 0) {
        s3d_redraw_view(1);
        return;
    }
    top = anim_top();
    bot = (int16_t)(g_q3d_view_h / 3 - 3);
    q3d_free_anim_buffer(1);
    q3d_wait_retrace(0x0E);
    sys_fill_slot(0x43, VIEW_BYTES, 0xFE);
    q3d_render_view(0x43);
    g_s3d_x = (int16_t)(g_s3d_x + g_s3d_step_dx);
    g_s3d_y = (int16_t)(g_s3d_y + g_s3d_step_dy);
    sys_fill_slot(0x42, VIEW_BYTES, 0xFE);
    sys_fill_slot(0x44, VIEW_BYTES, 0xFE);
    anim_render_layers(0x42, top, bot);
    q3d_wait_retrace(1);
    steps = anim_steps();
    a = (int16_t)(g_q3d_view_h / 3);
    b = (int16_t)(g_q3d_view_h / 10);
    if (steps != 0)
        c = (int16_t)(m16(a, 100) / steps);
    if (steps != 0)
        d = (int16_t)(m16(b, 100) / steps);
    g = (int16_t)(g_q3d_view_w / 5);
    g_s3d_x = (int16_t)(g_s3d_x - g_s3d_step_dx);
    g_s3d_y = (int16_t)(g_s3d_y - g_s3d_step_dy);
    i = (int16_t)(steps - 1);
    t0 = (uint16_t)g_clock_ms;
    while (i > 0) {
        ctl_flush_keys();
        sys_fill_slot(0x41, VIEW_BYTES, 0xFE);
        anim_sky();
        e2 = (int16_t)(g_q3d_view_w - m16((int16_t)(i << 1), c) / 100);
        k = muldiv((int16_t)(a - m16(i, c) / 100), g_q3d_view_w,
                   (int16_t)(g_q3d_view_w - m16((int16_t)(i << 1), c) / 100));
        m = muldiv((int16_t)(b - m16(i, d) / 100), (int16_t)(g_q3d_view_w - (g << 1)),
                   (int16_t)(g_q3d_view_w - (g << 1) - m16((int16_t)(i << 1), d) / 100));
        g = muldiv((int16_t)(k - m), g_q3d_view_w, (int16_t)(g_q3d_view_w - (m << 1)));
        sys_blit_scaled((uint16_t)(m16(i, c) / 100), (uint16_t)(m16(i, c) / 100), (uint16_t)e2,
                        (uint16_t)e2, 0, 0, (uint16_t)g_q3d_view_w, (uint16_t)g_q3d_view_h,
                        0x43, 0x41, 0);
        sys_blit_scaled((uint16_t)g, (uint16_t)g, (uint16_t)(g_q3d_view_w - (g << 1)),
                        (uint16_t)(g_q3d_view_h - (g << 1)), (uint16_t)k, (uint16_t)k,
                        (uint16_t)(g_q3d_view_w - (k << 1)), (uint16_t)(g_q3d_view_h - (k << 1)),
                        0x44, 0x41, 0);
        sys_blit_scaled(0, 0, (uint16_t)g_q3d_view_w, (uint16_t)g_q3d_view_h, (uint16_t)k,
                        (uint16_t)k, (uint16_t)(g_q3d_view_w - (k << 1)),
                        (uint16_t)(g_q3d_view_h - (k << 1)), 0x42, 0x41, 0);
        q3d_blit_view(0x41, 0x30, 0x34);
        i = (int16_t)(steps - anim_tick(t0));
    }
    sys_blit_0615(0x43, 0x42, VIEW_BYTES, 0, sys_span_memcpy);
    sys_fill_slot(0x41, VIEW_BYTES, 0xFE);
    anim_sky();
    sys_blit_0615(0x42, 0x41, VIEW_BYTES, 0xFE, mm_copy_transparent);
    q3d_blit_view(0x41, 0x30, 0x34);
    q3d_free_anim_buffer(0x0C);
}

/* The 60x32 box of the interface picture (surface 51h) next to the view
   (x = (mode - 1) * view_w + 46h, y = 94h for mode 1 else 14h) that the
   turn animations save into 44h at (0, 44h) and restore at the end. */
static int16_t box_x(void) { return (int16_t)(m16((int16_t)(g_q3d_anim_mode - 1), g_q3d_view_w) + 0x46); }
static int16_t box_y(void) { return g_q3d_anim_mode == 1 ? 0x94 : 0x14; }

static void box_save(void)
{
    sys_blit((uint16_t)box_x(), (uint16_t)box_y(), 0x3C, 0x20, 0, 0x44, 0x51, 0x44, sys_rect_copy);
    sys_fill_rect((uint16_t)box_x(), (uint16_t)box_y(), 0x3C, 0x20, 0x51, 0xFE);
}

static void box_restore(void)
{
    sys_blit(0, 0x44, 0x3C, 0x20, (uint16_t)box_x(), (uint16_t)box_y(), 0x44, 0x51, sys_rect_copy);
}

/*
 * q3d_anim_turn_a - zoom from the interface picture into the view: the
 * view's part of surface 51h (column (mode - 1) * view_w, in 42h) grows
 * out of the window over 18 steps while the newly rendered view (43h)
 * comes in from the side: mode 1 from the bottom-left corner, mode 2 from
 * the top-left.  Disabled (plain redraw) unless g_q3d_anim_turn.  Any
 * other mode is fatal (line 750h, code 50h).
 *
 * Original: IST.EXE 2700:6F18 (3DQUICK.L), far cdecl.
 * Verified: tests/py/test_q3dview_anim.py (test_turn).
 */
void q3d_anim_turn_a(void)
{
    int16_t i, steps, a, c = 0, e6, k, kw, kh;
    uint16_t t0;

    if (g_q3d_anim_turn == 0) {
        s3d_redraw_view(1);
        return;
    }
    steps = 0x12;
    a = (int16_t)(g_q3d_view_h / 3);
    if (steps != 0)
        c = (int16_t)(m16(a, 100) / steps);
    q3d_wait_retrace(0x0B);
    box_save();
    q3d_wait_retrace(4);
    sys_fill_slot(0x43, VIEW_BYTES, 0xFE);
    q3d_render_view(0x43);
    sys_blit((uint16_t)m16((int16_t)(g_q3d_anim_mode - 1), g_q3d_view_w), 0,
             (uint16_t)g_q3d_view_w, (uint16_t)g_q3d_view_h, 0, 0, 0x51, 0x42, sys_rect_copy);
    if (g_q3d_anim_mode == 1 || g_q3d_anim_mode == 2) {
        int mode1 = g_q3d_anim_mode == 1;

        i = 1;
        t0 = (uint16_t)g_clock_ms;
        while (i <= steps) {
            ctl_flush_keys();
            sys_fill_slot(0x41, VIEW_BYTES, 0xFE);
            anim_sky();
            e6 = (int16_t)(g_q3d_view_w - m16((int16_t)(i << 1), c) / 100);
            k = muldiv((int16_t)(a - m16(i, c) / 100), g_q3d_view_w,
                       (int16_t)(g_q3d_view_w - m16((int16_t)(i << 1), c) / 100));
            kw = (int16_t)(g_q3d_view_w - (k << 1));
            kh = (int16_t)(g_q3d_view_h - (k << 1));
            sys_blit_scaled(0, 0, (uint16_t)g_q3d_view_w, (uint16_t)g_q3d_view_h, (uint16_t)k,
                            mode1 ? (uint16_t)(g_q3d_view_h - kh) : 0, (uint16_t)kw, (uint16_t)kh,
                            0x43, 0x41, 0);
            sys_blit_scaled((uint16_t)(m16(i, c) / 100),
                            mode1 ? (uint16_t)(g_q3d_view_h - e6) : 0, (uint16_t)e6, (uint16_t)e6,
                            0, 0, (uint16_t)g_q3d_view_w, (uint16_t)g_q3d_view_h, 0x42, 0x41, 0);
            q3d_blit_view(0x41, 0x30, 0x34);
            i = anim_tick(t0);
        }
    } else {
        o559A_fatal(ANIM_FILE2, 0x750, NULL, 0x50);
    }
    sys_fill_slot(0x41, VIEW_BYTES, 0xFE);
    anim_sky();
    sys_blit_0615(0x43, 0x41, VIEW_BYTES, 0xFE, mm_copy_transparent);
    q3d_blit_view(0x41, 0x30, 0x34);
    sys_blit_0615(0x43, 0x42, VIEW_BYTES, 0, sys_span_memcpy);
    q3d_free_anim_buffer(4);
    box_restore();
    q3d_free_anim_buffer(8);
}

/*
 * q3d_anim_turn_b - zoom out of the view into the interface picture (the
 * reverse of turn_a): the party moves one step (also with animations
 * off), the view rendered there is shrunk to a 68x68 thumbnail (44h) that
 * slides into the 60x32 box while the old picture (43h = surface 51h's
 * view column) shrinks into the window and the frame around it (42h, four
 * scaled borders) narrows.  speed / 2 steps.
 *
 * Original: IST.EXE 2700:73D8 (3DQUICK.L), far cdecl.  The thumbnail
 * slide divides by the step count without the zero check of the others
 * (only reached with >= 2 steps).
 * Verified: tests/py/test_q3dview_anim.py (test_turn).
 */
void q3d_anim_turn_b(void)
{
    int16_t i, steps, c = 0, e6, e8, k, ic, kw, kh, sh, qq;
    uint16_t t0;

    if (g_q3d_anim_speed == 0) {
        g_s3d_x = (int16_t)(g_s3d_x + g_s3d_step_dx);
        g_s3d_y = (int16_t)(g_s3d_y + g_s3d_step_dy);
        return;
    }
    steps = (int16_t)(g_q3d_anim_speed / 2);
    if (steps != 0)
        c = (int16_t)(m16(0x1E, 100) / steps);
    q3d_free_anim_buffer(1);
    q3d_wait_retrace(0x0E);
    sys_fill_slot(0x43, VIEW_BYTES, 0xFE);
    sys_fill_slot(0x44, VIEW_BYTES, 0xFE);
    g_s3d_x = (int16_t)(g_s3d_x + g_s3d_step_dx);
    g_s3d_y = (int16_t)(g_s3d_y + g_s3d_step_dy);
    q3d_render_view(0x43);
    sys_blit_scaled(0, 0, (uint16_t)g_q3d_view_w, (uint16_t)g_q3d_view_h, 0, 0, 0x44, 0x44,
                    0x43, 0x44, 0);
    box_save();
    sys_fill_slot(0x43, VIEW_BYTES, 0xFE);
    sys_blit((uint16_t)m16((int16_t)(g_q3d_anim_mode - 1), g_q3d_view_w), 0,
             (uint16_t)g_q3d_view_w, (uint16_t)g_q3d_view_h, 0, 0, 0x51, 0x43, sys_rect_copy);
    q3d_wait_retrace(1);
    i = 1;
    t0 = (uint16_t)g_clock_ms;
    while (i < steps) {
        ctl_flush_keys();
        sys_fill_slot(0x41, VIEW_BYTES, 0xFE);
        anim_sky();
        e6 = (int16_t)(g_q3d_view_w - m16((int16_t)(i << 1), c) / 100);
        e8 = (int16_t)(0x1E - m16(i, c) / 100);
        k = muldiv((int16_t)(0x1E - m16(i, c) / 100), g_q3d_view_w,
                   (int16_t)(g_q3d_view_w - m16((int16_t)(i << 1), c) / 100));
        sh = (int16_t)(g_q3d_view_h - 0x3C);
        kw = (int16_t)(g_q3d_view_w - (k << 1));
        kh = (int16_t)(g_q3d_view_h - (k << 1));
        ic = (int16_t)(m16(i, c) / 100);
        sys_blit_scaled((uint16_t)ic, (uint16_t)ic, (uint16_t)e6, (uint16_t)e8, 0, 0,
                        (uint16_t)g_q3d_view_w, (uint16_t)k, 0x42, 0x41, 0);
        sys_blit_scaled((uint16_t)ic, 0x1E, (uint16_t)e8, (uint16_t)sh, 0, (uint16_t)k,
                        (uint16_t)k, (uint16_t)kh, 0x42, 0x41, 0);
        sys_blit_scaled((uint16_t)(g_q3d_view_w - ic - e8), 0x1E, (uint16_t)e8, (uint16_t)sh,
                        (uint16_t)(kw + k), (uint16_t)k, (uint16_t)k, (uint16_t)kh, 0x42, 0x41, 0);
        sys_blit_scaled((uint16_t)ic, (uint16_t)(g_q3d_view_h - ic - e8), (uint16_t)e6,
                        (uint16_t)e8, 0, (uint16_t)(g_q3d_view_h - k), (uint16_t)g_q3d_view_w,
                        (uint16_t)k, 0x42, 0x41, 0);
        sys_blit(6, 0x14, 0x3C, 0x20, 0x46, (uint16_t)box_y(), 0x44, 0x43, sys_rect_copy);
        qq = (int16_t)(m16((int16_t)(m16(0x20, 100) / steps), i) / 100);
        sys_blit(0, (uint16_t)(qq + 0x44), 0x3C, (uint16_t)(0x20 - qq), 0x45, (uint16_t)box_y(),
                 0x44, 0x43, sys_rect_copy);
        sys_blit_scaled(0, 0, (uint16_t)g_q3d_view_w, (uint16_t)g_q3d_view_h, (uint16_t)k,
                        (uint16_t)k, (uint16_t)kw, (uint16_t)kh, 0x43, 0x41, 0);
        q3d_blit_view(0x41, 0x30, 0x34);
        i = anim_tick(t0);
    }
    anim_sky();
    sys_blit(6, 0x14, 0x3C, 0x20, 0x46, (uint16_t)box_y(), 0x44, 0x43, sys_rect_copy);
    sys_blit_0615(0x43, 0x41, VIEW_BYTES, 0xFE, mm_copy_transparent);
    q3d_blit_view(0x41, 0x30, 0x34);
    q3d_free_anim_buffer(4);
    box_restore();
    q3d_free_anim_buffer(8);
}

/* Door decoration n, frame f (surface 20h + n holds 4 frames side by side,
   src_w wide each) at its place in the front wall (g_q3d_decor[n]) into
   dst: scaled when the front piece (23) is scaled, else a keyed copy;
   mirrored to the other side of the view when `mirror`.
   Quirk kept: only the first (closed) frame into 42h (`at_mirror_x`) puts
   an unscaled mirrored frame at view_w - x - src_w (2700:7BFD); the
   frames of the animation and the no-turn frame mirror it in place at x
   (2700:7E4B, 8036: the mirror blit shares the unmirrored arguments). */
static void door_frame(int16_t n, uint8_t dst, int16_t f, int mirror, int at_mirror_x)
{
    Q3dDecor *d = &g_q3d_decor[n];
    uint16_t sx = (uint16_t)m16(f, d->src_w);
    uint8_t src = (uint8_t)(n + 0x20);

    if (!mirror) {
        if (g_q3d_pieces[23].flags & 2)
            sys_blit_scaled(sx, 0, (uint16_t)d->src_w, (uint16_t)d->src_h, (uint16_t)d->x,
                            (uint16_t)d->y, (uint16_t)d->w, (uint16_t)d->h, src, dst, 0);
        else
            sys_blit(sx, 0, (uint16_t)d->src_w, (uint16_t)d->src_h, (uint16_t)d->x, (uint16_t)d->y,
                     src, dst, asm_copy_rect_key);
    } else {
        if (g_q3d_pieces[23].flags & 2)
            sys_blit_scaled(sx, 0, (uint16_t)d->src_w, (uint16_t)d->src_h,
                            (uint16_t)(g_q3d_view_w - d->x - d->w), (uint16_t)d->y,
                            (uint16_t)d->w, (uint16_t)d->h, src, dst, 1);
        else
            sys_blit(sx, 0, (uint16_t)d->src_w, (uint16_t)d->src_h,
                     (uint16_t)(at_mirror_x ? g_q3d_view_w - d->x - d->src_w : d->x),
                     (uint16_t)d->y, src, dst, asm_copy_rect_key_mirror);
    }
}

/* The frames are mirrored when facing % 4 is 2 or 3 and - in the animated
   uses - KOR slot n + 4's first header word is nonzero. */
static int door_mirror(int16_t n, int check_kor)
{
    int16_t f4 = (int16_t)(g_s3d_facing % 4);

    if (f4 == 1 || f4 == 0)
        return 0;
    return check_kor ? g_q3d_kor[n + 4].hdr0 != 0 : 1;
}

/*
 * q3d_anim_door - open the door decoration n in front of the party: the
 * view is rebuilt without the front layer's decoration (42h: sky + far,
 * mid, near and side layers, the front layer drawn separately into 41h
 * with the decoration's rectangle cleared, then keyed over 42h), the door
 * sound plays (g_q3d_sound: voice wall_set(n) + 0Dh) and with
 * g_q3d_anim_turn the 4 frames are shown 150 ms apart (busy wait on the
 * clock), otherwise only the last one.  Speed 0 skips the first (closed)
 * frame.  Without g_q3d_anim_mode the party then moves one step.
 *
 * Original: IST.EXE 2700:798C (3DQUICK.L), far cdecl (int n).  The wait
 * loop gets a pc_poll() (port: lets the clock run).
 * Verified: tests/py/test_q3dview_anim.py (test_door), tests/py/test_s3d_district.py
 * (test_try_move_animated: a real district, facing south, mirrored frames).
 */
void q3d_anim_door(int16_t n)
{
    int16_t f;
    uint16_t t0;
    Q3dDecor *d = &g_q3d_decor[n];

    q3d_wait_retrace(3);
    sys_fill_slot(0x42, VIEW_BYTES, 0xFE);
    sys_fill_slot(0x41, VIEW_BYTES, 0xFE);
    q3d_shade_view((float)(int16_t)(g_s3d_facing % 4), 0x40, 0x42);
    q3d_render_layer_far(0x42);
    q3d_render_layer_mid(0x42);
    q3d_render_layer_near(0x42);
    q3d_render_layer_side(0x42);
    g_q3d_anim_keep = 1;
    q3d_render_layer_front(0x41);
    sys_fill_rect((uint16_t)d->x, (uint16_t)d->y, (uint16_t)d->w, (uint16_t)d->h, 0x41, 0xFE);
    sys_blit_0615(0x41, 0x42, VIEW_BYTES, 0xFE, mm_copy_transparent);
    if (g_q3d_anim_speed != 0)
        door_frame(n, 0x42, 3, door_mirror(n, 1), 1);
    if (g_q3d_sound != 0) {
        int16_t set = g_q3d_wall_set(n);
        ui_load_voice((int16_t)(set + 0x0D), DG16((uint16_t)(0x23C5 + m16(set, 0x11))), 0, 0);
    }
    if (g_q3d_anim_turn != 0) {
        for (f = 0; f < 4; f++) {
            t0 = (uint16_t)g_clock_ms;
            ctl_flush_keys();
            sys_blit_0615(0x42, 0x41, VIEW_BYTES, 0, sys_span_memcpy);
            door_frame(n, 0x41, f, door_mirror(n, 1), 0);
            q3d_blit_view(0x41, 0x30, 0x34);
            while ((uint16_t)((uint16_t)g_clock_ms - t0) < 0x96)
                pc_poll();
        }
    } else {
        sys_blit_0615(0x42, 0x41, VIEW_BYTES, 0, sys_span_memcpy);
        door_frame(n, 0x41, 3, door_mirror(n, 0), 0);
        q3d_blit_view(0x41, 0x30, 0x34);
    }
    if (g_q3d_anim_mode == 0) {
        g_s3d_x = (int16_t)(g_s3d_x + g_s3d_step_dx);
        g_s3d_y = (int16_t)(g_s3d_y + g_s3d_step_dy);
    }
}

/*
 * q3d_anim_dispatch - the interface <-> view zoom: g_q3d_anim_mode = mode,
 * sound 14h + mode (g_q3d_sound), for modes 1 and 2 turn_b then turn_a;
 * then the sound slot is freed unless its table flags keep it (bit 0)
 * (cdiStopChannel(channels - 1), line 83Fh); mode back to 0.
 *
 * Original: IST.EXE 2700:80D0 (3DQUICK.L), far cdecl (u8 mode).
 * Verified: tests/py/test_q3dview_anim.py (test_dispatch).
 */
void q3d_anim_dispatch(uint8_t mode)
{
    int16_t v = (int16_t)(mode + 0x14);

    g_q3d_anim_mode = mode;
    if (g_q3d_sound != 0)
        ui_load_voice(v, g_snd_rate(v), 0x40, 0);
    if (g_q3d_anim_mode == 1 || g_q3d_anim_mode == 2) {
        q3d_anim_turn_b();
        q3d_anim_turn_a();
        g_q3d_anim_mode = 0;
    }
    if ((DG16(SND_SLOT_MM_REF(v)) | DG16(SND_SLOT_MM_REF(v) + 2)) != 0 &&
        !(g_snd_table_flags(v) & 1)) {
        cdiStopChannel((int32_t)(g_snd_channels - 1));
        ctl_free_checked(SND_SLOT_MM_REF(v), 0x83F, DGSTR(0x3D27));
    }
    g_q3d_anim_mode = 0;
}

/* ---- rotations ------------------------------------------------------------ */
#define K_PI_2          0x3D39          /* float 1.5707964 */
#define K_PI            0x3D3D          /* float 3.1415927 */
#define ROT_FRONT       ((Q3dQuad *)DGP(0xD938))   /* the two walls of the turn */
#define ROT_SIDE        ((Q3dQuad *)DGP(0xD968))
#define ROT_TMP         ((Q3dQuad *)DGP(0xD9C8))   /* rotated + clipped copy */

static Ext ext_i(int16_t v) { return ext_from_i32(v); }

/* Start of a rotation: the new view into 43h (42h holds the current one).
   In the 100x100 window the 200x200 views of 42h and 43h are first scaled
   down to 100x100 surfaces (the view size is 100 during the rotation). */
static void rot_begin(void)
{
    int16_t j;

    q3d_wait_retrace(7);
    sys_fill_slot(0x43, VIEW_BYTES, 0xFE);
    q3d_render_view(0x43);
    if (g_q3d_win_w == 100 && g_q3d_win_h == 100) {
        for (j = 1; j < 3; j++) {
            sys_fill_rect(0, 0, (uint16_t)g_q3d_win_w, (uint16_t)g_q3d_win_h, 0x41, 0xFE);
            sys_blit_scaled(0, 0, (uint16_t)g_q3d_view_w, (uint16_t)g_q3d_view_h, 0, 0,
                            (uint16_t)g_q3d_win_w, (uint16_t)g_q3d_win_h, (uint8_t)(0x41 + j),
                            0x41, 0);
            q3d_free_anim_buffer((uint8_t)(1 << j));
            g_q3d_view_w = 100;
            g_q3d_view_h = 100;
            q3d_wait_retrace((uint8_t)(1 << j));
            g_q3d_view_w = 200;
            g_q3d_view_h = 200;
            sys_blit(0, 0, (uint16_t)g_q3d_win_w, (uint16_t)g_q3d_win_h, 0, 0, 0x41,
                     (uint8_t)(0x41 + j), sys_rect_copy);
        }
        g_q3d_view_w = 100;
        g_q3d_view_h = 100;
        q3d_free_anim_buffer(1);
        q3d_wait_retrace(1);
    }
}

/* End: back to 200 (re-rendering the new view into 42h) in the small
   window, else 43h -> 42h; the last frame (sky + 42h) to the screen. */
static void rot_end(void)
{
    if (g_q3d_win_w == 100 && g_q3d_win_h == 100) {
        g_q3d_view_w = 200;
        g_q3d_view_h = 200;
        q3d_free_anim_buffer(2);
        q3d_wait_retrace(2);
        q3d_free_anim_buffer(1);
        q3d_wait_retrace(1);
        sys_fill_slot(0x42, VIEW_BYTES, 0xFE);
        q3d_render_view(0x42);
    } else {
        sys_blit_0615(0x43, 0x42, VIEW_BYTES, 0, sys_span_memcpy);
    }
    sys_fill_slot(0x41, VIEW_BYTES, 0xFE);
    anim_sky();
    sys_blit_0615(0x42, 0x41, VIEW_BYTES, 0xFE, mm_copy_transparent);
    q3d_blit_view(0x41, 0x30, 0x34);
    q3d_free_anim_buffer(4);
}

/* Projection of frame i: the eye moves back and forth (plane z +
   |(15000 - (i + 1) * (31000 / n)) / 1000|, 16-bit products, unsigned
   31000 / n). */
static void rot_window(int16_t i, int16_t n, int16_t cx, int16_t cy)
{
    int16_t t = 0x0F;
    uint16_t q = (uint16_t)((uint16_t)m16((int16_t)((t << 1) + 1), 1000) / (uint16_t)n);
    int16_t r = (int16_t)(m16(t, 1000) - m16((int16_t)(i + 1), (int16_t)q));
    int16_t a = (int16_t)(r / 1000);

    q3d_set_view_window(cx, cy, (int16_t)(g_q3d_rot_plane + (a < 0 ? -a : a)), g_q3d_rot_focal);
}

/* quad q rotated by angle about (cx, cz), near-clipped, mapped from src
   into 41h at (ox, 0) clipped to (0, 0, cw, ch) */
static void rot_draw(const Q3dQuad *q, int16_t cx, int16_t cz, float angle, uint8_t src,
                     int16_t ox, int16_t cw, int16_t ch)
{
    Q3dQuad *t = ROT_TMP;

    *t = q3d_rotate_quad(cx, cz, angle, *q);
    q3d_clip_quad_near(t);
    q3d_draw_quad(t->tu, t->tv, t->w, t->h, ox, 0, *t, src, 0x41, 0, 0, cw, ch);
}

/* angle i * (pi/2) / n (x87: fild i; fild n; fdivr dword pi/2; fmulp) */
static float rot_angle(int16_t i, int16_t n)
{
    return ext_to_float(ext_mul(ext_i(i), ext_div(ext_from_float(dg_float(K_PI_2)), ext_i(n))));
}

/*
 * q3d_anim_rotate_left - turn left: the current view (42h, front wall) and
 * the new one (43h, side wall) are mapped onto two quads at right angles
 * that rotate by -i * 90 / n degrees about the centre of the cell, with
 * the sky turning along ((n - i) / n + facing).  Speed 0: redraw.
 * Returns 1.
 *
 * Original: IST.EXE 2700:8197 (3DQUICK.L), far cdecl.  After the loop
 * (i >= n) the "last frame" for i = n - 1 is unreachable.
 * 3DSYS only animates in the 100x100 window (speed 9; the 200 window gets
 * speed 0).  With a 200x200 destination q3d_draw_quad is fatal (the
 * 40000-byte surface does not fit the EMS window at physical page 2).
 * Verified: tests/py/test_q3dview_anim.py (test_rotate) and
 * tests/py/test_q3dview_gap.py (test_rotate_game: the game's projection
 * 6D76/6D78, whole screen and every surface; test_rotate_wrap).
 */
int16_t q3d_anim_rotate_left(void)
{
    int16_t x0, z, w, h, e = 0x0F, cx, cz, n, i;
    uint16_t t0;
    float sky;

    if (g_q3d_anim_speed == 0) {
        s3d_redraw_view(1);
        return 1;
    }
    rot_begin();
    x0 = 0x32;
    z = (int16_t)(g_q3d_view_w + 0x12C);
    w = g_q3d_view_w;
    h = g_q3d_view_h;
    cx = (int16_t)(x0 + w / 2);
    cz = (int16_t)(z - h / 2);
    n = g_q3d_anim_speed;
    q3d_build_side_quad((int16_t)(x0 + e), 0, (int16_t)(z - 2), 0, 0, (int16_t)(w - e), h, ROT_SIDE);
    q3d_build_front_quad((int16_t)(x0 + e - 1), 0, (int16_t)(z - w + e), (int16_t)(e - 1), 0,
                         (int16_t)(w - e + 1), h, ROT_FRONT);
    i = 1;
    t0 = (uint16_t)g_clock_ms;
    while (i < n) {
        ctl_flush_keys();
        rot_window(i, n, (int16_t)(x0 + w / 2), (int16_t)(h / 2));
        sys_fill_slot(0x41, VIEW_BYTES, 0xFE);
        sky = ext_to_float(ext_add(ext_div(ext_i((int16_t)(n - i)), ext_i(n)),
                                   ext_i((int16_t)(g_s3d_facing % 4))));
        q3d_shade_view(sky, 0x40, 0x41);
        rot_draw(ROT_FRONT, cx, cz, rot_angle((int16_t)-i, n), 0x42, -0x31,
                 (int16_t)(g_q3d_view_w - 1), (int16_t)(g_q3d_view_h - 1));
        rot_draw(ROT_SIDE, cx, cz, rot_angle((int16_t)-i, n), 0x43, -0x31,
                 (int16_t)(g_q3d_view_w - 1), (int16_t)(g_q3d_view_h - 1));
        q3d_blit_view(0x41, 0x30, 0x34);
        i = anim_tick(t0);
    }
    if ((int16_t)(n - 1) > i) {
        i = (int16_t)(n - 1);
        sys_fill_slot(0x41, VIEW_BYTES, 0xFE);
        anim_sky();
        rot_draw(ROT_FRONT, cx, cz, rot_angle((int16_t)-i, n), 0x42, -0x31,
                 (int16_t)(g_q3d_view_w - 1), (int16_t)(g_q3d_view_h - 1));
        rot_draw(ROT_SIDE, cx, cz, rot_angle((int16_t)-i, n), 0x43, -0x31,
                 (int16_t)(g_q3d_view_w - 1), (int16_t)(g_q3d_view_h - 1));
        q3d_blit_view(0x41, 0x30, 0x34);
    }
    rot_end();
    return 1;
}

/*
 * q3d_anim_rotate_right - turn right: as rotate_left mirrored (the side
 * quad is built on the right and turned by 180 degrees first, rotation
 * +i * 90 / n, sky i / n + (facing + 3) % 4, clip view_w x view_h), and
 * the final frame i = n is drawn.  Returns 1.
 *
 * Original: IST.EXE 2700:8896 (3DQUICK.L), far cdecl.
 * Verified: tests/py/test_q3dview_anim.py (test_rotate) and
 * tests/py/test_q3dview_gap.py (test_rotate_game, test_rotate_wrap; see
 * q3d_anim_rotate_left).
 */
int16_t q3d_anim_rotate_right(void)
{
    int16_t x0, z, w, h, e = 0x0F, ox, cx, cz, n, i;
    uint16_t t0;
    float sky;

    if (g_q3d_anim_speed == 0) {
        s3d_redraw_view(1);
        return 1;
    }
    rot_begin();
    x0 = (int16_t)(g_q3d_view_w / 2);
    z = (int16_t)(g_q3d_view_w + 0x12C);
    w = g_q3d_view_w;
    h = g_q3d_view_h;
    ox = (int16_t)((int16_t)-g_q3d_view_w / 2);
    cx = (int16_t)(x0 + w / 2);
    cz = (int16_t)(z - h / 2);
    n = g_q3d_anim_speed;
    q3d_build_side_quad((int16_t)(x0 + w - e), 0, (int16_t)(z + 2), e, 0, (int16_t)(w - e), h,
                        ROT_FRONT);
    *ROT_FRONT = q3d_rotate_quad((int16_t)(x0 + w - e), (int16_t)(z - (int16_t)(w - e) / 2),
                                 dg_float(K_PI), *ROT_FRONT);
    q3d_build_front_quad((int16_t)(x0 + 1), 0, (int16_t)(z - w + e), 0, 0, (int16_t)(w - e), h,
                         ROT_SIDE);
    i = 1;
    t0 = (uint16_t)g_clock_ms;
    while (i < n) {
        ctl_flush_keys();
        rot_window(i, n, (int16_t)(x0 + w / 2), (int16_t)(h / 2));
        sys_fill_slot(0x41, VIEW_BYTES, 0xFE);
        sky = ext_to_float(ext_add(ext_div(ext_i(i), ext_i(n)),
                                   ext_i((int16_t)((g_s3d_facing + 3) % 4))));
        q3d_shade_view(sky, 0x40, 0x41);
        rot_draw(ROT_SIDE, cx, cz, rot_angle(i, n), 0x42, ox, g_q3d_view_w, g_q3d_view_h);
        rot_draw(ROT_FRONT, cx, cz, rot_angle(i, n), 0x43, ox, g_q3d_view_w, g_q3d_view_h);
        q3d_blit_view(0x41, 0x30, 0x34);
        i = anim_tick(t0);
    }
    i = n;
    if (i != 0) {
        sys_fill_slot(0x41, VIEW_BYTES, 0xFE);
        anim_sky();
        rot_draw(ROT_SIDE, cx, cz, rot_angle(i, n), 0x42, ox, g_q3d_view_w, g_q3d_view_h);
        rot_draw(ROT_FRONT, cx, cz, rot_angle(i, n), 0x43, ox, g_q3d_view_w, g_q3d_view_h);
        q3d_blit_view(0x41, 0x30, 0x34);
    }
    rot_end();
    return 1;
}
