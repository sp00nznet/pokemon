#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>

#include "config.h"
#include "save.h"
#include "hal/cpu.h"
#include "hal/memory.h"
#include "hal/ppu.h"
#include "hal/apu.h"
#include "hal/timer.h"
#include "hal/joypad.h"
#include "hal/serial.h"
#include "hal/interrupts.h"
#include "platform/window.h"
#include "platform/renderer.h"
#include "platform/audio.h"
#include "platform/input.h"

/* Generated code dispatch - declared in generated/dispatch.h */
extern void dispatch_init(gb_state_t *gb);
extern void dispatch_run(gb_state_t *gb);

/* Game name from build define */
#if defined(GAME_RED)
#define GAME_NAME "red"
#define GAME_TITLE "Pokemon Red"
#define ROM_FILE "roms/Pokemon Red Version.gb"
#define MBC_TYPE MBC_MBC3
#elif defined(GAME_BLUE)
#define GAME_NAME "blue"
#define GAME_TITLE "Pokemon Blue"
#define ROM_FILE "roms/Pokemon Blue Version.gb"
#define MBC_TYPE MBC_MBC3
#elif defined(GAME_YELLOW)
#define GAME_NAME "yellow"
#define GAME_TITLE "Pokemon Yellow"
#define ROM_FILE "roms/Pokemon Yellow Version - Special Pikachu Edition.gbc"
#define MBC_TYPE MBC_MBC5
#else
#define GAME_NAME "red"
#define GAME_TITLE "Pokemon Red"
#define ROM_FILE "roms/Pokemon Red Version.gb"
#define MBC_TYPE MBC_MBC3
#endif

/* CPU clock: 4.194304 MHz */
#define CPU_FREQ        4194304
/* Cycles per frame at ~59.73 FPS */
#define CYCLES_PER_FRAME 70224

/* State passed to frame callback */
typedef struct {
    platform_window_t *window;
    platform_audio_t *audio;
    key_bindings_t *keys;
    game_config_t *config;
    int frame_count;
    Uint32 last_frame_ticks;
    /* Tracking for change-detection diagnostics */
    uint8_t prev_lcdc;
    uint8_t prev_bgp;
    int prev_vram_lo;
    int prev_vram_hi;
} frame_ctx_t;

static uint8_t *load_rom(const char *path, size_t *size_out) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Error: Cannot open ROM: %s\n", path);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t *data = (uint8_t *)malloc(size);
    if (!data) {
        fclose(f);
        return NULL;
    }

    if (fread(data, 1, size, f) != (size_t)size) {
        free(data);
        fclose(f);
        return NULL;
    }

    fclose(f);
    *size_out = (size_t)size;
    return data;
}

/* Dump framebuffer as BMP for debugging */
static void dump_framebuffer_bmp(const uint32_t fb[SCREEN_HEIGHT][SCREEN_WIDTH], int frame) {
    char path[64];
    snprintf(path, sizeof(path), "frame_%04d.bmp", frame);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    int w = SCREEN_WIDTH, h = SCREEN_HEIGHT;
    int row_bytes = w * 3;
    int pad = (4 - (row_bytes % 4)) % 4;
    int data_size = (row_bytes + pad) * h;
    int file_size = 54 + data_size;
    /* BMP header */
    uint8_t hdr[54] = {0};
    hdr[0]='B'; hdr[1]='M';
    hdr[2]=file_size; hdr[3]=file_size>>8; hdr[4]=file_size>>16; hdr[5]=file_size>>24;
    hdr[10]=54;
    hdr[14]=40;
    hdr[18]=w; hdr[19]=w>>8;
    hdr[22]=h; hdr[23]=h>>8;
    hdr[26]=1; hdr[28]=24;
    fwrite(hdr, 1, 54, f);
    /* BMP is bottom-up */
    uint8_t zero[4] = {0};
    for (int y = h - 1; y >= 0; y--) {
        for (int x = 0; x < w; x++) {
            uint32_t px = fb[y][x]; /* 0xAARRGGBB */
            uint8_t bgr[3] = { (uint8_t)(px), (uint8_t)(px >> 8), (uint8_t)(px >> 16) };
            fwrite(bgr, 1, 3, f);
        }
        if (pad) fwrite(zero, 1, pad, f);
    }
    fclose(f);
    fprintf(stderr, "Dumped %s\n", path);
}

