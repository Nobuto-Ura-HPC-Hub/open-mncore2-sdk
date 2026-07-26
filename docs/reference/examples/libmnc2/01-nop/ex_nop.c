/* ex_nop.c — libmnc2 01-nop 使用例 (demo)
 *
 * nop カーネルを使った最小 DMA round-trip:
 *   1. mnc2_open(0)
 *   2. データ準備 + mnc2_send (エンディアン変換は send 内部で自動処理)
 *   3. mnc2_load_kernel + mnc2_exec_kernel
 *   4. mnc2_recv (エンディアン変換は recv 内部で自動処理)
 *   5. データ比較
 *
 * 環境変数:
 *   MNC2_EMU_CONFIG — emu:process 用 config.json のパス
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mnc2.h"

#define TEST_COUNT   8

static int test_round_trip(void)
{
    mnc2_device_t dev = mnc2_open(0);
    if (dev == NULL) {
        printf("FAIL: mnc2_open returned NULL\n");
        printf("  MNC2_EMU_CONFIG=%s\n",
                getenv("MNC2_EMU_CONFIG") ? getenv("MNC2_EMU_CONFIG") : "(unset)");
        return 1;
    }

    printf("=== libmnc2 %s DMA round-trip (%d doubles) ===\n",
           mnc2_get_backend_name(dev), TEST_COUNT);

    size_t data_size = TEST_COUNT * sizeof(double);

    void* sendbuf = mnc2_alloc_host_buffer(dev, data_size);
    void* recvbuf = mnc2_alloc_host_buffer(dev, data_size);
    if (sendbuf == NULL || recvbuf == NULL) {
        printf("  FAIL: mnc2_alloc_host_buffer\n");
        mnc2_close(dev);
        return 1;
    }
    memset(recvbuf, 0, data_size);

    double* dp = (double*)sendbuf;
    for (int i = 0; i < TEST_COUNT; i++)
        dp[i] = (double)(i + 1) * 1.5;

    printf("  send: ");
    for (int i = 0; i < TEST_COUNT; i++)
        printf("%.1f ", dp[i]);
    printf("\n");

    int rc = mnc2_send(dev, sendbuf, 0, data_size, /*send_tag=*/1);
    if (rc != 0) {
        printf("  FAIL: mnc2_send returned %d\n", rc);
        goto cleanup;
    }

    mnc2_kernel_t kernel = mnc2_load_kernel(dev, "_build/nop.idma.dat");
    if (kernel == NULL) {
        printf("  FAIL: mnc2_load_kernel\n");
        rc = 1;
        goto cleanup;
    }

    rc = mnc2_exec_kernel(kernel);
    mnc2_free_kernel(kernel);
    if (rc != 0) {
        printf("  FAIL: mnc2_exec_kernel returned %d\n", rc);
        goto cleanup;
    }

    rc = mnc2_recv(dev, recvbuf, 0, data_size, /*recv_tag=*/0);
    if (rc != 0) {
        printf("  FAIL: mnc2_recv returned %d\n", rc);
        goto cleanup;
    }

    double* rp = (double*)recvbuf;
    printf("  recv: ");
    for (int i = 0; i < TEST_COUNT; i++)
        printf("%.1f ", rp[i]);
    printf("\n");

    int errors = 0;
    for (int i = 0; i < TEST_COUNT; i++) {
        double expected = (double)(i + 1) * 1.5;
        if (rp[i] != expected) {
            printf("  MISMATCH [%d]: got=%g expected=%g\n",
                    i, rp[i], expected);
            errors++;
        }
    }

    if (errors > 0) {
        printf("  FAIL: %d mismatches\n", errors);
        rc = 1;
    } else {
        printf("  PASS\n");
        rc = 0;
    }

cleanup:
    mnc2_free_host_buffer(dev, sendbuf, data_size);
    mnc2_free_host_buffer(dev, recvbuf, data_size);
    mnc2_close(dev);
    return rc;
}

int main(void)
{
    int failed = test_round_trip();

    printf("\n");
    printf(failed == 0 ? "ALL PASS\n" : "FAILED\n");
    return failed ? 1 : 0;
}
