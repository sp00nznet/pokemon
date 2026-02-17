/* stubs.c - fallback stubs for functions the analyzer missed */
/* These are functions that exist in the ROM but weren't traced by
 * the recursive descent analyzer. They execute the ROM data
 * as an interpreter fallback. */

#include "hal/cpu.h"
#include "hal/memory.h"
#include <stdio.h>

/* Interpreter fallback: execute SM83 code byte-by-byte from ROM.
 * This is slow but handles functions the static analyzer missed. */
static void interpret_fallback(gb_state_t *gb, uint16_t start_addr) {
    fprintf(stderr, "STUB: fallback interpreter at 0x%04X (not yet implemented)\n", start_addr);
    /* For now, just return. A full interpreter would be needed for
     * complete correctness, but Pokemon may not hit these paths. */
}

/* func_b00_FF80: HRAM routine (used by OAM DMA wait loop) */
void func_b00_FF80(gb_state_t *gb) {
    /* This is typically the OAM DMA wait loop that games copy to HRAM.
     * The standard routine:
     *   FF80: LD A, source_high
     *   FF82: LDH (DMA), A     ; start DMA
     *   FF84: LD A, 0x28       ; wait 40 iterations
     *   FF86: DEC A
     *   FF87: JR NZ, FF86
     *   FF89: RET
     * We emulate it by just triggering a DMA from the stored source. */
    uint8_t dma_source = mem_read8(gb, 0xFF46);
    if (dma_source == 0) dma_source = 0xC0; /* Default: copy from WRAM */
    /* Trigger OAM DMA */
    mem_write8(gb, 0xFF46, dma_source);
    gb->cycles += 160; /* DMA takes ~160 M-cycles */
}

/* Missing bank 0 functions - interpreted fallbacks */
void func_b00_0011(gb_state_t *gb) { interpret_fallback(gb, 0x0011); }
void func_b00_1F49(gb_state_t *gb) { interpret_fallback(gb, 0x1F49); }
void func_b00_3071(gb_state_t *gb) { interpret_fallback(gb, 0x3071); }
void func_b00_34A3(gb_state_t *gb) { interpret_fallback(gb, 0x34A3); }
void func_b00_3C3C(gb_state_t *gb) { interpret_fallback(gb, 0x3C3C); }
void func_b00_3F9B(gb_state_t *gb) { interpret_fallback(gb, 0x3F9B); }
void func_b00_42B7(gb_state_t *gb) { interpret_fallback(gb, 0x42B7); }
void func_b00_5D5F(gb_state_t *gb) { interpret_fallback(gb, 0x5D5F); }

/* SRAM/WRAM function calls - code copied to RAM at runtime */
void func_b02_A9CB(gb_state_t *gb) {
    fprintf(stderr, "STUB: SRAM function at 0xA9CB (bank 2 context)\n");
    (void)gb;
}
void func_b0B_D02F(gb_state_t *gb) {
    fprintf(stderr, "STUB: WRAM function at 0xD02F (bank 0x0B context)\n");
    (void)gb;
}
