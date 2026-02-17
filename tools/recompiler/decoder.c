#include "decoder.h"
#include <stdio.h>
#include <string.h>

/* Register lookup tables for opcode bit patterns */
static const sm83_operand_t reg8_table[8] = {
    OPERAND_B, OPERAND_C, OPERAND_D, OPERAND_E,
    OPERAND_H, OPERAND_L, OPERAND_IND_HL, OPERAND_A
};

static const sm83_operand_t reg16_table[4] = {
    OPERAND_BC, OPERAND_DE, OPERAND_HL, OPERAND_SP
};

static const sm83_operand_t reg16_push_table[4] = {
    OPERAND_BC, OPERAND_DE, OPERAND_HL, OPERAND_AF
};

static const sm83_operand_t cond_table[4] = {
    OPERAND_COND_NZ, OPERAND_COND_Z, OPERAND_COND_NC, OPERAND_COND_C
};

static const sm83_operand_t rst_table[8] = {
    OPERAND_RST_00, OPERAND_RST_08, OPERAND_RST_10, OPERAND_RST_18,
    OPERAND_RST_20, OPERAND_RST_28, OPERAND_RST_30, OPERAND_RST_38
};

static const sm83_operand_t bit_table[8] = {
    OPERAND_BIT0, OPERAND_BIT1, OPERAND_BIT2, OPERAND_BIT3,
    OPERAND_BIT4, OPERAND_BIT5, OPERAND_BIT6, OPERAND_BIT7
};

/* Decode CB-prefixed instruction */
static sm83_inst_t decode_cb(uint8_t op) {
    sm83_inst_t inst = {0};
    inst.cb_prefix = true;
    inst.opcode = op;
    inst.length = 2; /* CB prefix + opcode */
    inst.branch = BRANCH_NONE;

    uint8_t r = op & 0x07;
    uint8_t b = (op >> 3) & 0x07;
    bool is_hl = (r == 6); /* (HL) */

    if (op < 0x08) {
        inst.mnemonic = OP_RLC;
        inst.op1 = reg8_table[r];
        inst.cycles = is_hl ? 16 : 8;
    } else if (op < 0x10) {
        inst.mnemonic = OP_RRC;
        inst.op1 = reg8_table[r];
        inst.cycles = is_hl ? 16 : 8;
    } else if (op < 0x18) {
        inst.mnemonic = OP_RL;
        inst.op1 = reg8_table[r];
        inst.cycles = is_hl ? 16 : 8;
    } else if (op < 0x20) {
        inst.mnemonic = OP_RR;
        inst.op1 = reg8_table[r];
        inst.cycles = is_hl ? 16 : 8;
    } else if (op < 0x28) {
        inst.mnemonic = OP_SLA;
        inst.op1 = reg8_table[r];
        inst.cycles = is_hl ? 16 : 8;
    } else if (op < 0x30) {
        inst.mnemonic = OP_SRA;
        inst.op1 = reg8_table[r];
        inst.cycles = is_hl ? 16 : 8;
    } else if (op < 0x38) {
        inst.mnemonic = OP_SWAP;
        inst.op1 = reg8_table[r];
        inst.cycles = is_hl ? 16 : 8;
    } else if (op < 0x40) {
        inst.mnemonic = OP_SRL;
        inst.op1 = reg8_table[r];
        inst.cycles = is_hl ? 16 : 8;
    } else if (op < 0x80) {
        inst.mnemonic = OP_BIT;
        inst.op1 = bit_table[b];
        inst.op2 = reg8_table[r];
        inst.cycles = is_hl ? 12 : 8;
    } else if (op < 0xC0) {
        inst.mnemonic = OP_RES;
        inst.op1 = bit_table[b];
        inst.op2 = reg8_table[r];
        inst.cycles = is_hl ? 16 : 8;
    } else {
        inst.mnemonic = OP_SET;
        inst.op1 = bit_table[b];
        inst.op2 = reg8_table[r];
        inst.cycles = is_hl ? 16 : 8;
    }
    return inst;
}

