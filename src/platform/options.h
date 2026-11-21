/*
 * options.h - port options (not part of the original game).
 *
 * Sources, later ones override earlier ones:
 *   1. built-in defaults (= the original's behaviour)
 *   2. lale.ini in the working directory   (key = value, ';'/'#' comments)
 *   3. the command line
 *
 *   ini key        command line            default  meaning
 *   cd_music       --cd-music / --no-cd-music   0   play the CD audio tracks
 *   cd_image       --cd-image <file.cue>      ""    the game CD as cue/bin
 *   game_dir       --game-dir <dir>           ""    folder with the original game files
 *                                                   ("" = the working directory if it
 *                                                   has them, else ./orig)
 *   ist_args       --ist-args <n>             0     arguments IST.EXE gets (only their
 *                                                   number counts, src/game/istanbul.h:
 *                                                   1 = skip the mode-12h pictures,
 *                                                   2 = videos then menu, 3..6 = menu
 *                                                   item n-3 at once)
 *
 * cd_music is off by default because the IST.EXE the port reproduces never
 * enables its CD-audio module (docs/DISC.md).  Turning it on makes the port
 * call o5737_cd_init() at start-up, as the 1997 CD release presumably did.
 */
#ifndef LALE_PLATFORM_OPTIONS_H
#define LALE_PLATFORM_OPTIONS_H

typedef struct LaleOptions {
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
