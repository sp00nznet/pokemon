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

/* Process SDL events - called from generated code yield points */
bool hal_process_events(gb_state_t *gb) {
    (void)gb;
    /* Events are processed in the main loop */
    return gb->running;
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

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

    /* Initialize generated code dispatch */
    dispatch_init(&gb);

    printf("Starting %s...\n", GAME_TITLE);

    /* Main loop */
    uint64_t frame_start_cycles;
    Uint32 frame_start_ticks;
    bool request_fullscreen_toggle = false;
    bool request_mute_toggle = false;

    while (gb.running) {
        frame_start_cycles = gb.cycles;
        frame_start_ticks = SDL_GetTicks();

        /* Process SDL events */
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_QUIT:
                gb.running = false;
                break;
            case SDL_KEYDOWN:
                if (!event.key.repeat) {
                    input_handle_key(&gb, &keys, event.key.keysym.scancode, true);
                    if (event.key.keysym.scancode == keys.key_fullscreen)
                        request_fullscreen_toggle = true;
                    if (event.key.keysym.scancode == keys.key_mute)
                        request_mute_toggle = true;
                }
                break;
            case SDL_KEYUP:
                input_handle_key(&gb, &keys, event.key.keysym.scancode, false);
                break;
            }
        }

        if (request_fullscreen_toggle) {
            window_toggle_fullscreen(&window);
            request_fullscreen_toggle = false;
        }
        if (request_mute_toggle) {
            platform_audio_toggle_mute(&audio);
            request_mute_toggle = false;
        }

        /* Run generated code for one frame worth of cycles */
        gb.target_cycles = gb.cycles + CYCLES_PER_FRAME;
        dispatch_run(&gb);

        /* Update audio */
        apu_tick(&apu, &gb, CYCLES_PER_FRAME);

        /* If PPU has a frame ready, display it */
        if (ppu.frame_ready) {
            window_update(&window, (const uint32_t *)ppu.framebuffer,
                         SCREEN_WIDTH, SCREEN_HEIGHT);
            ppu.frame_ready = false;
        }

        /* Frame timing (skip if fast-forwarding) */
        if (!input_fast_forward_active()) {
            Uint32 frame_time = SDL_GetTicks() - frame_start_ticks;
            if (frame_time < 16) {
                SDL_Delay(16 - frame_time);
            }
        }
    }

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
