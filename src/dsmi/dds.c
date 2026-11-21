/*
 * dds.c - DSMI Dynamic Driver System (IST.EXE seg0DF2, DSMI library).
 *
 * Three circular singly linked lists with a sentinel head each: functions
 * {name, fn}, resources {name, ptr, size} and drivers.  Drivers are "DS"
 * files (an MZ variant, docs/FORMATS.md) loaded into the heap, relocated and
 * started with entry(1, DDSAccessFunction, 0); they find the rest of DSMI by
 * name through DDSAccessFunction.
 *
 * Quirks kept (they decide which duplicates are accepted):
 *   - the duplicate checks of DDSRegisterFunction/-Resource/LoadDriver/
 *     RegisterDriver test every node EXCEPT THE LAST ONE: registering the
 *     same name (or device id) twice in a row succeeds, and lookups then find
 *     the first of the two;
 *   - DDSUnloadDriver unlinks and frees the driver even when it refuses.
 *
 * Port notes:
 *   - list heads (DGROUP far pointers) are native globals; nodes are native
 *     structures beside far-heap blocks of the original's size
 *     (MemAllocNative, src/dsmi/memalloc.h), names and images are far-heap
 *     blocks (MemAlloc);
 *   - files go through the virtual archive layer vfs_open/read/seek/close
 *     (o569A) like the original: the .DSD files are never inside an archive,
 *     so that falls back to a Borland stdio fopen whose 512-byte buffer is
 *     malloc'ed and freed on the far heap (it decides where the driver image
 *     and everything after it land, and so farcoreleft());
 *   - a driver image cannot run: DDSLoadDriver loads it like the original
 *     (same checks, sizes, reads) and then calls the native driver bound to
 *     the file name with dds_register_native_driver().
 */
#include "dsmi/dds.h"
#include "dsmi/memalloc.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dsmi/dsmistr.h"
#include "engine/vfs.h"
#include "rtl/bc_farheap.h"

DdsFuncNode   *dds_functions;                    /* 16E0:E77E */
DdsResNode    *dds_resources;                    /* 16E0:E77A */
DdsDriverNode *dds_drivers;                      /* 16E0:E782 */

/* ======================================================================== */
/* native drivers (port)                                                    */
/* ======================================================================== */

static struct {
    char           name[13];
    DdsDriverEntry entry;
} native_drivers[16];

/* the file part of a DOS path, for the native-driver lookup */
static const char *base_name(const char *path)
{
    const char *b = path;
    for (const char *s = path; *s; s++)
        if (*s == '\\' || *s == '/' || *s == ':')
            b = s + 1;
    return b;
}

static int name_eq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++)
        if (toupper((uint8_t)*a) != toupper((uint8_t)*b))
            return 0;
    return *a == *b;
}

void dds_register_native_driver(const char *file, DdsDriverEntry entry)
{
    const char *b = base_name(file);
    int free_slot = -1;
    for (int i = 0; i < 16; i++) {
        if (native_drivers[i].name[0] && name_eq(native_drivers[i].name, b)) {
            native_drivers[i].entry = entry;
            if (!entry)
                native_drivers[i].name[0] = 0;
            return;
        }
        if (!native_drivers[i].name[0] && free_slot < 0)
            free_slot = i;
    }
    if (entry && free_slot >= 0) {
        snprintf(native_drivers[free_slot].name, sizeof native_drivers[free_slot].name, "%s", b);
        native_drivers[free_slot].entry = entry;
    }
}

static DdsDriverEntry native_driver_for(const char *file)
{
    const char *b = base_name(file);
    for (int i = 0; i < 16; i++)
        if (native_drivers[i].name[0] && name_eq(native_drivers[i].name, b))
            return native_drivers[i].entry;
    return NULL;
}

/* ======================================================================== */
/* registry                                                                 */
/* ======================================================================== */

/*
 * DDSInit - create the three list sentinels and register DDS and Mem*.
 *
 * Allocation order: drivers (24 bytes), functions (12), resources (16), each
 * MemCalloc(1, size) (port: MemCallocNative - far-heap blocks of the
 * original's size, native nodes beside them); a failure
 * returns 2.  Then magic 0xBEEF, next driver
 * handle 1, and ten functions registered by name (DGROUP strings at
 * 16E0:75FE..768C).  Registration errors are ignored; returns 0.  Calling it
 * again starts empty lists (the old nodes leak).
 *
 * Original: IST.EXE 0DF2:0000, far.
 * Verified: tests/py/test_dsmi_infra.py (unicorn differential), 2026-11-20.
 */
