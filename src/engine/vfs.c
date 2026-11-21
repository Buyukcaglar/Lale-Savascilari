/*
 * vfs.c - ovl569A: the game's virtual file system over FLiB (.CKD) archives.
 *
 * Original: IST.EXE ovl569A (569A:0000-09B0), far pascal except the two
 * near cdecl helpers.  See vfs.h for the data layout and docs/FORMATS.md for
 * the archive format.
 * Verified: tests/py/test_vfs.py (unicorn differential with real archives
 * from orig/), 2026-11-20.
 *
 * Quirks kept (callers may depend on them):
 *   - handle checks accept h == g_vfs_count (one record past the table);
 *   - reading past a member's end returns the clamped count (not 0) and
 *     leaves the position at size-1, so vfs_eof() (pos == size-1) is true;
 *     a read that hits the end exactly returns 0 and leaves pos == size,
 *     where vfs_eof() says 0;
 *   - vfs_seek(h, -n, SEEK_END) on a member moves to size+n, not size-n;
 *   - vfs_flush/vfs_eof pass the vfs handle, not the record's vfile handle,
 *     to the vfile layer for real files.
 */
#include "engine/vfs.h"

#include <stdlib.h>
#include <string.h>

#include "dsmi/memalloc.h"
#include "engine/vfile.h"

VfsRecord *vfs_files;                               /* 16E0:E868 */
void      *vfs_arch_tag[VFS_MAX_ARCHIVES + 1];      /* slot +04 far ptr */
VfsRecord *vfs_arch_dir[VFS_MAX_ARCHIVES + 1];      /* slot +08 far ptr */

