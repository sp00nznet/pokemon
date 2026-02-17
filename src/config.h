#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include "platform/renderer.h"
#include "platform/input.h"

typedef struct {
    int window_scale;
    palette_preset_t palette;
    float volume;
    key_bindings_t keys;
    bool start_fullscreen;
    char save_dir[256];
} game_config_t;

/* Load config from file (or set defaults) */
void config_load(game_config_t *cfg, const char *path);

/* Save config to file */
void config_save(const game_config_t *cfg, const char *path);

/* Set all defaults */
void config_defaults(game_config_t *cfg);

#endif /* CONFIG_H */
