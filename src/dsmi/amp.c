/*
 * amp.c - DSMI AMP 2.00 module player.  Original: IST.EXE seg0EF6
 * ("AMP 2.00 - (C) 1992,1994 Otto Chrons").  Structures and DGROUP names: amp.h.
 *
 * How the player works
 * --------------------
 * ampTick() runs at tickHz = BPM*50/125 (integer).  Every `speed` ticks it
 * reads one row: for every track it consumes the 3-byte entries {row, cmd,
 * arg} whose row <= the current row (notes, instrument changes, row commands
 * 0x80..0x97), then re-sends volume (and frequency if the effect changed).
 * On every tick, each track's active effect (AmpTrack.cmd) runs from the tick
 * table; on the row tick itself only arpeggio and retrigger run.
 *
 * Register-level details the C keeps (see the per-function notes):
 *  - Row command handlers run inside ampReadTrackRow's frame; the instrument
 *    command writes ampReadTrackRow's local "previous effect" byte
 *    ([BP-2]) - here amp_row_saved_cmd.
 *  - The row commands are dispatched through the CS:036B jump table with the
 *    index check `cmd&0x7F <= 0x18`: index 0x18 (cmd 0x98) is accepted but
 *    lies past the 24-entry table (it reads the first code word of
 *    ampScaleVolume, 0x00C8, and would jump into the 2^(n/12) table at
 *    CS:00C8), and entry 5 (cmd 0x85) points at a stray RETF byte (CS:03BD),
 *    which returns far from a near call.  Both crash the original; the music
 *    never uses them.  The port ignores them.
 *  - ampInterrupt switches to a private stack at DGROUP 7BE4 (old SS:SP saved
 *    at 7BE4/7BE6); irrelevant in C, those DGROUP bytes are not written.
 *
 * Verified: tests/py/test_amp.py - every AMF in extracted/MUZIK played for
 * thousands of ticks against the original in Unicorn; the CDI call streams
 * and the player state in DGROUP are identical.
 */
#include <string.h>

#include "dsmi/amp.h"
#include "dsmi/dds.h"
#include "dsmi/dsmitime.h"

/* ---- code-segment data -------------------------------------------------------
 * IST.EXE 0EF6:000A..039C, extracted byte for byte from build/ist_flat.bin
 * (flat file offset 0xEF60 + off).  Contents (CS offsets):
 *   000A  "AMP 2.00 - (C) 1992,1994 Otto Chrons", 1A
 *   0030  "MCPSampleRealAddress"
 *   0045  u16[32] t*45   track state offsets (7C91 + ...)
 *   0085  u16[32] ch*26  SAMPLEINFO offsets (8231 + ...)
 *   00C5  u16[128] 2^(n/12)*1024 (1023 at n=60, saturates 0x8000)
 *   01CD  u16[127] period per note (6848 at n=60, 0xD5FF at the bottom)
 *   02CB  u16[64] half sine 0..4095..200
 *   034B  u16[16] 2^(k/12)*1024 (arpeggio)
 *   036B  u16[24] row command jump table for cmd 0x80..0x97
 *   039B  first word of ampScaleVolume's code (C8 00), read by index 0x18
 * Tables are read through amp_cs16() at their original offsets, so an index
 * past a table's end reads the following table exactly as the original does.
 */
