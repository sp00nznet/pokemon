#include "ppu.h"
#include "cpu.h"
#include "memory.h"
#include <string.h>
#include <stdio.h>

/* --------------------------------------------------------------------------
 * DMG green palette (RGBA, 0xAARRGGBB stored as 0xAABBGGRR depending on
 * endianness -- we use 0xRRGGBBAA in our framebuffer, matching SDL's
 * PIXELFORMAT_RGBA8888 on little-endian which stores bytes as R,G,B,A).
 *
 * We store as 0xAARRGGBB (ARGB32) which maps to SDL PIXELFORMAT_ARGB8888.
 * Adjust if your SDL surface/texture uses a different format.
 * -------------------------------------------------------------------------- */
static const uint32_t dmg_colors[4] = {
    0xFF9BBC0F,  /* Color 0 - lightest */
    0xFF8BAC0F,  /* Color 1 */
    0xFF306230,  /* Color 2 */
    0xFF0F380F   /* Color 3 - darkest */
};

/* --------------------------------------------------------------------------
 * Helper: decode a DMG palette register into 4 RGBA colors
 * The palette register maps 2-bit color IDs (0-3) to DMG shade values (0-3).
 * -------------------------------------------------------------------------- */
static void decode_dmg_palette(uint32_t *dest, uint8_t palette_reg) {
    for (int i = 0; i < 4; i++) {
        uint8_t shade = (palette_reg >> (i * 2)) & 0x03;
        dest[i] = dmg_colors[shade];
    }
}

/* --------------------------------------------------------------------------
 * Helper: decode a CGB 15-bit color (RGB555) to RGBA8888
 * CGB color: bit 0-4 = red, bit 5-9 = green, bit 10-14 = blue
 * -------------------------------------------------------------------------- */
