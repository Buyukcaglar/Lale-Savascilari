/*
 * hdvideo.h - the game's videos in a better copy (port code, not part of the
 * original game).
 *
 * tools/export_videos.py writes every GFL video of the game as <NAME>.mp4;
 * those can be upscaled (to 4K, by whatever tool) and put into the folder
 * VIDEO beside the executable.  When the program finds such files at start-up
 * it can show them in place of the original 320x200 picture while the game's
 * own player (src/game/gfl.c) runs unchanged underneath: the game still
 * reads and decodes its video, keeps its clock, plays its sound and takes
 * its keys; only what reaches the window differs.  The picture shown is the
 * frame of the .mp4 that belongs to the frame the game itself is showing
 * (frame n of the original = the time n / 10 s in the file), so the two
 * change together, also when the game drops frames; a copy with more frames
 * per second than the original's 10 shows its in-between frames in between.
 * The sound track of the .mp4 is not used.
 *
 * Starting a video in 4K costs the time to open the file and decode its
 * first frame (0.1-0.2 s measured), spent before the game starts the video.
 * Switching to 4K in the middle of a video decodes from the key frame before
 * that moment and shows the original until it has caught up: at once for a
 * file with a key frame every second or two, a second or more for one with
 * a single key frame.
 *
 * The player switches between the two with Ctrl+V (Windows and macOS alike)
 * at any moment, also in the middle of a video; "4K Video" or "Orijinal
 * Video" appears in the top right corner for three seconds at each switch.
 * Option `video` (4k | original) says what the program starts with.  A video
 * without a file in VIDEO is shown as the original.
 *
 * Files: VIDEO/<NAME>.mp4 (.m4v, .mov), the name as exported (INTRO.mp4,
 * BES-D-IN.mp4 ...), case ignored; a suffix after the name is accepted when
 * it is set off by '_', '-', ' ', '.' or '(' (INTRO_4k.mp4).
 * Picture: any size; it is drawn over the game's 4:3 area.  A frame wider
 * than 16:10 is taken to be padded at the sides (a 16:9 canvas): its black
 * bars are measured and the 4:3 or 16:10 picture in the middle is used.
 * Decoding: the operating system's - Media Foundation on Windows
 * (hdvideo_mf.c), AVFoundation on macOS (hdvideo_avf.m); H.264 plays on
 * both without anything installed.  Elsewhere there is no decoder and the
 * feature is off.
 *
 * This file: the list of files, and a decoder that runs on its own thread
 * and keeps a few frames ahead (hdvideo.c).  The switch, the drawing and the
 * notice are the host's (sdl3_main.c).
 */
#ifndef LALE_PLATFORM_HDVIDEO_H
#define LALE_PLATFORM_HDVIDEO_H

#include <stddef.h>
#include <stdint.h>

/* Lists the video files of `dir`.  Returns how many there are; 0 also when
   this build has no decoder. */
int hdvideo_scan(const char *dir);

/* The file for the game's video `name` ("INTRO"); 1 with its path in out. */
int hdvideo_find(const char *name, char *out, size_t size);

typedef struct HdVideo HdVideo;

/* Starts decoding `path` from `start` seconds on.  Returns at once. */
HdVideo *hdvideo_open(const char *path, double start);
void     hdvideo_close(HdVideo *v);

/* 1 a frame is waiting, 0 not yet, -1 the file cannot be played (the reason
   is logged once). */
int hdvideo_ready(HdVideo *v);

/* Hands the newest frame whose time is <= t to `upload` (pixels as B, G, R, X
   bytes, top row first, pitch = 4 * w), unless it has been handed over
   before.  Returns 1 when upload was called. */
typedef void (*HdUploadFn)(void *ctx, const uint8_t *bgrx, int w, int h);
int hdvideo_present(HdVideo *v, double t, HdUploadFn upload, void *ctx);

/* The time of the frame handed over last, -1 before the first. */
double hdvideo_shown_pts(const HdVideo *v);

/* ---- the system's decoder (hdvideo_mf.c, hdvideo_avf.m) ------------------------ */

typedef struct HdReader HdReader;

int       hdreader_available(void);
HdReader *hdreader_open(const char *path, int *w, int *h, char *err, size_t errlen);
/* Reading goes on at `seconds` (decoders start at the key frame before it;
   hdvideo.c drops what comes too early). */
int       hdreader_seek(HdReader *r, double seconds);
/* The next frame into bgrx (4 * w * h bytes), its time in *pts.  Returns 1,
   0 at the end of the file, -1 on an error. */
int       hdreader_read(HdReader *r, uint8_t *bgrx, double *pts);
void      hdreader_close(HdReader *r);

#endif
