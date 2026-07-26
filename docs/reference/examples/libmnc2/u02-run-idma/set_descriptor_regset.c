/* set_descriptor_regset.c — gpfn3_set_descriptor_regset 単体実行ツール
 *
 * Descriptor memory に u64 値を連続して書く。
 * 値は引数 (hex リテラル) または stdin (u64 little-endian バイナリ) から読む。
 *
 * Usage:
 *   set-descriptor-regset REGSET DM_INDEX [HEX1 HEX2 ...]
 *   echo -ne "\x..." | set-descriptor-regset REGSET DM_INDEX
 *
 *   REGSET:   register set index
 *   DM_INDEX: descriptor memory starting index
 *   HEX*:     u64 values in hex (e.g. 0x8000000000001001)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <gpfn3.h>

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr,
            "Usage: set-descriptor-regset REGSET DM_INDEX [HEX1 HEX2 ...]\n"
            "  REGSET:   register set index\n"
            "  DM_INDEX: descriptor memory starting index\n"
            "  HEX*:     u64 values in hex (e.g. 0x8000000000001001)\n"
            "            引数省略時は stdin から u64 little-endian を読む\n");
        return 1;
    }
    unsigned int regset   = (unsigned int)strtoul(argv[1], NULL, 0);
    size_t       dm_index = (size_t)strtoul(argv[2], NULL, 0);

    /* 引数 or stdin から u64 値を収集 */
    uint64_t *values = NULL;
    size_t    count  = 0;

    if (argc >= 4) {
        count = (size_t)(argc - 3);
        values = (uint64_t *)calloc(count, sizeof(uint64_t));
        if (!values) { perror("calloc"); return 1; }
        for (size_t i = 0; i < count; i++)
            values[i] = strtoull(argv[3 + i], NULL, 0);
    } else {
        /* stdin から読む */
        size_t cap = 16;
        values = (uint64_t *)calloc(cap, sizeof(uint64_t));
        if (!values) { perror("calloc"); return 1; }
        while (1) {
            if (count >= cap) {
                cap *= 2;
                uint64_t *nv = (uint64_t *)realloc(values, cap * sizeof(uint64_t));
                if (!nv) { perror("realloc"); free(values); return 1; }
                values = nv;
            }
            ssize_t n = read(STDIN_FILENO, &values[count], sizeof(uint64_t));
            if (n == 0) break;
            if (n < 0) { perror("read"); free(values); return 1; }
            if (n != sizeof(uint64_t)) {
                fprintf(stderr, "FAIL: stdin の長さが 8 byte 単位ではない (残 %zd byte)\n", n);
                free(values);
                return 1;
            }
            count++;
        }
    }

    if (count == 0) {
        fprintf(stderr, "FAIL: 値がゼロ個\n");
        free(values);
        return 1;
    }

    /* ---- device open ---- */
    gpfn3_device_id_t dev = gpfn3_get_device_id(0);
    if (dev == GPFN3_INVALID_DEVICE_ID) {
        fprintf(stderr, "FAIL: gpfn3_get_device_id\n");
        free(values);
        return 1;
    }

    /* gpfn3_set_descriptor_regset は host_table + dm_index を受けるが、
       dm_count は持たない。1 regset 分の領域に連続して書き込むので、
       count は regset のサイズに依存する (HW 仕様)。
       ここでは count 分の values をそのまま渡す。 */
    gpfn3_error_t err = gpfn3_set_descriptor_regset(dev, regset, values, dm_index);
    if (err != GPFN3_SUCCESS) {
        fprintf(stderr, "FAIL: gpfn3_set_descriptor_regset rc=%d\n", (int)err);
        gpfn3_close_device(dev);
        free(values);
        return 1;
    }

    fprintf(stderr, "OK: set_descriptor_regset regset=%u dm_index=%zu count=%zu\n",
            regset, dm_index, count);

    gpfn3_close_device(dev);
    free(values);
    return 0;
}
