/*
 * nses.c - ALTDUZEY\NSES.L (IST.EXE seg0D42): DSMI hooks, the two sound
 * timer routines and the sample slot table.  See nses.h.
 */
#include "engine/nses.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dsmi/amp.h"
#include "dsmi/cdi.h"
#include "dsmi/emsheap.h"
#include "engine/fileman.h"
#include "engine/nkontrol.h"
#include "engine/systems.h"
#include "engine/vfs.h"
#include "platform/audio.h"
#include "platform/pc.h"

uint8_t *snd_slot_data[SND_SLOTS];

/* SetVector bookkeeping (no interrupt vectors in the port, see below). */
static void *nses_vectors[256];
static CdiSampleInfo nses_si[SND_SLOTS];

void nses_reset(void)
{
    memset(snd_slot_data, 0, sizeof snd_slot_data);
    memset(nses_vectors, 0, sizeof nses_vectors);
    memset(nses_si, 0, sizeof nses_si);
}

static int slot_ok(int16_t slot) { return slot >= 0 && slot < SND_SLOTS; }

void snd_slot_clear(int16_t slot)
{
    memset(DGP(SND_SI_OFF(slot)), 0, SND_SI_CLEAR_BYTES);
    if (slot_ok(slot))
        snd_slot_data[slot] = NULL;
}

CdiSampleInfo *snd_sampleinfo(int16_t slot)
{
    static CdiSampleInfo junk;
    CdiSampleInfo *si = slot_ok(slot) ? &nses_si[slot] : &junk;
    uint16_t o = SND_SI_OFF(slot);
    memset(si, 0, sizeof *si);
    if (DG16(o + 2) == 0xFFFF) {                 /* FFFF:handle = DSMI EMS sample */
        si->sample.in_ems = 1;
        si->sample.ems_handle = DG16(o);
    } else {
        si->sample.ptr = slot_ok(slot) ? snd_slot_data[slot] : NULL;
    }
    si->length    = DG32(o + 0x04);
    si->loopstart = DG32(o + 0x08);
    si->loopend   = DG32(o + 0x0C);
    si->mode      = DG8(o + 0x10);
    si->sampleID  = DG16(o + 0x11);
    /* +13..+19 overlap the next slot (see nses.h); passed on as they are */
    si->rate      = DG16(o + 0x13);
    si->freq      = DG32(o + 0x15);
    si->volume    = DG8(o + 0x19);
    return si;
}

/* native pointer -> offset part of the original far pointer (EMS frame
   pointers: E000:off; anything else: 0, i.e. a normalised pointer) */
static uint16_t far_off(const uint8_t *p)
{
    if (p >= pc_ems_frame && p < pc_ems_frame + sizeof pc_ems_frame)
        return (uint16_t)(p - pc_ems_frame);
    return 0;
}

/*
 * snd_ems_map_sample - "MCPSampleRealAddress": address of an EMS sample.
 * Maps the whole block (EMSLock(handle, 0, FFFFh)) and returns its start;
 * pos is ignored (the original even zeroes it on the stack).  NULL when
 * MEMMAN has no EMS heap.
 *
 * Original: IST.EXE 0D42:0005 (NSES.L), far pascal (long handle, long pos).
 * Verified: tests/py/test_sound.py (unicorn differential), 2026-11-20.
 */
uint8_t *snd_ems_map_sample(uint32_t handle, uint32_t pos)
{
    (void)pos;
    if (g_mm_ems_status == 0)                  /* 6E02 | 6E04 */
        return NULL;
    return EMSLock((int16_t)handle, 0, 0xFFFF);
}

/*
 * snd_ems_save_map - "MCPEnableVirtualSamples": EMSSaveState() when MEMMAN
 * has an EMS heap.
 *
 * Original: IST.EXE 0D42:0041 (NSES.L), far.
 * Verified: tests/py/test_sound.py (unicorn differential), 2026-11-20.
 */
void snd_ems_save_map(void)
{
    if (g_mm_ems_status != 0)
        EMSSaveState();
}

