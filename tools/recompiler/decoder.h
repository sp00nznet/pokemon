#ifndef DECODER_H
#define DECODER_H

#include <stdint.h>
#include <stdbool.h>

/* SM83 (Game Boy CPU) instruction decoder */

typedef enum {
    OP_NOP, OP_STOP, OP_HALT, OP_DI, OP_EI,
    OP_LD, OP_LDH, OP_PUSH, OP_POP,
    OP_ADD, OP_ADC, OP_SUB, OP_SBC, OP_AND, OP_XOR, OP_OR, OP_CP,
    OP_INC, OP_DEC,
    OP_DAA, OP_CPL, OP_SCF, OP_CCF,
    OP_RLCA, OP_RRCA, OP_RLA, OP_RRA,
    OP_JP, OP_JR, OP_CALL, OP_RET, OP_RETI, OP_RST,
    /* CB-prefix */
    OP_RLC, OP_RRC, OP_RL, OP_RR,
    OP_SLA, OP_SRA, OP_SWAP, OP_SRL,
    OP_BIT, OP_RES, OP_SET,
    /* Special */
    OP_INVALID
} sm83_mnemonic_t;

typedef enum {
    OPERAND_NONE = 0,
    /* 8-bit registers */
    OPERAND_A, OPERAND_B, OPERAND_C, OPERAND_D, OPERAND_E, OPERAND_H, OPERAND_L,
    /* 16-bit registers */
    OPERAND_AF, OPERAND_BC, OPERAND_DE, OPERAND_HL, OPERAND_SP,
    /* Indirect */
    OPERAND_IND_BC,   /* (BC) */
    OPERAND_IND_DE,   /* (DE) */
    OPERAND_IND_HL,   /* (HL) */
    OPERAND_IND_HLI,  /* (HL+) */
    OPERAND_IND_HLD,  /* (HL-) */
    OPERAND_IND_C,    /* (0xFF00+C) */
    /* Immediates */
    OPERAND_IMM8,     /* d8 / r8 (signed) */
    OPERAND_IMM16,    /* d16 / a16 */
    OPERAND_IND_IMM8, /* (0xFF00+d8) */
    OPERAND_IND_IMM16,/* (a16) */
    OPERAND_SP_R8,    /* SP+r8 */
    /* Conditions */
    OPERAND_COND_NZ, OPERAND_COND_Z, OPERAND_COND_NC, OPERAND_COND_C,
    /* Bit index (0-7 for CB prefix) */
    OPERAND_BIT0, OPERAND_BIT1, OPERAND_BIT2, OPERAND_BIT3,
    OPERAND_BIT4, OPERAND_BIT5, OPERAND_BIT6, OPERAND_BIT7,
    /* RST vectors */
    OPERAND_RST_00, OPERAND_RST_08, OPERAND_RST_10, OPERAND_RST_18,
    OPERAND_RST_20, OPERAND_RST_28, OPERAND_RST_30, OPERAND_RST_38
} sm83_operand_t;

typedef enum {
    BRANCH_NONE = 0,       /* Not a branch */
    BRANCH_JUMP,           /* Unconditional JP/JR */
    BRANCH_JUMP_COND,      /* Conditional JP/JR */
    BRANCH_CALL,           /* Unconditional CALL */
    BRANCH_CALL_COND,      /* Conditional CALL */
    BRANCH_RET,            /* Unconditional RET/RETI */
    BRANCH_RET_COND,       /* Conditional RET */
    BRANCH_RST,            /* RST vector call */
    BRANCH_JUMP_INDIRECT,  /* JP (HL) */
    BRANCH_HALT,           /* HALT - waits for interrupt */
    BRANCH_STOP            /* STOP */
} sm83_branch_t;

typedef struct {
    sm83_mnemonic_t mnemonic;
    sm83_operand_t  op1;
    sm83_operand_t  op2;
    uint8_t         length;     /* Instruction length in bytes (1-3) */
    uint8_t         cycles;     /* Base cycles (not-taken for conditional) */
    uint8_t         cycles_taken; /* Cycles when branch taken (0 if N/A) */
    sm83_branch_t   branch;
    bool            cb_prefix;  /* true if CB-prefixed instruction */
    uint8_t         opcode;     /* Raw opcode byte */
} sm83_inst_t;

/* Decode a single instruction from ROM data.
 * data points to the opcode byte, data_len is remaining bytes available.
 * imm8/imm16 are filled with any immediate values read.
 * Returns decoded instruction info. */
sm83_inst_t sm83_decode(const uint8_t *data, size_t data_len,
                        uint8_t *imm8, uint16_t *imm16);

/* Get human-readable mnemonic string */
const char *sm83_mnemonic_str(sm83_mnemonic_t m);

/* Get human-readable operand string */
const char *sm83_operand_str(sm83_operand_t op);

/* Format a decoded instruction as a string (for disassembly listing).
 * buf must be at least 64 bytes. Returns buf. */
char *sm83_format(char *buf, size_t bufsz, const sm83_inst_t *inst,
                  uint16_t addr, uint8_t imm8, uint16_t imm16);

#endif /* DECODER_H */
