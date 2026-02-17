#include "joypad.h"
#include "cpu.h"
#include "memory.h"
#include "interrupts.h"

void joypad_init(joypad_state_t *joy) {
    joy->p1_select = 0x30;  /* Both groups deselected */
    joy->directions = 0x0F; /* All released (active low) */
    joy->buttons = 0x0F;
}

uint8_t joypad_read(const joypad_state_t *joy) {
    uint8_t result = 0xCF; /* Upper 2 bits always 1, lower nibble all 1 */

    if (!(joy->p1_select & 0x10)) {
        /* Direction keys selected */
        result = (result & 0xF0) | (joy->directions & 0x0F);
    }
    if (!(joy->p1_select & 0x20)) {
        /* Button keys selected */
        result = (result & 0xF0) | (joy->buttons & 0x0F);
    }

    result = (joy->p1_select & 0x30) | (result & 0x0F) | 0xC0;
    return result;
}

void joypad_write(joypad_state_t *joy, uint8_t val) {
    joy->p1_select = val & 0x30;
}

void joypad_press(joypad_state_t *joy, gb_state_t *gb, uint8_t button, bool is_direction) {
    if (is_direction) {
        joy->directions &= ~button; /* Active low */
    } else {
        joy->buttons &= ~button;
    }
    /* Request joypad interrupt */
    if (gb) {
        interrupt_request(gb, INT_JOYPAD_BIT);
    }
}

void joypad_release(joypad_state_t *joy, uint8_t button, bool is_direction) {
    if (is_direction) {
        joy->directions |= button;
    } else {
        joy->buttons |= button;
    }
}