static const uint8_t amp_cs_data[0x39D - 0xA] = {
    /* 000A */ 0x41, 0x4D, 0x50, 0x20, 0x32, 0x2E, 0x30, 0x30, 0x20, 0x2D, 0x20, 0x28, 0x43, 0x29, 0x20, 0x31,
    /* 001A */ 0x39, 0x39, 0x32, 0x2C, 0x31, 0x39, 0x39, 0x34, 0x20, 0x4F, 0x74, 0x74, 0x6F, 0x20, 0x43, 0x68,
    /* 002A */ 0x72, 0x6F, 0x6E, 0x73, 0x00, 0x1A, 0x4D, 0x43, 0x50, 0x53, 0x61, 0x6D, 0x70, 0x6C, 0x65, 0x52,
    /* 003A */ 0x65, 0x61, 0x6C, 0x41, 0x64, 0x64, 0x72, 0x65, 0x73, 0x73, 0x00, 0x00, 0x00, 0x2D, 0x00, 0x5A,
    /* 004A */ 0x00, 0x87, 0x00, 0xB4, 0x00, 0xE1, 0x00, 0x0E, 0x01, 0x3B, 0x01, 0x68, 0x01, 0x95, 0x01, 0xC2,
    /* 005A */ 0x01, 0xEF, 0x01, 0x1C, 0x02, 0x49, 0x02, 0x76, 0x02, 0xA3, 0x02, 0xD0, 0x02, 0xFD, 0x02, 0x2A,
    /* 006A */ 0x03, 0x57, 0x03, 0x84, 0x03, 0xB1, 0x03, 0xDE, 0x03, 0x0B, 0x04, 0x38, 0x04, 0x65, 0x04, 0x92,
    /* 007A */ 0x04, 0xBF, 0x04, 0xEC, 0x04, 0x19, 0x05, 0x46, 0x05, 0x73, 0x05, 0x00, 0x00, 0x1A, 0x00, 0x34,
    /* 008A */ 0x00, 0x4E, 0x00, 0x68, 0x00, 0x82, 0x00, 0x9C, 0x00, 0xB6, 0x00, 0xD0, 0x00, 0xEA, 0x00, 0x04,
    /* 009A */ 0x01, 0x1E, 0x01, 0x38, 0x01, 0x52, 0x01, 0x6C, 0x01, 0x86, 0x01, 0xA0, 0x01, 0xBA, 0x01, 0xD4,
    /* 00AA */ 0x01, 0xEE, 0x01, 0x08, 0x02, 0x22, 0x02, 0x3C, 0x02, 0x56, 0x02, 0x70, 0x02, 0x8A, 0x02, 0xA4,
    /* 00BA */ 0x02, 0xBE, 0x02, 0xD8, 0x02, 0xF2, 0x02, 0x0C, 0x03, 0x26, 0x03, 0x1F, 0x00, 0x21, 0x00, 0x23,
    /* 00CA */ 0x00, 0x26, 0x00, 0x28, 0x00, 0x2A, 0x00, 0x2D, 0x00, 0x2F, 0x00, 0x32, 0x00, 0x35, 0x00, 0x39,
    /* 00DA */ 0x00, 0x3C, 0x00, 0x3F, 0x00, 0x43, 0x00, 0x47, 0x00, 0x4C, 0x00, 0x50, 0x00, 0x55, 0x00, 0x5A,
    /* 00EA */ 0x00, 0x5F, 0x00, 0x65, 0x00, 0x6B, 0x00, 0x72, 0x00, 0x78, 0x00, 0x7F, 0x00, 0x87, 0x00, 0x8F,
    /* 00FA */ 0x00, 0x98, 0x00, 0xA1, 0x00, 0xAA, 0x00, 0xB5, 0x00, 0xBF, 0x00, 0xCB, 0x00, 0xD7, 0x00, 0xE4,
    /* 010A */ 0x00, 0xF1, 0x00, 0xFF, 0x00, 0x0F, 0x01, 0x1F, 0x01, 0x30, 0x01, 0x42, 0x01, 0x55, 0x01, 0x6A,
    /* 011A */ 0x01, 0x7F, 0x01, 0x96, 0x01, 0xAE, 0x01, 0xC8, 0x01, 0xE3, 0x01, 0xFF, 0x01, 0x1E, 0x02, 0x3E,
    /* 012A */ 0x02, 0x60, 0x02, 0x85, 0x02, 0xAB, 0x02, 0xD4, 0x02, 0xFF, 0x02, 0x2C, 0x03, 0x5D, 0x03, 0x90,
    /* 013A */ 0x03, 0xC6, 0x03, 0xFF, 0x03, 0x3C, 0x04, 0x7D, 0x04, 0xC1, 0x04, 0x0A, 0x05, 0x56, 0x05, 0xA8,
    /* 014A */ 0x05, 0xFE, 0x05, 0x59, 0x06, 0xBA, 0x06, 0x20, 0x07, 0x8D, 0x07, 0x00, 0x08, 0x79, 0x08, 0xFA,
    /* 015A */ 0x08, 0x83, 0x09, 0x14, 0x0A, 0xAD, 0x0A, 0x50, 0x0B, 0xFC, 0x0B, 0xB3, 0x0C, 0x74, 0x0D, 0x41,
    /* 016A */ 0x0E, 0x1A, 0x0F, 0x00, 0x10, 0xF3, 0x10, 0xF5, 0x11, 0x06, 0x13, 0x28, 0x14, 0x5B, 0x15, 0xA0,
    /* 017A */ 0x16, 0xF9, 0x17, 0x66, 0x19, 0xE8, 0x1A, 0x82, 0x1C, 0x34, 0x1E, 0x00, 0x20, 0xE7, 0x21, 0xEB,
    /* 018A */ 0x23, 0x0D, 0x26, 0x51, 0x28, 0xB7, 0x2A, 0x41, 0x2D, 0xF2, 0x2F, 0xCC, 0x32, 0xD1, 0x35, 0x04,
    /* 019A */ 0x39, 0x68, 0x3C, 0x00, 0x40, 0xCE, 0x43, 0xD6, 0x47, 0x1B, 0x4C, 0xA2, 0x50, 0x6E, 0x55, 0x82,
    /* 01AA */ 0x5A, 0xE4, 0x5F, 0x98, 0x65, 0xA2, 0x6B, 0xEB, 0x71, 0xD0, 0x78, 0x00, 0x80, 0x00, 0x80, 0x00,
    /* 01BA */ 0x80, 0x00, 0x80, 0x00, 0x80, 0x00, 0x80, 0x00, 0x80, 0x00, 0x80, 0x00, 0x80, 0x00, 0x80, 0x00,
    /* 01CA */ 0x80, 0x00, 0x80, 0xFF, 0xD5, 0xFF, 0xD5, 0xFF, 0xD5, 0xFF, 0xD5, 0xFF, 0xD5, 0xFF, 0xD5, 0xFF,
    /* 01DA */ 0xD5, 0xFF, 0xD5, 0xFF, 0xD5, 0xFF, 0xD5, 0xFF, 0xD5, 0xFF, 0xD5, 0xFF, 0xD5, 0xFF, 0xD5, 0xFF,
    /* 01EA */ 0xD5, 0xFF, 0xD5, 0xFF, 0xD5, 0xFF, 0xD5, 0xFF, 0xD5, 0xFF, 0xD5, 0xFF, 0xD5, 0xFF, 0xD5, 0xFF,
    /* 01FA */ 0xD5, 0xFF, 0xD5, 0xFF, 0xD5, 0xFD, 0xC9, 0xA6, 0xBE, 0xF3, 0xB3, 0xDA, 0xA9, 0x51, 0xA0, 0x52,
    /* 020A */ 0x97, 0xD3, 0x8E, 0xCF, 0x86, 0x3E, 0x7F, 0x1A, 0x78, 0x5C, 0x71, 0xFF, 0x6A, 0xFE, 0x64, 0x53,
    /* 021A */ 0x5F, 0xF9, 0x59, 0xED, 0x54, 0x28, 0x50, 0xA9, 0x4B, 0x69, 0x47, 0x67, 0x43, 0x9F, 0x3F, 0x0D,
    /* 022A */ 0x3C, 0xAE, 0x38, 0x7F, 0x35, 0x7F, 0x32, 0xA9, 0x2F, 0xFC, 0x2C, 0x76, 0x2A, 0x14, 0x28, 0xD4,
    /* 023A */ 0x25, 0xB4, 0x23, 0xB3, 0x21, 0xCF, 0x1F, 0x06, 0x1E, 0x57, 0x1C, 0xC0, 0x1A, 0x3F, 0x19, 0xD4,
    /* 024A */ 0x17, 0x7E, 0x16, 0x3B, 0x15, 0x0A, 0x14, 0xEA, 0x12, 0xDA, 0x11, 0xD9, 0x10, 0xE7, 0x0F, 0x03,
    /* 025A */ 0x0F, 0x2B, 0x0E, 0x60, 0x0D, 0x9F, 0x0C, 0xEA, 0x0B, 0x3F, 0x0B, 0x9D, 0x0A, 0x05, 0x0A, 0x75,
    /* 026A */ 0x09, 0xED, 0x08, 0x6C, 0x08, 0xF3, 0x07, 0x81, 0x07, 0x15, 0x07, 0xB0, 0x06, 0x4F, 0x06, 0xF5,
    /* 027A */ 0x05, 0x9F, 0x05, 0x4E, 0x05, 0x02, 0x05, 0xBA, 0x04, 0x76, 0x04, 0x36, 0x04, 0xF9, 0x03, 0xC0,
    /* 028A */ 0x03, 0x8A, 0x03, 0x58, 0x03, 0x27, 0x03, 0xFA, 0x02, 0xCF, 0x02, 0xA7, 0x02, 0x81, 0x02, 0x5D,
    /* 029A */ 0x02, 0x3B, 0x02, 0x1B, 0x02, 0xFC, 0x01, 0xE0, 0x01, 0xC5, 0x01, 0xAC, 0x01, 0x93, 0x01, 0x7D,
    /* 02AA */ 0x01, 0x67, 0x01, 0x53, 0x01, 0x40, 0x01, 0x2E, 0x01, 0x1D, 0x01, 0x0D, 0x01, 0xFE, 0x00, 0xF0,
    /* 02BA */ 0x00, 0xE2, 0x00, 0xD6, 0x00, 0xC9, 0x00, 0xBE, 0x00, 0xB3, 0x00, 0xA9, 0x00, 0xA0, 0x00, 0x97,
    /* 02CA */ 0x00, 0x00, 0x00, 0xC8, 0x00, 0x91, 0x01, 0x58, 0x02, 0x1E, 0x03, 0xE3, 0x03, 0xA4, 0x04, 0x63,
    /* 02DA */ 0x05, 0x1F, 0x06, 0xD6, 0x06, 0x8A, 0x07, 0x39, 0x08, 0xE3, 0x08, 0x87, 0x09, 0x25, 0x0A, 0xBE,
    /* 02EA */ 0x0A, 0x4F, 0x0B, 0xDA, 0x0B, 0x5D, 0x0C, 0xD9, 0x0C, 0x4C, 0x0D, 0xB8, 0x0D, 0x1B, 0x0E, 0x75,
    /* 02FA */ 0x0E, 0xC7, 0x0E, 0x0F, 0x0F, 0x4E, 0x0F, 0x84, 0x0F, 0xB0, 0x0F, 0xD2, 0x0F, 0xEB, 0x0F, 0xFA,
    /* 030A */ 0x0F, 0xFF, 0x0F, 0xFA, 0x0F, 0xEB, 0x0F, 0xD2, 0x0F, 0xB0, 0x0F, 0x84, 0x0F, 0x4E, 0x0F, 0x0F,
    /* 031A */ 0x0F, 0xC7, 0x0E, 0x75, 0x0E, 0x1B, 0x0E, 0xB8, 0x0D, 0x4C, 0x0D, 0xD9, 0x0C, 0x5D, 0x0C, 0xDA,
    /* 032A */ 0x0B, 0x4F, 0x0B, 0xBE, 0x0A, 0x25, 0x0A, 0x87, 0x09, 0xE3, 0x08, 0x39, 0x08, 0x8A, 0x07, 0xD6,
    /* 033A */ 0x06, 0x1F, 0x06, 0x63, 0x05, 0xA4, 0x04, 0xE3, 0x03, 0x1E, 0x03, 0x58, 0x02, 0x91, 0x01, 0xC8,
    /* 034A */ 0x00, 0x00, 0x04, 0x3D, 0x04, 0x7D, 0x04, 0xC2, 0x04, 0x0A, 0x05, 0x57, 0x05, 0xA8, 0x05, 0xFE,
    /* 035A */ 0x05, 0x59, 0x06, 0xBA, 0x06, 0x21, 0x07, 0x8D, 0x07, 0x00, 0x08, 0x7A, 0x08, 0xFA, 0x08, 0x84,
    /* 036A */ 0x09, 0xA7, 0x07, 0x74, 0x07, 0x0E, 0x07, 0x1E, 0x07, 0xC7, 0x06, 0xBD, 0x03, 0x42, 0x06, 0x5D,
    /* 037A */ 0x05, 0xD0, 0x05, 0x9F, 0x05, 0x4D, 0x05, 0x3D, 0x05, 0x6E, 0x07, 0x43, 0x07, 0x35, 0x05, 0x21,
    /* 038A */ 0x05, 0xDD, 0x04, 0xA2, 0x04, 0x42, 0x04, 0x02, 0x04, 0xF2, 0x03, 0x86, 0x07, 0xFA, 0x06, 0xBE,
    /* 039A */ 0x03, 0xC8, 0x00,
};

static uint16_t amp_cs16(uint16_t off)
{
    uint16_t i = (uint16_t)(off - 0x0A);
    if (i + 1u >= sizeof amp_cs_data)       /* never reached by the player */
        return 0;
    return (uint16_t)(amp_cs_data[i] | amp_cs_data[i + 1] << 8);
}

#define CS_TRACK_OFS  0x0045
#define CS_SEMITONE   0x00C5
#define CS_PERIOD     0x01CD
#define CS_SINE       0x02CB
#define CS_ARPEGGIO   0x034B

