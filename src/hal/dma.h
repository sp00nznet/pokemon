#ifndef DMA_H
#define DMA_H

#include <stdint.h>

typedef struct gb_state gb_state_t;

/* Start OAM DMA transfer (triggered by write to 0xFF46) */
void dma_start(gb_state_t *gb, uint8_t source_high);

#endif /* DMA_H */
