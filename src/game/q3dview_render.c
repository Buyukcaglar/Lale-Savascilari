/*
 * q3dview_render.c - 3DQUICK.L part 2 (IST.EXE ovl2700:3932-5F31): composing
 * the first-person view.  See q3dview.h.
 *
 * The view is drawn from far to near in five layers.  Each layer walks the
 * row of map cells at one distance in front of the party (i = lateral
 * position, negative = left), draws the floor and ceiling piece of every
 * cell (surfaces 23h + floor byte + 5k, 37h + ceiling byte + 2k, k = a
 * checkerboard/facing variant) and then the walls of the cell through
 * q3d_draw_cell_wall.  Pieces 27..32 of g_q3d_pieces are the floor/ceiling
 * pieces of the distances 3..0; the side cells are drawn with the slanted
 * strip blitters (sys_blit_0F21).
 *
 * Map coordinates: forward = (step_dx, step_dy); the cell at distance d and
 * lateral i is (x + d*dx - i*dy, y + d*dy + i*dx), and its byte offset
 * relative to the centre cell of the row is ((i*dx)*map_w - i*dy) * 15.
 * All arithmetic is 16 bit (int16 wrap), as in the original.
 */
#include "game/q3dview.h"

#include <stdio.h>
#include <stdlib.h>

#include "engine/blit386.h"
#include "engine/memman.h"
#include "engine/systems.h"
#include "platform/pc.h"

/* map byte at the sign-extended 16-bit offset (F_PADD) */
static uint8_t map_at(int16_t off)
{
    return s3d_map[(int32_t)off];
}

static int in_map(int16_t x, int16_t y)
{
    return x >= 0 && x < g_s3d_map_w && y >= 0 && y < g_s3d_map_h;
}

/* offset of lateral cell i from the row's centre cell */
static int16_t lat_off(int16_t i)
{
    return (int16_t)(((int16_t)(i * g_s3d_step_dx) * g_s3d_map_w + (int16_t)(-i * g_s3d_step_dy)) * 15);
}

static int16_t lat_x(int16_t d, int16_t i)
{
    return (int16_t)(g_s3d_x + d * g_s3d_step_dx - i * g_s3d_step_dy);
}

static int16_t lat_y(int16_t d, int16_t i)
{
    return (int16_t)(g_s3d_y + d * g_s3d_step_dy + i * g_s3d_step_dx);
}

/* the strip blits' slope byte: (|i| - 1) * 2 + 1, 8 bit */
static uint8_t slope_of(int16_t i)
{
    return (uint8_t)(((uint8_t)abs(i) - 1) * 2 + 1);
}

static uint8_t floor_slot(int16_t cell, uint8_t k)
{
    return (uint8_t)(map_at((int16_t)(cell + 9)) + k * 5 + 0x23);
}

static uint8_t ceil_slot(int16_t cell, uint8_t k)
{
    return (uint8_t)(map_at((int16_t)(cell + 8)) + (uint8_t)(k << 1) + 0x37);
}

static uint8_t side_l(void) { return (uint8_t)((g_s3d_facing + 3) % 4); }
static uint8_t side_r(void) { return (uint8_t)((g_s3d_facing + 1) % 4); }
static uint8_t side_f(void) { return (uint8_t)(g_s3d_facing % 4); }

/* 2700:3932 */
void q3d_render_view(uint8_t slot)
{
    q3d_render_layer_far(slot);
    q3d_render_layer_mid(slot);
    q3d_render_layer_near(slot);
    q3d_render_layer_side(slot);
    q3d_render_layer_front(slot);
}

/* 2700:3969 - the wall on side 'side' of the cell at cell_off as wall piece
   'piece'.  Map bytes [2*side] = wall type (1-based wall set), [2*side+1] =
   kind: 1 wall (set slot), 2 door (slot + 4), 3 = wall drawn whole (strip
   a, piece 23 mirrored).  Walls facing the viewer on sides 2/3 borrow the
   texture rectangle of the opposite piece and are mirrored. */
