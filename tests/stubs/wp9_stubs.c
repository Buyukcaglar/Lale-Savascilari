/*
 * wp9_stubs.c - TEST ONLY stand-ins for tests/py/test_gfl.py (WP9: GFL
 * player).  Not part of the game build.  The emulator side stubs the same
 * routines with the same behaviour (test_gfl.py).
 *
 *  NGRAPHS, NSES, CDI and AMP are the real ports (CDI calls recorded by
 *  the wp5 device; palettes compared through the emulated VGA DAC).
 *  - wp9_pump: host pump for pc_poll() (scripted clock and keys).
 */
#include <stdint.h>

#include "engine/dgroup.h"
#include "engine/nkontrol.h"
#include "platform/pc.h"

/* ---- scripted time and keys ------------------------------------------------- */
/* Every pc_poll() runs the pump: the clock advances by wp9_steps[polls & 255]
   ms, and at poll wp9_key_poll the key wp9_key is pushed. */
int32_t  wp9_steps[256];
int32_t  wp9_polls;
int32_t  wp9_key_poll = -1;
uint16_t wp9_key;

static void wp9_pump(void)
{
    g_clock_ms += (uint32_t)wp9_steps[wp9_polls & 255];
    if (wp9_polls == wp9_key_poll)
        pc_key_push(wp9_key);
    wp9_polls++;
}

void wp9_setup(void)
{
    wp9_polls = 0;
    pc_set_virtual_time(1, 0);
    pc_set_host_pump(wp9_pump);
}
