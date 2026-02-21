#include "save.h"
#include "hal/cpu.h"
#include "hal/memory.h"
#include "hal/ppu.h"
#include "hal/apu.h"
#include "hal/timer.h"
#include "hal/joypad.h"
#include <stdio.h>
#include <string.h>

/* Save state magic and version */
#define SAVE_STATE_MAGIC "PKSS"
#define SAVE_STATE_VERSION 1

/* Helper macros for binary serialization */
#define WRITE_VAR(f, var) fwrite(&(var), sizeof(var), 1, f)
#define READ_VAR(f, var) fread(&(var), sizeof(var), 1, f)

bool save_write(const gb_state_t *gb, const char *path) {
    if (!gb->mem) return false;

    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "Warning: Cannot write save file: %s\n", path);
        return false;
    }

    /* Write all external RAM banks */
    size_t written = fwrite(gb->mem->extram, 1, sizeof(gb->mem->extram), f);
    fclose(f);

    if (written != sizeof(gb->mem->extram)) {
        fprintf(stderr, "Warning: Incomplete save write\n");
        return false;
    }

    printf("Saved to %s\n", path);
    return true;
}

bool save_load(gb_state_t *gb, const char *path) {
    if (!gb->mem) return false;

    FILE *f = fopen(path, "rb");
    if (!f) return false; /* No save file, not an error */

    size_t read = fread(gb->mem->extram, 1, sizeof(gb->mem->extram), f);
    fclose(f);

    if (read > 0) {
        printf("Loaded save from %s (%zu bytes)\n", path, read);
        return true;
    }

    return false;
}

void save_make_path(char *buf, size_t bufsz, const char *save_dir,
                    const char *game_name) {
    snprintf(buf, bufsz, "%s/pokemon_%s.sav", save_dir, game_name);
}

void save_state_make_path(char *buf, size_t bufsz, const char *save_dir,
                          const char *game_name) {
    snprintf(buf, bufsz, "%s/pokemon_%s.state", save_dir, game_name);
}

