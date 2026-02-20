#include "timer.h"
#include "cpu.h"
#include "memory.h"

/* TAC frequency bit positions in the 16-bit DIV counter */
static const int tac_bit_pos[4] = {9, 3, 5, 7}; /* 4096, 262144, 65536, 16384 Hz */

void timer_init(timer_state_t *timer) {
    timer->div_counter = 0xAB00; /* Post-boot value (DIV = 0xAB) */
    timer->tima = 0;
    timer->tma = 0;
    timer->tac = 0;
    timer->tima_overflow = false;
    timer->overflow_cycles = 0;
}

void timer_tick(timer_state_t *timer, gb_state_t *gb, uint32_t cycles) {
    for (uint32_t i = 0; i < cycles; i++) {
        /* Handle TIMA overflow reload (4-cycle delay) */
        if (timer->tima_overflow) {
            timer->overflow_cycles--;
            if (timer->overflow_cycles == 0) {
                timer->tima = timer->tma;
                timer->tima_overflow = false;
                /* Request timer interrupt */
                gb->mem->io[0x0F] |= 0x04; /* IF bit 2 */
            }
        }

        /* Get old falling edge detector bit */
        bool tac_enabled = (timer->tac & 0x04) != 0;
        int bit_pos = tac_bit_pos[timer->tac & 0x03];
        bool old_bit = tac_enabled && ((timer->div_counter >> bit_pos) & 1);

        /* Increment DIV counter */
        timer->div_counter++;

        /* Check for falling edge */
        bool new_bit = tac_enabled && ((timer->div_counter >> bit_pos) & 1);

        if (old_bit && !new_bit) {
            /* TIMA increment */
            timer->tima++;
            if (timer->tima == 0) {
                /* Overflow - schedule reload after 4 cycles */
                timer->tima_overflow = true;
                timer->overflow_cycles = 4;
            }
        }
    }
}

uint8_t timer_read_div(const timer_state_t *timer) {
    return (uint8_t)(timer->div_counter >> 8);
}

/* Shared glitch helper: if the selected bit goes from 1→0,
 * TIMA gets an extra increment (hardware falling-edge glitch). */
static void timer_check_glitch(timer_state_t *timer, bool old_bit) {
    bool tac_enabled = (timer->tac & 0x04) != 0;
    int bit_pos = tac_bit_pos[timer->tac & 0x03];
    bool new_bit = tac_enabled && ((timer->div_counter >> bit_pos) & 1);
    if (old_bit && !new_bit) {
        timer->tima++;
        if (timer->tima == 0) {
            timer->tima_overflow = true;
            timer->overflow_cycles = 4;
        }
    }
}

void timer_write_div(timer_state_t *timer) {
    /* Detect falling edge before reset (DIV write glitch) */
    bool tac_enabled = (timer->tac & 0x04) != 0;
    int bit_pos = tac_bit_pos[timer->tac & 0x03];
    bool old_bit = tac_enabled && ((timer->div_counter >> bit_pos) & 1);

    /* Writing any value resets the entire counter */
    timer->div_counter = 0;

    /* Abort pending TIMA overflow reload (hardware behavior) */
    if (timer->tima_overflow) {
        timer->tima_overflow = false;
    }

    /* If the selected bit was 1 and is now 0, TIMA increments */
    timer_check_glitch(timer, old_bit);
}

void timer_write_tac(timer_state_t *timer, uint8_t val) {
    /* Detect falling edge before TAC change (TAC write glitch) */
    bool old_enabled = (timer->tac & 0x04) != 0;
    int old_bit_pos = tac_bit_pos[timer->tac & 0x03];
    bool old_bit = old_enabled && ((timer->div_counter >> old_bit_pos) & 1);

    timer->tac = val & 0x07;

    /* If the selected bit went from 1→0 due to TAC change, TIMA increments */
    timer_check_glitch(timer, old_bit);
}
