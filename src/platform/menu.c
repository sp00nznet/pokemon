#include "menu.h"

#ifdef _WIN32

#include <SDL.h>
#include <SDL_syswm.h>
#include <windows.h>

static HMENU s_menu_bar = NULL;
static HMENU s_config_menu = NULL;

void menu_init(platform_window_t *win, int current_scale) {
    SDL_SysWMinfo wminfo;
    SDL_VERSION(&wminfo.version);
    if (!SDL_GetWindowWMInfo(win->window, &wminfo))
        return;

    HWND hwnd = wminfo.info.win.window;

    /* Enable SDL_SYSWMEVENT so we receive WM_COMMAND */
    SDL_EventState(SDL_SYSWMEVENT, SDL_ENABLE);

    /* Build menu bar */
    s_menu_bar = CreateMenu();

    /* File menu */
    HMENU file_menu = CreatePopupMenu();
    AppendMenuA(file_menu, MF_STRING, IDM_SAVE_STATE, "Save State\tF5");
    AppendMenuA(file_menu, MF_STRING, IDM_LOAD_STATE, "Load State\tF9");
    AppendMenuA(file_menu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(file_menu, MF_STRING, IDM_EXIT, "Exit");
    AppendMenuA(s_menu_bar, MF_POPUP, (UINT_PTR)file_menu, "File");

    /* Config menu (scale radio group) */
    s_config_menu = CreatePopupMenu();
    AppendMenuA(s_config_menu, MF_STRING, IDM_SCALE_1X, "Scale 1x");
    AppendMenuA(s_config_menu, MF_STRING, IDM_SCALE_2X, "Scale 2x");
    AppendMenuA(s_config_menu, MF_STRING, IDM_SCALE_3X, "Scale 3x");
    AppendMenuA(s_config_menu, MF_STRING, IDM_SCALE_4X, "Scale 4x");
    AppendMenuA(s_menu_bar, MF_POPUP, (UINT_PTR)s_config_menu, "Config");

    /* Set initial radio check */
    menu_update_scale_check(current_scale);

    SetMenu(hwnd, s_menu_bar);
    DrawMenuBar(hwnd);
}

int menu_handle_event(const SDL_Event *event) {
    if (event->type != SDL_SYSWMEVENT)
        return 0;

    const SDL_SysWMmsg *msg = event->syswm.msg;
    if (msg->msg.win.msg == WM_COMMAND) {
        int id = LOWORD(msg->msg.win.wParam);
        if (id >= IDM_SAVE_STATE && id <= IDM_SCALE_4X)
            return id;
    }
    return 0;
}

void menu_update_scale_check(int new_scale) {
    if (!s_config_menu)
        return;

    CheckMenuRadioItem(s_config_menu,
        IDM_SCALE_1X, IDM_SCALE_4X,
        IDM_SCALE_1X + (new_scale - 1),
        MF_BYCOMMAND);
}

#else /* non-Windows stubs */

void menu_init(platform_window_t *win, int current_scale) {
    (void)win; (void)current_scale;
}

int menu_handle_event(const SDL_Event *event) {
    (void)event;
    return 0;
}

void menu_update_scale_check(int new_scale) {
    (void)new_scale;
}

#endif /* _WIN32 */
