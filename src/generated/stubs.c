/* stubs.c - fallback stubs for functions the analyzer missed */
/* These are functions that exist in the ROM but weren't traced by
 * the recursive descent analyzer. They execute the ROM data
 * as an interpreter fallback. */

#include "hal/cpu.h"
#include "hal/memory.h"
#include <stdio.h>

/* Cross-bank dispatch functions (defined in generated dispatch.c) */
extern void dispatch_call(gb_state_t *gb, uint8_t bank, uint16_t addr);
extern void dispatch_jump(gb_state_t *gb, uint16_t addr);

/* GetPredefPointer (in bank 0x13, called by Predef stub) */
extern void func_b00_7E49(gb_state_t *gb);

/* Interpreter fallback: execute SM83 code byte-by-byte from ROM.
 * This is slow but handles functions the static analyzer missed. */
static void interpret_fallback(gb_state_t *gb, uint16_t start_addr) {
    fprintf(stderr, "STUB: fallback interpreter at 0x%04X (not yet implemented)\n", start_addr);
    fflush(stderr);
    /* For now, just return. A full interpreter would be needed for
     * complete correctness, but Pokemon may not hit these paths. */
}

/* func_b00_FF80: HRAM routine (used by OAM DMA wait loop) */
void func_b00_FF80(gb_state_t *gb) {
    /* This is typically the OAM DMA wait loop that games copy to HRAM.
     * The standard routine:
     *   FF80: LD A, source_high    (opcode 0x3E, operand at FF81)
     *   FF82: LDH (DMA), A        ; start DMA
     *   FF84: LD A, 0x28          ; wait 40 iterations
     *   FF86: DEC A
     *   FF87: JR NZ, FF86
     *   FF89: RET
     *
     * The DMA source high byte is the immediate operand of "LD A, n"
     * stored at HRAM address 0xFF81 when the game copies this routine
     * to HRAM during initialization. Read it from there. */
    uint8_t dma_source = gb->mem->hram[0xFF81 - 0xFF80];
    if (dma_source == 0) dma_source = 0xC3; /* Default for Pokemon Red */
    /* Trigger OAM DMA: copy 160 bytes from (dma_source << 8) to OAM */
    mem_write8(gb, 0xFF46, dma_source);
    gb->cycles += 160; /* DMA takes ~160 M-cycles */
}

/* ========================================================================
 * Hand-written stubs for PUSH return_addr + JP (HL) trampoline functions.
 *
 * In the real Game Boy, these use PUSH + JP (HL) to simulate a CALL with
 * cleanup code at the pushed return address. In a static recompiler where
 * CALL = C function call and RET = C return, the cleanup code never
 * executes. These stubs implement the full behavior correctly.
 * ======================================================================== */

/* func_b00_35D6: Bankswitch (farcall mechanism)
 *
 * Original code at 0x35D6:
 *   LDH  A, (hLoadedROMBank)  ; save current bank
 *   PUSH AF
 *   LD   A, B                 ; B = target bank
 *   LDH  (hLoadedROMBank), A
 *   LD   (MBC1RomBank), A     ; switch ROM bank
 *   LD   BC, .Return
 *   PUSH BC                   ; push cleanup address
 *   JP   (HL)                 ; call target
 * .Return (0x35E4):
 *   POP  AF                   ; restore saved bank
 *   LDH  (hLoadedROMBank), A
 *   LD   (MBC1RomBank), A     ; restore ROM bank
 *   RET
 *
 * Called with: B = target bank, HL = target address
 */
void func_b00_35D6(gb_state_t *gb) {
    /* Save current ROM bank */
    uint8_t saved_bank = mem_read8(gb, 0xFFB8);

    /* Switch to target bank */
    uint8_t target_bank = gb->b;
    uint16_t target_addr = REG_HL(gb);

    /* Validate target address is in ROM space */
    if (target_addr >= 0x8000) {
        fprintf(stderr, "Bankswitch: bad target bank=%02X addr=%04X (saved_bank=%02X)\n",
                target_bank, target_addr, saved_bank);
        return;
    }

    mem_write8(gb, 0xFFB8, target_bank);
    mem_write8(gb, 0x2000, target_bank);

    /* For addresses in always-mapped bank 0 (0x0000-0x3FFF), route to bank 0 */
    uint8_t call_bank = (target_addr < 0x4000) ? 0 : target_bank;

    /* Call target function */
    dispatch_call(gb, call_bank, target_addr);

    /* Restore original ROM bank (cleanup code at 0x35E4) */
    mem_write8(gb, 0xFFB8, saved_bank);
    mem_write8(gb, 0x2000, saved_bank);

    gb->cycles += 100;
}

/* func_b00_3E6D: Predef (predefined function dispatch)
 *
 * Original code at 0x3E6D:
 *   LD   (wPredefID), A       ; store predef index
 *   LDH  A, (hLoadedROMBank)  ; save current bank
 *   LD   (wPredefParentBank), A
 *   PUSH AF
 *   LD   A, BANK(GetPredefPointer)  ; bank 0x13
 *   LDH  (hLoadedROMBank), A
 *   LD   (MBC1RomBank), A
 *   CALL GetPredefPointer     ; sets HL=target, bank in wPredefBank
 *   LD   A, (wPredefBank)
 *   LDH  (hLoadedROMBank), A
 *   LD   (MBC1RomBank), A
 *   LD   DE, .Return
 *   PUSH DE
 *   JP   (HL)                 ; call predef target
 * .Return (0x3E8D):
 *   POP  AF                   ; restore saved bank
 *   LDH  (hLoadedROMBank), A
 *   LD   (MBC1RomBank), A
 *   RET
 *
 * Called with: A = predef index
 */
