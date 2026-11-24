/*
 * setup.h - what happens between starting the program and starting the game
 * (port code): the folders beside the executable, the first-run import of
 * the player's copy of the game with its progress display, the choice of
 * edition.  Implementation: src/platform/setup.c; the work itself is
 * src/platform/install.c.  Player's view: docs/INSTALL.md.
 *
 *   <base>/DATA   LALE.CHD (from a CD) or the game's data files (2005 release)
 *   <base>/SAVE   everything the game writes - the same for every edition
 *   <base>/VIDEO  optional: the videos as 4K .mp4 files (hdvideo.h)
 *
 * <base> is the folder of the executable (macOS: the folder that holds the
 * .app); if nothing can be written there, the user's application-data folder
 * for "Lale64"; the base_dir option overrides both.
 *
 * Order of events in setup_game():
 *   1. A source named by the player (option `source`, or the first plain
 *      argument - a file or folder dropped on the program) is imported.
 *   2. With nothing installed, sources are looked for beside the executable
 *      and in the working directory, then a game CD in an optical drive;
 *      each is tried until one imports.
 *   3. Still nothing: the player is asked for a file or a folder (not in
 *      scripted or counted runs).
 *   4. Edition (option `edition`): "cd" and "2005" as said; "auto" is the CD
 *      edition when DATA holds a disc with audio tracks, else the 2005
 *      edition - but a source imported in this very run decides for the run.
 *      The 2005 edition also runs from a disc alone (the data files of both
 *      releases are identical); the CD edition needs the disc.
 *   5. SAVE/SES.CFG is written with defaults when the game would find none.
 */
#ifndef LALE_PLATFORM_SETUP_H
#define LALE_PLATFORM_SETUP_H

#include <SDL3/SDL.h>

#include "platform/options.h"

typedef struct GameSetup {
    char base[1024];
    char data[1100], save[1100];
    char video[1100];       /* the VIDEO folder, "" when there is none */
    int  edition;           /* EDITION_2005 or EDITION_CD (engine/edition.h) */
    int  disc;              /* DATA/LALE.CHD is open as the virtual CD drive */
} GameSetup;

/* Returns 0 when the game can start (g filled in; with g->disc the image is
   mounted), 1 when the player gave up, -1 on an error (already shown).
   interactive = message boxes and file dialogs are allowed. */
int setup_game(const LaleOptions *o, SDL_Window *window, SDL_Renderer *renderer, int interactive,
               GameSetup *g);

#endif
