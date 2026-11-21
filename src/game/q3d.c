/*
 * q3d.c - 3D quad build/rotate/clip/texture-map (IST.EXE seg08D0, source file
 * ALTDUZEY\PERPEC.L).  See q3d.h.
 *
 * Floating point.  q3d_draw_quad and q3d_rotate_quad compute on the x87
 * stack with 80-bit intermediates.  The port reproduces that portably with a
 * small software model of the x87 extended format (Ext below: 64-bit
 * significand, round-to-nearest-even, no denormals - the values here never
 * get near them), so the result does not depend on the host's long double
 * or FPU control word.  sin/cos/atan2 are the host libm's double results,
 * which is what the Unicorn reference does (QEMU computes FSIN/FCOS/FPATAN as
 * host double functions).  The real original computes them with the Borland
 * emulator or a 387 to 64 bits (the DOSBox-X reference run has _8087 = 0 at
 * main(), i.e. the Borland software emulator); the results feed only integer
 * truncations after being rounded to double/float, so they differ only when
 * a quotient lands within an ulp of an integer.
 *
 * Exceptional cases (x/0, inf-inf, FTOL out of range) follow a MASKED x87:
 * inf/NaN, FTOL -> 8000000000000000h.  The game runs with Borland's default
 * control word, which (per Borland's float.h CW_DEFAULT) leaves invalid and
 * zero-divide unmasked, so there the original would stop with a floating
 * point error instead; the turn animations never produce such quads.
 */
#include "game/q3d.h"

#include <math.h>
#include <string.h>

#if __has_include("engine/memman.h")
#include "engine/memman.h"
#define Q3D_HAVE_MEMMAN 1
#endif

double q3d_stale_angle = 0.0;

/* ======================================================================== */
/* x87 extended precision model                                              */
/* ======================================================================== */

typedef unsigned __int128 u128;

enum { EXT_ZERO, EXT_NORM, EXT_INF, EXT_NAN };

/* value = (-1)^s * m * 2^(e - 63), m normalised (bit 63 set) for EXT_NORM */
typedef struct Ext {
    uint64_t m;
    int32_t  e;
    uint8_t  s;
    uint8_t  cls;
} Ext;

static Ext ext_special(uint8_t cls, uint8_t s)
{
    Ext r = { cls == EXT_NAN ? 0xC000000000000000ull : 0, 0, s, cls };
    return r;
}

static int top_bit(u128 v)
{
    int p = 0;
    if (v >> 64) { p = 64; v >>= 64; }
    uint64_t w = (uint64_t)v;
    while (w >>= 1)
        p++;
    return p;
}

/* Round sig * 2^base (sig != 0) to 64 bits, nearest-even.  sticky = some
   nonzero bits below sig were dropped. */
static Ext ext_round(uint8_t s, u128 sig, int32_t base, int sticky)
{
    Ext r;
    int p = top_bit(sig);
    r.s = s;
    r.cls = EXT_NORM;
    if (p <= 63) {
        r.m = (uint64_t)(sig << (63 - p));
        r.e = base + p;
        return r;                       /* exact (sticky is 0 on these paths) */
    }
    int sh = p - 63;
    u128 keep = sig >> sh;
    u128 rest = sig - (keep << sh);
    u128 half = (u128)1 << (sh - 1);
    r.m = (uint64_t)keep;
    r.e = base + p;
    if (rest > half || (rest == half && (sticky || (r.m & 1)))) {
        if (++r.m == 0) {               /* carried out of 64 bits */
            r.m = 0x8000000000000000ull;
            r.e++;
        }
    }
    return r;
}

static Ext ext_from_i32(int32_t v)
{
    if (v == 0)
        return ext_special(EXT_ZERO, 0);
    uint8_t s = v < 0;
    uint64_t a = s ? (uint64_t)(-(int64_t)v) : (uint64_t)v;
    return ext_round(s, a, 0, 0);
}

