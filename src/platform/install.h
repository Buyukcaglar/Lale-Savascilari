/*
 * install.h - bringing the player's copy of the game into the port's own
 * folders (port code, not part of the original game).  Implementation:
 * src/platform/install.c.  Player's view: docs/INSTALL.md.
 *
 * Beside the executable (<base>):
 *   DATA/   what the game reads.  From a CD - a disc in a drive or a disc
 *           image - one file, LALE.CHD, the whole disc compressed (chd.h);
 *           the game reads its files out of it and plays its audio tracks.
 *           From the files of the 2005 release - a ZIP, 7Z or RAR archive or
 *           a folder - the game's data files, copied.
 *   SAVE/   everything the game writes, whatever the source was: saved
 *           games, the party file, the sound settings.
 * The import never changes a file that is already in SAVE.
 *
 * A source is
 *   a folder             with the game files in it or below it, or with a
 *                        disc image in it (a redumper dump folder)
 *   .zip .7z .rar        the same, packed (ZIP is read here; the others, and
 *                        ZIPs this code cannot read, by the system's bsdtar
 *                        or 7-Zip)
 *   .cue                 cue sheet + track files -> LALE.CHD
 *   .iso                 the data track alone -> LALE.CHD (no CD music)
 *   .chd                 a CD image in CHD form -> copied (cdzl/cdfl only)
 *   "D:"                 an optical drive (drive.h) -> LALE.CHD
 *
 * redumper's split of this disc: the data track was mastered 48 samples
 * early, its post-gap and the audio tracks were not (tools/cddump.py), and
 * redumper cuts everything at the data track's offset.  A cue/bin image with
 * that signature - the last sectors of the data track without a sync
 * pattern, the pattern found a few bytes later - is read at the shifted
 * position from there on, which gives the redump.org layout.
 */
#ifndef LALE_PLATFORM_INSTALL_H
#define LALE_PLATFORM_INSTALL_H

#include <stddef.h>
#include <stdint.h>

enum { INSTALL_NONE = 0, INSTALL_FILES = 1, INSTALL_DISC = 2 };

/* Called often during an import: what is going on and how far (total 0 =
   unknown).  Returns nonzero to cancel. */
typedef int (*InstallProgressFn)(void *ctx, const char *text, uint64_t done, uint64_t total);

/* 1 when `dir` holds the data files the game needs (3D.CKD and the rest). */
int install_have_files(const char *dir);
/* 1 when <data_dir>/LALE.CHD exists; its path in `path` (may be NULL). */
int install_have_chd(const char *data_dir, char *path, size_t size);

/* Imports `source` into <base>/DATA and <base>/SAVE (created as needed).
   Returns INSTALL_FILES or INSTALL_DISC, 0 when cancelled, -1 on an error
   (message in err). */
int install_import(const char *base, const char *source, InstallProgressFn progress, void *pctx,
                   char *err, size_t errlen);

/* Sources worth trying in `dir`: disc images, the folder itself and its
   subfolders when they hold the game, archives - in that order.  Returns how
   many were written to paths[][1024]. */
int install_find_sources(const char *dir, char paths[][1024], int max);

/* Writes <save_dir>/SES.CFG - Sound Blaster 16 at 220h, IRQ 7, DMA 5,
   22050 Hz, what the shipped file of the 2005 release holds - unless the
   game would already find one (in save_dir or data_dir).  The CD carries
   none: its installer wrote it. */
void install_default_sescfg(const char *data_dir, const char *save_dir);

/* mkdir -p for one level; 0 when the folder exists afterwards. */
int install_mkdir(const char *path);

#endif