/* ---- native state (far pointers of the original) ------------------------------ */
uint8_t       *amp_track_data[32];          /* 7C91 + t*45 + 0  */
CdiSampleInfo  amp_sampleinfo[32];          /* 8231 + ch*26     */
AmpPattern    *amp_orders;                  /* 7C5C             */
AmpInstrument *amp_instruments;             /* 7C60             */
AmpInstrument *amp_mod_instruments;         /* 7C33             */
AmpPattern    *amp_mod_patterns;            /* 7C38             */
uint8_t      **amp_mod_tracks;              /* 7C3E             */
McpSampleRealAddressFn amp_sample_real_address;   /* 85A6       */

/* ampReadTrackRow's local [BP-2]: the effect active before this row.  The
 * instrument row command (run in that frame) overwrites it. */
static uint8_t amp_row_saved_cmd;

AmpTrack *amp_track(uint16_t t)
{
    return (AmpTrack *)DGP(g_amp_tracks_base + amp_cs16((uint16_t)(CS_TRACK_OFS + 2 * t)));
}

static uint8_t **amp_track_data_slot(uint16_t t)
{
    return &amp_track_data[amp_cs16((uint16_t)(CS_TRACK_OFS + 2 * t)) / 45];
}

static CdiSampleInfo *amp_si(uint8_t ch)
{
    /* CS:0085 table, ch*26; ch is always < channelCount <= 32 */
    return &amp_sampleinfo[ch & 31];
}

/* freq = c2spd * 6848 / period (MUL EDX / DIV ECX, 64-bit dividend). */
static uint32_t amp_period_freq(uint16_t rate, uint16_t period)
{
    if (period == 0)        /* DIV by zero -> INT 0 in the original; only a  */
        return 0xFFFF;      /* tone porta onto a zero target could get here  */
    return (uint32_t)((uint64_t)rate * 0x1AC0u / period);
}

/* freq = c2spd * 2^(n/12)*1024 >> 10 (MUL EBX, SHRD EAX,EDX,10) */
static uint32_t amp_note_freq(uint16_t rate, uint8_t note)
{
    return (uint32_t)(((uint64_t)rate * amp_cs16((uint16_t)(CS_SEMITONE + 2 * note))) >> 10);
}

/*
 * ampScaleVolume - vol * trackVolume[ch] >> 6 (32-bit IMUL, logical shift).
 *
 * Original: IST.EXE 0EF6:039B (AMP), far pascal (long ch, long vol) -> EAX.
 * Verified: tests/py/test_amp.py (through every volume the player sends).
 */
uint32_t ampScaleVolume(int32_t ch, uint32_t vol)
{
    return (uint32_t)(vol * DG8(0x8571 + (uint16_t)ch)) >> 6;
}

/* ======================= row commands (CS:036B table) ======================
 * Near handlers called by ampDoCommand with ES:DI = the entry {row,cmd,arg}
 * (e[3..5] is the next entry; every track ends with FF FF FF) and the track
 * record.  Original: seg0EF6.  Verified: tests/py/test_amp.py (the shipped
 * music uses 80 81 82 83 84 86 88 89 8C 8D 94 95; the others are compared on
 * synthetic tracks). */

/* ampCmdPanning - 0x97: pan, clamped to -63..63 unless 100 (surround).  0EF6:03BE */
static void ampCmdPanning(AmpTrack *tr, const uint8_t *e)
{
    int8_t p = (int8_t)e[2];

    tr->cmd = 0x17;
    tr->param = e[2];
    if (e[2] != 100) {
        if (p < -63) p = -63;
        if (p > 63) p = 63;
    }
    ampSetPanning(g_amp_cur_track, p);
}

/* ampCmdNoteCut - 0x94: arm the cut (tick effect 0x14).  0EF6:03F2 */
static void ampCmdNoteCut(AmpTrack *tr, const uint8_t *e)
{
    tr->cmd = 0x14;
    tr->param = e[2];
}

/* ampCmdNoteDelay - 0x93: if a note follows on the same row, take it (and its
 * volume) and arm tick effect 0x13; the note entry is consumed.  Without a
 * following note only the delay (0 -> 1) is stored.  0EF6:0402 */
static void ampCmdNoteDelay(AmpTrack *tr, const uint8_t *e)
{
    tr->param = e[2] ? e[2] : 1;
    if (e[0] == e[3] && !(e[4] & 0x80)) {
        if (e[4] != 0x7F)
            tr->note = e[4];
        if (e[5] != 0xFF)
            tr->volume = e[5];
        tr->cmd = 0x13;
        tr->pos += 3;
    }
}

/* ampCmdFinePorta - 0x92: period += (signed arg)<<4 once.  A result with bit
 * 15 set (JS) or <= 100 becomes 100.  0EF6:0442 */
static void ampCmdFinePorta(AmpTrack *tr, const uint8_t *e)
{
    uint8_t ch = g_amp_cur_track;
    CdiSampleInfo *si;
    uint16_t cx;

    tr->cmd = 0x12;
    tr->param = e[2];
    cx = (uint16_t)(tr->period + (uint16_t)((int16_t)(int8_t)e[2] << 4));
    if ((int16_t)cx < 0 || cx <= 100)
        cx = 100;
    tr->period = cx;
    si = amp_si(ch);
    si->freq = amp_period_freq(si->rate, cx);
    cdiSetFrequency(ch, si->freq);
}

/* ampCmdFineVolSlide - 0x91: volume += signed arg once; an 8-bit result with
 * bit 7 set becomes 0 (so 64+100 gives 0), above 64 becomes 64.  0EF6:04A2 */
static void ampCmdFineVolSlide(AmpTrack *tr, const uint8_t *e)
{
    uint8_t ch = g_amp_cur_track;
    uint8_t v;

    tr->cmd = 0x11;
    tr->param = e[2];
    v = (uint8_t)(e[2] + tr->volume);
    if (v & 0x80) v = 0;
    if (v > 0x40) v = 0x40;
    tr->volume = v;
    cdiSetVolume(ch, ampScaleVolume(ch, v));
}

/* ampCmdSampleOffset - 0x90: position = arg*256 (0 = last value); past the
 * sample length it becomes the loop start.  0EF6:04DD */
static void ampCmdSampleOffset(AmpTrack *tr, const uint8_t *e)
{
    uint8_t ch = g_amp_cur_track;
    uint8_t ah = e[2];
    uint32_t pos;

    tr->cmd = 0x10;                     /* param is left 0 */
    if (!ah)
        ah = tr->offset_mem;
    tr->offset_mem = ah;
    pos = (uint32_t)ah << 8;
    if (pos > amp_si(ch)->length)
        pos = amp_si(ch)->loopstart;
    cdiSetPosition(ch, pos);
}

/* ampCmdRetrig - 0x8F: retrigger every arg ticks (0 = ignored).  0EF6:0521 */
static void ampCmdRetrig(AmpTrack *tr, const uint8_t *e)
{
    if (e[2]) {
        tr->cmd = 0x0F;
        tr->param = e[2];
    }
}

/* ampCmdSync - 0x8E: store the sync byte for ampGetSync.  0EF6:0535 */
static void ampCmdSync(AmpTrack *tr, const uint8_t *e)
{
    (void)tr;
    g_amp_sync = e[2];
}

/* ampCmdVibratoVolSlide - 0x8B.  0EF6:053D */
static void ampCmdVibratoVolSlide(AmpTrack *tr, const uint8_t *e)
{
    tr->cmd = 0x0B;
    tr->param = e[2];
}

/* ampCmdTonePortaVolSlide - 0x8A.  0EF6:054D */
static void ampCmdTonePortaVolSlide(AmpTrack *tr, const uint8_t *e)
{
    tr->cmd = 0x0A;
    tr->param = e[2];
}

/* merge the non-zero nibbles of a into the memory byte m */
static uint8_t amp_merge_nibbles(uint8_t m, uint8_t a)
{
    if (a) {
        if (a & 0x0F) m = (uint8_t)((m & 0xF0) | (a & 0x0F));
        if (a & 0xF0) m = (uint8_t)((m & 0x0F) | (a & 0xF0));
    }
    return m;
}

/* ampCmdTremolo - 0x87: speed/depth nibbles with memory (sine tremolo).
 * param keeps the raw argument.  0EF6:055D */
static void ampCmdTremolo(AmpTrack *tr, const uint8_t *e)
{
    uint8_t m;

    tr->param = e[2];
    m = amp_merge_nibbles(tr->trem_mem, e[2]);
    tr->trem_mem = m;
    tr->cmd = 0x07;
    tr->trem_speed = (uint8_t)(m >> 4);
    tr->trem_depth = (uint8_t)(m & 0x0F);
}

/* ampCmdVibrato - 0x89: speed/depth nibbles with memory.  0EF6:059F */
static void ampCmdVibrato(AmpTrack *tr, const uint8_t *e)
{
    uint8_t m;

    tr->cmd = 0x09;
    m = amp_merge_nibbles(tr->vib_mem, e[2]);
    tr->vib_mem = m;
    tr->param = m;
}

