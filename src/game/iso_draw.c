/*
 * iso_draw.c - ISOSYS.L 4595:2721-3F5A: the battle screen (redraw, unit
 * sprites with their hit/death/talk effects), footstep sounds and a step
 * of the actor (or of the target cursor).  See iso.h.
 *
 * Screen geometry: cell (x, y) relative to the view origin (vx, vy) is at
 * screen x = (x-vx)*30 + (y-vy)*14, y = (y-vy)*14 (plus per-picture
 * offsets); the 7x7 visible cells are drawn back to front: rows y = vy ..
 * vy+6, in each row x = vx+6 down to vx.
 */
#include "game/iso.h"

#include <stdio.h>
#include <string.h>

#include "engine/intrface.h"
#include "engine/memman.h"
#include "engine/ngraphs.h"
#include "engine/systems.h"
#include "game/chargen.h"
#include "rtl/bc_rand.h"

#define SCR_X(x, y) ((int16_t)(((x) - g_iso_view_x) * 0x1E + ((y) - g_iso_view_y) * 0x0E))
#define SCR_Y(y)    ((int16_t)(((y) - g_iso_view_y) * 0x0E))

static int cursor_at(int16_t x, int16_t y)
{
    return g_iso_cursor_on == 1 && g_iso_cursor_x == x && g_iso_cursor_y == y;
}

/*
 * iso_redraw - compose the battle screen on the work screen (slot 6) and
 * show it (only in the battle window): background (slot 1), then per cell
 * back to front: floor tile (terrain 1..9 = bar picture t), the cursor
 * frame (picture 10), walls (terrain > 9: picture 2616[t] at the floor,
 * then the wall top t+6 - or picture 15 when the animated unit D576 or
 * the cursor stands right behind it), and the unit standing there
 * (iso_draw_combatants; for a terrain-0 cell with an offset of 20).  Then
 * the big sprite of the actor (or the unit under the cursor) at 4Bh/8Eh,
 * the mini map, the name / character panel, the pending sound, the damage
 * number (631C), the pending speech (631E: text A510 by unit D5CE), and
 * with 6318 a 10-frame pause.
 *
 * Original: IST.EXE ovl4595:2721 (ISOSYS.L), far cdecl.
 * Verified: tests/py/test_iso.py (battle screens), 2026-11-21.
 */
