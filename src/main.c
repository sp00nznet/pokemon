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

/* Maximum scheduled frame dumps */
#define MAX_DUMP_FRAMES 64
/* Maximum scheduled auto-input events */
#define MAX_AUTO_INPUTS 256
/* Duration to hold an auto-input button (frames) */
#define AUTO_INPUT_HOLD 10

/* Auto-input event: press a button at a specific frame */
typedef struct {
    int frame;        /* Frame to press the button */
    uint8_t button;   /* Button mask (BTN_A, BTN_START, etc.) */
    bool is_direction;
} auto_input_t;

/* State passed to frame callback */
typedef struct {
    platform_window_t *window;
    platform_audio_t *audio;
    key_bindings_t *keys;
    game_config_t *config;
    char state_path[512];     /* Save state file path */
    int frame_count;
    int screenshot_counter;   /* For sequential screenshot filenames */
    Uint64 last_frame_perf;   /* SDL_GetPerformanceCounter at last frame */
    Uint64 perf_freq;         /* SDL_GetPerformanceFrequency */
    /* Automated frame dump mode */
    int dump_frames[MAX_DUMP_FRAMES];
    int dump_frame_count;
    int stop_at_frame;        /* Auto-exit after this frame (-1 = disabled) */
    /* Automated input injection */
    auto_input_t auto_inputs[MAX_AUTO_INPUTS];
    int auto_input_count;
} frame_ctx_t;

/* Game Boy frame period: 4194304 Hz / (456*154 lines) ≈ 59.7275 fps
 * Frame period in nanoseconds: 1e9 / 59.7275 ≈ 16742706 ns */
#define FRAME_PERIOD_NS 16742706ULL

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

static void take_screenshot(gb_state_t *gb, frame_ctx_t *ctx) {
    char path[256];
    snprintf(path, sizeof(path), "screenshot_%04d_frame%d.bmp",
             ctx->screenshot_counter++, ctx->frame_count);
    screenshot_save((const uint32_t *)gb->ppu->framebuffer,
                    SCREEN_WIDTH, SCREEN_HEIGHT, path);
}

/* Check if current frame is a scheduled dump frame */
static bool is_dump_frame(frame_ctx_t *ctx) {
    for (int i = 0; i < ctx->dump_frame_count; i++) {
        if (ctx->dump_frames[i] == ctx->frame_count)
            return true;
    }
    return false;
}

