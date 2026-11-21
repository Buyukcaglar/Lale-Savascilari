/*
 * audio.h - the sound card's DMA side and the host audio mix (no SDL here).
 * Implementation: src/platform/audio.c.  The SDL3 output on top of it:
 * src/platform/audio_sdl.h.
 *
 * ---------------------------------------------------------------------------
 * The model
 * ---------------------------------------------------------------------------
 * In the original, SDISB16 starts the SB16 in auto-init DMA mode over the
 * ring MCPOutputBuffer (port: mcp_dma_buffer[0 .. mcp_buffer_size())).  The
 * DMA controller moves one sample frame per 1/rate s to the DSP, wrapping
 * at the ring end.  The software mixer (src/dsmi/mcp.c) only runs inside
 * cdiPoll, from the 40 Hz timer routine (snd_timer_40hz); each poll reads
 * the DMA position (SDIGetBufferPosition) and mixes from its own write
 * position up to it - i.e. it refills the part of the ring the DMA has
 * just played, which is heard one lap (8016 bytes = 2004 frames ~ 91 ms
 * with the shipped SES.CFG) later.
 *
 * The port reproduces this with emulated time (the PIT clock, pc_pit_ticks):
 *
 *   frames_played(t) = floor((t - t_start) * rate / 1193182)
 *   mcp_dma_position = frames_played(t) * bytes_per_frame  mod  ring size
 *
 * where t_start is the PIT clock at SDIStartOutput (audio_dma_start).  Timer
 * routines run from pc_poll() with pc_pit_ticks() equal to the clock of
 * their IRQ, so each poll sees the DMA position the original would see for
 * an IRQ at that instant: the mixer issues the same CDI commands at the
 * same DSMI times, mixes the same byte ranges, and mcp_dma_buffer holds
 * bit-identical data.  (Assumption: an ideal DMA - the position moves in
 * whole frames exactly at the nominal rate; a real 8237/SB16 or DOSBox-X
 * may run a FIFO or a 1 ms transfer granularity ahead of that.)
 *
 * Capturing what is "heard": the ring only changes inside a poll, and a
 * poll overwrites exactly the bytes the DMA played since the previous one.
 * audio_dma_sync() advances the DMA to the current PIT clock and copies the
 * bytes it passes, in play order, into a FIFO.  It runs right before every
 * poll (snd_timer_40hz calls it) and from the host pump, so every byte is
 * captured after it was mixed and before it is overwritten - the FIFO is
 * the exact byte stream the DSP would have played (a lap is replayed
 * verbatim if no poll happens, as on the hardware).
 *
 * Host output (audio_render): one 44.1 kHz signed 16-bit stereo stream.
 *   - game audio: FIFO frames converted to s16 stereo (8-bit unsigned:
 *     (b - 128) << 8; mono: both sides) and resampled rate -> 44100 by
 *     linear interpolation (22050 -> 44100 inserts one midpoint per frame).
 *     The game stream is consumed at exactly the rate it is produced (both
 *     follow the PIT clock); output runs AUDIO_LAG_FRAMES behind so the
 *     interpolator always has its next input frame.
 *   - CD audio: cdrom_render() frames (already 44.1 kHz) added sample by
 *     sample, the sum clipped to -32768..32767.  The CD drive position thus
 *     advances with emulated time too (deterministic under virtual time).
 * Both of these happen after the game's bytes are final, so they cannot
 * change the game's audio data, only how it is played back.
 *
 * Everything here runs on the game thread (pc_poll / host pump).
 */
#ifndef LALE_PLATFORM_AUDIO_H
#define LALE_PLATFORM_AUDIO_H

#include <stddef.h>
#include <stdint.h>

#define AUDIO_OUT_RATE    44100u
#define AUDIO_LAG_FRAMES  64u          /* output frames behind the DMA (~1.5 ms) */
#define AUDIO_FIFO_BYTES  (1u << 20)   /* captured game bytes kept (oldest dropped) */

/* SDIStartOutput: the DMA starts at ring offset 0 now (pc_pit_ticks()).
   rate in Hz, sample_size 1 (unsigned 8-bit) or 2 (signed 16-bit),
   stereo 0/1, ring = mcp_buffer_size() bytes. */
void     audio_dma_start(uint16_t rate, uint8_t sample_size, uint8_t stereo, uint16_t ring);
/* SDIStopOutput / SDICloseDevice: the DMA stops (position frozen). */
void     audio_dma_stop(void);
/* Advance the DMA to pc_pit_ticks(): capture the played bytes, update
   mcp_dma_position.  Cheap when nothing changed. */
void     audio_dma_sync(void);
int      audio_dma_running(void);
uint64_t audio_dma_bytes_played(void);   /* total since audio_dma_start */

/* Raw captured game bytes, in play order (ring format), oldest first. */
size_t   audio_game_available(void);
size_t   audio_game_take(uint8_t *out, size_t max);
uint32_t audio_game_dropped(void);       /* bytes lost because the FIFO was full */

/* Mix CD audio (cdrom_render) into audio_render's output. */
void     audio_set_cd(int on);

/* Produce up to max_frames 44.1 kHz s16 stereo frames (interleaved L,R) up
   to the current emulated time; syncs the DMA first.  Returns the number
   of frames written. */
size_t   audio_render(int16_t *out, size_t max_frames);
uint64_t audio_frames_rendered(void);

/* Tests / restart: forget everything (DMA stopped, FIFO empty, clock 0). */
void     audio_reset(void);

#endif
