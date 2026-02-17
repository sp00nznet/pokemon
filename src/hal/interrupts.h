#ifndef INTERRUPTS_H
#define INTERRUPTS_H

#include <stdint.h>

/* Interrupt bit definitions */
#define INT_VBLANK_BIT   0x01
#define INT_STAT_BIT     0x02
#define INT_TIMER_BIT    0x04
#define INT_SERIAL_BIT   0x08
#define INT_JOYPAD_BIT   0x10

/* Interrupt vector addresses */
#define INT_VBLANK_VEC   0x0040
#define INT_STAT_VEC     0x0048
#define INT_TIMER_VEC    0x0050
#define INT_SERIAL_VEC   0x0058
#define INT_JOYPAD_VEC   0x0060

typedef struct gb_state gb_state_t;

/* Request an interrupt (set IF bit) */
void interrupt_request(gb_state_t *gb, uint8_t interrupt_bit);

/* Check and dispatch pending interrupts */
void interrupt_dispatch(gb_state_t *gb);

#endif /* INTERRUPTS_H */