void iso_redraw(void)
{
    int16_t t, n1, n2, n3, n4, n5, near, adj, u, v, x, y;
    char num[0x14];

    if (!iso_in_battle_window())
        return;
    mm_copy_block(mm_surface(1), mm_surface(6));
    for (g_iso_cell_y = g_iso_view_y; g_iso_cell_y <= (int16_t)(g_iso_view_y + 6);
         g_iso_cell_y++)
        for (g_iso_cell_x = (int16_t)(g_iso_view_x + 6); g_iso_cell_x >= g_iso_view_x;
             g_iso_cell_x--) {
            x = g_iso_cell_x;
            y = g_iso_cell_y;
            t = g_iso_terrain(x, y);
            g_iso_cell_unit = g_iso_occ(x, y);
            n1 = g_iso_occ(x + 1, y);
            n2 = g_iso_occ(x + 1, y - 1);
            n3 = g_iso_occ(x, y - 1);
            n4 = g_iso_occ(x + 1, y - 2);
            n5 = g_iso_occ(x + 1, y - 3);
            /* max(0, ...) macro: evaluated twice, no side effects */
            v = (int16_t)(iso_unit(g_iso_cell_unit)->facing +
                          (iso_unit(g_iso_cell_unit)->tmpl - 1) * 8);
            g_iso_frame = (int16_t)((v < 0 ? 0 : v) + 0x11);
            if (t == 0) {
                if (cursor_at(x, y))
                    iso_draw_bar((int16_t)(SCR_X(x, y) + 10), (int16_t)(SCR_Y(y) + 0x17), 10);
                if (g_iso_cell_unit > 0)
                    iso_draw_combatants(0x14);
                continue;
            }
            if (t > 9) {
                iso_draw_bar((int16_t)(SCR_X(x, y) + 10), (int16_t)(SCR_Y(y) + 0x18),
                             DGS16((uint16_t)(0x2616 + 2 * t)));
                if (cursor_at(x, y))
                    iso_draw_bar((int16_t)(SCR_X(x, y) + 10), (int16_t)(SCR_Y(y) + 0x17), 10);
                near = (n1 == g_iso_anim_unit || n2 == g_iso_anim_unit ||
                        n3 == g_iso_anim_unit || n4 == g_iso_anim_unit ||
                        n5 == g_iso_anim_unit) && g_iso_anim_unit > 0;
                adj = (x + 1 == g_iso_cursor_x && y == g_iso_cursor_y) ||
                      (x + 1 == g_iso_cursor_x && y - 1 == g_iso_cursor_y) ||
                      (x == g_iso_cursor_x && y - 1 == g_iso_cursor_y) ||
                      (x + 1 == g_iso_cursor_x && y - 2 == g_iso_cursor_y) ||
                      (x + 1 == g_iso_cursor_x && y - 3 == g_iso_cursor_y) ||
                      (x == g_iso_cursor_x && y == g_iso_cursor_y);
                iso_draw_bar((int16_t)(SCR_X(x, y) + 7), (int16_t)(SCR_Y(y) - 0x0C),
                             near || (adj && g_iso_cursor_on == 1) ? 0x0F : (int16_t)(t + 6));
            } else {
                iso_draw_bar((int16_t)(SCR_X(x, y) + 10), (int16_t)(SCR_Y(y) + 0x18), t);
                if (cursor_at(x, y))
                    iso_draw_bar((int16_t)(SCR_X(x, y) + 10), (int16_t)(SCR_Y(y) + 0x17), 10);
            }
            if (g_iso_cell_unit > 0)
                iso_draw_combatants(0);
        }
    u = g_iso_cursor_on == 0 ? g_iso_actor : g_iso_occ(g_iso_cursor_x, g_iso_cursor_y);
    g_iso_frame = (int16_t)((iso_unit(u)->tmpl - 1) * 8 + 0x14);
    if (u != 0) {
        x = 0x4B;
        y = 0x8E;
        iso_draw_sprite(&x, &y, (int16_t)(iso_unit(u)->tmpl + 10), 3);
    }
    iso_draw_map();
    if (g_iso_actor > 0x18)
        iso_draw_char_info();
    else if (g_iso_actor < 0x18)
        iso_draw_name();                   /* unit 24 gets neither */
    if (g_iso_sfx != 8)
        iso_play_sfx(g_iso_sfx);
    if (DGS16(0x631C) == 1) {
        DGS16(0x631C) = 0;
        sprintf(num, "%d", g_iso_damage);                   /* itoa(D570, buf, 10) */
        iso_draw_text(2, (int16_t)(g_iso_num_x + 0x0C), (int16_t)(g_iso_num_y + 0x0A), num, 0);
        sprintf(num, "%d", g_iso_damage);
        iso_draw_text(2, (int16_t)(g_iso_num_x + 0x0C), (int16_t)(g_iso_num_y + 0x0A), num,
                      DG8(0x9665));
        g_iso_num_dy = 0;
    }
    if (DGS16(0x631E) == 1) {
        DGS16(0x631E) = 0;
        iso_show_message(DGSTR(0xA510), g_iso_talker);
        g_iso_voice = 0;
    }
    iso_show_picture(6);
    if (DGS16(0x6318) == 1) {
        ui_wait_frames(10);
        DGS16(0x6318) = 0;
    }
}

/*
 * iso_step_sfx - footstep sound for the actor stepping on terrain `tile`:
 * in the battle window monsters of types 14/16 use 17h, 15/17/18/30/31
 * use 18h; else by terrain: 0, 2 -> 6; 1, 7, 8 -> 0Ch; 3 -> 7; 5 -> 0Bh;
 * other -> 1.
 *
 * Original: IST.EXE ovl4595:2C7B (ISOSYS.L), far cdecl (tile).  3DSYS
 * calls it for the party's steps.
 * Verified: tests/py/test_iso.py (exhaustive), 2026-11-21.
 */
