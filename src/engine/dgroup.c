/* dgroup.c - storage for the original data segment (see dgroup.h). */
#include "engine/dgroup.h"

#include <string.h>

uint8_t dg[0x10000];

void dg_reset(void)
{
    memset(dg, 0, sizeof dg);
    memcpy(dg, dg_init, dg_init_size);
}