/* ampCmdArpeggio - 0x88: precompute base, +x and +y semitone frequencies from
 * the channel's current frequency (arg 0 = nothing).  0EF6:05D0 */
static void ampCmdArpeggio(AmpTrack *tr, const uint8_t *e)
{
    uint32_t f;

    if (!e[2])
        return;
    tr->cmd = 0x08;
    tr->param = e[2];
    f = amp_si(g_amp_cur_track)->freq;
    tr->arp_freq[0] = f;
    tr->arp_freq[1] = (uint32_t)(((uint64_t)amp_cs16((uint16_t)(CS_ARPEGGIO + 2 * (e[2] >> 4))) * f) >> 10);
    tr->arp_freq[2] = (uint32_t)(((uint64_t)amp_cs16((uint16_t)(CS_ARPEGGIO + 2 * (e[2] & 15))) * f) >> 10);
}

/*
 * ampCmdTonePorta - 0x86: slide towards the note that follows on the same row
 * (that entry is consumed), speed with memory, sign = direction.
 *
 * Original: 0EF6:0642.  Quirk kept: the volume sent with cdiSetVolume is EAX,
 * which the code only loads on the "note follows" path - there it is the
 * note's volume byte even when that is 0xFF ("no volume", so 255*trackVol/64
 * goes out once).  Without a following note EAX is the current entry's row
 * byte (or the next entry's command byte when that is on the same row) in the
 * low word, and in the high word whatever EAX held before: the driver's
 * result of the previous track's last call or the segment of a track pointer
 * - not reproducible.  The port uses 0 for the high word.  The shipped music
 * has a single 0x86 and it is followed by a note.
 */
static void ampCmdTonePorta(AmpTrack *tr, const uint8_t *e)
{
    uint8_t ch = g_amp_cur_track;
    uint8_t cl = e[2] ? e[2] : tr->tp_mem;
    uint32_t eax;

    tr->tp_mem = cl;
    if (e[0] != e[3])
        eax = e[0];
    else if (e[4] & 0x80)
        eax = e[4];
    else {
        uint8_t note = e[4] == 0x7F ? tr->note : e[4];
        tr->note = note;
        tr->target = amp_cs16((uint16_t)(CS_PERIOD + 2 * note));
        tr->pos += 3;
        eax = e[5];
        if (e[5] != 0xFF)
            tr->volume = e[5];
    }
    cdiSetVolume(ch, ampScaleVolume(ch, eax));
    if (tr->target < tr->period)
        cl = (uint8_t)-cl;
    tr->cmd = 0x06;
    tr->tp_speed = (int8_t)cl;
}

/* ampCmdPortamento - 0x84: signed slide; 0 = memory made positive,
 * 0x80 = memory made negative.  0EF6:06C7 */
static void ampCmdPortamento(AmpTrack *tr, const uint8_t *e)
{
    int8_t cl = (int8_t)e[2];

    if ((uint8_t)cl == 0x80) {
        cl = tr->porta_mem;
        if (!(cl < 0))
            cl = (int8_t)-cl;
    } else if (cl == 0) {
        cl = tr->porta_mem;
        if (!(cl > 0))
            cl = (int8_t)-cl;
    }
    tr->porta_mem = cl;
    tr->cmd = 0x04;
    tr->param = (uint8_t)cl;
}

/* ampCmdExtraFinePorta - 0x96: arm tick effect 0x16 (arg 0 = ignored).  0EF6:06FA */
static void ampCmdExtraFinePorta(AmpTrack *tr, const uint8_t *e)
{
    if (e[2]) {
        tr->cmd = 0x16;
        tr->param = e[2];
    }
}

/* ampCmdVolumeSlide - 0x82: arm the per-tick slide.  0EF6:070E */
static void ampCmdVolumeSlide(AmpTrack *tr, const uint8_t *e)
{
    tr->cmd = 0x02;
    tr->param = e[2];
}

/* ampCmdSetVolume - 0x83: volume = arg, not clamped.  0EF6:071E */
static void ampCmdSetVolume(AmpTrack *tr, const uint8_t *e)
{
    uint8_t ch = g_amp_cur_track;

    tr->volume = e[2];
    cdiSetVolume(ch, ampScaleVolume(ch, e[2]));
}

/* ampCmdPositionJump - 0x8D: continue at order arg (cur = arg-1, then the
 * row-end increment).  Backwards (signed compare) only with the loop option;
 * never past the end order.  0EF6:0743 */
static void ampCmdPositionJump(AmpTrack *tr, const uint8_t *e)
{
    int8_t to = (int8_t)(e[2] - 1);

    (void)tr;
    if (to >= (int8_t)g_amp_cur_order || (g_amp_play_options & 1)) {
        if (!(to > (int8_t)g_amp_end_order)) {
            g_amp_cur_order = (uint8_t)to;
            g_amp_break = 1;
        }
    }
}

/* ampCmdPatternBreak - 0x8C: next order at row 0 (arg ignored).  0EF6:076E */
static void ampCmdPatternBreak(AmpTrack *tr, const uint8_t *e)
{
    (void)tr; (void)e;
    g_amp_break = 1;
}

/* ampCmdSetSpeed - 0x81: ticks per row (0 ignored).  0EF6:0774 */
static void ampCmdSetSpeed(AmpTrack *tr, const uint8_t *e)
{
    (void)tr;
    if (e[2] != g_amp_speed && e[2])
        g_amp_speed = e[2];
}

/* ampCmdSetTempo - 0x95: BPM; tickHz = BPM*50/125 (integer).  0EF6:0786 */
static void ampCmdSetTempo(AmpTrack *tr, const uint8_t *e)
{
    (void)tr;
    if (e[2] != g_amp_tempo && e[2]) {
        g_amp_tempo = e[2];
        g_amp_tick_hz = (uint16_t)(e[2] * 50) / 125;
    }
}

/*
 * ampCmdInstrument - 0x80: load instrument arg into the channel SAMPLEINFO,
 * cdiSetSample, volume = instrument volume.  Writes the caller's [BP-2]:
 * 0 if a note follows on the same row, else 1 (so ampReadTrackRow re-sends
 * the frequency unless the effect after the row equals that value).
 *
 * Original: 0EF6:07A7.  The instrument index is not range-checked.
 */
static void ampCmdInstrument(AmpTrack *tr, const uint8_t *e)
{
    uint8_t ch = g_amp_cur_track;
    const AmpInstrument *ins;
    CdiSampleInfo *si;

    tr->instrument = e[2];
    amp_row_saved_cmd = 1;
    if (e[0] == e[3] && !(e[4] & 0x80))
        amp_row_saved_cmd = 0;
    ins = &amp_instruments[e[2]];
    si = amp_si(ch);
    si->sample = ins->sample;
    si->length = ins->length;
    si->rate = ins->rate;
    si->loopstart = ins->loopstart;
    si->loopend = ins->loopend;
    si->volume = ins->volume;
    tr->volume = ins->volume;
    si->mode = 1;
    si->sampleID = 0;
    if (ins->sample.in_ems)                     /* segment == FFFFh */
        si->sampleID = ins->sample.ems_handle;  /* = the offset word */
    cdiSetSample(ch, si);
    cdiSetVolume(ch, ampScaleVolume(ch, si->volume));
}

typedef void (*AmpRowCmd)(AmpTrack *, const uint8_t *);

/* CS:036B, index cmd & 0x7F.  NULL = the two broken entries (see top). */
static const AmpRowCmd amp_row_cmds[24] = {
    ampCmdInstrument,        /* 80  07A7 */
    ampCmdSetSpeed,          /* 81  0774 */
    ampCmdVolumeSlide,       /* 82  070E */
    ampCmdSetVolume,         /* 83  071E */
    ampCmdPortamento,        /* 84  06C7 */
    NULL,                    /* 85  03BD: a RETF byte - crashes the original */
    ampCmdTonePorta,         /* 86  0642 */
    ampCmdTremolo,           /* 87  055D */
    ampCmdArpeggio,          /* 88  05D0 */
    ampCmdVibrato,           /* 89  059F */
    ampCmdTonePortaVolSlide, /* 8A  054D */
    ampCmdVibratoVolSlide,   /* 8B  053D */
    ampCmdPatternBreak,      /* 8C  076E */
    ampCmdPositionJump,      /* 8D  0743 */
    ampCmdSync,              /* 8E  0535 */
    ampCmdRetrig,            /* 8F  0521 */
    ampCmdSampleOffset,      /* 90  04DD */
    ampCmdFineVolSlide,      /* 91  04A2 */
    ampCmdFinePorta,         /* 92  0442 */
    ampCmdNoteDelay,         /* 93  0402 */
    ampCmdNoteCut,           /* 94  03F2 */
    ampCmdSetTempo,          /* 95  0786 */
    ampCmdExtraFinePorta,    /* 96  06FA */
    ampCmdPanning,           /* 97  03BE */
};

