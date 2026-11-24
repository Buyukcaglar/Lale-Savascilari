/*
 * cdrom.h - virtual CD-ROM drive behind an MSCDEX-style device interface.
 *
 * Replaces MSCDEX + the CD-ROM device driver the original talks to through
 * INT 2Fh (AX=1500h install check, AX=1510h send device request).  The disc is
 * a cue/bin image (e.g. the redump dump of the original CD, or the simulated
 * image built by tools/cdsim.py) or another sector source: the game's own
 * DATA/LALE.CHD (src/platform/chd.c).  Red Book audio is rendered as 44.1 kHz
 * 16-bit stereo PCM; the host pulls it with cdrom_render() and plays it.  The
 * data track is read through cdrom_read_data() (src/platform/iso9660.c: the
 * files of the disc).
 *
 * The decompiled game code (src/game/cd5737.c) builds the request blocks
 * byte for byte like the original; this file interprets them like a DOS CD
 * driver does (behaviour per the MSCDEX 2.x spec, cross-checked against
 * DOSBox-X where the spec is vague - noted per field in cdrom.c).
 *
 * Not thread-safe: call everything, including cdrom_render(), from one thread.
 */
#ifndef LALE_PLATFORM_CDROM_H
#define LALE_PLATFORM_CDROM_H

#include <stdint.h>
#include <stddef.h>

#define CDROM_FRAMES_PER_SECTOR 588   /* 2352 bytes / (2 ch * 2 bytes)        */
#define CDROM_SECTORS_PER_SEC   75
#define CDROM_LEADIN            150   /* MSF 00:02:00 = LBA 0                 */
#define CDROM_MAX_TRACKS        99

/* drive letter index (0 = A:) the virtual drive appears as; D: like the
 * usual single-CD DOS setup and like tools/dosbox/ist_cd.conf */
#define CDROM_DRIVE_INDEX       3

typedef struct CdromTrack {
    uint8_t  number;
    uint8_t  audio;          /* 1 = audio, 0 = data                      */
    uint32_t start;          /* absolute LBA of INDEX 01                 */
    uint32_t sectors;        /* length up to the next track / lead-out   */
    uint16_t sector_size;    /* cue/bin: bytes per sector in the image file */
    int      file;           /* cue/bin: index into the image's file table  */
    uint64_t file_offset;    /* cue/bin: byte offset of `start` in that file */
} CdromTrack;

/* Where the sectors of a disc come from when it is not a cue/bin image. */
typedef struct CdromSource {
    /* One sector as 2352 bytes in the order of a .bin file: audio as
       little-endian samples, data as the raw sector (sync, header, user data,
       EDC/ECC).  Returns 0, or -1 when the sector cannot be read. */
    int  (*read_sector)(void *ctx, uint32_t lba, uint8_t *raw);
    void (*close)(void *ctx);               /* may be NULL */
    void  *ctx;
} CdromSource;

/* Open a cue sheet (FILE ... BINARY, TRACK nn AUDIO|MODE1/2352|MODE1/2048|
 * MODE2/2352, INDEX 00/01, PREGAP).  Returns 0 on success, -1 on error
 * (message in cdrom_error()).  Opening a new image closes the previous one. */
int  cdrom_open(const char *cue_path);
/* Open a disc given by its tracks (number, audio, start, sectors), lead-out and
 * sector source.  The source is closed by cdrom_close() - and by this call
 * when it fails. */
int  cdrom_open_source(const CdromTrack *tracks, int ntracks, uint32_t leadout, const CdromSource *src);
void cdrom_close(void);
int  cdrom_is_open(void);
const char *cdrom_error(void);

/* Disc layout (valid while open). */
int  cdrom_track_count(void);
const CdromTrack *cdrom_track(int number);   /* by track number, NULL if none */
uint32_t cdrom_leadout(void);                /* absolute LBA of the lead-out  */

/* One raw sector (2352 bytes, see CdromSource), or the 2048 bytes of user data
 * of a data-track sector (Mode 1, Mode 2 form 1).  0, or -1: no such sector,
 * read error, or (cdrom_read_data) an audio sector. */
int  cdrom_read_raw(uint32_t lba, uint8_t *raw);
int  cdrom_read_data(uint32_t lba, uint8_t *user);

/* ---- MSCDEX ---------------------------------------------------------------
 * mscdex_present(): whether INT 2Fh AX=1500h finds MSCDEX.  When the CD-music
 * option is off the port behaves as if MSCDEX were not loaded.             */
void mscdex_set_present(int present);
int  mscdex_present(void);

/* INT 2Fh AX=1500h: returns BX = number of CD drives, CX = first drive index.
 * Without MSCDEX the registers come back unchanged (DOS has no handler). */
void mscdex_install_check(uint16_t *bx, uint16_t *cx);

/* INT 2Fh AX=1510h: send the device request `req` (the request header bytes
 * exactly as the game built them) to drive index `drive`.  For IOCTL requests
 * the far transfer pointer at req+14 is not usable on the host, so the caller
 * passes the transfer buffer as `xfer`.  The driver writes the status word
 * into req[3..4] and IOCTL results into `xfer`.  Without MSCDEX nothing is
 * written. */
void mscdex_send_request(uint8_t drive, uint8_t *req, uint8_t *xfer);

/* ---- audio output -------------------------------------------------------- */
/* Fill `frames` stereo frames (interleaved L,R int16 at 44.1 kHz) and advance
 * the drive's play position.  Writes silence when nothing plays. */
void cdrom_render(int16_t *out, size_t frames);

/* Current head position (absolute LBA) and whether audio is playing. */
uint32_t cdrom_position(void);
int  cdrom_playing(void);

#endif
