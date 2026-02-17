#include "codegen.h"
#include "decoder.h"
#include "symbols.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(path) _mkdir(path)
#define PATH_SEP '\\'
#else
#define MKDIR(path) mkdir(path, 0755)
#define PATH_SEP '/'
#endif

/* --------------------------------------------------------------------------
 * Internal helpers
 * -------------------------------------------------------------------------- */

/* Generate a bank-qualified function name to avoid collisions across banks.
 * Every function gets a bank prefix: "func_bNN_XXXX". */
static const char *func_name(char *buf, size_t bufsz, int bank, uint16_t addr) {
    snprintf(buf, bufsz, "func_b%02X_%04X", bank, addr);
    return buf;
}

/* Generate a bank-qualified data name */
static const char *data_name(char *buf, size_t bufsz, int bank, uint16_t addr) {
    snprintf(buf, bufsz, "data_b%02X_%04X", bank, addr);
    return buf;
}

/* Ensure a directory exists, creating it if necessary */
static void ensure_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        MKDIR(path);
    }
}

/* Return C expression for reading an 8-bit operand value.
 * buf must be >= 128 bytes. */
static const char *operand_read8(char *buf, size_t bufsz,
                                 sm83_operand_t op,
                                 uint8_t imm8, uint16_t imm16) {
    switch (op) {
    case OPERAND_A:         snprintf(buf, bufsz, "gb->a"); break;
    case OPERAND_B:         snprintf(buf, bufsz, "gb->b"); break;
    case OPERAND_C:         snprintf(buf, bufsz, "gb->c"); break;
    case OPERAND_D:         snprintf(buf, bufsz, "gb->d"); break;
    case OPERAND_E:         snprintf(buf, bufsz, "gb->e"); break;
    case OPERAND_H:         snprintf(buf, bufsz, "gb->h"); break;
    case OPERAND_L:         snprintf(buf, bufsz, "gb->l"); break;
    case OPERAND_IMM8:      snprintf(buf, bufsz, "0x%02X", imm8); break;
    case OPERAND_IND_BC:    snprintf(buf, bufsz, "mem_read8(gb, REG_BC(gb))"); break;
    case OPERAND_IND_DE:    snprintf(buf, bufsz, "mem_read8(gb, REG_DE(gb))"); break;
    case OPERAND_IND_HL:    snprintf(buf, bufsz, "mem_read8(gb, REG_HL(gb))"); break;
    case OPERAND_IND_HLI:   snprintf(buf, bufsz, "mem_read8(gb, REG_HL(gb))"); break;
    case OPERAND_IND_HLD:   snprintf(buf, bufsz, "mem_read8(gb, REG_HL(gb))"); break;
    case OPERAND_IND_C:     snprintf(buf, bufsz, "mem_read8(gb, 0xFF00 + gb->c)"); break;
    case OPERAND_IND_IMM8:  snprintf(buf, bufsz, "mem_read8(gb, 0xFF00 + 0x%02X)", imm8); break;
    case OPERAND_IND_IMM16: snprintf(buf, bufsz, "mem_read8(gb, 0x%04X)", imm16); break;
    default:                snprintf(buf, bufsz, "0 /* unknown operand */"); break;
    }
    return buf;
}

/* Write C statement(s) to store an 8-bit value into a destination operand.
 * Also handles HL+/HL- post-increment/decrement. */
static void operand_write8(FILE *f, int indent,
                           sm83_operand_t op,
                           const char *val_expr,
                           uint8_t imm8, uint16_t imm16) {
    const char *pad = "                ";
    if (indent > 16) indent = 16;
    const char *ws = pad + (16 - indent);

    switch (op) {
    case OPERAND_A:
        fprintf(f, "%sgb->a = %s;\n", ws, val_expr); break;
    case OPERAND_B:
        fprintf(f, "%sgb->b = %s;\n", ws, val_expr); break;
    case OPERAND_C:
        fprintf(f, "%sgb->c = %s;\n", ws, val_expr); break;
    case OPERAND_D:
        fprintf(f, "%sgb->d = %s;\n", ws, val_expr); break;
    case OPERAND_E:
        fprintf(f, "%sgb->e = %s;\n", ws, val_expr); break;
    case OPERAND_H:
        fprintf(f, "%sgb->h = %s;\n", ws, val_expr); break;
    case OPERAND_L:
        fprintf(f, "%sgb->l = %s;\n", ws, val_expr); break;
    case OPERAND_IND_BC:
        fprintf(f, "%smem_write8(gb, REG_BC(gb), %s);\n", ws, val_expr); break;
    case OPERAND_IND_DE:
        fprintf(f, "%smem_write8(gb, REG_DE(gb), %s);\n", ws, val_expr); break;
    case OPERAND_IND_HL:
        fprintf(f, "%smem_write8(gb, REG_HL(gb), %s);\n", ws, val_expr); break;
    case OPERAND_IND_HLI:
        fprintf(f, "%smem_write8(gb, REG_HL(gb), %s);\n", ws, val_expr);
        fprintf(f, "%s{ uint16_t hl = REG_HL(gb) + 1; gb->h = hl >> 8; gb->l = hl & 0xFF; }\n", ws);
        break;
    case OPERAND_IND_HLD:
        fprintf(f, "%smem_write8(gb, REG_HL(gb), %s);\n", ws, val_expr);
        fprintf(f, "%s{ uint16_t hl = REG_HL(gb) - 1; gb->h = hl >> 8; gb->l = hl & 0xFF; }\n", ws);
        break;
    case OPERAND_IND_C:
        fprintf(f, "%smem_write8(gb, 0xFF00 + gb->c, %s);\n", ws, val_expr); break;
    case OPERAND_IND_IMM8:
        fprintf(f, "%smem_write8(gb, 0xFF00 + 0x%02X, %s);\n", ws, imm8, val_expr); break;
    case OPERAND_IND_IMM16:
        fprintf(f, "%smem_write8(gb, 0x%04X, %s);\n", ws, imm16, val_expr); break;
    default:
        fprintf(f, "%s/* unknown write target */\n", ws); break;
    }
}

/* Check if operand is a 16-bit register pair */
static bool is_reg16(sm83_operand_t op) {
    return op == OPERAND_BC || op == OPERAND_DE || op == OPERAND_HL ||
           op == OPERAND_SP || op == OPERAND_AF;
}

/* Return C expression for reading a 16-bit register value */
static const char *reg16_read(char *buf, size_t bufsz, sm83_operand_t op, uint16_t imm16) {
    switch (op) {
    case OPERAND_BC:    snprintf(buf, bufsz, "REG_BC(gb)"); break;
    case OPERAND_DE:    snprintf(buf, bufsz, "REG_DE(gb)"); break;
    case OPERAND_HL:    snprintf(buf, bufsz, "REG_HL(gb)"); break;
    case OPERAND_SP:    snprintf(buf, bufsz, "gb->sp"); break;
    case OPERAND_AF:    snprintf(buf, bufsz, "REG_AF(gb)"); break;
    case OPERAND_IMM16: snprintf(buf, bufsz, "0x%04X", imm16); break;
    default:            snprintf(buf, bufsz, "0 /* unknown reg16 */"); break;
    }
    return buf;
}

/* Emit C statement(s) to write a 16-bit value into a register pair */
static void reg16_write(FILE *f, int indent, sm83_operand_t op, const char *val_expr) {
    const char *pad = "                ";
    if (indent > 16) indent = 16;
    const char *ws = pad + (16 - indent);

    switch (op) {
    case OPERAND_BC:
        fprintf(f, "%s{ uint16_t _v = %s; gb->b = _v >> 8; gb->c = _v & 0xFF; }\n", ws, val_expr);
        break;
    case OPERAND_DE:
        fprintf(f, "%s{ uint16_t _v = %s; gb->d = _v >> 8; gb->e = _v & 0xFF; }\n", ws, val_expr);
        break;
    case OPERAND_HL:
        fprintf(f, "%s{ uint16_t _v = %s; gb->h = _v >> 8; gb->l = _v & 0xFF; }\n", ws, val_expr);
        break;
    case OPERAND_SP:
        fprintf(f, "%sgb->sp = %s;\n", ws, val_expr);
        break;
    case OPERAND_AF:
        fprintf(f, "%s{ uint16_t _v = %s; gb->a = _v >> 8; "
                "gb->f_z = (_v >> 7) & 1; gb->f_n = (_v >> 6) & 1; "
                "gb->f_h = (_v >> 5) & 1; gb->f_c = (_v >> 4) & 1; }\n",
                ws, val_expr);
        break;
    default:
        fprintf(f, "%s/* unknown 16-bit write target */\n", ws);
        break;
    }
}

/* Return the condition expression for a conditional operand */
static const char *cond_expr(sm83_operand_t op) {
    switch (op) {
    case OPERAND_COND_NZ: return "!gb->f_z";
    case OPERAND_COND_Z:  return "gb->f_z";
    case OPERAND_COND_NC: return "!gb->f_c";
    case OPERAND_COND_C:  return "gb->f_c";
    default:              return "1";
    }
}

