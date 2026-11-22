/*
 * iso_field.c - ISOSYS.L 4595:568C-6344: the battlefield, built from the
 * 3D map around the party.  See iso.h.
 */
#include "game/iso.h"

#include "engine/memman.h"
#include "game/q3dview.h"
#include "rtl/bc_rand.h"

/* the 3x3 map cells around the party -> 3x3 blocks of 9x9 battle cells */
#define g_iso_field_dx       DGS16(0xD554)  /* i16 party cell - left map cell of the 3x3 */
#define g_iso_field_dy       DGS16(0xD552)
/* the map cell being converted: wall type (byte 2k - 1, 1-based KOR slot)
   and wall kind (byte 2k+1) of side k = 0 north, 1 east, 2 south, 3 west */
#define g_iso_wall_t(k)      DGS16((uint16_t)(0xD53E - 2 * (k)))
#define g_iso_wall_k(k)      DGS16((uint16_t)(0xD546 - 2 * (k)))
/* picture number of wall type t (1..9; 8 = a hedge / water: blocks a whole
   block, 9 = open), set by iso_battle from the 3D wall set */
#define WALL_PIC(t)          DGS16((uint16_t)(0x9F10 + 2 * (t)))

/* terrain word at a raw grid position; x or y may be -1 (the original
   writes there through the same address arithmetic) */
#define TER(x, y)            g_iso_terrain(x, y)

/* one side of block (i, j): returns the "open" flag (1 = no wall: the
   decoration may reach one cell further that way)
     side 0: north line  y = 9i - 1,   whole block (i - 1) when i == 1
     side 1: east line   x = 9j + 8,   block (j + 1) when j == 1
     side 2: south line  y = 9i + 8,   block (i + 1) when i == 1
     side 3: west line   x = 9j - 1,   block (j - 1) when j == 1
   A wall type with picture 8 on a border block draws the line as 6
   (impassable, invisible); on the middle block it fills the neighbouring
   block with 6.  Other pictures < 9: the line is wall t + 10.  Kind 2 with
   picture 8: the line is door picture `door` + 9. */
static int16_t side_wall(int16_t side, int16_t i, int16_t j, int16_t door)
{
    int16_t t = g_iso_wall_t(side), kind = g_iso_wall_k(side), a, b;
    int16_t edge_blk = side == 0 ? i : side == 1 ? j : side == 2 ? i : j;
    int16_t border = side == 0 || side == 3 ? 0 : 2;

    if ((kind == 1 || kind == 3) && WALL_PIC(t) < 9) {
        for (a = 0; a < 9; a++) {
            if (WALL_PIC(t) == 8 && edge_blk == border) {
                switch (side) {
                case 0: TER(j * 9 + a, i * 9 - 1) = 6; break;
                case 1: TER(j * 9 + 8, i * 9 + a) = 6; break;
                case 2: TER(j * 9 + a, i * 9 + 8) = 6; break;
                default: TER(j * 9 - 1, i * 9 + a) = 6; break;
                }
            } else if (WALL_PIC(t) == 8 && edge_blk == 1) {
                for (b = 0; b < 9; b++)
                    switch (side) {
                    case 0: TER(j * 9 + a, (i - 1) * 9 + b) = 6; break;
                    case 1: TER((j + 1) * 9 + a, i * 9 + b) = 6; break;
                    case 2: TER(j * 9 + a, (i + 1) * 9 + b) = 6; break;
                    default: TER((j - 1) * 9 + a, i * 9 + b) = 6; break;
                    }
            } else if (WALL_PIC(t) != 8) {
                int16_t v = (int16_t)(t + 10);
                switch (side) {
                case 0: TER(j * 9 + a, i * 9 - 1) = v; break;
                case 1: TER(j * 9 + 8, i * 9 + a) = v; break;
                case 2: TER(j * 9 + a, i * 9 + 8) = v; break;
                default: TER(j * 9 - 1, i * 9 + a) = v; break;
                }
            }
        }
        return 0;
    }
    if (kind == 2 && WALL_PIC(t) == 8) {
        for (a = 0; a < 9; a++)
            switch (side) {
            case 0: TER(j * 9 + a, i * 9 - 1) = (int16_t)(door + 9); break;
            case 1: TER(j * 9 + 8, i * 9 + a) = (int16_t)(door + 9); break;
            case 2: TER(j * 9 + a, i * 9 + 8) = (int16_t)(door + 9); break;
            default: TER(j * 9 - 1, i * 9 + a) = (int16_t)(door + 9); break;
            }
        return 0;
    }
    return 1;
}

/*
 * iso_build_battlefield - the 27x27 battle terrain from the 3x3 map cells
 * around the party (left/top cell max(0, min(size - 3, pos - 1)), which
 * also gives D554/D552 = the party's cell in the 3x3).  Every map cell
 * (15 bytes, q3dview.h) becomes a 9x9 block:
 *  - floor byte 9 everywhere (unless the block's corner is already 6, set
 *    by a neighbour);
 *  - its four walls, in the order north, east, south, west (side_wall);
 *    after the west wall, when the north wall is a wall (kind 1/3, picture
 *    < 9) the corner cell (9j - 1, 9i - 1) becomes north wall + 10 unless
 *    it is >= 10 already;
 *  - decoration on every 5th cell from (9i + 1, 9j + 1) (one cell less
 *    on a side without wall), cells that are 6 are left alone; r =
 *    random(101):
 *      floor 1: r < 70 -> 1, < 90 -> 7, else random(2) == 0 with walls
 *               (non-zero, not 6) north and west of the cell -> 0, else 6
 *               when both are non-zero;
 *      floor 2, 3: r < 2 with both neighbours non-zero -> 0;
 *      floor 4: as 1 with 4 / 9 / 8 (and 8 instead of 6 in the test).
 * The door picture is that of the first wall type (9F10..) with picture
 * 3, or else the last with picture 2, else 5 (+9).
 *
 * Original: IST.EXE ovl4595:568C (ISOSYS.L), far cdecl.  The original
 * clears only 27x27 of the 28x28 grid, writes the outer wall lines at
 * y = -1 / x = -1 through the grid's address arithmetic (the end of the
 * previous column / the word before the grid), and unlocks the map's EMS
 * mapping (BB3B) before reading the map through its far pointer.
 * Verified: tests/py/test_iso_b.py (every district map, every cell, random
 * wall pictures), 2026-11-21.
 */
