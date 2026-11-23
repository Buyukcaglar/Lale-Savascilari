/*
 * bc_farheap.h - Borland C++ 3.x far heap (IST.EXE seg0000 0000:238E-29D6):
 * farmalloc/farfree/farrealloc/farcalloc/farcoreleft and the large-model
 * malloc/free/realloc/calloc on top of them.
 * Implementation: src/rtl/bc_farheap.c.
 *
 * The port reproduces the allocator *exactly*, including where every block
 * lands: the far heap is a byte array (bc_fh_arena) standing for
 * conventional memory from the heap base (runtime segment 2EADh) up to the
 * end of DOS memory (9FFFh), and the allocator is a transcription of the
 * original working on paragraph headers inside it.  Returned pointers are
 * native pointers into that array; bc_far_addr() gives the original's far
 * pointer (always seg:0004).  So farcoreleft() - what the game decides on
 * (MEMMAN's memory checks) - and the bytes a fresh or reused block contains
 * (stale data of freed blocks, DOS MCB residue) are the original's.
 *
 * Block layout (paragraph aligned, user pointer = seg:0004):
 *   +0 u16 size in paragraphs (header included)
 *   +2 u16 previous physical block (used block), 0 = block is free
 *   +4 u16 prev free  +6 u16 next free  +8 u16 saved previous physical
 *      block (free blocks only; circular free list, rover CS:2386)
 * The block at the break is never free: freeing it gives the memory back
 * with brk() (and the program's DOS block shrinks with INT 21h AH=4Ah).
 *
 * Segments are the RUNTIME ones of the DOSBox-X oracle (IST.EXE loaded at
 * 0824h, PSP 0814h), i.e. what the original's far pointers hold.
 */
#ifndef LALE_RTL_BC_FARHEAP_H
#define LALE_RTL_BC_FARHEAP_H

#include <stddef.h>
#include <stdint.h>

#include "engine/dgroup.h"

/* ---- DGROUP (c0 / brk variables) -------------------------------------------- */
#define bc_psp           DG16(0x007B)   /* _psp: PSP segment (0814h) */
#define bc_heapbase_off  DG16(0x0087)   /* _heapbase: far ptr, lowest break (2EAD:0000) */
#define bc_heapbase_seg  DG16(0x0089)
#define bc_brklvl_off    DG16(0x008B)   /* _brklvl: far ptr, current break (normalised) */
#define bc_brklvl_seg    DG16(0x008D)
#define bc_heaptop_off   DG16(0x008F)   /* _heaptop: far ptr, end of DOS memory the */
#define bc_heaptop_seg   DG16(0x0091)   /*   break may reach (9FFF:0000) */
#define bc_heap_kb       DG16(0x9582)   /* size of the program's DOS block in 1 KB
                                           (64-paragraph) units, as last set by
                                           __brk_setblock (0110h at main) */

/* ---- the arena ---------------------------------------------------------------- */
#define BC_FH_ARENA_SEG  0x2EADu        /* runtime segment of bc_fh_arena[0] (_heapbase) */
#define BC_FH_DOS_TOP    0x9FFFu        /* end of the DOS memory chain (MCB 9FFF = UMB link) */
#define BC_FH_ARENA_SIZE ((uint32_t)(0xA000u - BC_FH_ARENA_SEG) * 16u)
/* Memory behind the arena (A000:0000 and up) for writes that run past a
   block near the top of conventional memory - the same huge-pointer blits
   as PC_A000_SLACK in platform/pc.h, with the screen surface in a far-heap
   block: offset up to 65535 + 199*320 + a row.  The original puts them into
   video memory; the port keeps them off the host's other variables (the
   heap's own bc_fh_first/last/rover follow the arena). */
#define BC_FH_ARENA_SLACK 0x20000u
extern uint8_t bc_fh_arena[];

/* Heap state in the RTL code segment (CS:2382-238C). */
extern uint16_t bc_fh_first;   /* CS:2382 __first: first block (0 = no heap yet) */
extern uint16_t bc_fh_last;    /* CS:2384 __last:  block at the break */
extern uint16_t bc_fh_rover;   /* CS:2386 __rover: free-list entry (0 = no free block) */

/* The DOS side: size (paragraphs) of the program's memory block, starting at
   the PSP.  Only INT 21h AH=4Ah (setblock) changes it after main(). */
extern uint16_t bc_fh_dos_block;

/* Set the state c0 leaves at main() (DOSBox-X oracle, build/oracle/main_mem.bin):
   _psp 0814h, _heapbase 2EAD:0000, _brklvl 4BD7:0000, _heaptop 9FFF:0000,
   heap_kb 0110h, __first 2EADh, __last 4BD4h, no free block, three used blocks
   2EADh (1D06h paragraphs: overlay buffer), 4BB3h (21h: stdio buffer), 4BD4h
   (3: argv); DOS block 4400h paragraphs followed by a free MCB at 4C14h
   (53EAh paragraphs, up to 9FFFh).  Arena bytes not listed are 0 (DOSBox-X
   memory is zeroed at start; checked against the dump). */
void bc_farheap_reset(void);

/* The 512-byte stream buffer c0's _setupio allocated before main(): block
   4BB3h of the reset state (stdin's: a terminal, line buffered).  Returned
   once after each bc_farheap_reset() (bc_setupio hands it to stdin instead
   of allocating a second one), NULL afterwards. */
void *bc_farheap_take_stdio_buffer(void);

void    *bc_farmalloc(uint32_t nbytes);                       /* 0000:25D6 */
void     bc_farfree(void *block);                             /* 0000:24C2 (= free) */
void    *bc_farrealloc(void *block, uint32_t nbytes);         /* 0000:273A */
void    *bc_farcalloc(uint32_t nunits, uint32_t unitsz);      /* 0000:28F7 */
uint32_t bc_farcoreleft(void);                                /* 0000:2987 */
void    *bc_malloc(uint16_t size);                            /* 0000:25CC */
void     bc_free(void *block);                                /* 0000:24C2 */
void    *bc_realloc(void *block, uint16_t size);              /* 0000:2733 */
void    *bc_calloc(uint16_t nitems, uint16_t size);           /* 0000:1F64 */

/* The original's far pointer for a pointer into the arena: seg << 16 | off
   (runtime segment); 0 for NULL.  Native pointer for a far pointer. */
uint32_t bc_far_addr(const void *p);
void    *bc_far_ptr(uint32_t segoff);

/* ---- port: native objects with the original's heap footprint ----------------
 * Structures holding far pointers are larger natively (8-byte pointers), so
 * they cannot live in a block of the original's size.  These allocate the
 * block the original allocates (same size, same seg:off, same farcoreleft())
 * and give back a separate host object of native_size bytes, zeroed (the
 * original's block holds stale heap bytes; callers that use these write
 * every field before reading it).  bc_farfree() / bc_free() of the native
 * pointer free both; bc_far_addr() gives the block's far address.  NULL
 * when the far heap has no room (or the host none). */
void *bc_farmalloc_native(uint32_t orig_size, size_t native_size);            /* farmalloc */
void *bc_farcalloc_native(uint32_t nunits, uint32_t orig_unit, size_t native_unit); /* farcalloc */
void *bc_calloc_native(uint16_t nitems, uint16_t orig_unit, size_t native_unit);    /* calloc */
/* The far-heap block standing for a native object (NULL if p is none). */
void *bc_native_block(const void *p);
/* farfree() the block of a native object but keep the object (the caller
   then owns it: host free()).  For code that keeps using memory the
   original has already freed. */
void  bc_native_detach(void *p);

#endif
