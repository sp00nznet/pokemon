#ifndef MENU_H
#define MENU_H

#include <SDL.h>
#include "window.h"

/* Menu item IDs */
#define IDM_SAVE_STATE  1001
#define IDM_LOAD_STATE  1002
#define IDM_EXIT        1003
#define IDM_SCALE_1X    1010
#define IDM_SCALE_2X    1011
#define IDM_SCALE_3X    1012
#define IDM_SCALE_4X    1013

/* Initialize native menu bar and attach to SDL window.
 * current_scale: 1-4, used to set initial radio check. */
void menu_init(platform_window_t *win, int current_scale);

/* Process an SDL event for menu commands.
 * Returns a menu item ID (IDM_*) if a menu command was received, 0 otherwise. */
int menu_handle_event(const SDL_Event *event);

/* Update the scale radio-check marks after a scale change. */
void menu_update_scale_check(int new_scale);

#endif /* MENU_H */