void q3d_draw_cell_wall(int16_t piece, int16_t cell_off, uint8_t side, uint8_t dst_slot)
{
    int8_t   kind = (int8_t)map_at((int16_t)(side * 2 + cell_off + 1));
    int16_t  slot;
    Q3dPiece *tab;
    uint8_t  flags;

    if (kind == 0)
        return;
    slot = (int16_t)(map_at((int16_t)(side * 2 + cell_off)) - 1);
    /* slot -1 reads 9F0E, which nothing writes (0): always the message */
    if (slot >= 4 || g_q3d_wall_set(slot) == 0) {
        sprintf(g_q3d_msg, DGSTR(0x3CD3), cell_off);   /* "Beklenmedik duvar!!!!; Harita offset: %d" */
        return;
    }
    sprintf(g_q3d_msg, DGSTR(0x3CFC), piece);          /* "OrspUm[%d]   " */
    switch ((uint8_t)kind) {
    case 2:
        slot += 4;
        /* fall through */
    case 1:
        if (g_q3d_kor[slot].hdr0 == 0) {
            q3d_draw_strip_a(piece, (uint8_t)(slot + 0x18), dst_slot, q3d_kor_pieces[slot]);
            return;
        }
        tab = q3d_kor_pieces[slot];
        if (side > 1 && g_s3d_facing == side) {
            int16_t tx = tab[piece].tx, ty = tab[piece].ty, m;
            flags = (uint8_t)tab[piece].flags;
            switch (piece) {
            case 0x0D: m = 0x11; break;
            case 0x11: m = 0x0D; break;
            case 0x16: m = 0x18; break;
            case 0x18: m = 0x16; break;
            default:   m = piece; break;
            }
            tab[piece].tx = tab[m].tx;
            tab[piece].ty = tab[m].ty;
            tab[piece].flags |= 1;
            q3d_draw_strip_b(piece, (uint8_t)(slot + 0x18), dst_slot, tab);
            tab[piece].tx = tx;
            tab[piece].ty = ty;
            tab[piece].flags = flags;
            return;
        }
        q3d_draw_strip_b(piece, (uint8_t)(slot + 0x18), dst_slot, tab);
        return;
    case 3:
        tab = q3d_kor_pieces[slot];
        flags = (uint8_t)tab[piece].flags;
        if (piece == 0x17)
            tab[piece].flags |= 1;
        q3d_draw_strip_a(piece, (uint8_t)(slot + 0x18), dst_slot, tab);
        tab[piece].flags = flags;
        return;
    default:
        return;
    }
}

/* 2700:3CDD - distance 3: the 2-pixel edge columns of the cells two to the
   side, the horizon picture (surface 4Fh) when the row reaches outside the
   map, then the floor/ceiling of lateral cells -3..3 and the side walls of
   -2..2 (order -3 -2 -1 0 3 2 1). */
void q3d_render_layer_far(uint8_t slot)
{
    const Q3dPiece *p = g_q3d_pieces;
    int16_t cell = (int16_t)(((int16_t)(lat_y(3, 0) * g_s3d_map_w) + lat_x(3, 0)) * 15);
    int16_t off, i;
    uint8_t hb = 0;

    off = (int16_t)(((int16_t)(g_s3d_step_dx * g_s3d_map_w) * -2 + g_s3d_step_dy * 2) * 15);
    if (in_map(lat_x(3, -2), lat_y(3, -2))) {
        sys_blit(0, (uint16_t)p[27].ty, 2, (uint16_t)(p[0].h / 2), 0,
                 (uint16_t)(p[27].y - p[0].h / 2), floor_slot((int16_t)(cell + off), 0), slot,
                 asm_copy_rect_key);
        sys_blit(0, (uint16_t)(p[30].ty - p[0].h / 2), 2, (uint16_t)(p[0].h / 2), 0,
                 (uint16_t)p[30].y, ceil_slot((int16_t)(cell + off), 0), slot, asm_copy_rect_key);
    }
    off = (int16_t)(((int16_t)(g_s3d_step_dx * 2 * g_s3d_map_w) + g_s3d_step_dy * -2) * 15);
    if (in_map(lat_x(3, 2), lat_y(3, 2))) {
        sys_blit(0, (uint16_t)p[27].ty, 2, (uint16_t)(p[0].h / 2), (uint16_t)(g_q3d_view_w - 2),
                 (uint16_t)(p[27].y - p[0].h / 2), floor_slot((int16_t)(cell + off), 0), slot,
                 asm_copy_rect_key);
        sys_blit(0, (uint16_t)(p[30].ty - p[0].h / 2), 2, (uint16_t)(p[0].h / 2),
                 (uint16_t)(g_q3d_view_w - 2), (uint16_t)p[30].y, ceil_slot((int16_t)(cell + off), 0),
                 slot, asm_copy_rect_key);
    }

    if (g_surf_mm_off(Q3D_SURF_HORIZON) | g_surf_mm_seg(Q3D_SURF_HORIZON)) {
        if (!in_map(lat_x(2, 0), lat_y(2, 0)))
            hb = (uint8_t)g_surf_h(Q3D_SURF_HORIZON);
        else if (g_q3d_view_h / 2 + 1 > g_surf_h(Q3D_SURF_HORIZON))
            hb = (uint8_t)g_surf_h(Q3D_SURF_HORIZON);
        else
            hb = (uint8_t)(g_q3d_view_h / 2 + 1);
        if (!in_map(lat_x(3, -3), lat_y(3, -3)) || !in_map(lat_x(3, 3), lat_y(3, 3)))
            sys_blit(0, (uint16_t)(g_surf_h(Q3D_SURF_HORIZON) - hb), (uint16_t)g_q3d_view_w, hb, 0,
                     (uint16_t)(g_q3d_view_h - hb), Q3D_SURF_HORIZON, slot, asm_copy_rect_key);
    }

    for (i = -3; i < 4; i++) {
        if (i > 0)
            i = (int16_t)(4 - i);
        off = lat_off(i);
        if (in_map(lat_x(3, i), lat_y(3, i))) {
            int16_t c = (int16_t)(cell + off);
            sys_blit(0, (uint16_t)p[27].ty, (uint16_t)p[0].w, (uint16_t)(p[0].h / 2 + 1),
                     (uint16_t)p[i + 3].x, (uint16_t)(p[27].y - p[0].h / 2), floor_slot(c, 0), slot,
                     asm_copy_rect_key);
            if (map_at((int16_t)(c + 8)) != 0)
                sys_blit(0, (uint16_t)(p[30].ty - p[0].h / 2), (uint16_t)p[0].w, (uint16_t)(p[0].h / 2),
                         (uint16_t)p[i + 3].x, (uint16_t)p[30].y, ceil_slot(c, 0), slot,
                         asm_copy_rect_key);
            switch (i) {
            case -2: q3d_draw_cell_wall(0x21, c, side_l(), slot); break;
            case -1: q3d_draw_cell_wall(0x22, c, side_l(), slot); break;
            case 1:  q3d_draw_cell_wall(0x23, c, side_r(), slot); break;
            case 2:  q3d_draw_cell_wall(0x24, c, side_r(), slot); break;
            default: break;
            }
        }
        if (i > 0)
            i = (int16_t)(4 - i);
    }
}

