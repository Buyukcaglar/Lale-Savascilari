/*
 * wp17_stubs.c - TEST ONLY stand-ins for routines the ISOSYS port (WP17)
 * calls but that are not decompiled yet (or live in modules outside the
 * private test build): FRPOS fr_flag_test, 3DSYS s3d_advance_clock and
 * the ISOSYS functions still to do.  Not part of the game build.
 *
 * Each stand-in counts its calls in wp17_calls[id] (read by
 * tests/py/test_iso.py); fr_flag_test answers from wp17_flags.
 */
#include <stdint.h>

#include "game/iso.h"

int32_t wp17_calls[16];
uint8_t wp17_flags[256];

uint8_t fr_flag_test(int16_t n) { wp17_calls[0]++; return wp17_flags[(uint8_t)n]; }
int16_t s3d_advance_clock(int16_t n) { wp17_calls[1]++; return 0; }
void iso_calc_weapon(void) { wp17_calls[2]++; }
void iso_cast_vecize(void) { wp17_calls[3]++; }
void iso_char_register(void) { wp17_calls[4]++; }
void iso_draw_char_info(void) { wp17_calls[5]++; }
void iso_draw_name(void) { wp17_calls[6]++; }
void iso_draw_textbox(const char *s, int16_t a, int16_t b) { wp17_calls[7]++; }
void iso_init_panel_layout(void) { wp17_calls[8]++; }
