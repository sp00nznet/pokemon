#include "config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void config_defaults(game_config_t *cfg) {
    cfg->window_scale = 3;
    cfg->palette = PALETTE_DMG_GREEN;
    cfg->volume = 0.5f;
    cfg->start_fullscreen = false;
    strcpy(cfg->save_dir, ".");
    input_init(&cfg->keys);
}

void config_load(game_config_t *cfg, const char *path) {
    config_defaults(cfg);

    FILE *f = fopen(path, "r");
    if (!f) return; /* Use defaults */

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        /* Skip comments and empty lines */
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;

        char key[64], value[256];
        if (sscanf(line, "%63[^=]=%255[^\n]", key, value) == 2) {
            /* Trim whitespace */
            char *k = key;
            while (*k == ' ') k++;
            char *v = value;
            while (*v == ' ') v++;

            if (strcmp(k, "scale") == 0) cfg->window_scale = atoi(v);
            else if (strcmp(k, "palette") == 0) cfg->palette = atoi(v);
            else if (strcmp(k, "volume") == 0) cfg->volume = (float)atof(v);
            else if (strcmp(k, "fullscreen") == 0) cfg->start_fullscreen = atoi(v) != 0;
            else if (strcmp(k, "save_dir") == 0) strncpy(cfg->save_dir, v, sizeof(cfg->save_dir) - 1);
        }
    }

    fclose(f);
}

void config_save(const game_config_t *cfg, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "Warning: Cannot save config to %s\n", path);
        return;
    }

    fprintf(f, "# Pokemon Recompilation Configuration\n");
    fprintf(f, "scale=%d\n", cfg->window_scale);
    fprintf(f, "palette=%d\n", cfg->palette);
    fprintf(f, "volume=%.2f\n", cfg->volume);
    fprintf(f, "fullscreen=%d\n", cfg->start_fullscreen ? 1 : 0);
    fprintf(f, "save_dir=%s\n", cfg->save_dir);

    fclose(f);
}
