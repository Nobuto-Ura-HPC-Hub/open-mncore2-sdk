/* run_idma2.c — Device-DRAM 経由の IDMA2 で .idma.dat を実行するユーティリティ
 *
 * 流れ:
 *   1. .idma.dat を host DMA memory に読み込み
 *   2. CH3 DDMA-2 で Host → Device-DRAM 転送 (dram_offset 位置)
 *   3. DDMA 完了待ち (2 段構え)
 *   4. Descriptor Memory (BAR2 0x10000+) に descriptor を書き込み
 *      (loader.cpp の dm_setup() を port)
 *   5. 直接 PIO で IDMA kick (IDMA_ADR=0x008, IDMA_KICK=0x010)
 *      gpfn3_kick_inst_dma2 は kernel driver の IDMA2 未対応 bug で EFAULT に
 *      なるので回避 (kpio_support=true 時の ioctl 経路が find_dma_memory で
 *      必ず失敗する)
 *   6. IDMA 完了待ち (2 段構え)
 *
 * descriptor (loader.cpp dm_setup):
 *   bit 63     = valid
 *   bits[62:24] = (DRAM address / dm_address_boundary) & mask<30>
 *   bits[22:0]  = entry_len (dm_address_boundary 単位)
 *
 * Usage:
 *   run-idma2 PATH.idma.dat [--dram-offset OFF] [--dm-index IDX] [--no-reset]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <unistd.h>
#include <gpfn3.h>

/* ---- 定数 (gpfn3-loader.h 参照) ---- */
#define Q_IN_BYTE            128u
#define DM_ADDRESS_BOUNDARY  64u
#define DM_MAX_LEN           ((1u << 23) - 1u)
#define DM_ALIGN             8u
#define DM_MAX_ENTRY         ((64u * 1024u) / 8u)
#define ALIGNED_DM_MAX_LEN   DM_MAX_LEN

/* BAR2 register addresses (gpfn3_private.h 参照) */
#define IDMA_KICK_REG   0x010u
#define IDMA_STAT_REG   0x018u
#define IDMA_ADR_REG    0x020u
#define DDMA_STAT_REG   0x038u

#define DM_BASE_ADDR    0x10000u   /* BAR2 DM 領域の先頭 PIO アドレス */

/* ---- DDMA 完了待ち ---- */
#define DDMA_BUSY_MASK  0x401f401f401f401fULL

static int ddma_wait(gpfn3_device_id_t dev, int max_cnt)
{
    for (int i = 0; i < 100; i++) {
        uint64_t stat;
        if (gpfn3_read_pio(dev, DDMA_STAT_REG, &stat) != GPFN3_SUCCESS) return -1;
        if ((stat & DDMA_BUSY_MASK) != 0) break;
    }
    for (int i = 0; i < max_cnt; i++) {
        uint64_t stat;
        if (gpfn3_read_pio(dev, DDMA_STAT_REG, &stat) != GPFN3_SUCCESS) return -1;
        if ((stat & DDMA_BUSY_MASK) == 0) return 0;
    }
    fprintf(stderr, "FAIL: ddma_wait timeout\n");
    return -1;
}

/* ---- IDMA 完了待ち ---- */
static int idma_wait(gpfn3_device_id_t dev, int max_cnt)
{
    for (int i = 0; i < 100; i++) {
        uint64_t stat;
        if (gpfn3_read_pio(dev, IDMA_STAT_REG, &stat) != GPFN3_SUCCESS) return -1;
        if ((stat & 0x1fffffULL) != 0) break;
    }
    for (int i = 0; i < max_cnt; i++) {
        uint64_t stat;
        if (gpfn3_read_pio(dev, IDMA_STAT_REG, &stat) != GPFN3_SUCCESS) return -1;
        if ((stat & 0x1fffffULL) == 0) return 0;
    }
    fprintf(stderr, "FAIL: idma_wait timeout\n");
    return -1;
}

/* ---- DM setup (loader.cpp の dm_setup 準拠) ----
   戻り値: total_count (dm_align の倍数、kick の dm_count に使う)
   負値: エラー */
