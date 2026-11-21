/*
 * istanbul.h - ISTANBUL.C: main() of IST.EXE (seg0546, 0546:0001-0963).
 *
 * ist_main(argc, argv) is the game: start-up (file system, overlay EMS,
 * DSMI timer, sound, MEMMAN/EMS, keyboard/timer vectors, the 8 CKD
 * archives, fonts, anti-alias table, FRPOS flags, 3D tables, the UI
 * pictures, mouse, graphics) and then the main menu loop for ever.  It
 * never returns: the way out is o559A_fatal (exit code -1 after the
 * credits, "sorunsuz bitti!"), which calls exit() in the port.
 *
 * Command line: only argc counts.  argc-1 (as a byte) is demo_intro_menu's
 * first argument: 0 = full intro (no arguments), 1 = skip the mode-12h
 * pictures, 2 = videos then menu, 3..6 = choose menu item argc-3 at once
 * (3 = new game, 4 = continue (explore loop), 5 = quick start "grphazir",
 * 6 = credits/exit).  argv is not read.
 *
 * What the host must have done before calling ist_main (sdl3_main.c does
 * all of them):
 *   1. pc_reset()              - emulated PC: EMS (pc_ems_configure before,
 *                                 if not the default), VGA, keyboard, mouse,
 *                                 PIT, clock;
 *   2. pc_set_game_dir(dir)    - directory with the game files (CKD
 *                                 archives, SES.CFG, the .GFL videos, CD
 *                                 image settings);
 *   3. dg_reset()              - DGROUP from the image (c0 start-up);
 *      bc_farheap_reset()      - the Borland far heap as it is at main();
 *      bc_setupio()            - the RTL's #pragma startup (stdin/stdout);
 *      ist_ovr_prepare()       - the overlay manager's start-up (DGROUP 8F8E);
 *   4. optional: cd5737_port_enable(...) for CD music, audio backend open;
 *   5. the host pump/present hooks (pc_set_host_pump etc.) so pc_poll()
 *      drives the window.
 * ist_main itself registers the overlay far-code entries of all game
 * modules with INTRFACE (port addition, no effect on DGROUP).
 */
#ifndef LALE_GAME_ISTANBUL_H
#define LALE_GAME_ISTANBUL_H

#include <stdint.h>

int ist_main(int argc, char **argv);                       /* 0546:0001 */

/* port: model of Borland's _OvrInitEms(0, 0, 18) - allocates the overlay
   cache (min(18, overlays' size) = 14 EMS pages) through INT 67h exactly as
   seg122B does; 0 = ok. */
int16_t ist_ovr_init_ems(uint16_t handle, uint16_t first, uint16_t pages);
/* port: the overlay manager's c0 start-up effect on DGROUP (8F8E = 35D10h) */
void    ist_ovr_prepare(void);

/* port: time() for srand; tests set ist_time_override >= 0 */
extern int32_t ist_time_override;

#endif