/* Return RST vector address from operand */
static uint16_t rst_vector(sm83_operand_t op) {
    switch (op) {
    case OPERAND_RST_00: return 0x0000;
    case OPERAND_RST_08: return 0x0008;
    case OPERAND_RST_10: return 0x0010;
    case OPERAND_RST_18: return 0x0018;
    case OPERAND_RST_20: return 0x0020;
    case OPERAND_RST_28: return 0x0028;
    case OPERAND_RST_30: return 0x0030;
    case OPERAND_RST_38: return 0x0038;
    default:             return 0x0000;
    }
}

/* Return bit index from a BIT operand */
static int bit_index(sm83_operand_t op) {
    switch (op) {
    case OPERAND_BIT0: return 0;
    case OPERAND_BIT1: return 1;
    case OPERAND_BIT2: return 2;
    case OPERAND_BIT3: return 3;
    case OPERAND_BIT4: return 4;
    case OPERAND_BIT5: return 5;
    case OPERAND_BIT6: return 6;
    case OPERAND_BIT7: return 7;
    default:           return 0;
    }
}

/* Get indentation whitespace string for a given indent level */
static const char *indent_str(int indent) {
    static const char spaces[] = "                                ";
    if (indent > 32) indent = 32;
    return spaces + (32 - indent);
}

/* --------------------------------------------------------------------------
 * codegen_emit_instruction - translate one SM83 instruction to C
 * -------------------------------------------------------------------------- */

