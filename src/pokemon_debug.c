/*
 * pokemon_debug.c - Debug infrastructure implementation
 *
 * Dispatch trace ring buffer + memory watchpoints.
 */

#include "pokemon_debug.h"
#include <string.h>

/* ========================================================================
 * Dispatch trace ring buffer
 * ======================================================================== */

dtrace_state_t g_dtrace = {0};

void dtrace_dump(const char *reason) {
    fprintf(stderr, "\n=== DISPATCH TRACE DUMP ===\n");
    if (reason)
        fprintf(stderr, "Reason: %s\n", reason);

    uint32_t total = g_dtrace.count;
    uint32_t n = total < DTRACE_SIZE ? total : DTRACE_SIZE;

    if (n == 0) {
        fprintf(stderr, "(empty)\n");
        return;
    }

    fprintf(stderr, "Last %u dispatch events (of %u total):\n", n, total);

    uint32_t start = g_dtrace.head - n;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t idx = (start + i) & (DTRACE_SIZE - 1);
        dtrace_entry_t *e = &g_dtrace.entries[idx];
        fprintf(stderr, "  [%3u] %c bank=%02X addr=%04X  A=%02X SP=%04X HL=%04X\n",
                total - n + i, e->type, e->bank, e->addr,
                e->a, e->sp, e->hl);
    }
    fprintf(stderr, "=== END TRACE ===\n\n");
}

/* ========================================================================
 * Memory watchpoints
 * ======================================================================== */

mem_watchpoint_t g_watchpoints[MAX_WATCHPOINTS] = {0};
int g_watchpoint_count = 0;

int g_debug_dispatch = 0;
int g_debug_watchpoints = 0;

void watchpoint_add(uint16_t addr, uint8_t initial_val) {
    if (g_watchpoint_count >= MAX_WATCHPOINTS) {
        fprintf(stderr, "WARNING: Max watchpoints (%d) reached, cannot add %04X\n",
                MAX_WATCHPOINTS, addr);
        return;
    }
    for (int i = 0; i < g_watchpoint_count; i++) {
        if (g_watchpoints[i].addr == addr && g_watchpoints[i].active) {
            g_watchpoints[i].prev_val = initial_val;
            return;
        }
    }
    g_watchpoints[g_watchpoint_count].addr = addr;
    g_watchpoints[g_watchpoint_count].prev_val = initial_val;
    g_watchpoints[g_watchpoint_count].active = 1;
    g_watchpoint_count++;
    fprintf(stderr, "WATCHPOINT: added %04X (initial=%02X)\n", addr, initial_val);
}

void watchpoint_remove(uint16_t addr) {
    for (int i = 0; i < g_watchpoint_count; i++) {
        if (g_watchpoints[i].addr == addr) {
            g_watchpoints[i].active = 0;
            fprintf(stderr, "WATCHPOINT: removed %04X\n", addr);
            return;
        }
    }
}

void watchpoint_check(uint16_t addr, uint8_t old_val, uint8_t new_val) {
    for (int i = 0; i < g_watchpoint_count; i++) {
        if (g_watchpoints[i].active && g_watchpoints[i].addr == addr) {
            if (old_val != new_val) {
                fprintf(stderr, "WATCHPOINT HIT: [%04X] %02X -> %02X\n",
                        addr, old_val, new_val);
                dtrace_dump("watchpoint triggered");
            }
            g_watchpoints[i].prev_val = new_val;
            return;
        }
    }
}
