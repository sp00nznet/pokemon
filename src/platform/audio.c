#include "audio.h"
#include "../hal/cpu.h"
#include "../hal/apu.h"
#include <SDL.h>
#include <string.h>

static platform_audio_t *g_audio = NULL;
static gb_state_t *g_gb = NULL;

static void audio_callback(void *userdata, Uint8 *stream, int len) {
    (void)userdata;
    float *out = (float *)stream;
    int samples = len / sizeof(float) / 2; /* Stereo */

    /* Zero-fill first to handle underruns and mute cleanly */
    memset(stream, 0, len);

    if (!g_gb || !g_gb->apu || g_audio->muted) {
        return;
    }

    int got = apu_get_samples(g_gb->apu, out, samples);

    /* Apply master volume */
    float vol = g_audio->volume;
    for (int i = 0; i < got * 2; i++) {
        out[i] *= vol;
    }
}

int platform_audio_init(platform_audio_t *audio, gb_state_t *gb) {
    g_audio = audio;
    g_gb = gb;

    audio->sample_rate = 48000;
    audio->buffer_size = 2048;
    audio->volume = 0.5f;
    audio->muted = false;

    SDL_AudioSpec desired, obtained;
    SDL_memset(&desired, 0, sizeof(desired));
    desired.freq = audio->sample_rate;
    desired.format = AUDIO_F32SYS;
    desired.channels = 2;
    desired.samples = audio->buffer_size;
    desired.callback = audio_callback;

    audio->device_id = SDL_OpenAudioDevice(NULL, 0, &desired, &obtained, 0);
    if (audio->device_id == 0) {
        fprintf(stderr, "SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
        return -1;
    }

    audio->sample_rate = obtained.freq;
    return 0;
}

void platform_audio_start(platform_audio_t *audio) {
    if (audio->device_id) {
        SDL_PauseAudioDevice(audio->device_id, 0);
    }
}

void platform_audio_stop(platform_audio_t *audio) {
    if (audio->device_id) {
        SDL_PauseAudioDevice(audio->device_id, 1);
    }
}

void platform_audio_set_volume(platform_audio_t *audio, float volume) {
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;
    audio->volume = volume;
}

void platform_audio_toggle_mute(platform_audio_t *audio) {
    audio->muted = !audio->muted;
}

void platform_audio_destroy(platform_audio_t *audio) {
    if (audio->device_id) {
        SDL_CloseAudioDevice(audio->device_id);
        audio->device_id = 0;
    }
    g_audio = NULL;
    g_gb = NULL;
}
