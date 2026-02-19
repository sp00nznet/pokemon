#ifndef CODEGEN_H
#define CODEGEN_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "analyzer.h"

/* C code generation from analyzed SM83 ROM */

typedef struct {
    const analysis_ctx_t *analysis;
    const uint8_t *rom_data;
    size_t rom_size;
    const char *output_dir;     /* Base output directory */
    const char *game_name;      /* "red", "blue", or "yellow" */
    bool emit_debug_comments;   /* Include address comments */
} codegen_ctx_t;

/* Initialize code generation context */
void codegen_init(codegen_ctx_t *ctx, const analysis_ctx_t *analysis,
                  const uint8_t *rom, size_t rom_size,
                  const char *output_dir, const char *game_name);

/* Generate C source for one bank */
int codegen_emit_bank(codegen_ctx_t *ctx, int bank);

/* Generate dispatch table (cross-bank call resolution) */
int codegen_emit_dispatch(codegen_ctx_t *ctx);

/* Generate all banks + dispatch */
int codegen_emit_all(codegen_ctx_t *ctx);

/* Emit a single SM83 instruction as C code to a file */
void codegen_emit_instruction(FILE *f, const sm83_inst_t *inst,
                              uint16_t addr, uint8_t imm8, uint16_t imm16,
                              int indent, int bank,
                              const uint16_t *valid_labels, int num_valid_labels);

#endif /* CODEGEN_H */
