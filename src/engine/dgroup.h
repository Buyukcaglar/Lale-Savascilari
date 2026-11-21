/*
 * dgroup.h - the original data segment (DGROUP, IST.EXE 16E0:0000-FFFF).
 *
 * All original global *data* (scalars, tables, strings, BSS) lives in one
 * 64 KB byte array at its original offsets.  Named globals are macros over
 * it, declared by the module that owns them, e.g. in nkontrol.h:
 *
 *     #define g_clock_ms  DG32(0xE74A)   // u32  100 Hz ms clock
 *
 * and used like ordinary variables (g_clock_ms += 10;).
 *
 * Why: layout-dependent behaviour (out-of-bounds reads of neighbours, tables
 * indexed past their end, struct overlays) stays exactly the original's, and
 * the differential tests copy DGROUP between the emulated original and the
 * port byte for byte.
 *
 * What is NOT in dg[]: far pointers to heap/EMS/VGA memory.  They are native
 * pointers in ordinary C globals next to the owning module's code, with a
 * comment naming their DGROUP address; the 4 bytes in dg[] stay unused.
 *
 * Accesses are little-endian and unaligned (both targets are LE: x86-64,
 * arm64).  The access types below are aligned(1) and may_alias so the
 * compiler emits safe unaligned loads (clang/gcc only - the project's
 * compilers on Windows and macOS).
 */
#ifndef LALE_ENGINE_DGROUP_H
#define LALE_ENGINE_DGROUP_H

#include <stdint.h>

extern uint8_t dg[0x10000];

typedef uint16_t dg_u16 __attribute__((aligned(1), may_alias));
typedef int16_t  dg_i16 __attribute__((aligned(1), may_alias));
typedef uint32_t dg_u32 __attribute__((aligned(1), may_alias));
typedef int32_t  dg_i32 __attribute__((aligned(1), may_alias));
typedef float    dg_f32 __attribute__((aligned(1), may_alias));
typedef double   dg_f64 __attribute__((aligned(1), may_alias));

#define DG8(o)   (*(uint8_t *)&dg[(uint16_t)(o)])
#define DGS8(o)  (*(int8_t *)&dg[(uint16_t)(o)])
#define DG16(o)  (*(dg_u16 *)&dg[(uint16_t)(o)])
#define DGS16(o) (*(dg_i16 *)&dg[(uint16_t)(o)])
#define DG32(o)  (*(dg_u32 *)&dg[(uint16_t)(o)])
#define DGS32(o) (*(dg_i32 *)&dg[(uint16_t)(o)])
#define DGF32(o) (*(dg_f32 *)&dg[(uint16_t)(o)])
#define DGF64(o) (*(dg_f64 *)&dg[(uint16_t)(o)])
/* pointer to bytes at a DGROUP offset (strings, tables, struct overlays) */
#define DGP(o)   (&dg[(uint16_t)(o)])
#define DGSTR(o) ((char *)&dg[(uint16_t)(o)])

/* 16-bit index arithmetic on DGROUP offsets wraps like the original DS:off */
static inline uint8_t *dg_ptr(uint16_t off) { return &dg[off]; }

/* Restore dg[] to the start-up image (initialised data + zeroed BSS). */
void dg_reset(void);

extern const uint8_t  dg_init[];
extern const uint16_t dg_init_size;
extern const uint16_t dg_farptr_offsets[];
extern const uint16_t dg_farptr_count;

#endif