int16_t iso_step_sfx(int16_t tile)
{
    if (iso_in_battle_window() && g_iso_actor < 0x19)
        switch (g_iso_group_type(iso_unit(g_iso_actor)->tmpl - 1)) {  /* 5BC2+6*tmpl; 2D15 */
        case 14: case 16: return 0x17;
        case 15: case 17: case 18: case 30: case 31: return 0x18;
        default: break;
        }
    switch (tile) {                                                 /* table 4595:2D01 */
    case 0: case 2: return 6;
    case 1: case 7: case 8: return 0x0C;
    case 3: return 7;
    case 5: return 0x0B;
    default: return 1;
    }
}

/* the compass rose of the panel: four lines on the screen per direction */
typedef struct {
    int16_t dx, dy, facing;
    int16_t l[4][5];         /* x1, y1, x2, y2, colour */
} Compass;

static const Compass compass[11] = {   /* scan codes 47h..51h, jump table 4595:393F */
    { -1, -1, 2, { {0, 0x4D, 7, 0x4D, 0xF8}, {0, 0x4F, 8, 0x57, 0xF8},
                   {8, 0x4D, 0x12, 0x57, 6}, {9, 0x57, 0x12, 0x57, 6} } },          /* 47 Home */
    { 0, -1, 1, { {10, 0x4D, 0x16, 0x4D, 0xF8}, {10, 0x4D, 0x13, 0x56, 0xF8},
                  {0x14, 0x57, 0x21, 0x57, 6}, {0x17, 0x4D, 0x21, 0x57, 6} } },     /* 48 Up */
    { 1, -1, 4, { {0x19, 0x4D, 0x23, 0x4D, 0xF8}, {0x19, 0x4D, 0x22, 0x56, 0xF8},
                  {0x23, 0x57, 0x2E, 0x57, 6}, {0x24, 0x4D, 0x2E, 0x57, 6} } },     /* 49 PgUp */
    { 0, 0, -1, { {0} } },                                                          /* 4A */
    { -1, 0, 2, { {10, 0x59, 0x14, 0x59, 0xF8}, {10, 0x59, 0x14, 0x63, 0xF8},
                  {0x15, 0x64, 0x20, 0x64, 6}, {0x15, 0x59, 0x20, 0x64, 6} } },     /* 4B Left */
    { 0, 0, -1, { {0} } },                                                          /* 4C */
    { 1, 0, 4, { {0x25, 0x59, 0x30, 0x59, 0xF8}, {0x25, 0x59, 0x2F, 0x63, 0xF8},
                 {0x30, 0x64, 0x3B, 0x64, 6}, {0x30, 0x59, 0x3B, 0x64, 6} } },      /* 4D Right */
    { 0, 0, -1, { {0} } },                                                          /* 4E */
    { -1, 1, 2, { {0x17, 0x66, 0x21, 0x66, 0xF8}, {0x17, 0x66, 0x1F, 0x6E, 0xF8},
                  {0x20, 0x6F, 0x2B, 0x6F, 6}, {0x22, 0x66, 0x2B, 0x6F, 6} } },     /* 4F End */
    { 0, 1, 3, { {0x24, 0x66, 0x30, 0x66, 0xF8}, {0x24, 0x66, 0x2C, 0x6E, 0xF8},
                 {0x2D, 0x6F, 0x3A, 0x6F, 6}, {0x31, 0x66, 0x3A, 0x6F, 6} } },      /* 50 Down */
    { 1, 1, 4, { {0x33, 0x66, 0x3C, 0x66, 0xF8}, {0x33, 0x66, 0x3B, 0x6E, 0xF8},
                 {0x3C, 0x6F, 0x46, 0x6F, 6}, {0x3D, 0x66, 0x46, 0x6F, 6} } },      /* 51 PgDn */
};

static int16_t clamp26(int16_t v)      /* max(0, min(v, 26)), macros: no side effects */
{
    v = v < 0x1A ? v : 0x1A;
    return v < 0 ? 0 : v;
}

