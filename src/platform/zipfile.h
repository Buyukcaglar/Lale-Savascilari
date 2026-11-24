/*
 * zipfile.h - reading ZIP archives (port code, not part of the original
 * game): the central directory, entries stored or deflated, CRC-32 checked.
 * What the importer (install.c) needs to take the game files out of the
 * .zip the 2005 release came in; other archive kinds go through the
 * system's bsdtar there.  Not handled (zip_extract fails, the importer then
 * tries bsdtar): ZIP64, encryption, methods other than 0 and 8.
 * Implementation: src/platform/zipfile.c.  Format: PKWARE APPNOTE.TXT.
 */
#ifndef LALE_PLATFORM_ZIPFILE_H
#define LALE_PLATFORM_ZIPFILE_H

#include <stddef.h>
#include <stdint.h>

typedef struct ZipFile ZipFile;

typedef struct ZipEntry {
    char     name[512];      /* as stored: '/' between folders, a folder ends in '/' */
    uint32_t csize, usize;   /* compressed, uncompressed bytes */
    uint32_t crc;
    uint32_t offset;         /* of the local header */
    uint16_t method;         /* 0 stored, 8 deflated */
    uint16_t flags;
} ZipEntry;

ZipFile *zip_open(const char *path, char *err, size_t errlen);
void     zip_close(ZipFile *z);
int      zip_count(const ZipFile *z);
const ZipEntry *zip_entry(const ZipFile *z, int i);

/* Writes entry e to the file `dest`.  `tick`, when given, is called with the
   bytes written so far and returns nonzero to stop.  Returns 0, -1 on an
   error (message in err; `dest` is removed), 1 when stopped. */
int zip_extract(ZipFile *z, const ZipEntry *e, const char *dest,
                int (*tick)(void *ctx, uint32_t bytes), void *ctx, char *err, size_t errlen);

#endif
