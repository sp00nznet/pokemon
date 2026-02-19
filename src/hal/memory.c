#include "memory.h"
#include "cpu.h"
#include "ppu.h"
#include "apu.h"
#include "timer.h"
#include "joypad.h"
#include "dma.h"
#include "serial.h"
#include "interrupts.h"
#include <string.h>
#include <stdio.h>

void mem_init(memory_state_t *mem, const uint8_t *rom, size_t rom_size, mbc_type_t mbc) {
    memset(mem, 0, sizeof(*mem));
    mem->rom = rom;
    mem->rom_size = rom_size;
    mem->mbc_type = mbc;
    mem->rom_bank = 1;
    mem->ram_bank = 0;
    mem->ram_enabled = false;

    /* Initialize I/O registers to post-boot values */
    mem->io[0x00] = 0xCF; /* P1 */
    mem->io[0x01] = 0x00; /* SB */
    mem->io[0x02] = 0x7E; /* SC */
    mem->io[0x04] = 0xAB; /* DIV */
    mem->io[0x05] = 0x00; /* TIMA */
    mem->io[0x06] = 0x00; /* TMA */
    mem->io[0x07] = 0xF8; /* TAC */
    mem->io[0x0F] = 0xE1; /* IF */
    mem->io[0x40] = 0x91; /* LCDC */
    mem->io[0x41] = 0x85; /* STAT */
    mem->io[0x42] = 0x00; /* SCY */
    mem->io[0x43] = 0x00; /* SCX */
    mem->io[0x44] = 0x00; /* LY */
    mem->io[0x45] = 0x00; /* LYC */
    mem->io[0x47] = 0xFC; /* BGP */
    mem->io[0x48] = 0xFF; /* OBP0 */
    mem->io[0x49] = 0xFF; /* OBP1 */
    mem->io[0x4A] = 0x00; /* WY */
    mem->io[0x4B] = 0x00; /* WX */
    mem->ie_reg = 0x00;

    mem->wram_bank = 1;
    mem->vram_bank = 0;
}

/* I/O register read handler */
static uint8_t io_read(gb_state_t *gb, uint8_t reg) {
    switch (reg) {
    case 0x00: /* P1 - Joypad */
        if (gb->joypad) return joypad_read(gb->joypad);
        return gb->mem->io[0x00] | 0xC0;

    case 0x04: /* DIV */
        if (gb->timer) return timer_read_div(gb->timer);
        return gb->mem->io[0x04];

    case 0x05: /* TIMA */
        if (gb->timer) return gb->timer->tima;
        return gb->mem->io[0x05];

    case 0x06: /* TMA */
        return gb->mem->io[0x06];

    case 0x07: /* TAC */
        return gb->mem->io[0x07] | 0xF8;

    case 0x0F: /* IF */
        return gb->mem->io[0x0F] | 0xE0;

    case 0x41: /* STAT */
        if (gb->ppu) return ppu_read_stat(gb->ppu) | 0x80;
        return gb->mem->io[0x41] | 0x80;

    case 0x44: /* LY */
        if (gb->ppu) return gb->ppu->ly;
        return gb->mem->io[0x44];

    /* Audio registers */
    case 0x10: case 0x11: case 0x12: case 0x13: case 0x14:
    case 0x16: case 0x17: case 0x18: case 0x19:
    case 0x1A: case 0x1B: case 0x1C: case 0x1D: case 0x1E:
    case 0x20: case 0x21: case 0x22: case 0x23:
    case 0x24: case 0x25: case 0x26:
        if (gb->apu) return apu_read_reg(gb->apu, reg);
        return gb->mem->io[reg];

    /* Wave RAM */
    case 0x30: case 0x31: case 0x32: case 0x33:
    case 0x34: case 0x35: case 0x36: case 0x37:
    case 0x38: case 0x39: case 0x3A: case 0x3B:
    case 0x3C: case 0x3D: case 0x3E: case 0x3F:
        if (gb->apu) return apu_read_wave(gb->apu, reg - 0x30);
        return gb->mem->io[reg];

    default:
        return gb->mem->io[reg];
    }
}

