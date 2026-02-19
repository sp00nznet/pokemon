#ifndef CPU_H
#define CPU_H

#include <stdint.h>
#include <stdbool.h>

/* Game Boy CPU (SM83) state */

typedef struct gb_state gb_state_t;

struct gb_state {
    /* 8-bit registers */
    uint8_t a, b, c, d, e, h, l;

    /* Flags (individual bits for fast access in generated code) */
    uint8_t f_z;    /* Zero flag */
    uint8_t f_n;    /* Subtract flag */
    uint8_t f_h;    /* Half-carry flag */
    uint8_t f_c;    /* Carry flag */

    /* 16-bit registers */
    uint16_t sp;    /* Stack pointer */
    uint16_t pc;    /* Program counter (used for tracking, not execution) */

    /* Interrupt state */
    uint8_t ime;    /* Interrupt Master Enable */
    uint8_t ime_pending; /* EI delays by one instruction */
    uint8_t halted;

    /* Cycle counter */
    uint64_t cycles;
    uint64_t target_cycles; /* For synchronization */
    uint64_t sync_cycles;   /* Cycles at last hal_sync call */

    /* Memory subsystem (defined in memory.h) */
    struct memory_state *mem;

    /* PPU state */
    struct ppu_state *ppu;

    /* APU state */
    struct apu_state *apu;

    /* Timer state */
    struct timer_state *timer;

    /* Joypad state */
    struct joypad_state *joypad;

    /* Running flag */
    bool running;

    /* Frame callback - called when PPU has a frame ready */
    void (*frame_callback)(struct gb_state *gb, void *userdata);
    void *frame_userdata;

    /* Debug */
    bool debug_cpu;
    bool debug_mem;
    bool debug_ppu;
};

/* Register pair access macros */
#define REG_AF(gb) ((uint16_t)((gb)->a << 8) | cpu_get_f(gb))
#define REG_BC(gb) ((uint16_t)((gb)->b << 8) | (gb)->c)
#define REG_DE(gb) ((uint16_t)((gb)->d << 8) | (gb)->e)
#define REG_HL(gb) ((uint16_t)((gb)->h << 8) | (gb)->l)

#define SET_REG_AF(gb, v) do { (gb)->a = (uint8_t)((v) >> 8); cpu_set_f(gb, (uint8_t)(v)); } while(0)
#define SET_REG_BC(gb, v) do { (gb)->b = (uint8_t)((v) >> 8); (gb)->c = (uint8_t)(v); } while(0)
#define SET_REG_DE(gb, v) do { (gb)->d = (uint8_t)((v) >> 8); (gb)->e = (uint8_t)(v); } while(0)
#define SET_REG_HL(gb, v) do { (gb)->h = (uint8_t)((v) >> 8); (gb)->l = (uint8_t)(v); } while(0)

/* F register packing/unpacking (flags are in bits 7-4, bits 3-0 always 0) */
static inline uint8_t cpu_get_f(const gb_state_t *gb) {
    return (uint8_t)((gb->f_z ? 0x80 : 0) |
                     (gb->f_n ? 0x40 : 0) |
                     (gb->f_h ? 0x20 : 0) |
                     (gb->f_c ? 0x10 : 0));
}

static inline void cpu_set_f(gb_state_t *gb, uint8_t f) {
    gb->f_z = (f >> 7) & 1;
    gb->f_n = (f >> 6) & 1;
    gb->f_h = (f >> 5) & 1;
    gb->f_c = (f >> 4) & 1;
}

/* Stack operations */
static inline void cpu_push8(gb_state_t *gb, uint8_t val);
static inline uint8_t cpu_pop8(gb_state_t *gb);
static inline void cpu_push16(gb_state_t *gb, uint16_t val);
static inline uint16_t cpu_pop16(gb_state_t *gb);

/* Initialize CPU state to post-boot values */
void cpu_init(gb_state_t *gb);

/* Process pending interrupts (called at yield points) */
void cpu_check_interrupts(gb_state_t *gb);

/* HALT implementation */
void cpu_halt(gb_state_t *gb);

/* HAL functions called by generated code */
void hal_halt(gb_state_t *gb);
void hal_stop(gb_state_t *gb);
void hal_invalid(gb_state_t *gb, uint16_t addr, uint8_t opcode);

/* Yield point - sync hardware and check for events */
void hal_sync(gb_state_t *gb, uint32_t cycles);

/* Process SDL events */
bool hal_process_events(gb_state_t *gb);

/* Forward declarations for memory access (defined in memory.h) */
uint8_t mem_read8(gb_state_t *gb, uint16_t addr);
void mem_write8(gb_state_t *gb, uint16_t addr, uint8_t val);

/* Stack ops need memory access - implemented after memory.h is available */
#include "memory.h"

static inline void cpu_push8(gb_state_t *gb, uint8_t val) {
    gb->sp--;
    mem_write8(gb, gb->sp, val);
}

static inline uint8_t cpu_pop8(gb_state_t *gb) {
    uint8_t val = mem_read8(gb, gb->sp);
    gb->sp++;
    return val;
}

static inline void cpu_push16(gb_state_t *gb, uint16_t val) {
    cpu_push8(gb, (uint8_t)(val >> 8));
    cpu_push8(gb, (uint8_t)(val & 0xFF));
}

static inline uint16_t cpu_pop16(gb_state_t *gb) {
    uint8_t lo = cpu_pop8(gb);
    uint8_t hi = cpu_pop8(gb);
    return (uint16_t)(hi << 8) | lo;
}

#endif /* CPU_H */