static uint32_t cgb_color_to_rgba(uint16_t color15) {
    uint8_t r5 = (color15 >>  0) & 0x1F;
    uint8_t g5 = (color15 >>  5) & 0x1F;
    uint8_t b5 = (color15 >> 10) & 0x1F;

    /* Scale 5-bit to 8-bit: (val << 3) | (val >> 2) for accurate mapping */
    uint8_t r = (uint8_t)((r5 << 3) | (r5 >> 2));
    uint8_t g = (uint8_t)((g5 << 3) | (g5 >> 2));
    uint8_t b = (uint8_t)((b5 << 3) | (b5 >> 2));

    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

/* --------------------------------------------------------------------------
 * Helper: decode an entire CGB palette bank (8 palettes, 4 colors each)
 * from 64 bytes of palette RAM.
 * -------------------------------------------------------------------------- */
static void decode_cgb_palettes(uint32_t palettes[8][4], const uint8_t *ram) {
    for (int pal = 0; pal < 8; pal++) {
        for (int col = 0; col < 4; col++) {
            int offset = pal * 8 + col * 2;
            uint16_t color15 = (uint16_t)(ram[offset] | (ram[offset + 1] << 8));
            palettes[pal][col] = cgb_color_to_rgba(color15);
        }
    }
}

/* --------------------------------------------------------------------------
 * Helper: request a STAT interrupt (set bit 1 of IF register)
 * -------------------------------------------------------------------------- */
static void request_stat_interrupt(gb_state_t *gb) {
    /* IF register is at 0xFF0F, which is io[0x0F] */
    gb->mem->io[0x0F] |= 0x02;
}

/* --------------------------------------------------------------------------
 * Helper: request a VBlank interrupt (set bit 0 of IF register)
 * -------------------------------------------------------------------------- */
static void request_vblank_interrupt(gb_state_t *gb) {
    gb->mem->io[0x0F] |= 0x01;
}

/* --------------------------------------------------------------------------
 * Helper: evaluate the STAT interrupt line (OR of all enabled conditions).
 * On real hardware the interrupt fires only on the RISING EDGE of this
 * composite line, preventing spurious double-fires when multiple conditions
 * overlap (known as "STAT blocking").
 * -------------------------------------------------------------------------- */
static bool eval_stat_line(const ppu_state_t *ppu) {
    bool line = false;

    /* LYC == LY coincidence */
    if ((ppu->stat & STAT_LYC_INT) && (ppu->ly == ppu->lyc))
        line = true;

    /* Mode-based STAT interrupts */
    switch (ppu->mode) {
        case PPU_MODE_HBLANK:
            if (ppu->stat & STAT_HBLANK_INT) line = true;
            break;
        case PPU_MODE_VBLANK:
            if (ppu->stat & STAT_VBLANK_INT) line = true;
            break;
        case PPU_MODE_OAM:
            if (ppu->stat & STAT_OAM_INT) line = true;
            break;
        default:
            break;
    }
    return line;
}

static void check_stat_interrupts(ppu_state_t *ppu, gb_state_t *gb) {
    bool new_line = eval_stat_line(ppu);

    /* Fire only on rising edge (LOW → HIGH) */
    if (new_line && !ppu->prev_stat_line) {
        request_stat_interrupt(gb);
    }
    ppu->prev_stat_line = new_line;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

void ppu_init(ppu_state_t *ppu) {
    memset(ppu, 0, sizeof(*ppu));

    /* Post-boot register defaults (as if boot ROM just finished) */
    ppu->lcdc = 0x91;  /* LCD on, BG on, BG tile data 0x8000 */
    ppu->stat = 0x00;
    ppu->scy  = 0x00;
    ppu->scx  = 0x00;
    ppu->ly   = 0x00;
    ppu->lyc  = 0x00;
    ppu->bgp  = 0xFC;  /* Default palette: 11 11 00 00 */
    ppu->obp[0] = 0xFF;
    ppu->obp[1] = 0xFF;
    ppu->wy   = 0x00;
    ppu->wx   = 0x00;
    ppu->mode = PPU_MODE_OAM;
    ppu->mode_cycles = 0;
    ppu->frame_ready = false;
    ppu->window_line = 0;
    ppu->cgb_mode = false;

    /* Decode initial palettes */
    decode_dmg_palette(ppu->bg_palette, ppu->bgp);
    decode_dmg_palette(ppu->obj_palette[0], ppu->obp[0]);
    decode_dmg_palette(ppu->obj_palette[1], ppu->obp[1]);

    /* Clear framebuffer to lightest color */
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            ppu->framebuffer[y][x] = dmg_colors[0];
        }
    }
}

/* -------------------------------------------------------------------------- */

void ppu_tick(ppu_state_t *ppu, gb_state_t *gb, uint32_t cycles) {
    /* If LCD is disabled, do nothing */
    if (!(ppu->lcdc & LCDC_LCD_ENABLE)) {
        return;
    }

    ppu->mode_cycles += cycles;

    /* Process all mode transitions that fit within accumulated cycles */
    bool progress = true;
    while (progress) {
        progress = false;

        switch (ppu->mode) {
            case PPU_MODE_OAM: /* Mode 2: OAM Search */
                if (ppu->mode_cycles >= CYCLES_OAM_SEARCH) {
                    ppu->mode_cycles -= CYCLES_OAM_SEARCH;
                    ppu->mode = PPU_MODE_TRANSFER;
                    progress = true;
                }
                break;

            case PPU_MODE_TRANSFER: /* Mode 3: Pixel Transfer */
                if (ppu->mode_cycles >= CYCLES_PIXEL_XFER) {
                    ppu->mode_cycles -= CYCLES_PIXEL_XFER;

                    /* Render the scanline at the end of mode 3 */
                    if (ppu->ly < SCREEN_HEIGHT) {
                        ppu_render_scanline(ppu, gb);
                    }

                    /* Transition to HBlank (mode 0) */
                    ppu->mode = PPU_MODE_HBLANK;
                    check_stat_interrupts(ppu, gb);
                    progress = true;
                }
                break;

            case PPU_MODE_HBLANK: /* Mode 0: HBlank */
                if (ppu->mode_cycles >= CYCLES_HBLANK) {
                    ppu->mode_cycles -= CYCLES_HBLANK;
                    ppu->ly++;

                    if (ppu->ly == SCREEN_HEIGHT) {
                        /* Enter VBlank (mode 1) */
                        ppu->mode = PPU_MODE_VBLANK;
                        ppu->frame_ready = true;
                        request_vblank_interrupt(gb);
                    } else {
                        /* Next visible scanline: back to OAM search */
                        ppu->mode = PPU_MODE_OAM;
                    }

                    /* Re-evaluate STAT line after LY change and mode change
                     * (handles both LYC coincidence and mode interrupts) */
                    check_stat_interrupts(ppu, gb);
                    progress = true;
                }
                break;

            case PPU_MODE_VBLANK: /* Mode 1: VBlank (lines 144-153) */
                if (ppu->mode_cycles >= CYCLES_PER_LINE) {
                    ppu->mode_cycles -= CYCLES_PER_LINE;
                    ppu->ly++;

                    if (ppu->ly >= LINES_PER_FRAME) {
                        /* Frame complete, reset to line 0 */
                        ppu->ly = 0;
                        ppu->window_line = 0;
                        ppu->mode = PPU_MODE_OAM;
                    }

                    /* Re-evaluate STAT line after LY change
                     * (handles LYC coincidence and mode interrupts) */
                    check_stat_interrupts(ppu, gb);
                    progress = true;
                }
                break;
        }
    }
}

