#ifndef SAVE_H
#define SAVE_H

#include <stdint.h>
#include <stdbool.h>

typedef struct gb_state gb_state_t;

/* Save external RAM to .sav file */
bool save_write(const gb_state_t *gb, const char *path);

/* Load external RAM from .sav file */
bool save_load(gb_state_t *gb, const char *path);

/* Generate save path from ROM path */
void save_make_path(char *buf, size_t bufsz, const char *save_dir,
                    const char *game_name);

#endif /* SAVE_H */