static Ext ext_from_double(double d)
{
    uint64_t b;
    memcpy(&b, &d, 8);
    uint8_t s = (uint8_t)(b >> 63);
    int ex = (int)((b >> 52) & 0x7FF);
    uint64_t f = b & 0xFFFFFFFFFFFFFull;
    if (ex == 0x7FF)
        return ext_special(f ? EXT_NAN : EXT_INF, s);
    if (ex == 0) {
        if (f == 0)
            return ext_special(EXT_ZERO, s);
        return ext_round(s, f, -1074, 0);                 /* subnormal double */
    }
    return ext_round(s, f | (1ull << 52), ex - 1075, 0);
}

static Ext ext_from_float(float x)
{
    return ext_from_double((double)x);                    /* exact */
}

static Ext ext_neg(Ext a)
{
    a.s ^= 1;
    return a;
}

static Ext ext_add(Ext a, Ext b)
{
    if (a.cls == EXT_NAN || b.cls == EXT_NAN)
        return ext_special(EXT_NAN, 1);
    if (a.cls == EXT_INF || b.cls == EXT_INF) {
        if (a.cls == EXT_INF && b.cls == EXT_INF && a.s != b.s)
            return ext_special(EXT_NAN, 1);               /* inf - inf */
        return a.cls == EXT_INF ? a : b;
    }
    if (a.cls == EXT_ZERO && b.cls == EXT_ZERO)
        return ext_special(EXT_ZERO, (uint8_t)(a.s & b.s));   /* +0 unless -0 + -0 */
    if (a.cls == EXT_ZERO)
        return b;
    if (b.cls == EXT_ZERO)
        return a;
    /* make a the larger magnitude */
    if (b.e > a.e || (b.e == a.e && b.m > a.m)) {
        Ext t = a; a = b; b = t;
    }
    int d = a.e - b.e;
    u128 A = (u128)a.m << 63;                             /* value A * 2^(a.e-126) */
    u128 B = (u128)b.m << 63;
    int sticky = 0;
    if (d >= 127) {
        sticky = 1;
        B = 0;
    } else if (d > 0) {
        u128 lost = B & (((u128)1 << d) - 1);
        B >>= d;
        sticky = lost != 0;
    }
    u128 S;
    if (a.s == b.s) {
        S = A + B;
    } else {
        S = A - B;
        if (sticky)                     /* true B is a bit larger than B: */
            S -= 1;                     /* borrow, keep the sticky below */
        if (S == 0 && !sticky)
            return ext_special(EXT_ZERO, 0);              /* exact cancellation: +0 */
    }
    return ext_round(a.s, S, a.e - 126, sticky);
}

static Ext ext_sub(Ext a, Ext b)
{
    return ext_add(a, ext_neg(b));
}

static Ext ext_mul(Ext a, Ext b)
{
    uint8_t s = a.s ^ b.s;
    if (a.cls == EXT_NAN || b.cls == EXT_NAN)
        return ext_special(EXT_NAN, 1);
    if (a.cls == EXT_INF || b.cls == EXT_INF) {
        if (a.cls == EXT_ZERO || b.cls == EXT_ZERO)
            return ext_special(EXT_NAN, 1);
        return ext_special(EXT_INF, s);
    }
    if (a.cls == EXT_ZERO || b.cls == EXT_ZERO)
        return ext_special(EXT_ZERO, s);
    return ext_round(s, (u128)a.m * b.m, a.e + b.e - 126, 0);
}

static Ext ext_div(Ext a, Ext b)
{
    uint8_t s = a.s ^ b.s;
    if (a.cls == EXT_NAN || b.cls == EXT_NAN)
        return ext_special(EXT_NAN, 1);
    if (a.cls == EXT_INF)
        return b.cls == EXT_INF ? ext_special(EXT_NAN, 1) : ext_special(EXT_INF, s);
    if (b.cls == EXT_INF)
        return ext_special(EXT_ZERO, s);
    if (b.cls == EXT_ZERO)
        return a.cls == EXT_ZERO ? ext_special(EXT_NAN, 1) : ext_special(EXT_INF, s);
    if (a.cls == EXT_ZERO)
        return ext_special(EXT_ZERO, s);
    u128 num = (u128)a.m << 64;
    u128 q = num / b.m, r = num % b.m;
    /* two more quotient bits so there are always guard bits below the 64 */
    r <<= 2;
    q = (q << 2) | (r / b.m);
    r %= b.m;
    /* a/b = (a.m/b.m) * 2^(a.e-b.e), q = a.m/b.m * 2^66 */
    return ext_round(s, q, a.e - b.e - 66, r != 0);
}