/*
 * ampDoCommand - dispatch a row command entry through the CS:036B table.
 *
 * Original: IST.EXE 0EF6:086A (AMP), near.  Index (cmd & 0x7F) > 0x18 is
 * skipped (an 0xFF command, e.g.).  Index 0x18 passes the check and runs
 * CS:00C8 - see the file header.  Verified: tests/py/test_amp.py.
 */
static void ampDoCommand(AmpTrack *tr, const uint8_t *e)
{
    uint16_t idx = e[1] & 0x7F;

    if (idx > 0x18)
        return;
    if (idx < 24 && amp_row_cmds[idx])
        amp_row_cmds[idx](tr, e);
    /* idx 5 and 0x18: undefined in the original (crash), ignored here */
}

/*
 * ampPlayNote - row note entry: note 0 = volume 0 + stop; 0x7F = keep the
 * note; volume 0xFF = keep.  Resets ticks and vibrato position, sets period
 * and tone porta target, freq = c2spd*2^(n/12), plays unless the track is
 * paused.
 *
 * Original: IST.EXE 0EF6:088D (AMP), near.  Verified: tests/py/test_amp.py.
 */
static void ampPlayNote(AmpTrack *tr, const uint8_t *e)
{
    uint8_t ch = g_amp_cur_track;
    CdiSampleInfo *si = amp_si(ch);
    uint16_t p;
    uint32_t vol;

    if (e[1] == 0) {
        tr->volume = 0;
        cdiStopChannel(ch);
        return;
    }
    if (e[1] != 0x7F)
        tr->note = e[1];
    if (e[2] != 0xFF)
        tr->volume = e[2];
    tr->ticks = 0;
    tr->vib_pos = 0;
    p = amp_cs16((uint16_t)(CS_PERIOD + 2 * tr->note));
    tr->period = p;
    tr->target = p;
    si->freq = amp_note_freq(si->rate, tr->note);
    si->volume = tr->volume;
    vol = ampScaleVolume(ch, tr->volume);
    if (!(tr->flags & 2))
        cdiPlaySample(ch, si->freq, vol);
}

/*
 * ampReadTrackRow - consume all entries of track t with row <= current row,
 * then re-send the frequency if the active effect changed and always the
 * volume.  An entry with row 0xFF ends the track (pos = 0xFFFF).
 *
 * Original: IST.EXE 0EF6:0955 (AMP), far, BX = track.  Commands that consume
 * the following entry advance pos by 3; DI follows pos (the CWDE delta).
 * Verified: tests/py/test_amp.py.
 */
static void ampReadTrackRow(uint16_t t)
{
    AmpTrack *tr;
    uint8_t ch;
    uint8_t *data;

    g_amp_cur_track = (uint8_t)t;
    tr = amp_track(t);
    amp_row_saved_cmd = tr->cmd;
    tr->cmd = 0;
    tr->param = 0;
    data = *amp_track_data_slot(t);
    if (tr->pos != 0xFFFF && data) {
        const uint8_t *e = data + tr->pos;
        for (;;) {
            uint16_t before;
            if (e[0] == 0xFF) {
                tr->pos = 0xFFFF;
                break;
            }
            if ((int16_t)e[0] > g_amp_row)
                break;
            before = tr->pos;
            if (e[1] & 0x80)
                ampDoCommand(tr, e);
            else
                ampPlayNote(tr, e);
            e += (int16_t)(uint16_t)(tr->pos - before) + 3;
            tr->pos += 3;
        }
    }
    ch = g_amp_cur_track;
    if (amp_row_saved_cmd != tr->cmd)
        cdiSetFrequency(ch, amp_si(ch)->freq);
    cdiSetVolume(ch, ampScaleVolume(ch, tr->volume));
}

/*
 * ampNextPattern - start the current order at row 0; past the end order stop
 * (returns -1) or, with the loop option, restart at the start order.
 *
 * Original: IST.EXE 0EF6:0A16 (AMP), near.  The channel loop is LOOP with
 * CX = channel count (0 would mean 65536 iterations; modules have >= 1).
 * Verified: tests/py/test_amp.py.
 */
static int16_t ampNextPattern(void)
{
    const AmpPattern *o;

    g_amp_row = 0;
    if (g_amp_cur_order >= g_amp_end_order) {
        if (!(g_amp_play_options & 1)) {
            ampStopModule();
            return -1;
        }
        g_amp_cur_order = g_amp_start_order;
    }
    o = &amp_orders[g_amp_cur_order];
    g_amp_rows = (int16_t)o->rows;
    for (uint16_t i = 0; i < g_amp_channels; i++) {
        amp_track_data[i] = o->track[i];                 /* 7C91 + i*0x2D */
        ((AmpTrack *)DGP(g_amp_tracks_base + i * 0x2D))->pos = 3;
    }
    return 0;
}

/* ======================= tick effects (CS:0DD0 table) ======================
 * Near, BX = track record.  Original: seg0EF6.  Verified: tests/py/test_amp.py. */

/* ampTickVolumeSlide - cmd 2 (and 0x0A/0x0B): volume += signed param, 8-bit
 * negative -> 0, > 64 -> 64.  0EF6:0A71 */
static void ampTickVolumeSlide(AmpTrack *tr)
{
    uint8_t ch = g_amp_cur_track;
    uint8_t v = (uint8_t)(tr->param + tr->volume);

    if (v & 0x80) v = 0;
    if (v > 0x40) v = 0x40;
    tr->volume = v;
    cdiSetVolume(ch, ampScaleVolume(ch, v));
}

/* ampTickExtraFinePorta - cmd 0x16: period += signed param (no <<4).
 * 0EF6:0AA1 */
static void ampTickExtraFinePorta(AmpTrack *tr)
{
    uint8_t ch = g_amp_cur_track;
    int16_t ax = (int8_t)tr->param;
    uint16_t cx = (uint16_t)(tr->period + ax);
    CdiSampleInfo *si;

    if ((int16_t)cx < 0)
        cx = 100;
    else {
        if (ax == 0)
            return;
        if (cx <= 100)
            cx = 100;
    }
    tr->period = cx;
    si = amp_si(ch);
    si->freq = amp_period_freq(si->rate, cx);
    cdiSetFrequency(ch, si->freq);
}

/* ampTickPortamento - cmd 4: period += param<<4, clamped 100..0xD5FF (an
 * upward overflow wraps unclamped).  0EF6:0AF7 */
static void ampTickPortamento(AmpTrack *tr)
{
    uint8_t ch = g_amp_cur_track;
    uint16_t cx = tr->period;
    uint16_t ax = (uint16_t)((int16_t)(int8_t)tr->param << 4);
    CdiSampleInfo *si;

    if (!ax)
        return;
    if (!((int16_t)ax < 0))
        cx = (uint16_t)(cx + ax);
    else {
        ax = (uint16_t)-ax;
        if (ax > cx)
            cx = 100;
        else {
            cx = (uint16_t)(cx - ax);
            if (cx <= 100)
                cx = 100;
        }
    }
    if (cx >= 0xD5FF)
        cx = 0xD5FF;
    tr->period = cx;
    si = amp_si(ch);
    si->freq = amp_period_freq(si->rate, cx);
    cdiSetFrequency(ch, si->freq);
}

/* ampTickTonePorta - cmd 6: move the period by speed<<4 towards the target,
 * snapping to it (a zero target is never reached).  0EF6:0B63 */
static void ampTickTonePorta(AmpTrack *tr)
{
    uint8_t ch = g_amp_cur_track;
    uint16_t cx = tr->period;
    uint16_t ax = (uint16_t)((int16_t)tr->tp_speed << 4);
    uint16_t dx;
    CdiSampleInfo *si;

    if (!ax)
        return;
    dx = (uint16_t)(tr->target - cx);
    if ((int16_t)dx < 0) {
        ax = (uint16_t)-ax;
        dx = (uint16_t)-dx;
        if (ax > dx)
            goto snap;
        ax = (uint16_t)-ax;
    } else if (ax > dx)
        goto snap;
    cx = (uint16_t)(cx + ax);
    goto store;
snap:
    cx = tr->target;
    if (cx == 0)                    /* JCXZ */
        return;
store:
    tr->period = cx;
    si = amp_si(ch);
    si->freq = amp_period_freq(si->rate, cx);
    cdiSetFrequency(ch, si->freq);
}

/* ampTickArpeggio - cmd 8: frequency arp_freq[tick % 3] (also on tick 0).
 * 0EF6:0BD2 */
static void ampTickArpeggio(AmpTrack *tr)
{
    cdiSetFrequency(g_amp_cur_track, tr->arp_freq[g_amp_tick % 3]);
}

/* ampTickVibrato - cmd 9: period +- depth*sin>>7 (sine index (pos>>2)&31,
 * pos bit 7 = negative half), floor 100, not stored; pos += speed*4.
 * 0EF6:0BFB */
