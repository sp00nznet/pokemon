/* stubs.c - fallback stubs for functions the analyzer missed */
/* Game-specific stubs for PUSH+JP(HL) trampoline functions.
 * Red/Blue and Yellow have the same patterns at different addresses. */

#include "gbrt.h"
#include <stdio.h>

/* Cross-bank dispatch functions (defined in generated dispatch.c) */
extern void dispatch_call(GBContext *ctx, uint8_t bank, uint16_t addr);
extern void dispatch_jump(GBContext *ctx, uint16_t addr);

/* ========================================================================
 * OAM DMA HRAM routine (same for all games)
 * ======================================================================== */
void func_b00_FF80(GBContext *ctx) {
    uint8_t dma_source = ctx->hram[0xFF81 - 0xFF80];
    if (dma_source == 0) dma_source = 0xC3;
    gb_write8(ctx, 0xFF46, dma_source);
    gb_add_cycles(ctx, 160);
}

/* ========================================================================
 * Bankswitch (farcall) - saves current bank, switches to target, calls,
 * then restores original bank.
 * Red/Blue: 0x35D6, Yellow: 0x3E84
 * Called with: B = target bank, HL = target address
 * ======================================================================== */
static void bankswitch_impl(GBContext *ctx) {
    uint8_t saved_bank = gb_read8(ctx, 0xFFB8);
    uint8_t target_bank = ctx->b;
    uint16_t target_addr = ctx->hl;

    if (target_addr >= 0x8000) {
        fprintf(stderr, "Bankswitch: bad target bank=%02X addr=%04X\n",
                target_bank, target_addr);
        return;
    }

    gb_write8(ctx, 0xFFB8, target_bank);
    gb_write8(ctx, 0x2000, target_bank);

    uint8_t call_bank = (target_addr < 0x4000) ? 0 : target_bank;
    dispatch_call(ctx, call_bank, target_addr);

    gb_write8(ctx, 0xFFB8, saved_bank);
    gb_write8(ctx, 0x2000, saved_bank);
    gb_add_cycles(ctx, 100);
}

#if defined(GAME_YELLOW)
void func_b00_3E84(GBContext *ctx) { bankswitch_impl(ctx); }
#else
void func_b00_35D6(GBContext *ctx) { bankswitch_impl(ctx); }
#endif

/* ========================================================================
 * Predef (predefined function dispatch) - looks up function by index in
 * a table, switches to the target bank, calls it, restores bank.
 * Red/Blue: 0x3E6D, Yellow: 0x3EB4
 * Called with: A = predef index
 * ======================================================================== */