bool save_state_write(gb_state_t *gb, const char *path) {
    if (!gb || !gb->mem || !gb->ppu || !gb->apu || !gb->timer || !gb->joypad)
        return false;

    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "Save state: cannot open %s for writing\n", path);
        return false;
    }

    /* Header */
    uint16_t version = SAVE_STATE_VERSION;
    fwrite(SAVE_STATE_MAGIC, 1, 4, f);
    WRITE_VAR(f, version);

    /* CPU registers and state */
    WRITE_VAR(f, gb->a);
    WRITE_VAR(f, gb->b);
    WRITE_VAR(f, gb->c);
    WRITE_VAR(f, gb->d);
    WRITE_VAR(f, gb->e);
    WRITE_VAR(f, gb->h);
    WRITE_VAR(f, gb->l);
    WRITE_VAR(f, gb->f_z);
    WRITE_VAR(f, gb->f_n);
    WRITE_VAR(f, gb->f_h);
    WRITE_VAR(f, gb->f_c);
    WRITE_VAR(f, gb->sp);
    WRITE_VAR(f, gb->pc);
    WRITE_VAR(f, gb->ime);
    WRITE_VAR(f, gb->ime_pending);
    WRITE_VAR(f, gb->halted);
    WRITE_VAR(f, gb->cycles);
    WRITE_VAR(f, gb->target_cycles);
    WRITE_VAR(f, gb->sync_cycles);

    /* Memory banking state */
    memory_state_t *mem = gb->mem;
    WRITE_VAR(f, mem->mbc_type);
    WRITE_VAR(f, mem->rom_bank);
    WRITE_VAR(f, mem->ram_bank);
    WRITE_VAR(f, mem->ram_enabled);
    fwrite(mem->rtc_regs, 1, sizeof(mem->rtc_regs), f);
    WRITE_VAR(f, mem->rtc_latched);
    fwrite(mem->rtc_latch_regs, 1, sizeof(mem->rtc_latch_regs), f);

    /* Memory arrays */
    fwrite(mem->vram, 1, sizeof(mem->vram), f);
    fwrite(mem->extram, 1, sizeof(mem->extram), f);
    fwrite(mem->wram, 1, sizeof(mem->wram), f);
    fwrite(mem->oam, 1, sizeof(mem->oam), f);
    fwrite(mem->io, 1, sizeof(mem->io), f);
    fwrite(mem->hram, 1, sizeof(mem->hram), f);
    WRITE_VAR(f, mem->ie_reg);
    WRITE_VAR(f, mem->vram_bank);
    fwrite(mem->vram2, 1, sizeof(mem->vram2), f);
    WRITE_VAR(f, mem->wram_bank);
    fwrite(mem->wram_extra, 1, sizeof(mem->wram_extra), f);

    /* PPU state (skip framebuffer, bg_priority, frame_ready) */
    ppu_state_t *ppu = gb->ppu;
    WRITE_VAR(f, ppu->ly);
    WRITE_VAR(f, ppu->lyc);
    WRITE_VAR(f, ppu->mode);
    WRITE_VAR(f, ppu->mode_cycles);
    WRITE_VAR(f, ppu->lcdc);
    WRITE_VAR(f, ppu->stat);
    WRITE_VAR(f, ppu->scy);
    WRITE_VAR(f, ppu->scx);
    WRITE_VAR(f, ppu->wy);
    WRITE_VAR(f, ppu->wx);
    WRITE_VAR(f, ppu->bgp);
    fwrite(ppu->obp, 1, sizeof(ppu->obp), f);
    fwrite(ppu->bg_palette, sizeof(uint32_t), 4, f);
    fwrite(ppu->obj_palette, sizeof(uint32_t), 8, f);
    fwrite(ppu->cgb_bg_palette_ram, 1, sizeof(ppu->cgb_bg_palette_ram), f);
    fwrite(ppu->cgb_obj_palette_ram, 1, sizeof(ppu->cgb_obj_palette_ram), f);
    WRITE_VAR(f, ppu->cgb_bg_palette_idx);
    WRITE_VAR(f, ppu->cgb_obj_palette_idx);
    fwrite(ppu->cgb_bg_palettes, sizeof(uint32_t), 32, f);
    fwrite(ppu->cgb_obj_palettes, sizeof(uint32_t), 32, f);
    WRITE_VAR(f, ppu->cgb_mode);
    WRITE_VAR(f, ppu->window_line);
    WRITE_VAR(f, ppu->prev_stat_line);

    /* APU state (skip lock, sample_buffer, sample_count) */
    apu_state_t *apu = gb->apu;
    WRITE_VAR(f, apu->ch1);
    WRITE_VAR(f, apu->ch2);
    WRITE_VAR(f, apu->ch3);
    WRITE_VAR(f, apu->ch4);
    WRITE_VAR(f, apu->nr50);
    WRITE_VAR(f, apu->nr51);
    WRITE_VAR(f, apu->nr52);
    WRITE_VAR(f, apu->frame_seq_counter);
    WRITE_VAR(f, apu->frame_seq_step);
    WRITE_VAR(f, apu->sample_accum);
    WRITE_VAR(f, apu->hpf_capacitor_l);
    WRITE_VAR(f, apu->hpf_capacitor_r);
    WRITE_VAR(f, apu->enabled);

    /* Timer state */
    timer_state_t *timer = gb->timer;
    WRITE_VAR(f, timer->div_counter);
    WRITE_VAR(f, timer->tima);
    WRITE_VAR(f, timer->tma);
    WRITE_VAR(f, timer->tac);
    WRITE_VAR(f, timer->tima_overflow);
    WRITE_VAR(f, timer->overflow_cycles);

    /* Joypad state */
    joypad_state_t *joy = gb->joypad;
    WRITE_VAR(f, joy->p1_select);
    WRITE_VAR(f, joy->directions);
    WRITE_VAR(f, joy->buttons);

    fclose(f);
    printf("Save state written to %s\n", path);
    return true;
}

