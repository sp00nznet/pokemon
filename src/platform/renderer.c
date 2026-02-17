#include "renderer.h"

static const palette_t palettes[PALETTE_COUNT] = {
    [PALETTE_DMG_GREEN] = {
        .colors = {0xFF0F380F, 0xFF306230, 0xFF8BAC0F, 0xFF9BBC0F},
        .name = "DMG Green"
    },
    [PALETTE_GRAYSCALE] = {
        .colors = {0xFF000000, 0xFF555555, 0xFFAAAAAA, 0xFFFFFFFF},
        .name = "Grayscale"
    },
    [PALETTE_GB_POCKET] = {
        .colors = {0xFF000000, 0xFF656565, 0xFFAAAAAA, 0xFFC8C8C8},
        .name = "Game Boy Pocket"
    },
    [PALETTE_SEPIA] = {
        .colors = {0xFF342816, 0xFF6B5130, 0xFFB89868, 0xFFE8D0A0},
        .name = "Sepia"
    },
};

const palette_t *renderer_get_palette(palette_preset_t preset) {
    if (preset >= PALETTE_COUNT) preset = PALETTE_DMG_GREEN;
    return &palettes[preset];
}

void renderer_apply_palette(uint32_t *framebuffer, int width, int height,
                           const palette_t *palette) {
    int size = width * height;
    for (int i = 0; i < size; i++) {
        uint32_t idx = framebuffer[i] & 0x03;
        framebuffer[i] = palette->colors[idx];
    }
}
