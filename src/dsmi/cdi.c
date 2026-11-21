/*
 * cdi.c - DSMI CDI channel distributor.  Original: IST.EXE seg0EAE (DSMI).
 * See cdi.h for the device contract.
 *
 * All wrappers have the same shape in the original:
 *
 *     ENTER; PUSH DS; DS = DGROUP
 *     MOV EBX,[ch]; MOVZX BX,byte [BX+7696]       ; device index (low word of ch)
 *     IMUL BX,68h; ADD BX,7796                     ; its function table
 *     PUSH <args>; CALLF [BX+slot]                 ; DX:AX passed back unchanged
 *     POP DS; LEAVE; RETF n
 *
 * so the device is chosen by dg[0x7696 + (uint16_t)ch] and the result is the
 * device's.  An unregistered channel maps to device 0 (the map is zeroed by
 * cdiInit); with no device registered the original calls a zeroed far pointer
 * (0000:0000, i.e. into the IVT).  The port returns 0 instead (cdi_dev()).
 *
 * Verified: tests/py/test_amp.py (the AMP call stream goes through these
 * wrappers on both sides; cdiInit/cdiRegister state compared in DGROUP).
 */
#include <string.h>

#include "dsmi/cdi.h"
#include "dsmi/dds.h"

/* 16E0:7796 + n*0x68 - the registered tables (far pointers -> native). */
CdiDevice cdi_devices[CDI_MAX_DEVICES];

static const CdiDevice cdi_null_device;     /* all slots NULL: see cdi_dev() */

/* device selected by a channel: byte at 7696 + low word of ch */
static const CdiDevice *cdi_dev(int32_t ch)
{
    uint8_t n = DG8(0x7696 + (uint16_t)ch);
    /* The original indexes 7796 + n*0x68 for any n; past the 8 slots that is
     * the count byte / AMP data - garbage function pointers.  Unreachable
     * with the one device the game registers. */
    return n < CDI_MAX_DEVICES ? &cdi_devices[n] : &cdi_null_device;
}

#define CDI_CALL(dev, slot, ...) ((dev)->slot ? (dev)->slot(__VA_ARGS__) : 0)

/*
 * cdiInit - clear the channel map and the first device table, set the magic,
 * export cdiRegister through DDS.
 *
 * Original: IST.EXE 0EAE:0006 (DSMI CDI), far.  Note: only the first 0x68
 * bytes of the device area (slot 0) are cleared, not all 8 slots.
 * Verified: tests/py/test_amp.py (DGROUP 7694..7AE2).
 */
void cdiInit(void)
{
    memset(DGP(0x7696), 0, 0x100);
    memset(&cdi_devices[0], 0, sizeof cdi_devices[0]);   /* 7796, 0x68 bytes */
    g_cdi_dev_count = 0;
    g_cdi_magic = 0xDA;
    DDSRegisterFunction(g_cdi_name_register, (DdsFn)cdiRegister);
}

/*
 * cdiRegister - register a device table; it gets channels n*32 .. n*32+31.
 * Returns the first channel, or -1 if cdiInit has not run.
 *
 * Original: IST.EXE 0EAE:0036 (DSMI CDI), far pascal (CDIDEVICE far *).
 * A 9th device would overwrite the count byte at 7AD6 in the original; the
 * port does not store tables beyond the 8 slots (unreachable in the game).
 * Verified: tests/py/test_amp.py.
 */
int16_t cdiRegister(const CdiDevice *dev)
{
    uint8_t n;

    if (g_cdi_magic != 0xDA)
        return -1;
    n = g_cdi_dev_count;
    for (int i = 0; i < 32; i++)                         /* REP STOSB, 32 x n */
        DG8(0x7696 + n * 32 + i) = n;
    if (n < CDI_MAX_DEVICES)
        cdi_devices[n] = *dev;                           /* REP MOVSB, 0x68 bytes */
    g_cdi_dev_count = (uint8_t)(n + 1);
    return (int16_t)(n << 5);
}

/* ---- the 26 dispatchers ------------------------------------------------------
 * Original addresses in seg0EAE (far pascal).  Verified: tests/py/test_amp.py
 * (the ones AMP uses through playback, the others in test_cdi_dispatchers;
 * cdiGetSample is not compared - its ref has no 16-bit equivalent). */

/* cdiSetSample - slot 00.  Original: 0EAE:008F. */
int32_t cdiSetSample(int32_t ch, CdiSampleInfo *si)
{ return CDI_CALL(cdi_dev(ch), setSample, ch, si); }

/* cdiPlaySample - slot 04.  Original: 0EAE:00B8. */
int32_t cdiPlaySample(int32_t ch, uint32_t freq, uint32_t volume)
{ return CDI_CALL(cdi_dev(ch), playSample, ch, freq, volume); }

/* cdiStopChannel - slot 28.  Original: 0EAE:00E6. */
int32_t cdiStopChannel(int32_t ch)
{ return CDI_CALL(cdi_dev(ch), stopChannel, ch); }

/* cdiSetVolume - slot 08.  Original: 0EAE:010C. */
int32_t cdiSetVolume(int32_t ch, uint32_t volume)
{ return CDI_CALL(cdi_dev(ch), setVolume, ch, volume); }

/* cdiSetFrequency - slot 0C.  Original: 0EAE:0136. */
int32_t cdiSetFrequency(int32_t ch, uint32_t freq)
{ return CDI_CALL(cdi_dev(ch), setFrequency, ch, freq); }

