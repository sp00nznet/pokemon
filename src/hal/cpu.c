#include "cpu.h"
#include "interrupts.h"
#include "timer.h"
#include "ppu.h"
#include "apu.h"
#include "serial.h"
#include <stdio.h>

/* Dispatch into generated interrupt handler code */
extern void dispatch_call(gb_state_t *gb, uint8_t bank, uint16_t addr);

void cpu_init(gb_state_t *gb) {
    /* Post-boot ROM register values (DMG) */
    gb->a = 0x01;
    gb->f_z = 1; gb->f_n = 0; gb->f_h = 1; gb->f_c = 1;
    gb->b = 0x00; gb->c = 0x13;
    gb->d = 0x00; gb->e = 0xD8;
    gb->h = 0x01; gb->l = 0x4D;
    gb->sp = 0xFFFE;
    gb->pc = 0x0150;

    gb->ime = 1;
    gb->ime_pending = 0;
    gb->halted = 0;
    gb->cycles = 0;
    gb->target_cycles = 0;
    gb->sync_cycles = 0;
    gb->running = true;

    gb->debug_cpu = false;
    gb->debug_mem = false;
    gb->debug_ppu = false;
}

void cpu_check_interrupts(gb_state_t *gb) {
    /* EI delay: promote ime_pending to ime, but don't service interrupts
     * this check.  The instruction after EI has already executed by the
     * time we reach this point; however, giving one full check-cycle of
     * delay matches the hardware behaviour in the cooperative model. */
    if (gb->ime_pending) {
        gb->ime = 1;
        gb->ime_pending = 0;
        /* Still need to un-halt if pending, but don't service yet */
        if (!gb->halted) return;
    }

    if (!gb->ime && !gb->halted) return;

    uint8_t ie = gb->mem->ie_reg;
    uint8_t iflag = gb->mem->io[0x0F]; /* IF register */
    uint8_t pending = ie & iflag;

    if (pending == 0) return;

    /* Un-halt even if IME is off */
    gb->halted = 0;

    if (!gb->ime) return;

    /* Service highest priority interrupt */
    gb->ime = 0;

    for (int bit = 0; bit < 5; bit++) {
        if (pending & (1 << bit)) {
            /* Clear the interrupt flag */
            gb->mem->io[0x0F] &= ~(1 << bit);

            /* In the static recompiler, we dispatch to the interrupt handler
             * as a C function call instead of setting PC. The handler's RET
             * maps to a C return, bringing us back here. */
            uint16_t isr_addr = 0x0040 + (uint16_t)(bit * 8);
            gb->cycles += 20;

            dispatch_call(gb, 0, isr_addr);
            return;
        }
    }
}

void cpu_halt(gb_state_t *gb) {
    /* EI followed by HALT: HALT counts as the "next instruction" after EI,
     * so promote ime_pending to ime before entering the halt loop. */
    if (gb->ime_pending) {
        gb->ime = 1;
        gb->ime_pending = 0;
    }

    gb->halted = 1;

    /* Advance cycles until an interrupt is pending */
    while (gb->halted && gb->running) {
        /* Advance 4 cycles at a time */
        gb->cycles += 4;
        gb->sync_cycles = gb->cycles;
        hal_sync(gb, 4);

        /* Check if any interrupt can wake us */
        uint8_t ie = gb->mem->ie_reg;
        uint8_t iflag = gb->mem->io[0x0F];
        if (ie & iflag) {
            gb->halted = 0;
        }
    }

    /* Dispatch the pending interrupt BEFORE returning to generated code.
     * This ensures the ISR runs (e.g., sets hVBlankOccurred) before the
     * code after HALT checks for it. */
    cpu_check_interrupts(gb);
}

void hal_halt(gb_state_t *gb) {
    cpu_halt(gb);
}

void hal_stop(gb_state_t *gb) {
    /* STOP instruction - on DMG this halts until a button press.
     * On CGB it can trigger speed switch. For now, treat like HALT. */
    cpu_halt(gb);
}

void hal_invalid(gb_state_t *gb, uint16_t addr, uint8_t opcode) {
    (void)gb;
    fprintf(stderr, "WARNING: Invalid opcode 0x%02X at address 0x%04X\n", opcode, addr);
}

void hal_sync(gb_state_t *gb, uint32_t cycles) {
    /* Update timer */
    if (gb->timer) {
        timer_tick(gb->timer, gb, cycles);
    }

    /* Update PPU */
    if (gb->ppu) {
        ppu_tick(gb->ppu, gb, cycles);

        /* When a frame is ready, call the frame callback for rendering + events */
        if (gb->ppu->frame_ready && gb->frame_callback) {
            gb->frame_callback(gb, gb->frame_userdata);
            gb->ppu->frame_ready = false;
        }
    }

    /* Update APU */
    if (gb->apu) {
        apu_tick(gb->apu, gb, cycles);
    }

    /* Update serial transfer */
    serial_tick(gb, cycles);

    /* Check and dispatch pending interrupts.
     * Skip if we're inside a HALT loop (HALT handles its own dispatch). */
    if (!gb->halted) {
        cpu_check_interrupts(gb);
    }
}