static double ext_to_double(Ext a)
{
    if (a.cls == EXT_ZERO)
        return a.s ? -0.0 : 0.0;
    if (a.cls == EXT_INF)
        return a.s ? -INFINITY : INFINITY;
    if (a.cls == EXT_NAN)
        return NAN;
    /* round the 64-bit significand to 53 bits, nearest-even */
    uint64_t keep = a.m >> 11, rest = a.m & 0x7FF;
    int32_t e = a.e;
    if (rest > 0x400 || (rest == 0x400 && (keep & 1))) {
        if (++keep == (1ull << 53)) {
            keep >>= 1;
            e++;
        }
    }
    /* normal doubles only: |exponent| stays tiny for these computations */
    double d = ldexp((double)keep, e - 52);
    return a.s ? -d : d;
}

/* Borland F_FTOL (0000:1099): FISTP qword with rounding forced to chop; the
   low dword is returned in DX:AX.  Out of range/NaN gives the integer
   indefinite 8000000000000000h, i.e. 0. */
static int32_t ext_ftol(Ext a)
{
    if (a.cls == EXT_ZERO)
        return 0;
    if (a.cls != EXT_NORM || a.e >= 63)
        return 0;               /* indefinite (and -2^63 itself): low dword 0 */
    if (a.e < 0)
        return 0;
    uint64_t v = a.m >> (63 - a.e);
    return (int32_t)(uint32_t)(a.s ? (uint64_t)0 - v : v);
}

/* ======================================================================== */
/* Borland long helpers (N_LXMUL@ / N_LDIV@): plain 32-bit maths            */
/* ======================================================================== */

static int32_t lmul(int32_t a, int32_t b)
{
    return (int32_t)((uint32_t)a * (uint32_t)b);
}

static int32_t ldiv32(int32_t a, int32_t b)
{
    if (b == 0)
        return 0;               /* the original takes the divide-error exit */
    if (a == INT32_MIN && b == -1)
        return INT32_MIN;
    return a / b;
}

static int16_t abs16(int16_t v)  /* CWD / XOR AX,DX / SUB AX,DX */
{
    return (int16_t)((v ^ (v >> 15)) - (v >> 15));
}

/* projection used by q3d_draw_quad and q3d_clip_quad_near:
   (k * (z - plane_z) - focal * c) / (z - plane_z - focal) */
static int16_t q3d_project(int16_t k, int16_t z, int16_t c)
{
    int16_t dz = (int16_t)(z - g_q3d_plane_z);
    int32_t num = lmul(dz, k) - lmul(c, g_q3d_focal);
    return (int16_t)ldiv32(num, (int16_t)(dz - g_q3d_focal));
}

/*
 * edge_scale - the floating-point part of q3d_draw_quad (08D0:0259-0406 and
 * 04A8-0655): project the point (x, z) and the point one unit further along
 * the wall direction a, (x - cos a, z - sin a), and return the two screen x
 * values as doubles.  The first projection multiplies sin/cos by 0.0 (dead
 * terms in the original source, kept: they are exact no-ops for finite
 * values).
 */
