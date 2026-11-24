/*
 * drive.h - the host's optical drives (port code, not part of the original
 * game): finding the game CD and reading it sector by sector, so the
 * importer (install.c) can write DATA/LALE.CHD from a disc in a drive.
 * Implementation: src/platform/drive.c.
 *
 * Windows: the drive is opened as \\.\X:, the table of contents comes from
 * IOCTL_CDROM_READ_TOC, data sectors are read as 2048 bytes of user data and
 * completed to raw Mode 1 sectors (cd_mode1_build), audio sectors with
 * IOCTL_CDROM_RAW_READ.  The post-gap that ends a data track before an audio
 * track (150 sectors) cannot be read this way on most drives; it is written
 * as the empty Mode 1 sectors a pressed disc carries there.
 * Other systems: no raw access is implemented - drive_find_game() finds
 * nothing and drive_open() fails; a mounted disc is imported as a folder.
 */
#ifndef LALE_PLATFORM_DRIVE_H
#define LALE_PLATFORM_DRIVE_H

#include <stddef.h>

#include "platform/cdrom.h"

/* 1 when `path` names an optical drive ("D:", "D:\"). */
int drive_is_optical(const char *path);

/* The first optical drive that holds a disc with the volume label `label`:
   its name ("D:") in out.  Returns 1, or 0 when there is none. */
int drive_find_game(const char *label, char *out, size_t size);

/* Opens the drive: the disc's tracks and lead-out, and a sector source for
   cdrom_open_source() / chd_create_cd().  Returns the number of tracks,
   or -1 (message in err).  src->close releases the drive. */
int drive_open(const char *drive, CdromTrack *tracks, uint32_t *leadout, CdromSource *src,
               char *err, size_t errlen);

#endif
