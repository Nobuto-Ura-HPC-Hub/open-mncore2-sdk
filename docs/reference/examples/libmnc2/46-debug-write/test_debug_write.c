/* test_debug_write.c — mnc2_debug_write ラウンドトリップテスト
 *
 * debug_write で書き込み → debug_read で読み返して一致確認。
 * カーネル不要 (emu:lib) / nop カーネル経由 (emu:process)。
 *
 * 使い方:
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "mnc2.h"

/* ---- テスト対象のメモリ領域 ---- */

struct region_info {
    int         mem_region;
    const char* name;
};

static const struct region_info regions[] = {
    { MNC2_MEM_L2BM, "L2BM" },
    { MNC2_MEM_L1BM, "L1BM" },
    { MNC2_MEM_LM0,  "LM0"  },
    { MNC2_MEM_LM1,  "LM1"  },
    { MNC2_MEM_GRF0, "GRF0" },
    { MNC2_MEM_GRF1, "GRF1" },
};
#define NUM_REGIONS (sizeof(regions) / sizeof(regions[0]))

/* ---- write → read ラウンドトリップ (emu:lib 用) ---- */

static int test_write_read_roundtrip(mnc2_device_t dev,
                                     int mem_region, const char* name)
{
    printf("[test] debug_write → debug_read roundtrip: %s\n", name);

    const int count = 4;
    uint64_t write_data[4];
    for (int i = 0; i < count; i++)
        write_data[i] = 0xDEAD000000000000ULL | (uint64_t)(mem_region << 8) | (uint64_t)i;

    mnc2_loc_t loc = MNC2_LOC_INIT;
    int rc = mnc2_debug_write(dev, mem_region, &loc, 0, count, write_data);
    if (rc != MNC2_SUCCESS) {
        printf("  FAIL: debug_write %s returned %d\n", name, rc);
        return 1;
    }

    uint64_t read_data[4];
    memset(read_data, 0, sizeof(read_data));
    rc = mnc2_debug_read(dev, mem_region, &loc, 0, count, read_data);
    if (rc != MNC2_SUCCESS) {
        printf("  FAIL: debug_read %s returned %d\n", name, rc);
        return 1;
    }

    int mismatch = 0;
    for (int i = 0; i < count; i++) {
        if (read_data[i] != write_data[i]) {
            printf("  FAIL: %s[%d] expected 0x%016" PRIx64 " got 0x%016" PRIx64 "\n",
                    name, i, write_data[i], read_data[i]);
            mismatch++;
        }
    }

    if (mismatch) return 1;
    printf("  PASS\n");
    return 0;
}

/* ---- パラメータバリデーション ---- */

static int test_debug_write_params(mnc2_device_t dev)
{
    printf("[test] debug_write parameter validation\n");

    int errors = 0;
    uint64_t data[1] = { 42 };
    mnc2_loc_t loc = MNC2_LOC_INIT;

    /* PDM → MNC2_ERROR_PARAM */
    if (mnc2_debug_write(dev, MNC2_MEM_PDM, &loc, 0, 1, data) != MNC2_ERROR_PARAM) {
        printf("  FAIL: PDM should return MNC2_ERROR_PARAM\n");
        errors++;
    }

    /* DRAM → MNC2_ERROR_PARAM */
    if (mnc2_debug_write(dev, MNC2_MEM_DRAM, &loc, 0, 1, data) != MNC2_ERROR_PARAM) {
        printf("  FAIL: DRAM should return MNC2_ERROR_PARAM\n");
        errors++;
    }

    /* count=0 → MNC2_ERROR_PARAM */
    if (mnc2_debug_write(dev, MNC2_MEM_LM0, &loc, 0, 0, data) != MNC2_ERROR_PARAM) {
        printf("  FAIL: count=0 should return MNC2_ERROR_PARAM\n");
        errors++;
    }

    /* data=NULL → MNC2_ERROR_PARAM */
    if (mnc2_debug_write(dev, MNC2_MEM_LM0, &loc, 0, 1, NULL) != MNC2_ERROR_PARAM) {
        printf("  FAIL: data=NULL should return MNC2_ERROR_PARAM\n");
        errors++;
    }

    /* 不正な mem_region → MNC2_ERROR_PARAM */
    if (mnc2_debug_write(dev, 99, &loc, 0, 1, data) != MNC2_ERROR_PARAM) {
        printf("  FAIL: invalid region should return MNC2_ERROR_PARAM\n");
        errors++;
    }

    if (errors > 0) {
        printf("  FAIL: %d errors\n", errors);
        return 1;
    }
    printf("  PASS\n");
    return 0;
}

int main(void)
{
    printf("=== mnc2_debug_write test ===\n\n");

    mnc2_device_t dev = mnc2_open(0);
    if (dev == NULL) {
        printf("FAIL: mnc2_open failed\n");
        return 1;
    }

    int failed = 0;
    printf("backend: %s\n\n", mnc2_get_backend_name(dev));

    /* パラメータバリデーション */
    failed |= test_debug_write_params(dev);
    printf("\n");

    /* この example は emu:lib 専用 (build.ninja で test-emu-lib のみ)。
       write → read (カーネル不要) の roundtrip を検証 */
    for (int i = 0; i < (int)NUM_REGIONS; i++) {
        failed |= test_write_read_roundtrip(dev, regions[i].mem_region, regions[i].name);
        printf("\n");
    }

    mnc2_close(dev);

    if (failed == 0)
        printf("ALL PASS\n");
    else
        printf("FAILED\n");

    return failed ? 1 : 0;
}
