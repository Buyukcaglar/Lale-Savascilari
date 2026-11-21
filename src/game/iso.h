/*
 * iso.h - ISOSYS.L: the isometric turn-based battle ("savaş").
 * Original: IST.EXE ovl4595 (Borland C, far cdecl), 77 functions, entry
 * thunks 1694:0020..019C.  Implementation: src/game/iso_*.c, split by area:
 *
 *   iso_util.c    4595:0000-0D1A  callbacks, small helpers, graphics loading
 *   iso_map.c     4595:0D1B-16EA  mini map, line of sight, view scrolling
 *   iso_turn.c    4595:16EB-2720  messages, face animation, turn order,
 *                                 status slots, sounds, voices
 *   iso_draw.c    4595:2721-3F5A  screen redraw, moving, drawing units
 *   iso_combat.c  4595:3F5B-568B  attack, target cursor, player commands
 *   iso_field.c   4595:568C-6344  battlefield from the 3D map
 *   iso_char.c    4595:6345-8268  party <-> battle, character stats/panel
 *   iso_spell.c   4595:8269-9667  vecize (spells)
 *   iso_ai.c      4595:9668-B04C  monster AI, experience
 *   iso_battle.c  4595:B04D-D7CB  item icon, portrait, iso_battle (entry)
 *
 * How a battle is started and ended (3DSYS 303F:8524, FRPOS 3FD1:0DF6):
 *   the caller fills the encounter (DGROUP 5BE0 + 6*i: monster type / count
 *   of group i = 0..2, BB39 encounter level) and calls iso_battle(); it
 *   loads palbat.pal and the battle graphics, builds the battlefield from
 *   the 3D map around the party, runs the turn loop and returns with
 *   g_iso_result (632E) = 1 won, 2 lost/fled; the party records have been
 *   updated by iso_battle_to_party.
 *
 * DGROUP layout (offsets in 16E0):
 *   units       A08A + 28h*c, c = 1..28 (1-24 monsters, 8 per group;
 *               25-28 the party)                              IsoUnit
 *   templates   9F18 + 2Eh*g, g = 1..4 party, 5..7 monster groups  IsoTmpl
 *   occupancy   C2CE: 28x28 words, [x][y] at C2CE + 38h*x + 2*y (unit no.)
 *   terrain     C8EE: same layout
 *   mini map    CF0E: 27x27 words, [x][y] at CF0E + 36h*x + 2*y (colours)
 */
#ifndef LALE_GAME_ISO_H
#define LALE_GAME_ISO_H

#include <stdint.h>

#include "engine/dgroup.h"

/* ---- units and templates -------------------------------------------------- */
#pragma pack(push, 1)
typedef struct IsoUnit {      /* A08A + 28h * c */
    int16_t hp;               /* +00 hit points (<= 0 dead) */
    int16_t spell;            /* +02 vecize points / value */
    int16_t facing;           /* +04 1..4 */
    int16_t tmpl;             /* +06 template index g */
    int16_t x;                /* +08 cell */
    int16_t y;                /* +0A */
    int16_t target;           /* +0C last target */
    int16_t side;             /* +0E 1 party and allies, 2 enemies */
    int16_t f10;              /* +10 charmer: when that unit dies, side flips (1<->2) */
    int16_t to_hit;           /* +12 */
    int16_t dodge;            /* +14 */
    int16_t status[5];        /* +16..+1E duration*100 + id */
    int16_t on_hit;           /* +20 vecize cast on a hit */
    int16_t stun;             /* +22 turns to skip */
    int16_t hit;              /* +24 was hit */
    int16_t f26;              /* +26 */
} IsoUnit;
_Static_assert(sizeof(IsoUnit) == 0x28, "IsoUnit is 28h bytes");

typedef struct IsoTmpl {      /* 9F18 + 2Eh * g: monster record words */
    int16_t w[23];
} IsoTmpl;
_Static_assert(sizeof(IsoTmpl) == 0x2E, "IsoTmpl is 2Eh bytes");
#pragma pack(pop)

#define ISO_UNITS            28
#define ISO_UNIT_BASE        0xA08A
#define ISO_TMPL_BASE        0x9F18
#define iso_unit(c)          ((IsoUnit *)DGP((uint16_t)(ISO_UNIT_BASE + 0x28 * (c))))
#define iso_tmpl(g)          ((IsoTmpl *)DGP((uint16_t)(ISO_TMPL_BASE + 0x2E * (g))))
#define iso_utmpl(c)         iso_tmpl(iso_unit(c)->tmpl)

