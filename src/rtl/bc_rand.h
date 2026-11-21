/* bc_rand.h - Borland C++ 3.x rand()/srand(), bit-exact.
 *
 * The game's random stream (combat initiative, loot, encounters) depends on the
 * exact Borland LCG, so the host C library's rand() is never used. */
#ifndef LALE_RTL_BC_RAND_H
#define LALE_RTL_BC_RAND_H

#include <stdint.h>

#include "engine/dgroup.h"

/* IST.EXE 16E0:94C4 (low word) / 16E0:94C6 (high word).  Initial value 1. */
#define bc_rand_seed DG32(0x94C4)

void    bc_srand(uint16_t seed);   /* IST.EXE 0000:1768 */
int16_t bc_rand(void);             /* IST.EXE 0000:1779, result 0..7FFFh */

/* Borland's random(n) macro from <stdlib.h>, inlined at every call site in the
 * original: (int)(((long)rand() * n) / 0x8000).  n is an int (16-bit). */
static inline int16_t bc_random(int16_t n)
{
    return (int16_t)(((int32_t)bc_rand() * n) / 0x8000);
}

#endif