/* Frame callback - called from hal_sync when PPU produces a frame */
static void on_frame(gb_state_t *gb, void *userdata) {
    frame_ctx_t *ctx = (frame_ctx_t *)userdata;
    ctx->frame_count++;

    /* Change-detection diagnostics for first 600 frames */
    if (ctx->frame_count <= 600) {
        int vram_lo = 0, vram_hi = 0;
        for (int i = 0; i < 0x1000; i++) {
            if (gb->mem->vram[i] != 0) vram_lo++;
        }
        for (int i = 0x1000; i < 0x1800; i++) {
            if (gb->mem->vram[i] != 0) vram_hi++;
        }
        uint8_t lcdc = gb->ppu->lcdc;
        uint8_t bgp = gb->ppu->bgp;

        /* Print on change or at key frames */
        bool changed = (lcdc != ctx->prev_lcdc || bgp != ctx->prev_bgp ||
                       vram_lo != ctx->prev_vram_lo || vram_hi != ctx->prev_vram_hi);
        bool key_frame = (ctx->frame_count <= 5 || ctx->frame_count % 60 == 0);

        if (changed || key_frame) {
            uint16_t map_base = (lcdc & 0x08) ? 0x9C00 : 0x9800;
            fprintf(stderr, "Frame %d:%s LCDC=%02X SCX=%d SCY=%d BGP=%02X VRAM[lo=%d hi=%d] map@%04X IE=%02X SP=%04X\n",
                    ctx->frame_count, changed ? " [CHANGED]" : "",
                    lcdc, gb->ppu->scx, gb->ppu->scy, bgp,
                    vram_lo, vram_hi, map_base, gb->mem->ie_reg, gb->sp);
        }

        ctx->prev_lcdc = lcdc;
        ctx->prev_bgp = bgp;
        ctx->prev_vram_lo = vram_lo;
        ctx->prev_vram_hi = vram_hi;

        /* At key transition frames, print a compact framebuffer summary:
         * For every 8th row, count non-lightest pixels across the row */
        if (changed && vram_lo + vram_hi > 0) {
            /* Dump BOTH tile maps - full 18 rows */
            for (int map_idx = 0; map_idx < 2; map_idx++) {
                uint16_t mb = map_idx ? 0x9C00 : 0x9800;
                int content = 0;
                for (int r = 0; r < 18; r++)
                    for (int c = 0; c < 20; c++) {
                        uint8_t t = gb->mem->vram[(mb - 0x8000) + r * 32 + c];
                        if (t != 0x00) content++;
                    }
                if (content > 0) {
                    fprintf(stderr, "  Map @%04X (%d non-zero tiles):\n", mb, content);
                    for (int r = 0; r < 18; r++) {
                        int row_content = 0;
                        for (int c = 0; c < 20; c++) {
                            uint8_t t = gb->mem->vram[(mb - 0x8000) + r * 32 + c];
                            if (t != 0x00) row_content++;
                        }
                        if (row_content > 0) {
                            fprintf(stderr, "    row%02d: ", r);
                            for (int c = 0; c < 20; c++) {
                                uint8_t t = gb->mem->vram[(mb - 0x8000) + r * 32 + c];
                                fprintf(stderr, "%02X ", t);
                            }
                            fprintf(stderr, "\n");
                        }
                    }
                } else {
                    fprintf(stderr, "  Map @%04X: all zero\n", mb);
                }
            }
        }
    }

    /* Dump BMP at first VRAM change and at periodic frames */
    if ((ctx->frame_count <= 300 && (ctx->frame_count % 60 == 0 || ctx->frame_count <= 5)) ||
        (ctx->frame_count >= 600 && ctx->frame_count <= 1200 && ctx->frame_count % 60 == 0)) {
        dump_framebuffer_bmp(gb->ppu->framebuffer, ctx->frame_count);
    }

    /* Render the frame */
    window_update(ctx->window, (const uint32_t *)gb->ppu->framebuffer,
                 SCREEN_WIDTH, SCREEN_HEIGHT);

    /* Process SDL events */
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_QUIT:
            gb->running = false;
            return;
        case SDL_KEYDOWN:
            if (!event.key.repeat) {
                input_handle_key(gb, ctx->keys, event.key.keysym.scancode, true);
                if (event.key.keysym.scancode == ctx->keys->key_fullscreen)
                    window_toggle_fullscreen(ctx->window);
                if (event.key.keysym.scancode == ctx->keys->key_mute)
                    platform_audio_toggle_mute(ctx->audio);
            }
            break;
        case SDL_KEYUP:
            input_handle_key(gb, ctx->keys, event.key.keysym.scancode, false);
            break;
        }
    }

    /* Frame timing */
    if (!input_fast_forward_active()) {
        Uint32 now = SDL_GetTicks();
        Uint32 elapsed = now - ctx->last_frame_ticks;
        if (elapsed < 16) {
            SDL_Delay(16 - elapsed);
        }
    }
    ctx->last_frame_ticks = SDL_GetTicks();
}

