#include <stdio.h>
#include <stdint.h>

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "Usage: check_predefs <rom>\n"); return 1; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror("fopen"); return 1; }

    /* PredefPointers table at 0x7E79 in bank 0x13 */
    /* ROM offset = 0x13 * 0x4000 + (0x7E79 - 0x4000) */
    long table_off = 0x13 * 0x4000L + (0x7E79 - 0x4000);
    printf("Table ROM offset: 0x%lX\n", table_off);

    fseek(f, table_off, SEEK_SET);

    for (int i = 0; i < 99; i++) {
        uint8_t bank, lo, hi;
        if (fread(&bank, 1, 1, f) != 1) break;
        if (fread(&lo, 1, 1, f) != 1) break;
        if (fread(&hi, 1, 1, f) != 1) break;
        uint16_t addr = (hi << 8) | lo;
        /* Show all entries, highlight bank 1 and addr 0x4538 */
        const char *flag = "";
        if (addr == 0x4538) flag = " <-- TARGET!";
        if (bank == 1) flag = (addr == 0x4538) ? " <-- BANK1 + TARGET!" : " (bank 1)";
        printf("  Predef %2d: bank=0x%02X addr=0x%04X%s\n", i, bank, addr, flag);
    }

    fclose(f);
    return 0;
}
