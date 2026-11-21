/*
 * wp17b_stubs.c - TEST ONLY stand-ins for routines the ISOSYS port (WP17b
 * build: iso_util/map/turn/draw/combat/ai/field/spell) calls and that a
 * test wants to see called instead of running (none at the moment: the
 * shared build now has the whole of ISOSYS, tests/stubs/iso_pending.c
 * holds what is not decompiled yet).  Not part of the game build.
 *
 * Each call is appended to wp17b_log as (id, arg0, arg1, arg2) words, read
 * by tests/py/test_iso_b.py and compared with the original's stub calls;
 * fr_flag_test answers from wp17b_flags.
 */
#include <stdint.h>

#include "game/iso.h"

int16_t wp17b_log[4 * 512];
int32_t wp17b_nlog;

void wp17b_logc(int16_t id, int16_t a, int16_t b, int16_t c)
{
    if (wp17b_nlog < 512) {
        int16_t *p = &wp17b_log[4 * wp17b_nlog++];
        p[0] = id; p[1] = a; p[2] = b; p[3] = c;
    }
}