static void ampTickVibrato(AmpTrack *tr)
{
    uint8_t ch = g_amp_cur_track;
    uint16_t s = amp_cs16((uint16_t)(CS_SINE + 2 * ((tr->vib_pos >> 2) & 0x1F)));
    uint16_t d = (uint16_t)(((uint32_t)(tr->vib_mem & 0x0F) * s) >> 7);   /* MUL DX; SHRD 7 */
    uint16_t cx = tr->period;

    if ((int8_t)tr->vib_pos < 0)
        cx = (uint16_t)(cx - d);
    else
        cx = (uint16_t)(cx + d);
    if (cx <= 100)
        cx = 100;
    cdiSetFrequency(ch, amp_period_freq(amp_si(ch)->rate, cx));
    tr->vib_pos = (uint8_t)(tr->vib_pos + ((tr->vib_mem >> 2) & 0x3C));
}

/* ampTickTremolo - cmd 7: volume -+ depth*sin>>10 (pos bit 7 clear =
 * subtract), clamped 0..64, volume itself not stored; pos += speed*4.
 * 0EF6:0C76 */
static void ampTickTremolo(AmpTrack *tr)
{
    uint8_t ch = g_amp_cur_track;
    uint16_t s = amp_cs16((uint16_t)(CS_SINE + 2 * ((tr->trem_pos >> 2) & 0x1F)));
    uint16_t d = (uint16_t)(((uint32_t)tr->trem_depth * s) >> 10);       /* MUL DX; SHRD 10 */
    int16_t cx = tr->volume;

    if ((int8_t)tr->trem_pos < 0)
        cx = (int16_t)(cx + d);
    else {
        cx = (int16_t)(cx - d);
        if (cx < 0)
            cx = 0;
    }
    if (cx > 0x40) cx = 0x40;
    if (cx < 0) cx = 0;
    cdiSetVolume(ch, ampScaleVolume(ch, (uint16_t)cx));
    tr->trem_pos = (uint8_t)(tr->trem_pos + ((tr->trem_speed << 2) & 0x3C));
}

/* ampTickTonePortaVolSlide - cmd 0x0A.  0EF6:0CE6 */
static void ampTickTonePortaVolSlide(AmpTrack *tr)
{
    ampTickTonePorta(tr);
    ampTickVolumeSlide(tr);
}

/* ampTickVibratoVolSlide - cmd 0x0B.  0EF6:0CEF */
static void ampTickVibratoVolSlide(AmpTrack *tr)
{
    ampTickVibrato(tr);
    ampTickVolumeSlide(tr);
}

/* ampTickRetrig - cmd 0x0F: position 0 when tick % param == 0 (also tick 0).
 * 0EF6:0CF8 */
static void ampTickRetrig(AmpTrack *tr)
{
    if (!tr->param)                 /* DIV by 0 - cannot be armed with 0 */
        return;
    if (g_amp_tick % tr->param == 0)
        cdiSetPosition(g_amp_cur_track, 0);
}

/*
 * ampTickNoteDelay - cmd 0x13: on tick == param trigger the stored note.
 * Original: 0EF6:0D18.  Bug kept: the period/target come from TRACK 0's note
 * (BX is zeroed before MOV BL,[BX+7C99]) while the frequency uses this
 * track's note; the vibrato position is not reset.
 */
static void ampTickNoteDelay(AmpTrack *tr)
{
    uint8_t ch = g_amp_cur_track;
    CdiSampleInfo *si = amp_si(ch);
    uint16_t p;
    uint32_t vol;

    if (tr->param != g_amp_tick)
        return;
    tr->ticks = 0;
    p = amp_cs16((uint16_t)(CS_PERIOD + 2 * DG8(0x7C99)));   /* track 0's note */
    tr->period = p;
    tr->target = p;
    si->freq = amp_note_freq(si->rate, tr->note);
    si->volume = tr->volume;
    vol = ampScaleVolume(ch, tr->volume);
    if (!(tr->flags & 2))
        cdiPlaySample(ch, si->freq, vol);
}

/* ampTickNoteCut - cmd 0x14: volume 0 when param == 0 (every tick) or
 * tick == param; unscaled cdiSetVolume(0).  0EF6:0DAC */
static void ampTickNoteCut(AmpTrack *tr)
{
    if (tr->param == 0 || tr->param == g_amp_tick) {
        tr->volume = 0;
        cdiSetVolume(g_amp_cur_track, 0);
    }
}

/* CS:0DD0, 12 x {u16 cmd, u16 handler}; the last two (CS:0DF8) are the only
 * ones searched on tick 0. */
static const struct { uint16_t cmd; void (*fn)(AmpTrack *); } amp_tick_cmds[12] = {
    { 0x02, ampTickVolumeSlide },        /* 0A71 */
    { 0x04, ampTickPortamento },         /* 0AF7 */
    { 0x06, ampTickTonePorta },          /* 0B63 */
    { 0x09, ampTickVibrato },            /* 0BFB */
    { 0x07, ampTickTremolo },            /* 0C76 */
    { 0x0A, ampTickTonePortaVolSlide },  /* 0CE6 */
    { 0x0B, ampTickVibratoVolSlide },    /* 0CEF */
    { 0x13, ampTickNoteDelay },          /* 0D18 */
    { 0x14, ampTickNoteCut },            /* 0DAC */
    { 0x16, ampTickExtraFinePorta },     /* 0AA1 */
    { 0x08, ampTickArpeggio },           /* 0BD2 */
    { 0x0F, ampTickRetrig },             /* 0CF8 */
};

/*
 * ampDoTickEffects - run track t's active effect from the tick table.
 * Original: IST.EXE 0EF6:0E00 (AMP), near, BX = track.
 * Verified: tests/py/test_amp.py.
 */
static void ampDoTickEffects(uint16_t t)
{
    AmpTrack *tr;
    int first = 0;

    g_amp_cur_track = (uint8_t)t;
    tr = amp_track(t);
    if (tr->cmd == 0)
        return;
    if (g_amp_tick == 0)
        first = 10;
    for (int i = first; i < 12; i++) {
        if (amp_tick_cmds[i].cmd == tr->cmd) {
            amp_tick_cmds[i].fn(tr);
            return;
        }
    }
}

/* ampIncTrackTicks - saturating ticks-since-note counter.
 * Original: IST.EXE 0EF6:0E3F (AMP), near.  Verified: tests/py/test_amp.py. */
static void ampIncTrackTicks(uint16_t t)
{
    AmpTrack *tr = amp_track(t);
    if (tr->ticks != 0xFFFF)
        tr->ticks++;
}

/*
 * ampTick - one player tick: on the speed wrap read the next row (advancing
 * the order on break or at the order's end), then tick effects and tick
 * counters for all tracks.
 *
 * Original: IST.EXE 0EF6:0E56 (AMP), far.  The track loops are LOOP with
 * CX = channel count.  Verified: tests/py/test_amp.py.
 */
void ampTick(void)
{
    if (!(g_amp_status & 1) || (g_amp_status & 2))
        return;
    g_amp_tick++;
    if (g_amp_tick >= g_amp_speed) {
        g_amp_tick = 0;
        if (g_amp_break || !(g_amp_row < g_amp_rows)) {
            g_amp_break = 0;
            g_amp_cur_order++;
            if (ampNextPattern())
                return;
        }
        for (uint16_t t = 0; t < g_amp_channels; t++)
            ampReadTrackRow(t);
        g_amp_last_row = (uint16_t)g_amp_row;
        g_amp_row++;
    }
    for (uint16_t t = 0; t < g_amp_channels; t++)
        ampDoTickEffects(t);
    for (uint16_t t = 0; t < g_amp_channels; t++)
        ampIncTrackTicks(t);
}

/*
 * ampPoll - run ampTick for every elapsed tick length (catch-up loop).
 * Ticks happen while (now - last) - k*len > 0 (strict), last += len each.
 *
 * Original: IST.EXE 0EF6:0ED2 (AMP), far.  Returns -1 if not initialised or
 * re-entered; on the normal path AX is whatever UnlockDSMITime left (0 here,
 * nobody reads it).  Verified: tests/py/test_amp.py (tick counts against the
 * original for a scripted DSMI time).
 */
int16_t ampPoll(void)
{
    uint32_t len, diff;

    if (!(g_amp_initialized & 1))
        return -1;
    if (g_amp_poll_lock == 1)
        return -1;
    g_amp_poll_lock = 1;
    LockDSMITime();
    len = (uint32_t)ampGetTickLength();
    diff = GetDSMITime() - g_amp_last_tick;
    for (;;) {
        /* SUB ESI,EDI; JLE: signed (diff <= len) */
        if ((int32_t)diff <= (int32_t)len)
            break;
        diff -= len;
        ampTick();
        g_amp_last_tick += len;
        len = (uint32_t)ampGetTickLength();
    }
    g_amp_poll_lock = 0;
    UnlockDSMITime();
    return 0;
}