/* grids: [x][y] words */
#define g_iso_occ(x, y)      DGS16((uint16_t)(0xC2CE + 0x38 * (x) + 2 * (y)))
#define g_iso_terrain(x, y)  DGS16((uint16_t)(0xC8EE + 0x38 * (x) + 2 * (y)))
#define g_iso_minimap(x, y)  DGS16((uint16_t)(0xCF0E + 0x36 * (x) + 2 * (y)))

/* a word of a table: base + stride * i (the listing's [BX + base]) */
#define ISO_W(base, stride, i) DGS16((uint16_t)((base) + (stride) * (i)))
#define ISO_B(base, stride, i) DG8((uint16_t)((base) + (stride) * (i)))

/* ---- DGROUP globals --------------------------------------------------------- */
#define g_iso_wall_color(k)  DG8((uint16_t)(0x2624 + (k)))   /* u8  party slot k=2..5 */
#define g_iso_wall_kind(k)   DGS16((uint16_t)(0x2626 + 2 * (k)))
#define g_iso_terrain_col(t) DG8((uint16_t)(0x261C + (t)))   /* u8  mini-map colour of terrain t */
#define g_iso_group_type(i)  DGS16((uint16_t)(0x5BC8 + 6 * (i)))  /* i = 4..6 (5BE0..) */
#define g_iso_alive(side)    DGS16((uint16_t)(0x2632 + 2 * (side)))  /* i16 some unit of side 1/2 alive */
#define g_iso_voice          DGS16(0x631A)  /* i16 voice (vecize / answer number) to speak, 0 none */
#define g_iso_speaker        DGS16(0x6320)  /* i16 voice id of the speaker: unit+40 (party) or
                                               monster type; FFh after speaking */
