/*
 * wp12_anim_stubs.c - test support for tests/py/test_q3dview_anim.py: a
 * host pump that advances the 100 Hz clock by a fixed amount at every
 * pc_poll (the animations' only clock source; the test stubs
 * ctl_flush_keys of the original the same way).
 */
#include <stdint.h>

#include "engine/nkontrol.h"

uint32_t wp12_anim_clock_step;

void wp12_anim_pump(void)
{
    g_clock_ms += wp12_anim_clock_step;
}