int16_t DDSInit(void)
{
    if (MemCallocNative(1, 0x18, sizeof(DdsDriverNode), (void **)&dds_drivers))
        return 2;
    if (MemCallocNative(1, 0x0C, sizeof(DdsFuncNode), (void **)&dds_functions))
        return 2;
    if (MemCallocNative(1, 0x10, sizeof(DdsResNode), (void **)&dds_resources))
        return 2;
    dds_drivers->next = dds_drivers;
    dds_functions->next = dds_functions;
    dds_resources->next = dds_resources;
    dds_init_magic = 0xBEEF;
    dds_next_handle = 1;
    DDSRegisterFunction(DGSTR(0x75FE), (DdsFn)DDSRegisterFunction);
    DDSRegisterFunction(DGSTR(0x7612), (DdsFn)DDSRegisterResource);
    DDSRegisterFunction(DGSTR(0x7626), (DdsFn)DDSAccessFunction);
    DDSRegisterFunction(DGSTR(0x7638), (DdsFn)DDSAccessResource);
    DDSRegisterFunction(DGSTR(0x764A), (DdsFn)DDSRemoveFunction);
    DDSRegisterFunction(DGSTR(0x765C), (DdsFn)DDSRemoveResource);
    DDSRegisterFunction(DGSTR(0x766E), (DdsFn)MemAlloc);
    DDSRegisterFunction(DGSTR(0x7677), (DdsFn)MemCalloc);
    DDSRegisterFunction(DGSTR(0x7681), (DdsFn)MemRealloc);
    DDSRegisterFunction(DGSTR(0x768C), (DdsFn)MemFree);
    return 0;
}

/* Heap copy of a name: MemAlloc(dsmiStrLen + 1) - one spare byte, since
   dsmiStrLen already counts the terminator; sign-extended from 16 bits. */
static int16_t copy_name(const char *name, char **out)
{
    int32_t size = (int16_t)(dsmiStrLen(name) + 1);   /* INC AX / MOVSX EAX,AX */
    if (MemAlloc(size, (void **)out))
        return 12;
    dsmiStrCpy(*out, name);
    return 0;
}

/*
 * DDSRegisterFunction - append {name, fn} to the function list.
 *
 * Returns 2 not initialised, 1 NULL name/fn, 7 duplicate, 12 no memory
 * (a node allocated before a failing name allocation leaks).  The duplicate
 * scan skips the last node (see the top comment).
 *
 * Original: IST.EXE 0DF2:011B, far pascal (char far *name, void far *fn).
 * Verified: tests/py/test_dsmi_infra.py (unicorn differential), 2026-11-20.
 */
int16_t DDSRegisterFunction(const char *name, DdsFn fn)
{
    DdsFuncNode *p = dds_functions;
    DdsFuncNode *node = NULL;
    if (dds_init_magic != 0xBEEF)
        return 2;
    if (name == NULL || fn == NULL)
        return 1;
    while (p->next != dds_functions) {
        if (p->name != NULL && dsmiStrCmp(p->name, name) == 0)
            return 7;
        p = p->next;
    }
    if (MemAllocNative(0x0C, sizeof(DdsFuncNode), (void **)&node))
        return 12;
    if (copy_name(name, &node->name))
        return 12;
    node->fn = fn;
    p->next = node;
    node->next = dds_functions;
    return 0;
}

/*
 * DDSRegisterResource - append {name, ptr, size} to the resource list.
 *
 * Same checks and quirks as DDSRegisterFunction (ptr must be non-NULL).
 *
 * Original: IST.EXE 0DF2:0214, far pascal (name, void far *ptr, long size) -
 * unreferenced by the game itself.
 * Verified: tests/py/test_dsmi_infra.py (unicorn differential), 2026-11-20.
 */