/* floor + ceiling strips of a side cell at lateral i (pieces fl/ce) */
static void side_strips(const Q3dPiece *fl, const Q3dPiece *ce, int16_t i, int16_t c, uint8_t k,
                        uint8_t slot, int right)
{
    uint16_t limit = (uint16_t)(right ? g_q3d_view_ox + g_q3d_view_w : g_q3d_view_ox);

    sys_blit_0F21((uint16_t)fl->tx, (uint16_t)fl->ty, (uint16_t)fl->tw, (uint16_t)fl->th,
                  (uint16_t)(fl->x + i * fl->tw), (uint16_t)fl->y, limit, slope_of(i),
                  floor_slot(c, k), slot, right ? sys_line_10F1 : sys_line_1185);
    if (map_at((int16_t)(c + 8)) != 0)
        sys_blit_0F21((uint16_t)ce->tx, (uint16_t)(ce->ty - ce->th), (uint16_t)ce->tw,
                      (uint16_t)ce->th, (uint16_t)(ce->x + i * ce->tw), (uint16_t)(ce->y - ce->th),
                      limit, slope_of(i), ceil_slot(c, k), slot,
                      right ? sys_line_1261 : sys_line_1307);
}

/* the walls of the centre cell: bit set in g_q3d_wall_bits when the side's
   wall kind byte is non-zero */
static void wall_bit(int16_t cell, uint8_t side, uint16_t bit)
{
    if (map_at((int16_t)(cell + side * 2 + 1)) != 0)
        g_q3d_wall_bits |= bit;
    else
        g_q3d_wall_bits &= (uint16_t)~bit;
}

