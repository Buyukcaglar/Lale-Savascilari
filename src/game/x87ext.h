/*
 * x87ext.h - software model of x87 80-bit extended arithmetic (64-bit
 * significand, round-to-nearest-even, masked exceptions), for the FP sites
 * of the first-person renderer.
 *
 * The code is a copy of the model in src/game/q3d.c (verified bit-exact
 * against x87 on 2 million random operations, see q3d.c); it is kept
 * separate because q3d.c's copy is file-static and owned by another work
 * package.  Additions here: ext_to_float (FSTP dword) and ext_round_float.
 */
#ifndef LALE_GAME_X87EXT_H
#define LALE_GAME_X87EXT_H

#include <math.h>
#include <stdint.h>
#include <string.h>

typedef unsigned __int128 u128;

enum { EXT_ZERO, EXT_NORM, EXT_INF, EXT_NAN };

/* value = (-1)^s * m * 2^(e - 63), m normalised (bit 63 set) for EXT_NORM */
typedef struct Ext {
    uint64_t m;
    int32_t  e;
    uint8_t  s;
    uint8_t  cls;
} Ext;

static inline Ext ext_special(uint8_t cls, uint8_t s)
{
    Ext r = { cls == EXT_NAN ? 0xC000000000000000ull : 0, 0, s, cls };
    return r;
}

static inline int top_bit(u128 v)
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
static inline Ext ext_round(uint8_t s, u128 sig, int32_t base, int sticky)
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

static inline Ext ext_from_i32(int32_t v)
{
    if (v == 0)
        return ext_special(EXT_ZERO, 0);
    uint8_t s = v < 0;
    uint64_t a = s ? (uint64_t)(-(int64_t)v) : (uint64_t)v;
    return ext_round(s, a, 0, 0);
}

static inline Ext ext_from_double(double d)
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

static inline Ext ext_from_float(float x)
{
    return ext_from_double((double)x);                    /* exact */
}

static inline Ext ext_neg(Ext a)
{
    a.s ^= 1;
    return a;
}

static inline Ext ext_add(Ext a, Ext b)
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

static inline Ext ext_sub(Ext a, Ext b)
{
    return ext_add(a, ext_neg(b));
}

static inline Ext ext_mul(Ext a, Ext b)
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

static inline Ext ext_div(Ext a, Ext b)
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

static inline double ext_to_double(Ext a)
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
static inline int32_t ext_ftol(Ext a)
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


/* FSTP dword: round the 64-bit significand to 24 bits (nearest-even).
   Normal floats only (the renderer's values are small integers/ratios). */
static inline float ext_to_float(Ext a)
{
    if (a.cls == EXT_ZERO)
        return a.s ? -0.0f : 0.0f;
    if (a.cls == EXT_INF)
        return a.s ? -INFINITY : INFINITY;
    if (a.cls == EXT_NAN)
        return NAN;
    uint64_t keep = a.m >> 40, rest = a.m & 0xFFFFFFFFFFull;
    int32_t e = a.e;
    if (rest > 0x8000000000ull || (rest == 0x8000000000ull && (keep & 1))) {
        if (++keep == (1ull << 24)) {
            keep >>= 1;
            e++;
        }
    }
    float f = (float)ldexp((double)keep, e - 23);
    return a.s ? -f : f;
}

/* value as stored by FSTP dword and reloaded by FLD dword */
static inline Ext ext_round_float(Ext a)
{
    return ext_from_float(ext_to_float(a));
}

#endif
