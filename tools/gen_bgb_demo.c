/*
 * gen_bgb_demo.c - Generate BGB demo files for reference frame capture
 *
 * BGB demo format: 1 byte per frame, joypad bits:
 *   Low nibble:  bit0=A, bit1=B, bit2=Select, bit3=Start
 *   High nibble: bit4=Right, bit5=Left, bit6=Up, bit7=Down
 *
 * Usage: gen_bgb_demo <output.dem> <total_frames> [frame:button ...]
 *   button names: a, b, start, select, up, down, left, right
 *   Example: gen_bgb_demo test.dem 900 420:start 620:a
 *
 * This generates a demo that presses Start at frame 420 and A at frame 620,
 * running for 900 total frames. Each button press lasts 10 frames.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define PRESS_DURATION 10  /* frames to hold a button */

typedef struct {
    int frame;
    uint8_t button_bit;
} input_event_t;

static uint8_t parse_button(const char *name) {
    if (strcmp(name, "a") == 0)      return 0x01;
    if (strcmp(name, "b") == 0)      return 0x02;
    if (strcmp(name, "select") == 0) return 0x04;
    if (strcmp(name, "start") == 0)  return 0x08;
    if (strcmp(name, "right") == 0)  return 0x10;
    if (strcmp(name, "left") == 0)   return 0x20;
    if (strcmp(name, "up") == 0)     return 0x40;
    if (strcmp(name, "down") == 0)   return 0x80;
    fprintf(stderr, "Unknown button: %s\n", name);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <output.dem> <total_frames> [frame:button ...]\n", argv[0]);
        fprintf(stderr, "  Buttons: a, b, start, select, up, down, left, right\n");
        fprintf(stderr, "  Example: %s ref.dem 900 420:start 620:a\n", argv[0]);
        return 1;
    }

    const char *output = argv[1];
    int total_frames = atoi(argv[2]);
    if (total_frames <= 0) {
        fprintf(stderr, "Invalid frame count: %s\n", argv[2]);
        return 1;
    }

    /* Parse input events */
    input_event_t events[256];
    int event_count = 0;

    for (int i = 3; i < argc && event_count < 256; i++) {
        char *colon = strchr(argv[i], ':');
        if (!colon) {
            fprintf(stderr, "Invalid event format: %s (expected frame:button)\n", argv[i]);
            return 1;
        }
        *colon = '\0';
        events[event_count].frame = atoi(argv[i]);
        events[event_count].button_bit = parse_button(colon + 1);
        if (events[event_count].button_bit == 0) return 1;
        printf("  Frame %d: press %s (0x%02X)\n", events[event_count].frame,
               colon + 1, events[event_count].button_bit);
        event_count++;
    }

    /* Generate demo data */
    uint8_t *demo = (uint8_t *)calloc(total_frames, 1);
    if (!demo) { fprintf(stderr, "Out of memory\n"); return 1; }

    for (int e = 0; e < event_count; e++) {
        int start = events[e].frame;
        int end = start + PRESS_DURATION;
        if (end > total_frames) end = total_frames;
        for (int f = start; f < end; f++) {
            demo[f] |= events[e].button_bit;
        }
    }

    /* Write demo file */
    FILE *f = fopen(output, "wb");
    if (!f) {
        fprintf(stderr, "Cannot open %s for writing\n", output);
        free(demo);
        return 1;
    }
    fwrite(demo, 1, total_frames, f);
    fclose(f);
    free(demo);

    printf("Generated %s (%d frames)\n", output, total_frames);
    return 0;
}