/*
 * snd_ems_restore_map - "MCPDisableVirtualSamples": EMSRestoreState().
 *
 * Original: IST.EXE 0D42:005E (NSES.L), far.
 * Verified: tests/py/test_sound.py (unicorn differential), 2026-11-20.
 */
void snd_ems_restore_map(void)
{
    if (g_mm_ems_status != 0)
        EMSRestoreState();
}

/*
 * snd_identity_addr - unreferenced.  Despite its look (it copies the second
 * argument over the first one on the stack) it returns 0:0: a "real
 * address" hook for conventional samples that was never finished.
 *
 * Original: IST.EXE 0D42:007B (NSES.L), far pascal (long, long).
 * Verified: tests/py/test_sound.py (unicorn differential), 2026-11-20.
 */
uint32_t snd_identity_addr(uint32_t a, uint32_t b)
{
    (void)a; (void)b;
    return 0;
}

/*
 * snd_timer_40hz - Timer Service routine (divisor 7485h = 40.0 Hz, added by
 * oses_init when SES.CFG flags bit 7 is set): drives the MCP mixer.
 * Guarded against re-entry by 73D8; saves the EMS page map around cdiPoll
 * (the mixer maps EMS samples into the frame).
 *
 * Port: audio_dma_sync() first - the "DMA" advances to the clock of this
 * IRQ, so the poll reads the position the SB16 would report (audio.h).
 *
 * Original: IST.EXE 0D42:0093 (NSES.L), interrupt routine (IRET).
 * Verified: tests/py/test_sound.py (guard + calls, unicorn differential).
 */
void snd_timer_40hz(void)
{
    if (g_snd_poll_busy != 0)
        return;
    g_snd_poll_busy = 1;
    EMSSaveState();
    audio_dma_sync();                          /* port: DMA position = now */
    cdiPoll(0);
    EMSRestoreState();
    g_snd_poll_busy = 0;
}

/*
 * snd_getenv_hook - "GetEnvString": getenv(name).
 *
 * The DOS environment of the original (BLASTER=...) has no equivalent; the
 * host environment is used.  No port driver asks for it (the native
 * SDISB16 takes everything from SES.CFG).
 *
 * Original: IST.EXE 0D42:00CF (NSES.L), far pascal (char far *).
 * Verified: no (host environment).
 */
const char *snd_getenv_hook(const char *name)
{
    return getenv(name);
}

/*
 * snd_setvect_hook - "SetVector": install isr at intno, return the old one
 * (_dos_getvect / _dos_setvect).
 *
 * The port has no interrupt vector table: the native drivers do not hook
 * IRQs.  The hook keeps a private table so that get/set pairs behave
 * (NULL = the vector the game started with).
 *
 * Original: IST.EXE 0D42:00EB (NSES.L), far pascal (long intno, far isr).
 * Verified: no (no IVT in the port).
 */
void *snd_setvect_hook(int32_t intno, void *isr)
{
    uint8_t n = (uint8_t)intno;               /* INT 21h AH=35h/25h take AL */
    void *old = nses_vectors[n];
    nses_vectors[n] = isr;
    return old;
}

/*
 * snd_timer_100hz - Timer Service routine (divisor 2E9Bh = 100 Hz): the AMP
 * module player, guarded against re-entry by 73DB.
 *
 * Original: IST.EXE 0D42:0120 (NSES.L), interrupt routine (IRET); calls
 * ampInterrupt (0EF6:0F4C) with PUSHF + CALLF.
 * Verified: tests/py/test_sound.py (guard + call, unicorn differential).
 */
void snd_timer_100hz(void)
{
    if (g_snd_amp_busy != 0)
        return;
    g_snd_amp_busy = 1;
    ampInterrupt();
    g_snd_amp_busy = 0;
}

/*
 * snd_play_sample - play slot `slot` of the sample table on channel ch:
 * cdiSetSample, cdiSetPanning, cdiPlaySample(freq, volume).  Nothing when
 * the sound is off.  A negative frequency is fatal ("İc hata! ...").
 *
 * Original: IST.EXE 0D42:014F (NSES.L), far cdecl (int slot, long freq,
 * long volume, long pan, unsigned char ch).
 * Verified: tests/py/test_sound.py (unicorn differential), 2026-11-20.
 */