/* scroll the view by 3 when (x, y) gets near its edge */
static void follow(int16_t x, int16_t y)
{
    if ((int16_t)(g_iso_view_x + 4) < x)
        g_iso_view_x = (int16_t)(g_iso_view_x + 3) < 0x15 ? (int16_t)(g_iso_view_x + 3) : 0x14;
    if (x < (int16_t)(g_iso_view_x + 2))
        g_iso_view_x = g_iso_view_x < 3 ? 0 : (int16_t)(g_iso_view_x - 3);
    if ((int16_t)(g_iso_view_y + 4) < y)
        g_iso_view_y = (int16_t)(g_iso_view_y + 3) < 0x15 ? (int16_t)(g_iso_view_y + 3) : 0x14;
    if (y < (int16_t)(g_iso_view_y + 2))
        g_iso_view_y = g_iso_view_y < 3 ? 0 : (int16_t)(g_iso_view_y - 3);
}

static int16_t abs16(int16_t v) { return v < 0 ? (int16_t)-v : v; }

static void face_towards(int16_t u, int16_t fx, int16_t fy, int16_t ux, int16_t uy)
{
    /* unit u at (ux, uy) turns to (fx, fy): 2 if fx < ux, 4 if fx > ux,
       else 1 if fy < uy, 3 otherwise */
    if (fx < ux)
        iso_unit(u)->facing = 2;
    else if (fx > ux)
        iso_unit(u)->facing = 4;
    else if (fy < uy)
        iso_unit(u)->facing = 1;
    else
        iso_unit(u)->facing = 3;
}

/*
 * iso_move_step - one step in the direction of the key D5D6 (keypad scan
 * codes 47h..51h; 4Ah/4Ch/4Eh: none).  The compass of the panel is
 * highlighted.  With the target cursor on (D57C = 1) the cursor moves
 * (0..26) and the view follows.  Otherwise the actor turns that way and:
 *  - bumps into a unit of the other side: the party attacks it
 *    (iso_attack), a monster does nothing;
 *  - steps if the cell is free and walkable (terrain < 10, not 6, not 8):
 *    AP - 1; every enemy next to the old cell that is not stunned and is
 *    not next to the new cell gets a free attack (D5A4 = 1); if the actor
 *    survives it moves, the view follows, a footstep sound is chosen; then
 *    every enemy next to the new cell that was not next to the old one and
 *    has no other opponent next to it gets a free attack too.
 * Afterwards D55E (facing) and the step (D5C8/D5CA) are 0.
 *
 * Original: IST.EXE ovl4595:2D39 (ISOSYS.L), far cdecl.
 * Verified: tests/py/test_iso.py (scripted steps), 2026-11-21.
 */
