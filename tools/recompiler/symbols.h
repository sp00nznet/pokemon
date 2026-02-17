#ifndef SYMBOLS_H
#define SYMBOLS_H

#include <stdint.h>
#include <stdbool.h>

/* Known hardware register names and symbol database */

/* Get human-readable name for an I/O register address (0xFF00-0xFF7F) */
const char *sym_io_name(uint8_t io_offset);

/* Get human-readable name for a known address (interrupt vectors, etc.) */
const char *sym_addr_name(uint16_t addr);

/* ROM header offsets */
#define ROM_ENTRY_POINT     0x0100
#define ROM_LOGO_START      0x0104
#define ROM_LOGO_END        0x0133
#define ROM_TITLE           0x0134
#define ROM_CGB_FLAG        0x0143
#define ROM_CART_TYPE       0x0147
#define ROM_ROM_SIZE        0x0148
#define ROM_RAM_SIZE        0x0149
#define ROM_HEADER_CHECKSUM 0x014D
#define ROM_GLOBAL_CHECKSUM 0x014E

/* Interrupt vector addresses */
#define INT_VBLANK          0x0040
#define INT_LCD_STAT        0x0048
#define INT_TIMER           0x0050
#define INT_SERIAL          0x0058
#define INT_JOYPAD          0x0060

/* Entry point after boot ROM */
#define ENTRY_POINT         0x0150

/* Game Boy bank size */
#define BANK_SIZE           0x4000  /* 16 KB */

/* Cartridge type codes */
#define CART_MBC3           0x13
#define CART_MBC3_RAM_BAT   0x13
#define CART_MBC5           0x19
#define CART_MBC5_RAM       0x1A
#define CART_MBC5_RAM_BAT   0x1B

/* ROM size helpers */
int sym_rom_banks(uint8_t rom_size_code);
int sym_ram_banks(uint8_t ram_size_code);
const char *sym_cart_type_name(uint8_t cart_type);

/* Known entry points for recursive disassembly */
typedef struct {
    uint16_t addr;
    uint8_t  bank;
    const char *name;
} sym_entry_point_t;

/* Get list of initial entry points (interrupt vectors + entry point).
 * Returns count, fills entries array. */
int sym_get_entry_points(sym_entry_point_t *entries, int max_entries);

#endif /* SYMBOLS_H */