/* I/O register write handler */
static void io_write(gb_state_t *gb, uint8_t reg, uint8_t val) {
    switch (reg) {
    case 0x00: /* P1 - Joypad */
        gb->mem->io[0x00] = (val & 0x30) | (gb->mem->io[0x00] & 0x0F);
        if (gb->joypad) joypad_write(gb->joypad, val);
        return;

    case 0x04: /* DIV - writing resets to 0 */
        if (gb->timer) timer_write_div(gb->timer);
        gb->mem->io[0x04] = 0;
        return;

    case 0x05: /* TIMA */
        if (gb->timer) gb->timer->tima = val;
        gb->mem->io[0x05] = val;
        return;

    case 0x06: /* TMA */
        if (gb->timer) gb->timer->tma = val;
        gb->mem->io[0x06] = val;
        return;

    case 0x07: /* TAC */
        if (gb->timer) timer_write_tac(gb->timer, val);
        gb->mem->io[0x07] = val;
        return;

    case 0x0F: /* IF */
        gb->mem->io[0x0F] = val | 0xE0;
        return;

    case 0x40: /* LCDC */
        if (val != gb->mem->io[0x40]) {
            fprintf(stderr, "LCDC write: %02X -> %02X\n", gb->mem->io[0x40], val);
        }
        if (gb->ppu) ppu_write_lcdc(gb->ppu, gb, val);
        gb->mem->io[0x40] = val;
        return;

    case 0x41: /* STAT */
        if (gb->ppu) ppu_write_stat(gb->ppu, val);
        gb->mem->io[0x41] = (val & 0x78) | (gb->mem->io[0x41] & 0x07);
        return;

    case 0x42: /* SCY */
        if (gb->ppu) gb->ppu->scy = val;
        gb->mem->io[0x42] = val;
        return;

    case 0x43: /* SCX */
        if (gb->ppu) gb->ppu->scx = val;
        gb->mem->io[0x43] = val;
        return;

    case 0x44: /* LY - read only */
        return;

    case 0x45: /* LYC */
        if (gb->ppu) gb->ppu->lyc = val;
        gb->mem->io[0x45] = val;
        return;

    case 0x46: /* DMA */
        dma_start(gb, val);
        gb->mem->io[0x46] = val;
        return;

    case 0x47: /* BGP */
        if (gb->ppu) ppu_write_bgp(gb->ppu, val);
        gb->mem->io[0x47] = val;
        return;

    case 0x48: /* OBP0 */
        if (gb->ppu) ppu_write_obp(gb->ppu, 0, val);
        gb->mem->io[0x48] = val;
        return;

    case 0x49: /* OBP1 */
        if (gb->ppu) ppu_write_obp(gb->ppu, 1, val);
        gb->mem->io[0x49] = val;
        return;

    case 0x4A: /* WY */
        if (gb->ppu) gb->ppu->wy = val;
        gb->mem->io[0x4A] = val;
        return;

    case 0x4B: /* WX */
        if (gb->ppu) gb->ppu->wx = val;
        gb->mem->io[0x4B] = val;
        return;

    /* Audio registers */
    case 0x10: case 0x11: case 0x12: case 0x13: case 0x14:
    case 0x16: case 0x17: case 0x18: case 0x19:
    case 0x1A: case 0x1B: case 0x1C: case 0x1D: case 0x1E:
    case 0x20: case 0x21: case 0x22: case 0x23:
    case 0x24: case 0x25: case 0x26:
        if (gb->apu) apu_write_reg(gb->apu, reg, val);
        gb->mem->io[reg] = val;
        return;

    /* Wave RAM */
    case 0x30: case 0x31: case 0x32: case 0x33:
    case 0x34: case 0x35: case 0x36: case 0x37:
    case 0x38: case 0x39: case 0x3A: case 0x3B:
    case 0x3C: case 0x3D: case 0x3E: case 0x3F:
        if (gb->apu) apu_write_wave(gb->apu, reg - 0x30, val);
        gb->mem->io[reg] = val;
        return;

    default:
        gb->mem->io[reg] = val;
        return;
    }
}

