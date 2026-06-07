/* Headless platform shim for the rom_headless DLL.
 *
 * The core gbrt runtime (gbrt.c) reads the joypad straight from these two
 * globals when the game polls JOYP ($FF00) -- they are normally provided by
 * platform_sdl.cpp. The headless build links neither SDL nor ImGui, so we
 * supply just these symbols here and let the DLL bridge drive them through
 * gbrom_set_buttons().
 *
 * Both are active-low (0 = pressed), matching platform_sdl.cpp:
 *   g_joypad_buttons: bit0 A, bit1 B, bit2 Select, bit3 Start
 *   g_joypad_dpad   : bit0 Right, bit1 Left, bit2 Up, bit3 Down
 */
#include <stdint.h>

uint8_t g_joypad_buttons = 0xFF;
uint8_t g_joypad_dpad    = 0xFF;