sm83_inst_t sm83_decode(const uint8_t *data, size_t data_len,
                        uint8_t *imm8, uint16_t *imm16) {
    sm83_inst_t inst = {0};
    *imm8 = 0;
    *imm16 = 0;

    if (data_len == 0) {
        inst.mnemonic = OP_INVALID;
        inst.length = 1;
        inst.cycles = 4;
        return inst;
    }

    uint8_t op = data[0];
    inst.opcode = op;
    inst.branch = BRANCH_NONE;

    /* CB prefix */
    if (op == 0xCB) {
        if (data_len < 2) {
            inst.mnemonic = OP_INVALID;
            inst.length = 1;
            inst.cycles = 4;
            return inst;
        }
        inst = decode_cb(data[1]);
        return inst;
    }

    /* Extract common bit fields */
    uint8_t x = (op >> 6) & 0x03;  /* bits 7-6 */
    uint8_t y = (op >> 3) & 0x07;  /* bits 5-3 */
    uint8_t z = op & 0x07;         /* bits 2-0 */
    uint8_t p = (y >> 1) & 0x03;   /* bits 5-4 */
    uint8_t q = y & 0x01;          /* bit 3 */

    switch (x) {
    case 0:
        switch (z) {
        case 0:
            switch (y) {
            case 0: /* NOP */
                inst.mnemonic = OP_NOP;
                inst.length = 1; inst.cycles = 4;
                break;
            case 1: /* LD (a16), SP */
                inst.mnemonic = OP_LD;
                inst.op1 = OPERAND_IND_IMM16;
                inst.op2 = OPERAND_SP;
                inst.length = 3; inst.cycles = 20;
                if (data_len >= 3) *imm16 = data[1] | (data[2] << 8);
                break;
            case 2: /* STOP */
                inst.mnemonic = OP_STOP;
                inst.length = 2; inst.cycles = 4;
                inst.branch = BRANCH_STOP;
                break;
            case 3: /* JR r8 */
                inst.mnemonic = OP_JR;
                inst.op1 = OPERAND_IMM8;
                inst.length = 2; inst.cycles = 12;
                inst.branch = BRANCH_JUMP;
                if (data_len >= 2) *imm8 = data[1];
                break;
            case 4: case 5: case 6: case 7: /* JR cc, r8 */
                inst.mnemonic = OP_JR;
                inst.op1 = cond_table[y - 4];
                inst.op2 = OPERAND_IMM8;
                inst.length = 2;
                inst.cycles = 8; inst.cycles_taken = 12;
                inst.branch = BRANCH_JUMP_COND;
                if (data_len >= 2) *imm8 = data[1];
                break;
            }
            break;
        case 1:
            if (q == 0) { /* LD rr, d16 */
                inst.mnemonic = OP_LD;
                inst.op1 = reg16_table[p];
                inst.op2 = OPERAND_IMM16;
                inst.length = 3; inst.cycles = 12;
                if (data_len >= 3) *imm16 = data[1] | (data[2] << 8);
            } else { /* ADD HL, rr */
                inst.mnemonic = OP_ADD;
                inst.op1 = OPERAND_HL;
                inst.op2 = reg16_table[p];
                inst.length = 1; inst.cycles = 8;
            }
            break;
        case 2:
            if (q == 0) {
                switch (p) {
                case 0: /* LD (BC), A */
                    inst.mnemonic = OP_LD;
                    inst.op1 = OPERAND_IND_BC;
                    inst.op2 = OPERAND_A;
                    inst.length = 1; inst.cycles = 8;
                    break;
                case 1: /* LD (DE), A */
                    inst.mnemonic = OP_LD;
                    inst.op1 = OPERAND_IND_DE;
                    inst.op2 = OPERAND_A;
                    inst.length = 1; inst.cycles = 8;
                    break;
                case 2: /* LD (HL+), A */
                    inst.mnemonic = OP_LD;
                    inst.op1 = OPERAND_IND_HLI;
                    inst.op2 = OPERAND_A;
                    inst.length = 1; inst.cycles = 8;
                    break;
                case 3: /* LD (HL-), A */
                    inst.mnemonic = OP_LD;
                    inst.op1 = OPERAND_IND_HLD;
                    inst.op2 = OPERAND_A;
                    inst.length = 1; inst.cycles = 8;
                    break;
                }
            } else {
                switch (p) {
                case 0: /* LD A, (BC) */
                    inst.mnemonic = OP_LD;
                    inst.op1 = OPERAND_A;
                    inst.op2 = OPERAND_IND_BC;
                    inst.length = 1; inst.cycles = 8;
                    break;
                case 1: /* LD A, (DE) */
                    inst.mnemonic = OP_LD;
                    inst.op1 = OPERAND_A;
                    inst.op2 = OPERAND_IND_DE;
                    inst.length = 1; inst.cycles = 8;
                    break;
                case 2: /* LD A, (HL+) */
                    inst.mnemonic = OP_LD;
                    inst.op1 = OPERAND_A;
                    inst.op2 = OPERAND_IND_HLI;
                    inst.length = 1; inst.cycles = 8;
                    break;
                case 3: /* LD A, (HL-) */
                    inst.mnemonic = OP_LD;
                    inst.op1 = OPERAND_A;
                    inst.op2 = OPERAND_IND_HLD;
                    inst.length = 1; inst.cycles = 8;
                    break;
                }
            }
            break;
        case 3:
            if (q == 0) { /* INC rr */
                inst.mnemonic = OP_INC;
                inst.op1 = reg16_table[p];
                inst.length = 1; inst.cycles = 8;
            } else { /* DEC rr */
                inst.mnemonic = OP_DEC;
                inst.op1 = reg16_table[p];
                inst.length = 1; inst.cycles = 8;
            }
            break;
        case 4: /* INC r */
            inst.mnemonic = OP_INC;
            inst.op1 = reg8_table[y];
            inst.length = 1;
            inst.cycles = (y == 6) ? 12 : 4;
            break;
        case 5: /* DEC r */
            inst.mnemonic = OP_DEC;
            inst.op1 = reg8_table[y];
            inst.length = 1;
            inst.cycles = (y == 6) ? 12 : 4;
            break;
        case 6: /* LD r, d8 */
            inst.mnemonic = OP_LD;
            inst.op1 = reg8_table[y];
            inst.op2 = OPERAND_IMM8;
            inst.length = 2;
            inst.cycles = (y == 6) ? 12 : 8;
            if (data_len >= 2) *imm8 = data[1];
            break;
        case 7:
            switch (y) {
            case 0: inst.mnemonic = OP_RLCA; inst.length = 1; inst.cycles = 4; break;
            case 1: inst.mnemonic = OP_RRCA; inst.length = 1; inst.cycles = 4; break;
            case 2: inst.mnemonic = OP_RLA;  inst.length = 1; inst.cycles = 4; break;
            case 3: inst.mnemonic = OP_RRA;  inst.length = 1; inst.cycles = 4; break;
            case 4: inst.mnemonic = OP_DAA;  inst.length = 1; inst.cycles = 4; break;
            case 5: inst.mnemonic = OP_CPL;  inst.length = 1; inst.cycles = 4; break;
            case 6: inst.mnemonic = OP_SCF;  inst.length = 1; inst.cycles = 4; break;
            case 7: inst.mnemonic = OP_CCF;  inst.length = 1; inst.cycles = 4; break;
            }
            break;
        }
        break;

    case 1:
        if (op == 0x76) { /* HALT */
            inst.mnemonic = OP_HALT;
            inst.length = 1; inst.cycles = 4;
            inst.branch = BRANCH_HALT;
        } else { /* LD r, r' */
            inst.mnemonic = OP_LD;
            inst.op1 = reg8_table[y];
            inst.op2 = reg8_table[z];
            inst.length = 1;
            /* 8 cycles if either operand is (HL), else 4 */
            inst.cycles = (y == 6 || z == 6) ? 8 : 4;
        }
        break;

    case 2: /* ALU A, r */
        inst.op1 = reg8_table[z];
        inst.length = 1;
        inst.cycles = (z == 6) ? 8 : 4;
        switch (y) {
        case 0: inst.mnemonic = OP_ADD; break;
        case 1: inst.mnemonic = OP_ADC; break;
        case 2: inst.mnemonic = OP_SUB; break;
        case 3: inst.mnemonic = OP_SBC; break;
        case 4: inst.mnemonic = OP_AND; break;
        case 5: inst.mnemonic = OP_XOR; break;
        case 6: inst.mnemonic = OP_OR;  break;
        case 7: inst.mnemonic = OP_CP;  break;
        }
        break;

    case 3:
        switch (z) {
        case 0:
            switch (y) {
            case 0: case 1: case 2: case 3: /* RET cc */
                inst.mnemonic = OP_RET;
                inst.op1 = cond_table[y];
                inst.length = 1;
                inst.cycles = 8; inst.cycles_taken = 20;
                inst.branch = BRANCH_RET_COND;
                break;
            case 4: /* LDH (a8), A */
                inst.mnemonic = OP_LDH;
                inst.op1 = OPERAND_IND_IMM8;
                inst.op2 = OPERAND_A;
                inst.length = 2; inst.cycles = 12;
                if (data_len >= 2) *imm8 = data[1];
                break;
            case 5: /* ADD SP, r8 */
                inst.mnemonic = OP_ADD;
                inst.op1 = OPERAND_SP;
                inst.op2 = OPERAND_IMM8;
                inst.length = 2; inst.cycles = 16;
                if (data_len >= 2) *imm8 = data[1];
                break;
            case 6: /* LDH A, (a8) */
                inst.mnemonic = OP_LDH;
                inst.op1 = OPERAND_A;
                inst.op2 = OPERAND_IND_IMM8;
                inst.length = 2; inst.cycles = 12;
                if (data_len >= 2) *imm8 = data[1];
                break;
            case 7: /* LD HL, SP+r8 */
                inst.mnemonic = OP_LD;
                inst.op1 = OPERAND_HL;
                inst.op2 = OPERAND_SP_R8;
                inst.length = 2; inst.cycles = 12;
                if (data_len >= 2) *imm8 = data[1];
                break;
            }
            break;
        case 1:
            if (q == 0) { /* POP rr */
                inst.mnemonic = OP_POP;
                inst.op1 = reg16_push_table[p];
                inst.length = 1; inst.cycles = 12;
            } else {
                switch (p) {
                case 0: /* RET */
                    inst.mnemonic = OP_RET;
                    inst.length = 1; inst.cycles = 16;
                    inst.branch = BRANCH_RET;
                    break;
                case 1: /* RETI */
                    inst.mnemonic = OP_RETI;
                    inst.length = 1; inst.cycles = 16;
                    inst.branch = BRANCH_RET;
                    break;
                case 2: /* JP (HL) */
                    inst.mnemonic = OP_JP;
                    inst.op1 = OPERAND_IND_HL;
                    inst.length = 1; inst.cycles = 4;
                    inst.branch = BRANCH_JUMP_INDIRECT;
                    break;
                case 3: /* LD SP, HL */
                    inst.mnemonic = OP_LD;
                    inst.op1 = OPERAND_SP;
                    inst.op2 = OPERAND_HL;
                    inst.length = 1; inst.cycles = 8;
                    break;
                }
            }
            break;
        case 2:
            switch (y) {
            case 0: case 1: case 2: case 3: /* JP cc, a16 */
                inst.mnemonic = OP_JP;
                inst.op1 = cond_table[y];
                inst.op2 = OPERAND_IMM16;
                inst.length = 3;
                inst.cycles = 12; inst.cycles_taken = 16;
                inst.branch = BRANCH_JUMP_COND;
                if (data_len >= 3) *imm16 = data[1] | (data[2] << 8);
                break;
            case 4: /* LD (0xFF00+C), A */
                inst.mnemonic = OP_LD;
                inst.op1 = OPERAND_IND_C;
                inst.op2 = OPERAND_A;
                inst.length = 1; inst.cycles = 8;
                break;
            case 5: /* LD (a16), A */
                inst.mnemonic = OP_LD;
                inst.op1 = OPERAND_IND_IMM16;
                inst.op2 = OPERAND_A;
                inst.length = 3; inst.cycles = 16;
                if (data_len >= 3) *imm16 = data[1] | (data[2] << 8);
                break;
            case 6: /* LD A, (0xFF00+C) */
                inst.mnemonic = OP_LD;
                inst.op1 = OPERAND_A;
                inst.op2 = OPERAND_IND_C;
                inst.length = 1; inst.cycles = 8;
                break;
            case 7: /* LD A, (a16) */
                inst.mnemonic = OP_LD;
                inst.op1 = OPERAND_A;
                inst.op2 = OPERAND_IND_IMM16;
                inst.length = 3; inst.cycles = 16;
                if (data_len >= 3) *imm16 = data[1] | (data[2] << 8);
                break;
            }
            break;
        case 3:
            switch (y) {
            case 0: /* JP a16 */
                inst.mnemonic = OP_JP;
                inst.op1 = OPERAND_IMM16;
                inst.length = 3; inst.cycles = 16;
                inst.branch = BRANCH_JUMP;
                if (data_len >= 3) *imm16 = data[1] | (data[2] << 8);
                break;
            case 1: /* CB prefix - handled above, shouldn't reach here */
                inst.mnemonic = OP_INVALID;
                inst.length = 1; inst.cycles = 4;
                break;
            case 6: /* DI */
                inst.mnemonic = OP_DI;
                inst.length = 1; inst.cycles = 4;
                break;
            case 7: /* EI */
                inst.mnemonic = OP_EI;
                inst.length = 1; inst.cycles = 4;
                break;
            default: /* Illegal opcodes: 0xD3, 0xDB, 0xDD, 0xE3, 0xE4, 0xEB, 0xEC, 0xED */
                inst.mnemonic = OP_INVALID;
                inst.length = 1; inst.cycles = 4;
                break;
            }
            break;
        case 4:
            if (y <= 3) { /* CALL cc, a16 */
                inst.mnemonic = OP_CALL;
                inst.op1 = cond_table[y];
                inst.op2 = OPERAND_IMM16;
                inst.length = 3;
                inst.cycles = 12; inst.cycles_taken = 24;
                inst.branch = BRANCH_CALL_COND;
                if (data_len >= 3) *imm16 = data[1] | (data[2] << 8);
            } else { /* Illegal: 0xE4, 0xEC, 0xF4, 0xFC */
                inst.mnemonic = OP_INVALID;
                inst.length = 1; inst.cycles = 4;
            }
            break;
        case 5:
            if (q == 0) { /* PUSH rr */
                inst.mnemonic = OP_PUSH;
                inst.op1 = reg16_push_table[p];
                inst.length = 1; inst.cycles = 16;
            } else if (p == 0) { /* CALL a16 */
                inst.mnemonic = OP_CALL;
                inst.op1 = OPERAND_IMM16;
                inst.length = 3; inst.cycles = 24;
                inst.branch = BRANCH_CALL;
                if (data_len >= 3) *imm16 = data[1] | (data[2] << 8);
            } else { /* Illegal: 0xDD, 0xED, 0xFD */
                inst.mnemonic = OP_INVALID;
                inst.length = 1; inst.cycles = 4;
            }
            break;
        case 6: /* ALU A, d8 */
            inst.op1 = OPERAND_IMM8;
            inst.length = 2; inst.cycles = 8;
            if (data_len >= 2) *imm8 = data[1];
            switch (y) {
            case 0: inst.mnemonic = OP_ADD; break;
            case 1: inst.mnemonic = OP_ADC; break;
            case 2: inst.mnemonic = OP_SUB; break;
            case 3: inst.mnemonic = OP_SBC; break;
            case 4: inst.mnemonic = OP_AND; break;
            case 5: inst.mnemonic = OP_XOR; break;
            case 6: inst.mnemonic = OP_OR;  break;
            case 7: inst.mnemonic = OP_CP;  break;
            }
            break;
        case 7: /* RST */
            inst.mnemonic = OP_RST;
            inst.op1 = rst_table[y];
            inst.length = 1; inst.cycles = 16;
            inst.branch = BRANCH_RST;
            break;
        }
        break;
    }

    return inst;
}