/*
 * ampInterrupt - timer routine (called from the NSES 100 Hz routine): guard
 * against re-entry, switch to the private stack, ampPoll.
 *
 * Original: IST.EXE 0EF6:0F4C (AMP), IRET.  The stack switch (SS:SP saved at
 * DGROUP 7BE6/7BE4, new SP = 7BE4) is meaningless in C and not reproduced.
 * Verified: tests/py/test_amp.py.
 */
void ampInterrupt(void)
{
    if (g_amp_int_lock == 1)
        return;
    g_amp_int_lock = 1;
    ampPoll();
    g_amp_int_lock = 0;
}

/*
 * ampInit - clear the player state, import MCPSampleRealAddress (required),
 * tickHz = 50, all track volumes 0x40.
 *
 * Original: IST.EXE 0EF6:0F8C (AMP), far pascal (long options).  The state is
 * cleared before the import check, so a failed init still clears it.
 * Verified: tests/py/test_amp.py.
 */
int16_t ampInit(int32_t options)
{
    DdsFn fn = 0;

    memset(DGP(0x7C4E), 0, 0x43);               /* 7C4E..7C90 */
    amp_orders = NULL;                          /* 7C5C */
    amp_instruments = NULL;                     /* 7C60 */
    memset(DGP(g_amp_tracks_base), 0, 0x5A0);   /* 7C91: 32 x 45 */
    memset(amp_track_data, 0, sizeof amp_track_data);
    if (DDSAccessFunction((const char *)&amp_cs_data[0x30 - 0x0A], &fn) != 0)
        return -1;
    amp_sample_real_address = (McpSampleRealAddressFn)fn;
    g_amp_init_options = (uint16_t)options;
    g_amp_initialized |= 1;
    g_amp_poll_lock = 0;
    g_amp_int_lock = 0;
    g_amp_tick_hz = 50;
    g_amp_last_tick = GetDSMITime();
    ampSetTrackVolume(-1, 0x40);
    return 0;
}

/* ampClose - clear the initialised flag and the status.
 * Original: IST.EXE 0EF6:1014 (AMP), far.  Verified: tests/py/test_amp.py. */
int16_t ampClose(void)
{
    if (!(g_amp_initialized & 1))
        return -1;
    g_amp_initialized &= 0xFE;
    g_amp_status = 0;
    return 0;
}

/* copy the MODULE header to DGROUP 7BE8 (0x5C bytes; pointers native) */
static void amp_copy_module(const AmpModule *m)
{
    uint8_t *d = g_amp_mod_copy;

    d[0x00] = m->type;
    memcpy(d + 0x01, &m->size, 4);
    memcpy(d + 0x05, &m->filesize, 4);
    memcpy(d + 0x09, m->name, 32);
    d[0x29] = m->channelCount;
    memcpy(d + 0x2A, m->channelPanning, 32);
    d[0x4A] = m->instrumentCount;
    amp_mod_instruments = m->instruments;      /* +4B */
    d[0x4F] = m->patternCount;
    amp_mod_patterns = m->patterns;            /* +50 */
    memcpy(d + 0x54, &m->trackCount, 2);
    amp_mod_tracks = m->tracks;                /* +56 */
    d[0x5A] = m->tempo;
    d[0x5B] = m->speed;
}

/*
 * ampPlayRange - start playing orders start..end-1 of a module: copy the
 * header, reset the state, set tempo/speed/pans, load the first order, unmute
 * all tracks, download every sample (EMS ones at their real address).
 * Returns 0, -1 (not initialised / no module), -2 (bad range or a download
 * failed: then every instrument's sample is unloaded).
 *
 * Original: IST.EXE 0EF6:1032 (AMP), far pascal (MODULE far*, long start,
 * long end, long options).  Differences: for a module of type 0 the original
 * returns AX = segment|offset of the module pointer (the port: -1); the
 * instrument loops are LOOP with CX = instrument count (0 = 65536 passes in
 * the original, 0 here).  Verified: tests/py/test_amp.py.
 */
int16_t ampPlayRange(AmpModule *m, int32_t start, int32_t end, int32_t options)
{
    if (!(g_amp_initialized & 1))
        return -1;
    if (!m)
        return -1;
    amp_copy_module(m);
    if (g_amp_mod_type == 0)
        return -1;
    if ((uint8_t)start >= g_amp_mod_ordcount)
        return -2;
    if ((uint8_t)end > g_amp_mod_ordcount)
        return -2;
    g_amp_status = 0;
    memset(DGP(g_amp_tracks_base), 0, 0x5A0);
    memset(amp_track_data, 0, sizeof amp_track_data);
    g_amp_7c66 = 0x40;      /* set twice (the type-2 test changes nothing) */
    g_amp_break = 0;
    g_amp_cur_track = 0;
    g_amp_last_row = 0;
    g_amp_sync = 0;
    g_amp_row = 0;
    g_amp_7c6b = 0;
    g_amp_speed = g_amp_mod_speed;
    g_amp_tempo = g_amp_mod_tempo;
    g_amp_tick_hz = (uint16_t)(g_amp_tempo * 50) / 125;
    g_amp_start_order = (uint8_t)start;
    g_amp_cur_order = (uint8_t)start;
    g_amp_end_order = (uint8_t)end;
    g_amp_play_options = (uint16_t)options;
    amp_orders = amp_mod_patterns;
    amp_instruments = amp_mod_instruments;
    g_amp_channels = g_amp_mod_channels;
    for (uint16_t i = 0; i < g_amp_channels; i++) {
        int8_t pan = g_amp_mod_pans[i];
        ampSetPanning(i, pan);
        g_amp_pans[i] = pan;
    }
    ampNextPattern();               /* result ignored */
    for (uint16_t i = 0; i < g_amp_channels; i++)
        ampResumeTrack(i);
    for (uint16_t i = 0; i < g_amp_mod_inscount; i++) {
        AmpInstrument *ins = &amp_instruments[i];
        int32_t r = 0;
        if (!cdi_ref_is_null(ins->sample)) {
            if (ins->sample.in_ems) {
                uint8_t *real = amp_sample_real_address(ins->sample.ems_handle, 0);
                r = cdiDownloadSample(0, real, ins->sample, ins->length);
            } else
                r = cdiDownloadSample(0, ins->sample.ptr, ins->sample, ins->length);
        }
        if ((int16_t)r) {
            for (uint16_t j = 0; j < g_amp_mod_inscount; j++)
                if (!cdi_ref_is_null(amp_instruments[j].sample))
                    cdiUnloadSample(0, amp_instruments[j].sample);
            return -2;
        }
    }
    g_amp_status = 1;
    g_amp_last_tick = GetDSMITime();
    return 0;
}

/* ampPlayModule - play all orders.  Original: IST.EXE 0EF6:121F (AMP), far
 * pascal (MODULE far*, long options); the module pointer is not NULL-checked
 * there (the port treats NULL as type 0).  Verified: tests/py/test_amp.py. */
int16_t ampPlayModule(AmpModule *m, int32_t options)
{
    if (!(g_amp_initialized & 1))
        return -1;
    if (!m || m->type == 0)
        return -1;
    return ampPlayRange(m, 0, m->patternCount, options);
}

/* ampPlayPattern - play one order (start = end = order).  Returns the order
 * (low word) if it is out of range.  Original: IST.EXE 0EF6:125A (AMP), far
 * pascal (MODULE far*, long order, long options) (unreferenced).
 * Verified: tests/py/test_amp.py. */
int16_t ampPlayPattern(AmpModule *m, int32_t order, int32_t options)
{
    if (!(g_amp_initialized & 1))
        return -1;
    if (!m || m->type == 0)
        return -1;
    if ((uint8_t)order >= m->patternCount)
        return (int16_t)order;
    return ampPlayRange(m, order, order, options);
}

/* ampStopModule - stop every track's channel, unload every sample.
 * Original: IST.EXE 0EF6:1296 (AMP), far.  Returns 0, -1 (not initialised),
 * -2 (not playing).  Verified: tests/py/test_amp.py. */
int16_t ampStopModule(void)
{
    if (!(g_amp_initialized & 1))
        return -1;
    if (!(g_amp_status & 1))
        return -2;
    g_amp_status &= 0xFC;
    for (uint16_t i = 0; i < g_amp_channels; i++)
        cdiStopChannel(i);
    for (uint16_t i = 0; i < g_amp_mod_inscount; i++)
        if (!cdi_ref_is_null(amp_instruments[i].sample))
            cdiUnloadSample(0, amp_instruments[i].sample);
    return 0;
}

/* ampPauseModule - pause all tracks, status bit 1.
 * Original: IST.EXE 0EF6:12FE (AMP), far (unreferenced).  Verified: tests/py/test_amp.py. */
