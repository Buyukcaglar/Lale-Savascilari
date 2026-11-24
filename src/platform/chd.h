/*
 * chd.h - the game CD as a CHD file (MAME's "Compressed Hunks of Data", v5).
 *
 * The CD edition of the game keeps its disc as DATA/LALE.CHD: every sector of
 * the data track and of the two audio tracks, compressed (about 60 % of the
 * 331 MB of a cue/bin image).  The port writes that file itself - from a
 * cue/bin image or from the disc in a drive (src/platform/install.c) - and
 * the virtual CD-ROM drive (cdrom.c) then reads its sectors from it: the
 * game's files through iso9660.c, the music through cdrom_render().
 *
 * Format: CHD v5 as chdman's "createcd" writes it - hunks of 8 frames of
 * 2448 bytes (2352 sector + 96 subcode), 'CHT2' track metadata, every track
 * padded to 4 frames, audio stored big-endian - so the file also opens in
 * MAME, chdman and everything that reads CHDs with libchdr.
 * Codecs written:  cdzl (deflate; sync and ECC of regular Mode 1 sectors left
 *                  out), cdfl (FLAC, audio), stored hunks, references to an
 *                  identical earlier hunk.
 * Codecs read:     those, i.e. also other programs' CHDs as long as they use
 *                  cdzl / cdfl only (chdman createcd -c cdzl,cdfl); cdlz
 *                  (LZMA) and cdzs (Zstandard) hunks are refused with a
 *                  message.
 * Reference: MAME src/lib/util/chd.cpp, chdcodec.cpp, cdrom.cpp, huffman.cpp.
 */
#ifndef LALE_PLATFORM_CHD_H
#define LALE_PLATFORM_CHD_H

#include <stddef.h>
#include <stdint.h>

#include "platform/cdrom.h"

typedef struct Chd Chd;

/* done/total in sectors; return non-zero to cancel */
typedef int (*ChdProgressFn)(void *ctx, uint32_t done, uint32_t total);

typedef struct ChdStats {
    uint32_t sectors;           /* sectors of the disc                         */
    uint32_t unreadable;        /* sectors the source could not give (stored as zeros) */
    uint64_t bytes;             /* size of the CHD                             */
    uint8_t  sha1[20];          /* the CHD's overall SHA-1 (chdman info)       */
} ChdStats;

/* Writes a CD to `path`: tracks (number, audio, start, sectors, sector_size:
 * 2048 = keep Mode 1 user data only) up to `leadout`, sectors from `src` in
 * the order of a .bin file.  0, 1 = cancelled, -1 = error (text in err); the
 * file is removed unless 0. */
int chd_create_cd(const char *path, const CdromTrack *tracks, int ntracks, uint32_t leadout,
                  const CdromSource *src, ChdProgressFn progress, void *pctx,
                  ChdStats *stats, char *err, size_t errlen);

/* Opens a CD CHD.  NULL and a text in err on failure. */
Chd *chd_open(const char *path, char *err, size_t errlen);
void chd_close(Chd *c);
/* The disc's tracks (at most CDROM_MAX_TRACKS) and lead-out; returns the track count. */
int  chd_tracks(const Chd *c, CdromTrack *tracks, uint32_t *leadout);
/* One sector in the order of a .bin file (2352 bytes).  0 or -1. */
int  chd_read_sector(Chd *c, uint32_t lba, uint8_t *raw);
/* Reads every hunk and compares the SHA-1 sums with the header.  0 = intact,
 * 1 = cancelled, -1 = damaged (text in err). */
int  chd_verify(Chd *c, ChdProgressFn progress, void *pctx, char *err, size_t errlen);
const uint8_t *chd_sha1(const Chd *c);          /* overall SHA-1 from the header */

/* Opens `path` and makes it the disc of the virtual CD-ROM drive (cdrom.c),
 * which closes it again with cdrom_close().  0, or -1 and a text in err. */
int  chd_mount(const char *path, char *err, size_t errlen);

#endif