#define g_iso_rolled         DGS16(0x6322)  /* i16 initiative rolled once */
#define g_iso_result         DGS16(0x632E)  /* i16 1 won, 2 lost */
#define g_iso_turn           DGS16(0x6330)  /* i16 initiative index 1..28 */
#define G_ISO_NAMEBUF        0x6332         /* char[14] "" template copied by F_SCOPY */
#define g_iso_party_pic(k)   DGS16((uint16_t)(0x9F0C + 2 * (k)))  /* k = 2..5 */
#define g_iso_view_x         DGS16(0xD580)  /* i16 view origin (cells) */
#define g_iso_view_y         DGS16(0xD57E)
#define g_iso_cursor_x       DGS16(0xD57A)  /* i16 target cursor cell */
#define g_iso_cursor_y       DGS16(0xD578)
#define g_iso_cursor_on      DGS16(0xD57C)  /* i16 cursor shown on the mini map */
#define g_iso_los_clear      DGS16(0xD560)  /* i16 iso_trace_line: 1 clear */
#define g_iso_los_dist       DGS16(0xD564)  /* i16 iso_trace_line: steps */
#define g_iso_actor          DGS16(0xD5DE)  /* i16 unit whose turn it is */
#define g_iso_attacker       DGS16(0xD56E)
#define g_iso_defender       DGS16(0xD56C)
#define g_iso_ap             DGS16(0xD54C)  /* i16 action points left */
#define g_iso_round          DGS16(0xD54A)
#define g_iso_d54e           DGS16(0xD54E)  /* i16 cleared each round */
#define g_iso_d550           DGS16(0xD550)  /* i16 cleared each round */
#define g_iso_d556           DGS16(0xD556)
#define g_iso_d55a           DGS16(0xD55A)  /* i16 cleared at each turn */
#define g_iso_sfx            DGS16(0xD566)  /* i16 battle sound to play (iso_play_sfx) */
#define g_iso_anim_kind      DGS16(0xD574)  /* i16 unit animation: 1 turn, 2 death, 4 hit */
#define g_iso_anim_unit      DGS16(0xD576)  /* i16 unit of that animation */
#define g_iso_d59c           DGS16(0xD59C)
#define g_iso_damage         DGS16(0xD570)  /* i16 damage number shown on the hit unit */
#define g_iso_d572           DGS16(0xD572)
#define g_iso_def_dodge      DGS16(0xD548)  /* i16 iso_attack: defender's dodge */
#define g_iso_flee           DGS16(0xD568)  /* i16 a hard miss happened (set once) */
#define g_iso_d56a           DGS16(0xD56A)  /* i16 drain (attacker status 19) */
#define g_iso_stunned        DGS16(0xD59A)  /* i16 the stun weapon worked */
#define g_iso_dmg_bonus      DGS16(0xD59E)  /* i16 slayer / race damage bonus */
#define g_iso_hit_bonus      DGS16(0xD5A0)  /* i16 slayer / race to-hit bonus */
#define g_iso_wpn_base       DGS16(0xD5A8)  /* i16 iso_calc_weapon: damage base */
#define g_iso_wpn_dmg        DGS16(0xD5AA)  /* i16 iso_calc_weapon: damage dice (x100) */
#define g_iso_strike         DGS16(0xD5B6)  /* i16 iso_attack: attack number 1..2 */
#define g_iso_d5c2           DGS16(0xD5C2)
#define g_iso_chance         DGS16(0xD5DA)  /* i16 iso_attack: to-hit chance */
#define g_iso_d582           DGS16(0xD582)  /* i16 set when a unit effect was drawn */
#define g_iso_free_hit       DGS16(0xD5A4)  /* i16 iso_attack is a free (opportunity) attack */
#define g_iso_d5a6           DGS16(0xD5A6)  /* i16 someone died */
#define g_iso_d5b2           DGS16(0xD5B2)  /* i16 move_step: enemy is not busy */
#define g_iso_d5b4           DGS16(0xD5B4)
#define g_iso_d5b8           DGS16(0xD5B8)  /* i16 move_step: target cell is free */
#define g_iso_near           DGS16(0xD536)  /* i16 move_step: unit next to the actor */
#define g_iso_facing         DGS16(0xD55E)  /* i16 facing of the pending step */
#define g_iso_frame          DGS16(0xD5C4)  /* i16 sprite frame scratch of iso_redraw */
#define g_iso_num_dy         DGS16(0xD5C6)  /* i16 y offset of the cell of the damage number */
/* D5C8/D5CA: the cell being drawn (iso_redraw) and the step (iso_move_step) */
#define g_iso_cell_y         DGS16(0xD5C8)
#define g_iso_cell_x         DGS16(0xD5CA)
#define g_iso_step_y         g_iso_cell_y
#define g_iso_step_x         g_iso_cell_x
#define g_iso_cell_unit      DGS16(0xD5CC)  /* i16 unit on the cell being drawn */
#define g_iso_talker         DGS16(0xD5CE)  /* i16 unit saying the queued text / number */
#define g_iso_num_y          DGS16(0xD5D0)  /* i16 screen position of the damage number */
#define g_iso_num_x          DGS16(0xD5D2)
#define g_iso_quiet          DGS16(0xD5A2)  /* i16 iso_show_message says nothing */
#define g_iso_gum_flavour    DGS16(0xD5B0)  /* i16 0..6, names at 633F + 12*i */
#define g_iso_gum            DGS16(0xD5BA)  /* i16 a chewing gum ("ciklet") is used */
#define g_iso_d5bc           DGS16(0xD5BC)
#define g_iso_msg_time       DGS16(0xD5D4)  /* i16 clock (low word) of the last text box */
#define g_iso_target         DGS16(0xD5E0)  /* i16 vecize target */
#define g_iso_spell          DGS16(0xD5E2)  /* i16 vecize number */
/* the battle's list window (record 3, 974C) is the current UI record */
#define iso_in_battle_window() (DG8(0x9672) == DG8(0x9792))
#define g_iso_order(c)       DGS16((uint16_t)(0xD4C0 + 2 * (c)))
#define g_iso_init(c)        DGS16((uint16_t)(0xD4FA + 2 * (c)))
/* panel rectangles set by iso_wnd_cb_set_layout */
#define g_iso_panel_rect(i)  DGS16((uint16_t)(0xE725 + 2 * (i)))   /* i = 0..7 */
/* surface slots (memman.h g_surf_*): 1 anakes, 2 yericon, 6 the work
   screen (320x200), 7 tabletur, 11..17 unit sprites, 18 walls (300x60),
   19 (E337) the .KOR frame table of the sprites (6*12 bytes per frame
   set, 72 bytes per sprite slot) */