int16_t DDSRegisterResource(const char *name, void *ptr, uint32_t size)
{
    DdsResNode *p = dds_resources;
    DdsResNode *node = NULL;
    if (dds_init_magic != 0xBEEF)
        return 2;
    if (name == NULL || ptr == NULL)
        return 1;
    while (p->next != dds_resources) {
        if (p->name != NULL && dsmiStrCmp(p->name, name) == 0)
            return 7;
        p = p->next;
    }
    if (MemAllocNative(0x10, sizeof(DdsResNode), (void **)&node))
        return 12;
    if (copy_name(name, &node->name))
        return 12;
    node->ptr = ptr;
    node->size = size;
    p->next = node;
    node->next = dds_resources;
    return 0;
}

/*
 * DDSAccessFunction - look a function up by name (first match from the head).
 *
 * Returns 0 and *out, 2 not initialised, 1 NULL argument, 9 not found.
 *
 * Original: IST.EXE 0DF2:0316, far pascal (name, void far * far *out).
 * Verified: tests/py/test_dsmi_infra.py (unicorn differential), 2026-11-20.
 */
int16_t DDSAccessFunction(const char *name, DdsFn *out)
{
    /* the original reads head->next before the magic check; with no DDSInit
       that is a read through a NULL far pointer - harmless there, so the
       port checks first */
    if (dds_init_magic != 0xBEEF)
        return 2;
    DdsFuncNode *p = dds_functions->next;
    if (name == NULL || out == NULL)
        return 1;
    for (; p != dds_functions; p = p->next) {
        if (p->name != NULL && dsmiStrCmp(p->name, name) == 0) {
            *out = p->fn;
            return 0;
        }
    }
    return 9;
}

/*
 * DDSAccessResource - look a resource up by name; size is optional.
 *
 * Returns 0, 2 not initialised, 1 NULL name/ptr, 10 not found.
 *
 * Original: IST.EXE 0DF2:03A5, far pascal (name, void far * far *ptr,
 * long far *size) - unreferenced by the game itself.
 * Verified: tests/py/test_dsmi_infra.py (unicorn differential), 2026-11-20.
 */
int16_t DDSAccessResource(const char *name, void **ptr, uint32_t *size)
{
    if (dds_init_magic != 0xBEEF)
        return 2;
    DdsResNode *p = dds_resources->next;
    if (name == NULL || ptr == NULL)
        return 1;
    for (; p != dds_resources; p = p->next) {
        if (p->name != NULL && dsmiStrCmp(p->name, name) == 0) {
            *ptr = p->ptr;
            if (size != NULL)
                *size = p->size;
            return 0;
        }
    }
    return 10;
}

/*
 * DDSRemoveFunction - unlink the first function with this name and free the
 * name copy and the node.
 *
 * Returns 0, 2 not initialised, 1 NULL name, 9 not found.
 *
 * Original: IST.EXE 0DF2:044A, far pascal (name).
 * Verified: tests/py/test_dsmi_infra.py (unicorn differential), 2026-11-20.
 */
int16_t DDSRemoveFunction(const char *name)
{
    if (dds_init_magic != 0xBEEF)
        return 2;
    DdsFuncNode *p = dds_functions->next;
    DdsFuncNode *prev = dds_functions;
    if (name == NULL)
        return 1;
    while (p != dds_functions) {
        if (p->name != NULL && dsmiStrCmp(p->name, name) == 0) {
            prev->next = p->next;
            MemFree(p->name);
            MemFree(p);
            return 0;
        }
        prev = p;
        p = p->next;
    }
    return 9;
}

/*
 * DDSRemoveResource - as DDSRemoveFunction for the resource list.
 *
 * Returns 0, 2 not initialised, 1 NULL name, 10 not found.
 *
 * Original: IST.EXE 0DF2:04F8, far pascal (name) - unreferenced.
 * Verified: tests/py/test_dsmi_infra.py (unicorn differential), 2026-11-20.
 */
int16_t DDSRemoveResource(const char *name)
{
    if (dds_init_magic != 0xBEEF)
        return 2;
    DdsResNode *p = dds_resources->next;
    DdsResNode *prev = dds_resources;
    if (name == NULL)
        return 1;
    while (p != dds_resources) {
        if (p->name != NULL && dsmiStrCmp(p->name, name) == 0) {
            prev->next = p->next;
            MemFree(p->name);
            MemFree(p);
            return 0;
        }
        prev = p;
        p = p->next;
    }
    return 10;
}