static int dm_setup(gpfn3_device_id_t dev, size_t dm_index,
                    uint64_t dram_offset, size_t ksize)
{
    if (ksize == 0 || (ksize % Q_IN_BYTE) != 0) {
        fprintf(stderr, "FAIL: dm_setup: ksize %zu is not a multiple of %u\n",
                ksize, Q_IN_BYTE);
        return -1;
    }
    size_t q_count = ksize / Q_IN_BYTE;

    uint64_t addr_scaled = (dram_offset / DM_ADDRESS_BOUNDARY) & ((1ULL << 30) - 1);

    size_t valid_count   = (q_count + ALIGNED_DM_MAX_LEN - 1) / ALIGNED_DM_MAX_LEN;
    size_t invalid_count = DM_ALIGN - ((valid_count - 1) % DM_ALIGN + 1);
    size_t total_count   = valid_count + invalid_count;

    if (dm_index + total_count > DM_MAX_ENTRY) {
        fprintf(stderr, "FAIL: dm_setup: DM overflow\n");
        return -1;
    }

    /* backward fill (workaround #8881): invalid -> valid を逆順で書く */
    size_t ioff = total_count;
    while (ioff > valid_count) {
        --ioff;
        if (gpfn3_write_descriptor(dev, (unsigned)(dm_index + ioff), 0)
            != GPFN3_SUCCESS) {
            fprintf(stderr, "FAIL: write_descriptor invalid\n");
            return -1;
        }
    }

    size_t aoff = q_count * Q_IN_BYTE / DM_ADDRESS_BOUNDARY;
    while (ioff--) {
        size_t entry_len = (q_count - 1) % ALIGNED_DM_MAX_LEN + 1;
        aoff -= entry_len * Q_IN_BYTE / DM_ADDRESS_BOUNDARY;
        uint64_t value = (1ULL << 63)
                       | (((uint64_t)(addr_scaled + aoff)) << 24)
                       | ((uint64_t)entry_len & ((1ULL << 23) - 1));
        if (gpfn3_write_descriptor(dev, (unsigned)(dm_index + ioff), value)
            != GPFN3_SUCCESS) {
            fprintf(stderr, "FAIL: write_descriptor valid\n");
            return -1;
        }
    }
    assert(aoff == 0);

    return (int)total_count;
}

/* ---- IDMA2 kick (kernel driver bug 回避: 直接 PIO 書き込み) ----
   gpfn3_kick_inst_dma2 は kernel driver の ioctl_experimental_idma_kick が
   find_dma_memory で必ず EFAULT を返すため使えない。
   chip 仕様に従って IDMA_ADR と IDMA_KICK に直接 PIO 書き込み。 */
static int kick_idma2_direct(gpfn3_device_id_t dev, size_t dm_index, size_t dm_count)
{
    uint64_t addr = DM_BASE_ADDR + dm_index * sizeof(uint64_t);
    uint64_t kick = (1ULL << 62) | dm_count;
    fprintf(stderr, "kick_idma2_direct: IDMA_ADR=0x%lx IDMA_KICK=0x%lx\n",
            (unsigned long)addr, (unsigned long)kick);
    gpfn3_error_t err;
    err = gpfn3_write_pio(dev, IDMA_ADR_REG, addr);
    if (err != GPFN3_SUCCESS) {
        fprintf(stderr, "FAIL: write_pio IDMA_ADR rc=%d\n", (int)err);
        return -1;
    }
    err = gpfn3_write_pio(dev, IDMA_KICK_REG, kick);
    if (err != GPFN3_SUCCESS) {
        fprintf(stderr, "FAIL: write_pio IDMA_KICK rc=%d\n", (int)err);
        return -1;
    }
    return 0;
}

static void usage(FILE* fp)
{
    fprintf(fp,
        "Usage: run-idma2 PATH.idma.dat [--dram-offset OFF] [--dm-index IDX] [--no-reset]\n"
        "\n"
        "  PATH.idma.dat   assemble3 --loader 生成の idma.dat\n"
        "  --dram-offset   Device-DRAM 内配置オフセット (byte 単位、64 倍数)\n"
        "                  省略時 0\n"
        "  --dm-index      Descriptor Memory 開始 index (DM_ALIGN=8 倍数推奨)\n"
        "                  省略時 0\n"
        "  --no-reset      device open 直後の gpfn3_reset_device を省略\n");
}