void iso_move_step(void)
{
    int16_t x, y, nx, ny, t, o, dx, dy, ex, ey, ddx, ddy, m, i;

    gr_cursor_hide();
    if (g_ui_list_key != 0 && (uint16_t)(g_ui_list_key - 0x47) <= 0x0A) {
        const Compass *k = &compass[g_ui_list_key - 0x47];
        if (k->facing >= 0) {
            g_iso_step_x = k->dx;
            g_iso_step_y = k->dy;
            g_iso_facing = k->facing;
            for (i = 0; i < 4; i++)
                sys_fill_slot_rect(k->l[i][0], k->l[i][1], k->l[i][2], k->l[i][3],
                                   (uint8_t)k->l[i][4], 0);
        }
    }
    gr_cursor_set_shape((uint8_t)g_cursor_shape);
    gr_wait_buttons_up();
    if (g_iso_cursor_on == 1) {
        g_iso_cursor_x = clamp26((int16_t)(g_iso_cursor_x + g_iso_step_x));
        g_iso_cursor_y = clamp26((int16_t)(g_iso_cursor_y + g_iso_step_y));
        follow(g_iso_cursor_x, g_iso_cursor_y);
        goto done;
    }
    g_iso_defender = 0;
    g_iso_d5b2 = 1;
    g_iso_d5b4 = 0;
    x = iso_unit(g_iso_actor)->x;
    y = iso_unit(g_iso_actor)->y;
    iso_unit(g_iso_actor)->facing = g_iso_facing;
    nx = clamp26((int16_t)(x + g_iso_step_x));
    ny = clamp26((int16_t)(y + g_iso_step_y));
    t = g_iso_terrain(nx, ny);
    o = g_iso_occ(nx, ny);
    g_iso_d5b8 = t < 10 && t != 6 && t != 8 && o == 0;
    if (o <= 0 || iso_unit(o)->side == iso_unit(g_iso_actor)->side) {
        g_iso_anim_unit = g_iso_actor;
        g_iso_anim_kind = 1;
    } else if (iso_unit(g_iso_actor)->side == 1) {
        g_iso_attacker = g_iso_actor;
        g_iso_defender = o;
        iso_attack();
        goto done;
    }
    if (g_iso_d5b8 == 0)
        goto done;
    g_iso_ap--;
    for (dy = -1; dy <= 1; dy++)
        for (dx = -1; dx <= 1; dx++) {
            g_iso_d5b4 = 0;
            if (x + dx > 0x1B || x + dx < 1 || y + dy < 1 || y + dy > 0x1B)
                g_iso_near = 0;
            else
                g_iso_near = g_iso_occ(x + dx, y + dy);
            if (g_iso_near <= 0 || g_iso_near == g_iso_actor ||
                iso_unit(g_iso_near)->stun != 0)
                continue;
            ex = iso_unit(g_iso_near)->x;
            ey = iso_unit(g_iso_near)->y;
            if (iso_unit(g_iso_actor)->side == iso_unit(g_iso_near)->side)
                continue;
            if (abs16((int16_t)(ex - nx)) <= 1 && abs16((int16_t)(ey - ny)) <= 1)
                continue;
            /* leaving an enemy: it strikes */
            g_iso_attacker = g_iso_near;
            g_iso_defender = g_iso_actor;
            g_iso_free_hit = 1;
            g_iso_d5b4 = 1;
            face_towards(g_iso_attacker, x, y, ex, ey);
            iso_attack();
            g_iso_anim_unit = g_iso_defender;
            iso_redraw();
            g_iso_free_hit = 0;
        }
    if (iso_unit(g_iso_actor)->hp <= 0) {
        g_iso_ap = 0;
        goto done;
    }
    iso_set_cell(x, y, 0);
    iso_set_cell(nx, ny, g_iso_actor);
    follow(nx, ny);
    iso_unit(g_iso_actor)->x = nx;
    iso_unit(g_iso_actor)->y = ny;
    g_iso_sfx = iso_step_sfx(g_iso_terrain(nx, ny));
    for (dy = -1; dy <= 1; dy++)
        for (dx = -1; dx <= 1; dx++) {
            g_iso_d5b2 = 1;
            if (nx + dx > 0x1B || nx + dx < 1 || ny + dy < 1 || ny + dy > 0x1B)
                g_iso_near = 0;
            else
                g_iso_near = g_iso_occ(nx + dx, ny + dy);
            if (g_iso_near <= 0 || g_iso_near == g_iso_actor ||
                iso_unit(g_iso_near)->stun != 0)
                continue;
            ex = iso_unit(g_iso_near)->x;
            ey = iso_unit(g_iso_near)->y;
            if (iso_unit(g_iso_actor)->side == iso_unit(g_iso_near)->side)
                continue;
            if (abs16((int16_t)(ex - x)) <= 1 && abs16((int16_t)(ey - y)) <= 1)
                continue;
            /* a new enemy: it strikes unless it is busy with someone else */
            for (ddy = -1; ddy <= 1 && g_iso_d5b2; ddy++)
                for (ddx = -1; ddx <= 1; ddx++) {
                    if (ex + ddx > 0x1B || ex + ddx < 1 || ey + ddy < 1 || ey + ddy > 0x1B)
                        m = 0;
                    else
                        m = g_iso_occ(ex + ddx, ey + ddy);
                    if (m > 0 && m != g_iso_near && m != g_iso_actor &&
                        iso_unit(g_iso_near)->side != iso_unit(m)->side) {
                        g_iso_d5b2 = 0;
                        break;
                    }
                }
            if (g_iso_d5b2 == 0)
                continue;
            g_iso_attacker = g_iso_near;
            g_iso_defender = g_iso_actor;
            g_iso_free_hit = 1;
            face_towards(g_iso_attacker, nx, ny, ex, ey);
            iso_attack();
            g_iso_free_hit = 0;
            if (iso_unit(g_iso_actor)->hp < 1)
                g_iso_ap = 0;
        }
done:
    g_iso_facing = 0;
    g_iso_step_y = 0;
    g_iso_step_x = 0;
}

