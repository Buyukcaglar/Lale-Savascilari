/*
 * ems.h - DSMI raw EMS calls (LIM 4.0, INT 67h).  Original: IST.EXE seg1134
 * (assembly, Pascal convention, logical parameter order kept).
 * Implementation: src/dsmi/ems.c, on top of pc_int67()/pc_ems_move().
 *
 * The library owns exactly one EMS handle (g_ems_handle); emsMapPage maps
 * logical pages of that handle only.
 */
#ifndef LALE_DSMI_EMS_H
#define LALE_DSMI_EMS_H

#include <stdint.h>

#include "engine/dgroup.h"
#include "platform/pc.h"

/* ---- DGROUP ------------------------------------------------------------- */
#define g_ems_frame_seg  DG16(0x86BC)   /* u16  page-frame segment (INT 67h/41h) */
#define g_ems_ready      DG16(0x86BE)   /* u16  1 after emsInit, 0 after emsFreeHandle */
#define g_ems_handle     DG16(0x86C0)   /* u16  the library's EMS handle, FFFFh = none */
#define g_ems_pages      DG16(0x86C2)   /* u16  pages allocated on g_ems_handle */

/* Runtime DGROUP segment when IST.EXE is loaded at 0824h (DOSBox-X with
   tools/dosbox/ist.conf, and tools/emu86.py).  Some error returns leave
   its high byte in AH ("MOV AX,DGROUP; ... MOV AL,..."): the port returns
   the same value so the tests can compare AX exactly. */
#define EMS_RT_DGROUP 0x1F04u

/* The LIM 4.0 move descriptor (INT 67h AX=5700h).  Conventional sides carry
   a native pointer instead of seg:off (see pc.h). */
typedef PcEmsMove EmsMove;

int16_t  emsInit(void);                                 /* 1134:000E  0 ok, -2 no EMS 4.0 */
int16_t  emsAllocPages(uint16_t pages);                 /* 1134:0049  handle, or AX=xxFFh */
int16_t  emsMapPage(uint16_t logical, uint16_t physical); /* 1134:007A 0 ok, -3 bad page */
uint16_t emsSavePageMap(void);                          /* 1134:00AC  AX after INT 67h/47h */
uint16_t emsRestorePageMap(void);                       /* 1134:00BA  AX after INT 67h/48h */
uint16_t emsGetFreePages(void);                         /* 1134:00C8  FFFFh on error */
void     emsFreeHandle(void);                           /* 1134:00DC */
uint16_t emsGetPageFrame(void);                         /* 1134:00F6 */
uint16_t emsMoveMemory(const EmsMove *m);               /* 1134:00FF  AX after INT 67h/57h */

#endif
