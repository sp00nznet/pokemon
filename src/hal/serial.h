#ifndef SERIAL_H
#define SERIAL_H

#include <stdint.h>

typedef struct gb_state gb_state_t;

/* Serial transfer stub - no actual link cable support */
void serial_tick(gb_state_t *gb, uint32_t cycles);

#endif /* SERIAL_H */
