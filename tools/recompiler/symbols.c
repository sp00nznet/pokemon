#include "symbols.h"
#include <stdio.h>

/* I/O register names (offset from 0xFF00) */
const char *sym_io_name(uint8_t io_offset) {
    switch (io_offset) {
    case 0x00: return "P1";       /* Joypad */
    case 0x01: return "SB";       /* Serial transfer data */
    case 0x02: return "SC";       /* Serial transfer control */
    case 0x04: return "DIV";      /* Divider register */
    case 0x05: return "TIMA";     /* Timer counter */
    case 0x06: return "TMA";      /* Timer modulo */
    case 0x07: return "TAC";      /* Timer control */
    case 0x0F: return "IF";       /* Interrupt flag */
    case 0x10: return "NR10";     /* Sound channel 1 sweep */
    case 0x11: return "NR11";     /* Sound channel 1 length/duty */
    case 0x12: return "NR12";     /* Sound channel 1 volume envelope */
    case 0x13: return "NR13";     /* Sound channel 1 frequency lo */
    case 0x14: return "NR14";     /* Sound channel 1 frequency hi */
    case 0x16: return "NR21";     /* Sound channel 2 length/duty */
    case 0x17: return "NR22";     /* Sound channel 2 volume envelope */
    case 0x18: return "NR23";     /* Sound channel 2 frequency lo */
    case 0x19: return "NR24";     /* Sound channel 2 frequency hi */
    case 0x1A: return "NR30";     /* Sound channel 3 on/off */
    case 0x1B: return "NR31";     /* Sound channel 3 length */
    case 0x1C: return "NR32";     /* Sound channel 3 output level */
    case 0x1D: return "NR33";     /* Sound channel 3 frequency lo */
    case 0x1E: return "NR34";     /* Sound channel 3 frequency hi */
    case 0x20: return "NR41";     /* Sound channel 4 length */
    case 0x21: return "NR42";     /* Sound channel 4 volume envelope */
    case 0x22: return "NR43";     /* Sound channel 4 polynomial counter */
    case 0x23: return "NR44";     /* Sound channel 4 counter/consecutive */
    case 0x24: return "NR50";     /* Channel control / volume */
    case 0x25: return "NR51";     /* Sound output terminal selection */
    case 0x26: return "NR52";     /* Sound on/off */
    case 0x40: return "LCDC";     /* LCD control */
    case 0x41: return "STAT";     /* LCD status */
    case 0x42: return "SCY";      /* Scroll Y */
    case 0x43: return "SCX";      /* Scroll X */
    case 0x44: return "LY";       /* LCD Y coordinate */
    case 0x45: return "LYC";      /* LY compare */
    case 0x46: return "DMA";      /* DMA transfer */
    case 0x47: return "BGP";      /* BG palette data */
    case 0x48: return "OBP0";     /* Object palette 0 */
    case 0x49: return "OBP1";     /* Object palette 1 */
    case 0x4A: return "WY";       /* Window Y position */
    case 0x4B: return "WX";       /* Window X position */
    case 0x4D: return "KEY1";     /* CGB speed switch */
    case 0x4F: return "VBK";      /* CGB VRAM bank */
    case 0x51: return "HDMA1";    /* CGB DMA source high */
    case 0x52: return "HDMA2";    /* CGB DMA source low */
    case 0x53: return "HDMA3";    /* CGB DMA dest high */
    case 0x54: return "HDMA4";    /* CGB DMA dest low */
    case 0x55: return "HDMA5";    /* CGB DMA length/mode */
    case 0x68: return "BCPS";     /* CGB BG palette index */
    case 0x69: return "BCPD";     /* CGB BG palette data */
    case 0x6A: return "OCPS";     /* CGB OBJ palette index */
    case 0x6B: return "OCPD";     /* CGB OBJ palette data */
    case 0x70: return "SVBK";     /* CGB WRAM bank */
    case 0xFF: return "IE";       /* Interrupt enable */
    default:   return NULL;
    }
}

const char *sym_addr_name(uint16_t addr) {
    switch (addr) {
    case 0x0000: return "RST_00";
    case 0x0008: return "RST_08";
    case 0x0010: return "RST_10";
    case 0x0018: return "RST_18";
    case 0x0020: return "RST_20";
    case 0x0028: return "RST_28";
    case 0x0030: return "RST_30";
    case 0x0038: return "RST_38";
    case 0x0040: return "VBlankHandler";
    case 0x0048: return "LCDStatHandler";
    case 0x0050: return "TimerHandler";
    case 0x0058: return "SerialHandler";
    case 0x0060: return "JoypadHandler";
    case 0x0100: return "EntryPoint";
    case 0x0150: return "Start";
    default:     return NULL;
    }
}

