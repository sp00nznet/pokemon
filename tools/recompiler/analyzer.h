#ifndef ANALYZER_H
#define ANALYZER_H

#include <stdint.h>
#include <stdbool.h>
#include "decoder.h"
#include "symbols.h"

/* Control flow analysis for SM83 ROM code */

/* Maximum number of basic blocks per bank */
#define MAX_BLOCKS_PER_BANK  4096
#define MAX_FUNCTIONS_PER_BANK 1024
#define MAX_SUCCESSORS 8

/* A basic block is a straight-line sequence of instructions with
 * one entry point and one exit point */
typedef struct basic_block {
    uint16_t start_addr;        /* Address of first instruction */
    uint16_t end_addr;          /* Address after last instruction */
    uint8_t  bank;              /* ROM bank this block belongs to */
    int      inst_count;        /* Number of instructions */
    bool     is_data;           /* True if this is a data region */

    /* Control flow edges */
    int      num_successors;
    uint16_t successor_addr[MAX_SUCCESSORS];
    uint8_t  successor_bank[MAX_SUCCESSORS];

    /* Terminating instruction info */
    sm83_branch_t exit_type;
    bool     has_fallthrough;   /* Falls through to next block */
    bool     is_entry_point;    /* Interrupt vector or known entry */
    bool     visited;           /* Used during analysis */

    /* Function membership */
    int      function_id;       /* Which function this block belongs to (-1 = none) */
} basic_block_t;

/* A function is a collection of basic blocks reachable from a CALL target */
typedef struct function_info {
    uint16_t entry_addr;        /* First instruction address */
    uint8_t  bank;
    const char *name;           /* Symbol name (NULL if unnamed) */
    int      block_count;       /* Number of basic blocks */
    int      first_block_idx;   /* Index of entry block in blocks array */
    bool     is_interrupt;      /* Is an interrupt handler */
    bool     has_bank_switch;   /* Contains bank switch patterns */
} function_info_t;

/* Analysis result for one bank */
typedef struct {
    basic_block_t  blocks[MAX_BLOCKS_PER_BANK];
    int            block_count;
    function_info_t functions[MAX_FUNCTIONS_PER_BANK];
    int            function_count;

    /* Bitmap of addresses known to be code (vs data) */
    bool           is_code[BANK_SIZE];
    /* Bitmap of addresses that are instruction starts (first byte only) */
    bool           is_inst_start[BANK_SIZE];
    uint8_t        bank;
} bank_analysis_t;

/* Full ROM analysis context */
typedef struct {
    const uint8_t *rom_data;
    size_t         rom_size;
    int            num_banks;
    const char    *game_name;   /* "red", "blue", or "yellow" */
    bank_analysis_t *banks;     /* Array of num_banks analyses */

    /* Cross-bank call targets discovered during analysis */
    struct {
        uint16_t addr;
        uint8_t  bank;
    } xbank_calls[8192];
    int xbank_call_count;
} analysis_ctx_t;

/* Initialize analysis context */
void analysis_init(analysis_ctx_t *ctx, const uint8_t *rom, size_t rom_size, int num_banks, const char *game_name);

/* Load trace file and add entry points as function seeds.
 * Trace format: one "bank:addr" pair per line (hex). */
void analysis_load_trace(analysis_ctx_t *ctx, const char *trace_file);

/* Run recursive descent analysis starting from known entry points */
void analysis_run(analysis_ctx_t *ctx);

/* Analyze a single bank (called by analysis_run) */
void analysis_run_bank(analysis_ctx_t *ctx, int bank);

/* Free analysis context resources */
void analysis_free(analysis_ctx_t *ctx);

/* Find a basic block by address and bank, or return NULL */
basic_block_t *analysis_find_block(analysis_ctx_t *ctx, uint8_t bank, uint16_t addr);

/* Print analysis summary */
void analysis_print_summary(const analysis_ctx_t *ctx);

#endif /* ANALYZER_H */
