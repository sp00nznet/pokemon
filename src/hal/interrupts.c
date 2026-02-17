#include "interrupts.h"
#include "cpu.h"
#include "memory.h"

void interrupt_request(gb_state_t *gb, uint8_t interrupt_bit) {
    gb->mem->io[0x0F] |= interrupt_bit;
}

void interrupt_dispatch(gb_state_t *gb) {
    cpu_check_interrupts(gb);
}