const char *sm83_mnemonic_str(sm83_mnemonic_t m) {
    static const char *names[] = {
        [OP_NOP]  = "NOP",  [OP_STOP] = "STOP", [OP_HALT] = "HALT",
        [OP_DI]   = "DI",   [OP_EI]   = "EI",
        [OP_LD]   = "LD",   [OP_LDH]  = "LDH",
        [OP_PUSH] = "PUSH", [OP_POP]  = "POP",
        [OP_ADD]  = "ADD",  [OP_ADC]  = "ADC",
        [OP_SUB]  = "SUB",  [OP_SBC]  = "SBC",
        [OP_AND]  = "AND",  [OP_XOR]  = "XOR",
        [OP_OR]   = "OR",   [OP_CP]   = "CP",
        [OP_INC]  = "INC",  [OP_DEC]  = "DEC",
        [OP_DAA]  = "DAA",  [OP_CPL]  = "CPL",
        [OP_SCF]  = "SCF",  [OP_CCF]  = "CCF",
        [OP_RLCA] = "RLCA", [OP_RRCA] = "RRCA",
        [OP_RLA]  = "RLA",  [OP_RRA]  = "RRA",
        [OP_JP]   = "JP",   [OP_JR]   = "JR",
        [OP_CALL] = "CALL", [OP_RET]  = "RET",
        [OP_RETI] = "RETI", [OP_RST]  = "RST",
        [OP_RLC]  = "RLC",  [OP_RRC]  = "RRC",
        [OP_RL]   = "RL",   [OP_RR]   = "RR",
        [OP_SLA]  = "SLA",  [OP_SRA]  = "SRA",
        [OP_SWAP] = "SWAP", [OP_SRL]  = "SRL",
        [OP_BIT]  = "BIT",  [OP_RES]  = "RES",
        [OP_SET]  = "SET",
        [OP_INVALID] = "???",
    };
    return (m <= OP_INVALID) ? names[m] : "???";
}