/* ======================================================================== */
/* drivers                                                                  */
/* ======================================================================== */

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return rd16(p) | (uint32_t)rd16(p + 2) << 16; }

/* o569A_read(h, buf, long n): the result is never checked by DDS; a short
   read leaves the rest of the buffer as it was. */
static void file_read(int16_t h, void *buf, uint32_t n)
{
    (void)vfs_read(h, buf, (int32_t)n);
}

/*
 * DDSGetDriverInfo - read a DS driver header: device id, flags, version.
 *
 * Returns 0, 3 cannot open, 4 no "DS" signature.  (The flags are not checked
 * here.)  A file shorter than 28 bytes leaves the rest of the header as the
 * stack held it; the port has zeros there.
 *
 * Original: IST.EXE 0DF2:05A6, far pascal (char far *file, info far *).
 * Verified: tests/py/test_dsmi_infra.py (unicorn differential, the four
 * SDI*.DSD), 2026-11-20.
 */
int16_t DDSGetDriverInfo(const char *file, DdsDriverInfo *info)
{
    uint8_t hdr[0x1C] = {0};
    int16_t f = vfs_open(file, 2);              /* o569A_open(file, 2) */
    if (f == -1)
        return 3;
    file_read(f, hdr, sizeof hdr);
    vfs_close(f);
    if (rd16(hdr) != 0x5344)                    /* "DS" */
        return 4;
    info->device_id = rd16(hdr + 0x0C);
    info->flags = rd16(hdr + 0x1A);
    info->version = rd16(hdr + 0x12);
    return 0;
}

/* Shared tail of DDSLoadDriver / DDSRegisterDriver: start the driver, check
   its device id against the loaded ones, append the node. */
static int16_t start_driver(DdsDriverEntry entry, uint16_t loaded, void *image, void *block,
                            uint32_t *handle)
{
    DdsDriverNode *p = dds_drivers;
    DdsDriverNode *node = NULL;
    uint32_t r = entry(1, DDSAccessFunction, 0);   /* DX:AX */
    if (r & 0xFFFF)
        return 5;                               /* the image stays allocated (leak, as original) */
    int32_t id = (int32_t)(r >> 16);            /* zero-extended */
    while (p->next != dds_drivers) {            /* the last node is not checked */
        if (p->id != 0 && (int32_t)p->id == id) /* MOVSX: ids >= 8000h never match */
            return 8;
        p = p->next;
    }
    if (MemAllocNative(0x18, sizeof(DdsDriverNode), (void **)&node))
        return 12;
    node->id = (int16_t)(r >> 16);
    node->loaded = loaded;
    node->image = image;
    node->block = block;                        /* DDSRegisterDriver: never set (heap garbage) */
    node->entry = entry;
    *handle = dds_next_handle;
    node->handle = dds_next_handle;
    dds_next_handle++;
    p->next = node;
    node->next = dds_drivers;
    return 0;
}

/*
 * DDSLoadDriver - load a DS driver file and start it.
 *
 * Original sequence (kept): open (3), read the 28-byte header, "DS" (4),
 * flags bit 0 (4), allocate image + minalloc + 32 bytes (12), allocate the
 * relocation table (12), seek+read the relocations, seek+read the image,
 * close, apply the relocations (+ load segment), free the table, call
 * entry(1, DDSAccessFunction, 0) at CS:IP + load segment: low word != 0 -> 5;
 * high word = device id, a duplicate -> 8; append a node with the next
 * handle.  Sizes are computed in 16 bits like the original:
 *     image = ((pages - 1) * 512 + last_page_bytes - header_paras * 16 + 32) & 0xFFFF
 * (32 bytes more than the image: the read is short by that much).
 *
 * Port: the image is loaded paragraph-aligned inside the block as in the
 * original (available to the native driver as node->image), but not
 * relocated - the segment fixups only matter for running the real-mode
 * code - and the native entry bound to the file name runs instead (none
 * bound: 5).  A driver without relocations fails with 12 in both, because
 * farcalloc(0) returns NULL.
 *
 * Original: IST.EXE 0DF2:0611, far pascal (char far *file, long far *handle).
 * Verified: tests/py/test_dsmi_infra.py (unicorn differential: header checks,
 * returns, driver list; the native entry stands in for the image), 2026-11-20.
 */
