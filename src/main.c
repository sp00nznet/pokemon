/*
 * main.c - Pokemon Static Recompilation entry point
 *
 * Uses gb-recompiled runtime for hardware emulation, SDL2 + ImGui platform.
 * Pokemon-specific: auto-input, frame dumping, dispatch debugging.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gbrt.h"
#include "platform_sdl.h"
#include "hwtrace.h"
#include "pokemon_debug.h"
#include "pokemon_rt.h"

/* Generated code dispatch */
extern void dispatch_call(GBContext *ctx, uint8_t bank, uint16_t addr);
extern void dispatch_init(GBContext *ctx);
extern void dispatch_run(GBContext *ctx);

/* Game name from build define */
#if defined(GAME_RED)
#define GAME_NAME "red"
#define GAME_TITLE "Pokemon Red"
#define ROM_FILE "roms/Pokemon Red Version.gb"
#elif defined(GAME_BLUE)
#define GAME_NAME "blue"
#define GAME_TITLE "Pokemon Blue"
#define ROM_FILE "roms/Pokemon Blue Version.gb"
#elif defined(GAME_YELLOW)
#define GAME_NAME "yellow"
#define GAME_TITLE "Pokemon Yellow"
#define ROM_FILE "roms/Pokemon Yellow Version - Special Pikachu Edition.gbc"
#else
#define GAME_NAME "red"
#define GAME_TITLE "Pokemon Red"
#define ROM_FILE "roms/Pokemon Red Version.gb"
#endif

/* Globals for pokemon_rt.h */
int g_pokemon_quit = 0;
int g_pokemon_frame_count = 0;

/* Stop-at-frame for automated testing */
static int g_stop_at_frame = -1;

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
    if (!data) { fclose(f); return NULL; }
    if (fread(data, 1, size, f) != (size_t)size) {
        free(data);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *size_out = (size_t)size;
    return data;
}

/* Dump WRAM to binary file at specific frame */
static int g_wram_dump_frame = -1;

/* Frame callback - called from pokemon_sync/pokemon_halt when PPU produces a frame.
 * Handles rendering, input, and frame timing. */
bool pokemon_on_frame(GBContext *ctx) {
    g_pokemon_frame_count++;

    /* WRAM dump at requested frame */
    if (g_pokemon_frame_count == g_wram_dump_frame && ctx->wram) {
        char path[128];
        snprintf(path, sizeof(path), "wram_frame_%d.bin", g_pokemon_frame_count);
        FILE *wf = fopen(path, "wb");
        if (wf) {
            fwrite(ctx->wram, 1, 0x2000, wf);
            fclose(wf);
            fprintf(stderr, "WRAM dump written to %s (bank=%d SP=%04X)\n",
                    path, ctx->rom_bank, ctx->sp);
        }
    }

    /* Render the frame */
    const uint32_t *fb = gb_get_framebuffer(ctx);
    if (fb) gb_platform_render_frame(fb);

    /* Process SDL events (input, quit, ImGui) */
    if (!gb_platform_poll_events(ctx)) {
        g_pokemon_quit = 1;
        return false;
    }

    /* Auto-exit after stop frame */
    if (g_stop_at_frame >= 0 && g_pokemon_frame_count >= g_stop_at_frame) {
        g_pokemon_quit = 1;
        return false;
    }

    /* Frame timing */
    gb_platform_vsync();

    return true;
}

