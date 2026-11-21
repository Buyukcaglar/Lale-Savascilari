/*
 * speaker.h - PC speaker (PIT channel 2 + port 61h bits 0-1) and the
 * Borland RTL routines that drive it: sound(), nosound(), delay().
 * Implementation: src/platform/speaker.c.
 *
 * The port keeps the speaker state (divisor, gate/enable bits) but makes no
 * sound: the only user is the unreferenced o559A_beep.  delay() keeps its
 * timing: it busy-waits on the emulated PIT channel 0 exactly like the
 * original (pc_poll() runs inside, so timer interrupts happen).
 */
#ifndef LALE_PLATFORM_SPEAKER_H
#define LALE_PLATFORM_SPEAKER_H

#include <stdint.h>

#include "engine/dgroup.h"

/* 16E0:957E u32  delay(): PIT counts per millisecond.  0952h (2 x 1193: mode 3
   counts down by 2) unless _initdelay saw an even count (mode 2) and set
   04A9h - with the BIOS's mode 3 it never does. */
#define bc_delay_counts_per_ms  DG32(0x957E)

typedef struct PcSpeaker {
    uint16_t divisor;   /* PIT channel 2 divisor (last written to port 42h) */
    uint8_t  port61;    /* bits 0 (timer 2 gate) and 1 (speaker data) */
} PcSpeaker;
extern PcSpeaker pc_speaker;

void     bc_sound(uint16_t freq);     /* 0000:33EB sound(): no-op for freq <= 18 */
void     bc_nosound(void);            /* 0000:3417 nosound() */
void     bc_delay(uint16_t ms);       /* 0000:2321 delay() */
uint16_t bc_readtimer(void);          /* 0000:22E6 inverted PIT channel 0 count */

#endif
