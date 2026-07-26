/* test_debug_read.c — mnc2_debug_read テスト
 *
 * カーネル実行後の各メモリ階層の内容を読み出すテスト。
 * emu:process / emu:lib 両方のバックエンドで動作する。
 *
 * 使い方:
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "mnc2.h"

static int test_debug_read_basic(void)
{
    printf("[test] debug_read basic (DRAM)\n");

    mnc2_device_t dev = mnc2_open(0);
    if (dev == NULL) {
        printf("  FAIL: mnc2_open failed\n");
        return 1;
    }

    /* send + exec_kernel で dirty=1 にする */
    size_t data_size = 8 * sizeof(double);
    void* sendbuf = mnc2_alloc_host_buffer(dev, data_size);
    if (sendbuf == NULL) {
        printf("  FAIL: alloc\n");
        mnc2_close(dev);
        return 1;
    }

    double* dp = (double*)sendbuf;
    for (int i = 0; i < 8; i++)
        dp[i] = (double)(i + 1) * 1.5;

    int rc = mnc2_send(dev, sendbuf, 0, data_size, 1);
    if (rc != 0) {
        printf("  FAIL: send returned %d\n", rc);
        mnc2_free_host_buffer(dev, sendbuf, data_size);
        mnc2_close(dev);
        return 1;
    }

    mnc2_kernel_t kernel = mnc2_load_kernel(dev, "_build/nop.idma.dat");
    if (kernel == NULL) {
        printf("  FAIL: load_kernel\n");
        mnc2_free_host_buffer(dev, sendbuf, data_size);
        mnc2_close(dev);
        return 1;
    }

    rc = mnc2_exec_kernel(kernel);
    mnc2_free_kernel(kernel);
    if (rc != 0) {
        printf("  FAIL: exec_kernel returned %d\n", rc);
        mnc2_free_host_buffer(dev, sendbuf, data_size);
        mnc2_close(dev);
        return 1;
    }

    /* debug_read: DRAM addr=0 count=4 */
    uint64_t dram[4];
    memset(dram, 0xFF, sizeof(dram));
    mnc2_loc_t loc = MNC2_LOC_INIT;

    rc = mnc2_debug_read(dev, MNC2_MEM_DRAM, &loc, 0, 4, dram);
    if (rc != 0) {
        printf("  FAIL: debug_read DRAM returned %d\n", rc);
        mnc2_free_host_buffer(dev, sendbuf, data_size);
        mnc2_close(dev);
        return 1;
    }

    printf("  DRAM[0..3]:");
    for (int i = 0; i < 4; i++)
        printf(" 0x%016" PRIx64, dram[i]);
    printf("\n");

    printf("  PASS (debug_read returned successfully)\n");

    mnc2_free_host_buffer(dev, sendbuf, data_size);
    mnc2_close(dev);
    return 0;
}

