/* test_put_get.c — put_X + get_X ペアで各メモリ階層の roundtrip テスト
 *
 * emu:lib バックエンドではカーネル間でメモリ状態が保持されるため、
 * put_X で distribute → get_X で collect → recv で検証 ができる。
 *
 * 使い方:
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "mnc2.h"

#define ELEM_COUNT  4096
#define ELEM_BYTES  (ELEM_COUNT * sizeof(double))
#define OFFSET_IN   0
#define OFFSET_OUT  (131072ULL * 8)   /* PDM word 131072 → byte 1048576 */
#define SEND_TAG  0x10              /* put_X VSM: wait i10 */
#define RECV_TAG     0x1e              /* get_X VSM: 最終 mvp done flag */

static int test_put_get_pair(const char* put_kernel, const char* get_kernel,
                             const char* label)
{
    printf("[test] put+get %s\n", label);

    mnc2_device_t dev = mnc2_open(0);
    if (dev == NULL) {
        printf("  FAIL: mnc2_open failed\n");
        return 1;
    }

    /* この example は emu:lib / device 専用 (build.ninja で test-emu 不在)。
       put+get 間で memory 状態が保持される必要があり、emu:process 非対応。 */

    /* バッファ確保 */
    void* sendbuf = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    void* recvbuf = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    if (sendbuf == NULL || recvbuf == NULL) {
        printf("  FAIL: alloc\n");
        mnc2_close(dev);
        return 1;
    }
    memset(recvbuf, 0, ELEM_BYTES);

    /* テストデータ: input[i] = (i+1) * 1.5 */
    double* dp = (double*)sendbuf;
    for (int i = 0; i < ELEM_COUNT; i++)
        dp[i] = (double)(i + 1) * 1.5;

    /* 1. send */
    int rc = mnc2_send(dev, sendbuf, OFFSET_IN, ELEM_BYTES,
                       SEND_TAG);
    if (rc != 0) {
        printf("  FAIL: send returned %d\n", rc);
        goto fail;
    }

    /* 2. exec put_X (distribute to target memory) */
    mnc2_kernel_t kernel = mnc2_load_kernel(dev, put_kernel);
    if (kernel == NULL) {
        printf("  FAIL: load_kernel(%s)\n", put_kernel);
        goto fail;
    }
    rc = mnc2_exec_kernel(kernel);
    mnc2_free_kernel(kernel);
    if (rc != 0) {
        printf("  FAIL: exec put returned %d\n", rc);
        goto fail;
    }

    /* 3. exec get_X (collect from target memory to PDM output) */
    kernel = mnc2_load_kernel(dev, get_kernel);
    if (kernel == NULL) {
        printf("  FAIL: load_kernel(%s)\n", get_kernel);
        goto fail;
    }
    rc = mnc2_exec_kernel(kernel);
    mnc2_free_kernel(kernel);
    if (rc != 0) {
        printf("  FAIL: exec get returned %d\n", rc);
        goto fail;
    }

    /* 4. recv */
    rc = mnc2_recv(dev, recvbuf, OFFSET_OUT, ELEM_BYTES,
                   RECV_TAG);
    if (rc != 0) {
        printf("  FAIL: recv returned %d\n", rc);
        goto fail;
    }

    /* 5. 検証: 入力 == 出力 */
    double* rp = (double*)recvbuf;
    printf("  send[0..3]: %.1f %.1f %.1f %.1f\n", dp[0], dp[1], dp[2], dp[3]);
    printf("  recv[0..3]: %.1f %.1f %.1f %.1f\n", rp[0], rp[1], rp[2], rp[3]);

    int errors = 0;
    for (int i = 0; i < ELEM_COUNT; i++) {
        double expected = (double)(i + 1) * 1.5;
        if (fabs(rp[i] - expected) > 1e-10) {
            if (errors < 5)
                printf("  MISMATCH [%d]: got=%g expected=%g\n",
                        i, rp[i], expected);
            errors++;
        }
    }

    if (errors > 0) {
        printf("  FAIL: %d/%d mismatches\n", errors, ELEM_COUNT);
        goto fail;
    }

    printf("  PASS (%d elements verified)\n", ELEM_COUNT);

    mnc2_free_host_buffer(dev, sendbuf, ELEM_BYTES);
    mnc2_free_host_buffer(dev, recvbuf, ELEM_BYTES);
    mnc2_close(dev);
    return 0;

fail:
    mnc2_free_host_buffer(dev, sendbuf, ELEM_BYTES);
    mnc2_free_host_buffer(dev, recvbuf, ELEM_BYTES);
    mnc2_close(dev);
    return 1;
}

int main(void)
{
    printf("=== put+get memory layer roundtrip test ===\n\n");

    int failed = 0;

    failed |= test_put_get_pair(
        "_build/put_l2bm.idma.dat", "_build/get_l2bm.idma.dat", "L2BM");
    printf("\n");

    failed |= test_put_get_pair(
        "_build/put_l1bm.idma.dat", "_build/get_l1bm.idma.dat", "L1BM");
    printf("\n");

    failed |= test_put_get_pair(
        "_build/put_lm.idma.dat", "_build/get_lm.idma.dat", "LM0");
    printf("\n");

    failed |= test_put_get_pair(
        "_build/put_grf.idma.dat", "_build/get_grf.idma.dat", "GRF0");

    printf("\n");
    if (failed == 0)
        printf("ALL PASS\n");
    else
        printf("FAILED\n");

    return failed ? 1 : 0;
}