/* MBC register write handlers */
static void mbc_write(memory_state_t *mem, uint16_t addr, uint8_t val) {
    switch (mem->mbc_type) {
    case MBC_MBC3:
        if (addr < 0x2000) {
            /* RAM/RTC enable */
            mem->ram_enabled = ((val & 0x0F) == 0x0A);
        } else if (addr < 0x4000) {
            /* ROM bank number (7 bits) */
            uint8_t bank = val & 0x7F;
            if (bank == 0) bank = 1;
            mem->rom_bank = bank;
        } else if (addr < 0x6000) {
            /* RAM bank / RTC select */
            if (val <= 0x03) {
                mem->ram_bank = val;
            } else if (val >= 0x08 && val <= 0x0C) {
                /* RTC register select */
                mem->ram_bank = val;
            }
        } else {
            /* Latch clock data */
            if (val == 0x01 && !mem->rtc_latched) {
                memcpy(mem->rtc_latch_regs, mem->rtc_regs, 5);
                mem->rtc_latched = true;
            } else if (val == 0x00) {
                mem->rtc_latched = false;
            }
        }
        break;

    case MBC_MBC5:
        if (addr < 0x2000) {
            /* RAM enable */
            mem->ram_enabled = ((val & 0x0F) == 0x0A);
        } else if (addr < 0x3000) {
            /* ROM bank low 8 bits */
            mem->rom_bank = (mem->rom_bank & 0x100) | val;
        } else if (addr < 0x4000) {
            /* ROM bank bit 8 */
            mem->rom_bank = (mem->rom_bank & 0xFF) | ((val & 0x01) << 8);
        } else if (addr < 0x6000) {
            /* RAM bank (4 bits) */
            mem->ram_bank = val & 0x0F;
        }
        break;

    case MBC_MBC1:
        if (addr < 0x2000) {
            mem->ram_enabled = ((val & 0x0F) == 0x0A);
        } else if (addr < 0x4000) {
            uint8_t bank = val & 0x1F;
            if (bank == 0) bank = 1;
            mem->rom_bank = (mem->rom_bank & 0x60) | bank;
        } else if (addr < 0x6000) {
            mem->rom_bank = (mem->rom_bank & 0x1F) | ((val & 0x03) << 5);
        }
        break;

    default:
        break;
    }
}

uint8_t mem_read8(gb_state_t *gb, uint16_t addr) {
    memory_state_t *mem = gb->mem;

    if (addr <= MEM_ROM_BANK0_END) {
        /* ROM bank 0: 0x0000-0x3FFF */
        return mem->rom[addr];
    }
    if (addr <= MEM_ROM_BANKN_END) {
        /* ROM switchable bank: 0x4000-0x7FFF */
        uint32_t offset = (uint32_t)mem->rom_bank * 0x4000 + (addr - 0x4000);
        if (offset < mem->rom_size) return mem->rom[offset];
        return 0xFF;
    }
    if (addr <= MEM_VRAM_END) {
        /* VRAM: 0x8000-0x9FFF */
        uint16_t vram_addr = addr - MEM_VRAM_START;
        if (mem->vram_bank == 1) return mem->vram2[vram_addr];
        return mem->vram[vram_addr];
    }
    if (addr <= MEM_EXTRAM_END) {
        /* External RAM: 0xA000-0xBFFF */
        if (!mem->ram_enabled) return 0xFF;
        /* MBC3 RTC registers */
        if (mem->mbc_type == MBC_MBC3 && mem->ram_bank >= 0x08) {
            uint8_t rtc_idx = mem->ram_bank - 0x08;
            if (rtc_idx < 5) {
                return mem->rtc_latched ? mem->rtc_latch_regs[rtc_idx] : mem->rtc_regs[rtc_idx];
            }
            return 0xFF;
        }
        uint32_t offset = (uint32_t)mem->ram_bank * 0x2000 + (addr - MEM_EXTRAM_START);
        if (offset < sizeof(mem->extram)) return mem->extram[offset];
        return 0xFF;
    }
    if (addr <= MEM_WRAM_END) {
        /* WRAM: 0xC000-0xDFFF */
        if (addr < 0xD000) {
            return mem->wram[addr - MEM_WRAM_START];
        }
        /* CGB switchable WRAM bank */
        if (mem->wram_bank > 1) {
            return mem->wram_extra[(mem->wram_bank - 1) * 0x1000 + (addr - 0xD000)];
        }
        return mem->wram[addr - MEM_WRAM_START];
    }
    if (addr <= MEM_ECHO_END) {
        /* Echo RAM: mirror of C000-DDFF */
        return mem_read8(gb, addr - 0x2000);
    }
    if (addr <= MEM_OAM_END) {
        /* OAM: 0xFE00-0xFE9F */
        return mem->oam[addr - MEM_OAM_START];
    }
    if (addr < MEM_IO_START) {
        /* Unusable: 0xFEA0-0xFEFF */
        return 0xFF;
    }
    if (addr <= MEM_IO_END) {
        /* I/O registers: 0xFF00-0xFF7F */
        return io_read(gb, (uint8_t)(addr - MEM_IO_START));
    }
    if (addr <= MEM_HRAM_END) {
        /* HRAM: 0xFF80-0xFFFE */
        return mem->hram[addr - MEM_HRAM_START];
    }
    if (addr == MEM_IE_REG) {
        return mem->ie_reg;
    }
    return 0xFF;
}