void codegen_emit_instruction(FILE *f, const sm83_inst_t *inst,
                              uint16_t addr, uint8_t imm8, uint16_t imm16,
                              int indent, int bank) {
    const char *ws = indent_str(indent);
    char disasm[64];
    char buf1[128], buf2[128];
    char fname[64];

    /* Emit the disassembly as a comment */
    sm83_format(disasm, sizeof(disasm), inst, addr, imm8, imm16);
    fprintf(f, "%s/* %04X: %s */\n", ws, addr, disasm);

    /* Emit cycle count */
    fprintf(f, "%sgb->cycles += %d;\n", ws, inst->cycles);

    switch (inst->mnemonic) {

    /* ----- NOP ----- */
    case OP_NOP:
        /* No operation */
        break;

    /* ----- STOP ----- */
    case OP_STOP:
        fprintf(f, "%shal_stop(gb);\n", ws);
        break;

    /* ----- HALT ----- */
    case OP_HALT:
        fprintf(f, "%shal_halt(gb);\n", ws);
        break;

    /* ----- DI / EI ----- */
    case OP_DI:
        fprintf(f, "%sgb->ime = 0;\n", ws);
        break;
    case OP_EI:
        fprintf(f, "%sgb->ime = 1;\n", ws);
        break;

    /* ----- LD ----- */
    case OP_LD: {
        /* 16-bit register loads */
        if (is_reg16(inst->op1) && (is_reg16(inst->op2) || inst->op2 == OPERAND_IMM16)) {
            /* LD rr, d16 or LD SP, HL */
            reg16_read(buf1, sizeof(buf1), inst->op2, imm16);
            reg16_write(f, indent, inst->op1, buf1);
        }
        /* LD (a16), SP -- 16-bit store */
        else if (inst->op1 == OPERAND_IND_IMM16 && inst->op2 == OPERAND_SP) {
            fprintf(f, "%smem_write8(gb, 0x%04X, gb->sp & 0xFF);\n", ws, imm16);
            fprintf(f, "%smem_write8(gb, 0x%04X, gb->sp >> 8);\n", ws, (uint16_t)(imm16 + 1));
        }
        /* LD HL, SP+r8 */
        else if (inst->op1 == OPERAND_HL && inst->op2 == OPERAND_SP_R8) {
            fprintf(f, "%s{\n", ws);
            fprintf(f, "%s    int8_t offset = (int8_t)0x%02X;\n", ws, imm8);
            fprintf(f, "%s    uint16_t result = (uint16_t)(gb->sp + offset);\n", ws);
            fprintf(f, "%s    gb->f_z = 0;\n", ws);
            fprintf(f, "%s    gb->f_n = 0;\n", ws);
            fprintf(f, "%s    gb->f_h = ((gb->sp & 0xF) + (offset & 0xF)) > 0xF ? 1 : 0;\n", ws);
            fprintf(f, "%s    gb->f_c = ((gb->sp & 0xFF) + (offset & 0xFF)) > 0xFF ? 1 : 0;\n", ws);
            fprintf(f, "%s    gb->h = result >> 8; gb->l = result & 0xFF;\n", ws);
            fprintf(f, "%s}\n", ws);
        }
        /* 8-bit loads */
        else {
            operand_read8(buf1, sizeof(buf1), inst->op2, imm8, imm16);
            operand_write8(f, indent, inst->op1, buf1, imm8, imm16);
            /* Handle HLI/HLD on read side too */
            if (inst->op2 == OPERAND_IND_HLI) {
                fprintf(f, "%s{ uint16_t hl = REG_HL(gb) + 1; gb->h = hl >> 8; gb->l = hl & 0xFF; }\n", ws);
            } else if (inst->op2 == OPERAND_IND_HLD) {
                fprintf(f, "%s{ uint16_t hl = REG_HL(gb) - 1; gb->h = hl >> 8; gb->l = hl & 0xFF; }\n", ws);
            }
        }
        break;
    }

    /* ----- LDH ----- */
    case OP_LDH: {
        /* LDH (a8), A or LDH A, (a8) */
        operand_read8(buf1, sizeof(buf1), inst->op2, imm8, imm16);
        operand_write8(f, indent, inst->op1, buf1, imm8, imm16);
        break;
    }

    /* ----- PUSH ----- */
    case OP_PUSH: {
        reg16_read(buf1, sizeof(buf1), inst->op1, 0);
        fprintf(f, "%s{\n", ws);
        fprintf(f, "%s    uint16_t _pv = %s;\n", ws, buf1);
        fprintf(f, "%s    gb->sp -= 2;\n", ws);
        fprintf(f, "%s    mem_write8(gb, gb->sp + 1, _pv >> 8);\n", ws);
        fprintf(f, "%s    mem_write8(gb, gb->sp, _pv & 0xFF);\n", ws);
        fprintf(f, "%s}\n", ws);
        break;
    }

    /* ----- POP ----- */
    case OP_POP: {
        fprintf(f, "%s{\n", ws);
        fprintf(f, "%s    uint16_t _pv = mem_read8(gb, gb->sp) | (mem_read8(gb, gb->sp + 1) << 8);\n", ws);
        fprintf(f, "%s    gb->sp += 2;\n", ws);
        /* POP AF has special handling - low nibble of F is always 0 */
        if (inst->op1 == OPERAND_AF) {
            fprintf(f, "%s    _pv &= 0xFFF0;\n", ws);
        }
        char pop_val[] = "_pv";
        reg16_write(f, indent + 4, inst->op1, pop_val);
        fprintf(f, "%s}\n", ws);
        break;
    }

    /* ----- ADD (8-bit A + val, or 16-bit HL + rr, or SP + r8) ----- */
    case OP_ADD: {
        /* ADD SP, r8 */
        if (inst->op1 == OPERAND_SP) {
            fprintf(f, "%s{\n", ws);
            fprintf(f, "%s    int8_t offset = (int8_t)0x%02X;\n", ws, imm8);
            fprintf(f, "%s    gb->f_z = 0;\n", ws);
            fprintf(f, "%s    gb->f_n = 0;\n", ws);
            fprintf(f, "%s    gb->f_h = ((gb->sp & 0xF) + (offset & 0xF)) > 0xF ? 1 : 0;\n", ws);
            fprintf(f, "%s    gb->f_c = ((gb->sp & 0xFF) + (offset & 0xFF)) > 0xFF ? 1 : 0;\n", ws);
            fprintf(f, "%s    gb->sp = (uint16_t)(gb->sp + offset);\n", ws);
            fprintf(f, "%s}\n", ws);
        }
        /* ADD HL, rr (16-bit) */
        else if (inst->op1 == OPERAND_HL) {
            reg16_read(buf1, sizeof(buf1), inst->op2, imm16);
            fprintf(f, "%s{\n", ws);
            fprintf(f, "%s    uint16_t hl = REG_HL(gb);\n", ws);
            fprintf(f, "%s    uint16_t val = %s;\n", ws, buf1);
            fprintf(f, "%s    uint32_t result = hl + val;\n", ws);
            fprintf(f, "%s    gb->f_n = 0;\n", ws);
            fprintf(f, "%s    gb->f_h = ((hl & 0xFFF) + (val & 0xFFF)) > 0xFFF ? 1 : 0;\n", ws);
            fprintf(f, "%s    gb->f_c = result > 0xFFFF ? 1 : 0;\n", ws);
            fprintf(f, "%s    gb->h = (result >> 8) & 0xFF; gb->l = result & 0xFF;\n", ws);
            fprintf(f, "%s}\n", ws);
        }
        /* ADD A, val (8-bit) */
        else {
            operand_read8(buf1, sizeof(buf1), inst->op1, imm8, imm16);
            fprintf(f, "%s{\n", ws);
            fprintf(f, "%s    uint8_t val = %s;\n", ws, buf1);
            fprintf(f, "%s    uint16_t result = gb->a + val;\n", ws);
            fprintf(f, "%s    gb->f_h = ((gb->a & 0xF) + (val & 0xF)) > 0xF ? 1 : 0;\n", ws);
            fprintf(f, "%s    gb->f_c = result > 0xFF ? 1 : 0;\n", ws);
            fprintf(f, "%s    gb->a = result & 0xFF;\n", ws);
            fprintf(f, "%s    gb->f_z = (gb->a == 0) ? 1 : 0;\n", ws);
            fprintf(f, "%s    gb->f_n = 0;\n", ws);
            fprintf(f, "%s}\n", ws);
        }
        break;
    }

    /* ----- ADC ----- */
    case OP_ADC: {
        operand_read8(buf1, sizeof(buf1), inst->op1, imm8, imm16);
        fprintf(f, "%s{\n", ws);
        fprintf(f, "%s    uint8_t val = %s;\n", ws, buf1);
        fprintf(f, "%s    uint8_t carry = gb->f_c;\n", ws);
        fprintf(f, "%s    uint16_t result = gb->a + val + carry;\n", ws);
        fprintf(f, "%s    gb->f_h = ((gb->a & 0xF) + (val & 0xF) + carry) > 0xF ? 1 : 0;\n", ws);
        fprintf(f, "%s    gb->f_c = result > 0xFF ? 1 : 0;\n", ws);
        fprintf(f, "%s    gb->a = result & 0xFF;\n", ws);
        fprintf(f, "%s    gb->f_z = (gb->a == 0) ? 1 : 0;\n", ws);
        fprintf(f, "%s    gb->f_n = 0;\n", ws);
        fprintf(f, "%s}\n", ws);
        break;
    }

    /* ----- SUB ----- */
    case OP_SUB: {
        operand_read8(buf1, sizeof(buf1), inst->op1, imm8, imm16);
        fprintf(f, "%s{\n", ws);
        fprintf(f, "%s    uint8_t val = %s;\n", ws, buf1);
        fprintf(f, "%s    int16_t result = (int16_t)gb->a - (int16_t)val;\n", ws);
        fprintf(f, "%s    gb->f_h = ((gb->a & 0xF) - (val & 0xF)) < 0 ? 1 : 0;\n", ws);
        fprintf(f, "%s    gb->f_c = result < 0 ? 1 : 0;\n", ws);
        fprintf(f, "%s    gb->a = (uint8_t)(result & 0xFF);\n", ws);
        fprintf(f, "%s    gb->f_z = (gb->a == 0) ? 1 : 0;\n", ws);
        fprintf(f, "%s    gb->f_n = 1;\n", ws);
        fprintf(f, "%s}\n", ws);
        break;
    }

    /* ----- SBC ----- */
    case OP_SBC: {
        operand_read8(buf1, sizeof(buf1), inst->op1, imm8, imm16);
        fprintf(f, "%s{\n", ws);
        fprintf(f, "%s    uint8_t val = %s;\n", ws, buf1);
        fprintf(f, "%s    uint8_t carry = gb->f_c;\n", ws);
        fprintf(f, "%s    int16_t result = (int16_t)gb->a - (int16_t)val - (int16_t)carry;\n", ws);
        fprintf(f, "%s    gb->f_h = ((gb->a & 0xF) - (val & 0xF) - carry) < 0 ? 1 : 0;\n", ws);
        fprintf(f, "%s    gb->f_c = result < 0 ? 1 : 0;\n", ws);
        fprintf(f, "%s    gb->a = (uint8_t)(result & 0xFF);\n", ws);
        fprintf(f, "%s    gb->f_z = (gb->a == 0) ? 1 : 0;\n", ws);
        fprintf(f, "%s    gb->f_n = 1;\n", ws);
        fprintf(f, "%s}\n", ws);
        break;
    }

    /* ----- AND ----- */
    case OP_AND: {
        operand_read8(buf1, sizeof(buf1), inst->op1, imm8, imm16);
        fprintf(f, "%sgb->a &= %s;\n", ws, buf1);
        fprintf(f, "%sgb->f_z = (gb->a == 0) ? 1 : 0;\n", ws);
        fprintf(f, "%sgb->f_n = 0; gb->f_h = 1; gb->f_c = 0;\n", ws);
        break;
    }

    /* ----- XOR ----- */
    case OP_XOR: {
        operand_read8(buf1, sizeof(buf1), inst->op1, imm8, imm16);
        fprintf(f, "%sgb->a ^= %s;\n", ws, buf1);
        fprintf(f, "%sgb->f_z = (gb->a == 0) ? 1 : 0;\n", ws);
        fprintf(f, "%sgb->f_n = 0; gb->f_h = 0; gb->f_c = 0;\n", ws);
        break;
    }

    /* ----- OR ----- */
    case OP_OR: {
        operand_read8(buf1, sizeof(buf1), inst->op1, imm8, imm16);
        fprintf(f, "%sgb->a |= %s;\n", ws, buf1);
        fprintf(f, "%sgb->f_z = (gb->a == 0) ? 1 : 0;\n", ws);
        fprintf(f, "%sgb->f_n = 0; gb->f_h = 0; gb->f_c = 0;\n", ws);
        break;
    }

    /* ----- CP (compare - same as SUB but discard result) ----- */
    case OP_CP: {
        operand_read8(buf1, sizeof(buf1), inst->op1, imm8, imm16);
        fprintf(f, "%s{\n", ws);
        fprintf(f, "%s    uint8_t val = %s;\n", ws, buf1);
        fprintf(f, "%s    int16_t result = (int16_t)gb->a - (int16_t)val;\n", ws);
        fprintf(f, "%s    gb->f_z = ((result & 0xFF) == 0) ? 1 : 0;\n", ws);
        fprintf(f, "%s    gb->f_n = 1;\n", ws);
        fprintf(f, "%s    gb->f_h = ((gb->a & 0xF) - (val & 0xF)) < 0 ? 1 : 0;\n", ws);
        fprintf(f, "%s    gb->f_c = result < 0 ? 1 : 0;\n", ws);
        fprintf(f, "%s}\n", ws);
        break;
    }

    /* ----- INC (8-bit and 16-bit) ----- */
    case OP_INC: {
        if (is_reg16(inst->op1)) {
            /* 16-bit INC - no flags affected */
            reg16_read(buf1, sizeof(buf1), inst->op1, 0);
            char inc_expr[160];
            snprintf(inc_expr, sizeof(inc_expr), "(uint16_t)(%s + 1)", buf1);
            reg16_write(f, indent, inst->op1, inc_expr);
        } else if (inst->op1 == OPERAND_IND_HL) {
            /* INC (HL) */
            fprintf(f, "%s{\n", ws);
            fprintf(f, "%s    uint8_t val = mem_read8(gb, REG_HL(gb));\n", ws);
            fprintf(f, "%s    gb->f_h = (val & 0xF) == 0xF ? 1 : 0;\n", ws);
            fprintf(f, "%s    val++;\n", ws);
            fprintf(f, "%s    gb->f_z = (val == 0) ? 1 : 0;\n", ws);
            fprintf(f, "%s    gb->f_n = 0;\n", ws);
            fprintf(f, "%s    mem_write8(gb, REG_HL(gb), val);\n", ws);
            fprintf(f, "%s}\n", ws);
        } else {
            /* 8-bit register INC */
            operand_read8(buf1, sizeof(buf1), inst->op1, imm8, imm16);
            fprintf(f, "%s{\n", ws);
            fprintf(f, "%s    gb->f_h = (%s & 0xF) == 0xF ? 1 : 0;\n", ws, buf1);
            /* Write incremented value */
            char inc_val[160];
            snprintf(inc_val, sizeof(inc_val), "(uint8_t)(%s + 1)", buf1);
            fprintf(f, "%s    ", ws);
            operand_write8(f, 0, inst->op1, inc_val, imm8, imm16);
            operand_read8(buf2, sizeof(buf2), inst->op1, imm8, imm16);
            fprintf(f, "%s    gb->f_z = (%s == 0) ? 1 : 0;\n", ws, buf2);
            fprintf(f, "%s    gb->f_n = 0;\n", ws);
            fprintf(f, "%s}\n", ws);
        }
        break;
    }

    /* ----- DEC (8-bit and 16-bit) ----- */
    case OP_DEC: {
        if (is_reg16(inst->op1)) {
            /* 16-bit DEC - no flags affected */
            reg16_read(buf1, sizeof(buf1), inst->op1, 0);
            char dec_expr[160];
            snprintf(dec_expr, sizeof(dec_expr), "(uint16_t)(%s - 1)", buf1);
            reg16_write(f, indent, inst->op1, dec_expr);
        } else if (inst->op1 == OPERAND_IND_HL) {
            /* DEC (HL) */
            fprintf(f, "%s{\n", ws);
            fprintf(f, "%s    uint8_t val = mem_read8(gb, REG_HL(gb));\n", ws);
            fprintf(f, "%s    gb->f_h = (val & 0xF) == 0x0 ? 1 : 0;\n", ws);
            fprintf(f, "%s    val--;\n", ws);
            fprintf(f, "%s    gb->f_z = (val == 0) ? 1 : 0;\n", ws);
            fprintf(f, "%s    gb->f_n = 1;\n", ws);
            fprintf(f, "%s    mem_write8(gb, REG_HL(gb), val);\n", ws);
            fprintf(f, "%s}\n", ws);
        } else {
            /* 8-bit register DEC */
            operand_read8(buf1, sizeof(buf1), inst->op1, imm8, imm16);
            fprintf(f, "%s{\n", ws);
            fprintf(f, "%s    gb->f_h = (%s & 0xF) == 0x0 ? 1 : 0;\n", ws, buf1);
            char dec_val[160];
            snprintf(dec_val, sizeof(dec_val), "(uint8_t)(%s - 1)", buf1);
            fprintf(f, "%s    ", ws);
            operand_write8(f, 0, inst->op1, dec_val, imm8, imm16);
            operand_read8(buf2, sizeof(buf2), inst->op1, imm8, imm16);
            fprintf(f, "%s    gb->f_z = (%s == 0) ? 1 : 0;\n", ws, buf2);
            fprintf(f, "%s    gb->f_n = 1;\n", ws);
            fprintf(f, "%s}\n", ws);
        }
        break;
    }

    /* ----- DAA (Decimal Adjust Accumulator) ----- */
    case OP_DAA: {
        fprintf(f, "%s{\n", ws);
        fprintf(f, "%s    uint16_t a = gb->a;\n", ws);
        fprintf(f, "%s    if (!gb->f_n) {\n", ws);
        fprintf(f, "%s        if (gb->f_h || (a & 0xF) > 9)\n", ws);
        fprintf(f, "%s            a += 0x06;\n", ws);
        fprintf(f, "%s        if (gb->f_c || a > 0x9F)\n", ws);
        fprintf(f, "%s            a += 0x60;\n", ws);
        fprintf(f, "%s    } else {\n", ws);
        fprintf(f, "%s        if (gb->f_h)\n", ws);
        fprintf(f, "%s            a = (a - 0x06) & 0xFF;\n", ws);
        fprintf(f, "%s        if (gb->f_c)\n", ws);
        fprintf(f, "%s            a -= 0x60;\n", ws);
        fprintf(f, "%s    }\n", ws);
        fprintf(f, "%s    gb->f_h = 0;\n", ws);
        fprintf(f, "%s    if (a & 0x100) gb->f_c = 1;\n", ws);
        fprintf(f, "%s    gb->a = a & 0xFF;\n", ws);
        fprintf(f, "%s    gb->f_z = (gb->a == 0) ? 1 : 0;\n", ws);
        fprintf(f, "%s}\n", ws);
        break;
    }

    /* ----- CPL (complement A) ----- */
    case OP_CPL:
        fprintf(f, "%sgb->a = ~gb->a;\n", ws);
        fprintf(f, "%sgb->f_n = 1; gb->f_h = 1;\n", ws);
        break;

    /* ----- SCF (set carry flag) ----- */
    case OP_SCF:
        fprintf(f, "%sgb->f_n = 0; gb->f_h = 0; gb->f_c = 1;\n", ws);
        break;

    /* ----- CCF (complement carry flag) ----- */
    case OP_CCF:
        fprintf(f, "%sgb->f_n = 0; gb->f_h = 0; gb->f_c = !gb->f_c;\n", ws);
        break;

    /* ----- RLCA (rotate A left, old bit 7 to carry) ----- */
    case OP_RLCA:
        fprintf(f, "%s{\n", ws);
        fprintf(f, "%s    uint8_t bit7 = (gb->a >> 7) & 1;\n", ws);
        fprintf(f, "%s    gb->a = (gb->a << 1) | bit7;\n", ws);
        fprintf(f, "%s    gb->f_z = 0; gb->f_n = 0; gb->f_h = 0; gb->f_c = bit7;\n", ws);
        fprintf(f, "%s}\n", ws);
        break;

    /* ----- RRCA (rotate A right, old bit 0 to carry) ----- */
    case OP_RRCA:
        fprintf(f, "%s{\n", ws);
        fprintf(f, "%s    uint8_t bit0 = gb->a & 1;\n", ws);
        fprintf(f, "%s    gb->a = (gb->a >> 1) | (bit0 << 7);\n", ws);
        fprintf(f, "%s    gb->f_z = 0; gb->f_n = 0; gb->f_h = 0; gb->f_c = bit0;\n", ws);
        fprintf(f, "%s}\n", ws);
        break;

    /* ----- RLA (rotate A left through carry) ----- */
    case OP_RLA:
        fprintf(f, "%s{\n", ws);
        fprintf(f, "%s    uint8_t old_carry = gb->f_c;\n", ws);
        fprintf(f, "%s    gb->f_c = (gb->a >> 7) & 1;\n", ws);
        fprintf(f, "%s    gb->a = (gb->a << 1) | old_carry;\n", ws);
        fprintf(f, "%s    gb->f_z = 0; gb->f_n = 0; gb->f_h = 0;\n", ws);
        fprintf(f, "%s}\n", ws);
        break;

    /* ----- RRA (rotate A right through carry) ----- */
    case OP_RRA:
        fprintf(f, "%s{\n", ws);
        fprintf(f, "%s    uint8_t old_carry = gb->f_c;\n", ws);
        fprintf(f, "%s    gb->f_c = gb->a & 1;\n", ws);
        fprintf(f, "%s    gb->a = (gb->a >> 1) | (old_carry << 7);\n", ws);
        fprintf(f, "%s    gb->f_z = 0; gb->f_n = 0; gb->f_h = 0;\n", ws);
        fprintf(f, "%s}\n", ws);
        break;

    /* ----- CB prefix: RLC ----- */
    case OP_RLC: {
        if (inst->op1 == OPERAND_IND_HL) {
            fprintf(f, "%s{\n", ws);
            fprintf(f, "%s    uint8_t val = mem_read8(gb, REG_HL(gb));\n", ws);
            fprintf(f, "%s    uint8_t bit7 = (val >> 7) & 1;\n", ws);
            fprintf(f, "%s    val = (val << 1) | bit7;\n", ws);
            fprintf(f, "%s    gb->f_z = (val == 0) ? 1 : 0;\n", ws);
            fprintf(f, "%s    gb->f_n = 0; gb->f_h = 0; gb->f_c = bit7;\n", ws);
            fprintf(f, "%s    mem_write8(gb, REG_HL(gb), val);\n", ws);
            fprintf(f, "%s}\n", ws);
        } else {
            operand_read8(buf1, sizeof(buf1), inst->op1, imm8, imm16);
            fprintf(f, "%s{\n", ws);
            fprintf(f, "%s    uint8_t val = %s;\n", ws, buf1);
            fprintf(f, "%s    uint8_t bit7 = (val >> 7) & 1;\n", ws);
            fprintf(f, "%s    val = (val << 1) | bit7;\n", ws);
            fprintf(f, "%s    gb->f_z = (val == 0) ? 1 : 0;\n", ws);
            fprintf(f, "%s    gb->f_n = 0; gb->f_h = 0; gb->f_c = bit7;\n", ws);
            fprintf(f, "%s    ", ws);
            operand_write8(f, 0, inst->op1, "val", imm8, imm16);
            fprintf(f, "%s}\n", ws);
        }
        break;
    }

    /* ----- CB prefix: RRC ----- */
    case OP_RRC: {
        if (inst->op1 == OPERAND_IND_HL) {
            fprintf(f, "%s{\n", ws);
            fprintf(f, "%s    uint8_t val = mem_read8(gb, REG_HL(gb));\n", ws);
            fprintf(f, "%s    uint8_t bit0 = val & 1;\n", ws);
            fprintf(f, "%s    val = (val >> 1) | (bit0 << 7);\n", ws);
            fprintf(f, "%s    gb->f_z = (val == 0) ? 1 : 0;\n", ws);
            fprintf(f, "%s    gb->f_n = 0; gb->f_h = 0; gb->f_c = bit0;\n", ws);
            fprintf(f, "%s    mem_write8(gb, REG_HL(gb), val);\n", ws);
            fprintf(f, "%s}\n", ws);
        } else {
            operand_read8(buf1, sizeof(buf1), inst->op1, imm8, imm16);
            fprintf(f, "%s{\n", ws);
            fprintf(f, "%s    uint8_t val = %s;\n", ws, buf1);
            fprintf(f, "%s    uint8_t bit0 = val & 1;\n", ws);
            fprintf(f, "%s    val = (val >> 1) | (bit0 << 7);\n", ws);
            fprintf(f, "%s    gb->f_z = (val == 0) ? 1 : 0;\n", ws);
            fprintf(f, "%s    gb->f_n = 0; gb->f_h = 0; gb->f_c = bit0;\n", ws);
            fprintf(f, "%s    ", ws);
            operand_write8(f, 0, inst->op1, "val", imm8, imm16);
            fprintf(f, "%s}\n", ws);
        }
        break;
    }

    /* ----- CB prefix: RL (rotate left through carry) ----- */
    case OP_RL: {
        if (inst->op1 == OPERAND_IND_HL) {
            fprintf(f, "%s{\n", ws);
            fprintf(f, "%s    uint8_t val = mem_read8(gb, REG_HL(gb));\n", ws);
            fprintf(f, "%s    uint8_t old_carry = gb->f_c;\n", ws);
            fprintf(f, "%s    gb->f_c = (val >> 7) & 1;\n", ws);
            fprintf(f, "%s    val = (val << 1) | old_carry;\n", ws);
            fprintf(f, "%s    gb->f_z = (val == 0) ? 1 : 0;\n", ws);
            fprintf(f, "%s    gb->f_n = 0; gb->f_h = 0;\n", ws);
            fprintf(f, "%s    mem_write8(gb, REG_HL(gb), val);\n", ws);
            fprintf(f, "%s}\n", ws);
        } else {
            operand_read8(buf1, sizeof(buf1), inst->op1, imm8, imm16);
            fprintf(f, "%s{\n", ws);
            fprintf(f, "%s    uint8_t val = %s;\n", ws, buf1);
            fprintf(f, "%s    uint8_t old_carry = gb->f_c;\n", ws);
            fprintf(f, "%s    gb->f_c = (val >> 7) & 1;\n", ws);
            fprintf(f, "%s    val = (val << 1) | old_carry;\n", ws);
            fprintf(f, "%s    gb->f_z = (val == 0) ? 1 : 0;\n", ws);
            fprintf(f, "%s    gb->f_n = 0; gb->f_h = 0;\n", ws);
            fprintf(f, "%s    ", ws);
            operand_write8(f, 0, inst->op1, "val", imm8, imm16);
            fprintf(f, "%s}\n", ws);
        }
        break;
    }

    /* ----- CB prefix: RR (rotate right through carry) ----- */
    case OP_RR: {
        if (inst->op1 == OPERAND_IND_HL) {
            fprintf(f, "%s{\n", ws);
            fprintf(f, "%s    uint8_t val = mem_read8(gb, REG_HL(gb));\n", ws);
            fprintf(f, "%s    uint8_t old_carry = gb->f_c;\n", ws);
            fprintf(f, "%s    gb->f_c = val & 1;\n", ws);
            fprintf(f, "%s    val = (val >> 1) | (old_carry << 7);\n", ws);
            fprintf(f, "%s    gb->f_z = (val == 0) ? 1 : 0;\n", ws);
            fprintf(f, "%s    gb->f_n = 0; gb->f_h = 0;\n", ws);
            fprintf(f, "%s    mem_write8(gb, REG_HL(gb), val);\n", ws);
            fprintf(f, "%s}\n", ws);
        } else {
            operand_read8(buf1, sizeof(buf1), inst->op1, imm8, imm16);
            fprintf(f, "%s{\n", ws);
            fprintf(f, "%s    uint8_t val = %s;\n", ws, buf1);
            fprintf(f, "%s    uint8_t old_carry = gb->f_c;\n", ws);
            fprintf(f, "%s    gb->f_c = val & 1;\n", ws);
            fprintf(f, "%s    val = (val >> 1) | (old_carry << 7);\n", ws);
            fprintf(f, "%s    gb->f_z = (val == 0) ? 1 : 0;\n", ws);
            fprintf(f, "%s    gb->f_n = 0; gb->f_h = 0;\n", ws);
            fprintf(f, "%s    ", ws);
            operand_write8(f, 0, inst->op1, "val", imm8, imm16);
            fprintf(f, "%s}\n", ws);
        }
        break;
    }

    /* ----- CB prefix: SLA (shift left, bit 0 = 0) ----- */
    case OP_SLA: {
        if (inst->op1 == OPERAND_IND_HL) {
            fprintf(f, "%s{\n", ws);
            fprintf(f, "%s    uint8_t val = mem_read8(gb, REG_HL(gb));\n", ws);
            fprintf(f, "%s    gb->f_c = (val >> 7) & 1;\n", ws);
            fprintf(f, "%s    val <<= 1;\n", ws);
            fprintf(f, "%s    gb->f_z = (val == 0) ? 1 : 0;\n", ws);
            fprintf(f, "%s    gb->f_n = 0; gb->f_h = 0;\n", ws);
            fprintf(f, "%s    mem_write8(gb, REG_HL(gb), val);\n", ws);
            fprintf(f, "%s}\n", ws);
        } else {
            operand_read8(buf1, sizeof(buf1), inst->op1, imm8, imm16);
            fprintf(f, "%s{\n", ws);
            fprintf(f, "%s    uint8_t val = %s;\n", ws, buf1);
            fprintf(f, "%s    gb->f_c = (val >> 7) & 1;\n", ws);
            fprintf(f, "%s    val <<= 1;\n", ws);
            fprintf(f, "%s    gb->f_z = (val == 0) ? 1 : 0;\n", ws);
            fprintf(f, "%s    gb->f_n = 0; gb->f_h = 0;\n", ws);
            fprintf(f, "%s    ", ws);
            operand_write8(f, 0, inst->op1, "val", imm8, imm16);
            fprintf(f, "%s}\n", ws);
        }
        break;
    }

    /* ----- CB prefix: SRA (shift right, preserve bit 7) ----- */
    case OP_SRA: {
        if (inst->op1 == OPERAND_IND_HL) {
            fprintf(f, "%s{\n", ws);
            fprintf(f, "%s    uint8_t val = mem_read8(gb, REG_HL(gb));\n", ws);
            fprintf(f, "%s    gb->f_c = val & 1;\n", ws);
            fprintf(f, "%s    val = (val >> 1) | (val & 0x80);\n", ws);
            fprintf(f, "%s    gb->f_z = (val == 0) ? 1 : 0;\n", ws);
            fprintf(f, "%s    gb->f_n = 0; gb->f_h = 0;\n", ws);
            fprintf(f, "%s    mem_write8(gb, REG_HL(gb), val);\n", ws);
            fprintf(f, "%s}\n", ws);
        } else {
            operand_read8(buf1, sizeof(buf1), inst->op1, imm8, imm16);
            fprintf(f, "%s{\n", ws);
            fprintf(f, "%s    uint8_t val = %s;\n", ws, buf1);
            fprintf(f, "%s    gb->f_c = val & 1;\n", ws);
            fprintf(f, "%s    val = (val >> 1) | (val & 0x80);\n", ws);
            fprintf(f, "%s    gb->f_z = (val == 0) ? 1 : 0;\n", ws);
            fprintf(f, "%s    gb->f_n = 0; gb->f_h = 0;\n", ws);
            fprintf(f, "%s    ", ws);
            operand_write8(f, 0, inst->op1, "val", imm8, imm16);
            fprintf(f, "%s}\n", ws);
        }
        break;
    }

    /* ----- CB prefix: SWAP (swap nibbles) ----- */
    case OP_SWAP: {
        if (inst->op1 == OPERAND_IND_HL) {
            fprintf(f, "%s{\n", ws);
            fprintf(f, "%s    uint8_t val = mem_read8(gb, REG_HL(gb));\n", ws);
            fprintf(f, "%s    val = ((val & 0xF) << 4) | ((val >> 4) & 0xF);\n", ws);
            fprintf(f, "%s    gb->f_z = (val == 0) ? 1 : 0;\n", ws);
            fprintf(f, "%s    gb->f_n = 0; gb->f_h = 0; gb->f_c = 0;\n", ws);
            fprintf(f, "%s    mem_write8(gb, REG_HL(gb), val);\n", ws);
            fprintf(f, "%s}\n", ws);
        } else {
            operand_read8(buf1, sizeof(buf1), inst->op1, imm8, imm16);
            fprintf(f, "%s{\n", ws);
            fprintf(f, "%s    uint8_t val = %s;\n", ws, buf1);
            fprintf(f, "%s    val = ((val & 0xF) << 4) | ((val >> 4) & 0xF);\n", ws);
            fprintf(f, "%s    gb->f_z = (val == 0) ? 1 : 0;\n", ws);
            fprintf(f, "%s    gb->f_n = 0; gb->f_h = 0; gb->f_c = 0;\n", ws);
            fprintf(f, "%s    ", ws);
            operand_write8(f, 0, inst->op1, "val", imm8, imm16);
            fprintf(f, "%s}\n", ws);
        }
        break;
    }

    /* ----- CB prefix: SRL (shift right, bit 7 = 0) ----- */
    case OP_SRL: {
        if (inst->op1 == OPERAND_IND_HL) {
            fprintf(f, "%s{\n", ws);
            fprintf(f, "%s    uint8_t val = mem_read8(gb, REG_HL(gb));\n", ws);
            fprintf(f, "%s    gb->f_c = val & 1;\n", ws);
            fprintf(f, "%s    val >>= 1;\n", ws);
            fprintf(f, "%s    gb->f_z = (val == 0) ? 1 : 0;\n", ws);
            fprintf(f, "%s    gb->f_n = 0; gb->f_h = 0;\n", ws);
            fprintf(f, "%s    mem_write8(gb, REG_HL(gb), val);\n", ws);
            fprintf(f, "%s}\n", ws);
        } else {
            operand_read8(buf1, sizeof(buf1), inst->op1, imm8, imm16);
            fprintf(f, "%s{\n", ws);
            fprintf(f, "%s    uint8_t val = %s;\n", ws, buf1);
            fprintf(f, "%s    gb->f_c = val & 1;\n", ws);
            fprintf(f, "%s    val >>= 1;\n", ws);
            fprintf(f, "%s    gb->f_z = (val == 0) ? 1 : 0;\n", ws);
            fprintf(f, "%s    gb->f_n = 0; gb->f_h = 0;\n", ws);
            fprintf(f, "%s    ", ws);
            operand_write8(f, 0, inst->op1, "val", imm8, imm16);
            fprintf(f, "%s}\n", ws);
        }
        break;
    }

    /* ----- CB prefix: BIT (test bit) ----- */
    case OP_BIT: {
        int bit = bit_index(inst->op1);
        if (inst->op2 == OPERAND_IND_HL) {
            fprintf(f, "%s{\n", ws);
            fprintf(f, "%s    uint8_t val = mem_read8(gb, REG_HL(gb));\n", ws);
            fprintf(f, "%s    gb->f_z = !(val & (1 << %d)) ? 1 : 0;\n", ws, bit);
            fprintf(f, "%s    gb->f_n = 0; gb->f_h = 1;\n", ws);
            fprintf(f, "%s}\n", ws);
        } else {
            operand_read8(buf1, sizeof(buf1), inst->op2, imm8, imm16);
            fprintf(f, "%sgb->f_z = !(%s & (1 << %d)) ? 1 : 0;\n", ws, buf1, bit);
            fprintf(f, "%sgb->f_n = 0; gb->f_h = 1;\n", ws);
        }
        break;
    }

    /* ----- CB prefix: RES (reset/clear bit) ----- */
    case OP_RES: {
        int bit = bit_index(inst->op1);
        if (inst->op2 == OPERAND_IND_HL) {
            fprintf(f, "%s{\n", ws);
            fprintf(f, "%s    uint8_t val = mem_read8(gb, REG_HL(gb));\n", ws);
            fprintf(f, "%s    val &= ~(1 << %d);\n", ws, bit);
            fprintf(f, "%s    mem_write8(gb, REG_HL(gb), val);\n", ws);
            fprintf(f, "%s}\n", ws);
        } else {
            operand_read8(buf1, sizeof(buf1), inst->op2, imm8, imm16);
            char res_expr[160];
            snprintf(res_expr, sizeof(res_expr), "%s & ~(1 << %d)", buf1, bit);
            operand_write8(f, indent, inst->op2, res_expr, imm8, imm16);
        }
        break;
    }

    /* ----- CB prefix: SET (set bit) ----- */
    case OP_SET: {
        int bit = bit_index(inst->op1);
        if (inst->op2 == OPERAND_IND_HL) {
            fprintf(f, "%s{\n", ws);
            fprintf(f, "%s    uint8_t val = mem_read8(gb, REG_HL(gb));\n", ws);
            fprintf(f, "%s    val |= (1 << %d);\n", ws, bit);
            fprintf(f, "%s    mem_write8(gb, REG_HL(gb), val);\n", ws);
            fprintf(f, "%s}\n", ws);
        } else {
            operand_read8(buf1, sizeof(buf1), inst->op2, imm8, imm16);
            char set_expr[160];
            snprintf(set_expr, sizeof(set_expr), "%s | (1 << %d)", buf1, bit);
            operand_write8(f, indent, inst->op2, set_expr, imm8, imm16);
        }
        break;
    }

    /* ----- JP (absolute jump) ----- */
    case OP_JP: {
        if (inst->branch == BRANCH_JUMP_INDIRECT) {
            /* JP (HL) - indirect jump, emit as dispatch */
            fprintf(f, "%sdispatch_jump(gb, REG_HL(gb));\n", ws);
            fprintf(f, "%sreturn;\n", ws);
        } else if (inst->branch == BRANCH_JUMP) {
            /* Unconditional JP a16 */
            fprintf(f, "%sgoto label_%04X;\n", ws, imm16);
        } else if (inst->branch == BRANCH_JUMP_COND) {
            /* Conditional JP cc, a16 */
            fprintf(f, "%sif (%s) { gb->cycles += %d; goto label_%04X; }\n",
                    ws, cond_expr(inst->op1),
                    inst->cycles_taken - inst->cycles, imm16);
        }
        break;
    }

    /* ----- JR (relative jump) ----- */
    case OP_JR: {
        int8_t offset = (int8_t)imm8;
        uint16_t target = (uint16_t)(addr + 2 + offset);
        if (inst->branch == BRANCH_JUMP) {
            /* Unconditional JR r8 */
            fprintf(f, "%sgoto label_%04X;\n", ws, target);
        } else if (inst->branch == BRANCH_JUMP_COND) {
            /* Conditional JR cc, r8 */
            fprintf(f, "%sif (%s) { gb->cycles += %d; goto label_%04X; }\n",
                    ws, cond_expr(inst->op1),
                    inst->cycles_taken - inst->cycles, target);
        }
        break;
    }

    /* ----- CALL ----- */
    case OP_CALL: {
        int target_bank = (imm16 < 0x4000) ? 0 : bank;
        func_name(fname, sizeof(fname), target_bank, imm16);
        if (inst->branch == BRANCH_CALL) {
            /* Unconditional CALL a16 */
            fprintf(f, "%s%s(gb);\n", ws, fname);
        } else if (inst->branch == BRANCH_CALL_COND) {
            /* Conditional CALL cc, a16 */
            fprintf(f, "%sif (%s) { gb->cycles += %d; %s(gb); }\n",
                    ws, cond_expr(inst->op1),
                    inst->cycles_taken - inst->cycles, fname);
        }
        break;
    }

    /* ----- RET ----- */
    case OP_RET: {
        if (inst->branch == BRANCH_RET) {
            /* Unconditional RET */
            fprintf(f, "%sreturn;\n", ws);
        } else if (inst->branch == BRANCH_RET_COND) {
            /* Conditional RET cc */
            fprintf(f, "%sif (%s) { gb->cycles += %d; return; }\n",
                    ws, cond_expr(inst->op1),
                    inst->cycles_taken - inst->cycles);
        }
        break;
    }

    /* ----- RETI ----- */
    case OP_RETI:
        fprintf(f, "%sgb->ime = 1;\n", ws);
        fprintf(f, "%sreturn;\n", ws);
        break;

    /* ----- RST ----- */
    case OP_RST: {
        uint16_t vec = rst_vector(inst->op1);
        func_name(fname, sizeof(fname), 0, vec); /* RST always targets bank 0 */
        fprintf(f, "%s%s(gb);\n", ws, fname);
        break;
    }

    /* ----- INVALID ----- */
    case OP_INVALID:
        fprintf(f, "%s/* INVALID OPCODE 0x%02X */\n", ws, inst->opcode);
        fprintf(f, "%shal_invalid(gb, 0x%04X, 0x%02X);\n", ws, addr, inst->opcode);
        break;

    default:
        fprintf(f, "%s/* UNHANDLED: %s */\n", ws, sm83_mnemonic_str(inst->mnemonic));
        break;
    }
}

