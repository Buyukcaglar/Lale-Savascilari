/*
 * oses.c - ALTDUZEY\OSES.L (IST.EXE ovl55CC): sound set-up from SES.CFG and
 * the sample loader.  See oses.h.
 */
#include "engine/oses.h"

#include <stdlib.h>
#include <string.h>

#include "dsmi/amp.h"
#include "dsmi/cdi.h"
#include "dsmi/dds.h"
#include "dsmi/dsmitime.h"
#include "dsmi/emsheap.h"
#include "dsmi/mcp.h"
#include "dsmi/timer.h"
#include "engine/fileman.h"
#include "engine/memman.h"
#include "engine/nses.h"
#include "engine/systems.h"
#include "engine/vfs.h"
#include "platform/audio.h"
#include "platform/pc.h"
#include "rtl/bc_farheap.h"

OsesSdiInitFn oses_sdi_init_device;
OsesSdiVoidFn oses_sdi_start_output;
OsesSdiVoidFn oses_sdi_stop_output;

/*
 * oses_init - sound set-up.
 *
 * SES.CFG longer than 2 bytes: driver name (13 bytes), SDI config (5Ch ->
 * E7EC), a word (-> 8DDA).  With probe != 0 that is all (returns EEh).
 * Otherwise: flag 750A bit 3, dsmiTimeInit(100) + its 100 Hz routine, the
 * NSES hooks MCPSampleRealAddress / MCPEnable/DisableVirtualSamples (a
 * failure returns 3), cdiInit, GetEnvString / SetVector, DDSLoadDriver,
 * SDIInitDevice(E7EC) (result ignored), SDIStartOutput, ampInit(0), the AMP
 * routine at 100 Hz and - if SDI flags bit 7 - the mixer poll at 40 Hz,
 * cdiSetupChannels(channel count), sound on.  Returns FFh.
 * SES.CFG of 2 bytes or less: only the word at 8DDA is read, sound off,
 * flag 750A bit 3, returns FFh.  No SES.CFG: 1.
 *
 * Port: SDISB16.DSD is bound to the native MCP driver before the load, and
 * the emulated DMA starts with SDIStartOutput (see oses.h, audio.h).
 *
 * Original: IST.EXE 55CC:0000 (OSES.L), far cdecl (int probe).
 * Verified: tests/py/test_sound.py (unicorn differential with the driver
 * load stubbed on the original side), 2026-11-20.
 */
int16_t oses_init(int16_t probe)
{
    char drv[13];                              /* [bp-12h] */
    int16_t fd;
    uint32_t len;
    DdsFn f;

    fd = vfs_open(DGSTR(OSES_STR_CFG), 2);
    if (fd == -1)
        return 1;
    len = (uint32_t)fm_filelength(fd);
    if (len > 2) {                             /* OR DX,DX / JA / CMP AX,2 / JBE */
        vfs_read(fd, drv, 13);
        vfs_read(fd, g_sdi_config, 0x5C);
        vfs_read(fd, DGP(0x8DDA), 2);
        vfs_close(fd);
        if (probe != 0)
            return 0xEE;
    } else {
        vfs_read(fd, DGP(0x8DDA), 2);
        vfs_close(fd);
        g_sound_on = 0;
        g_sys_flags |= 8;
        return 0xFF;
    }

    g_sys_flags |= 8;
    dsmiTimeInit(100);
    tsAddRoutine(dsmiTimeISR, 0x2E9B);
    if (DDSRegisterFunction(DGSTR(0x8DE4), (DdsFn)snd_ems_map_sample) != 0)   /* MCPSampleRealAddress */
        return 3;
    if (DDSRegisterFunction(DGSTR(0x8DF9), (DdsFn)snd_ems_save_map) != 0)     /* MCPEnableVirtualSamples */
        return 3;
    if (DDSRegisterFunction(DGSTR(0x8E11), (DdsFn)snd_ems_restore_map) != 0)  /* MCPDisableVirtualSamples */
        return 3;
    cdiInit();
    DDSRegisterFunction(DGSTR(0x8E2A), (DdsFn)snd_getenv_hook);              /* GetEnvString */
    DDSRegisterFunction(DGSTR(0x8E37), (DdsFn)snd_setvect_hook);             /* SetVector */

    /* port: the .DSD's code is the native MCP driver (oses.h) */
    dds_register_native_driver("SDISB16.DSD", sdisb16_entry);
    {
        uint32_t handle = g_oses_driver;
        int16_t r = DDSLoadDriver(drv, &handle);
        g_oses_driver = handle;
        if (r != 0)
            return 3;
    }
    if (DDSAccessFunction(DGSTR(0x8E41), &f) != 0)                           /* SDIInitDevice */
        return 3;
    oses_sdi_init_device = (OsesSdiInitFn)f;
    if (DDSAccessFunction(DGSTR(0x8E4F), &f) != 0)                           /* SDIStartOutput */
        return 3;
    oses_sdi_start_output = (OsesSdiVoidFn)f;
    if (DDSAccessFunction(DGSTR(0x8E5E), &f) != 0)                           /* SDIStopOutput */
        return 3;
    oses_sdi_stop_output = (OsesSdiVoidFn)f;
    oses_sdi_init_device(g_sdi_config);        /* result ignored */
    oses_sdi_start_output();
    {   /* port: the SB16 DMA starts playing the ring now */
        McpConfig c;
        if (mcp_config_from_sdi(g_sdi_config, &c) == 0)
            audio_dma_start(c.rate, c.sample_size, c.stereo, mcp_buffer_size());
    }
    if (ampInit(0) != 0)
        return 3;
    tsAddRoutine(snd_timer_100hz, 0x2E9B);
    if (g_sdi_flags & 0x80)
        tsAddRoutine(snd_timer_40hz, 0x7485);
    cdiSetupChannels(0, g_snd_channels);
    g_sound_on = 1;
    return 0xFF;
}