/* --------------------------------------------------------------------------
 * Scanline renderer
 * -------------------------------------------------------------------------- */

void ppu_render_scanline(ppu_state_t *ppu, gb_state_t *gb) {
    uint8_t ly = ppu->ly;
    if (ly >= SCREEN_HEIGHT) return;

    uint32_t *line = ppu->framebuffer[ly];
    uint8_t *bg_prio = ppu->bg_priority;

    /* Clear priority array */
    memset(bg_prio, 0, SCREEN_WIDTH);

    /* ---- Background ---- */
    if (ppu->lcdc & LCDC_BG_ENABLE) {
        /* Tile map base: bit 3 of LCDC selects 0x9800 or 0x9C00 */
        uint16_t tile_map_base = (ppu->lcdc & LCDC_BG_TILEMAP) ? 0x9C00 : 0x9800;

        /* Background Y: current scanline + SCY, wrapped to 256 */
        uint8_t bg_y = ly + ppu->scy;
        /* Which row of tiles (0-31) and which pixel row within tile (0-7) */
        uint8_t tile_row = bg_y >> 3;           /* bg_y / 8 */
        uint8_t tile_y_offset = bg_y & 0x07;    /* bg_y % 8 */

        for (int px = 0; px < SCREEN_WIDTH; px++) {
            /* Background X: pixel + SCX, wrapped to 256 */
            uint8_t bg_x = (uint8_t)(px + ppu->scx);
            uint8_t tile_col = bg_x >> 3;       /* bg_x / 8 */
            uint8_t tile_x_offset = bg_x & 0x07;/* bg_x % 8 */

            /* Fetch tile index from tile map */
            uint16_t map_addr = tile_map_base + (uint16_t)(tile_row * 32 + tile_col);
            /* tile map is in VRAM: offset from 0x8000 */
            uint8_t tile_idx = gb->mem->vram[map_addr - 0x8000];

            /* Fetch tile data */
            uint16_t tile_data_addr;
            if (ppu->lcdc & LCDC_BG_TILEDATA) {
                /* Unsigned addressing: tiles 0-255 at 0x8000 */
                tile_data_addr = 0x8000 + (uint16_t)(tile_idx * 16);
            } else {
                /* Signed addressing: tile 0 at 0x9000, range -128..127 */
                tile_data_addr = (uint16_t)(0x9000 + ((int8_t)tile_idx) * 16);
            }

            /* Each tile row is 2 bytes; the pixel row offset selects which row */
            uint16_t row_addr = tile_data_addr + (uint16_t)(tile_y_offset * 2);
            uint8_t byte_lo = gb->mem->vram[row_addr - 0x8000];
            uint8_t byte_hi = gb->mem->vram[row_addr + 1 - 0x8000];

            /* Bit 7 is leftmost pixel; tile_x_offset 0 corresponds to bit 7 */
            uint8_t bit = 7 - tile_x_offset;
            uint8_t color_id = (uint8_t)(((byte_hi >> bit) & 1) << 1) |
                               ((byte_lo >> bit) & 1);

            /* Apply palette */
            if (ppu->cgb_mode) {
                /* In CGB mode, use CGB BG palette 0 for basic BG
                 * (full CGB attribute support would read palette from tile map attributes) */
                line[px] = ppu->cgb_bg_palettes[0][color_id];
            } else {
                line[px] = ppu->bg_palette[color_id];
            }
            bg_prio[px] = color_id;
        }
    } else {
        /* BG disabled: fill with color 0 (white/lightest) */
        for (int px = 0; px < SCREEN_WIDTH; px++) {
            line[px] = ppu->cgb_mode ? ppu->cgb_bg_palettes[0][0] : dmg_colors[0];
            bg_prio[px] = 0;
        }
    }

    /* ---- Window ---- */
    if ((ppu->lcdc & LCDC_WIN_ENABLE) && (ppu->lcdc & LCDC_BG_ENABLE)) {
        /* Window is visible if WX <= 166 and WY <= LY */
        if (ppu->wy <= ly && ppu->wx <= 166) {
            uint16_t win_map_base = (ppu->lcdc & LCDC_WIN_TILEMAP) ? 0x9C00 : 0x9800;
            uint8_t win_y = ppu->window_line;
            uint8_t tile_row = win_y >> 3;
            uint8_t tile_y_offset = win_y & 0x07;
            bool window_drawn = false;

            for (int px = 0; px < SCREEN_WIDTH; px++) {
                /* Window X position: WX is offset by 7 */
                int wx_adj = ppu->wx - 7;
                if (px < wx_adj) continue;

                window_drawn = true;
                int win_x = px - wx_adj;
                uint8_t tile_col = (uint8_t)(win_x >> 3);
                uint8_t tile_x_offset = (uint8_t)(win_x & 0x07);

                uint16_t map_addr = win_map_base + (uint16_t)(tile_row * 32 + tile_col);
                uint8_t tile_idx = gb->mem->vram[map_addr - 0x8000];

                uint16_t tile_data_addr;
                if (ppu->lcdc & LCDC_BG_TILEDATA) {
                    tile_data_addr = 0x8000 + (uint16_t)(tile_idx * 16);
                } else {
                    tile_data_addr = (uint16_t)(0x9000 + ((int8_t)tile_idx) * 16);
                }

                uint16_t row_addr = tile_data_addr + (uint16_t)(tile_y_offset * 2);
                uint8_t byte_lo = gb->mem->vram[row_addr - 0x8000];
                uint8_t byte_hi = gb->mem->vram[row_addr + 1 - 0x8000];

                uint8_t bit = 7 - tile_x_offset;
                uint8_t color_id = (uint8_t)(((byte_hi >> bit) & 1) << 1) |
                                   ((byte_lo >> bit) & 1);

                if (ppu->cgb_mode) {
                    line[px] = ppu->cgb_bg_palettes[0][color_id];
                } else {
                    line[px] = ppu->bg_palette[color_id];
                }
                bg_prio[px] = color_id;
            }

            if (window_drawn) {
                ppu->window_line++;
            }
        }
    }

    /* ---- Sprites (OBJ) ---- */
    if (ppu->lcdc & LCDC_OBJ_ENABLE) {
        /* Sprite height: 8 or 16 pixels */
        uint8_t sprite_height = (ppu->lcdc & LCDC_OBJ_SIZE) ? 16 : 8;

        /* Collect sprites on this scanline (max 10) */
        typedef struct {
            uint8_t y;
            uint8_t x;
            uint8_t tile;
            uint8_t flags;
            uint8_t oam_idx; /* Original OAM index for priority */
        } sprite_entry_t;

        sprite_entry_t sprites[MAX_SPRITES_PER_LINE];
        int sprite_count = 0;

        for (int i = 0; i < 40 && sprite_count < MAX_SPRITES_PER_LINE; i++) {
            uint8_t sy = gb->mem->oam[i * 4 + 0];  /* Y position + 16 */
            uint8_t sx = gb->mem->oam[i * 4 + 1];  /* X position + 8 */
            uint8_t tile = gb->mem->oam[i * 4 + 2];
            uint8_t flags = gb->mem->oam[i * 4 + 3];

            /* Sprite Y is offset by 16 */
            int real_y = (int)sy - 16;
            if (ly >= real_y && ly < real_y + sprite_height) {
                sprites[sprite_count].y = sy;
                sprites[sprite_count].x = sx;
                sprites[sprite_count].tile = tile;
                sprites[sprite_count].flags = flags;
                sprites[sprite_count].oam_idx = (uint8_t)i;
                sprite_count++;
            }
        }

        /* Sort by X coordinate (lower X = higher priority).
         * On DMG, sprites with the same X are sorted by OAM index (lower = higher priority).
         * Simple insertion sort since max 10 entries. */
        for (int i = 1; i < sprite_count; i++) {
            sprite_entry_t key = sprites[i];
            int j = i - 1;
            while (j >= 0 && (sprites[j].x > key.x ||
                             (sprites[j].x == key.x && sprites[j].oam_idx > key.oam_idx))) {
                sprites[j + 1] = sprites[j];
                j--;
            }
            sprites[j + 1] = key;
        }

        /* Render sprites in reverse order (lowest priority first, so highest
         * priority sprites overwrite). This gives correct layering. */
        for (int s = sprite_count - 1; s >= 0; s--) {
            sprite_entry_t *spr = &sprites[s];
            int real_y = (int)spr->y - 16;
            int real_x = (int)spr->x - 8;
            uint8_t flags = spr->flags;

            bool bg_over_obj = (flags >> 7) & 1;  /* Bit 7: BG priority */
            bool y_flip      = (flags >> 6) & 1;  /* Bit 6: Y flip */
            bool x_flip      = (flags >> 5) & 1;  /* Bit 5: X flip */
            int pal_idx      = (flags >> 4) & 1;  /* Bit 4: OBP0 or OBP1 */

            /* Which row of the sprite are we drawing? */
            int sprite_row = ly - real_y;
            if (y_flip) {
                sprite_row = sprite_height - 1 - sprite_row;
            }

            /* For 8x16 sprites, bit 0 of tile number is ignored */
            uint8_t tile_num = spr->tile;
            if (sprite_height == 16) {
                tile_num &= 0xFE;
            }

            /* Tile data always at 0x8000 for sprites */
            uint16_t tile_addr = 0x8000 + (uint16_t)(tile_num * 16) +
                                 (uint16_t)(sprite_row * 2);
            uint8_t byte_lo = gb->mem->vram[tile_addr - 0x8000];
            uint8_t byte_hi = gb->mem->vram[tile_addr + 1 - 0x8000];

            for (int bit_pos = 0; bit_pos < 8; bit_pos++) {
                int px = real_x + bit_pos;
                if (px < 0 || px >= SCREEN_WIDTH) continue;

                uint8_t bit = x_flip ? (uint8_t)bit_pos : (uint8_t)(7 - bit_pos);
                uint8_t color_id = (uint8_t)(((byte_hi >> bit) & 1) << 1) |
                                   ((byte_lo >> bit) & 1);

                /* Color 0 is transparent for sprites */
                if (color_id == 0) continue;

                /* BG priority: if set, sprite is hidden behind BG colors 1-3 */
                if (bg_over_obj && bg_prio[px] != 0) continue;

                /* Apply sprite palette */
                if (ppu->cgb_mode) {
                    line[px] = ppu->cgb_obj_palettes[pal_idx][color_id];
                } else {
                    line[px] = ppu->obj_palette[pal_idx][color_id];
                }
            }
        }
    }
}

