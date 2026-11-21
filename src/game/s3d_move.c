/*
 * s3d_move.c - 3DSYS.L part 1 (IST.EXE ovl303F:0000-0BDA): moving the party
 * on the district map and redrawing the first-person view.  See s3d.h.
 *
 * Map cells (q3dview.h s3d_map): cell (x, y) is 15 bytes at
 * (y * map_w + x) * 15; byte [2*side] is the wall type of that side (an
 * index into the wall-set table g_q3d_wall_set, 1-based), [2*side+1] the
 * wall kind: 0 open, 1/2 the two door kinds that animate as a passage,
 * 2 a door, 3 a wall that is broken through when walked into.  The
 * original computes the cell offset in 16 bits (IMUL) and adds it to the
 * map pointer sign-extended (F_PADD), as cell() does here.
 */
#include "game/s3d.h"

#include <stdint.h>

#include "dsmi/emsheap.h"
#include "engine/blit386.h"
#include "engine/fatal.h"
#include "engine/memman.h"
#include "engine/ngraphs.h"
#include "engine/nkontrol.h"
#include "engine/systems.h"
#include "game/s3d_ext.h"
#include "platform/pc.h"
#include "rtl/bc_rand.h"

/* map byte at the sign-extended 16-bit offset (F_PADD) */
static uint8_t *cell(int16_t off)
{
    return &s3d_map[(int32_t)off];
}

/* offset of the party's cell: (y * map_w + x) * 15, 16-bit */
static int16_t here(void)
{
    return (int16_t)((int16_t)(g_s3d_x + (int16_t)(g_s3d_y * g_s3d_map_w)) * 15);
}

/* offset of the cell at (x + dx, y + dy) */
static int16_t there(int16_t dx, int16_t dy)
{
    return (int16_t)((int16_t)(g_s3d_x + dx + (int16_t)((g_s3d_y + dy) * g_s3d_map_w)) * 15);
}

/*
 * s3d_try_move - try to move the party one cell by (dx, dy), looking
 * through wall `facing` of its cell.
 *
 * The wall's type and kind decide:
 *  - type 9 (a passage / door frame, table 9F0E) with kind != 0: only a
 *    step in the facing direction (dx, dy == the facing's step) is
 *    possible; the leave (3) and enter (1) events run, then the passage
 *    animation (kind 1 -> q3d_anim_dispatch(2), kind 2 -> (1)); the party
 *    does not move here (the animation's caller does).
 *  - kind 0 (open): leave/enter events, then the party moves; returns 1.
 *  - kind 2 (door), type != 8, a step forward: events, then the door
 *    animation q3d_anim_door(type - 1); returns 1.
 *  - kind 3 (breakable wall): q3d_wait_retrace(3) (allocates 41h/42h)
 *    whatever the direction; a step forward runs the events, then the
 *    break-through effect: the wall is drawn shaded, removed from the map
 *    (kind 0), the view re-rendered and flashed, kind set back to 3, and
 *    the party moves; returns 1.
 * An event may cancel the move (FRPOS sets g_s3d_cancel_move).  Every
 * failed move clears g_s3d_view_dirty and returns 0.
 *
 * Original: IST.EXE 303F:0000 (3DSYS.L), far cdecl (int dx, int dy,
 * unsigned char facing), returns AL.  The type is looked up through the
 * parameter `facing`, the kind-2/3 checks through the party's facing
 * B9F0.
 * Verified: tests/py/test_s3d_move.py (test_try_move).
 */
