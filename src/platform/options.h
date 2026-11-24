/*
 * options.h - port options (not part of the original game).
 *
 * Sources, later ones override earlier ones:
 *   1. built-in defaults (= the original's behaviour)
 *   2. lale.ini in the working directory   (key = value, ';'/'#' comments)
 *   3. the command line
 *
 *   ini key     command line          default  meaning
 *   source      --source <path>, or     ""    the player's copy of the game, imported at
 *               the first plain argument      start-up (setup.h): the game CD as a drive
 *                                             ("D:") or image (.cue, .iso, .chd), or the
 *                                             2005 release as .zip/.7z/.rar or a folder
 *   edition     --edition <e>          auto   auto | cd | 2005 (setup.h; docs/EDITIONS.md)
 *   base_dir    --base-dir <dir>        ""    the folder that holds DATA, SAVE and VIDEO
 *                                             ("" = the folder of the executable)
 *   video       --video <v>             4k    4k | original: what the videos start as when
 *                                             VIDEO holds 4K files (hdvideo.h; Ctrl+V
 *                                             switches while the game runs)
 *   ist_args    --ist-args <n>          0     arguments IST.EXE gets (only their number
 *                                             counts, src/game/istanbul.h: 1 = skip the
 *                                             mode-12h pictures, 2 = videos then menu,
 *                                             3..6 = menu item n-3 at once)
 * For development and the test tools (no import, no DATA/SAVE):
 *   game_dir    --game-dir <dir>        ""    run in this folder of the DOS game as it is
 *   cd_image    --cd-image <file>       ""    with game_dir and edition = cd: the game CD
 *                                             (.cue or .chd)
 *   cd_music    --cd-music              0     old name of edition = cd
 */
#ifndef LALE_PLATFORM_OPTIONS_H
#define LALE_PLATFORM_OPTIONS_H

typedef struct LaleOptions {
    char source[1024];
    char edition[16];
    char base_dir[1024];
    char video[16];
    int  cd_music;
    char cd_image[1024];
    char game_dir[1024];
    int  ist_args;
    /* test modes of the host program */
    int  cd_play_track;      /* --cd-play <n>: play CD track n and exit (0 = off) */
    int  cd_play_seconds;    /* --seconds <s>: how long (default 15)              */
    int  frames;             /* --frames <n>: quit after n presented frames (0 = run) */
    char screenshot[1024];   /* --screenshot <file.bmp>: last frame saved on quit */
} LaleOptions;

void options_defaults(LaleOptions *o);
/* Returns 0 if the file was read, -1 if it does not exist (not an error). */
int  options_load_ini(LaleOptions *o, const char *path);
/* Returns 0, or -1 on an unknown/malformed argument (message to stderr). */
int  options_parse_args(LaleOptions *o, int argc, char **argv);

#endif