/* Dump PPU/sprite debug state for a frame */
static void dump_debug_state(gb_state_t *gb, int frame) {
    ppu_state_t *ppu = gb->ppu;
    printf("\n=== Debug State @ Frame %d ===\n", frame);
    printf("LCDC=%02X SCX=%d SCY=%d WX=%d WY=%d LY=%d\n",
           ppu->lcdc, ppu->scx, ppu->scy, ppu->wx, ppu->wy, ppu->ly);
    printf("BGP=%02X OBP0=%02X OBP1=%02X\n", ppu->bgp, ppu->obp[0], ppu->obp[1]);
    printf("OAM visible sprites:\n");
    for (int i = 0; i < 40; i++) {
        uint8_t y = gb->mem->oam[i * 4 + 0];
        uint8_t x = gb->mem->oam[i * 4 + 1];
        uint8_t tile = gb->mem->oam[i * 4 + 2];
        uint8_t flags = gb->mem->oam[i * 4 + 3];
        if (y >= 1 && y < 160 && x > 0 && x < 168) {
            printf("  [%2d] scr(%3d,%3d) tile=%02X flags=%02X pal=%d%s%s\n",
                   i, x - 8, y - 16, tile, flags,
                   (flags >> 4) & 1,
                   (flags & 0x20) ? " xflip" : "",
                   (flags & 0x40) ? " yflip" : "");
        }
    }
    /* Full shadow OAM dump - ALL 40 entries */
    printf("Shadow OAM @ 0xC300 (all 40 entries):\n");
    for (int i = 0; i < 40; i++) {
        uint16_t addr = 0xC300 + (uint16_t)(i * 4);
        uint8_t y = mem_read8(gb, addr);
        uint8_t x = mem_read8(gb, addr + 1);
        uint8_t tile = mem_read8(gb, addr + 2);
        uint8_t flags = mem_read8(gb, addr + 3);
        if (y != 0 || x != 0 || tile != 0 || flags != 0) {
            printf("  [%2d] @%04X Y=%3d X=%3d tile=%02X flags=%02X scr(%3d,%3d)\n",
                   i, addr, y, x, tile, flags, x - 8, y - 16);
        }
    }
    /* Player sprite state data (wSpriteStateData1 @ C100, 16 bytes per entry) */
    printf("wSpriteStateData1 (first 8 entries, 16 bytes each):\n");
    for (int i = 0; i < 8; i++) {
        uint16_t base = 0xC100 + (uint16_t)(i * 16);
        printf("  [%d] @%04X:", i, base);
        for (int j = 0; j < 16; j++) {
            printf(" %02X", mem_read8(gb, base + (uint16_t)j));
        }
        printf("\n");
    }
    /* wSpriteStateData2 @ C200 */
    printf("wSpriteStateData2 (first 8 entries, 16 bytes each):\n");
    for (int i = 0; i < 8; i++) {
        uint16_t base = 0xC200 + (uint16_t)(i * 16);
        printf("  [%d] @%04X:", i, base);
        for (int j = 0; j < 16; j++) {
            printf(" %02X", mem_read8(gb, base + (uint16_t)j));
        }
        printf("\n");
    }
    /* Key WRAM variables for player/sprite system */
    printf("Key WRAM: wNumSprites=%02X wXCoord=%02X wYCoord=%02X wCurMap=%02X\n",
           mem_read8(gb, 0xD4E1),   /* wNumSprites */
           mem_read8(gb, 0xD362),   /* wXCoord (player X on map) */
           mem_read8(gb, 0xD361),   /* wYCoord (player Y on map) */
           mem_read8(gb, 0xD35E));  /* wCurMap */
    printf("wWalkCounter=%02X wPlayerDir=%02X wSpriteCount=%02X\n",
           mem_read8(gb, 0xCFC5),   /* wWalkCounter */
           mem_read8(gb, 0xC109),   /* wPlayerFacingDirection */
           mem_read8(gb, 0xD4E1));  /* wNumSprites on current map */
    /* Game phase indicators */
    printf("hLoadedROMBank=%02X wd72e=%02X wIsInBattle=%02X\n",
           mem_read8(gb, 0xFFB8),   /* hLoadedROMBank */
           mem_read8(gb, 0xD72E),   /* wd72e flags */
           mem_read8(gb, 0xCC57));  /* wIsInBattle */
    printf("wCurrentMenuItem=%02X wTextBoxID=%02X wCurOpponent=%02X\n",
           mem_read8(gb, 0xCC26),   /* wCurrentMenuItem */
           mem_read8(gb, 0xD125),   /* wTextBoxID */
           mem_read8(gb, 0xD059));  /* wCurOpponent */
    /* Player name (11 bytes at D158) - shows if name was entered */
    printf("wPlayerName=");
    for (int i = 0; i < 11; i++)
        printf("%02X", mem_read8(gb, 0xD158 + (uint16_t)i));
    printf("\n");
    /* WRAM 0xD700 area: game progress flags */
    printf("wd72d=%02X wd732=%02X wPlayTimeH=%02X wPlayTimeM=%02X\n",
           mem_read8(gb, 0xD72D),   /* wd72d flags */
           mem_read8(gb, 0xD732),   /* wd732 flags */
           mem_read8(gb, 0xDA40),   /* wPlayTimeHours */
           mem_read8(gb, 0xDA41));  /* wPlayTimeMinutes */
    /* Map connection/state */
    printf("wLastMap=%02X wDestMap=%02X wGameProgressFlags=%02X\n",
           mem_read8(gb, 0xD365),   /* wLastMap */
           mem_read8(gb, 0xFF8B),   /* hFF8B (temp) */
           mem_read8(gb, 0xD747));  /* wFlags_D747 */
}