static void edge_scale(int16_t x, int16_t z, double a, double *p0, double *p1)
{
    Ext Z = ext_from_i32((int16_t)(z - g_q3d_plane_z));
    Ext X = ext_from_i32(x);
    Ext cx = ext_from_i32(g_q3d_cx), f = ext_from_i32(g_q3d_focal);
    Ext zero = ext_special(EXT_ZERO, 0);
    Ext s = ext_from_double(sin(a)), c = ext_from_double(cos(a));
    Ext n, d;

    /* (cx*(Z - 0*sin a) - focal*(X + 0*cos a)) / ((Z - 0*sin a) - focal) */
    n = ext_sub(ext_mul(ext_sub(Z, ext_mul(s, zero)), cx),
                ext_mul(ext_add(X, ext_mul(c, zero)), f));
    d = ext_sub(ext_sub(Z, ext_mul(s, zero)), f);
    *p0 = ext_to_double(ext_div(n, d));                  /* [bp-5Eh] */
    /* (cx*(Z - sin a) - focal*(X - cos a)) / ((Z - sin a) - focal) */
    n = ext_sub(ext_mul(ext_sub(Z, s), cx), ext_mul(ext_sub(X, c), f));
    d = ext_sub(ext_sub(Z, s), f);
    *p1 = ext_to_double(ext_div(n, d));                  /* [bp-66h] */
}

/*
 * q3d_draw_quad_mapped - body of q3d_draw_quad once both surfaces are
 * mapped (see q3d_draw_quad).
 *
 * Original: IST.EXE 08D0:01A3-0973 (inside q3d_draw_quad).
 * Verified: tests/py/test_q3d.py (unicorn differential), 2026-11-20.
 *
 * The quad is drawn as vertical spans from the projected edge v0-v1 to
 * v2-v3, one per screen column between px(v0) and px(v2).  If v0 is nearer
 * the eye than v2 (z0 < z2) the edges are swapped and the texture is
 * mirrored (u = w + 2*tu - u).  The texture column advances by a quadratic
 * (perspective-ish) increment derived from "scale" = 1000 / (projected
 * width of one wall unit at the far edge), in 1/1000 texel units.
 */
