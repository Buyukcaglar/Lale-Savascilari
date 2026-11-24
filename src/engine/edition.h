/*
 * edition.h - which release of the game the port behaves as.
 *
 * Two builds of IST.EXE exist (docs/EDITIONS.md):
 *   EDITION_2005  IST.EXE of 2005-02-08 (385328 bytes), the re-release that
 *                 runs from a folder: no CD needed, DEMO.AMF as menu music.
 *   EDITION_CD    IST.EXE of 1996-11-10 (384560 bytes) on the game CD: wants
 *                 MSCDEX and the disc, plays the videos from the CD drive and
 *                 CD audio tracks 2 and 3 as music.
 * They are the same program but for main(), two routines of DEMOLAR, one of
 * NKONTROL and what follows from it: the line numbers and file names the two
 * builds report in error messages (tools/exediff.py compares them).  The port
 * keeps the data segment of the 2005 build for both; the few strings only the
 * CD build has are C literals where they are used.
 *
 * The decompiled code asks ED_CD at exactly the places the builds differ.
 */
#ifndef LALE_ENGINE_EDITION_H
#define LALE_ENGINE_EDITION_H

#include <stdint.h>

enum { EDITION_2005 = 0, EDITION_CD = 1 };

extern int16_t g_edition;                  /* set by the host before ist_main() */

#define ED_CD (g_edition == EDITION_CD)

/* __LINE__ of a call site: in the 2005 build, in the CD build */
#define ED_LINE(l2005, lcd) ((int16_t)(ED_CD ? (lcd) : (l2005)))

/* __FILE__ of DEMOLAR and NKONTROL: the CD build was compiled from drive T:
   ("T:CODE\DEMOLAR.L", "T:ALTDUZEY\NKONTROL.L"); file2005 is the 2005 build's
   string.  The result stays valid for the next three calls. */
const char *ed_file(const char *file2005);
#define ED_FILE(off) ed_file(DGSTR(off))

#endif