int16_t DDSLoadDriver(const char *file, uint32_t *handle)
{
    uint8_t hdr[0x1C] = {0};
    void *block = NULL, *relocs = NULL;
    int16_t f = vfs_open(file, 2);              /* o569A_open(file, 2) */
    if (f == -1)
        return 3;
    file_read(f, hdr, sizeof hdr);
    if (rd16(hdr) != 0x5344) {
        vfs_close(f);
        return 4;
    }
    if ((rd16(hdr + 0x1A) & 1) != 1) {          /* not loadable */
        vfs_close(f);
        return 4;
    }
    uint16_t last = rd16(hdr + 2), pages = rd16(hdr + 4), nrel = rd16(hdr + 6);
    uint16_t hdr_paras = rd16(hdr + 8), min_paras = rd16(hdr + 0x0A);
    uint16_t rel_off = rd16(hdr + 0x18);
    uint32_t cs_ip = rd32(hdr + 0x14);
    uint32_t image_size = (uint16_t)((uint16_t)((pages - 1) << 9) + last
                                     - (uint16_t)(hdr_paras << 4) + 0x20);
    uint32_t alloc = image_size + (uint16_t)(min_paras << 4) + 0x20;
    if (MemAlloc((int32_t)alloc, &block)) {
        vfs_close(f);
        return 12;
    }
    /* load segment = normalised block address rounded up to a paragraph */
    uint8_t *image = (uint8_t *)block + ((16 - (bc_far_addr(block) & 15)) & 15);
    if (MemCalloc(nrel, 4, &relocs)) {
        MemFree(block);
        vfs_close(f);
        return 12;
    }
    vfs_seek(f, rel_off, 0);                    /* o569A_seek(h, off, 0) */
    file_read(f, relocs, (uint16_t)(nrel << 2));
    vfs_seek(f, (uint16_t)(hdr_paras << 4), 0);
    file_read(f, image, image_size);
    vfs_close(f);
    /* 0DF2:0776 - for each (off, seg): word at image + (uint16)(seg*16+off)
       += load segment.  Not needed without the real-mode code. */
    MemFree(relocs);
    (void)cs_ip;                                /* entry = (CS + load seg):IP */
    DdsDriverEntry entry = native_driver_for(file);
    if (entry == NULL)
        return 5;
    return start_driver(entry, 1, image, block, handle);
}

/*
 * DDSRegisterDriver - start a driver linked into the program.
 *
 * Same protocol and checks as DDSLoadDriver from the entry call on; the node
 * has loaded = 0 and image = NULL.
 *
 * Original: IST.EXE 0DF2:08EF, far pascal (void far *entry, long far *handle)
 * - unreferenced by the game.
 * Verified: tests/py/test_dsmi_infra.py (unicorn differential), 2026-11-20.
 */
int16_t DDSRegisterDriver(DdsDriverEntry entry, uint32_t *handle)
{
    return start_driver(entry, 0, NULL, NULL, handle);
}

/*
 * DDSUnloadDriver - call entry(2, 0, 0), free the image (file drivers),
 * unlink and free the node.
 *
 * Returns 0, 11 if the driver returned non-zero (DX:AX) - it is removed
 * anyway - or 6 for an unknown handle.  No DDSInit check.
 *
 * Original: IST.EXE 0DF2:09EE, far pascal (long handle).
 * Verified: tests/py/test_dsmi_infra.py (unicorn differential), 2026-11-20.
 */
int16_t DDSUnloadDriver(uint32_t handle)
{
    if (dds_drivers == NULL)                    /* port: the original has no init check */
        return 6;
    DdsDriverNode *p = dds_drivers->next;
    DdsDriverNode *prev = NULL;
    while (p != dds_drivers) {
        if (p->handle == handle) {
            uint32_t r = p->entry(2, NULL, 0);
            if (p->loaded != 0)
                MemFree(p->block);
            if (prev != NULL)
                prev->next = p->next;
            else
                dds_drivers->next = p->next;
            MemFree(p);
            return r != 0 ? 11 : 0;
        }
        prev = p;
        p = p->next;
    }
    return 6;
}