int16_t q3d_draw_quad_mapped(const uint8_t *src, int16_t src_w, uint8_t *dst, int16_t dst_w,
                             int16_t tu, int16_t tv, int16_t w, int16_t h,
                             int16_t ox, int16_t oy, const Q3dQuad *qin,
                             int16_t clip_x, int16_t clip_y, int16_t clip_w, int16_t clip_h)
{
    Q3dQuad q = *qin;
    int16_t clip_r = (int16_t)(clip_x + clip_w);          /* [bp-3Ah] */
    int16_t clip_b = (int16_t)(clip_y + clip_h);          /* [bp-3Ch] */
    int32_t u_acc = lmul(tu, 1000);                       /* [bp-40h] texture u * 1000 */
    int16_t mirror, x_far;                                /* [bp-6Ah], [bp-1Ch] */
    double a, p0, p1;
    const Q3dVertex *vf;                                  /* far vertex */

    if (q.v[0].z < q.v[2].z) {
        int16_t t;
        /* 08D0:01C5 - swap the projected edges */
        t = q.v[0].px; q.v[0].px = q.v[2].px; q.v[2].px = t;
        t = q.v[0].py; q.v[0].py = q.v[2].py; q.v[2].py = t;
        t = q.v[1].px; q.v[1].px = q.v[3].px; q.v[3].px = t;
        t = q.v[1].py; q.v[1].py = q.v[3].py; q.v[3].py = t;
        if ((int16_t)(q.v[2].z - q.v[0].z) != 0 || (int16_t)(q.v[2].x - q.v[0].x) != 0)
            q3d_stale_angle = atan2((double)(int16_t)(q.v[2].z - q.v[0].z),
                                    (double)(int16_t)(q.v[2].x - q.v[0].x));
        vf = &q.v[2];
        mirror = 1;
    } else {
        if ((int16_t)(q.v[2].z - q.v[0].z) != 0 || (int16_t)(q.v[2].x - q.v[0].x) != 0)
            q3d_stale_angle = atan2((double)(int16_t)(q.v[0].z - q.v[2].z),
                                    (double)(int16_t)(q.v[0].x - q.v[2].x));
        vf = &q.v[0];
        mirror = 0;
    }
    a = q3d_stale_angle;                                  /* [bp-56h] */
    edge_scale(vf->x, vf->z, a, &p0, &p1);
    /* 08D0:040B: projected x of the far edge shifted by w (integer maths) */
    x_far = q3d_project(g_q3d_cx, vf->z, (int16_t)(vf->x + w));

    int16_t x0 = q.v[0].px, y0 = q.v[0].py;               /* [bp-12h], [bp-14h] */
    int16_t y1 = q.v[1].py, y3 = q.v[3].py;               /* [bp-16h], [bp-18h] */
    int16_t x2 = q.v[2].px, y2 = q.v[2].py;               /* [bp-2], [bp-4] */
    (void)x_far;                  /* [bp-24h] = x_far - x0 is overwritten unused */

    int16_t cols = (int16_t)(x2 - x0);                    /* [bp-26h] */
    if (cols == 0)
        return 0;                 /* 08D0:06E4: LEAVE/RETF, mm_ems_unlock skipped */
    int16_t step = cols < 0 ? -1 : 1;                     /* [bp-1Eh] */
    if ((int16_t)(step * cols) == 1)
        cols = (int16_t)(step << 1);                      /* one column -> two */
    cols = (int16_t)(step * cols);                        /* |cols| */

    /* 08D0:0714: texels per projected unit, *1000, truncated (FTOL) */
    Ext diff = ext_sub(ext_from_double(p1), ext_from_double(p0));
    int16_t scale = abs16((int16_t)ext_ftol(ext_div(ext_from_float(1000.0f), diff)));  /* [bp-28h] */

    int32_t quot = ldiv32(lmul(cols, scale), 1000);
    int32_t u_curve = lmul(1000, (int32_t)((uint32_t)((int32_t)w - quot) << 1));  /* [bp-30h] */
    int32_t u_div = lmul((int16_t)(cols - 1), cols);      /* [bp-2Ch] */
    cols = (int16_t)(step * cols);

    int16_t dy_top = (int16_t)(y2 - y0);                  /* [bp-24h] */
    int16_t dh_right = (int16_t)(y3 - y2);                /* [bp-4] */
    int16_t h_left = (int16_t)(y1 - y0);                  /* [bp-1Ah] */
    int16_t dh = (int16_t)(h_left - dh_right);            /* [bp-22h] */

    for (int16_t col = 0; col != cols; col = (int16_t)(col + step)) {   /* [bp-32h] */
        int32_t t;
        int16_t span, span_full, vbias, sx, sy, skip;

        u_acc += (int32_t)scale + ldiv32(lmul(u_curve, (int16_t)(step * col)), u_div);
        t = (int32_t)col * dh;                            /* [bp-6Eh] */
        span = (int16_t)(h_left - (int16_t)ldiv32(t, cols));   /* [bp-36h] */
        span_full = span;                                 /* [bp-42h] */
        /* 08D0:081C: 16-bit IDIV of the low word, remainder / 20, * span,
           low word again (CWD) / (cols * 10) */
        {
            int16_t rem = (int16_t)((int16_t)t % cols);
            int16_t p = (int16_t)((int32_t)(int16_t)(rem / 20) * span_full);
            vbias = (int16_t)(p / (int16_t)(cols * 10));  /* [bp-44h] */
        }
        sx = (int16_t)(ox + x0 + col);                    /* [bp-46h] */
        t = (int32_t)dy_top * col;
        sy = (int16_t)(oy + y0 + (int16_t)ldiv32(t, cols));   /* [bp-38h] */
        if (sx < clip_x || !(clip_r > sx))
            continue;
        int16_t row0;
        if (sy >= clip_y) {
            skip = 0;                                     /* [bp-20h] */
            row0 = sy;
        } else {
            skip = (int16_t)(clip_y - sy);
            row0 = (int16_t)(sy + skip);
        }
        uint16_t di = (uint16_t)(sx + (int16_t)(row0 * dst_w));
        if (clip_b < (int16_t)(sy + span))
            span = (int16_t)(clip_b - sy);
        if (span < skip)
            continue;
        int16_t ucol = (int16_t)ldiv32(u_acc, 1000);
        if (mirror)
            ucol = (int16_t)(w + (int16_t)(tu << 1) - ucol);   /* [bp-4Ah] */
        for (int16_t r = skip; r != span; r++) {          /* [bp-34h] */
            /* unsigned DIV: texture row = (r*h - vbias) / span_full */
            uint16_t v = (uint16_t)((uint16_t)(int16_t)(r * h) - (uint16_t)vbias);
            uint16_t row = (uint16_t)(v / (uint16_t)span_full);
            uint16_t si = (uint16_t)(ucol + (int16_t)((int16_t)(tv + row) * src_w));
            uint8_t al = src[si];
            if (al != 0xFE)
                dst[di] = al;
            di = (uint16_t)(di + dst_w);
        }
    }
    return 1;
}