/* --------------------------------------------------------------------------
 * codegen_init
 * -------------------------------------------------------------------------- */

void codegen_init(codegen_ctx_t *ctx, const analysis_ctx_t *analysis,
                  const uint8_t *rom, size_t rom_size,
                  const char *output_dir, const char *game_name) {
    ctx->analysis = analysis;
    ctx->rom_data = rom;
    ctx->rom_size = rom_size;
    ctx->output_dir = output_dir;
    ctx->game_name = game_name;
    ctx->emit_debug_comments = true;
}

/* --------------------------------------------------------------------------
 * Emit functions and data for a single bank
 * -------------------------------------------------------------------------- */

/* Emit a data region as a const uint8_t array */
static void emit_data_region(FILE *f, const codegen_ctx_t *ctx,
                             uint8_t bank, uint16_t start, uint16_t end) {
    size_t bank_offset = (size_t)bank * BANK_SIZE;
    uint16_t base = (bank == 0) ? 0 : 0x4000;
    size_t len = end - start;

    if (len == 0) return;

    char dname[64];
    data_name(dname, sizeof(dname), bank, start);
    fprintf(f, "\nconst uint8_t %s[] = {", dname);
    for (size_t i = 0; i < len; i++) {
        size_t rom_off = bank_offset + (start - base) + i;
        if (rom_off < ctx->rom_size) {
            if (i % 16 == 0) fprintf(f, "\n    ");
            fprintf(f, "0x%02X", ctx->rom_data[rom_off]);
            if (i + 1 < len) fprintf(f, ", ");
        }
    }
    fprintf(f, "\n};\n");
}