static uint16_t rd16le(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32le(const uint8_t *p) { return rd16le(p) | (uint32_t)rd16le(p + 2) << 16; }

/*
 * DSMI MemCalloc (0E9E:003F, farcalloc(n, 38h) on the Borland far heap);
 * NULL on failure.  The records have the original's layout, so the table
 * is a real far-heap block.
 */
static VfsRecord *mem_calloc_records(int32_t n)
{
    void *p = NULL;
    return MemCalloc(n, (int32_t)sizeof(VfsRecord), &p) ? NULL : p;
}

/*
 * vfs_check_handle - 0 if h names an open record, else -1.
 * Original: IST.EXE 569A:0000 o569A_check_handle (near cdecl).
 * Quirk: the range test is h <= g_vfs_count (jle), one too many.
 */
int16_t vfs_check_handle(int16_t h)
{
    if (h < 0 || h > g_vfs_count)
        return -1;
    if (vfs_files[h].handle == -1)
        return -1;
    return 0;
}

/*
 * vfs_toupper - ASCII a-z to upper case, everything else unchanged.
 * Original: IST.EXE 569A:002F o569A_toupper (near cdecl).
 */
int16_t vfs_toupper(int16_t c)
{
    if (c >= 'a' && c <= 'z')
        c -= 0x20;
    return c;
}

/*
 * vfs_mount_archive - open a FLiB archive and load its directory.
 * Takes the first free of the 8 slots, opens path with vfile mode 2 ("rb"),
 * checks the "FLiB" magic, seeks to the directory (header +08) and reads
 * count (header +04, low word) 0x41-byte "FiLE" entries into a new table of
 * VfsRecords.  Entries without the "FiLE" magic stay free (handle -1).
 * Returns the slot, or -1 (no slot, open/read failure, bad magic, no memory).
 * Original: IST.EXE 569A:0047 o569A_mount_archive (pascal (path, tag)).
 * Note: the slot's count and tag are written before any check, so a failed
 * mount leaves them behind (the slot stays free: handle -1).
 */
int16_t vfs_mount_archive(const char *path, void *tag)
{
    int16_t slot;
    for (slot = 0; slot < VFS_MAX_ARCHIVES; slot++)
        if (g_vfs_archives[slot].handle == -1)
            break;
    if (slot == VFS_MAX_ARCHIVES)
        return -1;
    VfsArchive *arch = &g_vfs_archives[slot];
    vfs_arch_tag[slot] = tag;

    int16_t fh = vfile_open(path, 2);
    if (fh < 0)
        return -1;
    uint8_t hdr[12];
    /* the original tests only the low word of the result here */
    if ((int16_t)vfile_read(fh, hdr, 12) != 0)
        goto fail_close;
    if (rd16le(hdr + 2) != 0x4269 || rd16le(hdr) != 0x4C46)        /* "FLiB" */
        goto fail_close;
    arch->count = (int16_t)rd16le(hdr + 4);
    vfile_seek(fh, (int32_t)rd32le(hdr + 8), 0);

    /* MemCalloc(count, 0x38, &dir); the original's pointer variable is an
     * uninitialised local that MemCalloc leaves alone on failure */
    VfsRecord *dir = mem_calloc_records(arch->count);
    if (dir == NULL)
        goto fail_close;
    vfs_arch_dir[slot] = dir;

    for (int16_t i = 0; i < arch->count; i++) {
        VfsRecord *rec = &dir[i];
        uint8_t ent[0x41];
        rec->handle = -1;
        if (vfile_read(fh, ent, 0x41) != 0) {
            MemFree(dir);                           /* slot+8 keeps the stale far ptr */
            vfs_arch_dir[slot] = NULL;
            goto fail_close;
        }
        if (rd16le(ent + 2) != 0x454C || rd16le(ent) != 0x6946)    /* "FiLE" */
            continue;
        rec->handle = fh;
        memcpy(rec->name, ent + 4, 32);
        rec->entry_31  = rd32le(ent + 0x31);
        rec->offset    = rd32le(ent + 0x35);
        rec->size      = (int32_t)rd32le(ent + 0x39);
        rec->orig_size = (int32_t)rd32le(ent + 0x3D);
        rec->pos = 0;
        rec->in_archive = 1;
    }
    arch->handle = fh;
    return slot;

fail_close:
    vfile_close(fh);
    return -1;
}

/*
 * vfs_unmount_archive - free the slot's directory and close the archive.
 * Returns 0 (also for a free slot), -1 for slot < 0 or > 8.
 * Original: IST.EXE 569A:0266 o569A_unmount_archive (pascal (slot)).
 * Quirk: slot 8 is accepted and reads the 12 DGROUP bytes after the table
 * (16E0:E8CC, the CD-ROM module's buffer); its native dir pointer is NULL.
 * Records already copied by vfs_open keep the closed vfile handle.
 */
int16_t vfs_unmount_archive(int16_t slot)
{
    if (slot < 0 || slot > VFS_MAX_ARCHIVES)
        return -1;
    VfsArchive *arch = &g_vfs_archives[slot];
    if (arch->handle != -1) {
        if (vfs_arch_dir[slot] != NULL) {
            MemFree(vfs_arch_dir[slot]);            /* far ptr not cleared */
            vfs_arch_dir[slot] = NULL;
        }
        vfile_close(arch->handle);
        arch->handle = -1;
    }
    return 0;
}

/*
 * vfs_init - allocate count records (all free), clear the 8 archive slots,
 * then vfile_init(count).  Returns vfile_init's result, or -1 if the
 * allocation failed.
 * Original: IST.EXE 569A:02CB o569A_init (pascal (count)).
 * The "h == g_vfs_count" accesses the original lets through reach the
 * memory after the block (farcalloc clears only the requested bytes; the
 * slack and the next block are whatever the far heap holds - the port's
 * table is a block of the same far heap, bc_farheap.h).  No game code
 * passes such a handle.
 * On allocation failure the original keeps whatever E868 held; so does the
 * port.  A second call leaks the old table (as the original).
 */
int16_t vfs_init(int16_t count)
{
    g_vfs_count = count;
    VfsRecord *recs = mem_calloc_records(count);
    if (recs != NULL)
        vfs_files = recs;
    if (vfs_files == NULL)
        return -1;
    for (int16_t i = 0; i < g_vfs_count; i++)
        vfs_files[i].handle = -1;
    for (int i = 0; i < VFS_MAX_ARCHIVES; i++)
        g_vfs_archives[i].handle = -1;
    return vfile_init(count);
}

/*
 * vfs_open - open name: an archive member (searched across the mounted
 * archives in slot order, case-insensitive over at most 32 characters; the
 * empty name never matches) gets a copy of its directory record, anything
 * else is opened as a real file with vfile_open(name, mode).
 * Returns the vfs handle, or -1.
 * Original: IST.EXE 569A:0342 o569A_open (pascal (name, mode)).
 */
int16_t vfs_open(const char *name, int16_t mode)
{
    int16_t h;
    for (h = 0; h < g_vfs_count; h++)
        if (vfs_files[h].handle == -1)
            break;
    if (h == g_vfs_count)
        return -1;

    int16_t found = 0;
    VfsRecord *rec = NULL;
    for (int16_t slot = 0; slot < VFS_MAX_ARCHIVES && !found; slot++) {
        if (g_vfs_archives[slot].handle == -1)
            continue;
        for (int16_t i = 0; g_vfs_archives[slot].count > i && !found; i++) {
            rec = &vfs_arch_dir[slot][i];
            if (rec->handle == -1)
                continue;
            for (int16_t j = 0; j < 32; j++) {
                if (name[j] == 0 && rec->name[j] == 0) {
                    if (j != 0)
                        found = 1;
                    break;
                }
                if (vfs_toupper((int8_t)name[j]) != vfs_toupper((int8_t)rec->name[j]))
                    break;
            }
        }
    }

    if (found) {
        memcpy(&vfs_files[h], rec, sizeof(VfsRecord));
        return h;
    }
    int16_t fh = vfile_open(name, mode);
    if (fh < 0)
        return -1;
    vfs_files[h].handle = fh;
    vfs_files[h].in_archive = 0;
    return h;
}

/*
 * vfs_close - free the record; real files are vfile_close'd (its result
 * returned), members return 0.  -1 for a bad or free handle.
 * Original: IST.EXE 569A:04CA o569A_close (pascal (h)).
 */
int16_t vfs_close(int16_t h)
{
    if (h < 0 || h > g_vfs_count)
        return -1;
    if (vfs_files[h].handle == -1)
        return -1;
    int16_t fh = vfs_files[h].handle;
    vfs_files[h].handle = -1;
    if (vfs_files[h].in_archive == 0)
        return vfile_close(fh);
    return 0;
}

/*
 * vfs_read - read count bytes.  Real files: vfile_read.  Members: seek the
 * archive to offset+pos, then
 *   - pos+count > size: read size-pos bytes (result ignored), pos = size-1,
 *     return size-pos (non-zero "error" for the callers);
 *   - else vfile_read: 0 -> pos += count, return 0; otherwise pos = size-1
 *     and return vfile_read's partial count.
 * -1 for a bad handle.
 * Original: IST.EXE 569A:053C o569A_read (pascal (h, buf, count)).
 * Hazard kept: if pos > size (possible after vfs_seek(-n, SEEK_END)), the
 * clamped count is negative and vfile_read reads (uint16)count bytes.
 */
int32_t vfs_read(int16_t h, void *buf, int32_t count)
{
    if (vfs_check_handle(h) != 0)
        return -1;
    VfsRecord *r = &vfs_files[h];
    int16_t fh = r->handle;
    if (r->in_archive == 0)
        return vfile_read(fh, buf, count);

    vfile_seek(fh, (int32_t)(r->offset + (uint32_t)r->pos), 0);
    if ((int32_t)((uint32_t)r->pos + (uint32_t)count) > r->size) {
        count = (int32_t)((uint32_t)r->size - (uint32_t)r->pos);
        vfile_read(fh, buf, count);
        r->pos = (int32_t)((uint32_t)r->size - 1u);
        return count;
    }
    int32_t res = vfile_read(fh, buf, count);
    if (res == 0) {
        r->pos = (int32_t)((uint32_t)r->pos + (uint32_t)count);
        return 0;
    }
    r->pos = (int32_t)((uint32_t)r->size - 1u);
    return res;
}

/*
 * vfs_write - real files only (vfile_write); members and bad handles -1.
 * Original: IST.EXE 569A:06F7 o569A_write (pascal (h, buf, count)).
 */
int32_t vfs_write(int16_t h, const void *buf, int32_t count)
{
    if (vfs_check_handle(h) != 0)
        return -1;
    if (vfs_files[h].in_archive != 0)
        return -1;
    return vfile_write(vfs_files[h].handle, buf, count);
}

/*
 * vfs_seek - real files: vfile_seek.  Members (returns 0, or -1):
 *   SEEK_SET: pos = min(offset, size)      (negative offsets accepted)
 *   SEEK_CUR: pos = pos+offset, at most size
 *   SEEK_END: -offset > size -> -1; offset > 0 -> size; else size - offset
 *             (= size + |offset|: past the end - original bug)
 *   other whence -> -1.
 * Original: IST.EXE 569A:074E o569A_seek (pascal (h, offset, whence)).
 */
int16_t vfs_seek(int16_t h, int32_t offset, int16_t whence)
{
    if (vfs_check_handle(h) != 0)
        return -1;
    VfsRecord *r = &vfs_files[h];
    if (r->in_archive == 0)
        return vfile_seek(r->handle, offset, whence);

    int32_t size = r->size;
    switch (whence) {
    case 0:
        r->pos = offset > size ? size : offset;
        break;
    case 1:
        if ((int32_t)((uint32_t)offset + (uint32_t)r->pos) > size)
            r->pos = size;
        else
            r->pos = (int32_t)((uint32_t)r->pos + (uint32_t)offset);
        break;
    case 2:
        if ((int32_t)(0u - (uint32_t)offset) > size)
            return -1;
        if (offset > 0)
            r->pos = size;
        else
            r->pos = (int32_t)((uint32_t)size - (uint32_t)offset);
        break;
    default:
        return -1;
    }
    return 0;
}

/*
 * vfs_tell - members: pos; real files: vfile_tell.  -1 for a bad handle.
 * Original: IST.EXE 569A:08B2 o569A_tell (pascal (h)).
 */
int32_t vfs_tell(int16_t h)
{
    if (vfs_check_handle(h) != 0)
        return -1;
    if (vfs_files[h].in_archive != 0)
        return vfs_files[h].pos;
    return vfile_tell(vfs_files[h].handle);
}

/*
 * vfs_flush - members -1; real files vfile_flush(h).
 * Original: IST.EXE 569A:090A o569A_flush (pascal (h)).  No callers.
 * Quirk: passes the vfs handle h, not the record's vfile handle.
 */
int16_t vfs_flush(int16_t h)
{
    if (vfs_check_handle(h) != 0)
        return -1;
    if (vfs_files[h].in_archive != 0)
        return -1;
    return vfile_flush(h);
}

/*
 * vfs_eof - members: pos == size-1 (off by one, matching what vfs_read
 * leaves after reading past the end); real files: vfile_eof(h).
 * Original: IST.EXE 569A:093F o569A_eof (pascal (h)).  No callers.
 * Quirk: passes the vfs handle h, not the record's vfile handle.
 */
int16_t vfs_eof(int16_t h)
{
    if (vfs_check_handle(h) != 0)
        return -1;
    VfsRecord *r = &vfs_files[h];
    if (r->in_archive != 0)
        return r->pos == (int32_t)((uint32_t)r->size - 1u) ? 1 : 0;
    return vfile_eof(h);
}

void vfs_reset(void)
{
    /* forgotten, not freed: the far heap is reset separately (bc_farheap_reset) */
    vfs_files = NULL;
    for (int i = 0; i <= VFS_MAX_ARCHIVES; i++) {
        vfs_arch_dir[i] = NULL;
        vfs_arch_tag[i] = NULL;
    }
}