/* far pointer offset of a native data pointer (EMS frame: E000:off) */
static uint16_t far_off(const uint8_t *p)
{
    if (p >= pc_ems_frame && p < pc_ems_frame + sizeof pc_ems_frame)
        return (uint16_t)(p - pc_ems_frame);
    return 0;
}

/*
 * oses_load_sample - load a sample file into the slot table and announce it
 * to the device (cdiDownloadSample(0, sample, sample, file length)).
 *
 *   type 0: one conventional block of the file's size (allocated before the
 *           loop); with flags == 0 the whole file goes into it.
 *   type 1: EMS blocks of at most FF00h bytes (minus the in-page offset of
 *           the mapping); with flags bit 1 the file continues in the next
 *           slots (up to slot 30h), each one marked 200h in the sound table
 *           except the last.  Without bit 1 only the first piece is loaded.
 *   flags bit 0: each piece is stored reversed (read backwards from the end
 *           of the file in 2000h-byte chunks); sound table flag 100h.
 * Each slot: sample, length = piece, loopstart/loopend = the arguments,
 * mode 0.  The first slot's SAMPLEINFO head is cleared first.
 *
 * Returns 0 (also when the sound is off), 1 cannot open, 2 out of memory,
 * 3 read error.  Error returns inside the loop leave the file open and the
 * MEMMAN lock taken, like the original.
 *
 * Quirks kept: the reversed piece length is the low word of the piece size;
 * the file offset advances by the full piece capacity even when less was
 * read; type 0 with flags bit 1 would continue into slots without a block
 * (the original then reads through a null handle - all callers use type 1,
 * the port stops there instead).
 *
 * Original: IST.EXE 55CC:01F3 (OSES.L), far cdecl (unsigned char type,
 * char far *name, int slot, unsigned char flags, long loopstart, long loopend).
 * Verified: tests/py/test_sound.py (unicorn differential, real .PUR files),
 * 2026-11-20.
 */
