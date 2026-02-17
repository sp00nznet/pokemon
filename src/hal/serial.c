#include "serial.h"
#include "cpu.h"
#include "memory.h"
#include "interrupts.h"

/* Serial transfer stub - completes transfers immediately with 0xFF */
static uint32_t serial_cycles = 0;
static bool serial_active = false;

void serial_tick(gb_state_t *gb, uint32_t cycles) {
    if (!serial_active) {
        /* Check if a transfer was initiated */
        uint8_t sc = gb->mem->io[0x02];
        if ((sc & 0x81) == 0x81) { /* Transfer start + internal clock */
            serial_active = true;
            serial_cycles = 0;
        }
        return;
    }

    serial_cycles += cycles;

    /* Complete after ~8 bit periods at 8192 Hz (~512 cycles per bit, 4096 total) */
    if (serial_cycles >= 4096) {
        serial_active = false;
        gb->mem->io[0x01] = 0xFF;           /* SB: no connected device */
        gb->mem->io[0x02] &= ~0x80;         /* SC: clear transfer bit */
        interrupt_request(gb, INT_SERIAL_BIT); /* Request serial interrupt */
    }
}