uint8_t s3d_try_move(int16_t dx, int16_t dy, uint8_t facing)
{
    int16_t off = here();
    int16_t side = (int16_t)(facing % 4) * 2;

    if (g_q3d_wall_set(*cell((int16_t)(off + side)) - 1) == 9 && *cell((int16_t)(off + side + 1)) != 0) {
        if (g_s3d_step_dx == dx && g_s3d_step_dy == dy) {
            g_s3d_cancel_move = 0;
            fr_run_event(here(), 3);
            fr_run_event(there(dx, dy), 1);
            if (g_s3d_cancel_move == 0) {
                switch (*cell((int16_t)(off + side + 1))) {
                case 1:  q3d_anim_dispatch(2); break;
                case 2:  q3d_anim_dispatch(1); break;
                default: o559A_fatal(DGSTR(S3D_FILE), 0x75, 0, 0x44); break;
                }
            }
        }
    } else {
        int16_t fside = (int16_t)(g_s3d_facing % 4) * 2;
        switch (*cell((int16_t)(off + side + 1))) {
        case 0:
            g_s3d_cancel_move = 0;
            fr_run_event(off, 3);
            if (g_s3d_cancel_move != 0)
                break;
            fr_run_event(there(dx, dy), 1);
            if (g_s3d_cancel_move != 0)
                break;
            g_s3d_x += dx;
            g_s3d_y += dy;
            return 1;
        case 2:
            if (g_q3d_wall_set(*cell((int16_t)(off + fside)) - 1) == 8)
                break;
            if (g_s3d_step_dx != dx || g_s3d_step_dy != dy)
                break;
            g_s3d_cancel_move = 0;
            fr_run_event(here(), 3);
            if (g_s3d_cancel_move != 0)
                break;
            fr_run_event(there(g_s3d_step_dx, g_s3d_step_dy), 1);
            if (g_s3d_cancel_move != 0)
                break;
            fside = (int16_t)(g_s3d_facing % 4) * 2;     /* re-read: events may turn */
            q3d_anim_door((int16_t)(*cell((int16_t)(off + fside)) - 1));
            return 1;
        case 3:
            q3d_wait_retrace(3);
            if (g_s3d_step_dx != dx || g_s3d_step_dy != dy)
                break;
            g_s3d_cancel_move = 0;
            fr_run_event(here(), 3);
            if (g_s3d_cancel_move != 0)
                break;
            fr_run_event(there(g_s3d_step_dx, g_s3d_step_dy), 1);
            if (g_s3d_cancel_move != 0)
                break;
            /* 303F:04D1 - 42h -> 41h, sky into 41h, walls (42h) over it, back
               to 42h; then 41h = the sky alone */
            mm_copy_block(mm_ref(0xE4AF), mm_ref(0xE4A7));
            q3d_shade_view((float)(g_s3d_facing % 4), 0x40, 0x41);
            sys_blit(0, 0, (uint16_t)g_q3d_view_w, (uint16_t)g_q3d_view_h, 0, 0, 0x42, 0x41,
                     asm_copy_rect_key);
            mm_copy_block(mm_ref(0xE4A7), mm_ref(0xE4AF));
            sys_fill_slot(0x41, (uint16_t)(g_q3d_view_h * g_q3d_view_w), 0xFE);
            q3d_shade_view((float)(g_s3d_facing % 4), 0x40, 0x41);
            mm_ems_unlock(g_blit_lock);
            /* render 41h without the wall (kind 0), then put it back */
            *cell((int16_t)(here() + side + 1)) = 0;
            q3d_render_view(0x41);
            *cell((int16_t)(here() + side + 1)) = 3;
            s3d_random_view_effect();
            g_s3d_x += g_s3d_step_dx;
            g_s3d_y += g_s3d_step_dy;
            q3d_free_anim_buffer(4);
            sys_fill_slot(0x42, (uint16_t)(g_q3d_view_h * g_q3d_view_w), 0xFE);
            q3d_render_view(0x42);
            return 1;
        default:
            break;
        }
    }
    g_s3d_view_dirty = 0;
    return 0;
}

/*
 * s3d_random_view_effect - flicker between the old view (42h) and the new
 * one (41h) for 1.5 s: every 50 ms one of the two is shown, the old one
 * with a probability that falls with time (random(5 - t/300 s)).
 *
 * Original: IST.EXE 303F:0715 (3DSYS.L), far cdecl.  The wait loop spins
 * on the low word of the clock; the port calls pc_poll() in it.
 * Verified: tests/py/test_s3d_move.py (test_try_move, kind 3 walls).
 */
void s3d_random_view_effect(void)
{
    uint16_t t0 = (uint16_t)g_clock_ms;
    uint16_t last = (uint16_t)g_clock_ms;

    for (;;) {
        pc_poll();                                      /* 303F:07A3 loop test */
        if ((uint16_t)((uint16_t)g_clock_ms - t0) >= 0x5DC)
            break;
        if ((uint16_t)((uint16_t)g_clock_ms - last) > 0x32) {
            int32_t r;
            uint16_t n;
            last = (uint16_t)g_clock_ms;
            r = bc_rand();
            n = (uint16_t)(5 - (uint16_t)((uint16_t)((uint16_t)g_clock_ms - t0) / 300));
            r = (int32_t)(r * (int32_t)n) / 0x8000;      /* random(n), n zero-extended */
            q3d_blit_view(r != 0 ? 0x42 : 0x41, 0x30, 0x34);
        }
    }
}