/* Process SDL events - called from generated code yield points */
bool hal_process_events(gb_state_t *gb) {
    (void)gb;
    return gb->running;
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    /* Ensure stderr is unbuffered so no output is lost on kill */
    setvbuf(stderr, NULL, _IONBF, 0);

    printf("%s - Static Recompilation\n", GAME_TITLE);

    /* Load config */
    game_config_t config;
    config_load(&config, "pokemon.cfg");

    /* Load ROM (still needed for data tables) */
    size_t rom_size;
    uint8_t *rom = load_rom(ROM_FILE, &rom_size);
    if (!rom) {
        fprintf(stderr, "Error: Cannot load ROM file. Make sure '%s' exists.\n", ROM_FILE);
        return 1;
    }
    printf("ROM loaded: %zu bytes\n", rom_size);

    /* Initialize Game Boy state */
    gb_state_t gb = {0};
    memory_state_t mem;
    ppu_state_t ppu;
    apu_state_t apu;
    timer_state_t timer;
    joypad_state_t joypad;

    mem_init(&mem, rom, rom_size, MBC_TYPE);
    ppu_init(&ppu);
    apu_init(&apu);
    timer_init(&timer);
    joypad_init(&joypad);

    gb.mem = &mem;
    gb.ppu = &ppu;
    gb.apu = &apu;
    gb.timer = &timer;
    gb.joypad = &joypad;

    cpu_init(&gb);

    /* Load save file */
    char save_path[512];
    save_make_path(save_path, sizeof(save_path), config.save_dir, GAME_NAME);
    save_load(&gb, save_path);

    /* Initialize platform */
    platform_window_t window;
    if (window_init(&window, GAME_TITLE, config.window_scale) != 0) {
        fprintf(stderr, "Error: Failed to initialize window\n");
        free(rom);
        return 1;
    }

    if (config.start_fullscreen) {
        window_toggle_fullscreen(&window);
    }

    platform_audio_t audio;
    if (platform_audio_init(&audio, &gb) == 0) {
        platform_audio_set_volume(&audio, config.volume);
        platform_audio_start(&audio);
    }

    key_bindings_t keys = config.keys;

    /* Set up frame callback */
    frame_ctx_t frame_ctx = {0};
    frame_ctx.window = &window;
    frame_ctx.audio = &audio;
    frame_ctx.keys = &keys;
    frame_ctx.config = &config;
    frame_ctx.frame_count = 0;
    frame_ctx.last_frame_ticks = SDL_GetTicks();

    gb.frame_callback = on_frame;
    gb.frame_userdata = &frame_ctx;

    /* Initialize generated code dispatch */
    dispatch_init(&gb);

    printf("Starting %s...\n", GAME_TITLE);
    fflush(stdout);

    /* Run the game - this calls into generated code which runs the game loop.
     * Frames are rendered via the frame_callback when the PPU produces them.
     * The generated code runs continuously and yields naturally at HALT. */
    dispatch_run(&gb);

    /* Save on exit */
    printf("Saving...\n");
    save_write(&gb, save_path);
    config_save(&config, "pokemon.cfg");

    /* Cleanup */
    platform_audio_stop(&audio);
    platform_audio_destroy(&audio);
    window_destroy(&window);
    free(rom);

    printf("Goodbye!\n");
    return 0;
}
