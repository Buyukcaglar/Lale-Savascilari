/*
 * cd5737.c - CD-audio module, decompiled from IST.EXE ovl5737 (5737:0000-04BD).
 *
 * The original talks to MSCDEX through INT 2Fh; here those calls go to the
 * virtual drive in src/platform/cdrom.c.  Request blocks are built byte for
 * byte in a mirror of the original DGROUP area (16E0:E8CC-E9CC), so stale
 * bytes left from one request to the next are the original's too.
 *
 * Verified: tests/test_cdaudio.c (behaviour against the asm in
 * analysis/ghidra_ist/asm/ovl5737.asm, 2026-11-20).
 */
#include "game/cd5737.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "platform/cdrom.h"

/* ---- DGROUP mirror -------------------------------------------------------- */

#pragma pack(push, 1)
typedef struct Cd5737Data {             /* 16E0:E8CC .. 16E0:E9CC */
    char     dev_name[8];               /* E8CC  driver name (drive_info)       */
    uint32_t length;                    /* E8D4  length of the last play, sectors */
    char     cfg_name[8];               /* E8D8  config file name (read_cfg)    */
    uint8_t  q[10];                     /* E8E0  last Q-channel info (from +1)  */
    uint8_t  req[200];                  /* E8EA  device request block           */
    uint16_t status;                    /* E9B2  (status lo|hi) << 8            */
    uint8_t  _e9b4[2];
    uint16_t num_drives;                /* E9B6  INT 2Fh 1500h BX               */
    char     drive_letter;              /* E9B8  'A' + drive                    */
    uint8_t  drive;                     /* E9B9  INT 2Fh 1500h CL               */
    uint8_t  subunit;                   /* E9BA  set only by drive_info: 0      */
    uint8_t  _e9bb[12];
    uint8_t  disk_info[6];              /* E9C7  lo track, hi track, lead-out   */
} Cd5737Data;
#pragma pack(pop)

#define DG_OFF(f) (0xE8CC + offsetof(Cd5737Data, f))
_Static_assert(DG_OFF(length)     == 0xE8D4, "E8D4");
_Static_assert(DG_OFF(q)          == 0xE8E0, "E8E0");
_Static_assert(DG_OFF(req)        == 0xE8EA, "E8EA");
_Static_assert(DG_OFF(status)     == 0xE9B2, "E9B2");
_Static_assert(DG_OFF(num_drives) == 0xE9B6, "E9B6");
_Static_assert(DG_OFF(drive)      == 0xE9B9, "E9B9");
_Static_assert(DG_OFF(subunit)    == 0xE9BA, "E9BA");
_Static_assert(DG_OFF(disk_info)  == 0xE9C7, "E9C7");

static Cd5737Data d;                    /* BSS: zero at start-up, like c0  */
static uint16_t   cd_active;            /* 16E0:8F4E                        */
static uint32_t   cd_start;             /* 16E0:8F50  start of the last play */
static FILE      *cfg_file;             /* 16E0:E9CD  far FILE*             */
/* 16E0:8F54 "rt" - fopen mode used by read_cfg */

uint16_t cd5737_active(void) { return cd_active; }
uint32_t cd5737_start(void)  { return cd_start; }
uint32_t cd5737_length(void) { return d.length; }

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* status word -> E9B2 and the "no error" result, as in 5737:017E / 0495:
 *   AL = status_lo | status_hi; CBW; SHL AX,8; result = !(AX & 0x8000)
 * i.e. bit 7 of (lo | hi): the error bit (status bit 15), or an error code
 * >= 80h in the low byte. */
static int16_t take_status(void)
{
    int8_t al = (int8_t)(d.req[3] | d.req[4]);
    d.status = (uint16_t)((int16_t)al << 8);
    return (d.status & 0x8000) ? 0 : 1;
}

/*
 * o5737_cd_drive_info - INT 2Fh AX=1501h (drive device list): remember the
 * first drive's subunit and copy the device driver's name.
 * Original: ovl5737:0000.  No callers in IST.EXE.
 * Port: the virtual drive has subunit 0 and is named "MSCD001".
 */
const char *o5737_cd_drive_info(void)
{
    d.subunit = 0;
    memset(d.dev_name, 0, sizeof d.dev_name);
    /* memccpy(E8CC, header+10, ' ', 8): copies up to and including a blank */
    memcpy(d.dev_name, "MSCD001 ", 8);
    return d.dev_name;
}

/*
 * o5737_cd_read_cfg - fopen(E8D8, "rt") and return byte 4 of the FILE
 * structure (its fd), or 0.  Original: ovl5737:0048.  No callers in IST.EXE;
 * E8D8 is never filled, so the original would open "".
 * Port: kept for completeness; returns 0 like a failed fopen.
 */