/* Frame callback - called from hal_sync when PPU produces a frame */
/* Process auto-input events for the current frame */
static void process_auto_inputs(gb_state_t *gb, frame_ctx_t *ctx) {
    for (int i = 0; i < ctx->auto_input_count; i++) {
        auto_input_t *inp = &ctx->auto_inputs[i];
        if (ctx->frame_count == inp->frame) {
            /* Press the button */
            joypad_press(gb->joypad, gb, inp->button, inp->is_direction);
        } else if (ctx->frame_count == inp->frame + AUTO_INPUT_HOLD) {
            /* Release the button after hold duration */
            joypad_release(gb->joypad, inp->button, inp->is_direction);
        }
    }
}

static void on_frame(gb_state_t *gb, void *userdata) {
    frame_ctx_t *ctx = (frame_ctx_t *)userdata;
    ctx->frame_count++;

    /* Process automated inputs (press/release at scheduled frames) */
    process_auto_inputs(gb, ctx);

    /* Auto-dump scheduled frames */
    if (is_dump_frame(ctx)) {
        char path[256];
        snprintf(path, sizeof(path), "frame_%05d.bmp", ctx->frame_count);
        screenshot_save((const uint32_t *)gb->ppu->framebuffer,
                        SCREEN_WIDTH, SCREEN_HEIGHT, path);
        dump_debug_state(gb, ctx->frame_count);
    }

    /* Auto-exit after stop frame */
    if (ctx->stop_at_frame >= 0 && ctx->frame_count >= ctx->stop_at_frame) {
        gb->running = false;
        return;
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
                if (event.key.keysym.scancode == ctx->keys->key_save_state)
                    save_state_write(gb, ctx->state_path);
                if (event.key.keysym.scancode == ctx->keys->key_load_state)
                    save_state_load(gb, ctx->state_path);
                if (event.key.keysym.scancode == ctx->keys->key_screenshot)
                    take_screenshot(gb, ctx);
            }
            break;
        case SDL_KEYUP:
            input_handle_key(gb, ctx->keys, event.key.keysym.scancode, false);
            break;
        }
    }

    /* Frame timing: use high-resolution counter for accurate 59.7275 fps */
    if (!input_fast_forward_active()) {
        Uint64 now = SDL_GetPerformanceCounter();
        Uint64 elapsed_ticks = now - ctx->last_frame_perf;
        /* Convert elapsed ticks to nanoseconds */
        Uint64 elapsed_ns = (elapsed_ticks * 1000000000ULL) / ctx->perf_freq;
        if (elapsed_ns < FRAME_PERIOD_NS) {
            Uint32 wait_ms = (Uint32)((FRAME_PERIOD_NS - elapsed_ns) / 1000000ULL);
            if (wait_ms > 0)
                SDL_Delay(wait_ms);
            /* Spin-wait for remaining sub-millisecond portion */
            while (1) {
                now = SDL_GetPerformanceCounter();
                elapsed_ticks = now - ctx->last_frame_perf;
                elapsed_ns = (elapsed_ticks * 1000000000ULL) / ctx->perf_freq;
                if (elapsed_ns >= FRAME_PERIOD_NS) break;
            }
        }
    }
    ctx->last_frame_perf = SDL_GetPerformanceCounter();
}

/* Process SDL events - called from generated code yield points */
bool hal_process_events(gb_state_t *gb) {
    (void)gb;
    return gb->running;
}

/* Parse comma-separated frame numbers into array. Returns count. */
static int parse_frame_list(const char *str, int *out, int max) {
    int count = 0;
    const char *p = str;
    while (*p && count < max) {
        out[count++] = atoi(p);
        while (*p && *p != ',') p++;
        if (*p == ',') p++;
    }
    return count;
}

/* Parse a button name to button mask and direction flag.
 * Format: "a", "b", "start", "select", "up", "down", "left", "right" */