/* 2700:43C9 - distance 2 */
void q3d_render_layer_mid(uint8_t slot)
{
    const Q3dPiece *p = g_q3d_pieces;
    uint8_t k = (uint8_t)((g_s3d_facing & 1) | (uint8_t)((((uint8_t)g_s3d_x + (uint8_t)g_s3d_y) & 1) << 1));
    int16_t cell = (int16_t)(((int16_t)(lat_y(2, 0) * g_s3d_map_w) + lat_x(2, 0)) * 15);
    int16_t i, c;

    for (i = -3; i < 0; i++) {
        c = (int16_t)(cell + lat_off(i));
        if (!in_map(lat_x(2, i), lat_y(2, i)))
            continue;
        side_strips(&p[27], &p[30], i, c, k, slot, 0);
        switch (i) {
        case -3:
            q3d_draw_cell_wall(0, c, side_f(), slot);
            break;
        case -2:
            q3d_draw_cell_wall(7, c, side_l(), slot);
            q3d_draw_cell_wall(1, c, side_f(), slot);
            break;
        case -1:
            q3d_draw_cell_wall(8, c, side_l(), slot);
            q3d_draw_cell_wall(2, c, side_f(), slot);
            break;
        }
    }
    for (i = 3; i > 0; i--) {
        c = (int16_t)(cell + lat_off(i));
        if (!in_map(lat_x(2, i), lat_y(2, i)))
            continue;
        side_strips(&p[27], &p[30], i, c, k, slot, 1);
        switch (i) {
        case 3:
            q3d_draw_cell_wall(6, c, side_f(), slot);
            break;
        case 2:
            q3d_draw_cell_wall(0x0C, c, side_r(), slot);
            q3d_draw_cell_wall(5, c, side_f(), slot);
            break;
        case 1:
            q3d_draw_cell_wall(0x0B, c, side_r(), slot);
            q3d_draw_cell_wall(4, c, side_f(), slot);
            break;
        }
    }
    if (!in_map(lat_x(2, 0), lat_y(2, 0)))
        return;
    sys_blit((uint16_t)(p[27].tx - p[27].tw), (uint16_t)p[27].ty, (uint16_t)(p[27].tw * 3),
             (uint16_t)p[27].th, (uint16_t)(p[27].x - p[27].tw), (uint16_t)p[27].y,
             floor_slot(cell, k), slot, asm_copy_rect_key);
    if (map_at((int16_t)(cell + 8)) != 0)
        sys_blit((uint16_t)(p[30].tx - p[30].tw), (uint16_t)(p[30].ty - p[30].th),
                 (uint16_t)(p[30].tw * 3), (uint16_t)p[30].th, (uint16_t)(p[30].x - p[30].tw),
                 (uint16_t)(p[30].y - p[30].th), ceil_slot(cell, k), slot, asm_copy_rect_key);
    q3d_draw_cell_wall(3, cell, side_f(), slot);
    q3d_draw_cell_wall(9, cell, side_l(), slot);
    q3d_draw_cell_wall(0x0A, cell, side_r(), slot);
    wall_bit(cell, side_l(), 1);
    wall_bit(cell, side_r(), 2);
}

/* 2700:4F55-56DF - distance 1 (the listing splits it at 4F9A and 51E9) */
void q3d_render_layer_near(uint8_t slot)
{
    const Q3dPiece *p = g_q3d_pieces;
    uint8_t k = (uint8_t)((g_s3d_facing & 1) | (uint8_t)((((uint8_t)g_s3d_x + (uint8_t)g_s3d_y) & 1) << 1));
    int16_t cell = (int16_t)(((int16_t)(lat_y(1, 0) * g_s3d_map_w) + lat_x(1, 0)) * 15);
    int16_t i, c;

    for (i = -2; i < 0; i++) {
        c = (int16_t)(cell + lat_off(i));
        if (!in_map(lat_x(1, i), lat_y(1, i)))
            continue;
        side_strips(&p[28], &p[31], i, c, k, slot, 0);
        q3d_draw_cell_wall((int16_t)(i + 0x0F), c, side_f(), slot);
    }
    for (i = 2; i > 0; i--) {
        c = (int16_t)(cell + lat_off(i));
        if (!in_map(lat_x(1, i), lat_y(1, i)))
            continue;
        side_strips(&p[28], &p[31], i, c, k, slot, 1);
        q3d_draw_cell_wall((int16_t)(i + 0x0F), c, side_f(), slot);
    }
    if (!in_map(lat_x(1, 0), lat_y(1, 0)))
        return;
    sys_blit((uint16_t)(p[28].tx - p[28].tw), (uint16_t)p[28].ty, (uint16_t)(p[28].tw * 3),
             (uint16_t)p[28].th, (uint16_t)(p[28].x - p[28].tw), (uint16_t)p[28].y,
             floor_slot(cell, k), slot, asm_copy_rect_key);
    if (map_at((int16_t)(cell + 8)) != 0)
        sys_blit((uint16_t)(p[31].tx - p[31].tw), (uint16_t)(p[31].ty - p[31].th + 1),
                 (uint16_t)(p[31].tw * 3), (uint16_t)p[31].th, (uint16_t)(p[31].x - p[31].tw),
                 (uint16_t)(p[31].y - p[31].th + 1), ceil_slot(cell, k), slot, asm_copy_rect_key);
    q3d_draw_cell_wall(0x0F, cell, side_f(), slot);
}