void func_b00_3E6D(gb_state_t *gb) {
    /* Store predef index */
    uint8_t predef_id = gb->a;
    mem_write8(gb, 0xCC4E, predef_id);

    /* Save current ROM bank */
    uint8_t saved_bank = mem_read8(gb, 0xFFB8);
    mem_write8(gb, 0xCF12, saved_bank);

    /* Switch to bank 0x13 for GetPredefPointer */
    mem_write8(gb, 0xFFB8, 0x13);
    mem_write8(gb, 0x2000, 0x13);

    /* Inline GetPredefPointer logic (0x7E49 in bank 0x13).
     * The function saves registers, then reads PredefPointers table:
     *   0x7E5B: LD HL, $7E79  (table base address)
     *   Table entries are 3 bytes each: [bank, addr_lo, addr_hi].
     * After switching to bank 0x13, mem_read8 reads from bank 0x13 ROM. */

    /* Save registers as GetPredefPointer does (to wram CC4F-CC54) */
    mem_write8(gb, 0xCC4F, gb->h);
    mem_write8(gb, 0xCC50, gb->l);
    mem_write8(gb, 0xCC51, gb->d);
    mem_write8(gb, 0xCC52, gb->e);
    mem_write8(gb, 0xCC53, gb->b);
    mem_write8(gb, 0xCC54, gb->c);

    /* Read table base from LD HL instruction at 0x7E5B: operand at 0x7E5C/0x7E5D */
    uint16_t table_addr = mem_read8(gb, 0x7E5C) |
                          (mem_read8(gb, 0x7E5D) << 8);
    uint16_t entry_addr = table_addr + (uint16_t)(predef_id * 3);
    uint8_t target_bank = mem_read8(gb, entry_addr);
    uint8_t lo = mem_read8(gb, (uint16_t)(entry_addr + 1));
    uint8_t hi = mem_read8(gb, (uint16_t)(entry_addr + 2));
    uint16_t target_addr = (uint16_t)((hi << 8) | lo);

    /* Store target bank in wPredefBank */
    mem_write8(gb, 0xD0B7, target_bank);

    /* Set HL to target address (callers may inspect it after return) */
    gb->h = hi;
    gb->l = lo;

    /* For addresses in always-mapped bank 0 (0x0000-0x3FFF), route to bank 0 */
    uint8_t call_bank = target_bank;
    if (target_addr < 0x4000) {
        call_bank = 0;
    }

    /* Switch to target bank and call it */
    mem_write8(gb, 0xFFB8, target_bank);
    mem_write8(gb, 0x2000, target_bank);

    dispatch_call(gb, call_bank, target_addr);

    /* Restore original ROM bank (cleanup code at 0x3E8D) */
    mem_write8(gb, 0xFFB8, saved_bank);
    mem_write8(gb, 0x2000, saved_bank);

    gb->cycles += 200;
}

/* func_b00_3D97: CallFunctionInTable (jump table dispatch)
 *
 * Original code at 0x3D97:
 *   PUSH HL                   ; save table base
 *   PUSH DE                   ; save DE
 *   PUSH BC                   ; save BC
 *   ADD  A                    ; A = index * 2
 *   LD   D, 0
 *   LD   E, A
 *   ADD  HL, DE               ; HL = table_base + index*2
 *   LD   A, (HL+)             ; read low byte of target
 *   LD   H, (HL)              ; read high byte of target
 *   LD   L, A                 ; HL = target address
 *   LD   DE, .Return
 *   PUSH DE
 *   JP   (HL)                 ; call target
 * .Return (0x3DA7):
 *   POP  BC                   ; restore BC
 *   POP  DE                   ; restore DE
 *   POP  HL                   ; restore HL
 *   RET
 *
 * Called with: A = table index, HL = pointer to jump table
 */
void func_b00_3D97(gb_state_t *gb) {
    /* Save registers */
    uint16_t saved_hl = REG_HL(gb);
    uint16_t saved_de = REG_DE(gb);
    uint16_t saved_bc = REG_BC(gb);

    /* Index into table: each entry is 2 bytes */
    uint16_t table_base = REG_HL(gb);
    uint8_t index = gb->a;
    uint16_t entry_addr = table_base + (uint16_t)(index * 2);

    /* Read 16-bit function pointer from table */
    uint8_t lo = mem_read8(gb, entry_addr);
    uint8_t hi = mem_read8(gb, (uint16_t)(entry_addr + 1));
    uint16_t target_addr = (uint16_t)((hi << 8) | lo);

    /* Call target function */
    if (target_addr >= 0x4000 && target_addr < 0x8000) {
        dispatch_call(gb, (uint8_t)gb->mem->rom_bank, target_addr);
    } else {
        dispatch_jump(gb, target_addr);
    }

    /* Restore registers (cleanup code at 0x3DA7) */
    SET_REG_BC(gb, saved_bc);
    SET_REG_DE(gb, saved_de);
    SET_REG_HL(gb, saved_hl);

    gb->cycles += 100;
}

/* ========================================================================
 * Missing bank 0 functions - interpreted fallbacks
 * ======================================================================== */
void func_b00_0011(gb_state_t *gb) { interpret_fallback(gb, 0x0011); }
/* func_b00_1F49: SoftReset - now generated by recompiler in bank_00.c */
/* func_b00_3071: now generated by recompiler in bank_00.c */
void func_b00_34A3(gb_state_t *gb) { interpret_fallback(gb, 0x34A3); }
/* func_b00_3C3C: now generated by recompiler in bank_00.c */
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