static void predef_impl(GBContext *ctx) {
    uint8_t predef_id = ctx->a;
    gb_write8(ctx, 0xCC4E, predef_id);

    uint8_t saved_bank = gb_read8(ctx, 0xFFB8);
    gb_write8(ctx, 0xCF12, saved_bank);

    /* Save registers as GetPredefPointer does */
    gb_write8(ctx, 0xCC4F, ctx->h);
    gb_write8(ctx, 0xCC50, ctx->l);
    gb_write8(ctx, 0xCC51, ctx->d);
    gb_write8(ctx, 0xCC52, ctx->e);
    gb_write8(ctx, 0xCC53, ctx->b);
    gb_write8(ctx, 0xCC54, ctx->c);

    /* Look up predef table entry (game-specific addresses) */
#if defined(GAME_YELLOW)
    /* Yellow: GetPredefPointer in bank 0x3D, table at 0x681D */
    gb_write8(ctx, 0xFFB8, 0x3D);
    gb_write8(ctx, 0x2000, 0x3D);
    uint16_t table_addr = 0x681D;
#else
    /* Red/Blue: GetPredefPointer in bank 0x13, table at 0x7E79 */
    gb_write8(ctx, 0xFFB8, 0x13);
    gb_write8(ctx, 0x2000, 0x13);
    uint16_t table_addr = gb_read8(ctx, 0x7E5C) |
                          (gb_read8(ctx, 0x7E5D) << 8);
#endif
    uint16_t entry_addr = table_addr + (uint16_t)(predef_id * 3);
    uint8_t target_bank = gb_read8(ctx, entry_addr);
    uint8_t lo = gb_read8(ctx, (uint16_t)(entry_addr + 1));
    uint8_t hi = gb_read8(ctx, (uint16_t)(entry_addr + 2));
    uint16_t target_addr = (uint16_t)((hi << 8) | lo);

    /* Store target bank and set HL */
#if defined(GAME_YELLOW)
    gb_write8(ctx, 0xD0B6, target_bank);
#else
    gb_write8(ctx, 0xD0B7, target_bank);
#endif
    ctx->h = hi;
    ctx->l = lo;

    /* Switch to target bank and call */
    uint8_t call_bank = (target_addr < 0x4000) ? 0 : target_bank;
    /* Debug: uncomment to trace predef calls
    fprintf(stderr, "PREDEF[%02X]: bank=%02X addr=%04X\n",
            predef_id, target_bank, target_addr); */
    gb_write8(ctx, 0xFFB8, target_bank);
    gb_write8(ctx, 0x2000, target_bank);
    dispatch_call(ctx, call_bank, target_addr);

    /* Restore original ROM bank */
    gb_write8(ctx, 0xFFB8, saved_bank);
    gb_write8(ctx, 0x2000, saved_bank);
    gb_add_cycles(ctx, 200);
}

#if defined(GAME_YELLOW)
void func_b00_3EB4(GBContext *ctx) { predef_impl(ctx); }
#else
void func_b00_3E6D(GBContext *ctx) { predef_impl(ctx); }
#endif

/* ========================================================================
 * CallFunctionInTable - index into jump table and call target.
 * Red/Blue: 0x3D97, Yellow: 0x3D93
 * Called with: A = table index, HL = pointer to jump table
 * ======================================================================== */
static void call_function_in_table_impl(GBContext *ctx) {
    uint16_t saved_hl = ctx->hl;
    uint16_t saved_de = ctx->de;
    uint16_t saved_bc = ctx->bc;

    uint16_t table_base = ctx->hl;
    uint8_t index = ctx->a;
    uint16_t entry = table_base + (uint16_t)(index * 2);

    uint8_t lo = gb_read8(ctx, entry);
    uint8_t hi = gb_read8(ctx, (uint16_t)(entry + 1));
    uint16_t target_addr = (uint16_t)((hi << 8) | lo);

    if (target_addr >= 0x4000 && target_addr < 0x8000) {
        dispatch_call(ctx, (uint8_t)ctx->rom_bank, target_addr);
    } else {
        dispatch_jump(ctx, target_addr);
    }

    ctx->bc = saved_bc;
    ctx->de = saved_de;
    ctx->hl = saved_hl;
    gb_add_cycles(ctx, 100);
}

#if defined(GAME_YELLOW)
void func_b00_3D93(GBContext *ctx) { call_function_in_table_impl(ctx); }
#else
void func_b00_3D97(GBContext *ctx) { call_function_in_table_impl(ctx); }
#endif

/* ========================================================================
 * Missing functions - interpreter fallback
 * ======================================================================== */
void func_b00_0011(GBContext *ctx) { gb_interpret(ctx, 0x0011); }
void func_b00_34A3(GBContext *ctx) { gb_interpret(ctx, 0x34A3); }
void func_b00_3F9B(GBContext *ctx) { gb_interpret(ctx, 0x3F9B); }
void func_b00_42B7(GBContext *ctx) { gb_interpret(ctx, 0x42B7); }
void func_b00_5D5F(GBContext *ctx) { gb_interpret(ctx, 0x5D5F); }

/* SRAM/WRAM function calls - handled by interpreter */
void func_b02_A9CB(GBContext *ctx) { gb_interpret(ctx, 0xA9CB); }
void func_b0B_D02F(GBContext *ctx) { gb_interpret(ctx, 0xD02F); }
