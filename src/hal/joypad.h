#ifndef JOYPAD_H
#define JOYPAD_H

#include <stdint.h>
#include <stdbool.h>

typedef struct gb_state gb_state_t;

/* Button bit masks (active low) */
#define BTN_RIGHT   0x01
#define BTN_LEFT    0x02
#define BTN_UP      0x04
#define BTN_DOWN    0x08
#define BTN_A       0x01
#define BTN_B       0x02
#define BTN_SELECT  0x04
#define BTN_START   0x08

typedef struct joypad_state {
    uint8_t p1_select;     /* P1 register selection bits (bits 4-5) */
    uint8_t directions;    /* Direction button state (active low nibble) */
    uint8_t buttons;       /* Action button state (active low nibble) */
} joypad_state_t;

void joypad_init(joypad_state_t *joy);
uint8_t joypad_read(const joypad_state_t *joy);
void joypad_write(joypad_state_t *joy, uint8_t val);

/* Press/release a button */
void joypad_press(joypad_state_t *joy, gb_state_t *gb, uint8_t button, bool is_direction);
void joypad_release(joypad_state_t *joy, uint8_t button, bool is_direction);

#endif /* JOYPAD_H */
