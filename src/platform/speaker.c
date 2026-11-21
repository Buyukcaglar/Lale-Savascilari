/*
 * speaker.c - PC speaker state and Borland sound()/nosound()/delay().
 * See speaker.h.
 *
 * Verified: no differential test (o559A_beep, the only user, is
 * unreferenced); delay() is a transcription of 0000:2321 over a model of
 * PIT channel 0 in mode 3 (the mode DSMI's tsSetPIT and the BIOS use).
 */
#include "platform/speaker.h"

#include "platform/pc.h"

PcSpeaker pc_speaker;

/*
 * bc_sound - speaker on at freq Hz: divisor 1234DDh / freq to PIT channel
 * 2 (mode 3 command B6h only when the speaker was off), port 61h |= 3.
 * Frequencies up to 18 Hz (divisor would not fit) are ignored.
 * Original: IST.EXE 0000:33EB (Borland RTL), far cdecl (unsigned).
 */
void bc_sound(uint16_t freq)
{
    if (freq <= 0x12)
        return;
    pc_speaker.divisor = (uint16_t)(0x1234DDu / freq);
    pc_speaker.port61 |= 3;
}

/* bc_nosound - port 61h &= ~3.  Original: IST.EXE 0000:3417. */
void bc_nosound(void)
{
    pc_speaker.port61 &= (uint8_t)~3;
}

/*
 * bc_readtimer - latch and read PIT channel 0, inverted (so it counts up).
 * Mode 3: the count runs down from the (even part of the) divisor by 2 per
 * input clock, twice per period.
 * Original: IST.EXE 0000:22E6 (Borland RTL), near.
 */
uint16_t bc_readtimer(void)
{
    uint32_t n, ph, half, cnt;
    pc_poll();                                      /* time passes while polling */
    n = pc_pit_divisor() ? pc_pit_divisor() : 0x10000u;
    ph = (uint32_t)(pc_pit_ticks() % n);
    half = (n + 1) / 2;
    if (ph >= half)
        ph -= half;
    cnt = (n & ~1u) - 2 * ph;
    return (uint16_t)~cnt;
}

/*
 * bc_delay - wait ms milliseconds: ms * counts_per_ms PIT counts, where
 * every wrap of the 16-bit reading counts 10000h (correct only for divisor
 * 0 = 65536; with DSMI's 100 Hz divisor 11931 delays get much shorter -
 * kept, it is what the original does).
 * Original: IST.EXE 0000:2321 (Borland RTL), far cdecl (unsigned).
 */
void bc_delay(uint16_t ms)
{
    uint16_t last = bc_readtimer(), now;
    uint32_t target = last + bc_delay_counts_per_ms * ms;
    for (;;) {
        now = bc_readtimer();
        if ((target >> 16) == 0 && now >= (uint16_t)target)
            return;
        if (now < last) {                            /* wrapped */
            if ((target >> 16) == 0)
                return;
            target -= 0x10000u;
        }
        last = now;
    }
}
