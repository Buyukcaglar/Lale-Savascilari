/*
 * wp17c_stubs.c - TEST ONLY stand-ins for the WP17b routines that
 * iso_battle calls (battlefield, AI, vecize effects), for tests that want
 * them out of the way (test_iso_c.py setup_stubs(stub=True); the scripted
 * battles run the real ones).  Not part of the game build.  They are NOT linked
 * under the real names: tests/py/test_iso_c.py redirects the real
 * functions to them with lale.patch() for the duration of a test, and
 * stubs the same addresses in the original with e.stub().
 *
 * Each call is appended to wp17c_log as (id, a, b, c) words and compared
 * with the original's stub calls.  wp17c_ai_kill: iso_ai_turn kills the
 * acting monster (hp 0, cell freed) so that scripted battles end.
 */
#include <stdint.h>

#include "game/iso.h"

int16_t wp17c_log[4 * 1024];
int32_t wp17c_nlog;
int16_t wp17c_ai_kill;

static void logc(int16_t id, int16_t a, int16_t b, int16_t c)
{
    if (wp17c_nlog < 1024) {
        int16_t *p = &wp17c_log[4 * wp17c_nlog++];
        p[0] = id; p[1] = a; p[2] = b; p[3] = c;
    }
}

void    wp17c_build_battlefield(void) { logc(2, 0, 0, 0); }
void    wp17c_cast_vecize(void) { logc(3, g_iso_actor, g_iso_target, g_iso_spell); }
int16_t wp17c_eval_param(int16_t v) { logc(4, v, 0, 0); return 0; }
void    wp17c_vecize_effect(void) { logc(5, g_iso_actor, g_iso_target, 0); }
void    wp17c_after_hit(void) { logc(6, g_iso_actor, g_iso_defender, 0); }

void wp17c_ai_turn(void)
{
    logc(7, g_iso_actor, iso_unit(g_iso_actor)->hp, 0);
    if (wp17c_ai_kill) {
        iso_unit(g_iso_actor)->hp = 0;
        g_iso_occ(iso_unit(g_iso_actor)->x, iso_unit(g_iso_actor)->y) = 0;
    }
}
