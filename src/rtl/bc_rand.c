/* bc_rand.c - Borland C++ 3.x rand()/srand().  Verified: tests/py/test_rtl.py. */
#include "rtl/bc_rand.h"

/* IST.EXE 0000:1768 - srand(seed): the seed's high word is cleared. */
void bc_srand(uint16_t seed)
{
    bc_rand_seed = seed;
}

/* IST.EXE 0000:1779 - seed = seed * 015A4E35h + 1; return bits 16..30. */
int16_t bc_rand(void)
{
    bc_rand_seed = bc_rand_seed * 0x015A4E35u + 1u;
    return (int16_t)((bc_rand_seed >> 16) & 0x7FFF);
}