int16_t o5737_cd_read_cfg(void)
{
    cfg_file = NULL;                         /* fopen("", "rt") fails */
    return cfg_file ? 1 : 0;
}

/* o5737_nop - a lone RETF.  Original: ovl5737:0071, called by o559A_fatal. */
void o5737_nop(void)
{
}

/*
 * o5737_cd_ioctl - send an IOCTL input (fn 4402h -> command 3) or output
 * (any other fn -> command 0Ch) request with transfer buffer `buf`.
 * Original: ovl5737:0086.  Always returns 1, whatever the driver says.
 * Only header bytes 0..2, 13..19 are written; the rest keeps the previous
 * request's bytes (the play request uses the same block).
 */
uint8_t o5737_cd_ioctl(uint16_t fn, uint16_t count, uint8_t *buf)
{
    d.req[0] = 0x14;                         /* request length               */
    d.req[1] = d.subunit;
    d.req[2] = (fn == 0x4402) ? 0x03 : 0x0C;
    d.req[13] = 0;                           /* media descriptor             */
    /* req+14: far transfer pointer.  The host passes `buf` directly, so the
     * 4 bytes are left as they were (they hold no meaning for the port). */
    d.req[18] = (uint8_t)count;
    d.req[19] = (uint8_t)(count >> 8);
    mscdex_send_request(d.drive, d.req, buf);
    return 1;
}

/* o5737_cd_reset - IOCTL output 02h (reset drive).  Original: ovl5737:00CF. */
void o5737_cd_reset(void)
{
    uint8_t cb[2];
    cb[0] = 0x02;
    o5737_cd_ioctl(0x4403, 1, cb);
}

/*
 * o5737_cd_detect - INT 2Fh AX=1500h, then IOCTL input 0Ah (audio disk info)
 * into E9C7.  Original: ovl5737:00EA.
 */
void o5737_cd_detect(void)
{
    uint16_t bx = 0, cx = 0;                 /* XOR BX,BX / XOR CX,CX first */
    uint8_t cb[10];
    mscdex_install_check(&bx, &cx);
    d.num_drives = bx;
    d.drive = (uint8_t)cx;
    d.drive_letter = (char)(d.drive + 'A');
    memset(cb, 0, sizeof cb);
    cb[0] = 0x0A;
    o5737_cd_ioctl(0x4402, 6, cb);
    memcpy(d.disk_info, cb + 1, 6);
}

/*
 * o5737_cd_stop - device request 85h (stop/pause audio).  Returns 0 when the
 * module is inactive, else 1 if the driver reported no error.
 * Original: ovl5737:0144.
 */
int16_t o5737_cd_stop(void)
{
    if (cd_active == 0)
        return 0;                            /* MOV AL,0: AH is undefined in the
                                                original; callers only test AL */
    memset(d.req, 0, sizeof d.req);
    d.req[0] = 0x05;                         /* sic: the original writes 5   */
    d.req[1] = 0;
    d.req[2] = 0x85;
    mscdex_send_request(d.drive, d.req, NULL);
    return take_status();
}

/*
 * o5737_cd_init - reset the drive, mark the module active, detect MSCDEX and
 * read the disc's track range.  Original: ovl5737:0198.
 * NOTE: the reset is sent before detect, i.e. to drive E9B9 = 0 (A:) on the
 * first call; the driver rejects it as an unknown unit, harmlessly.
 */
uint8_t o5737_cd_init(void)
{
    o5737_cd_reset();
    cd_active = 1;
    o5737_cd_detect();
    return 1;
}

/*
 * o5737_cd_qchannel - IOCTL input 0Ch (audio Q-channel info); copies the 10
 * bytes after the control code to `out10`.  Returns the IOCTL result (1).
 * Original: ovl5737:01A9.
 * The original's buffer is uninitialised stack; if no driver answers, `out10`
 * receives that garbage.  The port zero-fills it.  This only matters with
 * the module inactive (ui_cd_music polls regardless), where the follow-up
 * o5737_cd_play_track(0) returns at once either way.
 */
uint8_t o5737_cd_qchannel(uint8_t *out10)
{
    uint8_t cb[14] = {0};
    cb[0] = 0x0C;
    uint8_t r = o5737_cd_ioctl(0x4402, 10, cb);
    memcpy(out10, cb + 1, 10);
    return r;
}

/*
 * o5737_msf_to_sectors - 00MMSSFF (binary) -> HSG sector:
 * MM*4500 + SS*75 + FF - 150, 32-bit.  Original: ovl5737:01DF.
 */