void iso_build_battlefield(void)
{
    int16_t x, y, i, j, ox, oy, base, cell, floor_t, open_n, open_e, open_s, open_w, r, t;
    uint8_t door;

    for (x = 0; x < 0x1B; x++)
        for (y = 0; y < 0x1B; y++)
            g_iso_terrain(x, y) = 0;
    /* door 5 = no door picture among the map's wall sets: a door wall then
       becomes terrain 14, which has no pictures of its own.  iso_redraw
       draws it with bar picture word 2632h (= g_iso_alive(0)) and wall top
       20 = "picture 7" of the 6 in slot 18:
         2632h = 0: the bar blit starts at x = FFCEh of yericon, past its
           EMS block -> mm_map prints the page map, waits for a key and ends
           the game with internal error 10h (MEMMAN.L line 748); the port
           does the same through the same calls;
         2632h = 1: bar picture 1, and the top is wall picture 1 shifted one
           row plus 50 bytes behind the walls' block in the EMS heap.
       No shipped map reaches this: the four without a wall set 2/3 (PC13,
       14, 16, 18) have no set 8 either, which a door wall needs.
       Verified: tests/py/test_iso_d.py (test_door14_*). */
    door = 5;
    for (i = 0; i < 4; i++) {
        if (WALL_PIC(i) == 3) {
            door = (uint8_t)(i + 1);
            break;
        }
        if (WALL_PIC(i) == 2)
            door = (uint8_t)(i + 1);
    }
    /* max(0, min(size - 3, pos - 1)): Borland macros, evaluated twice */
    ox = g_s3d_map_w - 3 < g_s3d_x - 1 ? g_s3d_map_w - 3 : (int16_t)(g_s3d_x - 1);
    if (ox < 0)
        ox = 0;
    oy = g_s3d_map_h - 3 < g_s3d_y - 1 ? g_s3d_map_h - 3 : (int16_t)(g_s3d_y - 1);
    if (oy < 0)
        oy = 0;
    base = (int16_t)((oy * g_s3d_map_w + ox) * 15);
    g_iso_field_dx = (int16_t)(g_s3d_x - ox);
    g_iso_field_dy = (int16_t)(g_s3d_y - oy);
    mm_ems_unlock((MmEmsLock *)DGP(0xBB3B));
    for (i = 0; i < 3; i++)
        for (j = 0; j < 3; j++) {
            /* F_PADD: far pointer + sign-extended int16 offset */
            cell = (int16_t)(base + (int16_t)((i * g_s3d_map_w + j) * 15));
            for (x = 0; x < 4; x++) {
                g_iso_wall_t(x) = (int16_t)(s3d_map[(int16_t)(cell + 2 * x)] - 1);
                g_iso_wall_k(x) = s3d_map[(int16_t)(cell + 2 * x + 1)];
            }
            floor_t = s3d_map[(int16_t)(cell + 9)];
            if (g_iso_terrain(j * 9, i * 9) != 6)
                for (x = 0; x < 9; x++)
                    for (g_iso_cell_y = 0; g_iso_cell_y < 9; g_iso_cell_y++)
                        g_iso_terrain(j * 9 + x, i * 9 + g_iso_cell_y) = floor_t;
            open_n = side_wall(0, i, j, door);
            open_e = side_wall(1, i, j, door);
            open_s = side_wall(2, i, j, door);
            open_w = side_wall(3, i, j, door);
            if (!open_w && (g_iso_wall_k(0) == 1 || g_iso_wall_k(0) == 3) &&
                WALL_PIC(g_iso_wall_t(0)) < 9 && TER(j * 9 - 1, i * 9 - 1) < 10)
                TER(j * 9 - 1, i * 9 - 1) = (int16_t)(g_iso_wall_t(0) + 10);

            for (g_iso_cell_y = (int16_t)(i * 9 + 1 - open_n);
                 g_iso_cell_y <= i * 9 + open_s + 8; g_iso_cell_y += 5)
                for (g_iso_cell_x = (int16_t)(j * 9 + 1 - open_w);
                     g_iso_cell_x <= j * 9 + open_e + 8; g_iso_cell_x += 5) {
                    int16_t n, w;

                    x = g_iso_cell_x;
                    y = g_iso_cell_y;
                    r = bc_random(0x65);
                    t = TER(x, y);
                    if (t == 6)
                        continue;
                    n = TER(x, y - 1);
                    w = TER(x - 1, y);
                    if (floor_t == 1 || floor_t == 4) {
                        int16_t blk = floor_t == 1 ? 6 : 8;
                        if (r < 0x46)
                            t = floor_t;
                        else if (r < 0x5A)
                            t = floor_t == 1 ? 7 : 9;
                        else if (bc_random(2) == 0 && n != 0 && w != 0 && n != blk && w != blk)
                            t = 0;
                        else if (n != 0 && w != 0)
                            t = blk;
                    } else if ((floor_t == 2 || floor_t == 3) && r < 2 && n != 0 && w != 0) {
                        t = 0;
                    }
                    TER(x, y) = t;
                }
        }
}
