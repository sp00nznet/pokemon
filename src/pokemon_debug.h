/*
 * pokemon_debug.h - Debug logging, dispatch trace ring buffer, and watchpoints
 *
 * Provides:
 *   1. Category-based debug macros (DBG_CPU, DBG_MEM, etc.)
 *   2. Dispatch trace ring buffer - records last N dispatch calls/jumps.
 *   3. Memory watchpoint support - trap writes to specific addresses.
 */

#ifndef POKEMON_DEBUG_H
#define POKEMON_DEBUG_H

#include <stdint.h>
#include <stdio.h>

/* ========================================================================
 * 1. Category-based debug logging
 * ======================================================================== */

#ifndef GB_DEBUG_CPU
#define GB_DEBUG_CPU  0
#endif
#ifndef GB_DEBUG_MEM
#define GB_DEBUG_MEM  0
#endif
#ifndef GB_DEBUG_PPU
#define GB_DEBUG_PPU  0
#endif
#ifndef GB_DEBUG_APU
#define GB_DEBUG_APU  0
#endif
#ifndef GB_DEBUG_TIMER
#define GB_DEBUG_TIMER 0
#endif
#ifndef GB_DEBUG_DMA
#define GB_DEBUG_DMA  0
#endif
#ifndef GB_DEBUG_INT
#define GB_DEBUG_INT  0
#endif
#ifndef GB_DEBUG_BANK
#define GB_DEBUG_BANK 0
#endif
#ifndef GB_DEBUG_DISPATCH
#define GB_DEBUG_DISPATCH 0
#endif

#if GB_DEBUG_CPU
#define DBG_CPU(fmt, ...) fprintf(stderr, "[CPU] " fmt "\n", ##__VA_ARGS__)
#else
#define DBG_CPU(fmt, ...) ((void)0)
#endif

#if GB_DEBUG_MEM
#define DBG_MEM(fmt, ...) fprintf(stderr, "[MEM] " fmt "\n", ##__VA_ARGS__)
#else
#define DBG_MEM(fmt, ...) ((void)0)
#endif

#if GB_DEBUG_PPU
#define DBG_PPU(fmt, ...) fprintf(stderr, "[PPU] " fmt "\n", ##__VA_ARGS__)
#else
#define DBG_PPU(fmt, ...) ((void)0)
#endif

#if GB_DEBUG_APU
#define DBG_APU(fmt, ...) fprintf(stderr, "[APU] " fmt "\n", ##__VA_ARGS__)
#else
#define DBG_APU(fmt, ...) ((void)0)
#endif

#if GB_DEBUG_TIMER
#define DBG_TIMER(fmt, ...) fprintf(stderr, "[TMR] " fmt "\n", ##__VA_ARGS__)
#else
#define DBG_TIMER(fmt, ...) ((void)0)
#endif

#if GB_DEBUG_DMA
#define DBG_DMA(fmt, ...) fprintf(stderr, "[DMA] " fmt "\n", ##__VA_ARGS__)
#else
#define DBG_DMA(fmt, ...) ((void)0)
#endif

#if GB_DEBUG_INT
#define DBG_INT(fmt, ...) fprintf(stderr, "[INT] " fmt "\n", ##__VA_ARGS__)
#else
#define DBG_INT(fmt, ...) ((void)0)
#endif

#if GB_DEBUG_BANK
#define DBG_BANK(fmt, ...) fprintf(stderr, "[BNK] " fmt "\n", ##__VA_ARGS__)
#else
#define DBG_BANK(fmt, ...) ((void)0)
#endif

#if GB_DEBUG_DISPATCH
#define DBG_DISP(fmt, ...) fprintf(stderr, "[DSP] " fmt "\n", ##__VA_ARGS__)
#else
#define DBG_DISP(fmt, ...) ((void)0)
#endif

/* ========================================================================
 * 2. Dispatch trace ring buffer
 * ======================================================================== */

#define DTRACE_SIZE 64  /* Must be power of 2 */

typedef struct {
    uint16_t addr;
    uint8_t  bank;
    uint8_t  a;
    uint16_t sp;
    uint16_t hl;
    uint8_t  type;  /* 'C' = call, 'J' = jump */
    uint8_t  _pad;
} dtrace_entry_t;

typedef struct {
    dtrace_entry_t entries[DTRACE_SIZE];
    uint32_t head;
    uint32_t count;
} dtrace_state_t;

extern dtrace_state_t g_dtrace;

static inline void dtrace_record(uint8_t bank, uint16_t addr,
                                  uint8_t a, uint16_t sp, uint16_t hl,
                                  uint8_t type) {
    uint32_t idx = g_dtrace.head & (DTRACE_SIZE - 1);
    g_dtrace.entries[idx].bank = bank;
    g_dtrace.entries[idx].addr = addr;
    g_dtrace.entries[idx].a = a;
    g_dtrace.entries[idx].sp = sp;
    g_dtrace.entries[idx].hl = hl;
    g_dtrace.entries[idx].type = type;
    g_dtrace.head++;
    g_dtrace.count++;
}

void dtrace_dump(const char *reason);

/* ========================================================================
 * 3. Memory watchpoints
 * ======================================================================== */

#define MAX_WATCHPOINTS 8

typedef struct {
    uint16_t addr;
    uint8_t  prev_val;
    uint8_t  active;
} mem_watchpoint_t;

extern mem_watchpoint_t g_watchpoints[MAX_WATCHPOINTS];
extern int g_watchpoint_count;

void watchpoint_add(uint16_t addr, uint8_t initial_val);
void watchpoint_remove(uint16_t addr);
void watchpoint_check(uint16_t addr, uint8_t old_val, uint8_t new_val);

/* ========================================================================
 * 4. Runtime-toggleable debug flags
 * ======================================================================== */

extern int g_debug_dispatch;
extern int g_debug_watchpoints;

#endif /* POKEMON_DEBUG_H */