/*
 * s3d_redraw_view - redraw the 3D view; `how` selects:
 *   0, 7, 8  nothing
 *   1        render the view: sky (40h) shaded into 41h, the walls into
 *            42h, 42h over 41h with colour key, 41h to the screen.  If
 *            byte 5BC2 is set first the target palette is cleared and the
 *            anti-aliasing table (surface 52h, 7F80h bytes) rebuilt.
 *   2 / 3    step forward / back animation
 *   4 / 5    turn right / left animation
 *   6        turn around (facing + 3, rotate left, twice)
 * Nothing at all while the wall textures are not loaded (3D5E bit 20h);
 * how > 8 is fatal.
 *
 * Original: IST.EXE 303F:07B6 (3DSYS.L), far cdecl (unsigned how), switch
 * table at 303F:097F.
 * Verified: tests/py/test_s3d_move.py (test_redraw_view).
 */
void s3d_redraw_view(int16_t how)
{
    if (!(g_q3d_opt_flags & 0x20))
        return;
    switch ((uint16_t)how) {
    case 0: case 7: case 8:
        break;
    case 1:
        if (DG8(0x5BC2) != 0) {
            asm_memset32(gr_target_pal, 0, 0x180);
            mm_ems_lock(g_blit_lock, 0xFE, DGSTR(0x3F23));
            sys_build_aa_table(EMSLock((int16_t)mm_ref(0xE52F)->ems, 0, 0x7F80), 0);
            mm_ems_unlock(g_blit_lock);
            DG8(0x5BC2) = 0;
        }
        sys_fill_slot(0x41, (uint16_t)(g_q3d_view_h * g_q3d_view_w), 0xFE);
        q3d_shade_view((float)(g_s3d_facing % 4), 0x40, 0x41);
        sys_fill_slot(0x42, (uint16_t)(g_q3d_view_h * g_q3d_view_w), 0xFE);
        q3d_render_view(0x42);
        sys_blit(0, 0, (uint16_t)g_q3d_view_w, (uint16_t)g_q3d_view_h, 0, 0, 0x42, 0x41,
                 asm_copy_rect_key);
        q3d_blit_view(0x41, 0x30, 0x34);
        break;
    case 2:
        q3d_anim_step_forward();
        break;
    case 3:
        q3d_anim_step_back();
        break;
    case 4:
        q3d_anim_rotate_right();
        break;
    case 6:
        g_s3d_facing += 3;
        q3d_alloc_view_buffers();
        q3d_anim_rotate_left();
        g_s3d_facing += 3;
        q3d_alloc_view_buffers();
        /* fall through */
    case 5:
        q3d_anim_rotate_left();
        break;
    default:
        o559A_fatal(DGSTR(0x3F32), 0x127, 0, 0x45);
        break;
    }
}

/*
 * s3d_scan_cells - (misnamed: sets the state of the wall in front) the
 * wall the party faces gets kind `value` and its type is normalised to
 * (type % 4) + 1; the same for the opposite wall of the cell in front.
 *
 * Original: IST.EXE 303F:0991 (3DSYS.L), far cdecl (unsigned char value).
 * Starts with mm_ems_unlock(DE26) (no matching lock here).
 * Verified: tests/py/test_s3d_move.py (test_scan_cells).
 */
void s3d_scan_cells(uint8_t value)
{
    int16_t off = here();
    int16_t s;

    mm_ems_unlock(g_blit_lock);
    s = (int16_t)(g_s3d_facing % 4) * 2;
    *cell((int16_t)(off + s + 1)) = value;
    *cell((int16_t)(off + s)) = (uint8_t)(*cell((int16_t)(off + s)) % 4);
    (*cell((int16_t)(off + s)))++;
    off = (int16_t)(off + (int16_t)((int16_t)(g_s3d_step_dx + (int16_t)(g_s3d_step_dy * g_s3d_map_w)) * 15));
    s = (int16_t)((g_s3d_facing + 2) % 4) * 2;
    *cell((int16_t)(off + s + 1)) = value;
    *cell((int16_t)(off + s)) = (uint8_t)(*cell((int16_t)(off + s)) % 4);
    (*cell((int16_t)(off + s)))++;
}
