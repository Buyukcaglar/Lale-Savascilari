/*
 * q3dview_map.c - 3DQUICK.L part 3 (IST.EXE ovl2700:5F32-647A): the automap
 * and two piece-table touch-ups.  See q3dview.h.
 */
#include "game/q3dview.h"

#include <stdio.h>

#include "engine/grtext.h"
#include "engine/systems.h"

/* map cell byte at offset off (F_PADD with the sign-extended 16-bit offset) */
static uint8_t map_byte(int16_t off)
{
    return s3d_map[(int32_t)off];
}

/*
 * q3d_draw_automap - draw the whole district map on the screen (slot 0,
 * mode 13h): clear to colour FEh, one 5x5 cell per map cell with a line per
 * wall side (kind != 0) in colour 80h + 4 * (kind - 1) + type (byte
 * arithmetic), the party as a line from the cell centre (+3, +3) two pixels
 * in the facing direction in colour FFh, and "x:%d  y:%d" at (10, 185).
 * Side 0 = top edge, 1 = right, 2 = bottom, 3 = left.
 *
 * Original: IST.EXE 2700:5F32 (3DQUICK.L), far cdecl.  The sprintf result
 * is compared with 12 without effect.
 * Verified: tests/py/test_q3dview.py (test_automap: real maps, whole
 * screen).
 */
void q3d_draw_automap(void)
{
    int16_t off = 0;                            /* [bp-6] cell offset */
    int16_t x, y;                               /* [bp-2], [bp-4] */
    char text[12];                              /* [bp-12h] */

    sys_fill_slot(0, 0xFA00, 0xFE);
    off = 0;
    for (y = 0; y < g_s3d_map_h; y++) {
        for (x = 0; x < g_s3d_map_w; x++) {
            int16_t x5 = (int16_t)(x * 5), y5 = (int16_t)(y * 5);
            if (map_byte((int16_t)(off + 1)) != 0)
                sys_fill_slot_rect(x5, y5, (int16_t)(x5 + 4), y5,
                                   (uint8_t)(((uint8_t)(map_byte((int16_t)(off + 1)) - 1) << 2) +
                                             map_byte(off) + 0x80), 0);
            if (map_byte((int16_t)(off + 3)) != 0)
                sys_fill_slot_rect((int16_t)(x5 + 4), y5, (int16_t)(x5 + 4), (int16_t)(y5 + 4),
                                   (uint8_t)(((uint8_t)(map_byte((int16_t)(off + 3)) - 1) << 2) +
                                             map_byte((int16_t)(off + 2)) + 0x80), 0);
            if (map_byte((int16_t)(off + 5)) != 0)
                sys_fill_slot_rect(x5, (int16_t)(y5 + 4), (int16_t)(x5 + 4), (int16_t)(y5 + 4),
                                   (uint8_t)(((uint8_t)(map_byte((int16_t)(off + 5)) - 1) << 2) +
                                             map_byte((int16_t)(off + 4)) + 0x80), 0);
            if (map_byte((int16_t)(off + 7)) != 0)
                sys_fill_slot_rect(x5, y5, x5, (int16_t)(y5 + 4),
                                   (uint8_t)(((uint8_t)(map_byte((int16_t)(off + 7)) - 1) << 2) +
                                             map_byte((int16_t)(off + 6)) + 0x80), 0);
            off = (int16_t)(off + 15);
        }
    }
    sys_fill_slot_rect((int16_t)(g_s3d_x * 5 + 3), (int16_t)(g_s3d_y * 5 + 3),
                       (int16_t)(g_s3d_x * 5 + g_s3d_step_dx * 2 + 3),
                       (int16_t)(g_s3d_y * 5 + g_s3d_step_dy * 2 + 3), 0xFF, 0);
    sprintf(text, DGSTR(0x3D0A), g_s3d_x, g_s3d_y);           /* "x:%d  y:%d" */
    grt_draw_text(0, 10, 0xB9, text, 0xFF, 5);
}

/*
 * q3d_init_a - nudge single fields of the piece table by one pixel (an
 * earlier version of q3d_init_tables: its first 18 steps, plus 18.x++ /
 * 18.x-- which cancel).
 *
 * Original: IST.EXE 2700:624A (3DQUICK.L), far cdecl (far tab).
 * (unreferenced)
 * Verified: tests/py/test_q3dview.py (test_init_tables).
 */
void q3d_init_a(Q3dPiece *tab)
{
    tab[8].x++;     tab[18].x++;    tab[7].w++;     tab[18].h++;
    tab[1].w++;     tab[4].w++;     tab[11].x--;    tab[21].x--;
    tab[12].x--;    tab[12].w++;    tab[18].x--;    tab[11].w++;
    tab[8].x--;     tab[8].w++;     tab[6].x--;     tab[17].x--;
    tab[17].w++;    tab[21].w++;    tab[19].y++;    tab[20].y++;
}

/*
 * q3d_init_tables - hand corrections of the computed destination
 * rectangles, applied by main after the start-up tables (+-1/2/4 pixel
 * fixes of single pieces, in the original's order; some fields are touched
 * twice).
 *
 * Original: IST.EXE 2700:62EC (3DQUICK.L), far cdecl (far tab); main
 * passes g_q3d_pieces.
 * Verified: tests/py/test_q3dview.py (test_init_tables).
 */
void q3d_init_tables(Q3dPiece *tab)
{
    tab[8].x++;     tab[7].w++;     tab[18].h++;    tab[1].w++;
    tab[4].w++;     tab[11].x--;    tab[21].x--;    tab[12].x--;
    tab[12].w++;    tab[11].w++;    tab[8].x--;     tab[8].w++;
    tab[6].x--;     tab[17].x--;    tab[17].w++;    tab[21].w++;
    tab[19].y++;    tab[20].y++;    tab[15].w++;    tab[27].y--;
    tab[28].y--;    tab[29].x--;    tab[18].y -= 2; tab[21].y -= 2;
    tab[7].y -= 2;  tab[12].y -= 2; tab[18].h += 2; tab[21].h += 2;
    tab[8].w -= 2;  tab[11].w -= 2; tab[8].h++;     tab[11].h++;
    tab[32].y--;    tab[32].x--;    tab[30].x++;    tab[30].y++;
    tab[34].y -= 4; tab[35].y -= 4; tab[34].h += 2; tab[35].h += 2;
    tab[35].x -= 4; tab[1].w -= 2;  tab[9].w++;     tab[10].w++;
    tab[8].y--;     tab[11].y--;    tab[22].tw++;   tab[25].tw++;
}
