/*
 * dds.h - DSMI Dynamic Driver System: name -> function/resource registry and
 * driver loading.  Original: IST.EXE seg0DF2 (Pascal convention, logical
 * parameter order kept).  Implementation: src/dsmi/dds.c.
 *
 * The port cannot execute real-mode .DSD driver images.  DDSLoadDriver
 * therefore reads and checks the driver file exactly like the original, then
 * runs the native driver registered for that file name with
 * dds_register_native_driver() (the MCP software mixer for SDISB16.DSD etc.)
 * through the same entry protocol.  The DS file format: docs/FORMATS.md.
 *
 * Return codes as the original:
 *   0 ok             1 NULL argument          2 DDS not initialised
 *                                               (from DDSInit: out of memory)
 *   3 cannot open    4 not a (loadable) DS file
 *   5 driver init failed (entry(1) low word != 0, or no native driver)
 *   6 unknown handle 7 duplicate name          8 duplicate device id
 *   9 function not found                       10 resource not found
 *   11 driver refused unload (it is unloaded anyway)   12 out of memory
 */
#ifndef LALE_DSMI_DDS_H
#define LALE_DSMI_DDS_H

#include <stdint.h>

#include "engine/dgroup.h"

typedef void (*DdsFn)(void);           /* any function, cast at use */

/* DDSAccessFunction itself, as handed to a driver entry (command 1). */
typedef int16_t (*DdsAccessFn)(const char *name, DdsFn *out);

/* Driver entry point, far pascal (long cmd, void far *arg, long arg2),
   returns DX:AX:
     cmd 1 (init):   arg = DDSAccessFunction, arg2 = 0.  Return
                     (device_id << 16) | error; error 0 = ok.
     cmd 2 (unload): arg = NULL, arg2 = 0.  Return 0 = ok, else refused. */
typedef uint32_t (*DdsDriverEntry)(int32_t cmd, DdsAccessFn access, int32_t arg2);

/* ---- registry nodes (heap; the original's far-pointer layout in comments) - */

typedef struct DdsFuncNode {           /* 12 bytes in the original */
    char               *name;          /* +0 far, own copy (MemAlloc) */
    DdsFn               fn;            /* +4 far */
    struct DdsFuncNode *next;          /* +8 far; circular, sentinel head */
} DdsFuncNode;

typedef struct DdsResNode {            /* 16 bytes */
    char              *name;           /* +0 */
    void              *ptr;            /* +4 */
    uint32_t           size;           /* +8 */
    struct DdsResNode *next;           /* +C */
} DdsResNode;

typedef struct DdsDriverNode {         /* 24 bytes */
    uint16_t              loaded;      /* +0  1 = loaded from a file (block is freed) */
    int16_t               id;          /* +2  device id (entry(1) DX) */
    uint32_t              handle;      /* +4  from dds_next_handle */
    DdsDriverEntry        entry;       /* +8  far */
    void                 *image;       /* +C  paragraph-aligned image (file contents) */
    void                 *block;       /* +10 the allocation holding it */
    struct DdsDriverNode *next;        /* +14 */
} DdsDriverNode;

/* List heads: sentinel nodes (MemCalloc'ed, name NULL / id 0). */
extern DdsFuncNode   *dds_functions;   /* 16E0:E77E far ptr */
extern DdsResNode    *dds_resources;   /* 16E0:E77A far ptr */
extern DdsDriverNode *dds_drivers;     /* 16E0:E782 far ptr */

#define dds_next_handle DG16(0xE776)   /* u16 next driver handle (starts at 1) */
#define dds_init_magic  DG16(0xE778)   /* u16 0xBEEF once DDSInit ran */

/* DDSGetDriverInfo output (3 words) */
typedef struct DdsDriverInfo {
    uint16_t device_id;                /* header +0Ch */
    uint16_t flags;                    /* header +1Ah, bit 0 = loadable */
    uint16_t version;                  /* header +12h (BCD-like, 0100h = 1.00) */
} DdsDriverInfo;

int16_t DDSInit(void);                                                    /* 0DF2:0000 */
int16_t DDSRegisterFunction(const char *name, DdsFn fn);                  /* 0DF2:011B */
int16_t DDSRegisterResource(const char *name, void *ptr, uint32_t size);  /* 0DF2:0214 */
int16_t DDSAccessFunction(const char *name, DdsFn *out);                  /* 0DF2:0316 */
int16_t DDSAccessResource(const char *name, void **ptr, uint32_t *size);  /* 0DF2:03A5  size may be NULL */
int16_t DDSRemoveFunction(const char *name);                              /* 0DF2:044A */
int16_t DDSRemoveResource(const char *name);                              /* 0DF2:04F8 */
int16_t DDSGetDriverInfo(const char *file, DdsDriverInfo *info);          /* 0DF2:05A6 */
int16_t DDSLoadDriver(const char *file, uint32_t *handle);                /* 0DF2:0611 */
int16_t DDSRegisterDriver(DdsDriverEntry entry, uint32_t *handle);        /* 0DF2:08EF */
int16_t DDSUnloadDriver(uint32_t handle);                                 /* 0DF2:09EE */

/* Port: bind a .DSD file name (no path, any case, e.g. "SDISB16.DSD") to the
   native driver that DDSLoadDriver runs instead of the file's real-mode
   code.  Re-registering a name replaces its entry; NULL removes it.  Up to
   16 names. */
void dds_register_native_driver(const char *file, DdsDriverEntry entry);

#endif