int main(int argc, char** argv)
{
    const char* path = NULL;
    uint64_t dram_offset = 0;
    size_t dm_index = 0;
    int do_reset = 1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--dram-offset") == 0 && i + 1 < argc) {
            dram_offset = strtoull(argv[++i], NULL, 0);
        } else if (strcmp(argv[i], "--dm-index") == 0 && i + 1 < argc) {
            dm_index = (size_t)strtoul(argv[++i], NULL, 0);
        } else if (strcmp(argv[i], "--no-reset") == 0) {
            do_reset = 0;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "FAIL: unknown option '%s'\n", argv[i]);
            usage(stderr);
            return 1;
        } else if (path == NULL) {
            path = argv[i];
        } else {
            fprintf(stderr, "FAIL: unexpected argument '%s'\n", argv[i]);
            usage(stderr);
            return 1;
        }
    }
    if (path == NULL) { usage(stderr); return 1; }
    if (dram_offset % DM_ADDRESS_BOUNDARY != 0) {
        fprintf(stderr, "FAIL: --dram-offset must be multiple of %u\n",
                DM_ADDRESS_BOUNDARY);
        return 1;
    }

    /* ---- kernel file ---- */
    FILE* fp = fopen(path, "rb");
    if (!fp) { fprintf(stderr, "FAIL: fopen(%s)\n", path); return 1; }
    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    rewind(fp);
    if (fsize <= 0 || (fsize % Q_IN_BYTE) != 0) {
        fprintf(stderr, "FAIL: size not multiple of %u\n", Q_IN_BYTE);
        fclose(fp);
        return 1;
    }
    size_t ksize = (size_t)fsize;

    /* ---- device open ---- */
    gpfn3_device_id_t dev = gpfn3_get_device_id(0);
    if (dev == GPFN3_INVALID_DEVICE_ID) {
        fprintf(stderr, "FAIL: gpfn3_get_device_id\n");
        fclose(fp);
        return 1;
    }
    if (do_reset) gpfn3_reset_device(dev);

    /* ---- kernel を host DMA memory に置く ---- */
    void* kdma = gpfn3_allocate_dma_memory(dev, ksize);
    if (!kdma) { fprintf(stderr, "FAIL: allocate_dma_memory\n"); goto out1; }
    if (fread(kdma, 1, ksize, fp) != ksize) {
        fprintf(stderr, "FAIL: fread\n");
        goto out2;
    }
    fclose(fp); fp = NULL;

    /* ---- Host → Device-DRAM 転送 (CH3 DDMA-2) ---- */
    if (gpfn3_set_regset(dev, 0, kdma, dram_offset) != GPFN3_SUCCESS) {
        fprintf(stderr, "FAIL: set_regset\n");
        goto out2;
    }
    struct gpfn3_data_dma_kick_t dma;
    memset(&dma, 0, sizeof(dma));
    dma.direction = GPFN3_DMA_DIRECTION_TO_DEVICE;
    dma.regset    = 0;
    dma.channel   = GPFN3_DMA_CHANNEL_CH3;
    dma.size      = ksize / 4;
    if (gpfn3_kick_data_dma(dev, dma) != GPFN3_SUCCESS) {
        fprintf(stderr, "FAIL: kick_data_dma\n");
        goto out2;
    }
    if (ddma_wait(dev, 5000) < 0) goto out2;

    /* ---- DM descriptor setup ---- */
    int dm_count = dm_setup(dev, dm_index, dram_offset, ksize);
    if (dm_count < 0) goto out2;

    /* ---- IDMA2 kick (direct PIO, ioctl bypass) ---- */
    if (kick_idma2_direct(dev, dm_index, (size_t)dm_count) < 0) goto out2;
    if (idma_wait(dev, 5000) < 0) goto out2;

    gpfn3_free_dma_memory(dev, kdma, ksize);
    gpfn3_close_device(dev);
    return 0;

out2:
    gpfn3_free_dma_memory(dev, kdma, ksize);
out1:
    if (fp) fclose(fp);
    gpfn3_close_device(dev);
    return 1;
}