void snd_play_sample(int16_t slot, int32_t freq, int32_t volume, int32_t pan, uint8_t ch)
{
    char buf[0x68];
    if ((int8_t)g_sound_on == 0)               /* CBW / OR AX,AX */
        return;
    if (freq < 0) {
        /* sprintf(buf, "İc hata!\n%d. kanaldan %d. s_membuffer'daki sample
           %lu frekans ve %lu volüm ile çalınayazdı.", ch, slot, freq, vol) */
        snprintf(buf, sizeof buf, DGSTR(SND_STR_PLAY_ERR), (int)ch, (int)slot,
                 (unsigned long)(uint32_t)freq, (unsigned long)(uint32_t)volume);
        o559A_fatal(DGSTR(0x743A), 0x120, buf, -1);
    }
    cdiSetSample(ch, snd_sampleinfo(slot));
    cdiSetPanning(ch, pan);
    cdiPlaySample(ch, (uint32_t)freq, (uint32_t)volume);
}

/*
 * snd_load_sound - play a long sample file by streaming ("playlongsample"):
 * a 60000-byte looping sample in slot 30h is played on channel ch while the
 * file is read into its halves (30000 bytes each) behind the play position,
 * which is watched on the LAST channel (g_snd_channels - 1; the callers
 * pass that channel).  A key press ends the streaming early (the key stays
 * in the buffer).  Then waits until the play position reaches the write
 * position, stops the channel, closes the file and frees the slot.
 *
 * Quirks kept: the read pointer is data + low word of the write position
 * (16-bit offset arithmetic); a read result of 0 counts as 30000 bytes; the
 * stream ends once more than filelength - 4 bytes were read; the final
 * wait compares unsigned 32-bit and can wait forever if the position never
 * gets there (the original spins on the IRQ-driven position - the port
 * keeps time running with pc_poll()).
 *
 * Original: IST.EXE 0D42:01F0 (NSES.L), far cdecl (char far *name, long
 * freq, long volume, long pan, unsigned char ch).
 * Verified: tests/py/test_sound.py (unicorn differential with a scripted
 * play position), 2026-11-20.
 */
void snd_load_sound(const char *name, int32_t freq, int32_t volume, int32_t pan, uint8_t ch)
{
    char buf[0x84];
    int16_t fd;
    int32_t flen = 0;              /* uninitialised in the original if the open fails */
    uint8_t *data;                 /* [bp-6] far ptr of the slot 30h block */
    int32_t wpos, total, lo, hi, pos;
    int16_t half;

    if ((int8_t)g_sound_on == 0)
        return;
    fd = vfs_open(name, 2);
    if (fd != -1) {
        flen = fm_filelength(fd);
    } else {
        snprintf(buf, sizeof buf, DGSTR(SND_STR_NOTFOUND), name);
        o559A_fatal(DGSTR(0x7479), 0x139, buf, -1);
    }
    snd_load_sample_ems(0, 0x30, 0x7530, 2);
    data = mm_ref(SND_SLOT_MM_REF(0x30)) ? mm_conv_ptr(mm_ref(SND_SLOT_MM_REF(0x30))) : NULL;
    if (vfs_read(fd, data, 0x7530) == -1)
        o559A_fatal(DGSTR(0x748B), 0x13D, NULL, 0x23);
    wpos = 0x7530;
    total = 0x7530;
    snd_play_sample(0x30, freq, volume, pan, ch);

    half = 1;
    lo = (int32_t)half * 0x7530;
    hi = (int32_t)(half + 1) * 0x7530;
    for (;;) {
        pos = cdiGetPosition((int16_t)(g_snd_channels - 1));
        if (pos < lo || pos > hi) {            /* the other half is free: refill it */
            uint16_t r = (uint16_t)vfs_read(fd, data ? data + (uint16_t)wpos : NULL, 0x7530);
            if (r == 0)
                r = 0x7530;
            wpos += (int16_t)r;
            total += (int16_t)r;
            if (flen - 4 < total)
                break;
            if (wpos > 0xEA5C)                 /* 59996 */
                wpos = 0;
            if (++half == 2)
                half = 0;
            lo = (int32_t)half * 0x7530;
            hi = (int32_t)(half + 1) * 0x7530;
        }
        if (pc_kbhit())
            break;
    }
    do {
        pos = cdiGetPosition((int16_t)(g_snd_channels - 1));
        if ((uint32_t)pos >= (uint32_t)wpos)
            break;
        pc_poll();                             /* port: the original waits for the IRQs */
    } while (1);
    cdiStopChannel((int16_t)(g_snd_channels - 1));
    vfs_close(fd);
    ctl_free_checked(SND_SLOT_MM_REF(0x30), 0x153, DGSTR(0x749D));
    if (!mm_ref(SND_SLOT_MM_REF(0x30)))
        snd_slot_data[0x30] = NULL;            /* port: native half of the freed block */
}