const char *sm83_operand_str(sm83_operand_t op) {
    static const char *names[] = {
        [OPERAND_NONE]       = "",
        [OPERAND_A]          = "A",    [OPERAND_B]    = "B",
        [OPERAND_C]          = "C",    [OPERAND_D]    = "D",
        [OPERAND_E]          = "E",    [OPERAND_H]    = "H",
        [OPERAND_L]          = "L",
        [OPERAND_AF]         = "AF",   [OPERAND_BC]   = "BC",
        [OPERAND_DE]         = "DE",   [OPERAND_HL]   = "HL",
        [OPERAND_SP]         = "SP",
        [OPERAND_IND_BC]     = "(BC)", [OPERAND_IND_DE]  = "(DE)",
        [OPERAND_IND_HL]     = "(HL)", [OPERAND_IND_HLI] = "(HL+)",
        [OPERAND_IND_HLD]    = "(HL-)",[OPERAND_IND_C]   = "(C)",
        [OPERAND_IMM8]       = "d8",   [OPERAND_IMM16]    = "d16",
        [OPERAND_IND_IMM8]   = "(a8)", [OPERAND_IND_IMM16]= "(a16)",
        [OPERAND_SP_R8]      = "SP+r8",
        [OPERAND_COND_NZ]    = "NZ",   [OPERAND_COND_Z]  = "Z",
        [OPERAND_COND_NC]    = "NC",   [OPERAND_COND_C]  = "C",
        [OPERAND_BIT0]       = "0",    [OPERAND_BIT1]    = "1",
        [OPERAND_BIT2]       = "2",    [OPERAND_BIT3]    = "3",
        [OPERAND_BIT4]       = "4",    [OPERAND_BIT5]    = "5",
        [OPERAND_BIT6]       = "6",    [OPERAND_BIT7]    = "7",
        [OPERAND_RST_00]     = "$00",  [OPERAND_RST_08]  = "$08",
        [OPERAND_RST_10]     = "$10",  [OPERAND_RST_18]  = "$18",
        [OPERAND_RST_20]     = "$20",  [OPERAND_RST_28]  = "$28",
        [OPERAND_RST_30]     = "$30",  [OPERAND_RST_38]  = "$38",
    };
    return names[op];
}

