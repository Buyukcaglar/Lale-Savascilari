/*
 * audio_sdl.h - SDL3 output of the port's audio (game + CD in one stream).
 * Implementation: src/platform/audio_sdl.c.  The logic (DMA model, capture,
 * resampling, CD mix) is in platform/audio.h; this only moves the frames
 * audio_render() produces to an SDL audio stream.
 *
 * Host (sdl3_main.c) usage:
 *
 *   audio_sdl_open(cd_music);   after SDL_Init(SDL_INIT_AUDIO | ...), before
 *                               the game starts.  cd_music != 0 mixes the CD
 *                               drive (cdrom_render) in - this replaces a
 *                               separate CD stream.  Returns 0, or -1 when no
 *                               device could be opened (everything keeps
 *                               running silently: CD position and game DMA
 *                               still advance with emulated time).
 *   audio_sdl_pump();           from the host pump (pc_set_host_pump), i.e.
 *                               on every pc_poll().  Renders up to the
 *                               current emulated time and queues it.
 *   audio_sdl_close();          at exit.
 *
 * Latency: the stream is kept at most AUDIO_SDL_MAX_QUEUE_MS of audio; when
 * the emulated clock runs ahead of the device (virtual time, a stall) the
 * excess queue is cleared.  Typical latency = the MCP ring (~91 ms, inherent
 * to the original) + one pump interval + the device buffer.
 */
#ifndef LALE_PLATFORM_AUDIO_SDL_H
#define LALE_PLATFORM_AUDIO_SDL_H

#define AUDIO_SDL_MAX_QUEUE_MS 250

int  audio_sdl_open(int cd_music);
void audio_sdl_pump(void);
void audio_sdl_close(void);

#endif