/* --------------------------------------------------------------------------
 * Register access
 * -------------------------------------------------------------------------- */

uint8_t ppu_read_stat(ppu_state_t *ppu) {
    uint8_t result = 0x80; /* Bit 7 always reads 1 */

    /* Bits 6-3: writable interrupt enable flags */
    result |= (ppu->stat & 0x78);

    /* Bit 2: LYC == LY coincidence flag */
    if (ppu->ly == ppu->lyc) {
        result |= STAT_LYC_FLAG;
    }

    /* Bits 1-0: current mode */
    if (ppu->lcdc & LCDC_LCD_ENABLE) {
        result |= (ppu->mode & 0x03);
    }

    return result;
}

void ppu_write_lcdc(ppu_state_t *ppu, gb_state_t *gb, uint8_t val) {
    bool was_enabled = (ppu->lcdc & LCDC_LCD_ENABLE) != 0;
    bool now_enabled = (val & LCDC_LCD_ENABLE) != 0;

    ppu->lcdc = val;

    if (was_enabled && !now_enabled) {
        /* LCD turning off: reset PPU state */
        ppu->ly = 0;
        ppu->mode = PPU_MODE_HBLANK;
        ppu->mode_cycles = 0;
        ppu->window_line = 0;
    } else if (!was_enabled && now_enabled) {
        /* LCD turning on: start at mode 2 (OAM search) */
        ppu->mode = PPU_MODE_OAM;
        ppu->mode_cycles = 0;
        ppu->window_line = 0;
        check_stat_interrupts(ppu, gb);
    }
}