#define ISO_SURF_WALLS       0x12
#define G_ISO_KOR_REF        0xE337         /* far MmHandle * (slot 19's record) */
#define G_ISO_SCREEN_REF     0xE2CF         /* far MmHandle * of slot 6 */
/* vecize (spells), WP17b: iso_combat.c / iso_spell.c */
#define g_iso_spell_arg      DGS16(0xD562)  /* i16 strength of the chosen vecize (iso_cmd_vecize) */
#define g_iso_caster_power   DGS16(0xD5BE)  /* i16 caster's template word 9 / 50 */
#define g_iso_d5c0           DGS16(0xD5C0)  /* i16 iso_vecize_reaction: the target answered */
#define g_iso_tgt_power      DGS16(0xD55C)  /* i16 vecize target's template word 10 / 50 */
/* battle loop, WP17c: iso_battle.c */
#define g_iso_new_turn       DGS16(0x6324)  /* i16 set by iso_next_turn: a new actor is up */
#define g_iso_key            DG16(0xD5D6)   /* u16 scan code of the key read by iso_battle */
#define g_iso_party_bx       DGS16(0xD554)  /* i16 party's 9x9 block on the field (build_battlefield) */
#define g_iso_party_by       DGS16(0xD552)
#define g_iso_monsters       DGS16(0xD558)  /* i16 monsters in the encounter (sum of the counts) */
#define g_iso_panel_used     DGS16(0xD5AE)  /* i16 the panel handled the input (no extra redraw) */
#define g_iso_resist         DGS16(0xD55C)  /* i16 target's cheek / 50 (+1 status 14) for a vecize */
#define g_iso_encounter(i, w) DGS16((uint16_t)(0x5BDA + 6 * (i) + 2 * (w)))  /* group i 1..3:
                                               w 0 monster type (+100: allied), w 1 count */

/* ---- functions (original address order) ------------------------------------ */
void    iso_register_far_fns(void);         /* port: ui_register_far_fn for 1694:xxxx */

void    iso_free_battle_gfx(void);                                         /* 4595:0000 */
void    iso_wnd_cb_picture6(void);                                         /* 4595:008A */
void    iso_wnd_cb_set_layout(int16_t col, int16_t row, int16_t h);        /* 4595:0097 */
void    iso_wnd_cb_button(int16_t col, int16_t row, int16_t h, int16_t hl); /* 4595:0136 */
void    iso_draw_text_shadow(const char *s, int16_t col, int16_t row, int16_t color,
                             int16_t h);                                   /* 4595:019F */
void    iso_clamp_add(int16_t *p, int16_t d, int16_t lo, int16_t hi);      /* 4595:0202 */
int16_t iso_sign(int16_t x);                                               /* 4595:0238 */
int16_t iso_bit_test(uint16_t n, uint16_t mask);                           /* 4595:0255 */
void    iso_blit_icon(uint16_t slot, int16_t x, int16_t y);                /* 4595:028E */
void    iso_draw_bar(int16_t x, int16_t y, int16_t n);                     /* 4595:02D1 */
void    iso_draw_sprite(int16_t *x, int16_t *y, int16_t slot, int16_t frame); /* 4595:0387 */
void    iso_load_battle_gfx(void);                                         /* 4595:0570 */
void    iso_draw_text(int16_t font, int16_t x, int16_t y, const char *s,
                      uint8_t color);                                      /* 4595:0B80 */
void    iso_show_picture(int16_t slot);                                    /* 4595:0BE1 */
void    iso_set_cell(int16_t x, int16_t y, int16_t v);                     /* 4595:0C27 */
void    iso_center_view(int16_t c);                                        /* 4595:0C40 */
void    iso_draw_map(void);                                                /* 4595:0D1B */
void    iso_trace_line(int16_t c, int16_t x, int16_t y);                   /* 4595:0F86 */
void    iso_scroll_view_dir(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                            int16_t icon);                                 /* 4595:1139 */
void    iso_scroll_view_to(int16_t x0, int16_t y0, int16_t x1, int16_t y1); /* 4595:14EA */
void    iso_show_message(char *s, int16_t c);                              /* 4595:16EB */
void    iso_face_anim_tick(uint32_t rec);                                  /* 4595:1935 */
void    iso_apply_dot_damage(int16_t d, int16_t c);                        /* 4595:1ACE */
int16_t iso_next_turn(void);                                               /* 4595:1B6D */
void    iso_roll_initiative(void);                                         /* 4595:1E75 */
void    iso_add_status(int16_t c, int16_t id, int16_t dur);                /* 4595:1FFC */
int16_t iso_has_status(int16_t c, int16_t id);                             /* 4595:20E0 */
void    iso_play_sfx(int16_t n);                                           /* 4595:214E */
void    iso_start_face_anim(int16_t type, int16_t x, int16_t y, int16_t w, int16_t h,
                            int16_t sx, int16_t sy);                       /* 4595:22B3 */
