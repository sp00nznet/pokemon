#ifndef INPUT_H
#define INPUT_H

#include <stdint.h>
#include <stdbool.h>

typedef struct gb_state gb_state_t;

/* Key binding configuration */
typedef struct {
    int key_a;
    int key_b;
    int key_start;
    int key_select;
    int key_up;
    int key_down;
    int key_left;
    int key_right;
    int key_fast_forward;
    int key_fullscreen;
    int key_mute;
    int key_save_state;
    int key_load_state;
    int key_screenshot;
} key_bindings_t;

/* Initialize input with default key bindings */
void input_init(key_bindings_t *bindings);

/* Process a single SDL key event */
void input_handle_key(gb_state_t *gb, const key_bindings_t *bindings,
                      int scancode, bool pressed);

/* Check if fast-forward is active */
bool input_fast_forward_active(void);

#endif /* INPUT_H */