/*
 * q3d_draw_quad - texture-map a projected quad (see q3d.h).
 *
 * Original: IST.EXE 08D0:000C, far cdecl; the quad is passed by value.
 * Verified: body via q3d_draw_quad_mapped (tests/py/test_q3d.py); the
 * MEMMAN calls around it are not tested here.
 *
 * Quirk kept: when the projected width is 0 the original returns without
 * mm_ems_unlock, leaving the lock context DE26 busy.
 */
#ifdef Q3D_HAVE_MEMMAN

void q3d_draw_quad(int16_t tu, int16_t tv, int16_t w, int16_t h, int16_t ox, int16_t oy,
                   Q3dQuad q, uint8_t src_slot, uint8_t dst_slot,
                   int16_t clip_x, int16_t clip_y, int16_t clip_w, int16_t clip_h)
{
    uint8_t *src, *dst;           /* [bp-0Ch], [bp-10h] */
    uint16_t len, mslot;          /* [bp-4Ch], [bp-4Eh] */
    int16_t src_w = g_surf_w(src_slot);                   /* [bp-8] */

    mm_ems_lock(g_blit_lock, 0x52, DGSTR(0x6D7C));          /* "T:ALTDUZEY\PERPEC.L" */
    mslot = 0;
    len = 0xFFFF;
    mm_map(mm_surface(src_slot), &src, &len, &mslot, 0);
    /* the original also sprintf()s "len=%u < membuffer[%d]->length=%lu" into
       a local buffer before each fatal call, and never uses it */
    if ((uint32_t)len < mm_surface(src_slot)->size)
        o559A_fatal(DGSTR(0x6DB3), 0x58, NULL, 0x70);
    mm_map(mm_surface(dst_slot), &dst, &len, &mslot, 0); /* len/mslot not reset */
    if ((uint32_t)len < mm_surface(dst_slot)->size)
        o559A_fatal(DGSTR(0x6DEA), 0x60, NULL, 0x70);
    if (q3d_draw_quad_mapped(src, src_w, dst, g_surf_w(dst_slot), tu, tv, w, h, ox, oy, &q,
                             clip_x, clip_y, clip_w, clip_h))
        mm_ems_unlock(g_blit_lock);
}
#endif

/*
 * q3d_draw_quad_clipped - clear a quad (setmem(q, 30h, 0)).  Called by
 * q3d_clip_quad_near when the whole quad is behind the near limit; the
 * name in names.tsv is historical.
 *
 * Original: IST.EXE 08D0:0975, far cdecl.
 * Verified: tests/py/test_q3d.py (via q3d_clip_quad_near), 2026-11-20.
 */
void q3d_draw_quad_clipped(Q3dQuad *q)
{
    memset(q, 0, sizeof *q);
}