/* 2700:56E0 - the side walls of the cells beside the one in front */
void q3d_render_layer_side(uint8_t slot)
{
    int16_t cell = (int16_t)(((int16_t)(lat_y(1, 0) * g_s3d_map_w) + lat_x(1, 0)) * 15);
    int16_t i, c;

    for (i = -1; i < 2; i += 2) {
        c = (int16_t)(cell + lat_off(i));
        if (!in_map(lat_x(1, i), lat_y(1, i)))
            continue;
        if (i == -1)
            q3d_draw_cell_wall(0x12, c, side_l(), slot);
        else if (i == 1)
            q3d_draw_cell_wall(0x15, c, side_r(), slot);
    }
    if (!in_map(lat_x(1, 0), lat_y(1, 0)))
        return;
    q3d_draw_cell_wall(0x14, cell, side_r(), slot);
    q3d_draw_cell_wall(0x13, cell, side_l(), slot);
    wall_bit(cell, side_l(), 4);
    wall_bit(cell, side_r(), 8);
}

/* 2700:58E2 - distance 0: the party's own row */
void q3d_render_layer_front(uint8_t slot)
{
    const Q3dPiece *p = g_q3d_pieces;
    uint8_t k = (uint8_t)((uint8_t)(g_s3d_facing % 2) |
                          (uint8_t)((uint8_t)((int16_t)(g_s3d_x + g_s3d_y) % 2) << 1));
    int16_t cell = (int16_t)(((int16_t)(g_s3d_y * g_s3d_map_w) + g_s3d_x) * 15);
    int16_t i, c;

    for (i = -1; i < 2; i += 2) {
        c = (int16_t)(cell + lat_off(i));
        if (!in_map(lat_x(0, i), lat_y(0, i)))
            continue;
        if (i == -1) {
            side_strips(&p[29], &p[32], i, c, k, slot, 0);
            q3d_draw_cell_wall(0x16, c, side_f(), slot);
        } else if (i == 1) {
            side_strips(&p[29], &p[32], i, c, k, slot, 1);
            q3d_draw_cell_wall(0x18, c, side_f(), slot);
        }
    }
    if (!in_map(g_s3d_x, g_s3d_y))
        return;
    sys_blit((uint16_t)(p[29].tx - p[29].th), (uint16_t)p[29].ty,
             (uint16_t)(p[29].tw + p[29].th * 2), (uint16_t)p[29].th,
             (uint16_t)(p[29].x - p[29].th), (uint16_t)p[29].y, floor_slot(cell, k), slot,
             asm_copy_rect_key);
    if (map_at((int16_t)(cell + 8)) != 0) {
        g_q3d_ceiling_here = 1;
        sys_blit((uint16_t)(p[32].tx - p[32].th), (uint16_t)(p[32].ty - p[32].th),
                 (uint16_t)(p[32].tw + p[32].th * 2), (uint16_t)(p[32].th + 1), 0, 0,
                 ceil_slot(cell, k), slot, asm_copy_rect_key);
    } else {
        g_q3d_ceiling_here = 0;
    }
    q3d_draw_cell_wall(0x17, cell, side_f(), slot);
    q3d_draw_cell_wall(0x1A, cell, side_r(), slot);
    q3d_draw_cell_wall(0x19, cell, side_l(), slot);
    wall_bit(cell, side_l(), 0x10);
    wall_bit(cell, side_r(), 0x20);
    wall_bit(cell, side_f(), 0x40);
}

/* 2700:5E7F - unreferenced test: the left near floor/ceiling strips of
   surfaces 24h/38h straight onto the screen (slot 0), then waits for a key. */
void q3d_debug_dump(void)
{
    const Q3dPiece *p = g_q3d_pieces;
    int16_t i;

    for (i = -3; i < 0; i++) {
        sys_blit_0F21((uint16_t)p[28].tx, (uint16_t)p[28].ty, (uint16_t)p[28].tw, (uint16_t)p[28].th,
                      (uint16_t)(p[28].x + i * p[28].tw), (uint16_t)p[28].y, 0, slope_of(i), 0x24, 0,
                      sys_line_1185);
        sys_blit_0F21((uint16_t)p[31].tx, (uint16_t)(p[31].ty - p[31].th), (uint16_t)p[31].tw,
                      (uint16_t)p[31].th, (uint16_t)(p[31].x + i * p[31].tw),
                      (uint16_t)(p[31].y - p[31].th), 0, slope_of(i), 0x38, 0, sys_line_1307);
    }
    pc_getch();
}