/* sprite of unit u at the current cell (D5CA, D5C8) with y offset `off`;
   *px, *py come back as the bar position (iso_draw_sprite) */
static void unit_sprite(int16_t u, int16_t off, int16_t frame, int16_t *px, int16_t *py)
{
    *px = (int16_t)(SCR_X(g_iso_cell_x, g_iso_cell_y) + 0x22);
    *py = (int16_t)(SCR_Y(g_iso_cell_y) + off + 0x28);
    iso_draw_sprite(px, py, (int16_t)(iso_unit(u)->tmpl + 10), frame);
}

/*
 * iso_draw_combatants - draw the unit D5CC standing at the current cell
 * (D5CA, D5C8), y offset `off`.  If it is the animated unit D576:
 *  - a hit (D574 4, damage D570 > 0 or D572 = 1 and D59C > 0), a talk (5)
 *    or a death (2) by a party actor first grants that actor experience
 *    (record +06 += (D5BC/60)*10 + 10, +0A += race mod +0A + D5BC/60 + 3,
 *    iso_xp_progress);
 *  - death: D5A6 = 1, the side's "lost one" flag (D54E / D550), only the
 *    bar position is computed (frame + 100), picture 11 drawn there, units
 *    charmed by it (+10 = it) change side back (iso_clamp_add wraps 1..2),
 *    the side-alive flags are recomputed (632E = 1 if a side is gone);
 *  - hit (3/4 with damage): sprite and picture D574+9, the damage number
 *    is queued (631C; at least D59C for 4);
 *  - talk (5): sprite, picture 14 above, speech queued if 631A;
 *  - a miss (4 with no damage) or 6, when D572 = 0: sprite and a random
 *    exclamation ("ne?", "kim?" ... random(10)+1 -> 631A, text A510).
 * Otherwise just the sprite.
 *
 * Original: IST.EXE ovl4595:3955 (ISOSYS.L), far cdecl (off).  The
 * exclamations are F_SCOPY'd (82h bytes from 6393) to the stack first.
 * Verified: tests/py/test_iso.py (battle screens), 2026-11-21.
 */
