#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "decoder.h"
#include "symbols.h"
#include "analyzer.h"
#include "codegen.h"

static uint8_t *load_rom(const char *path, size_t *size_out) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Error: Cannot open ROM file: %s\n", path);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0 || size > 8 * 1024 * 1024) {
        fprintf(stderr, "Error: Invalid ROM size: %ld bytes\n", size);
        fclose(f);
        return NULL;
    }

    uint8_t *data = (uint8_t *)malloc(size);
    if (!data) {
        fprintf(stderr, "Error: Out of memory\n");
        fclose(f);
        return NULL;
    }

    if (fread(data, 1, size, f) != (size_t)size) {
        fprintf(stderr, "Error: Failed to read ROM\n");
        free(data);
        fclose(f);
        return NULL;
    }

    fclose(f);
    *size_out = (size_t)size;
    return data;
}

static void print_rom_header(const uint8_t *rom, size_t rom_size) {
    if (rom_size < 0x0150) {
        fprintf(stderr, "ROM too small for header\n");
        return;
    }

    /* Title (up to 16 bytes at 0x0134) */
    char title[17] = {0};
    memcpy(title, rom + ROM_TITLE, 16);
    /* Null-terminate at first non-printable */
    for (int i = 0; i < 16; i++) {
        if (title[i] < 0x20 || title[i] > 0x7E) {
            title[i] = '\0';
            break;
        }
    }

    uint8_t cgb_flag = rom[ROM_CGB_FLAG];
    uint8_t cart_type = rom[ROM_CART_TYPE];
    uint8_t rom_size_code = rom[ROM_ROM_SIZE];
    uint8_t ram_size_code = rom[ROM_RAM_SIZE];

    int num_banks = sym_rom_banks(rom_size_code);
    int num_ram_banks = sym_ram_banks(ram_size_code);

    printf("=== ROM Header ===\n");
    printf("Title:     %s\n", title);
    printf("CGB Flag:  0x%02X (%s)\n", cgb_flag,
           cgb_flag == 0x80 ? "CGB Enhanced" :
           cgb_flag == 0xC0 ? "CGB Only" : "DMG");
    printf("Cart Type: 0x%02X (%s)\n", cart_type, sym_cart_type_name(cart_type));
    printf("ROM Size:  %d banks (%d KB)\n", num_banks, num_banks * 16);
    printf("RAM Size:  %d banks (%d KB)\n", num_ram_banks, num_ram_banks * 8);
    printf("==================\n\n");
}

static void disassemble_linear(const uint8_t *rom, size_t rom_size,
                               int bank, int max_lines) {
    uint32_t bank_offset = (uint32_t)bank * BANK_SIZE;
    uint16_t base_addr = (bank == 0) ? 0x0000 : 0x4000;
    uint32_t bank_end = bank_offset + BANK_SIZE;

    if (bank_end > rom_size) bank_end = (uint32_t)rom_size;

    printf("=== Linear Disassembly: Bank %02X ===\n", bank);

    uint32_t offset = bank_offset;
    int lines = 0;
    while (offset < bank_end && (max_lines <= 0 || lines < max_lines)) {
        uint16_t addr = base_addr + (uint16_t)(offset - bank_offset);
        const uint8_t *data = rom + offset;
        size_t remaining = bank_end - offset;

        uint8_t imm8;
        uint16_t imm16;
        sm83_inst_t inst = sm83_decode(data, remaining, &imm8, &imm16);

        char buf[64];
        sm83_format(buf, sizeof(buf), &inst, addr, imm8, imm16);

        /* Print address and raw bytes */
        printf("%04X: ", addr);
        for (int i = 0; i < 3; i++) {
            if (i < inst.length) {
                printf("%02X ", data[i]);
            } else {
                printf("   ");
            }
        }

        /* Check for known symbol */
        const char *sym = sym_addr_name(addr);
        if (sym) {
            printf(" %-24s ; %s\n", buf, sym);
        } else {
            printf(" %s\n", buf);
        }

        offset += inst.length;
        lines++;
    }
    printf("=== End Bank %02X (%d instructions) ===\n\n", bank, lines);
}

