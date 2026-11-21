/*
 * vfs.h - ovl569A: the game's virtual file system over FLiB (.CKD) archives.
 *
 * Up to 8 archives are "mounted": their trailing directory is read into a
 * table of 0x38-byte records.  vfs_open() looks a name up in the mounted
 * archives (in mount order, case-insensitive, 32 characters) and falls back
 * to a real DOS file (vfile.h).  Handles index a table of 0x38-byte records;
 * archive members share the archive's vfile handle and keep their own
 * position.  All functions were far pascal; parameter order is the logical one.
 *
 * Layout (byte-exact, so the original's out-of-range accesses behave alike):
 *   - archive slots: dg[] 16E0:E86C, 8 x 12 bytes (VfsArchive);
 *   - record tables: native heap blocks (16E0:E868 / slot+8 were far
 *     pointers), records laid out as VfsRecord.
 */
#ifndef LALE_ENGINE_VFS_H
#define LALE_ENGINE_VFS_H

#include <stddef.h>
#include <stdint.h>

#include "engine/dgroup.h"

#pragma pack(push, 1)
/* One open file / one archive directory entry (0x38 bytes). */
typedef struct VfsRecord {
    int16_t  handle;       /* +00 vfile handle (the archive's for members); -1 = free */
    char     name[32];     /* +02 member name, NUL padded (FiLE entry +04) */
    uint32_t offset;       /* +22 absolute offset of the data in the archive (entry +35) */
    int32_t  size;         /* +26 stored size (entry +39) */
    int32_t  orig_size;    /* +2A original size (entry +3D), unused */
    int32_t  pos;          /* +2E current position inside the member */
    uint32_t entry_31;     /* +32 entry bytes +31..+34 (tail of the 8.3 name + flags), unused */
    int16_t  in_archive;   /* +36 1 = archive member, 0 = real file */
} VfsRecord;

/* One mounted archive (12 bytes at 16E0:E86C + 12*i). */
typedef struct VfsArchive {
    int16_t  handle;       /* +00 vfile handle of the .CKD; -1 = free slot */
    int16_t  count;        /* +02 number of directory entries (low word of header +04) */
    uint32_t tag_far;      /* +04 far ptr given to vfs_mount_archive (native: vfs_arch_tag[]) */
    uint32_t dir_far;      /* +08 far ptr to the record table (native: vfs_arch_dir[]) */
} VfsArchive;
#pragma pack(pop)
_Static_assert(sizeof(VfsRecord) == 0x38, "VfsRecord is 0x38 bytes");
_Static_assert(offsetof(VfsRecord, offset) == 0x22, "VfsRecord.offset");
_Static_assert(offsetof(VfsRecord, pos) == 0x2E, "VfsRecord.pos");
_Static_assert(offsetof(VfsRecord, in_archive) == 0x36, "VfsRecord.in_archive");
_Static_assert(sizeof(VfsArchive) == 12, "VfsArchive is 12 bytes");

#define VFS_MAX_ARCHIVES 8
/* 16E0:E866 int  number of records in vfs_files */
#define g_vfs_count     DGS16(0xE866)
/* 16E0:E86C VfsArchive[8] */
#define VFS_ARCHIVES_OFF 0xE86C
#define g_vfs_archives  ((VfsArchive *)DGP(VFS_ARCHIVES_OFF))

/* 16E0:E868 far ptr -> calloc'ed VfsRecord[g_vfs_count] (+1 spare, see vfs.c) */
extern VfsRecord *vfs_files;
/* Native halves of the slots' far pointers.  Index 8 exists because
 * vfs_unmount_archive accepts slot 8 (reads past the table). */
extern void      *vfs_arch_tag[VFS_MAX_ARCHIVES + 1];
extern VfsRecord *vfs_arch_dir[VFS_MAX_ARCHIVES + 1];

int16_t vfs_check_handle(int16_t h);                            /* o569A_check_handle 569A:0000 */
int16_t vfs_toupper(int16_t c);                                 /* o569A_toupper      569A:002F */
int16_t vfs_mount_archive(const char *path, void *tag);         /* o569A_mount_archive 569A:0047 */
int16_t vfs_unmount_archive(int16_t slot);                      /* o569A_unmount_archive 569A:0266 */
int16_t vfs_init(int16_t count);                                /* o569A_init  569A:02CB */
int16_t vfs_open(const char *name, int16_t mode);               /* o569A_open  569A:0342 */
int16_t vfs_close(int16_t h);                                   /* o569A_close 569A:04CA */
int32_t vfs_read(int16_t h, void *buf, int32_t count);          /* o569A_read  569A:053C */
int32_t vfs_write(int16_t h, const void *buf, int32_t count);   /* o569A_write 569A:06F7 */
int16_t vfs_seek(int16_t h, int32_t offset, int16_t whence);    /* o569A_seek  569A:074E */
int32_t vfs_tell(int16_t h);                                    /* o569A_tell  569A:08B2 */
int16_t vfs_flush(int16_t h);                                   /* o569A_flush 569A:090A */
int16_t vfs_eof(int16_t h);                                     /* o569A_eof   569A:093F */

/* Tests: forget the tables (after dg_reset / bc_farheap_reset). */
void vfs_reset(void);

#endif