/* Check if an address is within one of the included blocks */
static bool addr_in_included_blocks(const bank_analysis_t *ba,
                                     const bool *block_included,
                                     uint16_t addr) {
    for (int b = 0; b < ba->block_count; b++) {
        if (block_included[b] &&
            addr >= ba->blocks[b].start_addr &&
            addr < ba->blocks[b].end_addr) {
            return true;
        }
    }
    return false;
}

/* Emit one function as C code */
static void emit_function(FILE *f, const codegen_ctx_t *ctx,
                          int bank, int func_idx) {
    const bank_analysis_t *ba = &ctx->analysis->banks[bank];
    const function_info_t *func = &ba->functions[func_idx];
    uint16_t base = (bank == 0) ? 0 : 0x4000;
    size_t bank_offset = (size_t)bank * BANK_SIZE;

    /* Function prototype */
    if (func->name) {
        fprintf(f, "\n/* %s */\n", func->name);
    }
    char fname[64];
    func_name(fname, sizeof(fname), bank, func->entry_addr);
    fprintf(f, "void %s(gb_state_t *gb) {\n", fname);

    /* Collect all blocks belonging to this function by function_id */
    int block_indices[MAX_BLOCKS_PER_BANK];
    bool block_included[MAX_BLOCKS_PER_BANK];
    int block_count = 0;
    memset(block_included, 0, ba->block_count * sizeof(bool));

    for (int b = 0; b < ba->block_count; b++) {
        if (ba->blocks[b].function_id == func_idx) {
            block_indices[block_count++] = b;
            block_included[b] = true;
        }
    }

    /* Also include blocks reachable via JP/JR edges from function blocks
     * that belong to other functions.  This handles tail-jumps: a JP to
     * a block owned by another function is pulled in as a copy so we
     * can emit a goto instead of requiring a separate function. */
    for (int i = 0; i < block_count; i++) {
        const basic_block_t *blk = &ba->blocks[block_indices[i]];
        if (blk->exit_type != BRANCH_JUMP &&
            blk->exit_type != BRANCH_JUMP_COND &&
            !blk->has_fallthrough)
            continue;

        int max_succ = blk->num_successors;
        /* For CALL/CALL_COND/RST, successor[0] = continuation,
         * successor[1] = call target.  Only follow continuation. */
        if (blk->exit_type == BRANCH_CALL ||
            blk->exit_type == BRANCH_CALL_COND ||
            blk->exit_type == BRANCH_RST) {
            max_succ = 1;
        }

        for (int s = 0; s < max_succ; s++) {
            if (blk->successor_bank[s] != ba->bank) continue;
            uint16_t succ_addr = blk->successor_addr[s];

            /* Find the block at this successor address */
            for (int b = 0; b < ba->block_count; b++) {
                if (!block_included[b] &&
                    ba->blocks[b].start_addr == succ_addr &&
                    !ba->blocks[b].is_data) {
                    block_indices[block_count++] = b;
                    block_included[b] = true;
                    break;
                }
            }
        }
    }

    /* Simple insertion sort by start_addr */
    for (int i = 1; i < block_count; i++) {
        int key = block_indices[i];
        int j = i - 1;
        while (j >= 0 && ba->blocks[block_indices[j]].start_addr >
                         ba->blocks[key].start_addr) {
            block_indices[j + 1] = block_indices[j];
            j--;
        }
        block_indices[j + 1] = key;
    }

    /* Emit each block */
    for (int bi = 0; bi < block_count; bi++) {
        const basic_block_t *blk = &ba->blocks[block_indices[bi]];

        if (blk->is_data) {
            /* Data block within a function -- unusual but handle it */
            fprintf(f, "    /* data region %04X-%04X */\n",
                    blk->start_addr, blk->end_addr);
            continue;
        }

        /* Emit label for this block */
        fprintf(f, "label_%04X:\n", blk->start_addr);

        /* Decode and emit each instruction in this block */
        uint16_t pc = blk->start_addr;
        while (pc < blk->end_addr) {
            size_t rom_off = bank_offset + (pc - base);
            if (rom_off >= ctx->rom_size) break;

            size_t remaining = ctx->rom_size - rom_off;
            uint8_t inst_imm8 = 0;
            uint16_t inst_imm16 = 0;
            sm83_inst_t inst = sm83_decode(&ctx->rom_data[rom_off], remaining,
                                           &inst_imm8, &inst_imm16);

            if (inst.length == 0) {
                inst.length = 1; /* Prevent infinite loop */
            }

            /* For jumps/calls, check if target is in the same function.
             * If the target is outside this function, emit a function call
             * or cross-bank dispatch instead of a goto. */
            bool patched = false;

            if ((inst.mnemonic == OP_JP || inst.mnemonic == OP_JR) &&
                inst.branch != BRANCH_JUMP_INDIRECT) {

                uint16_t target;
                if (inst.mnemonic == OP_JR) {
                    int8_t offset = (int8_t)inst_imm8;
                    target = (uint16_t)(pc + 2 + offset);
                } else {
                    target = inst_imm16;
                }

                bool in_func = addr_in_included_blocks(ba, block_included, target);

                if (!in_func) {
                    /* Cross-function jump: determine target bank.
                     * If target is in 0x4000-0x7FFF and we're in bank 0,
                     * the target bank is whatever is mapped at runtime. */
                    bool runtime_bank = (target >= 0x4000 && target < 0x8000 && bank == 0);
                    int tgt_bank = (target < 0x4000) ? 0 : bank;

                    bool has_func = false;
                    if (!runtime_bank) {
                        const bank_analysis_t *tgt_ba = &ctx->analysis->banks[tgt_bank];
                        for (int fi2 = 0; fi2 < tgt_ba->function_count; fi2++) {
                            if (tgt_ba->functions[fi2].entry_addr == target) {
                                has_func = true;
                                break;
                            }
                        }
                    }

                    char disasm[64];
                    sm83_format(disasm, sizeof(disasm), &inst, pc, inst_imm8, inst_imm16);
                    fprintf(f, "    /* %04X: %s */\n", pc, disasm);
                    fprintf(f, "    gb->cycles += %d;\n", inst.cycles);

                    if (inst.branch == BRANCH_JUMP) {
                        if (runtime_bank) {
                            /* Target is in switchable bank area - dispatch at runtime */
                            fprintf(f, "    dispatch_call(gb, (uint8_t)gb->mem->rom_bank, 0x%04X); return;\n", target);
                        } else if (has_func) {
                            char tname[64];
                            func_name(tname, sizeof(tname), tgt_bank, target);
                            fprintf(f, "    %s(gb); return;\n", tname);
                        } else {
                            fprintf(f, "    dispatch_jump(gb, 0x%04X); return;\n", target);
                        }
                    } else {
                        /* Conditional */
                        const char *cond = cond_expr(inst.op1);
                        if (runtime_bank) {
                            fprintf(f, "    if (%s) { gb->cycles += %d; dispatch_call(gb, (uint8_t)gb->mem->rom_bank, 0x%04X); return; }\n",
                                    cond, inst.cycles_taken - inst.cycles, target);
                        } else if (has_func) {
                            char tname[64];
                            func_name(tname, sizeof(tname), tgt_bank, target);
                            fprintf(f, "    if (%s) { gb->cycles += %d; %s(gb); return; }\n",
                                    cond, inst.cycles_taken - inst.cycles, tname);
                        } else {
                            fprintf(f, "    if (%s) { gb->cycles += %d; dispatch_jump(gb, 0x%04X); return; }\n",
                                    cond, inst.cycles_taken - inst.cycles, target);
                        }
                    }
                    patched = true;
                }
            }

            if (!patched) {
                codegen_emit_instruction(f, &inst, pc, inst_imm8, inst_imm16, 4, bank);
            }

            pc += inst.length;
        }
    }

    fprintf(f, "}\n");
}

