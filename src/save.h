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

/* Save state (full emulator snapshot) to file */
bool save_state_write(gb_state_t *gb, const char *path);

/* Load state (full emulator snapshot) from file */
bool save_state_load(gb_state_t *gb, const char *path);

/* Generate save state path */
void save_state_make_path(char *buf, size_t bufsz, const char *save_dir,
                          const char *game_name);

/* Save framebuffer as BMP screenshot */
bool screenshot_save(const uint32_t *framebuffer, int width, int height,
                     const char *path);

#endif /* SAVE_H */