static void format_operand(char *buf, size_t bufsz, sm83_operand_t op,
                           uint16_t addr, uint8_t imm8_val, uint16_t imm16_val,
                           bool is_jr) {
    switch (op) {
    case OPERAND_IMM8:
        if (is_jr) {
            int8_t offset = (int8_t)imm8_val;
            snprintf(buf, bufsz, "$%04X", (uint16_t)(addr + 2 + offset));
        } else {
            snprintf(buf, bufsz, "$%02X", imm8_val);
        }
        break;
    case OPERAND_IMM16:
        snprintf(buf, bufsz, "$%04X", imm16_val);
        break;
    case OPERAND_IND_IMM8:
        snprintf(buf, bufsz, "($FF00+$%02X)", imm8_val);
        break;
    case OPERAND_IND_IMM16:
        snprintf(buf, bufsz, "($%04X)", imm16_val);
        break;
    case OPERAND_SP_R8:
        snprintf(buf, bufsz, "SP+$%02X", imm8_val);
        break;
    case OPERAND_IND_HL:
        /* In JP (HL) context, display as (HL) */
        snprintf(buf, bufsz, "(HL)");
        break;
    default:
        snprintf(buf, bufsz, "%s", sm83_operand_str(op));
        break;
    }
}

char *sm83_format(char *buf, size_t bufsz, const sm83_inst_t *inst,
                  uint16_t addr, uint8_t imm8_val, uint16_t imm16_val) {
    char op1_buf[32] = {0};
    char op2_buf[32] = {0};
    bool is_jr = (inst->mnemonic == OP_JR);

    if (inst->op1 != OPERAND_NONE) {
        format_operand(op1_buf, sizeof(op1_buf), inst->op1,
                       addr, imm8_val, imm16_val, is_jr);
    }
    if (inst->op2 != OPERAND_NONE) {
        format_operand(op2_buf, sizeof(op2_buf), inst->op2,
                       addr, imm8_val, imm16_val, is_jr);
    }

    if (inst->op2 != OPERAND_NONE) {
        snprintf(buf, bufsz, "%-4s %s, %s",
                 sm83_mnemonic_str(inst->mnemonic), op1_buf, op2_buf);
    } else if (inst->op1 != OPERAND_NONE) {
        snprintf(buf, bufsz, "%-4s %s",
                 sm83_mnemonic_str(inst->mnemonic), op1_buf);
    } else {
        snprintf(buf, bufsz, "%s", sm83_mnemonic_str(inst->mnemonic));
    }

    return buf;
}
