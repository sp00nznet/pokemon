#ifndef WINDOW_H
#define WINDOW_H

#include <stdint.h>
#include <stdbool.h>

/* SDL2 forward declarations */
struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;

typedef struct {
    struct SDL_Window *window;
    struct SDL_Renderer *renderer;
    struct SDL_Texture *texture;
    int scale;
    bool fullscreen;
    const char *title;
} platform_window_t;

/* Initialize SDL2 and create window */
int window_init(platform_window_t *win, const char *title, int scale);

/* Update window with framebuffer contents */
void window_update(platform_window_t *win, const uint32_t *framebuffer,
                   int width, int height);

/* Toggle fullscreen */
void window_toggle_fullscreen(platform_window_t *win);

/* Cleanup */
void window_destroy(platform_window_t *win);

#endif /* WINDOW_H */