void ppu_write_stat(ppu_state_t *ppu, gb_state_t *gb, uint8_t val) {
    /* Only bits 6-3 are writable; bits 2-0 are read-only */
    ppu->stat = (ppu->stat & 0x07) | (val & 0x78);
    /* Re-evaluate STAT line: changing interrupt enables can cause a rising edge */
    check_stat_interrupts(ppu, gb);
}

void ppu_recheck_stat(ppu_state_t *ppu, gb_state_t *gb) {
    check_stat_interrupts(ppu, gb);
}

void ppu_write_bgp(ppu_state_t *ppu, uint8_t val) {
    ppu->bgp = val;
    decode_dmg_palette(ppu->bg_palette, val);
}

void ppu_write_obp(ppu_state_t *ppu, int idx, uint8_t val) {
    if (idx < 0 || idx > 1) return;
    ppu->obp[idx] = val;
    decode_dmg_palette(ppu->obj_palette[idx], val);
}

/* --------------------------------------------------------------------------
 * CGB palette access (for Pokemon Yellow GBC features)
 * -------------------------------------------------------------------------- */

void ppu_write_cgb_bg_palette_idx(ppu_state_t *ppu, uint8_t val) {
    ppu->cgb_bg_palette_idx = val;
}

void ppu_write_cgb_bg_palette_data(ppu_state_t *ppu, uint8_t val) {
    uint8_t index = ppu->cgb_bg_palette_idx & 0x3F;
    ppu->cgb_bg_palette_ram[index] = val;

    /* Auto-increment if bit 7 is set */
    if (ppu->cgb_bg_palette_idx & 0x80) {
        ppu->cgb_bg_palette_idx = 0x80 | ((index + 1) & 0x3F);
    }

    /* Re-decode all CGB BG palettes */
    decode_cgb_palettes(ppu->cgb_bg_palettes, ppu->cgb_bg_palette_ram);
}

void ppu_write_cgb_obj_palette_idx(ppu_state_t *ppu, uint8_t val) {
    ppu->cgb_obj_palette_idx = val;
}

void ppu_write_cgb_obj_palette_data(ppu_state_t *ppu, uint8_t val) {
    uint8_t index = ppu->cgb_obj_palette_idx & 0x3F;
    ppu->cgb_obj_palette_ram[index] = val;

    /* Auto-increment if bit 7 is set */
    if (ppu->cgb_obj_palette_idx & 0x80) {
        ppu->cgb_obj_palette_idx = 0x80 | ((index + 1) & 0x3F);
    }

    /* Re-decode all CGB OBJ palettes */
    decode_cgb_palettes(ppu->cgb_obj_palettes, ppu->cgb_obj_palette_ram);
}