int codegen_emit_bank(codegen_ctx_t *ctx, int bank) {
    char dir_path[512];
    char file_path[512];

    /* Ensure output directory exists */
    snprintf(dir_path, sizeof(dir_path), "%s", ctx->output_dir);
    ensure_dir(dir_path);

    snprintf(dir_path, sizeof(dir_path), "%s/banks", ctx->output_dir);
    ensure_dir(dir_path);

    snprintf(file_path, sizeof(file_path), "%s/banks/bank_%02X.c",
             ctx->output_dir, bank);

    FILE *f = fopen(file_path, "w");
    if (!f) {
        fprintf(stderr, "codegen: cannot open %s for writing: %s\n",
                file_path, strerror(errno));
        return -1;
    }

    /* File header */
    fprintf(f, "/* bank_%02X.c - auto-generated from %s ROM */\n", bank, ctx->game_name);
    fprintf(f, "/* Do not edit by hand. */\n\n");
    fprintf(f, "#include \"hal/cpu.h\"\n");
    fprintf(f, "#include \"hal/memory.h\"\n");
    fprintf(f, "#include \"dispatch.h\"\n");
    fprintf(f, "\n");

    const bank_analysis_t *ba = &ctx->analysis->banks[bank];
    uint16_t base = (bank == 0) ? 0 : 0x4000;
    uint16_t bank_end = base + BANK_SIZE;

    /* Forward declarations for all functions in this bank */
    fprintf(f, "/* Forward declarations */\n");
    for (int fi = 0; fi < ba->function_count; fi++) {
        char fname[64];
        func_name(fname, sizeof(fname), bank, ba->functions[fi].entry_addr);
        fprintf(f, "void %s(gb_state_t *gb);\n", fname);
    }
    fprintf(f, "\n");

    /* Emit data regions: scan addresses not covered by any function.
     * Walk through blocks and emit data blocks. */
    for (int b = 0; b < ba->block_count; b++) {
        const basic_block_t *blk = &ba->blocks[b];
        if (blk->is_data && blk->function_id < 0) {
            emit_data_region(f, ctx, bank, blk->start_addr, blk->end_addr);
        }
    }

    /* Also emit unanalyzed regions as raw data.
     * Walk through the bank and find gaps between analyzed regions. */
    {
        /* Build a sorted list of all covered address ranges */
        uint16_t covered_start[MAX_BLOCKS_PER_BANK];
        uint16_t covered_end[MAX_BLOCKS_PER_BANK];
        int covered_count = 0;

        for (int b = 0; b < ba->block_count; b++) {
            covered_start[covered_count] = ba->blocks[b].start_addr;
            covered_end[covered_count] = ba->blocks[b].end_addr;
            covered_count++;
        }

        /* Sort by start address */
        for (int i = 1; i < covered_count; i++) {
            uint16_t ks = covered_start[i];
            uint16_t ke = covered_end[i];
            int j = i - 1;
            while (j >= 0 && covered_start[j] > ks) {
                covered_start[j + 1] = covered_start[j];
                covered_end[j + 1] = covered_end[j];
                j--;
            }
            covered_start[j + 1] = ks;
            covered_end[j + 1] = ke;
        }

        /* Find gaps and emit as data */
        uint16_t cursor = base;
        for (int i = 0; i < covered_count; i++) {
            if (covered_start[i] > cursor && covered_start[i] > base) {
                emit_data_region(f, ctx, bank, cursor, covered_start[i]);
            }
            if (covered_end[i] > cursor) {
                cursor = covered_end[i];
            }
        }
        /* Trailing unanalyzed data */
        if (cursor < bank_end) {
            /* Only emit if the bank actually has ROM data */
            size_t bank_rom_start = (size_t)bank * BANK_SIZE;
            if (bank_rom_start < ctx->rom_size) {
                size_t avail = ctx->rom_size - bank_rom_start;
                uint16_t actual_end = base + (uint16_t)(avail < BANK_SIZE ? avail : BANK_SIZE);
                if (cursor < actual_end) {
                    emit_data_region(f, ctx, bank, cursor, actual_end);
                }
            }
        }
    }

    /* Emit all functions */
    for (int fi = 0; fi < ba->function_count; fi++) {
        emit_function(f, ctx, bank, fi);
    }

    fclose(f);

    printf("codegen: wrote %s (%d functions)\n", file_path, ba->function_count);
    return 0;
}

