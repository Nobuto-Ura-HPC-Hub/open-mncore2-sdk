/* read_dm.c — DM (Descriptor Memory) から descriptor を読み出す
 *
 * Usage: read-DM IDX [COUNT]
 *   IDX:   DM index (0..8191)
 *   COUNT: 読み出し数 (default 1)
 *
 * Output (per line):
 *   V:0xADDR:0xLEN
 *     V    = valid bit (0 or 1)
 *     ADDR = device-DRAM byte address (bits 62:24 * 64)
 *     LEN  = entry length in bytes (bits 22:0 * 128)
 *
 * descriptor layout:
 *   bit 63    : valid
 *   bit 62:24 : addr / 64
 *   bit 22:0  : entry_len / 128
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <gpfn3.h>
#include "parse_u64.h"

#define DM_BASE_ADDR     0x10000ULL
#define DM_ENTRY_COUNT   8192
#define DM_ADDR_BOUNDARY 64ULL
#define Q_IN_BYTE        128ULL

static gpfn3_device_id_t open_device(void)
{
    const char *env = getenv("GPFN3_DEVICE");
    int num = env ? atoi(env) : 0;
    gpfn3_device_id_t dev = gpfn3_get_device_id(num);
    if (dev == GPFN3_INVALID_DEVICE_ID) {
        fprintf(stderr, "FAIL: gpfn3_get_device_id(%d)\n", num);
        exit(1);
    }
    return dev;
}

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 3) {
        fprintf(stderr,
            "Usage: read-DM IDX [COUNT]\n"
            "  IDX:   DM index (0..8191)\n"
            "  COUNT: 読み出し数 (default 1)\n"
            "Output: V:0xADDR:0xLEN (per line)\n");
        return 1;
    }

    uint64_t idx   = parse_u64(argv[1]);
    uint64_t count = (argc == 3) ? parse_u64(argv[2]) : 1;

    if (idx >= DM_ENTRY_COUNT) {
        fprintf(stderr, "FAIL: IDX (%llu) が 0..%d の範囲外\n",
                (unsigned long long)idx, DM_ENTRY_COUNT - 1);
        return 1;
    }
    if (idx + count > DM_ENTRY_COUNT) {
        fprintf(stderr, "FAIL: IDX+COUNT (%llu) が %d を超える\n",
                (unsigned long long)(idx + count), DM_ENTRY_COUNT);
        return 1;
    }

    gpfn3_device_id_t dev = open_device();

    for (uint64_t i = 0; i < count; i++) {
        uint64_t val = 0;
        gpfn3_error_t err = gpfn3_read_pio(dev, DM_BASE_ADDR + (idx + i) * 8, &val);
        if (err != GPFN3_SUCCESS) {
            fprintf(stderr, "FAIL: gpfn3_read_pio(DM[%llu]) rc=%d\n",
                    (unsigned long long)(idx + i), (int)err);
            gpfn3_close_device(dev);
            return 1;
        }
        int      v    = (int)((val >> 63) & 1);
        uint64_t addr = ((val >> 24) & 0x7fffffffffULL) * DM_ADDR_BOUNDARY;
        uint64_t len  = (val & 0x7fffffULL) * Q_IN_BYTE;
        printf("%d:0x%llx:0x%llx\n", v,
               (unsigned long long)addr, (unsigned long long)len);
    }

    gpfn3_close_device(dev);
    return 0;
}