int sym_rom_banks(uint8_t rom_size_code) {
    switch (rom_size_code) {
    case 0x00: return 2;    /* 32 KB */
    case 0x01: return 4;    /* 64 KB */
    case 0x02: return 8;    /* 128 KB */
    case 0x03: return 16;   /* 256 KB */
    case 0x04: return 32;   /* 512 KB */
    case 0x05: return 64;   /* 1 MB */
    case 0x06: return 128;  /* 2 MB */
    case 0x07: return 256;  /* 4 MB */
    case 0x08: return 512;  /* 8 MB */
    default:   return -1;
    }
}

int sym_ram_banks(uint8_t ram_size_code) {
    switch (ram_size_code) {
    case 0x00: return 0;    /* No RAM */
    case 0x01: return 0;    /* Unused */
    case 0x02: return 1;    /* 8 KB (1 bank) */
    case 0x03: return 4;    /* 32 KB (4 banks) */
    case 0x04: return 16;   /* 128 KB (16 banks) */
    case 0x05: return 8;    /* 64 KB (8 banks) */
    default:   return -1;
    }
}

const char *sym_cart_type_name(uint8_t cart_type) {
    switch (cart_type) {
    case 0x00: return "ROM Only";
    case 0x01: return "MBC1";
    case 0x02: return "MBC1+RAM";
    case 0x03: return "MBC1+RAM+Battery";
    case 0x05: return "MBC2";
    case 0x06: return "MBC2+Battery";
    case 0x08: return "ROM+RAM";
    case 0x09: return "ROM+RAM+Battery";
    case 0x0F: return "MBC3+Timer+Battery";
    case 0x10: return "MBC3+Timer+RAM+Battery";
    case 0x11: return "MBC3";
    case 0x12: return "MBC3+RAM";
    case 0x13: return "MBC3+RAM+Battery";
    case 0x19: return "MBC5";
    case 0x1A: return "MBC5+RAM";
    case 0x1B: return "MBC5+RAM+Battery";
    case 0x1C: return "MBC5+Rumble";
    case 0x1D: return "MBC5+Rumble+RAM";
    case 0x1E: return "MBC5+Rumble+RAM+Battery";
    default:   return "Unknown";
    }
}

int sym_get_entry_points(sym_entry_point_t *entries, int max_entries) {
    int n = 0;
    if (n < max_entries) {
        entries[n++] = (sym_entry_point_t){INT_VBLANK, 0, "VBlankHandler"};
    }
    if (n < max_entries) {
        entries[n++] = (sym_entry_point_t){INT_LCD_STAT, 0, "LCDStatHandler"};
    }
    if (n < max_entries) {
        entries[n++] = (sym_entry_point_t){INT_TIMER, 0, "TimerHandler"};
    }
    if (n < max_entries) {
        entries[n++] = (sym_entry_point_t){INT_SERIAL, 0, "SerialHandler"};
    }
    if (n < max_entries) {
        entries[n++] = (sym_entry_point_t){INT_JOYPAD, 0, "JoypadHandler"};
    }
    if (n < max_entries) {
        entries[n++] = (sym_entry_point_t){ENTRY_POINT, 0, "Start"};
    }
    /* RST vectors */
    if (n < max_entries) entries[n++] = (sym_entry_point_t){0x0000, 0, "RST_00"};
    if (n < max_entries) entries[n++] = (sym_entry_point_t){0x0008, 0, "RST_08"};
    if (n < max_entries) entries[n++] = (sym_entry_point_t){0x0010, 0, "RST_10"};
    if (n < max_entries) entries[n++] = (sym_entry_point_t){0x0018, 0, "RST_18"};
    if (n < max_entries) entries[n++] = (sym_entry_point_t){0x0020, 0, "RST_20"};
    if (n < max_entries) entries[n++] = (sym_entry_point_t){0x0028, 0, "RST_28"};
    if (n < max_entries) entries[n++] = (sym_entry_point_t){0x0030, 0, "RST_30"};
    if (n < max_entries) entries[n++] = (sym_entry_point_t){0x0038, 0, "RST_38"};
    /* Additional known bank 0 functions missed by recursive descent */
    if (n < max_entries) entries[n++] = (sym_entry_point_t){0x1F49, 0, "SoftReset"};
    return n;
}
