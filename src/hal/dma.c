#include "dma.h"
#include "cpu.h"
#include "memory.h"
#include <string.h>

void dma_start(gb_state_t *gb, uint8_t source_high) {
    /* OAM DMA: copy 160 bytes from source_high*0x100 to OAM (0xFE00) */
    uint16_t source = (uint16_t)source_high << 8;

    for (int i = 0; i < 160; i++) {
        uint8_t val = mem_read8(gb, source + i);
        gb->mem->oam[i] = val;
    }

    /* DMA takes 160 M-cycles (640 T-cycles) */
    gb->cycles += 640;
}
