/*
 * iso9660.h - the files of an ISO 9660 volume (the game CD's data track).
 *
 * Read-only, level 1/2 names, no extensions (Joliet, Rock Ridge): what a DOS
 * CD-ROM of 1996 carries and what MSCDEX shows of it.  Names compare without
 * regard to case and to the ";version" suffix (the game CD has none,
 * docs/DISC.md).  The volume is read through a callback, one 2048-byte logical
 * sector at a time: cdrom_read_data() for the virtual drive.
 */
#ifndef LALE_PLATFORM_ISO9660_H
#define LALE_PLATFORM_ISO9660_H

#include <stdint.h>

typedef int (*IsoReadFn)(void *ctx, uint32_t lba, uint8_t *sector);   /* 0 = ok */

typedef struct IsoFs {
    IsoReadFn read;
    void     *ctx;
    uint32_t  root_lba, root_size;      /* extent of the root directory        */
    uint32_t  volume_sectors;           /* volume space size                   */
    char      volume_id[33];            /* trailing blanks removed             */
} IsoFs;

typedef struct IsoEntry {
    char     name[40];                  /* upper case, without ";1"            */
    uint32_t lba, size;
    uint8_t  is_dir;
    uint8_t  date[7];                   /* years since 1900, month, day, hour, minute, second, zone */
} IsoEntry;

/* Reads the primary volume descriptor (sector 16 onwards).  0, or -1 when the
 * volume is not ISO 9660 or cannot be read. */
int iso_mount(IsoFs *fs, IsoReadFn read, void *ctx);

/* Looks a file or directory up: path components separated by '\\' or '/', an
 * empty path is the root directory.  0 and *e, or -1. */
int iso_find(IsoFs *fs, const char *path, IsoEntry *e);

/* Calls fn for every entry of a directory ("" = root), in the order of the
 * directory; fn returns non-zero to stop.  0, or -1 when there is no such
 * directory. */
typedef int (*IsoListFn)(void *ctx, const IsoEntry *e);
int iso_list(IsoFs *fs, const char *dir, IsoListFn fn, void *ctx);

/* Reads n bytes of a file's extent from byte offset pos into buf; returns the
 * number of bytes read (less than n at the end of the file or on an error). */
uint32_t iso_read(IsoFs *fs, const IsoEntry *e, uint32_t pos, void *buf, uint32_t n);

#endif
