#ifndef PLATFORM_AUDIO_H
#define PLATFORM_AUDIO_H

#include <stdint.h>
#include <stdbool.h>

typedef struct gb_state gb_state_t;

typedef struct {
    uint32_t device_id;
    int sample_rate;
    int buffer_size;
    float volume;       /* Master volume 0.0 - 1.0 */
    bool muted;
} platform_audio_t;

/* Initialize SDL2 audio */
int platform_audio_init(platform_audio_t *audio, gb_state_t *gb);

/* Start/stop audio playback */
void platform_audio_start(platform_audio_t *audio);
void platform_audio_stop(platform_audio_t *audio);

/* Set volume */
void platform_audio_set_volume(platform_audio_t *audio, float volume);

/* Toggle mute */
void platform_audio_toggle_mute(platform_audio_t *audio);

/* Cleanup */
void platform_audio_destroy(platform_audio_t *audio);

#endif /* PLATFORM_AUDIO_H */