/*
 * q3d_clip_quad_near - clip the quad against z - plane_z = focal - 20 (the
 * focal distance is lowered by 20 while clipping) and project it.
 *
 * Original: IST.EXE 08D0:098C, far cdecl.
 * Verified: tests/py/test_q3d.py (unicorn differential), 2026-11-20.
 *
 * Only v0 and v2 are tested ("behind" = z - plane_z >= focal - 20):
 *   both behind  -> the quad is zeroed, nothing projected;
 *   v0 behind    -> v0/v1 move to z = plane_z + focal - 21 along x, the
 *                   texture start tu grows and w shrinks accordingly;
 *   v2 behind    -> v2/v3 likewise (w shrinks).
 * The 16-bit IMUL/IDIV use the low word of the product (CWD), and the two
 * cases are asymmetric (+1 / -1), both kept.  Projected fields written:
 * v0.px, v0.py, v1.py, v2.px, v2.py, v3.py (v1.px/v3.px stay as they were).
 */
void q3d_clip_quad_near(Q3dQuad *q)
{
    int16_t dx, dw;               /* [bp-2], [bp-4] */
    int16_t znear;

    g_q3d_focal = (int16_t)(g_q3d_focal - 20);
    if ((int16_t)(q->v[0].z - g_q3d_plane_z) >= g_q3d_focal) {
        if ((int16_t)(q->v[2].z - g_q3d_plane_z) >= g_q3d_focal) {
            q3d_draw_quad_clipped(q);
            g_q3d_focal = (int16_t)(g_q3d_focal + 20);
            return;
        }
        int16_t over = (int16_t)((int16_t)(q->v[0].z - g_q3d_plane_z) - (int16_t)(g_q3d_focal - 1));
        int16_t dz = (int16_t)(q->v[0].z - q->v[2].z);
        dx = (int16_t)((int16_t)(over * (int16_t)(q->v[0].x - q->v[2].x)) / dz);
        dw = (int16_t)((int16_t)(over * q->w) / dz);
        q->v[0].x = (int16_t)(q->v[0].x - dx);
        znear = (int16_t)(g_q3d_focal + g_q3d_plane_z - 1);
        q->v[0].z = znear;
        q->v[1].z = znear;
        q->tu = (int16_t)(q->tu + abs16(dw));
        q->w = (int16_t)(q->w - abs16(dw));
    } else if ((int16_t)(q->v[2].z - g_q3d_plane_z) >= g_q3d_focal) {
        int16_t over = (int16_t)((int16_t)(q->v[2].z - g_q3d_plane_z - g_q3d_focal) - 1);
        int16_t dz = (int16_t)(q->v[2].z - q->v[0].z);
        dx = (int16_t)((int16_t)(over * (int16_t)(q->v[2].x - q->v[0].x)) / dz);
        dw = (int16_t)((int16_t)(over * q->w) / dz);
        q->v[2].x = (int16_t)(q->v[2].x - dx);
        znear = (int16_t)(g_q3d_focal + g_q3d_plane_z - 1);
        q->v[2].z = znear;
        q->v[3].z = znear;
        q->w = (int16_t)(q->w - abs16(dw));
    }
    g_q3d_focal = (int16_t)(g_q3d_focal + 20);

    q->v[0].px = q3d_project(g_q3d_cx, q->v[0].z, q->v[0].x);
    q->v[0].py = q3d_project(g_q3d_cy, q->v[0].z, q->v[0].y);
    q->v[1].py = q3d_project(g_q3d_cy, q->v[1].z, q->v[1].y);
    q->v[2].px = q3d_project(g_q3d_cx, q->v[2].z, q->v[2].x);
    q->v[2].py = q3d_project(g_q3d_cy, q->v[2].z, q->v[2].y);
    q->v[3].py = q3d_project(g_q3d_cy, q->v[3].z, q->v[3].y);
}

/*
 * q3d_set_view_window - set the projection centre, plane and focal distance.
 *
 * Original: IST.EXE 08D0:0D2C, far cdecl.
 * Verified: tests/py/test_q3d.py (unicorn differential), 2026-11-20.
 */
void q3d_set_view_window(int16_t cx, int16_t cy, int16_t plane_z, int16_t focal)
{
    g_q3d_cx = cx;
    g_q3d_cy = cy;
    g_q3d_plane_z = plane_z;
    g_q3d_focal = focal;
}