int main(int argc, char *argv[]) {
    /* Parse command-line arguments */
    const char *auto_input_script = NULL;
    const char *dump_frames_str = NULL;
    const char *hwtrace_file = NULL;
    const char *trace_entries_file = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--hwtrace") == 0 && i + 1 < argc) {
            hwtrace_file = argv[++i];
            printf("HW trace output: %s\n", hwtrace_file);
        } else if (strcmp(argv[i], "--trace-entries") == 0 && i + 1 < argc) {
            trace_entries_file = argv[++i];
            printf("Entry trace output: %s\n", trace_entries_file);
        } else if (strcmp(argv[i], "--stop-at") == 0 && i + 1 < argc) {
            g_stop_at_frame = atoi(argv[++i]);
            printf("Will stop at frame %d\n", g_stop_at_frame);
        } else if (strcmp(argv[i], "--auto-input") == 0 && i + 1 < argc) {
            /* Collect all subsequent non-flag args as input script
             * Format for gb-recompiled: "frame:buttons:duration,..." */
            auto_input_script = argv[++i];
            printf("Auto-input: %s\n", auto_input_script);
        } else if (strcmp(argv[i], "--dump-frames") == 0 && i + 1 < argc) {
            dump_frames_str = argv[++i];
            printf("Will dump frames: %s\n", dump_frames_str);
        } else if (strcmp(argv[i], "--wram-dump") == 0 && i + 1 < argc) {
            g_wram_dump_frame = atoi(argv[++i]);
            printf("Will dump WRAM at frame %d\n", g_wram_dump_frame);
        } else if (strcmp(argv[i], "--debug-dispatch") == 0) {
            g_debug_dispatch = 1;
            printf("Debug: dispatch logging enabled\n");
        } else if (strcmp(argv[i], "--watch") == 0 && i + 1 < argc) {
            uint16_t waddr = (uint16_t)strtol(argv[++i], NULL, 16);
            watchpoint_add(waddr, 0);
            g_debug_watchpoints = 1;
            printf("Debug: watchpoint on 0x%04X\n", waddr);
        }
    }

    setvbuf(stderr, NULL, _IONBF, 0);
    printf("%s - Static Recompilation (gb-recompiled runtime)\n", GAME_TITLE);

    /* Load ROM */
    size_t rom_size;
    uint8_t *rom = load_rom(ROM_FILE, &rom_size);
    if (!rom) {
        fprintf(stderr, "Error: Cannot load ROM '%s'\n", ROM_FILE);
        return 1;
    }
    printf("ROM loaded: %zu bytes\n", rom_size);

    /* Create gb-recompiled context */
    GBConfig config = {
        .model = GB_MODEL_DMG,
        .enable_bootrom = false,
        .enable_audio = true,
        .enable_serial = false,
        .speed_percent = 100
    };
    GBContext *ctx = gb_context_create(&config);
    if (!ctx) {
        fprintf(stderr, "Error: Failed to create GB context\n");
        free(rom);
        return 1;
    }

    /* Load ROM into context */
    if (!gb_context_load_rom(ctx, rom, rom_size)) {
        fprintf(stderr, "Error: Failed to load ROM into context\n");
        gb_context_destroy(ctx);
        free(rom);
        return 1;
    }

    /* Fix post-boot registers based on game type.
     * Pokemon Red/Blue are DMG (A=0x01), Yellow is GBC-enhanced (A=0x11). */
#if defined(GAME_YELLOW)
    /* GBC mode: A=0x11 signals CGB to the game, enabling color palettes */
    ctx->af = 0x1180;  /* A=0x11 (CGB), F=0x80 (Z=1) */
    gb_unpack_flags(ctx);
    ctx->bc = 0x0000;
    ctx->de = 0xFF56;
    ctx->hl = 0x000D;
#else
    /* DMG mode: A=0x01 for Red/Blue */
    ctx->af = 0x01B0;  /* A=0x01 (DMG), F=0xB0 (Z=1 N=0 H=1 C=1) */
    gb_unpack_flags(ctx);
    ctx->bc = 0x0013;
    ctx->de = 0x00D8;
    ctx->hl = 0x014D;
#endif
    ctx->pc = 0x0100;  /* Entry point */

    /* Initialize SDL2 platform (window + ImGui) */
    if (!gb_platform_init(3)) {
        fprintf(stderr, "Error: Failed to initialize platform\n");
        gb_context_destroy(ctx);
        free(rom);
        return 1;
    }

    gb_platform_set_title(GAME_TITLE);
    gb_platform_register_context(ctx);

    /* Set up automation features */
    if (auto_input_script)
        gb_platform_set_input_script(auto_input_script);
    if (dump_frames_str)
        gb_platform_set_dump_frames(dump_frames_str);

    /* Initialize dispatch */
    dispatch_init(ctx);

    /* Initialize hardware trace if requested */
    if (hwtrace_file)
        hwtrace_init(hwtrace_file);
    if (trace_entries_file)
        gbrt_set_trace_file(trace_entries_file);

    printf("Starting %s...\n", GAME_TITLE);
    fflush(stdout);

    /* Run the game - generated code runs continuously.
     * Frame rendering happens via pokemon_sync when PPU produces frames.
     * Exits when g_pokemon_quit is set (user closes window or stop-at-frame). */
    dispatch_run(ctx);

    /* Close trace files */
    hwtrace_close();

    /* Save battery RAM on exit */
    printf("Saving...\n");
    gb_context_save_ram(ctx);

    /* Cleanup */
    gb_platform_shutdown();
    gb_context_destroy(ctx);
    free(rom);

    printf("Goodbye!\n");
    return 0;
}
