/*
 * iso_map.c - ISOSYS.L 4595:0D1B-16EA: the mini map, line of sight and the
 * two line-walking animations (missile flight, view scroll).  See iso.h.
 *
 * Lines.  The three walkers below share one algorithm (written out three
 * times in the original): from (x0, y0) to (x1, y1), dx = x1-x0, dy = y1-y0.
 * If |dx| >= |dy|: slope = dy*100/dx, s = sign(dx), a = dx, b = dy*100;
 * cells (x0 + a, y0 + b/100) while a != 0, then a -= s, b -= slope*s.
 * Otherwise the same with the roles swapped (a = dx*100, b = dy).  So the
 * walk starts at the far end (x1, y1) and stops before (x0, y0).
 */
#include "game/iso.h"

#include <stddef.h>

#include "dsmi/emsheap.h"
#include "engine/intrface.h"
#include "engine/memman.h"
#include "engine/systems.h"

/* one cell of a walk; return nonzero to stop */
typedef int (*IsoVisit)(int16_t x, int16_t y, void *ctx);

static void walk_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, IsoVisit visit, void *ctx)
{
    int16_t dx = (int16_t)(x1 - x0), dy = (int16_t)(y1 - y0);
    int16_t slope, s, a, b;
    if ((dx < 0 ? -dx : dx) >= (dy < 0 ? -dy : dy)) {
        slope = (int16_t)((int16_t)(dy * 100) / dx);
        s = dx > 0 ? 1 : -1;
        a = dx;
        b = (int16_t)(dy * 100);
        while (a != 0) {
            if (visit((int16_t)(x0 + a), (int16_t)(b / 100 + y0), ctx))
                return;
            a = (int16_t)(a - s);
            b = (int16_t)(b - slope * s);
        }
    } else {
        slope = (int16_t)((int16_t)(dx * 100) / dy);
        s = dy > 0 ? 1 : -1;
        a = (int16_t)(dx * 100);
        b = dy;
        while (b != 0) {
            if (visit((int16_t)(a / 100 + x0), (int16_t)(b + y0), ctx))
                return;
            a = (int16_t)(a - slope * s);
            b = (int16_t)(b - s);
        }
    }
}

static int is_wall(int16_t x, int16_t y)
{
    return g_iso_terrain(x, y) >= 10 && g_iso_terrain(x, y) <= 13;
}

/* view origin on cell (x, y): clamp(v - 3, 0, 20) */
static void view_on(int16_t x, int16_t y)
{
    int16_t v = (int16_t)(x - 3);
    g_iso_view_x = v < 0 ? 0 : (v < 0x14 ? v : 0x14);
    v = (int16_t)(y - 3);
    g_iso_view_y = v < 0 ? 0 : (v < 0x14 ? v : 0x14);
}

/*
 * iso_draw_map - the 27x27 mini map into the work screen (slot 6), as a
 * slanted block at (5 + x + y, 126 + y): walls/doors (terrain 10..13)
 * in their colour (261C + terrain), terrain 6 in colour 9669, other cells
 * 0; living units in 9664 (enemies) / 966A (others), the actor in 9665,
 * the target cursor (if D57C) in 966B.  Every pixel maps the screen block
 * anew (EMSLock, no MEMMAN lock).
 *
 * Original: IST.EXE ovl4595:0D1B (ISOSYS.L), far cdecl.
 * Verified: tests/py/test_iso.py, 2026-11-21.
 */
void iso_draw_map(void)
{
    int16_t x, y, c, row;
    uint16_t col;
    for (y = 0, row = 0; y < 0x1B; y++, row++)
        for (x = 0; x < 0x1B; x++) {
            if (is_wall(x, y))
                g_iso_minimap(x, y) = g_iso_terrain_col(g_iso_terrain(x, y));
            else
                g_iso_minimap(x, y) = 0;
            if (g_iso_terrain(x, y) == 6)
                g_iso_minimap(x, y) = DG8(0x9669);
        }
    for (c = 1; c <= ISO_UNITS; c++)
        if (iso_unit(c)->hp > 0) {
            col = iso_unit(c)->side == 2 ? DG8(0x9664) : DG8(0x966A);
            if (c == g_iso_actor)
                col = DG8(0x9665);
            g_iso_minimap(iso_unit(c)->x, iso_unit(c)->y) = (int16_t)col;
        }
    if (g_iso_cursor_on != 0)
        g_iso_minimap(g_iso_cursor_x, g_iso_cursor_y) = DG8(0x966B);
    for (y = 0, row = 0; y < 0x1B; y++, row++)
        for (x = 0; x < 0x1B; x++) {
            MmHandle *scr = mm_ref(G_ISO_SCREEN_REF);
            uint8_t *buf;
            if (scr->type == 0)
                buf = mm_conv_ptr(scr);
            else
                buf = EMSLock((int16_t)scr->ems, 0,
                              (uint16_t)(scr->size >> 16 ? 0xFFFF : scr->size));
            sys_putpixel((uint16_t)(x + row + 5), (uint16_t)(y + 0x7E),
                         (uint8_t)g_iso_minimap(x, y), buf, (uint16_t)g_surf_w(6));
        }
}