/*
 * snd_load_sample_ems - allocate the block of a sample slot (count * unit
 * bytes, 16-bit product) and fill in its SAMPLEINFO; no data is loaded.
 *   type 0: conventional block, sample = its far pointer, sampleID 0;
 *   type 1: EMS block of at most FF00h bytes, sample = FFFF:handle,
 *           sampleID = handle; length = min(FF00h - in-page offset, size);
 *   other : fatal 27h (and the SAMPLEINFO is still completed).
 * loopstart 0, loopend = size (the sample loops), mode 0.
 * Returns 0 (also when the sound is off), 2 = out of memory (the MEMMAN
 * lock taken at the start is not released then).  A slot already in use is
 * fatal (24h/25h).
 *
 * Original: IST.EXE 0D42:044D (NSES.L), far cdecl (unsigned char type,
 * int slot, int count, int unit).
 * Verified: tests/py/test_sound.py (unicorn differential), 2026-11-20.
 */
int16_t snd_load_sample_ems(uint8_t type, int16_t slot, int16_t count, int16_t unit)
{
    uint32_t cap = 0xFF00;                     /* [bp-0A] */
    uint32_t size = (uint16_t)(count * unit);  /* IMUL, low word, high word 0 */
    uint16_t inpage = 0;                       /* [bp-06] */
    uint16_t ref = SND_SLOT_MM_REF(slot);
    MmHandle *h;

    if ((int8_t)g_sound_on == 0)
        return 0;
    mm_ems_lock(g_blit_lock, 0x160, DGSTR(0x74AF));
    snd_slot_clear(slot);
    if (type == 0) {
        if (mm_ref(ref))
            o559A_fatal(DGSTR(0x74C1), 0x166, NULL, 0x24);
        h = mm_alloc(size, type);
        mm_ref_set(ref, h);
        if (!h)
            return 2;
        /* sample = *(far *)handle: the record's first dword (native pointer
           beside it) */
        memcpy(DGP(SND_SI_OFF(slot)), h, 4);
        if (slot_ok(slot))
            snd_slot_data[slot] = mm_conv_ptr(h);
        g_snd_si_id(slot) = 0;
    } else if (type == 1) {
        uint8_t *p;
        if (mm_ref(ref))
            o559A_fatal(DGSTR(0x74D3), 0x16F, NULL, 0x25);
        h = mm_alloc(size > cap ? cap : size, type);
        mm_ref_set(ref, h);
        if (!h)
            return 2;
        p = EMSLock((int16_t)h->ems, 0, (uint16_t)cap);
        if (!p)
            o559A_fatal(DGSTR(0x74E5), 0x171, NULL, 0x26);
        inpage = far_off(p) & 0x3FFF;
        if (cap - inpage > size)
            cap = size + inpage;
        g_snd_si_ptr_seg(slot) = 0xFFFF;
        g_snd_si_ptr_off(slot) = h->ems;
        g_snd_si_id(slot) = h->ems;
    } else {
        o559A_fatal(DGSTR(0x74F7), 0x177, NULL, 0x27);
    }
    g_snd_si_length(slot) = (cap - inpage < size) ? cap - inpage : size;
    g_snd_si_loopstart(slot) = 0;
    g_snd_si_loopend(slot) = size;
    g_snd_si_mode(slot) = 0;
    mm_ems_unlock(g_blit_lock);
    return 0;
}
