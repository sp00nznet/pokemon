#include "window.h"
#include <SDL.h>
#include <stdio.h>

#define GB_WIDTH  160
#define GB_HEIGHT 144

int window_init(platform_window_t *win, const char *title, int scale) {
    win->scale = scale;
    win->fullscreen = false;
    win->title = title;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return -1;
    }

    win->window = SDL_CreateWindow(
        title,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        GB_WIDTH * scale, GB_HEIGHT * scale,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );
    if (!win->window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return -1;
    }

    win->renderer = SDL_CreateRenderer(win->window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!win->renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return -1;
    }

    /* Maintain aspect ratio */
    SDL_RenderSetLogicalSize(win->renderer, GB_WIDTH, GB_HEIGHT);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");

    win->texture = SDL_CreateTexture(win->renderer,
        SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
        GB_WIDTH, GB_HEIGHT);
    if (!win->texture) {
        fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        return -1;
    }

    return 0;
}

void window_update(platform_window_t *win, const uint32_t *framebuffer,
                   int width, int height) {
    SDL_UpdateTexture(win->texture, NULL, framebuffer, width * sizeof(uint32_t));
    SDL_RenderClear(win->renderer);
    SDL_RenderCopy(win->renderer, win->texture, NULL, NULL);
    SDL_RenderPresent(win->renderer);
}

void window_toggle_fullscreen(platform_window_t *win) {
    win->fullscreen = !win->fullscreen;
    SDL_SetWindowFullscreen(win->window,
        win->fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}

void window_destroy(platform_window_t *win) {
    if (win->texture) SDL_DestroyTexture(win->texture);
    if (win->renderer) SDL_DestroyRenderer(win->renderer);
    if (win->window) SDL_DestroyWindow(win->window);
    SDL_Quit();
}