uint32_t o5737_msf_to_sectors(uint32_t msf)
{
    uint32_t m = (msf >> 16) & 0xFF, s = (msf >> 8) & 0xFF, f = msf & 0xFF;
    return m * 4500u + s * 75u + f - 150u;
}

/*
 * o5737_cd_track_info - IOCTL input 0Bh: start sector (HSG) of `track`.
 * Original: ovl5737:024F.
 */
uint32_t o5737_cd_track_info(uint8_t track)
{
    uint8_t cb[10] = {0};
    cb[0] = 0x0B;
    cb[1] = track;
    o5737_cd_ioctl(0x4402, 6, cb);
    /* Red Book frame, second, minute, 0 read as a little-endian dword */
    return o5737_msf_to_sectors(get32(cb + 2));
}

/*
 * o5737_cd_vtoc_ok - INT 2Fh AX=1505h (read volume descriptor) for the drive;
 * 1 if the carry flag is clear.  Original: ovl5737:0291.
 * Caller: the FRPOS video opcode (3FD1:1566), as a disc-present check, only
 * when 16E0:3D5C == 1 - which main() clears in this build (0546:02DD).
 * Port: succeeds when the virtual drive has an image.
 */
int16_t o5737_cd_vtoc_ok(void)
{
    return (mscdex_present() && cdrom_is_open()) ? 1 : 0;
}

/*
 * o5737_cd_play - "is the last requested play still running?"  Reads the
 * head position from the Q channel and returns 1 if
 *     start < position < start + length - 4      (unsigned 32-bit)
 * else 0; 0FFh if the Q-channel IOCTL failed (it never does: ioctl returns 1).
 * Original: ovl5737:02D0.  Used by the menus to loop the CD track.
 */
uint8_t o5737_cd_play(void)
{
    if (o5737_cd_qchannel(d.q) == 0)
        return 0xFF;
    /* E8E7..E8E9 = absolute minute, second, frame */
    uint32_t msf = (uint32_t)d.q[7] << 16 | (uint32_t)d.q[8] << 8 | d.q[9];
    uint32_t pos = o5737_msf_to_sectors(msf);
    uint32_t end = cd_start + d.length - 4;
    if (end <= pos)
        return 0;
    if (pos <= cd_start)
        return 0;
    return 1;
}

/*
 * o5737_cd_status - IOCTL input 08h (volume size).  Original: ovl5737:0361.
 */
uint32_t o5737_cd_status(void)
{
    uint8_t cb[10] = {0};
    cb[0] = 0x08;
    o5737_cd_ioctl(0x4402, 4, cb);
    return get32(cb + 1);
}

/*
 * o5737_cd_play_track - play `track` for `length` sectors.
 *   track != 0: start = track start; length < 0 means "to the end of the
 *               track": next track's start - start, or for the last track
 *               volume size - start - 150.  Start and length are remembered.
 *   track == 0: replay the remembered start/length (the menu loop).
 * Returns 0 when the module is inactive, else 1 if the driver reported no
 * error.  Original: ovl5737:0396.
 */
int16_t o5737_cd_play_track(uint8_t track, int32_t length)
{
    const uint32_t offset = 0;              /* [BP-4]: always 0 in the original */
    uint32_t len;

    if (cd_active == 0)
        return 0;
    if (track != 0) {
        cd_start = o5737_cd_track_info(track);
        if (length < 0) {
            if (track < d.disk_info[1])     /* E9C8: highest track number */
                length = (int32_t)(o5737_cd_track_info((uint8_t)(track + 1)) - cd_start);
            else
                length = (int32_t)(o5737_cd_status() - cd_start - 150u);
        }
        d.length = (uint32_t)length;
        len = (uint32_t)length;
    } else {
        len = d.length;
    }
    len -= offset;
    cd_start += offset;
    memset(d.req, 0, sizeof d.req);
    d.req[0] = 0x16;
    d.req[1] = d.subunit;
    d.req[2] = 0x84;                        /* PLAY AUDIO, HSG addressing (req[13]=0) */
    put32(d.req + 14, cd_start);
    put32(d.req + 18, len);
    mscdex_send_request(d.drive, d.req, NULL);
    int16_t ok = take_status();
    cd_start -= offset;
    return ok;
}

/* ---- port: CD-music option ------------------------------------------------ */

int cd5737_port_enable(const char *cue_path)
{
    if (!cue_path || !*cue_path || cdrom_open(cue_path) != 0)
        return -1;
    mscdex_set_present(1);
    o5737_cd_init();
    return 0;
}
