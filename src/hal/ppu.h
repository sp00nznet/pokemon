#ifndef PPU_H
#define PPU_H

#include <stdint.h>
#include <stdbool.h>

/* Forward declare gb_state_t (defined in cpu.h) */
typedef struct gb_state gb_state_t;

/* Screen dimensions */
#define SCREEN_WIDTH  160
#define SCREEN_HEIGHT 144

/* PPU modes */
#define PPU_MODE_HBLANK   0
#define PPU_MODE_VBLANK   1
#define PPU_MODE_OAM      2
#define PPU_MODE_TRANSFER 3

/* PPU mode durations in T-cycles */
#define CYCLES_OAM_SEARCH  80
#define CYCLES_PIXEL_XFER  172
#define CYCLES_HBLANK      204
#define CYCLES_PER_LINE    456
#define LINES_PER_FRAME    154

/* LCDC register bit masks */
#define LCDC_BG_ENABLE      (1 << 0)
#define LCDC_OBJ_ENABLE     (1 << 1)
#define LCDC_OBJ_SIZE       (1 << 2)  /* 0=8x8, 1=8x16 */
#define LCDC_BG_TILEMAP     (1 << 3)  /* 0=9800, 1=9C00 */
#define LCDC_BG_TILEDATA    (1 << 4)  /* 0=8800 signed, 1=8000 unsigned */
#define LCDC_WIN_ENABLE     (1 << 5)
#define LCDC_WIN_TILEMAP    (1 << 6)  /* 0=9800, 1=9C00 */
#define LCDC_LCD_ENABLE     (1 << 7)

/* STAT register bit masks */
#define STAT_MODE_MASK      0x03       /* bits 0-1: mode (read-only) */
#define STAT_LYC_FLAG       (1 << 2)  /* LYC == LY coincidence (read-only) */
#define STAT_HBLANK_INT     (1 << 3)  /* Mode 0 HBlank interrupt enable */
#define STAT_VBLANK_INT     (1 << 4)  /* Mode 1 VBlank interrupt enable */
#define STAT_OAM_INT        (1 << 5)  /* Mode 2 OAM interrupt enable */
#define STAT_LYC_INT        (1 << 6)  /* LYC==LY interrupt enable */

/* Max sprites per scanline */
#define MAX_SPRITES_PER_LINE 10

/* CGB palette size: 64 bytes each for BG and OBJ */
#define CGB_PALETTE_SIZE 64

typedef struct ppu_state {
    /* Scanline registers */
    uint8_t ly;             /* Current scanline (0-153) */
    uint8_t lyc;            /* LY compare value */

    /* Mode tracking */
    uint8_t mode;           /* Current PPU mode (0-3) */
    uint32_t mode_cycles;   /* Cycles accumulated in current mode */

    /* LCD registers */
    uint8_t lcdc;           /* LCD control register (0xFF40) */
    uint8_t stat;           /* LCD status register (0xFF41), writable bits only */

    /* Scroll registers */
    uint8_t scy;            /* Background scroll Y (0xFF42) */
    uint8_t scx;            /* Background scroll X (0xFF43) */

    /* Window position */
    uint8_t wy;             /* Window Y position (0xFF4A) */
    uint8_t wx;             /* Window X position (0xFF4B) */

    /* DMG palettes (raw register values) */
    uint8_t bgp;            /* Background palette (0xFF47) */
    uint8_t obp[2];         /* Object palettes (0xFF48, 0xFF49) */

    /* Decoded DMG palettes: 2-bit color index -> RGBA */
    uint32_t bg_palette[4];
    uint32_t obj_palette[2][4];

    /* CGB palette RAM (for Pokemon Yellow GBC compatibility) */
    uint8_t  cgb_bg_palette_ram[CGB_PALETTE_SIZE];   /* BG palette data */
    uint8_t  cgb_obj_palette_ram[CGB_PALETTE_SIZE];   /* OBJ palette data */
    uint8_t  cgb_bg_palette_idx;    /* BCPS: index + auto-increment bit 7 */
    uint8_t  cgb_obj_palette_idx;   /* OCPS: index + auto-increment bit 7 */
    uint32_t cgb_bg_palettes[8][4]; /* Decoded CGB BG palettes (8 palettes x 4 colors) */
    uint32_t cgb_obj_palettes[8][4];/* Decoded CGB OBJ palettes */
    bool     cgb_mode;              /* True if running in CGB mode */

    /* Internal window line counter (tracks which window line to render next) */
    uint8_t window_line;

    /* Framebuffer: RGBA pixels */
    uint32_t framebuffer[SCREEN_HEIGHT][SCREEN_WIDTH];

    /* Per-pixel BG priority info for sprite ordering */
    uint8_t bg_priority[SCREEN_WIDTH];

    /* Frame completion flag */
    bool frame_ready;

    /* STAT interrupt edge detection: interrupt fires only on rising edge */
    bool prev_stat_line;
} ppu_state_t;

/* Initialize PPU state */
void ppu_init(ppu_state_t *ppu);

/* Advance PPU by the given number of T-cycles */
void ppu_tick(ppu_state_t *ppu, gb_state_t *gb, uint32_t cycles);

/* Render one scanline (called internally at end of Mode 3) */
void ppu_render_scanline(ppu_state_t *ppu, gb_state_t *gb);

/* STAT register read (combines writable bits with mode/LYC flag) */
uint8_t ppu_read_stat(ppu_state_t *ppu);

/* Register write handlers */
void ppu_write_lcdc(ppu_state_t *ppu, gb_state_t *gb, uint8_t val);
void ppu_write_stat(ppu_state_t *ppu, uint8_t val);
void ppu_write_bgp(ppu_state_t *ppu, uint8_t val);
void ppu_write_obp(ppu_state_t *ppu, int idx, uint8_t val);

/* CGB palette writes */
void ppu_write_cgb_bg_palette_idx(ppu_state_t *ppu, uint8_t val);
void ppu_write_cgb_bg_palette_data(ppu_state_t *ppu, uint8_t val);
void ppu_write_cgb_obj_palette_idx(ppu_state_t *ppu, uint8_t val);
void ppu_write_cgb_obj_palette_data(ppu_state_t *ppu, uint8_t val);

#endif /* PPU_H */
