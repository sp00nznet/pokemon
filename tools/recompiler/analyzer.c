#include "analyzer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                   */
/* ------------------------------------------------------------------ */

/* Convert a bank number + mapped address (0x0000-0x7FFF) to a ROM file
 * offset.  Bank 0 is at offset 0, bank N >= 1 is at offset N*BANK_SIZE. */
static size_t bank_rom_offset(int bank)
{
    return (size_t)bank * BANK_SIZE;
}

/* Convert a mapped address (0x0000-0x7FFF) to a bank-local offset.
 * For bank 0: local offset = addr  (range 0x0000-0x3FFF)
 * For bank N: local offset = addr - 0x4000 (range 0x0000-0x3FFF)
 * Returns false if addr is out of range for the bank. */
static bool addr_to_local(int bank, uint16_t addr, uint16_t *local_out)
{
    if (bank == 0) {
        if (addr >= BANK_SIZE) return false;
        *local_out = addr;
        return true;
    } else {
        if (addr < 0x4000 || addr >= 0x8000) return false;
        *local_out = addr - 0x4000;
        return true;
    }
}

/* Check whether an address belongs to the given bank's mapped region. */
static bool addr_in_bank(int bank, uint16_t addr)
{
    if (bank == 0) return addr < 0x4000;
    return (addr >= 0x4000 && addr < 0x8000);
}

/* Resolve a JR target: addr is the address of the JR instruction,
 * imm8 is the signed offset byte.  Result = addr + 2 + (int8_t)imm8. */
static uint16_t resolve_jr_target(uint16_t addr, uint8_t imm8)
{
    return (uint16_t)(addr + 2 + (int8_t)imm8);
}