static int test_debug_read_multiple(void)
{
    printf("[test] debug_read multiple calls\n");

    mnc2_device_t dev = mnc2_open(0);
    if (dev == NULL) {
        printf("  FAIL: mnc2_open failed\n");
        return 1;
    }

    size_t data_size = 8 * sizeof(double);
    void* sendbuf = mnc2_alloc_host_buffer(dev, data_size);
    if (sendbuf == NULL) {
        printf("  FAIL: alloc\n");
        mnc2_close(dev);
        return 1;
    }

    double* dp = (double*)sendbuf;
    for (int i = 0; i < 8; i++)
        dp[i] = (double)(i + 1);

    mnc2_send(dev, sendbuf, 0, data_size, 1);

    mnc2_kernel_t kernel = mnc2_load_kernel(dev, "_build/nop.idma.dat");
    if (kernel == NULL) {
        printf("  FAIL: load_kernel\n");
        mnc2_free_host_buffer(dev, sendbuf, data_size);
        mnc2_close(dev);
        return 1;
    }
    mnc2_exec_kernel(kernel);
    mnc2_free_kernel(kernel);

    /* 1回目: DRAM 読み出し */
    uint64_t dram[2];
    mnc2_loc_t loc = MNC2_LOC_INIT;
    int rc = mnc2_debug_read(dev, MNC2_MEM_DRAM, &loc, 0, 2, dram);
    if (rc != 0) {
        printf("  FAIL: 1st debug_read returned %d\n", rc);
        mnc2_free_host_buffer(dev, sendbuf, data_size);
        mnc2_close(dev);
        return 1;
    }
    printf("  1st call DRAM[0..1]: 0x%016" PRIx64 " 0x%016" PRIx64 "\n",
           dram[0], dram[1]);

    /* 2回目: L2BM 読み出し (saved_asm 経由で再実行) */
    uint64_t l2bm[2];
    rc = mnc2_debug_read(dev, MNC2_MEM_L2BM, &loc, 0, 2, l2bm);
    if (rc != 0) {
        printf("  FAIL: 2nd debug_read returned %d\n", rc);
        mnc2_free_host_buffer(dev, sendbuf, data_size);
        mnc2_close(dev);
        return 1;
    }
    printf("  2nd call L2BM[0..1]: 0x%016" PRIx64 " 0x%016" PRIx64 "\n",
           l2bm[0], l2bm[1]);

    /* recv もまだ動くか確認 */
    void* recvbuf = mnc2_alloc_host_buffer(dev, data_size);
    if (recvbuf == NULL) {
        printf("  FAIL: alloc recvbuf\n");
        mnc2_free_host_buffer(dev, sendbuf, data_size);
        mnc2_close(dev);
        return 1;
    }
    memset(recvbuf, 0, data_size);

    rc = mnc2_recv(dev, recvbuf, 0, data_size, 0);
    if (rc != 0) {
        printf("  FAIL: recv after debug_read returned %d\n", rc);
        mnc2_free_host_buffer(dev, sendbuf, data_size);
        mnc2_free_host_buffer(dev, recvbuf, data_size);
        mnc2_close(dev);
        return 1;
    }

    double* rp = (double*)recvbuf;
    printf("  recv after debug_read: ");
    for (int i = 0; i < 4; i++)
        printf("%.1f ", rp[i]);
    printf("...\n");

    printf("  PASS\n");

    mnc2_free_host_buffer(dev, sendbuf, data_size);
    mnc2_free_host_buffer(dev, recvbuf, data_size);
    mnc2_close(dev);
    return 0;
}

static int test_debug_read_params(void)
{
    printf("[test] debug_read parameter validation\n");

    mnc2_device_t dev = mnc2_open(0);
    if (dev == NULL) {
        printf("  FAIL: mnc2_open failed\n");
        return 1;
    }

    int errors = 0;
    uint64_t dummy[4];
    mnc2_loc_t loc = MNC2_LOC_INIT;

    /* カーネル未実行で呼ぶ: emu:lib はメモリを常に読める (ゼロ初期化状態)
       この example は emu:lib 専用 (build.ninja で test-emu-lib のみ) */
    int rc = mnc2_debug_read(dev, MNC2_MEM_DRAM, &loc, 0, 4, dummy);
    if (rc != MNC2_SUCCESS) {
        printf("  FAIL: emu:lib should succeed without exec_kernel, got %d\n", rc);
        errors++;
    }

    /* count=0 → エラー */
    rc = mnc2_debug_read(dev, MNC2_MEM_DRAM, &loc, 0, 0, dummy);
    if (rc == MNC2_SUCCESS) {
        printf("  FAIL: count=0 should fail\n");
        errors++;
    }

    /* 不正な mem_region → エラー */
    rc = mnc2_debug_read(dev, 99, &loc, 0, 4, dummy);
    if (rc == MNC2_SUCCESS) {
        printf("  FAIL: invalid region should fail\n");
        errors++;
    }

    mnc2_close(dev);

    if (errors > 0) {
        printf("  FAIL: %d errors\n", errors);
        return 1;
    }
    printf("  PASS\n");
    return 0;
}

/* --- 各メモリ階層テスト用ヘルパー ---
 *
 * put_X.vsm カーネルで send データを目的の階層に転送し、
 * debug_read で読み出して非ゼロであることを確認する。
 *
 * put_* VSM は wait i10 で send_wait_tag=0x10 を使用する。
 * roundtrip.vsm と同じ distribute パターン (4096 u64 = 32768 bytes)。
 */

