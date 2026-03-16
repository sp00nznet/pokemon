/*
 * pokemon_rt.h - Runtime helpers for Pokemon static recompilation
 *
 * Provides sync, interrupt, halt, and frame handling functions
 * that bridge the recompiled code to gb-recompiled's runtime.
 */

#ifndef POKEMON_RT_H
#define POKEMON_RT_H

#include "gbrt.h"
#include "platform_sdl.h"
#include "ppu.h"
#include "pokemon_debug.h"
#include <stdio.h>

/* Global quit flag - set when user closes window */
extern int g_pokemon_quit;

/* Frame counter for auto-input and dump-frames */
extern int g_pokemon_frame_count;

/* Frame callback - called when PPU produces a frame.
 * Implemented in main.c. Returns false if quit requested. */
extern bool pokemon_on_frame(GBContext *ctx);

/* Forward declaration for dispatch (defined in generated dispatch.c) */
extern void dispatch_call(GBContext *ctx, uint8_t bank, uint16_t addr);

/* Check and handle pending interrupts.
 * In the recompiled code model, ISRs are dispatched as C function calls
 * via dispatch_call, so they execute and return inline. */
static inline void pokemon_check_interrupts(GBContext *ctx) {
    if (!ctx->ime) return;
    uint8_t pending = ctx->io[0x0F] & ctx->io[0x80] & 0x1F;
    if (!pending) return;

    uint16_t vec = 0;
    uint8_t bit = 0;
    if      (pending & 0x01) { vec = 0x0040; bit = 0x01; }
    else if (pending & 0x02) { vec = 0x0048; bit = 0x02; }
    else if (pending & 0x04) { vec = 0x0050; bit = 0x04; }
    else if (pending & 0x08) { vec = 0x0058; bit = 0x08; }
    else if (pending & 0x10) { vec = 0x0060; bit = 0x10; }

    if (vec) {
        ctx->ime = 0;
        ctx->halted = 0;
        ctx->io[0x0F] &= ~bit;
        /* ISR dispatch costs 20 T-cycles (5 M-cycles) */
        gb_add_cycles(ctx, 20);
        dispatch_call(ctx, 0, vec);
    }
}

/* Sync hardware at basic block boundaries.
 * Ticks timer/DMA/PPU, checks interrupts, handles frame rendering. */
static inline void pokemon_sync(GBContext *ctx) {
    uint32_t sc = ctx->cycles - ctx->last_sync_cycles;
    if (sc > 0) gb_tick(ctx, sc);
    pokemon_check_interrupts(ctx);
    if (ctx->frame_done) {
        pokemon_on_frame(ctx);
        gb_reset_frame(ctx);
    }
}

/* HALT implementation - loops advancing 4 cycles at a time until
 * an interrupt wakes the CPU. Handles frame boundaries during halt. */
static inline void pokemon_halt(GBContext *ctx) {
    ctx->halted = 1;
    while (ctx->halted) {
        gb_add_cycles(ctx, 4);
        gb_tick(ctx, 4);
        /* Check if any interrupt can wake us (even with IME=0) */
        if (ctx->io[0x0F] & ctx->io[0x80] & 0x1F) {
            ctx->halted = 0;
            pokemon_check_interrupts(ctx);
        }
        if (ctx->frame_done) {
            pokemon_on_frame(ctx);
            gb_reset_frame(ctx);
        }
        if (g_pokemon_quit) return;
    }
}

#endif /* POKEMON_RT_H */