void iso_draw_combatants(int16_t off)
{
    int16_t px, py, k, c, g;

    if (g_iso_anim_unit != g_iso_cell_unit) {
        unit_sprite(g_iso_cell_unit, off, iso_unit(g_iso_cell_unit)->facing, &px, &py);
        return;
    }
    if ((g_iso_anim_kind == 4 && (g_iso_damage > 0 || (g_iso_d572 == 1 && g_iso_d59c > 0))) ||
        g_iso_anim_kind == 5 || g_iso_anim_kind == 2) {
        k = (int16_t)(g_iso_d5bc / 0x3C);
        g = iso_unit(g_iso_actor)->tmpl;
        if (g < 5) {
            DGS16((uint16_t)(0x99AC + 0x5C * g)) += (int16_t)(k * 10 + 10);
            DGS16((uint16_t)(0x99B0 + 0x5C * g)) += (int16_t)(DGS16((uint16_t)(0x9B7C + 0x24 * g)) + k + 3);
            iso_xp_progress(g);
        }
    }
    if (g_iso_anim_kind == 2) {
        g_iso_d5a6 = 1;
        if (iso_unit(g_iso_anim_unit)->side == 2)
            g_iso_d54e = 1;
        else if (iso_unit(g_iso_anim_unit)->side == 1)
            g_iso_d550 = 1;
        unit_sprite(g_iso_anim_unit, off, (int16_t)(iso_unit(g_iso_anim_unit)->facing + 100),
                    &px, &py);
        if (g_surf_h(iso_unit(g_iso_anim_unit)->tmpl + 10) < 0x1C)
            iso_draw_bar(px, (int16_t)(SCR_Y(g_iso_cell_y) + off + 7), 0x0B);
        else
            iso_draw_bar(px, py, 0x0B);
        DGS16(0x6318) = 1;
        g_iso_alive(1) = 0;
        g_iso_alive(2) = 0;
        for (c = 1; c <= ISO_UNITS; c++) {
            if (iso_unit(c)->f10 == g_iso_anim_unit) {
                int16_t s = iso_unit(c)->side;
                iso_clamp_add(&s, 1, 1, 2);
                iso_unit(c)->side = s;
            }
            if (iso_unit(c)->hp >= 1)
                g_iso_alive(iso_unit(c)->side) = 1;
        }
        if (g_iso_alive(1) == 0 || g_iso_alive(2) == 0)
            g_iso_result = 1;
        return;
    }
    if ((g_iso_anim_kind == 3 || g_iso_anim_kind == 4) &&
        (g_iso_damage > 0 || (g_iso_d572 == 1 && g_iso_d59c > 0 && g_iso_anim_kind == 4))) {
        unit_sprite(g_iso_anim_unit, off, iso_unit(g_iso_anim_unit)->facing, &px, &py);
        if (g_surf_h(iso_unit(g_iso_anim_unit)->tmpl + 10) < 0x1C) {
            iso_draw_bar(px, (int16_t)(SCR_Y(g_iso_cell_y) + off + 7),
                         (int16_t)(g_iso_anim_kind + 9));
            g_iso_num_x = px;
            g_iso_num_y = (int16_t)(SCR_Y(g_iso_cell_y) + off + 7);
        } else {
            iso_draw_bar(px, py, (int16_t)(g_iso_anim_kind + 9));
            g_iso_num_x = px;
            g_iso_num_y = py;
        }
        if (g_iso_anim_kind == 4 && g_iso_d59c > g_iso_damage)
            g_iso_damage = g_iso_d59c;
        DGS16(0x631C) = 1;
        g_iso_talker = g_iso_cell_unit;
        g_iso_num_dy = off;
        DGS16(0x6318) = 1;
        g_iso_d582 = 1;
        return;
    }
    if (g_iso_anim_kind == 5) {
        unit_sprite(g_iso_cell_unit, off, iso_unit(g_iso_cell_unit)->facing, &px, &py);
        iso_draw_bar((int16_t)(SCR_X(g_iso_cell_x, g_iso_cell_y) + 0x0B),
                     (int16_t)(SCR_Y(g_iso_cell_y) + off - 0x0C),
                     (int16_t)(g_iso_anim_kind + 9));
        if (g_iso_voice != 0) {
            g_iso_talker = g_iso_cell_unit;
            DGS16(0x631E) = 1;
        }
    } else if (((g_iso_anim_kind == 4 && g_iso_damage == 0) || g_iso_anim_kind == 6) &&
               g_iso_d572 == 0) {
        unit_sprite(g_iso_cell_unit, off, iso_unit(g_iso_cell_unit)->facing, &px, &py);
        g_iso_voice = (int16_t)(bc_random(10) + 1);
        strcpy(DGSTR(0xA510), DGSTR((uint16_t)(0x6393 + 0x0D * (g_iso_voice - 1))));
        DGS16(0x631E) = 1;
        g_iso_talker = g_iso_cell_unit;
    } else {
        unit_sprite(g_iso_cell_unit, off, iso_unit(g_iso_cell_unit)->facing, &px, &py);
        return;
    }
    g_iso_d582 = 1;
    DGS16(0x6318) = 1;
}
