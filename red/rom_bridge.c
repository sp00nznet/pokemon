/* rom_bridge.c -- stable C ABI for driving recompiled Pokemon Red from an
 * external process (e.g. a Python ctypes shim that mimics PyBoy).
 *
 * Every entry point wraps an existing gbrt primitive; no game logic lives
 * here. The DLL is SDL/ImGui-free: see platform_headless.c for the joypad
 * globals the core runtime references.
 *
 * Joypad masks (active-low, 0 = pressed) match platform_sdl.cpp:
 *   dpad : bit0 Right, bit1 Left,   bit2 Up,     bit3 Down
 *   btn  : bit0 A,     bit1 B,      bit2 Select, bit3 Start
 */
#include "rom.h"
#include "gbrt.h"
#include "ppu.h"
#include <stdint.h>
#include <string.h>

#ifdef _WIN32
#  define GBROM_API __declspec(dllexport)
#else
#  define GBROM_API __attribute__((visibility("default")))
#endif

/* Defined in platform_headless.c */
extern uint8_t g_joypad_buttons;
extern uint8_t g_joypad_dpad;

#define GB_SCREEN_W 160
#define GB_SCREEN_H 144

/* Create a DMG context with Red loaded and initialized. Returns an opaque
 * GBContext* (NULL on failure). */
GBROM_API void* gbrom_create(void) {
    GBConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.model = GB_MODEL_DMG;
    cfg.enable_audio = true;
    GBContext* ctx = gb_context_create(&cfg);
    if (!ctx) return NULL;
    rom_init(ctx);
    return ctx;
}

GBROM_API void gbrom_destroy(void* c) {
    if (c) gb_context_destroy((GBContext*)c);
}

/* Advance exactly `frames` rendered frames. gb_run_frame() resets the frame
 * flag itself and runs until the next VBlank, so one call == one frame. */
GBROM_API void gbrom_step(void* c, int frames) {
    GBContext* ctx = (GBContext*)c;
    if (!ctx) return;
    for (int i = 0; i < frames; i++) {
        gb_run_frame(ctx);
    }
}

GBROM_API uint8_t gbrom_read(void* c, uint16_t addr) {
    return c ? gb_read8((GBContext*)c, addr) : 0xFF;
}

GBROM_API void gbrom_write(void* c, uint16_t addr, uint8_t value) {
    if (c) gb_write8((GBContext*)c, addr, value);
}

/* Set the full joypad state at once. Pass active-low masks; 0xFF = nothing
 * pressed. The caller owns press/release timing. */
GBROM_API void gbrom_set_buttons(void* c, uint8_t dpad_mask, uint8_t btn_mask) {
    (void)c; /* joypad globals are process-wide in this single-context DLL */
    g_joypad_dpad    = dpad_mask;
    g_joypad_buttons = btn_mask;
}

/* Copy the current framebuffer into `out_rgb` as 160*144*3 bytes (R,G,B).
 * The runtime stores ARGB8888 (0xAARRGGBB). */
GBROM_API void gbrom_framebuffer(void* c, uint8_t* out_rgb) {
    GBContext* ctx = (GBContext*)c;
    if (!ctx || !out_rgb) return;
    const uint32_t* fb = gb_get_framebuffer(ctx);
    if (!fb) return;
    for (int i = 0; i < GB_SCREEN_W * GB_SCREEN_H; i++) {
        uint32_t px = fb[i];
        out_rgb[i * 3 + 0] = (uint8_t)((px >> 16) & 0xFF); /* R */
        out_rgb[i * 3 + 1] = (uint8_t)((px >> 8)  & 0xFF); /* G */
        out_rgb[i * 3 + 2] = (uint8_t)( px        & 0xFF); /* B */
    }
}

/* --- Optional fast episode reset: snapshot/restore our own state blob. ---
 * NOT compatible with PyBoy .state files. Layout is opaque and versioned by
 * size only; intended purely for round-tripping within this DLL. */

typedef struct {
    uint8_t  wram[0x1000 * 8];
    uint8_t  vram[0x2000 * 2];
    uint8_t  oam[0xA0];
    uint8_t  hram[0x7F];
    uint8_t  io[0x80 + 1];
    /* CPU + bank state */
    uint16_t af, bc, de, hl, sp, pc;
    uint16_t rom_bank;
    uint8_t  ram_bank, wram_bank, vram_bank;
    uint8_t  ime, halted;
    /* Full PPU struct (ly/lcdc/mode/scroll/framebuffer/...). Without this a
     * restored state renders blank until the game happens to resync the PPU. */
    GBPPU    ppu;
} GbromSnapshot;

GBROM_API int gbrom_snapshot_size(void) { return (int)sizeof(GbromSnapshot); }

GBROM_API void gbrom_snapshot(void* c, void* blob) {
    GBContext* ctx = (GBContext*)c;
    if (!ctx || !blob) return;
    GbromSnapshot* s = (GbromSnapshot*)blob;
    memcpy(s->wram, ctx->wram, sizeof(s->wram));
    memcpy(s->vram, ctx->vram, sizeof(s->vram));
    memcpy(s->oam,  ctx->oam,  sizeof(s->oam));
    memcpy(s->hram, ctx->hram, sizeof(s->hram));
    memcpy(s->io,   ctx->io,   sizeof(s->io));
    s->af = ctx->af; s->bc = ctx->bc; s->de = ctx->de; s->hl = ctx->hl;
    s->sp = ctx->sp; s->pc = ctx->pc;
    s->rom_bank = ctx->rom_bank; s->ram_bank = ctx->ram_bank;
    s->wram_bank = ctx->wram_bank; s->vram_bank = ctx->vram_bank;
    s->ime = ctx->ime; s->halted = ctx->halted;
    if (ctx->ppu) s->ppu = *(GBPPU*)ctx->ppu;
}

GBROM_API void gbrom_restore(void* c, const void* blob) {
    GBContext* ctx = (GBContext*)c;
    if (!ctx || !blob) return;
    const GbromSnapshot* s = (const GbromSnapshot*)blob;
    memcpy(ctx->wram, s->wram, sizeof(s->wram));
    memcpy(ctx->vram, s->vram, sizeof(s->vram));
    memcpy(ctx->oam,  s->oam,  sizeof(s->oam));
    memcpy(ctx->hram, s->hram, sizeof(s->hram));
    memcpy(ctx->io,   s->io,   sizeof(s->io));
    ctx->af = s->af; ctx->bc = s->bc; ctx->de = s->de; ctx->hl = s->hl;
    ctx->sp = s->sp; ctx->pc = s->pc;
    ctx->rom_bank = s->rom_bank; ctx->ram_bank = s->ram_bank;
    ctx->wram_bank = s->wram_bank; ctx->vram_bank = s->vram_bank;
    ctx->ime = s->ime; ctx->halted = s->halted;
    if (ctx->ppu) *(GBPPU*)ctx->ppu = s->ppu;
    gb_unpack_flags(ctx);
}