static void print_usage(const char *prog) {
    printf("Pokemon Static Recompiler\n\n");
    printf("Usage: %s [options] <rom_file>\n\n", prog);
    printf("Options:\n");
    printf("  -d                  Disassemble only (linear, no code generation)\n");
    printf("  -b <bank>           Disassemble specific bank (with -d)\n");
    printf("  -n <count>          Max instructions to disassemble (with -d)\n");
    printf("  -o <dir>            Output directory for generated C files\n");
    printf("  -g <name>           Game name (red, blue, yellow)\n");
    printf("  --use-trace <file>  Load trace file for additional entry points\n");
    printf("  --debug             Include debug comments in generated code\n");
    printf("  -h, --help          Show this help\n");
}

int main(int argc, char *argv[]) {
    const char *rom_path = NULL;
    const char *output_dir = NULL;
    const char *game_name = NULL;
    const char *trace_file = NULL;
    int disasm_only = 0;
    int disasm_bank = -1;
    int disasm_max = 0;
    int debug_comments = 0;

    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-d") == 0) {
            disasm_only = 1;
        } else if (strcmp(argv[i], "-b") == 0 && i + 1 < argc) {
            disasm_bank = (int)strtol(argv[++i], NULL, 0);
        } else if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            disasm_max = (int)strtol(argv[++i], NULL, 0);
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_dir = argv[++i];
        } else if (strcmp(argv[i], "-g") == 0 && i + 1 < argc) {
            game_name = argv[++i];
        } else if (strcmp(argv[i], "--use-trace") == 0 && i + 1 < argc) {
            trace_file = argv[++i];
        } else if (strcmp(argv[i], "--debug") == 0) {
            debug_comments = 1;
        } else if (argv[i][0] != '-') {
            rom_path = argv[i];
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    if (!rom_path) {
        fprintf(stderr, "Error: No ROM file specified\n\n");
        print_usage(argv[0]);
        return 1;
    }

    /* Load ROM */
    size_t rom_size;
    uint8_t *rom = load_rom(rom_path, &rom_size);
    if (!rom) return 1;

    printf("Loaded ROM: %s (%zu bytes)\n", rom_path, rom_size);
    print_rom_header(rom, rom_size);

    int num_banks = sym_rom_banks(rom[ROM_ROM_SIZE]);
    if (num_banks <= 0) {
        fprintf(stderr, "Error: Invalid ROM size code: 0x%02X\n", rom[ROM_ROM_SIZE]);
        free(rom);
        return 1;
    }

    /* Disassemble-only mode */
    if (disasm_only) {
        if (disasm_bank >= 0) {
            if (disasm_bank >= num_banks) {
                fprintf(stderr, "Error: Bank %d out of range (0-%d)\n",
                        disasm_bank, num_banks - 1);
                free(rom);
                return 1;
            }
            disassemble_linear(rom, rom_size, disasm_bank, disasm_max);
        } else {
            /* Disassemble all banks */
            for (int b = 0; b < num_banks; b++) {
                disassemble_linear(rom, rom_size, b, disasm_max);
            }
        }
        free(rom);
        return 0;
    }

    /* Full recompilation pipeline */
    if (!output_dir) output_dir = "src/generated";
    if (!game_name) {
        /* Auto-detect from ROM title */
        char title[17] = {0};
        memcpy(title, rom + ROM_TITLE, 16);
        if (strstr(title, "RED")) game_name = "red";
        else if (strstr(title, "BLUE")) game_name = "blue";
        else if (strstr(title, "YELLOW")) game_name = "yellow";
        else game_name = "unknown";
    }

    printf("Game: %s\n", game_name);
    printf("Output: %s/%s/\n", output_dir, game_name);
    printf("Banks: %d\n\n", num_banks);

    /* Phase 1: Analyze control flow */
    printf("=== Phase 1: Control Flow Analysis ===\n");
    analysis_ctx_t analysis;
    analysis_init(&analysis, rom, rom_size, num_banks, game_name);
    if (trace_file) {
        analysis_load_trace(&analysis, trace_file);
    }
    analysis_run(&analysis);
    analysis_print_summary(&analysis);

    /* Phase 2: Generate C code */
    printf("\n=== Phase 2: C Code Generation ===\n");
    codegen_ctx_t codegen;
    codegen_init(&codegen, &analysis, rom, rom_size, output_dir, game_name);
    codegen.emit_debug_comments = debug_comments;

    int result = codegen_emit_all(&codegen);
    if (result != 0) {
        fprintf(stderr, "Error: Code generation failed\n");
        analysis_free(&analysis);
        free(rom);
        return 1;
    }

    printf("\nRecompilation complete!\n");
    printf("Generated C files are in: %s/%s/\n", output_dir, game_name);

    analysis_free(&analysis);
    free(rom);
    return 0;
}
