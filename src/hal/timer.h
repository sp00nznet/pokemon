#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>
#include <stdbool.h>

typedef struct gb_state gb_state_t;

typedef struct timer_state {
    uint16_t div_counter;   /* Internal 16-bit counter (DIV = high byte) */
    uint8_t  tima;          /* Timer counter */
    uint8_t  tma;           /* Timer modulo */
    uint8_t  tac;           /* Timer control */
    bool     tima_overflow; /* TIMA overflowed, pending reload */
    uint8_t  overflow_cycles; /* Cycles until TIMA reload */
} timer_state_t;

void timer_init(timer_state_t *timer);
void timer_tick(timer_state_t *timer, gb_state_t *gb, uint32_t cycles);
uint8_t timer_read_div(const timer_state_t *timer);
void timer_write_div(timer_state_t *timer);
void timer_write_tac(timer_state_t *timer, uint8_t val);

#endif /* TIMER_H */