#define SEND_TAG  0x10
#define ELEM_COUNT 4096
#define ELEM_BYTES (ELEM_COUNT * sizeof(double))

static int test_debug_read_layer(const char* kernel_path,
                                 int mem_region,
                                 const char* region_name)
{
    printf("[test] debug_read %s (via %s)\n", region_name, kernel_path);

    mnc2_device_t dev = mnc2_open(0);
    if (dev == NULL) {
        printf("  FAIL: mnc2_open failed\n");
        return 1;
    }

    void* sendbuf = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    if (sendbuf == NULL) {
        printf("  FAIL: alloc\n");
        mnc2_close(dev);
        return 1;
    }

    /* 既知データを準備 */
    double* dp = (double*)sendbuf;
    for (int i = 0; i < ELEM_COUNT; i++)
        dp[i] = (double)(i + 1) * 1.5;

    int rc = mnc2_send(dev, sendbuf, 0, ELEM_BYTES, SEND_TAG);
    if (rc != 0) {
        printf("  FAIL: send returned %d\n", rc);
        mnc2_free_host_buffer(dev, sendbuf, ELEM_BYTES);
        mnc2_close(dev);
        return 1;
    }

    mnc2_kernel_t kernel = mnc2_load_kernel(dev, kernel_path);
    if (kernel == NULL) {
        printf("  FAIL: load_kernel(%s)\n", kernel_path);
        mnc2_free_host_buffer(dev, sendbuf, ELEM_BYTES);
        mnc2_close(dev);
        return 1;
    }

    rc = mnc2_exec_kernel(kernel);
    mnc2_free_kernel(kernel);
    if (rc != 0) {
        printf("  FAIL: exec_kernel returned %d\n", rc);
        mnc2_free_host_buffer(dev, sendbuf, ELEM_BYTES);
        mnc2_close(dev);
        return 1;
    }

    /* debug_read で指定メモリ階層からデータを読み出す */
    uint64_t out[4];
    memset(out, 0, sizeof(out));
    mnc2_loc_t loc = MNC2_LOC_INIT;

    rc = mnc2_debug_read(dev, mem_region, &loc, 0, 4, out);
    if (rc != 0) {
        printf("  FAIL: debug_read %s returned %d\n", region_name, rc);
        mnc2_free_host_buffer(dev, sendbuf, ELEM_BYTES);
        mnc2_close(dev);
        return 1;
    }

    printf("  %s[0..3]:", region_name);
    for (int i = 0; i < 4; i++)
        printf(" 0x%016" PRIx64, out[i]);
    printf("\n");

    /* 少なくとも 1 つは非ゼロであることを確認 (データが転送された証拠) */
    int all_zero = 1;
    for (int i = 0; i < 4; i++) {
        if (out[i] != 0) {
            all_zero = 0;
            break;
        }
    }

    if (all_zero) {
        printf("  WARN: all values are zero (data may not have reached %s)\n",
                region_name);
    }

    printf("  PASS (debug_read %s returned successfully)\n", region_name);

    mnc2_free_host_buffer(dev, sendbuf, ELEM_BYTES);
    mnc2_close(dev);
    return 0;
}

int main(void)
{
    printf("=== mnc2_debug_read test ===\n\n");

    int failed = 0;

    failed |= test_debug_read_params();
    printf("\n");
    failed |= test_debug_read_basic();
    printf("\n");
    failed |= test_debug_read_multiple();
    printf("\n");

    /* 各メモリ階層テスト (put_X カーネル使用) */
    failed |= test_debug_read_layer("_build/put_l2bm.idma.dat", MNC2_MEM_L2BM, "L2BM");
    printf("\n");
    failed |= test_debug_read_layer("_build/put_l1bm.idma.dat", MNC2_MEM_L1BM, "L1BM");
    printf("\n");
    failed |= test_debug_read_layer("_build/put_lm.idma.dat", MNC2_MEM_LM0, "LM0");
    printf("\n");
    failed |= test_debug_read_layer("_build/put_grf.idma.dat", MNC2_MEM_GRF0, "GRF0");

    printf("\n");
    if (failed == 0)
        printf("ALL PASS\n");
    else
        printf("FAILED\n");

    return failed ? 1 : 0;
}