/* RST target from operand enum */
static uint16_t rst_target(sm83_operand_t op)
{
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

/* ------------------------------------------------------------------ */
/*  Block management helpers                                           */
/* ------------------------------------------------------------------ */

/* Find (or return NULL) an existing block whose range contains addr. */
static basic_block_t *find_block_containing(bank_analysis_t *ba, uint16_t addr)
{
    for (int i = 0; i < ba->block_count; i++) {
        basic_block_t *b = &ba->blocks[i];
        if (!b->is_data && addr >= b->start_addr && addr < b->end_addr)
            return b;
    }
    return NULL;
}

/* Find an existing block that starts exactly at addr. */
static basic_block_t *find_block_starting_at(bank_analysis_t *ba, uint16_t addr)
{
    for (int i = 0; i < ba->block_count; i++) {
        if (ba->blocks[i].start_addr == addr && !ba->blocks[i].is_data)
            return &ba->blocks[i];
    }
    return NULL;
}

/* Allocate a new block.  Returns NULL if the bank's block array is full. */
static basic_block_t *alloc_block(bank_analysis_t *ba)
{
    if (ba->block_count >= MAX_BLOCKS_PER_BANK)
        return NULL;
    basic_block_t *b = &ba->blocks[ba->block_count++];
    memset(b, 0, sizeof(*b));
    b->function_id = -1;
    return b;
}

/* Split an existing block at 'addr'.  The original block keeps the portion
 * before addr, and a new block is created starting at addr.  Control-flow
 * edges are transferred to the new block.  Returns the new (lower-half)
 * block, or NULL on error. */
static basic_block_t *split_block_at(bank_analysis_t *ba, basic_block_t *orig,
                                     uint16_t addr)
{
    if (addr <= orig->start_addr || addr >= orig->end_addr)
        return NULL;
    /* Only split at instruction boundaries, not mid-instruction */
    uint16_t local = addr & (BANK_SIZE - 1);
    if (ba->is_inst_start[local] == false && ba->is_code[local])
        return NULL;  /* This is an operand byte, not an instruction start */
    basic_block_t *tail = alloc_block(ba);
    if (!tail) return NULL;

    /* The new 'tail' block inherits the latter part of the original. */
    tail->start_addr     = addr;
    tail->end_addr       = orig->end_addr;
    tail->bank           = orig->bank;
    tail->exit_type      = orig->exit_type;
    tail->has_fallthrough = orig->has_fallthrough;
    tail->visited        = true;
    tail->function_id    = orig->function_id;
    tail->num_successors = orig->num_successors;
    memcpy(tail->successor_addr, orig->successor_addr, sizeof(orig->successor_addr));
    memcpy(tail->successor_bank, orig->successor_bank, sizeof(orig->successor_bank));

    /* Recount instructions (approximate: we just mark the split). */
    /* We will recount properly below by scanning the is_code bitmap. */
    tail->inst_count = 0; /* will be fixed up later */

    /* The original block is truncated. */
    orig->end_addr       = addr;
    orig->exit_type      = BRANCH_NONE;
    orig->has_fallthrough = true;
    orig->num_successors = 1;
    orig->successor_addr[0] = addr;
    orig->successor_bank[0] = orig->bank;

    /* Recount instruction counts by scanning the is_code bitmap and
     * re-decoding.  For simplicity we just leave inst_count approximate;
     * it is mainly informational. */
    orig->inst_count = 0; /* recounted as informational only */

    return tail;
}

/* ------------------------------------------------------------------ */
/*  Function management helpers                                        */
/* ------------------------------------------------------------------ */

static function_info_t *find_function(bank_analysis_t *ba, uint16_t addr)
{
    for (int i = 0; i < ba->function_count; i++) {
        if (ba->functions[i].entry_addr == addr)
            return &ba->functions[i];
    }
    return NULL;
}

static function_info_t *add_function(bank_analysis_t *ba, uint16_t addr,
                                     uint8_t bank, bool is_interrupt)
{
    if (find_function(ba, addr))
        return find_function(ba, addr);
    if (ba->function_count >= MAX_FUNCTIONS_PER_BANK)
        return NULL;
    function_info_t *f = &ba->functions[ba->function_count];
    memset(f, 0, sizeof(*f));
    f->entry_addr   = addr;
    f->bank         = bank;
    f->name         = sym_addr_name(addr);
    f->is_interrupt = is_interrupt;
    f->first_block_idx = -1;
    ba->function_count++;
    return f;
}

/* ------------------------------------------------------------------ */
/*  Cross-bank call recording                                          */
/* ------------------------------------------------------------------ */

static void record_xbank_call(analysis_ctx_t *ctx, uint16_t addr, uint8_t bank)
{
    /* Avoid duplicates */
    for (int i = 0; i < ctx->xbank_call_count; i++) {
        if (ctx->xbank_calls[i].addr == addr &&
            ctx->xbank_calls[i].bank == bank)
            return;
    }
    if (ctx->xbank_call_count >= 8192)
        return;
    ctx->xbank_calls[ctx->xbank_call_count].addr = addr;
    ctx->xbank_calls[ctx->xbank_call_count].bank = bank;
    ctx->xbank_call_count++;
}

/* ------------------------------------------------------------------ */
/*  Bank-switch pattern detection                                      */
/* ------------------------------------------------------------------ */

/* Look backwards from 'pos' (a bank-local offset) for a recent
 * LD A, imm8 instruction.  We scan up to 6 bytes back (two 3-byte
 * instructions).  Returns the immediate value in *bank_num and true
 * if found. */
static bool detect_ld_a_imm(const uint8_t *bank_data, uint16_t pos,
                             uint8_t *bank_num)
{
    /* Pattern: 0x3E nn  (LD A, n) */
    for (int lookback = 2; lookback <= 10 && lookback <= (int)pos; lookback++) {
        uint16_t check = pos - lookback;
        if (bank_data[check] == 0x3E) {
            *bank_num = bank_data[check + 1];
            return true;
        }
    }
    return false;
}

/* Check if a LD B,imm / LD HL,imm16 pattern preceded a CALL.
 * Returns true and fills bank_num with B value if found.
 * Pattern: 06 nn 21 lo hi  (LD B,n / LD HL,addr) */
static bool detect_ld_b_hl_pattern(const uint8_t *bank_data, uint16_t pos,
                                    uint8_t *bank_num, uint16_t *target_addr)
{
    /* We need at least 5 bytes before pos:  06 nn 21 lo hi */
    if (pos < 5) return false;

    /* Check for LD B,n at pos-5, LD HL,nn at pos-3 */
    if (bank_data[pos - 5] == 0x06 && bank_data[pos - 3] == 0x21) {
        *bank_num = bank_data[pos - 4];
        *target_addr = bank_data[pos - 2] | ((uint16_t)bank_data[pos - 1] << 8);
        return true;
    }

    /* Also check for just LD B,n at pos-2 (if the LD HL was earlier) */
    if (pos >= 2 && bank_data[pos - 2] == 0x06) {
        *bank_num = bank_data[pos - 1];
        *target_addr = 0; /* unknown */
        return true;
    }

    return false;
}

/* ------------------------------------------------------------------ */
/*  Recursive descent trace                                            */
/* ------------------------------------------------------------------ */

/* Worklist for iterative recursive descent (avoid deep C recursion). */
#define WORKLIST_SIZE 16384

typedef struct {
    uint16_t addr;
    int      function_id;   /* -1 if not inside a known function yet */
} work_item_t;

static work_item_t g_worklist[WORKLIST_SIZE];
static int         g_worklist_count;

static void worklist_push(uint16_t addr, int function_id)
{
    if (g_worklist_count >= WORKLIST_SIZE)
        return;
    g_worklist[g_worklist_count].addr = addr;
    g_worklist[g_worklist_count].function_id = function_id;
    g_worklist_count++;
}

/* Trace code reachable from a starting address within a single bank.
 * ctx   - the full analysis context
 * bank  - which bank we are analysing
 * start - the mapped address to begin tracing (must be in the bank's range)
 * func_id - function index to assign to discovered blocks (-1 if none)
 */
static void trace_from(analysis_ctx_t *ctx, int bank, uint16_t start,
                        int func_id)
{
    bank_analysis_t *ba = &ctx->banks[bank];
    size_t bank_file_offset = bank_rom_offset(bank);

    g_worklist_count = 0;
    worklist_push(start, func_id);

    while (g_worklist_count > 0) {
        /* Pop from worklist */
        g_worklist_count--;
        uint16_t addr = g_worklist[g_worklist_count].addr;
        int cur_func_id = g_worklist[g_worklist_count].function_id;

        /* Validate address is in this bank's region */
        if (!addr_in_bank(bank, addr))
            continue;

        uint16_t local;
        if (!addr_to_local(bank, addr, &local))
            continue;

        /* Already have a block starting here? Skip, but update function_id
         * if the block doesn't have one yet (fixes CALL targets that were
         * traced before their function record was created). */
        basic_block_t *existing = find_block_starting_at(ba, addr);
        if (existing) {
            if (existing->function_id < 0 && cur_func_id >= 0)
                existing->function_id = cur_func_id;
            if (existing->visited)
                continue;
            existing->visited = true;
            continue;
        }

        /* Check if addr falls in the middle of an existing block */
        basic_block_t *container = find_block_containing(ba, addr);
        if (container) {
            /* Split the existing block at this address */
            split_block_at(ba, container, addr);
            /* The new block at 'addr' now exists; we can continue
             * without re-tracing because the code was already decoded. */
            continue;
        }

        /* Start a new basic block */
        basic_block_t *block = alloc_block(ba);
        if (!block) continue;

        block->start_addr = addr;
        block->bank       = (uint8_t)bank;
        block->visited    = true;
        block->function_id = cur_func_id;

        /* Decode instructions sequentially until we hit a terminator
         * or run into already-decoded code. */
        uint16_t pc = addr;
        int inst_count = 0;

        for (;;) {
            if (!addr_in_bank(bank, pc))
                break;

            uint16_t pc_local;
            if (!addr_to_local(bank, pc, &pc_local))
                break;

            /* If we are about to decode into an existing block's start,
             * end this block with fallthrough. */
            if (pc != addr && find_block_starting_at(ba, pc)) {
                block->end_addr       = pc;
                block->inst_count     = inst_count;
                block->has_fallthrough = true;
                block->exit_type      = BRANCH_NONE;
                block->num_successors = 1;
                block->successor_addr[0] = pc;
                block->successor_bank[0] = (uint8_t)bank;
                goto next_worklist;
            }

            /* Already marked as an instruction start by another block?
             * Then this is a block boundary.  We use is_inst_start
             * (not is_code) so that operand bytes of multi-byte
             * instructions don't trigger spurious boundaries. */
            if (pc != addr && ba->is_inst_start[pc_local]) {
                block->end_addr       = pc;
                block->inst_count     = inst_count;
                block->has_fallthrough = true;
                block->exit_type      = BRANCH_NONE;
                block->num_successors = 1;
                block->successor_addr[0] = pc;
                block->successor_bank[0] = (uint8_t)bank;

                /* Make sure there is a block at that address. */
                if (!find_block_starting_at(ba, pc)) {
                    /* Need to split whatever block contains pc */
                    basic_block_t *cont = find_block_containing(ba, pc);
                    if (cont)
                        split_block_at(ba, cont, pc);
                }
                goto next_worklist;
            }

            /* Bounds check against ROM data */
            size_t file_off = bank_file_offset + pc_local;
            if (file_off >= ctx->rom_size)
                break;

            size_t remaining = ctx->rom_size - file_off;
            const uint8_t *data = ctx->rom_data + file_off;

            /* Decode */
            uint8_t  imm8;
            uint16_t imm16;
            sm83_inst_t inst = sm83_decode(data, remaining, &imm8, &imm16);

            if (inst.mnemonic == OP_INVALID) {
                /* Treat as end of block / data */
                block->end_addr   = pc + 1;
                block->inst_count = inst_count + 1;
                block->exit_type  = BRANCH_NONE;
                block->has_fallthrough = false;
                ba->is_code[pc_local] = true;
                ba->is_inst_start[pc_local] = true;
                goto next_worklist;
            }

            /* Mark bytes as code */
            ba->is_inst_start[pc_local] = true;  /* Only first byte */
            for (int b = 0; b < inst.length; b++) {
                uint16_t off = pc_local + b;
                if (off < BANK_SIZE)
                    ba->is_code[off] = true;
            }

            inst_count++;
            uint16_t next_pc = pc + inst.length;

            /* Process branches */
            switch (inst.branch) {
            case BRANCH_NONE:
                /* Continue sequential decode */
                pc = next_pc;
                continue;

            case BRANCH_JUMP: {
                /* Unconditional jump - block ends, no fallthrough */
                block->end_addr       = next_pc;
                block->inst_count     = inst_count;
                block->exit_type      = BRANCH_JUMP;
                block->has_fallthrough = false;

                uint16_t target;
                if (inst.mnemonic == OP_JR)
                    target = resolve_jr_target(pc, imm8);
                else
                    target = imm16;

                if (addr_in_bank(bank, target)) {
                    block->num_successors = 1;
                    block->successor_addr[0] = target;
                    block->successor_bank[0] = (uint8_t)bank;
                    worklist_push(target, cur_func_id);
                } else if (target >= 0x4000 && target < 0x8000 && bank == 0) {
                    /* JP from bank 0 to the switchable bank area.
                     * We can't know the target bank statically, so
                     * register this as a cross-bank JP target for ALL
                     * banks that have code at this address. */
                    if (ctx->xbank_call_count < 8192) {
                        ctx->xbank_calls[ctx->xbank_call_count].addr = target;
                        ctx->xbank_calls[ctx->xbank_call_count].bank = 0xFF; /* "any" */
                        ctx->xbank_call_count++;
                    }
                }
                goto next_worklist;
            }

            case BRANCH_JUMP_COND: {
                /* Conditional jump - block ends, has fallthrough + taken */
                block->end_addr       = next_pc;
                block->inst_count     = inst_count;
                block->exit_type      = BRANCH_JUMP_COND;
                block->has_fallthrough = true;

                uint16_t target;
                if (inst.mnemonic == OP_JR)
                    target = resolve_jr_target(pc, imm8);
                else
                    target = imm16;

                block->num_successors = 2;
                block->successor_addr[0] = next_pc;
                block->successor_bank[0] = (uint8_t)bank;
                block->successor_addr[1] = target;
                block->successor_bank[1] = (uint8_t)bank;

                worklist_push(next_pc, cur_func_id);
                if (addr_in_bank(bank, target))
                    worklist_push(target, cur_func_id);
                goto next_worklist;
            }

            case BRANCH_CALL: {
                /* Unconditional call - block continues after the call.
                 * Record call target as a function entry. */
                uint16_t target = imm16;

                /* Check for bankswitch pattern: LD A,n ... CALL addr */
                uint8_t switch_bank;
                uint16_t switch_target;
                bool is_bankswitch = false;

                if (detect_ld_a_imm(ctx->rom_data + bank_file_offset,
                                    pc_local, &switch_bank)) {
                    /* Heuristic: if LD A,n precedes a CALL and switch_bank
                     * is a valid bank number, this could be a bankswitch call.
                     * Applies both for calls to Bankswitch stub (target < 0x4000)
                     * and for direct calls to switched area (target >= 0x4000
                     * from bank 0 after LD ($2000), A). */
                    if (switch_bank > 0 && switch_bank < ctx->num_banks) {
                        is_bankswitch = true;
                    }
                }

                /* Also check LD B,n / LD HL,addr / CALL pattern */
                if (!is_bankswitch &&
                    detect_ld_b_hl_pattern(ctx->rom_data + bank_file_offset,
                                           pc_local, &switch_bank,
                                           &switch_target)) {
                    if (switch_bank > 0 && switch_bank < ctx->num_banks &&
                        target < 0x4000) {
                        is_bankswitch = true;
                        if (switch_target >= 0x4000 && switch_target < 0x8000) {
                            record_xbank_call(ctx, switch_target, switch_bank);
                        }
                    }
                }

                if (is_bankswitch && switch_bank > 0) {
                    /* Record the cross-bank call for later analysis */
                    /* The actual target in the switched bank is harder to
                     * determine; record what we can. */
                    if (cur_func_id >= 0 && cur_func_id < ba->function_count)
                        ba->functions[cur_func_id].has_bank_switch = true;
                }

                /* Record the CALL target as a function entry in whatever
                 * bank it belongs to. */
                if (target < 0x4000) {
                    /* Bank 0 call target */
                    if (ctx->banks[0].function_count < MAX_FUNCTIONS_PER_BANK)
                        add_function(&ctx->banks[0], target, 0, false);
                } else if (target >= 0x4000 && target < 0x8000) {
                    if (bank == 0) {
                        /* Bank 0 CALL to switchable area: the target bank
                         * depends on what was written to MBC register.
                         * Try to detect the bank from preceding LD A,n. */
                        uint8_t call_bank = 0xFF; /* default: wildcard */
                        if (is_bankswitch && switch_bank > 0 &&
                            switch_bank < ctx->num_banks) {
                            call_bank = switch_bank;
                            /* Also record wildcard so the function gets
                             * created in ALL banks for robustness. */
                            record_xbank_call(ctx, target, 0xFF);
                        }
                        record_xbank_call(ctx, target, call_bank);
                    } else {
                        /* Same bank call target */
                        if (ba->function_count < MAX_FUNCTIONS_PER_BANK)
                            add_function(ba, target, (uint8_t)bank, false);
                    }
                }

                /* Flow continues after the CALL */
                block->end_addr       = next_pc;
                block->inst_count     = inst_count;
                block->exit_type      = BRANCH_CALL;
                block->has_fallthrough = true;

                block->num_successors = 2;
                block->successor_addr[0] = next_pc;
                block->successor_bank[0] = (uint8_t)bank;
                block->successor_addr[1] = target;
                if (target < 0x4000)
                    block->successor_bank[1] = 0;
                else
                    block->successor_bank[1] = (uint8_t)bank;

                /* Continue tracing from call target (within same bank
                 * or bank 0).  Use the correct function_id so the
                 * target's blocks get assigned to their function. */
                if (addr_in_bank(bank, target)) {
                    function_info_t *tf = find_function(ba, target);
                    int tf_id = tf ? (int)(tf - ba->functions) : -1;
                    worklist_push(target, tf_id);
                } else if (target < 0x4000 && bank != 0)
                    ; /* Will be traced when bank 0 is analysed */

                worklist_push(next_pc, cur_func_id);
                goto next_worklist;
            }

            case BRANCH_CALL_COND: {
                /* Conditional call - same as CALL but fallthrough always */
                uint16_t target = imm16;

                if (target < 0x4000) {
                    if (ctx->banks[0].function_count < MAX_FUNCTIONS_PER_BANK)
                        add_function(&ctx->banks[0], target, 0, false);
                } else if (target >= 0x4000 && target < 0x8000) {
                    if (ba->function_count < MAX_FUNCTIONS_PER_BANK)
                        add_function(ba, target, (uint8_t)bank, false);
                }

                block->end_addr       = next_pc;
                block->inst_count     = inst_count;
                block->exit_type      = BRANCH_CALL_COND;
                block->has_fallthrough = true;

                block->num_successors = 2;
                block->successor_addr[0] = next_pc;
                block->successor_bank[0] = (uint8_t)bank;
                block->successor_addr[1] = target;
                if (target < 0x4000)
                    block->successor_bank[1] = 0;
                else
                    block->successor_bank[1] = (uint8_t)bank;

                if (addr_in_bank(bank, target)) {
                    function_info_t *tf = find_function(ba, target);
                    int tf_id = tf ? (int)(tf - ba->functions) : -1;
                    worklist_push(target, tf_id);
                }
                worklist_push(next_pc, cur_func_id);
                goto next_worklist;
            }

            case BRANCH_RET:
            case BRANCH_RET_COND: {
                /* RET/RETI - unconditional return ends the block.
                 * Conditional RET has fallthrough. */
                block->end_addr   = next_pc;
                block->inst_count = inst_count;
                block->exit_type  = inst.branch;

                if (inst.branch == BRANCH_RET_COND) {
                    block->has_fallthrough = true;
                    block->num_successors = 1;
                    block->successor_addr[0] = next_pc;
                    block->successor_bank[0] = (uint8_t)bank;
                    worklist_push(next_pc, cur_func_id);
                } else {
                    block->has_fallthrough = false;
                    block->num_successors = 0;
                }
                goto next_worklist;
            }

            case BRANCH_RST: {
                /* RST is like a CALL to a fixed vector in bank 0 */
                uint16_t target = rst_target(inst.op1);

                if (ctx->banks[0].function_count < MAX_FUNCTIONS_PER_BANK)
                    add_function(&ctx->banks[0], target, 0, false);

                block->end_addr       = next_pc;
                block->inst_count     = inst_count;
                block->exit_type      = BRANCH_RST;
                block->has_fallthrough = true;

                block->num_successors = 2;
                block->successor_addr[0] = next_pc;
                block->successor_bank[0] = (uint8_t)bank;
                block->successor_addr[1] = target;
                block->successor_bank[1] = 0;

                worklist_push(next_pc, cur_func_id);
                goto next_worklist;
            }

            case BRANCH_JUMP_INDIRECT:
                /* JP (HL) - we cannot statically resolve the target */
                block->end_addr        = next_pc;
                block->inst_count      = inst_count;
                block->exit_type       = BRANCH_JUMP_INDIRECT;
                block->has_fallthrough = false;
                block->num_successors  = 0;
                goto next_worklist;

            case BRANCH_HALT:
                /* HALT waits for interrupt - execution continues after */
                block->end_addr        = next_pc;
                block->inst_count      = inst_count;
                block->exit_type       = BRANCH_HALT;
                block->has_fallthrough = true;
                block->num_successors  = 1;
                block->successor_addr[0] = next_pc;
                block->successor_bank[0] = (uint8_t)bank;
                worklist_push(next_pc, cur_func_id);
                goto next_worklist;

            case BRANCH_STOP:
                /* STOP - typically not followed by code */
                block->end_addr        = next_pc;
                block->inst_count      = inst_count;
                block->exit_type       = BRANCH_STOP;
                block->has_fallthrough = false;
                block->num_successors  = 0;
                goto next_worklist;
            }

            /* Shouldn't reach here, but be safe */
            pc = next_pc;
        }

        /* Ran off end of bank without hitting a terminator */
        block->end_addr        = pc;
        block->inst_count      = inst_count;
        block->has_fallthrough = false;
        block->num_successors  = 0;

next_worklist:
        ;  /* continue with next worklist item */
    }
}

/* ------------------------------------------------------------------ */
/*  Function-ID propagation                                            */
/* ------------------------------------------------------------------ */

/* After tracing, many blocks still have function_id == -1 because they
 * were first reached from a different context (e.g. as part of a JP
 * chain) before their owning function was created.  This pass walks
 * from each function's entry block through intra-function successors
 * and assigns the correct function_id. */
static void propagate_function_ids(bank_analysis_t *ba)
{
    /* BFS queue - reuse a local array (at most MAX_BLOCKS_PER_BANK entries) */
    static uint16_t queue[MAX_BLOCKS_PER_BANK];

    for (int fi = 0; fi < ba->function_count; fi++) {
        function_info_t *f = &ba->functions[fi];
        basic_block_t *entry = find_block_starting_at(ba, f->entry_addr);
        if (!entry) continue;

        /* Claim the entry block for this function - ALWAYS claim it,
         * even if a previous function's fall-through already claimed it.
         * A function's entry block definitionally belongs to it. */
        entry->function_id = fi;

        /* Update first_block_idx */
        if (f->first_block_idx < 0)
            f->first_block_idx = (int)(entry - ba->blocks);

        /* BFS through intra-function successors */
        int head = 0, tail = 0;
        queue[tail++] = f->entry_addr;

        while (head < tail) {
            uint16_t addr = queue[head++];
            basic_block_t *blk = find_block_starting_at(ba, addr);
            if (!blk) continue;

            /* Determine which successors are intra-function edges.
             * For CALL/CALL_COND/RST the first successor is the
             * continuation (same function), the second is the call
             * target (different function) - only follow the first. */
            int max_succ = blk->num_successors;
            if (blk->exit_type == BRANCH_CALL ||
                blk->exit_type == BRANCH_CALL_COND ||
                blk->exit_type == BRANCH_RST) {
                max_succ = 1; /* only follow continuation */
            }

            for (int s = 0; s < max_succ; s++) {
                if (blk->successor_bank[s] != ba->bank)
                    continue;

                uint16_t succ_addr = blk->successor_addr[s];
                basic_block_t *succ = find_block_starting_at(ba, succ_addr);
                if (!succ) continue;

                /* Only claim blocks that don't already belong to a function */
                if (succ->function_id >= 0)
                    continue;

                /* Don't claim blocks that are entry points of OTHER functions.
                 * This prevents fall-through from function A absorbing
                 * function B's entry block, which would leave B empty. */
                {
                    function_info_t *succ_func = find_function(ba, succ_addr);
                    if (succ_func && succ_func != f)
                        continue;
                }

                succ->function_id = fi;
                if (tail < MAX_BLOCKS_PER_BANK)
                    queue[tail++] = succ_addr;
            }
        }
    }
}

/* After propagation, scan all blocks for JP/JR targets that cross
 * function boundaries.  When function A JPs to a block owned by
 * function B, the codegen will emit a tail-call to "func_bNN_XXXX"
 * at the JP target address.  That function must exist.
 *
 * This pass creates new function entries for such JP targets and
 * reassigns the target block (and its intra-function successors)
 * to the new function.  This effectively "splits" the target
 * block out of its original function. */
static void split_cross_function_jp_targets(bank_analysis_t *ba)
{
    static uint16_t queue[MAX_BLOCKS_PER_BANK];
    bool changed = true;

    while (changed) {
        changed = false;
        for (int bi = 0; bi < ba->block_count; bi++) {
            basic_block_t *blk = &ba->blocks[bi];

            if (blk->exit_type != BRANCH_JUMP &&
                blk->exit_type != BRANCH_JUMP_COND)
                continue;
            if (blk->function_id < 0) continue;

            for (int s = 0; s < blk->num_successors; s++) {
                if (blk->successor_bank[s] != ba->bank) continue;
                uint16_t target = blk->successor_addr[s];

                /* Skip the fallthrough successor for conditional jumps */
                if (blk->exit_type == BRANCH_JUMP_COND && s == 0)
                    continue;

                basic_block_t *tgt = find_block_starting_at(ba, target);
                if (!tgt) continue;
                if (tgt->function_id == blk->function_id) continue;

                /* Target block belongs to a different function.
                 * Check if a function entry already exists here. */
                if (find_function(ba, target)) continue;

                /* Create a new function at this address */
                function_info_t *nf = add_function(ba, target, ba->bank, false);
                if (!nf) continue;
                int new_fi = (int)(nf - ba->functions);

                /* Reassign the target block and propagate via BFS */
                tgt->function_id = new_fi;
                nf->first_block_idx = (int)(tgt - ba->blocks);

                int head = 0, tail = 0;
                queue[tail++] = target;

                while (head < tail) {
                    uint16_t addr = queue[head++];
                    basic_block_t *cur = find_block_starting_at(ba, addr);
                    if (!cur) continue;

                    int max_succ = cur->num_successors;
                    if (cur->exit_type == BRANCH_CALL ||
                        cur->exit_type == BRANCH_CALL_COND ||
                        cur->exit_type == BRANCH_RST) {
                        max_succ = 1; /* only continuation */
                    }

                    for (int ss = 0; ss < max_succ; ss++) {
                        if (cur->successor_bank[ss] != ba->bank)
                            continue;
                        uint16_t succ_addr = cur->successor_addr[ss];
                        basic_block_t *succ = find_block_starting_at(ba, succ_addr);
                        if (!succ) continue;
                        /* Claim blocks that belonged to the same OLD function
                         * or have no function yet */
                        if (succ->function_id != tgt->function_id &&
                            succ->function_id != new_fi) {
                            /* Don't steal from unrelated functions - only claim
                             * blocks that were in the same old function or unowned */
                            if (succ->function_id >= 0 &&
                                succ->function_id != blk->function_id)
                                continue;
                        }
                        if (succ->function_id == new_fi)
                            continue; /* already ours */
                        succ->function_id = new_fi;
                        if (tail < MAX_BLOCKS_PER_BANK)
                            queue[tail++] = succ_addr;
                    }
                }

                changed = true;
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Ensure all branch targets have block starts                        */
/* ------------------------------------------------------------------ */

/* After all tracing, some branch successor addresses may fall in the
 * middle of existing blocks (due to worklist ordering). This pass
 * ensures every successor address has a block starting at it. */
static void ensure_successor_blocks(bank_analysis_t *ba)
{
    int orig_count = ba->block_count;
    for (int i = 0; i < orig_count; i++) {
        basic_block_t *blk = &ba->blocks[i];
        if (blk->is_data) continue;

        for (int s = 0; s < blk->num_successors; s++) {
            if (blk->successor_bank[s] != ba->bank) continue;
            uint16_t target = blk->successor_addr[s];

            /* Already have a block starting here? */
            if (find_block_starting_at(ba, target)) continue;

            /* Check if target falls inside an existing block */
            basic_block_t *container = find_block_containing(ba, target);
            if (container) {
                split_block_at(ba, container, target);
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

void analysis_init(analysis_ctx_t *ctx, const uint8_t *rom, size_t rom_size,
                   int num_banks)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->rom_data  = rom;
    ctx->rom_size  = rom_size;
    ctx->num_banks = num_banks;

    ctx->banks = (bank_analysis_t *)malloc(sizeof(bank_analysis_t) * num_banks);
    if (!ctx->banks) {
        fprintf(stderr, "analyzer: failed to allocate %d banks\n", num_banks);
        return;
    }

    for (int i = 0; i < num_banks; i++) {
        memset(&ctx->banks[i], 0, sizeof(bank_analysis_t));
        ctx->banks[i].bank = (uint8_t)i;
    }

    ctx->xbank_call_count = 0;
}

void analysis_load_trace(analysis_ctx_t *ctx, const char *trace_file) {
    FILE *f = fopen(trace_file, "r");
    if (!f) {
        fprintf(stderr, "Warning: Cannot open trace file: %s\n", trace_file);
        return;
    }

    int count = 0;
    int seeded = 0;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        /* Skip comments and blank lines */
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r')
            continue;

        /* Parse "bank:addr" format (both hex) */
        int bank;
        unsigned int addr;
        if (sscanf(line, "%d:%x", &bank, &addr) != 2)
            continue;
        count++;

        /* Filter: only accept function-aligned addresses in ROM space */
        if (bank < 0 || bank >= ctx->num_banks)
            continue;
        if (bank == 0 && addr >= 0x4000)
            continue;
        if (bank > 0 && (addr < 0x4000 || addr >= 0x8000))
            continue;

        /* Only seed addresses that look like instruction boundaries.
         * Simple heuristic: check if the byte at addr is a valid first opcode. */
        size_t rom_off = (size_t)bank * BANK_SIZE + (addr - (bank == 0 ? 0 : 0x4000));
        if (rom_off >= ctx->rom_size)
            continue;
        uint8_t opcode = ctx->rom_data[rom_off];
        /* Skip if it's a data byte (0xFD, 0xDB, 0xDD, 0xE3, 0xE4, 0xF4, 0xFC are illegal) */
        if (opcode == 0xD3 || opcode == 0xDB || opcode == 0xDD ||
            opcode == 0xE3 || opcode == 0xE4 || opcode == 0xEB ||
            opcode == 0xEC || opcode == 0xED || opcode == 0xF4 ||
            opcode == 0xFC || opcode == 0xFD)
            continue;

        bank_analysis_t *ba = &ctx->banks[bank];
        if (ba->function_count < MAX_FUNCTIONS_PER_BANK &&
            !find_function(ba, (uint16_t)addr)) {
            add_function(ba, (uint16_t)addr, (uint8_t)bank, false);
            record_xbank_call(ctx, (uint16_t)addr, (uint8_t)bank);
            seeded++;
        }
    }

    fclose(f);
    printf("Loaded %d trace entries, seeded %d new function entries from %s\n",
           count, seeded, trace_file);
}

void analysis_run_bank(analysis_ctx_t *ctx, int bank)
{
    if (bank < 0 || bank >= ctx->num_banks)
        return;

    bank_analysis_t *ba = &ctx->banks[bank];

    if (bank == 0) {
        /* Bank 0: trace from all known entry points */
        sym_entry_point_t entries[32];
        int n = sym_get_entry_points(entries, 32);

        /* Also add the actual ROM entry point from the header JP at 0x0101 */
        if (ctx->rom_size >= 0x0104 && ctx->rom_data[0x0101] == 0xC3 && n < 32) {
            uint16_t rom_entry = ctx->rom_data[0x0102] | ((uint16_t)ctx->rom_data[0x0103] << 8);
            if (rom_entry < 0x4000 && rom_entry != ENTRY_POINT) {
                entries[n++] = (sym_entry_point_t){rom_entry, 0, "ROMEntryPoint"};
                printf("Added ROM entry point 0x%04X from header.\n", rom_entry);
            }
        }

        for (int i = 0; i < n; i++) {
            if (entries[i].bank != 0)
                continue;

            uint16_t addr = entries[i].addr;
            bool is_int = (addr == INT_VBLANK || addr == INT_LCD_STAT ||
                           addr == INT_TIMER  || addr == INT_SERIAL ||
                           addr == INT_JOYPAD);

            /* Create a function for this entry point */
            int func_id = -1;
            function_info_t *f = add_function(ba, addr, 0, is_int);
            if (f) {
                func_id = (int)(f - ba->functions);
            }

            /* Mark the entry block */
            trace_from(ctx, bank, addr, func_id);

            /* Tag the entry block */
            basic_block_t *entry_blk = find_block_starting_at(ba, addr);
            if (entry_blk) {
                entry_blk->is_entry_point = true;
                if (f && f->first_block_idx < 0)
                    f->first_block_idx = (int)(entry_blk - ba->blocks);
            }
        }

        /* Also trace from any function entries discovered during analysis
         * (CALL targets that point into bank 0) */
        for (int i = 0; i < ba->function_count; i++) {
            function_info_t *f = &ba->functions[i];
            if (!find_block_starting_at(ba, f->entry_addr)) {
                trace_from(ctx, bank, f->entry_addr, i);
                basic_block_t *entry_blk = find_block_starting_at(ba, f->entry_addr);
                if (entry_blk) {
                    entry_blk->is_entry_point = true;
                    if (f->first_block_idx < 0)
                        f->first_block_idx = (int)(entry_blk - ba->blocks);
                }
            }
        }
    } else {
        /* Banks 1+: trace from cross-bank call targets discovered so far.
         * bank == 0xFF means "any bank" (from JP to switchable area). */
        for (int i = 0; i < ctx->xbank_call_count; i++) {
            if (ctx->xbank_calls[i].bank != bank &&
                ctx->xbank_calls[i].bank != 0xFF)
                continue;

            uint16_t addr = ctx->xbank_calls[i].addr;
            if (!addr_in_bank(bank, addr))
                continue;

            function_info_t *f = add_function(ba, addr, (uint8_t)bank, false);
            int func_id = f ? (int)(f - ba->functions) : -1;

            trace_from(ctx, bank, addr, func_id);

            basic_block_t *entry_blk = find_block_starting_at(ba, addr);
            if (entry_blk) {
                entry_blk->is_entry_point = true;
                if (f && f->first_block_idx < 0)
                    f->first_block_idx = (int)(entry_blk - ba->blocks);
            }
        }

        /* Also trace from any CALL targets within this bank that were
         * registered as functions during tracing. */
        for (int i = 0; i < ba->function_count; i++) {
            function_info_t *f = &ba->functions[i];
            if (!find_block_starting_at(ba, f->entry_addr)) {
                trace_from(ctx, bank, f->entry_addr, i);
                basic_block_t *entry_blk = find_block_starting_at(ba, f->entry_addr);
                if (entry_blk) {
                    entry_blk->is_entry_point = true;
                    if (f->first_block_idx < 0)
                        f->first_block_idx = (int)(entry_blk - ba->blocks);
                }
            }
        }

        /* If no entry points were found at all for this bank, try tracing
         * from the beginning of the bank (0x4000).  Many banks have code
         * starting at the very beginning. */
        if (ba->block_count == 0) {
            function_info_t *f = add_function(ba, 0x4000, (uint8_t)bank, false);
            int func_id = f ? (int)(f - ba->functions) : -1;
            trace_from(ctx, bank, 0x4000, func_id);

            basic_block_t *entry_blk = find_block_starting_at(ba, 0x4000);
            if (entry_blk) {
                entry_blk->is_entry_point = true;
                if (f && f->first_block_idx < 0)
                    f->first_block_idx = (int)(entry_blk - ba->blocks);
            }
        }
    }

    /* Propagate function IDs from entry blocks through intra-function
     * successors.  This fixes blocks that were traced before their
     * owning function was created (they still have function_id == -1). */
    propagate_function_ids(ba);

    /* Create new functions for cross-function JP targets.
     * This ensures the codegen always has a valid function to tail-call. */
    split_cross_function_jp_targets(ba);

    /* Ensure all branch successor addresses have block starts.
     * Worklist ordering can leave some JR/JP targets unsplit. */
    ensure_successor_blocks(ba);

    /* Final bookkeeping: update first_block_idx and count blocks. */
    for (int fi = 0; fi < ba->function_count; fi++) {
        function_info_t *f = &ba->functions[fi];
        if (f->first_block_idx < 0) {
            basic_block_t *eb = find_block_starting_at(ba, f->entry_addr);
            if (eb)
                f->first_block_idx = (int)(eb - ba->blocks);
        }

        /* Count blocks belonging to this function */
        f->block_count = 0;
        for (int bi = 0; bi < ba->block_count; bi++) {
            if (ba->blocks[bi].function_id == fi)
                f->block_count++;
        }
    }
}

void analysis_run(analysis_ctx_t *ctx)
{
    if (!ctx->banks)
        return;

    /* Pass 1: Analyse bank 0 first to discover cross-bank calls */
    printf("Analyzing bank 0...\n");
    analysis_run_bank(ctx, 0);

    /* Pass 1.5: Seed function entry points from PredefPointers table.
     * The table is in bank 0x13 at an address encoded in GetPredefPointer.
     * GetPredefPointer (0x7E49): saves regs, then at 0x7E5B: LD HL, $7E79
     * (the PredefPointers table base). Each entry is 3 bytes: [bank, lo, hi].
     * Pokemon Red has 99 predefs (IDs 0-98). */
    if (ctx->num_banks > 0x13) {
        size_t gpp_rom_off = (size_t)0x13 * BANK_SIZE + (0x7E5B - 0x4000);
        if (gpp_rom_off + 3 <= ctx->rom_size && ctx->rom_data[gpp_rom_off] == 0x21) {
            /* Read PredefPointers table address from LD HL, imm16 at 0x7E5B */
            uint16_t table_addr = ctx->rom_data[gpp_rom_off + 1] |
                                  ((uint16_t)ctx->rom_data[gpp_rom_off + 2] << 8);
            int predef_count = 0;
            for (int id = 0; id < 99; id++) {
                uint16_t entry_rom_addr = table_addr + (uint16_t)(id * 3);
                /* Convert table entry address to ROM offset (in bank 0x13) */
                size_t entry_off = (size_t)0x13 * BANK_SIZE + (entry_rom_addr - 0x4000);
                if (entry_off + 3 > ctx->rom_size) break;
                uint8_t tgt_bank = ctx->rom_data[entry_off];
                uint8_t tgt_lo   = ctx->rom_data[entry_off + 1];
                uint8_t tgt_hi   = ctx->rom_data[entry_off + 2];
                uint16_t tgt_addr = (uint16_t)((tgt_hi << 8) | tgt_lo);
                /* Add as function entry point in the target bank */
                int eff_bank = (tgt_addr < 0x4000) ? 0 : tgt_bank;
                if (eff_bank < ctx->num_banks) {
                    bank_analysis_t *tgt_ba = &ctx->banks[eff_bank];
                    if (tgt_ba->function_count < MAX_FUNCTIONS_PER_BANK) {
                        if (!find_function(tgt_ba, tgt_addr)) {
                            add_function(tgt_ba, tgt_addr, (uint8_t)eff_bank, false);
                            predef_count++;
                        }
                    }
                }
            }
            printf("Seeded %d predef function entry points from table at 0x%04X.\n",
                   predef_count, table_addr);
        }
    }

    /* Pass 1.6: Seed function entry points from Bankswitch (farcall) patterns.
     * Scan all ROM for CALL 0x35D6 (Bankswitch) and extract target bank/addr
     * from the preceding LD B, imm8 + LD HL, imm16 instructions.
     * Pattern A: 06 BB 21 LL HH CD D6 35  (LD B,bank; LD HL,addr; CALL)
     * Pattern B: 21 LL HH 06 BB CD D6 35  (LD HL,addr; LD B,bank; CALL) */
    {
        int farcall_count = 0;
        for (size_t off = 0; off + 7 < ctx->rom_size; off++) {
            /* Look for CD D6 35 = CALL 0x35D6 */
            if (ctx->rom_data[off] != 0xCD) continue;
            if (ctx->rom_data[off+1] != 0xD6 || ctx->rom_data[off+2] != 0x35)
                continue;
            uint8_t target_bank = 0;
            uint16_t target_addr = 0;
            bool found = false;
            /* Pattern A: 06 BB 21 LL HH CD D6 35 (at off-5) */
            if (off >= 5 &&
                ctx->rom_data[off-5] == 0x06 &&
                ctx->rom_data[off-3] == 0x21) {
                target_bank = ctx->rom_data[off-4];
                target_addr = ctx->rom_data[off-2] |
                              ((uint16_t)ctx->rom_data[off-1] << 8);
                found = true;
            }
            /* Pattern B: 21 LL HH 06 BB CD D6 35 (at off-5) */
            else if (off >= 5 &&
                     ctx->rom_data[off-5] == 0x21 &&
                     ctx->rom_data[off-2] == 0x06) {
                target_addr = ctx->rom_data[off-4] |
                              ((uint16_t)ctx->rom_data[off-3] << 8);
                target_bank = ctx->rom_data[off-1];
                found = true;
            }
            if (found && target_addr >= 0x0150 && target_addr < 0x8000) {
                int eff_bank = (target_addr < 0x4000) ? 0 : target_bank;
                if (eff_bank < ctx->num_banks) {
                    bank_analysis_t *tgt_ba = &ctx->banks[eff_bank];
                    if (tgt_ba->function_count < MAX_FUNCTIONS_PER_BANK &&
                        !find_function(tgt_ba, target_addr)) {
                        add_function(tgt_ba, target_addr, (uint8_t)eff_bank, false);
                        farcall_count++;
                    }
                }
            }
        }
        printf("Seeded %d farcall target entry points.\n", farcall_count);
    }

    /* Pass 1.7: Seed jump table targets.
     * Some functions use JP (HL) with table-driven targets that the analyzer
     * cannot resolve statically. We parse known tables here. */
    {
        struct {
            uint8_t  bank;      /* Bank containing the table */
            uint16_t addr;      /* Address of table in the bank */
            int      entries;   /* Number of 2-byte LE address entries */
        } jump_tables[] = {
            /* Bank 0: TextCommandProcessor at func_b00_1B40, table at 0x1CC1 */
            { 0x00, 0x1CC1, 14 },
            /* Bank 1: func_b01_6596, table at 0x665E (game state dispatch) */
            { 0x01, 0x665E, 16 },
        };
        int jt_count = sizeof(jump_tables) / sizeof(jump_tables[0]);
        for (int jt = 0; jt < jt_count; jt++) {
            uint8_t  bank = jump_tables[jt].bank;
            uint16_t jt_addr = jump_tables[jt].addr;
            int      jt_entries = jump_tables[jt].entries;
            /* Compute ROM offset: bank 0 is at 0x0000, banks 1+ at bank*0x4000 */
            size_t rom_base = (bank == 0) ? 0 : (size_t)bank * 0x4000;
            /* For bank 0, addr is the ROM offset directly.
             * For banks 1+, switchable area starts at 0x4000,
             * so rom_offset = rom_base + (addr - 0x4000). */
            size_t table_rom_off = (bank == 0) ? (size_t)jt_addr
                                               : rom_base + (jt_addr - 0x4000);
            int jt_seeded = 0;
            bank_analysis_t *ba = &ctx->banks[bank];
            uint16_t addr_min = (bank == 0) ? 0x0150 : 0x4000;
            uint16_t addr_max = (bank == 0) ? 0x4000 : 0x8000;
            for (int i = 0; i < jt_entries; i++) {
                size_t off = table_rom_off + (size_t)i * 2;
                if (off + 2 > ctx->rom_size) break;
                uint16_t target = ctx->rom_data[off] |
                                  ((uint16_t)ctx->rom_data[off + 1] << 8);
                if (target >= addr_min && target < addr_max &&
                    ba->function_count < MAX_FUNCTIONS_PER_BANK &&
                    !find_function(ba, target)) {
                    add_function(ba, target, bank, false);
                    jt_seeded++;
                }
            }
            printf("Seeded %d jump table targets from bank %d @ 0x%04X.\n",
                   jt_seeded, bank, jt_addr);
        }
    }

    /* Pass 1.8: Manually seed banked functions that the analyzer misses.
     * These are bank-0 CALL targets in the switchable area where the
     * LD A,n bank-switch detection works but the target function doesn't
     * get generated (absorbed by neighboring functions or tracing fails). */
    {
        struct { uint8_t bank; uint16_t addr; } manual_seeds[] = {
            { 0x03, 0x4E04 },  /* called from func_b00_2BCF */
            { 0x01, 0x72EA },  /* called from func_b00_30E8 */
            { 0x00, 0x03A6 },  /* dispatch_jump target from bank 1 (func_b01_536E) */
            { 0x00, 0x1F54 },  /* dispatch_jump target from bank 1 (func_b01_5A5F) */
            { 0x01, 0x6692 },  /* jump table target in bank 1 (naming screen) */
            { 0x17, 0x40B0 },  /* overworld - called 951x during gameplay */
            { 0x01, 0x4C34 },  /* overworld - called 891x during gameplay */
            { 0x11, 0x69A0 },  /* called 61x during overworld init */
            { 0x0D, 0x7DA5 },  /* called once during map loading */
            { 0x17, 0x40C0 },  /* overworld - called 444x during movement */
            { 0x14, 0x6673 },  /* called 10x during overworld */
            { 0x0F, 0x6F12 },  /* called 2x during overworld */
            { 0x17, 0x40CE },  /* overworld movement handler */
            { 0x17, 0x5B79 },  /* overworld movement handler */
            /* Intro/dialogue dispatch seeds (found during full intro progression) */
            { 0x07, 0x4B0E },  /* text/dialogue handler - 318x during intro */
            { 0x12, 0x4168 },  /* intro sequence handler - 45x */
            { 0x06, 0x4E5B },  /* intro sequence handler - 38x */
            { 0x12, 0x4170 },  /* intro sequence handler - 3x */
            { 0x06, 0x63E0 },  /* intro sequence handler - 3x */
            { 0x07, 0x50CF },  /* text/dialogue handler - 2x */
            { 0x07, 0x6965 },  /* text/dialogue handler - 1x */
            { 0x07, 0x5114 },  /* text/dialogue handler - 1x */
            /* { 0x03, 0x43FF } - REMOVED: breaks warps and NPC interaction */
            /* Pallet Town / Route 1 dispatch seeds (Oak script, wild battles) */
            { 0x07, 0x4AAF },  /* text handler - 60x during Pallet Town */
            { 0x1E, 0x4FCE },  /* map script handler - 26x */
            { 0x06, 0x4E81 },  /* map handler - 24x */
            { 0x1D, 0x47DE },  /* map script handler - 16x */
            { 0x0F, 0x5B00 },  /* called 2x during Route 1 */
            { 0x1E, 0x4DDB },  /* map script handler - 1x */
            { 0x1C, 0x4D24 },  /* map script handler - 1x */
            { 0x16, 0x4EB3 },  /* event handler - 1x */
            { 0x16, 0x4D99 },  /* event handler - 1x */
            { 0x07, 0x4AB9 },  /* text handler - 1x */
            { 0x02, 0x50C6 },  /* called 1x during map transition */
            { 0x06, 0x4EB2 },  /* Oak sprite/animation handler - 812x */
            { 0x06, 0x4ED2 },  /* Oak event continuation - 994x */
            { 0x06, 0x4F97 },  /* Oak event handler - 1x */
            { 0x06, 0x57A1 },  /* map event handler - 1x */
            { 0x1C, 0x487E },  /* script handler - 2x */
            { 0x1C, 0x45BA },  /* warp script processor - 1x */
            { 0x01, 0x5D5F },  /* bank 1 handler - 1x */
            { 0x06, 0x4F12 },  /* Oak dialogue/event - 1066x */
            { 0x06, 0x5A2B },  /* Oak event handler - 174x */
            { 0x07, 0x5D3A },  /* text handler - 2x */
            { 0x06, 0x4FB5 },  /* Oak event handler - 1x */
            { 0x14, 0x4935 },  /* event handler - 1x */
            { 0x06, 0x57E6 },  /* map event handler - 1x */
            { 0x16, 0x544C },  /* sprite/NPC handler - 150x+ per frame */
            { 0x06, 0x4F4B },  /* Oak walk-back handler - 709x */
            { 0x00, 0x24D7 },  /* bank 0 jump target - 3x */
            /* TextBoxBorder and helpers - Predef 0x2D target, needed for text display */
            { 0x01, 0x5AB0 },  /* TextBoxBorder - draws bordered text box */
            { 0x01, 0x5AE0 },  /* TextBoxBorder fill helper */
            { 0x01, 0x5AE6 },  /* TextBoxBorder specific box drawer */
            /* Oak grass script / map transition dispatch targets */
            { 0x03, 0x43FF },  /* Bank 3 helper (called during Oak script) */
            { 0x06, 0x4C10 },  /* Bank 6 map/script function */
            { 0x11, 0x4C10 },  /* Bank 17 map/script function */
            { 0x12, 0x4152 },  /* Bank 18 critical function (called many times) */
            { 0x12, 0x4C10 },  /* Bank 18 map/script function */
            { 0x12, 0x4CD7 },  /* Bank 18 function */
            { 0x12, 0x5269 },  /* Bank 18 function */
            { 0x14, 0x4C10 },  /* Bank 20 map/script function */
            { 0x15, 0x4C10 },  /* Bank 21 map/script function */
            { 0x16, 0x5336 },  /* Bank 22 function */
            /* Bank 0 jump targets reached from bank 18 during Oak script */
            { 0x00, 0x04A0 },  /* Bank 0 function called during map scripts */
            { 0x00, 0x3600 },  /* Bank 0 function called during map scripts */
            /* Oak script iteration 2 - more missing targets */
            { 0x03, 0x442A },  /* Bank 3 helper */
            { 0x03, 0x443A },  /* Bank 3 helper */
            { 0x16, 0x53C5 },  /* Bank 22 script function */
            { 0x16, 0x5410 },  /* Bank 22 script function */
            { 0x16, 0x55F3 },  /* Bank 22 script function */
            { 0x00, 0x1154 },  /* Bank 0 jump target from bank 22 */
            { 0x00, 0x3219 },  /* Bank 0 jump target from bank 22 */
        };
        int manual_count = sizeof(manual_seeds) / sizeof(manual_seeds[0]);
        for (int i = 0; i < manual_count; i++) {
            uint8_t  b = manual_seeds[i].bank;
            uint16_t a = manual_seeds[i].addr;
            if (b < ctx->num_banks) {
                bank_analysis_t *ba = &ctx->banks[b];
                if (ba->function_count < MAX_FUNCTIONS_PER_BANK &&
                    !find_function(ba, a)) {
                    add_function(ba, a, b, false);
                }
                /* Also ensure an xbank_call record exists */
                record_xbank_call(ctx, a, b);
            }
        }
        printf("Seeded %d manual banked function entries.\n", manual_count);
    }

    /* Seed 0x4000 as entry point for all banks.
     * Many GB games have a function at the start of each bank. */
    for (int b = 1; b < ctx->num_banks; b++) {
        bank_analysis_t *ba = &ctx->banks[b];
        if (!find_function(ba, 0x4000) && ba->function_count < MAX_FUNCTIONS_PER_BANK) {
            add_function(ba, 0x4000, (uint8_t)b, false);
        }
    }

    /* Pass 2: Analyse all other banks */
    for (int b = 1; b < ctx->num_banks; b++) {
        if (b % 32 == 0)
            printf("Analyzing bank %d/%d...\n", b, ctx->num_banks);
        analysis_run_bank(ctx, b);
    }

    /* Pass 3: Re-analyse bank 0 to pick up any function entries
     * discovered during other banks' analysis (RST targets, etc.) */
    bank_analysis_t *b0 = &ctx->banks[0];
    for (int i = 0; i < b0->function_count; i++) {
        function_info_t *f = &b0->functions[i];
        if (!find_block_starting_at(b0, f->entry_addr)) {
            trace_from(ctx, 0, f->entry_addr, i);
            basic_block_t *eb = find_block_starting_at(b0, f->entry_addr);
            if (eb) {
                eb->is_entry_point = true;
                if (f->first_block_idx < 0)
                    f->first_block_idx = (int)(eb - b0->blocks);
            }
        }
    }

    /* Re-propagate function IDs for bank 0 and recount blocks,
     * since pass 3 may have added new traces. */
    propagate_function_ids(b0);
    split_cross_function_jp_targets(b0);
    ensure_successor_blocks(b0);
    for (int fi = 0; fi < b0->function_count; fi++) {
        function_info_t *f = &b0->functions[fi];
        if (f->first_block_idx < 0) {
            basic_block_t *eb = find_block_starting_at(b0, f->entry_addr);
            if (eb)
                f->first_block_idx = (int)(eb - b0->blocks);
        }
        f->block_count = 0;
        for (int bi = 0; bi < b0->block_count; bi++) {
            if (b0->blocks[bi].function_id == fi)
                f->block_count++;
        }
    }

    printf("Analysis complete: %d cross-bank calls discovered.\n",
           ctx->xbank_call_count);
}

void analysis_free(analysis_ctx_t *ctx)
{
    if (ctx->banks) {
        free(ctx->banks);
        ctx->banks = NULL;
    }
    ctx->num_banks = 0;
    ctx->xbank_call_count = 0;
}

basic_block_t *analysis_find_block(analysis_ctx_t *ctx, uint8_t bank,
                                   uint16_t addr)
{
    if (bank >= ctx->num_banks || !ctx->banks)
        return NULL;

    bank_analysis_t *ba = &ctx->banks[bank];
    for (int i = 0; i < ba->block_count; i++) {
        if (ba->blocks[i].start_addr == addr)
            return &ba->blocks[i];
    }
    return NULL;
}

void analysis_print_summary(const analysis_ctx_t *ctx)
{
    if (!ctx->banks)
        return;

    int total_blocks    = 0;
    int total_functions = 0;
    int total_code      = 0;
    int total_data      = 0;

    printf("\n=== Analysis Summary ===\n");
    printf("%-6s  %6s  %6s  %6s  %6s\n",
           "Bank", "Blocks", "Funcs", "Code", "Data");
    printf("------  ------  ------  ------  ------\n");

    for (int b = 0; b < ctx->num_banks; b++) {
        const bank_analysis_t *ba = &ctx->banks[b];

        /* Count code and data bytes */
        int code_bytes = 0;
        for (int i = 0; i < BANK_SIZE; i++) {
            if (ba->is_code[i])
                code_bytes++;
        }
        int data_bytes = BANK_SIZE - code_bytes;

        /* Only print banks that have content */
        if (ba->block_count > 0 || ba->function_count > 0) {
            printf("%-6d  %6d  %6d  %5dB  %5dB\n",
                   b, ba->block_count, ba->function_count,
                   code_bytes, data_bytes);
        }

        total_blocks    += ba->block_count;
        total_functions += ba->function_count;
        total_code      += code_bytes;
        total_data      += data_bytes;
    }

    printf("------  ------  ------  ------  ------\n");
    printf("%-6s  %6d  %6d  %5dB  %5dB\n",
           "Total", total_blocks, total_functions, total_code, total_data);
    printf("\nCross-bank calls: %d\n", ctx->xbank_call_count);
}