int16_t ampPauseModule(void)
{
    if (!(g_amp_initialized & 1))
        return -1;
    if (g_amp_status & 2)
        return -2;
    for (uint16_t i = 0; i < g_amp_channels; i++)
        ampPauseTrack(i);
    g_amp_status |= 2;
    return 0;
}

/* ampResumeModule - clear status bit 1, resume all tracks.
 * Original: IST.EXE 0EF6:1338 (AMP), far (unreferenced).  Verified: tests/py/test_amp.py. */
int16_t ampResumeModule(void)
{
    if (!(g_amp_initialized & 1))
        return -1;
    if (!(g_amp_status & 2))
        return -2;
    g_amp_status &= 0xFD;
    for (uint16_t i = 0; i < g_amp_channels; i++)
        ampResumeTrack(i);
    return 0;
}

/* ampGetModuleStatus - status byte (bit0 playing, bit1 paused).
 * Original: IST.EXE 0EF6:1372 (AMP), far.  Verified: tests/py/test_amp.py. */
int16_t ampGetModuleStatus(void)
{
    if (!(g_amp_initialized & 1))
        return -1;
    return g_amp_status;
}

/* ampGetTrackStatus - track flags.  Original: IST.EXE 0EF6:138A (AMP), far
 * pascal (long track) (unreferenced).  Verified: tests/py/test_amp.py. */
int16_t ampGetTrackStatus(int32_t track)
{
    if (!(g_amp_initialized & 1))
        return -1;
    if ((uint32_t)track >= 32)
        return -1;
    return (int16_t)amp_track((uint16_t)track)->flags;
}

/* ampPauseTrack - flag bit 1 + cdiMuteChannel.  -2 if the track or the
 * module is already paused.  Original: IST.EXE 0EF6:13B9 (AMP), far pascal
 * (long track).  Verified: tests/py/test_amp.py. */
int16_t ampPauseTrack(int32_t track)
{
    AmpTrack *tr;

    if (!(g_amp_initialized & 1))
        return -1;
    if ((uint32_t)track >= 32)
        return -1;
    tr = amp_track((uint16_t)track);
    if (tr->flags & 2)
        return -2;
    if (g_amp_status & 2)
        return -2;
    tr->flags |= 2;
    cdiMuteChannel(track);
    return 0;
}

/* ampResumeTrack - cdiUnmuteChannel, then clear flag bit 1 (-2 if it was not
 * set).  The unmute happens before the range check, and for track >= 32 the
 * result is the CDI call's AX.  Original: IST.EXE 0EF6:1407 (AMP), far pascal
 * (long track).  Verified: tests/py/test_amp.py. */
int16_t ampResumeTrack(int32_t track)
{
    AmpTrack *tr;
    int32_t r;

    if (!(g_amp_initialized & 1))
        return -1;
    if (g_amp_status & 2)
        return -2;
    r = cdiUnmuteChannel(track);
    if ((uint32_t)track >= 32)
        return (int16_t)r;
    tr = amp_track((uint16_t)track);
    if (!(tr->flags & 2))
        return -2;
    tr->flags &= 0xFFFD;
    return 0;
}

/* ampGetPattern - current order.  Original: IST.EXE 0EF6:1458 (AMP), far
 * (unreferenced).  Verified: tests/py/test_amp.py. */
int16_t ampGetPattern(void)
{
    if (!(g_amp_initialized & 1))
        return -1;
    return g_amp_cur_order;
}

/* ampGetRow - last row read.  Original: IST.EXE 0EF6:146F (AMP), far
 * (unreferenced).  Verified: tests/py/test_amp.py. */
int16_t ampGetRow(void)
{
    if (!(g_amp_initialized & 1))
        return -1;
    return (int16_t)g_amp_last_row;
}

/* ampGetSync - read and clear the 0x8E sync byte.  Original: IST.EXE
 * 0EF6:1484 (AMP), far (unreferenced).  Verified: tests/py/test_amp.py. */
int16_t ampGetSync(void)
{
    int16_t v;

    if (!(g_amp_initialized & 1))
        return -1;
    v = g_amp_sync;
    g_amp_sync = 0;
    return v;
}

/* ampGetTempo - AL = speed, AH = BPM.  Original: IST.EXE 0EF6:14A0 (AMP),
 * far (unreferenced).  Verified: tests/py/test_amp.py. */
uint16_t ampGetTempo(void)
{
    if (!(g_amp_initialized & 1))
        return 0xFFFF;
    return (uint16_t)(g_amp_speed | g_amp_tempo << 8);
}

/* ampSetTempo - low byte = speed, next byte = BPM, 0 = unchanged.  Returns
 * the new tickHz, or the argument's low word when the BPM byte is 0.
 * Original: IST.EXE 0EF6:14B9 (AMP), far pascal (long).  Verified: tests/py/test_amp.py. */
int16_t ampSetTempo(int32_t speed_bpm)
{
    uint8_t al = (uint8_t)speed_bpm, ah = (uint8_t)(speed_bpm >> 8);

    if (!(g_amp_initialized & 1))
        return -1;
    if (al)
        g_amp_speed = al;
    if (!ah)
        return (int16_t)speed_bpm;
    g_amp_tempo = ah;
    g_amp_tick_hz = (uint16_t)(ah * 50) / 125;
    return (int16_t)g_amp_tick_hz;
}

/* ampGetTrackData - fill the TRACKDATA at DGROUP 7C44 and return it (NULL if
 * not initialised).  Off by one kept: track == channel count is accepted
 * (and for 32 channels the CS:0045 table then yields track 0's record).
 * Original: IST.EXE 0EF6:14F6 (AMP), far pascal (long track) (unreferenced).
 * Verified: tests/py/test_amp.py. */
AmpTrackData *ampGetTrackData(int32_t track)
{
    AmpTrackData *td = g_amp_trackdata;
    const AmpTrack *tr;

    if (!(g_amp_initialized & 1))
        return NULL;
    if ((uint32_t)track > g_amp_channels) {
        memset(td, 0, sizeof *td);
        return td;
    }
    tr = amp_track((uint16_t)track);
    td->flags = tr->flags;
    td->note = tr->note;
    td->instrument = tr->instrument;
    td->volume = tr->volume;
    td->ticks = tr->ticks;
    td->cmd = (uint8_t)(tr->cmd | 0x80);
    td->param = tr->param;
    td->pan = (uint8_t)tr->pan;
    return td;
}

/* ampBreakPattern - +1 / -1: skip to the next / previous order (not before
 * order 0).  Other values: returns the argument's low word.  Original:
 * IST.EXE 0EF6:1577 (AMP), far pascal (long).  Verified: tests/py/test_amp.py. */
int16_t ampBreakPattern(int32_t dir)
{
    if (!(g_amp_initialized & 1))
        return -1;
    if (g_amp_cur_order == 0 && dir < 0)
        return -1;
    if (dir == 1 || dir == -1) {
        g_amp_cur_order = (uint8_t)(g_amp_cur_order + (uint8_t)dir);
        return ampNextPattern();
    }
    return (int16_t)dir;
}

/* ampGetTickLength - 10000 / tickHz in DSMI time units (0.1 ms).
 * Original: IST.EXE 0EF6:15B9 (AMP), far -> DX:AX.  Not initialised: AX =
 * FFFFh with DX unchanged (the port: 0xFFFF).  tickHz 0 (BPM 1 or 2) divides
 * by zero in the original (port: 0xFFFF).  Verified: tests/py/test_amp.py. */
int32_t ampGetTickLength(void)
{
    if (!(g_amp_initialized & 1))
        return 0xFFFF;
    if (g_amp_tick_hz == 0)
        return 0xFFFF;
    return (int32_t)(10000u / g_amp_tick_hz);
}

/* ampSetPanning - store the pan and cdiSetPanning.  Original: IST.EXE
 * 0EF6:15E5 (AMP), far pascal (long track, long pan).  Verified: tests/py/test_amp.py. */
int16_t ampSetPanning(int32_t track, int32_t pan)
{
    if (!(g_amp_initialized & 1))
        return -1;
    if ((uint32_t)track >= 32)
        return -1;
    amp_track((uint16_t)track)->pan = (int8_t)pan;
    return (int16_t)cdiSetPanning(track, pan);
}

/* ampSetTrackVolume - per-track volume (DSMI's ampSetMasterVolume); track -1
 * = all 32.  Only the low word of the track is range-checked.  No init check.
 * Original: IST.EXE 0EF6:1625 (AMP), far pascal (long track, long vol).
 * Verified: tests/py/test_amp.py. */
int16_t ampSetTrackVolume(int32_t track, int32_t vol)
{
    if (track == -1) {
        for (int i = 0; i < 32; i++)
            DG8(0x8571 + i) = (uint8_t)vol;
    } else if ((uint16_t)track <= 0x1F)
        DG8(0x8571 + (uint16_t)track) = (uint8_t)vol;
    return (int16_t)vol;
}