/* --------------------------------------------------------------------------
 * codegen_emit_dispatch - generate dispatch table
 * -------------------------------------------------------------------------- */

int codegen_emit_dispatch(codegen_ctx_t *ctx) {
    char dir_path[512];
    char h_path[512];
    char c_path[512];

    snprintf(dir_path, sizeof(dir_path), "%s", ctx->output_dir);
    ensure_dir(dir_path);

    /* ------ dispatch.h ------ */
    snprintf(h_path, sizeof(h_path), "%s/dispatch.h", ctx->output_dir);
    FILE *hf = fopen(h_path, "w");
    if (!hf) {
        fprintf(stderr, "codegen: cannot open %s: %s\n", h_path, strerror(errno));
        return -1;
    }

    fprintf(hf, "/* dispatch.h - auto-generated cross-bank dispatch */\n");
    fprintf(hf, "#ifndef DISPATCH_H\n");
    fprintf(hf, "#define DISPATCH_H\n\n");
    fprintf(hf, "#include <stdint.h>\n");
    fprintf(hf, "#include \"hal/cpu.h\"\n\n");
    fprintf(hf, "/* Call a function by bank and address */\n");
    fprintf(hf, "void dispatch_call(gb_state_t *gb, uint8_t bank, uint16_t addr);\n\n");
    fprintf(hf, "/* Jump (tail-call) to a function by address */\n");
    fprintf(hf, "void dispatch_jump(gb_state_t *gb, uint16_t addr);\n\n");
    fprintf(hf, "/* Initialize dispatch system */\n");
    fprintf(hf, "void dispatch_init(gb_state_t *gb);\n\n");
    fprintf(hf, "/* Run the game (calls entry point in a loop) */\n");
    fprintf(hf, "void dispatch_run(gb_state_t *gb);\n\n");

    /* Emit extern declarations for all known function entry points */
    for (int b = 0; b < ctx->analysis->num_banks; b++) {
        const bank_analysis_t *ba = &ctx->analysis->banks[b];
        for (int fi = 0; fi < ba->function_count; fi++) {
            char fname[64];
            func_name(fname, sizeof(fname), b, ba->functions[fi].entry_addr);
            fprintf(hf, "extern void %s(gb_state_t *gb);\n", fname);
        }
    }

    fprintf(hf, "\n#endif /* DISPATCH_H */\n");
    fclose(hf);

    /* ------ dispatch.c ------ */
    snprintf(c_path, sizeof(c_path), "%s/dispatch.c", ctx->output_dir);
    FILE *cf = fopen(c_path, "w");
    if (!cf) {
        fprintf(stderr, "codegen: cannot open %s: %s\n", c_path, strerror(errno));
        return -1;
    }

    fprintf(cf, "/* dispatch.c - auto-generated cross-bank dispatch */\n\n");
    fprintf(cf, "#include \"dispatch.h\"\n");
    fprintf(cf, "#include <stdio.h>\n");
    fprintf(cf, "#include <stdlib.h>\n\n");

    /* Build a dispatch table as a switch statement.
     * For each bank, list known function entry points. */
    fprintf(cf, "/* Dispatch table entry */\n");
    fprintf(cf, "typedef void (*dispatch_fn_t)(gb_state_t *gb);\n\n");

    /* Generate dispatch_call */
    fprintf(cf, "void dispatch_call(gb_state_t *gb, uint8_t bank, uint16_t addr) {\n");
    fprintf(cf, "    switch (bank) {\n");

    for (int b = 0; b < ctx->analysis->num_banks; b++) {
        const bank_analysis_t *ba = &ctx->analysis->banks[b];
        if (ba->function_count == 0) continue;

        fprintf(cf, "    case 0x%02X:\n", b);
        fprintf(cf, "        switch (addr) {\n");

        for (int fi = 0; fi < ba->function_count; fi++) {
            char fname[64];
            func_name(fname, sizeof(fname), b, ba->functions[fi].entry_addr);
            fprintf(cf, "        case 0x%04X: %s(gb); return;\n",
                    ba->functions[fi].entry_addr, fname);
        }

        fprintf(cf, "        default: break;\n");
        fprintf(cf, "        }\n");
        fprintf(cf, "        break;\n");
    }

    fprintf(cf, "    default: break;\n");
    fprintf(cf, "    }\n");
    fprintf(cf, "    fprintf(stderr, \"dispatch_call: unknown target bank=%%02X addr=%%04X\\n\", bank, addr);\n");
    fprintf(cf, "}\n\n");

    /* Generate dispatch_jump - for JP (HL) and similar indirect jumps.
     * We search across all banks for a matching function entry point,
     * preferring bank 0 for addresses in the 0x0000-0x3FFF range. */
    fprintf(cf, "void dispatch_jump(gb_state_t *gb, uint16_t addr) {\n");
    fprintf(cf, "    /* Try to find a function at this address in the current bank context */\n");
    fprintf(cf, "    switch (addr) {\n");

    /* Emit cases for all unique function addresses across all banks.
     * Bank 0 functions are always accessible (ROM bank 0 is always mapped). */
    {
        const bank_analysis_t *ba0 = &ctx->analysis->banks[0];
        for (int fi = 0; fi < ba0->function_count; fi++) {
            char fname[64];
            func_name(fname, sizeof(fname), 0, ba0->functions[fi].entry_addr);
            fprintf(cf, "    case 0x%04X: %s(gb); return;\n",
                    ba0->functions[fi].entry_addr, fname);
        }
    }

    fprintf(cf, "    default: break;\n");
    fprintf(cf, "    }\n");
    fprintf(cf, "    fprintf(stderr, \"dispatch_jump: unknown target addr=%%04X\\n\", addr);\n");
    fprintf(cf, "}\n\n");

    /* dispatch_init / dispatch_run */
    fprintf(cf, "void dispatch_init(gb_state_t *gb) {\n");
    fprintf(cf, "    (void)gb;\n");
    fprintf(cf, "}\n\n");
    fprintf(cf, "void dispatch_run(gb_state_t *gb) {\n");
    fprintf(cf, "    while (gb->running && gb->cycles < gb->target_cycles) {\n");
    fprintf(cf, "        func_b00_0150(gb);\n");
    fprintf(cf, "    }\n");
    fprintf(cf, "}\n");

    fclose(cf);

    printf("codegen: wrote %s\n", h_path);
    printf("codegen: wrote %s\n", c_path);
    return 0;
}

/* --------------------------------------------------------------------------
 * codegen_emit_all - generate everything
 * -------------------------------------------------------------------------- */

int codegen_emit_all(codegen_ctx_t *ctx) {
    int result = 0;

    printf("codegen: generating C code for %s (%d banks)\n",
           ctx->game_name, ctx->analysis->num_banks);

    /* Emit each bank */
    for (int b = 0; b < ctx->analysis->num_banks; b++) {
        int r = codegen_emit_bank(ctx, b);
        if (r != 0) {
            fprintf(stderr, "codegen: failed to emit bank %02X\n", b);
            result = -1;
        }
    }

    /* Emit dispatch table */
    if (codegen_emit_dispatch(ctx) != 0) {
        fprintf(stderr, "codegen: failed to emit dispatch table\n");
        result = -1;
    }

    if (result == 0) {
        printf("codegen: all files generated successfully in %s\n", ctx->output_dir);
    }

    return result;
}
