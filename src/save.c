#include "save.h"
#include "hal/cpu.h"
#include "hal/memory.h"
#include <stdio.h>
#include <string.h>

bool save_write(const gb_state_t *gb, const char *path) {
    if (!gb->mem) return false;

    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "Warning: Cannot write save file: %s\n", path);
        return false;
    }

    /* Write all external RAM banks */
    size_t written = fwrite(gb->mem->extram, 1, sizeof(gb->mem->extram), f);
    fclose(f);

    if (written != sizeof(gb->mem->extram)) {
        fprintf(stderr, "Warning: Incomplete save write\n");
        return false;
    }

    printf("Saved to %s\n", path);
    return true;
}

bool save_load(gb_state_t *gb, const char *path) {
    if (!gb->mem) return false;

    FILE *f = fopen(path, "rb");
    if (!f) return false; /* No save file, not an error */

    size_t read = fread(gb->mem->extram, 1, sizeof(gb->mem->extram), f);
    fclose(f);

    if (read > 0) {
        printf("Loaded save from %s (%zu bytes)\n", path, read);
        return true;
    }

    return false;
}

void save_make_path(char *buf, size_t bufsz, const char *save_dir,
                    const char *game_name) {
    snprintf(buf, bufsz, "%s/pokemon_%s.sav", save_dir, game_name);
}