void    iso_speak_vecize(int16_t n);                                       /* 4595:235B */
uint16_t iso_sfx_rate(int16_t who, uint32_t base);                         /* 4595:255B */
void    iso_voice_filename(int16_t who, char *buf, int16_t n);             /* 4595:2623 */
void    iso_redraw(void);                                                  /* 4595:2721 */
int16_t iso_step_sfx(int16_t tile);                                        /* 4595:2C7B */
void    iso_move_step(void);                                               /* 4595:2D39 */
void    iso_draw_combatants(int16_t mode);                                 /* 4595:3955 */
void    iso_attack(void);                                                  /* 4595:3F5B */
void    iso_pick_target(void);                                             /* 4595:482D */
void    iso_cmd_attack(void);                                              /* 4595:4DB8 */
void    iso_cmd_vecize(void);                                              /* 4595:50BD */
void    iso_vecize_reaction(void);                                         /* 4595:552C */
void    iso_build_battlefield(void);                                       /* 4595:568C */
void    iso_party_to_battle(void);                                         /* 4595:6345 */
int16_t iso_calc_char_stats(void);                                         /* 4595:64F5 */
void    iso_calc_weapon(void);                                             /* 4595:6828 */
int16_t iso_calc_stat_a(void);                                             /* 4595:6AC2 */
int16_t iso_calc_stat_b(void);                                             /* 4595:6B79 */
int16_t iso_calc_stat_c(void);                                             /* 4595:6CD6 */
int16_t iso_calc_ap_by_load(void);                                         /* 4595:6E4D */
void    iso_draw_char_frame(int16_t a, int16_t b);                         /* 4595:6E95 */
void    iso_item_effect_a(void);                                           /* 4595:6F5F */
void    iso_item_effect_b(void);                                           /* 4595:704E */
void    iso_item_effect_maxhp3(void);                                      /* 4595:713D */
void    iso_item_effect_c(void);                                           /* 4595:7172 */
void    iso_item_effect_d(void);                                           /* 4595:71A7 */
void    iso_item_effect_e(void);                                           /* 4595:71C4 */
void    iso_item_effect_f(void);                                           /* 4595:71F9 */
void    iso_item_effect_g(void);                                           /* 4595:7216 */
void    iso_item_effect_h(void);                                           /* 4595:7233 */
void    iso_init_panel_layout(void);                                       /* 4595:7250 */
void    iso_draw_name(void);                                               /* 4595:7470 */
void    iso_draw_char_info(void);                                          /* 4595:7574 */
void    iso_use_item(void);                                                /* 4595:7A03 */
void    iso_draw_textbox(const char *s, int16_t a, int16_t b);             /* 4595:7F29 */
void    iso_format_stat_line(void);                                        /* 4595:7F80 */
void    iso_refresh_desc(void);                                            /* 4595:80D6 */
void    iso_battle_to_party(void);                                         /* 4595:8111 */
int16_t iso_eval_param(int16_t v);                                         /* 4595:8269 */
void    iso_cast_vecize(void);                                             /* 4595:84E9 */
void    iso_vecize_effect(void);                                           /* 4595:8E4F */
void    iso_after_hit(void);                                               /* 4595:9490 */
void    iso_ai_turn(void);                                                 /* 4595:9668 */
void    iso_give_xp(int16_t c, int16_t n);                                 /* 4595:AA60 */
void    iso_xp_progress(int16_t c);                                        /* 4595:AAAE */
void    iso_draw_item_icon(int16_t a, int16_t b, int16_t c);               /* 4595:B04D */
void    iso_draw_portrait(int16_t a, int16_t b, int16_t c);                /* 4595:B22F */
void    iso_battle(void);                                                  /* 4595:B40A */

#endif
