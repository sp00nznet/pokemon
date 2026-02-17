#include "input.h"
#include "../hal/cpu.h"
#include "../hal/joypad.h"
#include <SDL.h>

static bool fast_forward = false;

void input_init(key_bindings_t *bindings) {
    bindings->key_a = SDL_SCANCODE_Z;
    bindings->key_b = SDL_SCANCODE_X;
    bindings->key_start = SDL_SCANCODE_RETURN;
    bindings->key_select = SDL_SCANCODE_BACKSPACE;
    bindings->key_up = SDL_SCANCODE_UP;
    bindings->key_down = SDL_SCANCODE_DOWN;
    bindings->key_left = SDL_SCANCODE_LEFT;
    bindings->key_right = SDL_SCANCODE_RIGHT;
    bindings->key_fast_forward = SDL_SCANCODE_TAB;
    bindings->key_fullscreen = SDL_SCANCODE_F11;
    bindings->key_mute = SDL_SCANCODE_M;
    bindings->key_save_state = SDL_SCANCODE_F5;
    bindings->key_load_state = SDL_SCANCODE_F9;
}

void input_handle_key(gb_state_t *gb, const key_bindings_t *bindings,
                      int scancode, bool pressed) {
    if (!gb || !gb->joypad) return;

    /* Game buttons */
    if (scancode == bindings->key_a) {
        if (pressed) joypad_press(gb->joypad, gb, BTN_A, false);
        else joypad_release(gb->joypad, BTN_A, false);
    } else if (scancode == bindings->key_b) {
        if (pressed) joypad_press(gb->joypad, gb, BTN_B, false);
        else joypad_release(gb->joypad, BTN_B, false);
    } else if (scancode == bindings->key_start) {
        if (pressed) joypad_press(gb->joypad, gb, BTN_START, false);
        else joypad_release(gb->joypad, BTN_START, false);
    } else if (scancode == bindings->key_select) {
        if (pressed) joypad_press(gb->joypad, gb, BTN_SELECT, false);
        else joypad_release(gb->joypad, BTN_SELECT, false);
    } else if (scancode == bindings->key_up) {
        if (pressed) joypad_press(gb->joypad, gb, BTN_UP, true);
        else joypad_release(gb->joypad, BTN_UP, true);
    } else if (scancode == bindings->key_down) {
        if (pressed) joypad_press(gb->joypad, gb, BTN_DOWN, true);
        else joypad_release(gb->joypad, BTN_DOWN, true);
    } else if (scancode == bindings->key_left) {
        if (pressed) joypad_press(gb->joypad, gb, BTN_LEFT, true);
        else joypad_release(gb->joypad, BTN_LEFT, true);
    } else if (scancode == bindings->key_right) {
        if (pressed) joypad_press(gb->joypad, gb, BTN_RIGHT, true);
        else joypad_release(gb->joypad, BTN_RIGHT, true);
    }

    /* Special keys (only on press) */
    if (pressed) {
        if (scancode == bindings->key_fast_forward) {
            fast_forward = true;
        }
    } else {
        if (scancode == bindings->key_fast_forward) {
            fast_forward = false;
        }
    }
}

bool input_fast_forward_active(void) {
    return fast_forward;
}
