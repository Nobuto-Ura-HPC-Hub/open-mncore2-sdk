/* ex_wd_zero.c — wd=0 無条件 DMA 開始の検証
 *
 * チップ仕様書: 「WD が 0 の場合には無条件で DMA を開始する」
 * send(tag=0) → nop exec → recv(tag=0) で round-trip。
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "mnc2.h"

#define TEST_COUNT  8
#define DATA_SIZE   (TEST_COUNT * sizeof(double))

int main(void)
{
    printf("=== wd=0 unconditional DMA test ===\n\n");

    mnc2_device_t dev = mnc2_open(0);
    if (dev == NULL) {
        printf("FAIL: mnc2_open failed\n");
        return 1;
    }

    printf("backend: %s\n", mnc2_get_backend_name(dev));

    void* sendbuf = mnc2_alloc_host_buffer(dev, DATA_SIZE);
    void* recvbuf = mnc2_alloc_host_buffer(dev, DATA_SIZE);
    if (sendbuf == NULL || recvbuf == NULL) {
        printf("FAIL: alloc\n");
        mnc2_close(dev);
        return 1;
    }
    memset(recvbuf, 0, DATA_SIZE);

    double* dp = (double*)sendbuf;
    for (int i = 0; i < TEST_COUNT; i++)
        dp[i] = (double)(i + 1) * 2.5;

    printf("[test] send(tag=0) -> nop -> recv(tag=0)\n");
    int rc = mnc2_send(dev, sendbuf, 0, DATA_SIZE, 0);
    if (rc != 0) {
        printf("  FAIL: send returned %d\n", rc);
        goto fail;
    }

    mnc2_kernel_t k = mnc2_load_kernel(dev, "_build/nop.idma.dat");
    if (k == NULL) {
        printf("  FAIL: load_kernel\n");
        goto fail;
    }
    rc = mnc2_exec_kernel(k);
    mnc2_free_kernel(k);
    if (rc != 0) {
        printf("  FAIL: exec_kernel returned %d\n", rc);
        goto fail;
    }

    rc = mnc2_recv(dev, recvbuf, 0, DATA_SIZE, 0);
    if (rc != 0) {
        printf("  FAIL: recv(tag=0) returned %d\n", rc);
        goto fail;
    }

    double* rp = (double*)recvbuf;
    int errors = 0;
    for (int i = 0; i < TEST_COUNT; i++) {
        if (fabs(rp[i] - dp[i]) > 1e-10) {
            if (errors < 5)
                printf("  MISMATCH [%d]: got=%g expected=%g\n",
                        i, rp[i], dp[i]);
            errors++;
        }
    }

    if (errors > 0) {
        printf("  FAIL: %d/%d mismatches\n", errors, TEST_COUNT);
        goto fail;
    }

    printf("  PASS: wd=0 recv completed without done_flag\n");
    mnc2_free_host_buffer(dev, sendbuf, DATA_SIZE);
    mnc2_free_host_buffer(dev, recvbuf, DATA_SIZE);
    mnc2_close(dev);
    printf("\nALL PASS\n");
    return 0;

fail:
    mnc2_free_host_buffer(dev, sendbuf, DATA_SIZE);
    mnc2_free_host_buffer(dev, recvbuf, DATA_SIZE);
    mnc2_close(dev);
    return 1;
}
