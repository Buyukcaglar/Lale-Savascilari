/*
 * q3d.h - 3D quad helpers of the first-person view (IST.EXE seg08D0, source
 * file ALTDUZEY\PERPEC.L per its fatal-error strings): build wall quads,
 * rotate them about a point, clip them against the near plane, project and
 * texture-map them column by column.  Used by the turn animations
 * q3d_anim_rotate_left/right (2700:8197 / 2700:8896).
 *
 * Projection (view window D930..D936, set by q3d_set_view_window):
 *     px = (cx * (z - plane_z) - focal * x) / (z - plane_z - focal)
 *     py = (cy * (z - plane_z) - focal * y) / (z - plane_z - focal)
 * i.e. centre (cx, cy), eye at z = plane_z + focal looking towards -z.
 */
#ifndef LALE_GAME_Q3D_H
#define LALE_GAME_Q3D_H

#include <stdint.h>

#include "engine/dgroup.h"

/* ---- DGROUP --------------------------------------------------------------- */
#define g_q3d_plane_z   DGS16(0xD930)   /* i16  z of the projection plane */
#define g_q3d_focal     DGS16(0xD932)   /* i16  eye distance; q3d_clip_quad_near
                                           lowers it by 20 while it runs */
#define g_q3d_cy        DGS16(0xD934)   /* i16  projection centre y */
#define g_q3d_cx        DGS16(0xD936)   /* i16  projection centre x */

#pragma pack(push, 1)
/* One corner: world x, y, z and the projected screen x, y. */
typedef struct Q3dVertex {
    int16_t x, y, z;
    int16_t px, py;
} Q3dVertex;

/* A wall quad, 30h bytes (the turn animations keep them in DGROUP at
   D938, D968, D998, D9C8).  v[0]-v[1] is one vertical edge (top, bottom),
   v[2]-v[3] the other.  Header: tu, tv = first texture column and row,
   w = width (texture columns), h = height. */
typedef struct Q3dQuad {
    int16_t tu, tv, w, h;       /* +0 */
    Q3dVertex v[4];             /* +8 */
} Q3dQuad;
#pragma pack(pop)
_Static_assert(sizeof(Q3dQuad) == 0x30, "Q3dQuad is 30h bytes");

/* Uninitialised local of q3d_draw_quad (double at [bp-56h]): the angle is
   only computed when v0 and v2 differ in x or z; otherwise the original uses
   whatever the stack held, normally the previous call's angle.  The port
   keeps the last angle here (tests set it). */
extern double q3d_stale_angle;

/* 08D0:000C - texture-map a projected quad column by column from surface
   slot src_slot into slot dst_slot (key 0FEh), clipped to the rectangle
   (clip_x, clip_y, clip_w, clip_h); the four header words and the screen
   offset (ox, oy) come first.  Needs MEMMAN (mm_ems_lock/mm_map). */
void q3d_draw_quad(int16_t tu, int16_t tv, int16_t w, int16_t h, int16_t ox, int16_t oy,
                   Q3dQuad q, uint8_t src_slot, uint8_t dst_slot,
                   int16_t clip_x, int16_t clip_y, int16_t clip_w, int16_t clip_h);
/* The body of q3d_draw_quad after the surfaces are mapped (src/dst are the
   mm_map pointers, offsets wrap at 64 KB like the original's).  Returns 0
   when the original returns early WITHOUT mm_ems_unlock (projected width 0),
   1 otherwise. */
int16_t q3d_draw_quad_mapped(const uint8_t *src, int16_t src_w, uint8_t *dst, int16_t dst_w,
                             int16_t tu, int16_t tv, int16_t w, int16_t h,
                             int16_t ox, int16_t oy, const Q3dQuad *q,
                             int16_t clip_x, int16_t clip_y, int16_t clip_w, int16_t clip_h);
void q3d_draw_quad_clipped(Q3dQuad *q);                          /* 08D0:0975 */
void q3d_clip_quad_near(Q3dQuad *q);                             /* 08D0:098C */
void q3d_set_view_window(int16_t cx, int16_t cy, int16_t plane_z, int16_t focal); /* 0D2C */
void q3d_build_side_quad(int16_t x, int16_t y, int16_t z, int16_t tu, int16_t tv,
                         int16_t w, int16_t h, Q3dQuad *out);    /* 08D0:0D49 */
void q3d_build_front_quad(int16_t x, int16_t y, int16_t z, int16_t tu, int16_t tv,
                          int16_t w, int16_t h, Q3dQuad *out);   /* 08D0:0DF9 */
Q3dQuad q3d_rotate_quad(int16_t cx, int16_t cz, float angle, Q3dQuad q); /* 08D0:0EA9 */

#endif
