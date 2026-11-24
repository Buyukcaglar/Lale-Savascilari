/*
 * cd5737.h - CD-audio module (IST.EXE overlay ovl5737; original source file
 * name unknown).  An MSCDEX client: plays Red Book tracks of the game CD.
 *
 * Track use in the game (see docs/DISC.md):
 *   track 2 - intro / main menu      (demo_intro_menu 3C2D:09DB, 3C2D:0A48)
 *   track 3 - story slideshow        (demo_story_scene 3C2D:130F)
 *   track 0 - replay the last track  (ui_cd_music 05DC:08F6, menu idle callback)
 *
 * FIDELITY: the IST.EXE this port is built from (2005-02-08) never calls
 * o5737_cd_init(), so the module stays inactive and the game is silent on the
 * CD side.  The port calls it only when the "CD music" option is on
 * (cd5737_port_enable); with the option off the behaviour is the original's.
 */
#ifndef LALE_GAME_CD5737_H
#define LALE_GAME_CD5737_H

#include <stdint.h>

/* --- decompiled functions (original names from analysis/names.tsv) ------ */
const char *o5737_cd_drive_info(void);                 /* 5737:0000 unused */
int16_t  o5737_cd_read_cfg(void);                      /* 5737:0048 unused */
void     o5737_nop(void);                              /* 5737:0071        */
uint8_t  o5737_cd_ioctl(uint16_t fn, uint16_t count, uint8_t *buf); /* 0086 */
void     o5737_cd_reset(void);                         /* 5737:00CF        */
void     o5737_cd_detect(void);                        /* 5737:00EA        */
int16_t  o5737_cd_stop(void);                          /* 5737:0144        */
uint8_t  o5737_cd_init(void);                          /* 5737:0198        */
uint8_t  o5737_cd_qchannel(uint8_t *out10);            /* 5737:01A9        */
uint32_t o5737_msf_to_sectors(uint32_t msf);           /* 5737:01DF        */
uint32_t o5737_cd_track_info(uint8_t track);           /* 5737:024F        */
int16_t  o5737_cd_vtoc_ok(void);                       /* 5737:0291        */
uint8_t  o5737_cd_play(void);                          /* 5737:02D0        */
uint32_t o5737_cd_status(void);                        /* 5737:0361        */
int16_t  o5737_cd_play_track(uint8_t track, int32_t length); /* 5737:0396  */

/* Port-only: CD-music option.  Opens the cue/bin image, makes the virtual
 * MSCDEX visible and runs o5737_cd_init().  Returns 0 on success, -1 if the
 * image cannot be opened (the game then runs exactly as the original). */
char cd5737_drive_letter(void);                /* E9B8, set by o5737_cd_init */
int cd5737_port_enable(const char *cue_path);

/* Port-only, for tests: the module's DGROUP state. */
uint16_t cd5737_active(void);           /* 16E0:8F4E */
uint32_t cd5737_start(void);            /* 16E0:8F50 */
uint32_t cd5737_length(void);           /* 16E0:E8D4 */

/* Test-only: pointers into the module's state (see cd5737.c). */
void *cd5737_test_ptr(int which);

#endif