static bool parse_button_name(const char *name, uint8_t *button, bool *is_dir) {
    if (strcmp(name, "a") == 0)      { *button = BTN_A;      *is_dir = false; return true; }
    if (strcmp(name, "b") == 0)      { *button = BTN_B;      *is_dir = false; return true; }
    if (strcmp(name, "start") == 0)  { *button = BTN_START;  *is_dir = false; return true; }
    if (strcmp(name, "select") == 0) { *button = BTN_SELECT; *is_dir = false; return true; }
    if (strcmp(name, "up") == 0)     { *button = BTN_UP;     *is_dir = true;  return true; }
    if (strcmp(name, "down") == 0)   { *button = BTN_DOWN;   *is_dir = true;  return true; }
    if (strcmp(name, "left") == 0)   { *button = BTN_LEFT;   *is_dir = true;  return true; }
    if (strcmp(name, "right") == 0)  { *button = BTN_RIGHT;  *is_dir = true;  return true; }
    return false;
}

/* Parse "frame:button" auto-input specs. Returns count. */
static int parse_auto_inputs(int argc, char *argv[], int start_idx,
                              auto_input_t *out, int max) {
    int count = 0;
    for (int i = start_idx; i < argc && count < max; i++) {
        /* Stop if it looks like another flag */
        if (argv[i][0] == '-') break;

        char buf[64];
        strncpy(buf, argv[i], sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';

        char *colon = strchr(buf, ':');
        if (!colon) break; /* not a frame:button spec */

        *colon = '\0';
        int frame = atoi(buf);
        uint8_t button;
        bool is_dir;
        if (!parse_button_name(colon + 1, &button, &is_dir)) {
            fprintf(stderr, "Unknown button: %s\n", colon + 1);
            break;
        }
        out[count].frame = frame;
        out[count].button = button;
        out[count].is_direction = is_dir;
        count++;
    }
    return count;
}

int main(int argc, char *argv[]) {
    /* Parse command-line arguments */
    int dump_frames[MAX_DUMP_FRAMES] = {0};
    int dump_frame_count = 0;
    int stop_at_frame = -1;
    auto_input_t auto_inputs[MAX_AUTO_INPUTS] = {0};
    int auto_input_count = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--dump-frames") == 0 && i + 1 < argc) {
            dump_frame_count = parse_frame_list(argv[++i], dump_frames, MAX_DUMP_FRAMES);
            printf("Will dump %d frames: ", dump_frame_count);
            for (int j = 0; j < dump_frame_count; j++)
                printf("%d%s", dump_frames[j], j < dump_frame_count - 1 ? "," : "");
            printf("\n");
        } else if (strcmp(argv[i], "--stop-at") == 0 && i + 1 < argc) {
            stop_at_frame = atoi(argv[++i]);
            printf("Will stop at frame %d\n", stop_at_frame);
        } else if (strcmp(argv[i], "--auto-input") == 0 && i + 1 < argc) {
            auto_input_count = parse_auto_inputs(argc, argv, i + 1,
                                                  auto_inputs, MAX_AUTO_INPUTS);
            printf("Auto-inputs: %d events\n", auto_input_count);
            for (int j = 0; j < auto_input_count; j++)
                printf("  Frame %d: button 0x%02X %s\n",
                       auto_inputs[j].frame, auto_inputs[j].button,
                       auto_inputs[j].is_direction ? "(dir)" : "(btn)");
            i += auto_input_count; /* skip consumed args */
        }
    }

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
    save_state_make_path(frame_ctx.state_path, sizeof(frame_ctx.state_path),
                         config.save_dir, GAME_NAME);
    frame_ctx.frame_count = 0;
    frame_ctx.screenshot_counter = 0;
    frame_ctx.stop_at_frame = stop_at_frame;
    frame_ctx.dump_frame_count = dump_frame_count;
    memcpy(frame_ctx.dump_frames, dump_frames, sizeof(dump_frames));
    frame_ctx.auto_input_count = auto_input_count;
    memcpy(frame_ctx.auto_inputs, auto_inputs, sizeof(auto_inputs));
    frame_ctx.perf_freq = SDL_GetPerformanceFrequency();
    frame_ctx.last_frame_perf = SDL_GetPerformanceCounter();

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
    apu_destroy(&apu);
    window_destroy(&window);
    free(rom);

    printf("Goodbye!\n");
    return 0;
}