static int trace_visit(int16_t x, int16_t y, void *ctx)
{
    (void)ctx;
    g_iso_los_dist++;
    if (is_wall(x, y)) {
        g_iso_los_clear = 0;
        return 1;
    }
    return 0;
}

/*
 * iso_trace_line - line of sight from unit c to cell (x, y): D560 = 1 if
 * no wall (terrain 10..13) lies on the line (the target cell included,
 * c's own cell not) and the target's terrain is not 0, D564 = 1 + cells
 * walked (up to and including a blocking wall).  Same cell: 1 / 1.
 *
 * Original: IST.EXE ovl4595:0F86 (ISOSYS.L), far cdecl.
 * Verified: tests/py/test_iso.py (random maps and lines), 2026-11-21.
 */
void iso_trace_line(int16_t c, int16_t x, int16_t y)
{
    int16_t x0 = iso_unit(c)->x, y0 = iso_unit(c)->y;
    g_iso_los_clear = 1;
    g_iso_los_dist = 1;
    if (x0 == x && y0 == y)
        return;
    walk_line(x0, y0, x, y, trace_visit, NULL);
    if (g_iso_terrain(x, y) == 0)
        g_iso_los_clear = 0;
}

typedef struct {
    int16_t icon;
    int16_t ox, oy;
    uint8_t flip;
} Missile;

static int missile_visit(int16_t x, int16_t y, void *ctx)
{
    Missile *m = ctx;
    if (is_wall(x, y))
        return 1;
    view_on(x, y);
    iso_redraw();
    switch (iso_unit(g_iso_attacker)->facing) {    /* jump table 4595:14E2 */
    case 5: m->ox = 0; m->oy = -10; break;
    case 6: m->ox = -10; m->oy = 0; break;
    case 7: m->ox = 0; m->oy = 10; break;
    case 8: m->ox = 10; m->oy = 0; break;
    default: break;
    }
    m->flip ^= 1;
    sys_blit_scaled((uint16_t)((m->icon - 1) * 0x0F), 0, 0x0F, 0x0F,
                    (uint16_t)((x - g_iso_view_x) * 0x1E + (y - g_iso_view_y) * 0x0E + m->ox + 0x14),
                    (uint16_t)((y - g_iso_view_y) * 0x0E + m->oy + 0x0D), 8, 8, 0x4B, 0,
                    m->flip ? 1 : 0);
    return 0;
}

/*
 * iso_scroll_view_dir - the flight of a thrown/shot missile between two
 * cells: for every cell of the line (from (x1, y1) back towards (x0, y0),
 * see above) centre the view there, redraw the battle screen and put the
 * missile picture `icon` (15x15 from slot 4Bh, scaled to 8x8, mirrored on
 * every other step) straight onto the display; stops at a wall.  The
 * offset of the picture comes from the attacker's facing 5..8.
 *
 * Original: IST.EXE ovl4595:1139 (ISOSYS.L), far cdecl (x0, y0, x1, y1,
 * icon).  The name in names.tsv predates this reading.
 * Verified: tests/py/test_iso.py, 2026-11-21.
 */
void iso_scroll_view_dir(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t icon)
{
    Missile m = { icon, 0, 0, 0 };
    if (x0 == x1 && y0 == y1)
        return;
    walk_line(x0, y0, x1, y1, missile_visit, &m);
}

static int scroll_visit(int16_t x, int16_t y, void *ctx)
{
    (void)ctx;
    view_on(x, y);
    iso_redraw();
    ui_wait_frames(2);
    return 0;
}

/*
 * iso_scroll_view_to - scroll the view along the line between two cells
 * (from (x1, y1) back towards (x0, y0)), redrawing and waiting 2 frames
 * per cell.
 *
 * Original: IST.EXE ovl4595:14EA (ISOSYS.L), far cdecl (x0, y0, x1, y1).
 * Verified: tests/py/test_iso.py, 2026-11-21.
 */
void iso_scroll_view_to(int16_t x0, int16_t y0, int16_t x1, int16_t y1)
{
    if (x0 == x1 && y0 == y1)
        return;
    walk_line(x0, y0, x1, y1, scroll_visit, NULL);
}
