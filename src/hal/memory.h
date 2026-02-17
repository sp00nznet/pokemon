#ifndef MEMORY_H
#define MEMORY_H

#include <stdint.h>
#include <stdbool.h>

/* Game Boy memory subsystem with MBC3/MBC5 support */

/* Memory map constants */
#define MEM_ROM_BANK0_START   0x0000
#define MEM_ROM_BANK0_END     0x3FFF
#define MEM_ROM_BANKN_START   0x4000
#define MEM_ROM_BANKN_END     0x7FFF
#define MEM_VRAM_START        0x8000
#define MEM_VRAM_END          0x9FFF
#define MEM_EXTRAM_START      0xA000
#define MEM_EXTRAM_END        0xBFFF
#define MEM_WRAM_START        0xC000
#define MEM_WRAM_END          0xDFFF
#define MEM_ECHO_START        0xE000
#define MEM_ECHO_END          0xFDFF
#define MEM_OAM_START         0xFE00
#define MEM_OAM_END           0xFE9F
#define MEM_IO_START          0xFF00
#define MEM_IO_END            0xFF7F
#define MEM_HRAM_START        0xFF80
#define MEM_HRAM_END          0xFFFE
#define MEM_IE_REG            0xFFFF

typedef enum {
    MBC_NONE = 0,
    MBC_MBC1,
    MBC_MBC3,
    MBC_MBC5
} mbc_type_t;

typedef struct memory_state {
    /* ROM data (pointer to loaded ROM) */
    const uint8_t *rom;
    size_t rom_size;

    /* Bank controller */
    mbc_type_t mbc_type;
    uint16_t rom_bank;      /* Current ROM bank (1-511) */
    uint8_t  ram_bank;      /* Current RAM bank (0-15) */
    bool     ram_enabled;   /* External RAM enable */

    /* MBC3 RTC */
    uint8_t  rtc_regs[5];   /* S, M, H, DL, DH */
    bool     rtc_latched;
    uint8_t  rtc_latch_regs[5];

    /* Memory arrays */
    uint8_t vram[0x2000];       /* 8 KB Video RAM */
    uint8_t extram[0x8000];     /* Up to 32 KB external RAM (4 banks) */
    uint8_t wram[0x2000];       /* 8 KB Work RAM */
    uint8_t oam[0xA0];          /* 160 bytes OAM */
    uint8_t io[0x80];           /* I/O registers */
    uint8_t hram[0x7F];         /* High RAM */
    uint8_t ie_reg;             /* Interrupt Enable register */

    /* CGB extras (Yellow) */
    uint8_t vram_bank;          /* CGB VRAM bank */
    uint8_t vram2[0x2000];      /* CGB second VRAM bank */
    uint8_t wram_bank;          /* CGB WRAM bank */
    uint8_t wram_extra[7 * 0x1000]; /* CGB WRAM banks 1-7 */
} memory_state_t;

/* Forward declare gb_state_t */
typedef struct gb_state gb_state_t;

/* Initialize memory subsystem */
void mem_init(memory_state_t *mem, const uint8_t *rom, size_t rom_size, mbc_type_t mbc);

/* Memory access */
uint8_t mem_read8(gb_state_t *gb, uint16_t addr);
void mem_write8(gb_state_t *gb, uint16_t addr, uint8_t val);

/* 16-bit convenience (little-endian) */
uint16_t mem_read16(gb_state_t *gb, uint16_t addr);
void mem_write16(gb_state_t *gb, uint16_t addr, uint16_t val);

/* Direct ROM access (for data tables) */
uint8_t mem_rom_read(const memory_state_t *mem, uint8_t bank, uint16_t addr);

/* Get current ROM bank number */
uint16_t mem_get_rom_bank(const memory_state_t *mem);

#endif /* MEMORY_H */