int16_t oses_load_sample(uint8_t type, const char *name, int16_t slot, uint8_t flags,
                         int32_t loopstart, int32_t loopend)
{
    uint32_t fileoff = 0;                      /* [bp-1Ah] */
    uint32_t cap = 0xFF00;                     /* [bp-20h] */
    uint32_t remaining, total;                 /* [bp-06h], [bp-0Ah] */
    uint16_t inpage = 0;                       /* [bp-1Ch] */
    uint8_t *data = NULL;                      /* [bp-16h] */
    int16_t fd;
    MmHandle *h;

    if ((int8_t)g_sound_on == 0)
        return 0;
    snd_slot_clear(slot);
    fd = vfs_open(name, 2);
    if (fd == -1)
        return 1;
    remaining = total = (uint32_t)fm_filelength(fd);
    vfs_close(fd);
    if (type == 0) {
        if (mm_ref(SND_SLOT_MM_REF(slot)))
            o559A_fatal(DGSTR(0x8E6C), 0x131, NULL, 0x1E);
        h = mm_alloc(remaining, type);
        mm_ref_set(SND_SLOT_MM_REF(slot), h);
        if (!h)
            return 2;
    }
    fd = vfs_open(name, 2);
    if (fd == -1)
        return 1;
    mm_ems_lock(g_blit_lock, 0x138, DGSTR(0x8E7E));

    do {
        uint32_t piece;
        if (type == 0) {
            h = mm_ref(SND_SLOT_MM_REF(slot));
            if (!h)
                break;                         /* port: see the header comment */
            data = mm_conv_ptr(h) + fileoff;   /* huge pointer add (F_PADD) */
            if (flags == 0)
                cap = remaining;
            inpage = 0;
        } else if (type == 1) {
            if (mm_ref(SND_SLOT_MM_REF(slot)))
                o559A_fatal(DGSTR(0x8E90), 0x142, NULL, 0x1F);
            h = mm_alloc(remaining > cap ? cap : remaining, type);
            mm_ref_set(SND_SLOT_MM_REF(slot), h);
            if (!h)
                return 2;
            data = EMSLock((int16_t)h->ems, 0, (uint16_t)cap);
            if (!data)
                o559A_fatal(DGSTR(0x8EA2), 0x144, NULL, 0x20);
            inpage = far_off(data) & 0x3FFF;
            if (cap - inpage > remaining)
                cap = remaining + inpage;
        } else {
            o559A_fatal(DGSTR(0x8EB4), 0x148, NULL, 0x21);
            /* o559A_fatal does not return in the game; the original would
               go on with an uninitialised data pointer - the port stops */
            break;
        }

        piece = (cap - inpage < remaining) ? cap - inpage : remaining;
        if (flags & 1) {
            uint8_t *tmp = mm_farmalloc(0x2000);
            uint16_t n, c;
            if (!tmp)
                return 2;
            g_snd_table_flags(slot) |= 0x100;
            n = (uint16_t)piece;               /* AX only */
            vfs_seek(fd, (int32_t)(total - (fileoff + n)), 0);
            do {
                c = n < 0x2000 ? n : 0x2000;
                if (vfs_read(fd, tmp, c) == -1)
                    return 3;
                {   /* LES SI,tmp / LDS DI,data / DI += n - 1; copy downwards.
                       DI is a 16-bit offset: inside the EMS frame it wraps
                       like the original (c == 0 means 65536 bytes there). */
                    uint16_t di = (uint16_t)(far_off(data) + n - 1);
                    const uint8_t *tseg = tmp - 4;   /* tmp is seg:0004: SI wraps in its segment */
                    uint8_t *base = data - far_off(data);
                    uint32_t cnt = c ? c : 0x10000u;
                    int in_frame = data >= pc_ems_frame && data < pc_ems_frame + sizeof pc_ems_frame;
                    if (!in_frame && c == 0)
                        cnt = 0;               /* port: conventional, unreachable */
                    for (uint32_t i = 0; i < cnt; i++) {
                        if (in_frame) {
                            /* beyond 2000h: whatever the heap holds (memory
                               past A000:0000, above the arena: the port reads 0) */
                            const uint8_t *q = tseg + (uint16_t)(4 + i);
                            base[di] = q < bc_fh_arena + BC_FH_ARENA_SIZE ? *q : 0;
                        } else {
                            data[(uint16_t)(n - 1 - i)] = tmp[i];
                        }
                        di--;
                    }
                }
                n = (uint16_t)(n - c);
            } while (n != 0);                  /* JA */
            bc_farfree(tmp);                   /* 55CC:0530 farfree */
        } else {
            if (vfs_read(fd, data, (int32_t)piece) == -1)
                return 3;
        }
        fileoff += cap - inpage;

        if (type == 0) {
            /* sample = *(far *)handle (native pointer beside it) */
            memcpy(DGP(SND_SI_OFF(slot)), h, 4);
            if (slot >= 0 && slot < SND_SLOTS)
                snd_slot_data[slot] = mm_conv_ptr(h);
            g_snd_si_id(slot) = 0;
        } else if (type == 1) {
            g_snd_si_ptr_seg(slot) = 0xFFFF;
            g_snd_si_ptr_off(slot) = h->ems;
            if (slot >= 0 && slot < SND_SLOTS)
                snd_slot_data[slot] = NULL;
            g_snd_si_id(slot) = h->ems;
            g_snd_table_flags(slot) |= 0x200;
        } else {
            o559A_fatal(DGSTR(0x8EC6), 0x172, NULL, 0x22);
        }
        g_snd_si_length(slot) = piece;
        g_snd_si_loopstart(slot) = (uint32_t)loopstart;
        g_snd_si_loopend(slot) = (uint32_t)loopend;
        g_snd_si_mode(slot) = 0;
        {
            CdiSampleInfo *si = snd_sampleinfo(slot);
            /* the original passes the SAMPLEINFO's far pointer twice; for an
               EMS sample (FFFF:handle) it is no address - the port passes NULL */
            cdiDownloadSample(0, si->sample.in_ems ? NULL : si->sample.ptr, si->sample, total);
        }
        slot++;
        remaining -= cap - inpage;
    } while ((int32_t)remaining > 0 && (flags & 2) && slot < 0x31);

    g_snd_table_flags(slot - 1) &= 0xFDFF;     /* 22D9 + slot*11h */
    vfs_close(fd);
    mm_ems_unlock(g_blit_lock);
    return 0;
}
