/* write_dm.c — DM (Descriptor Memory) に IDMA2 descriptor を 1 entry 書く
 *
 * Usage:
 *   write-DM [--invalid] [--via-dma] IDX DEVICE_DRAM_ADDR ENTRY_LEN
 *
 *   IDX:              DM index (0..8191)
 *   DEVICE_DRAM_ADDR: Device-DRAM byte アドレス (64 の倍数)
 *   ENTRY_LEN:        バイト長 (128 の倍数)
 *   --invalid:        valid bit (bit 63) を 0 にする
 *   --via-dma:        DDMA 経路で書く (診断用、pod 環境では失敗する見込み)
 *
 * descriptor layout (1 entry = u64):
 *   bit 63    : valid
 *   bit 62:24 : addr / 64        (39 bits = 0x7f_ffff_ffff)
 *   bit 22:0  : entry_len / 128  (23 bits = 0x7f_ffff)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <gpfn3.h>
#include "parse_u64.h"

#define DM_BASE_ADDR     0x10000ULL
#define DM_DMA_ADDR      0x1000000000ULL
#define DM_ENTRY_COUNT   8192
#define DM_ADDR_BOUNDARY 64ULL
#define Q_IN_BYTE        128ULL
#define DDMA_BUSY_MASK   0x401f401f401f401fULL

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

static int write_via_dma(gpfn3_device_id_t dev, size_t idx, uint64_t desc)
{
    void *buf = gpfn3_allocate_dma_memory(dev, 8);
    if (!buf) {
        fprintf(stderr, "FAIL: gpfn3_allocate_dma_memory(8)\n");
        return -1;
    }
    memcpy(buf, &desc, 8);

    gpfn3_error_t err = gpfn3_set_regset(dev, 0, buf, DM_DMA_ADDR + idx * 8);
    if (err != GPFN3_SUCCESS) {
        fprintf(stderr, "FAIL: gpfn3_set_regset rc=%d\n", (int)err);
        gpfn3_free_dma_memory(dev, buf, 8);
        return -1;
    }

    struct gpfn3_data_dma_kick_t dma;
    memset(&dma, 0, sizeof(dma));
    dma.direction = GPFN3_DMA_DIRECTION_TO_DEVICE;
    dma.regset    = 0;
    dma.channel   = GPFN3_DMA_CHANNEL_CH0;
    dma.size      = 8 / 4;  /* size is in 4-byte units */

    err = gpfn3_kick_data_dma(dev, dma);
    if (err != GPFN3_SUCCESS) {
        fprintf(stderr, "FAIL: gpfn3_kick_data_dma rc=%d\n", (int)err);
        gpfn3_free_dma_memory(dev, buf, 8);
        return -1;
    }

    /* Phase 1 開始確認 + Phase 2 完了確認 */
    for (int i = 0; i < 100; i++) {
        uint64_t stat;
        gpfn3_read_pio(dev, 0x038, &stat);
        if ((stat & DDMA_BUSY_MASK) != 0) break;
    }
    for (int i = 0; i < 5000; i++) {
        uint64_t stat;
        gpfn3_read_pio(dev, 0x038, &stat);
        if ((stat & DDMA_BUSY_MASK) == 0) {
            gpfn3_free_dma_memory(dev, buf, 8);
            return 0;
        }
    }
    fprintf(stderr, "FAIL: ddma_wait timeout\n");
    gpfn3_free_dma_memory(dev, buf, 8);
    return -1;
}

int main(int argc, char **argv)
{
    int invalid = 0;
    int via_dma = 0;
    const char *idx_s = NULL;
    const char *addr_s = NULL;
    const char *len_s = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--invalid") == 0) { invalid = 1; }
        else if (strcmp(argv[i], "--via-dma") == 0) { via_dma = 1; }
        else if (!idx_s)  { idx_s = argv[i]; }
        else if (!addr_s) { addr_s = argv[i]; }
        else if (!len_s)  { len_s = argv[i]; }
        else {
            fprintf(stderr, "FAIL: unexpected argument '%s'\n", argv[i]);
            return 1;
        }
    }

    if (!idx_s || !addr_s || !len_s) {
        fprintf(stderr,
            "Usage: write-DM [--invalid] [--via-dma] IDX DEVICE_DRAM_ADDR ENTRY_LEN\n"
            "  IDX:              DM index (0..8191)\n"
            "  DEVICE_DRAM_ADDR: Device-DRAM byte アドレス (64 の倍数)\n"
            "  ENTRY_LEN:        バイト長 (128 の倍数)\n"
            "  --invalid:        valid bit (bit63) を 0 にする\n"
            "  --via-dma:        DDMA 経路で書く (診断用)\n");
        return 1;
    }

    uint64_t idx  = parse_u64(idx_s);
    uint64_t addr = parse_u64(addr_s);
    uint64_t len  = parse_u64(len_s);

    if (idx >= DM_ENTRY_COUNT) {
        fprintf(stderr, "FAIL: IDX (%llu) が 0..%d の範囲外\n",
                (unsigned long long)idx, DM_ENTRY_COUNT - 1);
        return 1;
    }
    if (addr % DM_ADDR_BOUNDARY != 0) {
        fprintf(stderr, "FAIL: DEVICE_DRAM_ADDR (0x%llx) が %llu の倍数でない\n",
                (unsigned long long)addr, (unsigned long long)DM_ADDR_BOUNDARY);
        return 1;
    }
    if (len % Q_IN_BYTE != 0) {
        fprintf(stderr, "FAIL: ENTRY_LEN (0x%llx) が %llu の倍数でない\n",
                (unsigned long long)len, (unsigned long long)Q_IN_BYTE);
        return 1;
    }

    uint64_t addr_field = addr / DM_ADDR_BOUNDARY;
    uint64_t len_field  = len / Q_IN_BYTE;
    if (addr_field > 0x7fffffffffULL) {
        fprintf(stderr, "FAIL: addr/64 (0x%llx) が 39 bits を超える\n",
                (unsigned long long)addr_field);
        return 1;
    }
    if (len_field > 0x7fffffULL) {
        fprintf(stderr, "FAIL: len/128 (0x%llx) が 23 bits を超える\n",
                (unsigned long long)len_field);
        return 1;
    }

    uint64_t desc = ((invalid ? 0ULL : 1ULL) << 63)
                  | (addr_field << 24)
                  | (len_field & 0x7fffffULL);

    fprintf(stderr, "DM[%llu] = 0x%016llx (V=%d addr=0x%llx len=0x%llx)\n",
            (unsigned long long)idx, (unsigned long long)desc,
            invalid ? 0 : 1,
            (unsigned long long)addr, (unsigned long long)len);

    gpfn3_device_id_t dev = open_device();

    int rc;
    if (via_dma) {
        rc = write_via_dma(dev, (size_t)idx, desc);
    } else {
        gpfn3_error_t err = gpfn3_write_pio(dev, DM_BASE_ADDR + idx * 8, desc);
        if (err != GPFN3_SUCCESS) {
            fprintf(stderr, "FAIL: gpfn3_write_pio rc=%d\n", (int)err);
            rc = -1;
        } else {
            rc = 0;
        }
    }

    gpfn3_close_device(dev);
    return rc == 0 ? 0 : 1;
}
