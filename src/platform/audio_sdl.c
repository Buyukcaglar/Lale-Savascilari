/*
 * audio_sdl.c - SDL3 output stream for platform/audio.c (see audio_sdl.h).
 */
#include "platform/audio_sdl.h"

#include <SDL3/SDL.h>

#include "platform/audio.h"

static SDL_AudioStream *stream;
static int              opened;

int audio_sdl_open(int cd_music)
{
    SDL_AudioSpec spec = { SDL_AUDIO_S16LE, 2, (int)AUDIO_OUT_RATE };
    audio_set_cd(cd_music);
    opened = 1;
    stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (!stream) {
        SDL_Log("audio: no audio device (%s); playing silently", SDL_GetError());
        return -1;
    }
    SDL_ResumeAudioStreamDevice(stream);
    return 0;
}

void audio_sdl_pump(void)
{
    int16_t buf[2 * 2048];
    size_t n;
    if (!opened)
        return;
    while ((n = audio_render(buf, 2048)) != 0) {
        if (!stream)
            continue;                  /* no device: just keep time moving */
        if (SDL_GetAudioStreamQueued(stream) >
            (int)(AUDIO_OUT_RATE * 4 * AUDIO_SDL_MAX_QUEUE_MS / 1000))
            SDL_ClearAudioStream(stream);
        SDL_PutAudioStreamData(stream, buf, (int)(n * 4));
    }
}

void audio_sdl_close(void)
{
    if (stream)
        SDL_DestroyAudioStream(stream);
    stream = NULL;
    opened = 0;
}
