/*
 * chargen.h - CHARGEN.L: character generation (the "karakter" screen of a
 * new game) and GRP.DAT, the party file.  Also the owner of the party /
 * character records in DGROUP, which 3DSYS, FRPOS, ISOSYS and the save
 * game share (docs/GAME_WAVE.md rule 3).
 * Original: IST.EXE ovl39E5 (CODE\CHARGEN.L, from the assert strings
 * "T:\CODE\CHARGEN.L"), Borland C, far cdecl.  Implementation:
 * src/game/chargen.c.  GRP.DAT layout: docs/FORMATS.md "GRP.DAT".
 *
 * Called from outside: cg_main (new game, ISTANBUL main 0546:059C) and
 * cg_load_party (main 0546:0634 "grp.dat", 0546:07C1 "grphazir.dat").
 *
 * ---- Party data in DGROUP (index 0..4, the game uses members 1..4) ------
 *
 *   16E0:98FC  5 x 34   (unknown per-member table, saved by 3DSYS; not ours)
 *   16E0:99A6  5 x 92   PartyStats   g_party_stats[i]
 *   16E0:9B72  5 x 36   CgMods       g_cg_mods[i]    race modifiers (chargen work)
 *   16E0:9C26  5 x 148  PartyName    g_party_names[i]
 *   16E0:9F0A  char[]   g_cg_gender_text  "erkek"/"dişi" scratch string
 *
 * The 14 rolled attributes and their labels on the karakter screen (row =
 * text row of cg_draw_stats; the labels are part of KARAKTER.CMP):
 *
 *   row field label (Turkish)      gloss
 *    1  +24  boy                   height
 *    2  +26  kilo                  weight
 *    3  +0E  zeka                  intelligence
 *    4  +10  vuruş gücü            hitting power
 *    5  +0A  kelime dağarcığı      vocabulary (copied to +38 by cg_save_party)
 *    6  +0C  güç                   strength   (copied to +3A = current
 *                                              health, intrface g_party_hp)
 *    7  +14  çeviklik              agility
 *    8  +12  dayanıklılık          endurance
 *    9  +1A  batıl inanç           superstition
 *   10  +16  yüzsüzlük             cheek / shamelessness
 *   11  +18  istanbulluluk         "istanbul-ness" (street smarts)
 *   12  +1C  cesaret               courage
 *   13  +1E  etkileyicilik         charisma
 *   14  +20  inanç                 faith
 */
#ifndef LALE_GAME_CHARGEN_H
#define LALE_GAME_CHARGEN_H

#include <stddef.h>
#include <stdint.h>

#include "engine/dgroup.h"

#define PARTY_SLOTS      5          /* records 0..4; members are 1..4 */
#define PARTY_MEMBERS    4

#pragma pack(push, 1)
/* One party member's stat record, 92 (5Ch) bytes at 16E0:99A6 + 5Ch*i.
   Saved verbatim in GRP.DAT (members 1..4) and in SAKLAnnn.SAV (0..4). */
typedef struct PartyStats {
    int16_t u00;            /* +00 (heavily used by 3DSYS/ISOSYS; not set here) */
    int16_t race;           /* +02 race/class 1..8, name = g_race_name(race) */
    int16_t gender;         /* +04 1 = erkek (male), 2 = dişi (female) */
    int16_t u06;            /* +06 (only clamped to >= 50 by cg_roll_stats) */
    int16_t u08;            /* +08 (only clamped to >= 50 by cg_roll_stats) */
    int16_t vocabulary;     /* +0A kelime dağarcığı */
    int16_t strength;       /* +0C güç */
    int16_t intelligence;   /* +0E zeka */
    int16_t hit_power;      /* +10 vuruş gücü */
    int16_t endurance;      /* +12 dayanıklılık */
    int16_t agility;        /* +14 çeviklik */
    int16_t cheek;          /* +16 yüzsüzlük */
    int16_t istanbulness;   /* +18 istanbulluluk */
    int16_t superstition;   /* +1A batıl inanç */
    int16_t courage;        /* +1C cesaret */
    int16_t charisma;       /* +1E etkileyicilik */
    int16_t faith;          /* +20 inanç */
    int16_t u22;            /* +22 random(151)+50 at creation (gameA.md: hunger counter) */
    int16_t height;         /* +24 boy */
    int16_t weight;         /* +26 kilo */
    int16_t u28;            /* +28 random(6)+15 at creation */
    int16_t u2A;            /* +2A */
    int16_t special[6];     /* +2C [0] = race special (g_race_special pick);
                               [1..5] = intrface g_party_word(p,1..5) condition ids */
    int16_t vocabulary_cur; /* +38 = vocabulary when GRP.DAT is written */
    int16_t health;         /* +3A current health (= strength at creation);
                               >0 ok, -5..0 unconscious, <= -5 dead */
    int16_t u3C[3];         /* +3C */
    int16_t portrait;       /* +42 portrait 1..16, picture g_portrait_name(portrait) */
    int16_t u44[12];        /* +44..+5B */
} PartyStats;

/* Chargen work record, 36 (24h) bytes at 16E0:9B72 + 24h*i: the race's
   modifiers (mod[1..15] copied from g_race_mods by cg_random_gender;
   mod[15] then replaced by a random pick from g_race_special). */
typedef struct CgMods {
    int16_t mod[18];        /* [0], [16], [17] unused */
} CgMods;

/* One party member's text record, 148 (94h) bytes at 16E0:9C26 + 94h*i:
   four 37-byte strings. */