/* cdiSetLinearRate - slot 10.  Original: 0EAE:0160 (unreferenced). */
int32_t cdiSetLinearRate(int32_t ch, uint32_t rate)
{ return CDI_CALL(cdi_dev(ch), setLinearRate, ch, rate); }

/* cdiSetPosition - slot 14.  Original: 0EAE:018A. */
int32_t cdiSetPosition(int32_t ch, uint32_t pos)
{ return CDI_CALL(cdi_dev(ch), setPosition, ch, pos); }

/* cdiSetPanning - slot 18.  Original: 0EAE:01B4. */
int32_t cdiSetPanning(int32_t ch, int32_t pan)
{ return CDI_CALL(cdi_dev(ch), setPanning, ch, pan); }

/* cdiSetMasterVolume - slot 1C; ch only selects the device.
 * Original: 0EAE:01DE (unreferenced). */
int32_t cdiSetMasterVolume(int32_t ch, uint32_t volume)
{ return CDI_CALL(cdi_dev(ch), setMasterVolume, volume); }

/* cdiMuteChannel - slot 20.  Original: 0EAE:0204. */
int32_t cdiMuteChannel(int32_t ch)
{ return CDI_CALL(cdi_dev(ch), muteChannel, ch); }

/* cdiUnmuteChannel - slot 24.  Original: 0EAE:022A. */
int32_t cdiUnmuteChannel(int32_t ch)
{ return CDI_CALL(cdi_dev(ch), unmuteChannel, ch); }

/* cdiMuteAll - slot 2C; ch only selects the device.  Original: 0EAE:0250 (unreferenced). */
int32_t cdiMuteAll(int32_t ch)
{ return CDI_CALL(cdi_dev(ch), muteAll); }

/* cdiUnmuteAll - slot 30; ch only selects the device.  Original: 0EAE:0272 (unreferenced). */
int32_t cdiUnmuteAll(int32_t ch)
{ return CDI_CALL(cdi_dev(ch), unmuteAll); }

/* cdiDownloadSample - slot 34; ch only selects the device.  Original: 0EAE:0294. */
int32_t cdiDownloadSample(int32_t ch, uint8_t *sample, CdiSampleRef tag, uint32_t length)
{ return CDI_CALL(cdi_dev(ch), download, sample, tag, length); }

/* cdiUnloadAll - slot 3C; ch only selects the device.  Original: 0EAE:02C2 (unreferenced). */
int32_t cdiUnloadAll(int32_t ch)
{ return CDI_CALL(cdi_dev(ch), unloadAll); }

/* cdiUnloadSample - slot 38; ch only selects the device.  Original: 0EAE:02E4. */
int32_t cdiUnloadSample(int32_t ch, CdiSampleRef tag)
{ return CDI_CALL(cdi_dev(ch), unload, tag); }

/* cdiGetVolume - slot 40.  Original: 0EAE:030A (unreferenced). */
int32_t cdiGetVolume(int32_t ch)
{ return CDI_CALL(cdi_dev(ch), getVolume, ch); }

/* cdiGetFrequency - slot 44.  Original: 0EAE:0330 (unreferenced). */
int32_t cdiGetFrequency(int32_t ch)
{ return CDI_CALL(cdi_dev(ch), getFrequency, ch); }

/* cdiGetPosition - slot 48.  Original: 0EAE:0356. */
int32_t cdiGetPosition(int32_t ch)
{ return CDI_CALL(cdi_dev(ch), getPosition, ch); }

/* cdiGetPanning - slot 4C.  Original: 0EAE:037C (unreferenced). */
int32_t cdiGetPanning(int32_t ch)
{ return CDI_CALL(cdi_dev(ch), getPanning, ch); }

/* cdiGetSample - slot 50.  Original: 0EAE:03A2 (unreferenced). */
CdiSampleRef cdiGetSample(int32_t ch)
{
    const CdiDevice *d = cdi_dev(ch);
    CdiSampleRef none = { 0 };
    return d->getSample ? d->getSample(ch) : none;
}

/* cdiSetupChannels - slot 54; ch only selects the device.  Original: 0EAE:03C8. */
int32_t cdiSetupChannels(int32_t ch, int32_t count)
{ return CDI_CALL(cdi_dev(ch), setupChannels, count); }

/* cdiGetChannelStatus - slot 58.  Original: 0EAE:03EE (unreferenced). */
int32_t cdiGetChannelStatus(int32_t ch)
{ return CDI_CALL(cdi_dev(ch), getChannelStatus, ch); }

/* cdiPoll - slot 5C; ch only selects the device.  PUSHAD/POPAD around the
 * call: no result.  Original: 0EAE:0414 (NSES 40 Hz timer routine). */
void cdiPoll(int32_t ch)
{
    const CdiDevice *d = cdi_dev(ch);
    if (d->poll)
        d->poll();
}

/* cdiGetStatus - slot 64; ch only selects the device.  Original: 0EAE:043A (unreferenced). */
int32_t cdiGetStatus(int32_t ch, int32_t arg)
{ return CDI_CALL(cdi_dev(ch), getStatus, arg); }

/* cdiSetEffect - slot 60; ch only selects the device, the device gets (a, b).
 * Original: 0EAE:0460 (unreferenced). */
int32_t cdiSetEffect(int32_t ch, int32_t a, int32_t b)
{ return CDI_CALL(cdi_dev(ch), setEffect, a, b); }