void mem_write8(gb_state_t *gb, uint16_t addr, uint8_t val) {
    memory_state_t *mem = gb->mem;

    if (addr <= MEM_ROM_BANKN_END) {
        /* ROM area: MBC register writes */
        mbc_write(mem, addr, val);
        return;
    }
    if (addr <= MEM_VRAM_END) {
        uint16_t vram_addr = addr - MEM_VRAM_START;
        if (mem->vram_bank == 1)
            mem->vram2[vram_addr] = val;
        else {
            /* Track writes to tile maps with epoch-based summaries */
            if (addr >= 0x9C00 && addr <= 0x9FFF) {
                static int map_write_count = 0;
                static int map_content_writes = 0;
                static int map_zero_writes = 0;
                static int last_report = 0;
                map_write_count++;
                if (val != 0x00 && val != 0x7F) {
                    map_content_writes++;
                    if (map_content_writes <= 80) {
                        int offset = addr - 0x9C00;
                        fprintf(stderr, "MAP9C00[%d,%d]=%02X (write#%d)\n",
                                offset % 32, offset / 32, val, map_write_count);
                    }
                } else if (val == 0x00) {
                    map_zero_writes++;
                }
                /* Report every 500 writes */
                if (map_write_count - last_report >= 500) {
                    fprintf(stderr, "MAP9C00 summary: %d total, %d content, %d zeros\n",
                            map_write_count, map_content_writes, map_zero_writes);
                    last_report = map_write_count;
                }
            }
            mem->vram[vram_addr] = val;
        }
        return;
    }
    if (addr <= MEM_EXTRAM_END) {
        if (!mem->ram_enabled) return;
        if (mem->mbc_type == MBC_MBC3 && mem->ram_bank >= 0x08) {
            uint8_t rtc_idx = mem->ram_bank - 0x08;
            if (rtc_idx < 5) mem->rtc_regs[rtc_idx] = val;
            return;
        }
        uint32_t offset = (uint32_t)mem->ram_bank * 0x2000 + (addr - MEM_EXTRAM_START);
        if (offset < sizeof(mem->extram)) mem->extram[offset] = val;
        return;
    }
    if (addr <= MEM_WRAM_END) {
        if (addr < 0xD000) {
            mem->wram[addr - MEM_WRAM_START] = val;
        } else if (mem->wram_bank > 1) {
            mem->wram_extra[(mem->wram_bank - 1) * 0x1000 + (addr - 0xD000)] = val;
        } else {
            mem->wram[addr - MEM_WRAM_START] = val;
        }
        return;
    }
    if (addr <= MEM_ECHO_END) {
        mem_write8(gb, addr - 0x2000, val);
        return;
    }
    if (addr <= MEM_OAM_END) {
        mem->oam[addr - MEM_OAM_START] = val;
        return;
    }
    if (addr < MEM_IO_START) {
        /* Unusable */
        return;
    }
    if (addr <= MEM_IO_END) {
        io_write(gb, (uint8_t)(addr - MEM_IO_START), val);
        return;
    }
    if (addr <= MEM_HRAM_END) {
        mem->hram[addr - MEM_HRAM_START] = val;
        return;
    }
    if (addr == MEM_IE_REG) {
        if (val != mem->ie_reg) {
            fprintf(stderr, "IE write: %02X -> %02X\n", mem->ie_reg, val);
        }
        mem->ie_reg = val;
        return;
    }
}

uint16_t mem_read16(gb_state_t *gb, uint16_t addr) {
    uint8_t lo = mem_read8(gb, addr);
    uint8_t hi = mem_read8(gb, addr + 1);
    return (uint16_t)(hi << 8) | lo;
}

void mem_write16(gb_state_t *gb, uint16_t addr, uint16_t val) {
    mem_write8(gb, addr, (uint8_t)(val & 0xFF));
    mem_write8(gb, addr + 1, (uint8_t)(val >> 8));
}

uint8_t mem_rom_read(const memory_state_t *mem, uint8_t bank, uint16_t addr) {
    uint32_t offset;
    if (bank == 0) {
        offset = addr;
    } else {
        offset = (uint32_t)bank * 0x4000 + (addr - 0x4000);
    }
    if (offset < mem->rom_size) return mem->rom[offset];
    return 0xFF;
}

uint16_t mem_get_rom_bank(const memory_state_t *mem) {
    return mem->rom_bank;
}