bool save_state_load(gb_state_t *gb, const char *path) {
    if (!gb || !gb->mem || !gb->ppu || !gb->apu || !gb->timer || !gb->joypad)
        return false;

    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Save state: file not found: %s\n", path);
        return false;
    }

    /* Verify header */
    char magic[4];
    uint16_t version;
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, SAVE_STATE_MAGIC, 4) != 0) {
        fprintf(stderr, "Save state: invalid file (bad magic)\n");
        fclose(f);
        return false;
    }
    READ_VAR(f, version);
    if (version != SAVE_STATE_VERSION) {
        fprintf(stderr, "Save state: unsupported version %u (expected %u)\n",
                version, SAVE_STATE_VERSION);
        fclose(f);
        return false;
    }

    /* CPU registers and state */
    READ_VAR(f, gb->a);
    READ_VAR(f, gb->b);
    READ_VAR(f, gb->c);
    READ_VAR(f, gb->d);
    READ_VAR(f, gb->e);
    READ_VAR(f, gb->h);
    READ_VAR(f, gb->l);
    READ_VAR(f, gb->f_z);
    READ_VAR(f, gb->f_n);
    READ_VAR(f, gb->f_h);
    READ_VAR(f, gb->f_c);
    READ_VAR(f, gb->sp);
    READ_VAR(f, gb->pc);
    READ_VAR(f, gb->ime);
    READ_VAR(f, gb->ime_pending);
    READ_VAR(f, gb->halted);
    READ_VAR(f, gb->cycles);
    READ_VAR(f, gb->target_cycles);
    READ_VAR(f, gb->sync_cycles);

    /* Memory - preserve ROM pointer and size */
    memory_state_t *mem = gb->mem;
    const uint8_t *saved_rom = mem->rom;
    size_t saved_rom_size = mem->rom_size;

    READ_VAR(f, mem->mbc_type);
    READ_VAR(f, mem->rom_bank);
    READ_VAR(f, mem->ram_bank);
    READ_VAR(f, mem->ram_enabled);
    fread(mem->rtc_regs, 1, sizeof(mem->rtc_regs), f);
    READ_VAR(f, mem->rtc_latched);
    fread(mem->rtc_latch_regs, 1, sizeof(mem->rtc_latch_regs), f);

    fread(mem->vram, 1, sizeof(mem->vram), f);
    fread(mem->extram, 1, sizeof(mem->extram), f);
    fread(mem->wram, 1, sizeof(mem->wram), f);
    fread(mem->oam, 1, sizeof(mem->oam), f);
    fread(mem->io, 1, sizeof(mem->io), f);
    fread(mem->hram, 1, sizeof(mem->hram), f);
    READ_VAR(f, mem->ie_reg);
    READ_VAR(f, mem->vram_bank);
    fread(mem->vram2, 1, sizeof(mem->vram2), f);
    READ_VAR(f, mem->wram_bank);
    fread(mem->wram_extra, 1, sizeof(mem->wram_extra), f);

    mem->rom = saved_rom;
    mem->rom_size = saved_rom_size;

    /* PPU state */
    ppu_state_t *ppu = gb->ppu;
    READ_VAR(f, ppu->ly);
    READ_VAR(f, ppu->lyc);
    READ_VAR(f, ppu->mode);
    READ_VAR(f, ppu->mode_cycles);
    READ_VAR(f, ppu->lcdc);
    READ_VAR(f, ppu->stat);
    READ_VAR(f, ppu->scy);
    READ_VAR(f, ppu->scx);
    READ_VAR(f, ppu->wy);
    READ_VAR(f, ppu->wx);
    READ_VAR(f, ppu->bgp);
    fread(ppu->obp, 1, sizeof(ppu->obp), f);
    fread(ppu->bg_palette, sizeof(uint32_t), 4, f);
    fread(ppu->obj_palette, sizeof(uint32_t), 8, f);
    fread(ppu->cgb_bg_palette_ram, 1, sizeof(ppu->cgb_bg_palette_ram), f);
    fread(ppu->cgb_obj_palette_ram, 1, sizeof(ppu->cgb_obj_palette_ram), f);
    READ_VAR(f, ppu->cgb_bg_palette_idx);
    READ_VAR(f, ppu->cgb_obj_palette_idx);
    fread(ppu->cgb_bg_palettes, sizeof(uint32_t), 32, f);
    fread(ppu->cgb_obj_palettes, sizeof(uint32_t), 32, f);
    READ_VAR(f, ppu->cgb_mode);
    READ_VAR(f, ppu->window_line);
    READ_VAR(f, ppu->prev_stat_line);
    ppu->frame_ready = false;

    /* APU state - preserve SDL mutex */
    apu_state_t *apu = gb->apu;
    SDL_mutex *saved_lock = apu->lock;

    READ_VAR(f, apu->ch1);
    READ_VAR(f, apu->ch2);
    READ_VAR(f, apu->ch3);
    READ_VAR(f, apu->ch4);
    READ_VAR(f, apu->nr50);
    READ_VAR(f, apu->nr51);
    READ_VAR(f, apu->nr52);
    READ_VAR(f, apu->frame_seq_counter);
    READ_VAR(f, apu->frame_seq_step);
    READ_VAR(f, apu->sample_accum);
    READ_VAR(f, apu->hpf_capacitor_l);
    READ_VAR(f, apu->hpf_capacitor_r);
    READ_VAR(f, apu->enabled);

    apu->lock = saved_lock;
    /* Clear audio buffer to prevent stale samples */
    if (apu->lock) SDL_LockMutex(apu->lock);
    memset(apu->sample_buffer, 0, sizeof(apu->sample_buffer));
    apu->sample_count = 0;
    if (apu->lock) SDL_UnlockMutex(apu->lock);

    /* Timer state */
    timer_state_t *timer = gb->timer;
    READ_VAR(f, timer->div_counter);
    READ_VAR(f, timer->tima);
    READ_VAR(f, timer->tma);
    READ_VAR(f, timer->tac);
    READ_VAR(f, timer->tima_overflow);
    READ_VAR(f, timer->overflow_cycles);

    /* Joypad state */
    joypad_state_t *joy = gb->joypad;
    READ_VAR(f, joy->p1_select);
    READ_VAR(f, joy->directions);
    READ_VAR(f, joy->buttons);

    fclose(f);
    printf("Save state loaded from %s\n", path);
    return true;
}
