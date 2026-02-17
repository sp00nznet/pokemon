#ifndef RENDERER_H
#define RENDERER_H

#include <stdint.h>
#include "window.h"

/* Palette presets */
typedef enum {
    PALETTE_DMG_GREEN = 0,
    PALETTE_GRAYSCALE,
    PALETTE_GB_POCKET,
    PALETTE_SEPIA,
    PALETTE_COUNT
} palette_preset_t;

typedef struct {
    uint32_t colors[4]; /* 4 shades, darkest to lightest */
    const char *name;
} palette_t;

/* Get a palette preset */
const palette_t *renderer_get_palette(palette_preset_t preset);

/* Apply palette to framebuffer (convert 2-bit indices to RGBA) */
void renderer_apply_palette(uint32_t *framebuffer, int width, int height,
                           const palette_t *palette);

#endif /* RENDERER_H */