/*
 * q3d_build_side_quad - wall in an x = const plane: v0 (x,y,z), v1 below it
 * (y+h), v2/v3 at z-w.  Projected fields are not written.
 *
 * Original: IST.EXE 08D0:0D49, far cdecl.
 * Verified: tests/py/test_q3d.py (unicorn differential), 2026-11-20.
 */
void q3d_build_side_quad(int16_t x, int16_t y, int16_t z, int16_t tu, int16_t tv,
                         int16_t w, int16_t h, Q3dQuad *out)
{
    out->tu = tu;
    out->tv = tv;
    out->w = w;
    out->h = h;
    out->v[0].x = x; out->v[0].y = y;                    out->v[0].z = z;
    out->v[1].x = x; out->v[1].y = (int16_t)(y + h);     out->v[1].z = z;
    out->v[2].x = x; out->v[2].y = y;                    out->v[2].z = (int16_t)(z - w);
    out->v[3].x = x; out->v[3].y = (int16_t)(y + h);     out->v[3].z = (int16_t)(z - w);
}

/*
 * q3d_build_front_quad - wall in a z = const plane: v2/v3 at x+w.
 *
 * Original: IST.EXE 08D0:0DF9, far cdecl.
 * Verified: tests/py/test_q3d.py (unicorn differential), 2026-11-20.
 */
void q3d_build_front_quad(int16_t x, int16_t y, int16_t z, int16_t tu, int16_t tv,
                          int16_t w, int16_t h, Q3dQuad *out)
{
    out->tu = tu;
    out->tv = tv;
    out->w = w;
    out->h = h;
    out->v[0].x = x;                 out->v[0].y = y;                out->v[0].z = z;
    out->v[1].x = x;                 out->v[1].y = (int16_t)(y + h); out->v[1].z = z;
    out->v[2].x = (int16_t)(x + w);  out->v[2].y = y;                out->v[2].z = z;
    out->v[3].x = (int16_t)(x + w);  out->v[3].y = (int16_t)(y + h); out->v[3].z = z;
}

/*
 * q3d_rotate_quad - rotate the four vertices about (cx, cz) in the x/z plane:
 *     x' = ftol(dx*c + dz*s) + cx,  z' = ftol(dz*c - dx*s) + cz
 * with c = (float)cos(angle), s = (float)sin(angle), dx = x - cx, dz = z - cz,
 * the products and the sum in 80-bit precision, truncated (F_FTOL).
 *
 * Original: IST.EXE 08D0:0EA9, far cdecl, returns the quad by value (hidden
 * pointer, F_SCOPY).
 * Verified: tests/py/test_q3d.py (unicorn differential, many angles), 2026-11-20.
 *
 * The result is built in an uninitialised local: its px/py fields are stack
 * garbage in the original (never read: q3d_clip_quad_near writes the ones
 * q3d_draw_quad uses).  The port returns them as 0.
 */
Q3dQuad q3d_rotate_quad(int16_t cx, int16_t cz, float angle, Q3dQuad q)
{
    Q3dQuad r;
    Ext c = ext_from_float((float)cos((double)angle));   /* FSTP float [bp-6] */
    Ext s = ext_from_float((float)sin((double)angle));   /* FSTP float [bp-0Ah] */
    int i;

    memset(&r, 0, sizeof r);
    r.tu = q.tu;
    r.tv = q.tv;
    r.w = q.w;
    r.h = q.h;
    for (i = 0; i < 4; i++) {
        Ext dx, dz;
        int16_t nx, nz;
        r.v[i].y = q.v[i].y;
        dx = ext_from_i32((int16_t)(q.v[i].x - cx));
        dz = ext_from_i32((int16_t)(q.v[i].z - cz));
        nx = (int16_t)ext_ftol(ext_add(ext_mul(dx, c), ext_mul(dz, s)));
        nz = (int16_t)ext_ftol(ext_sub(ext_mul(dz, c), ext_mul(dx, s)));
        r.v[i].x = (int16_t)(nx + cx);
        r.v[i].z = (int16_t)(nz + cz);
    }
    return r;
}