typedef struct PartyName {
    char name[37];          /* +00 character name (typed on the karakter screen) */
    char unused[37];        /* +25 never written by CHARGEN, not in GRP.DAT */
    char race_name[37];     /* +4A copy of g_race_name(race), filled by cg_load_party */
    char sayings[37];       /* +6F known-aphorism flags, '1'/'0' x 36 + NUL;
                               cg_save_party writes "1000...0" */
} PartyName;
#pragma pack(pop)

_Static_assert(sizeof(PartyStats) == 0x5C, "PartyStats is 92 bytes");
_Static_assert(offsetof(PartyStats, vocabulary) == 0x0A, "PartyStats.vocabulary");
_Static_assert(offsetof(PartyStats, faith) == 0x20, "PartyStats.faith");
_Static_assert(offsetof(PartyStats, height) == 0x24, "PartyStats.height");
_Static_assert(offsetof(PartyStats, special) == 0x2C, "PartyStats.special");
_Static_assert(offsetof(PartyStats, health) == 0x3A, "PartyStats.health");
_Static_assert(offsetof(PartyStats, portrait) == 0x42, "PartyStats.portrait");
_Static_assert(sizeof(CgMods) == 0x24, "CgMods is 36 bytes");
_Static_assert(sizeof(PartyName) == 0x94, "PartyName is 148 bytes");
_Static_assert(offsetof(PartyName, race_name) == 0x4A, "PartyName.race_name");
_Static_assert(offsetof(PartyName, sayings) == 0x6F, "PartyName.sayings");

/* ---- DGROUP ---------------------------------------------------------------- */
/* g_party_cur (16E0:2898, i16 current member 1..4, 5 after the last one is
   accepted) is defined in engine/intrface.h. */
#define PARTY_STATS_OFF      0x99A6
#define CG_MODS_OFF          0x9B72
#define PARTY_NAMES_OFF      0x9C26
#define g_party_stats        ((PartyStats *)DGP(PARTY_STATS_OFF))   /* [PARTY_SLOTS] */
#define g_cg_mods            ((CgMods *)DGP(CG_MODS_OFF))           /* [PARTY_SLOTS] */
#define g_party_names        ((PartyName *)DGP(PARTY_NAMES_OFF))    /* [PARTY_SLOTS] */
#define g_cg_gender_text     DGSTR(0x9F0A)  /* char[] "erkek"/"dişi" for the header */
#define g_cg_race_text       DGSTR(0xBB7C)  /* char[11] race name of the current member */

/* constant tables */
#define g_race_name(r)       DGSTR((uint16_t)(0x2638 + 11 * (r)))   /* char[11], 1..8:
                               insan, öğrenci, öğretmen, maganda, lavuk, memur, irospa,
                               siliconian */
#define g_portrait_name(p)   DGSTR((uint16_t)(0x269B + 13 * (p)))   /* char[13] "insan1.cmp".. */
#define g_race_mods(r, i)    DGS16((uint16_t)(0x2778 + 0x20 * (r) + 2 * (i)))  /* i16[16] */
#define g_race_special(r, k) DGS16((uint16_t)(0x0FE7 + 10 * (r) + 2 * (k)))    /* i16[5] */
#define g_cg_stat_max(k)     DGS16((uint16_t)(0x48B8 + 2 * (k)))  /* i16[12] for +0A..+20: */
#define g_cg_stat_min(k)     DGS16((uint16_t)(0x48D0 + 2 * (k)))  /*   value shown red/blue */
#define g_cg_walk_frame      DGS16(0x48E8)  /* i16  walking sprite frame 0..3 */
#define g_cg_blink           DGS16(0x48EA)  /* i16  idle animation state 0..3 (see cg_main) */
#define CG_FACE_PREFIX       0x48EC         /* char[13] "s": face picture = "s" + portrait */

/* surface slots used by the screen (engine/memman.h g_surf_*) */
#define CG_SLOT_BACK         1      /* karakter.cmp, the clean background */
#define CG_SLOT_WORK         6      /* 320x200 conventional work screen */
#define CG_SLOT_SPRITE       0x0B   /* walking (battle) sprite sheet g_portrait_name(p),
                                       SAVAS.CKD, e.g. insan1.cmp 149x39 */
#define CG_SLOT_SPRITE_KOR   0x13   /* its .KOR frame table (12 bytes/frame), an mm block */
#define CG_SLOT_ICONS        0x45   /* sematik.cmp: race and gender icons, 20x20 */
#define CG_SLOT_FACE         0x47   /* "s" + portrait name (IVIR.CKD, 160x32): five
                                       32x32 faces, 0 = normal, 0..2 = blink frames */

/* ---- functions (original address order) ------------------------------------- */
void    cg_random_gender(void);          /* 39E5:0000 (name from the inventory: it
                                            loads the race modifiers) */
void    cg_load_char_graphics(void);     /* 39E5:011F */
void    cg_roll_misc(void);              /* 39E5:035D (walking sprite animation step) */
void    cg_roll_stats(void);             /* 39E5:0437 */
void    cg_draw_stats(void);             /* 39E5:0F4E */
void    cg_draw_header(void);            /* 39E5:164A */
void    cg_draw_field_a(void);           /* 39E5:1753 race + gender icons */
void    cg_draw_field_b(void);           /* 39E5:17D2 name */
void    cg_save_party(void);             /* 39E5:182E writes GRP.DAT */
void    cg_load_party(const char *name); /* 39E5:1932 reads GRP.DAT / GRPHAZIR.DAT */
void    cg_free_graphics(void);          /* 39E5:1A1A */
int16_t cg_main(void);                   /* 39E5:1AA2 1 = party created, 0 = cancelled */

#endif
