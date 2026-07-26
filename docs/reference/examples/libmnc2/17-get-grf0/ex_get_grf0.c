/* ex_get_grf0.c — get_grf collect roundtrip テスト
 *
 * PDM にデータを send し、put_grf で GRF0 に distribute、
 * get_grf で collect して PDM に書き戻し、recv で検証。
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "mnc2.h"

#define ELEM_COUNT  4096
#define ELEM_BYTES  (ELEM_COUNT * sizeof(double))
#define OFFSET_OUT  (131072ULL * 8)
#define SEND_TAG  0x10
#define RECV_TAG     0x1e

int main(void)
{
    printf("[test] put+get GRF0 roundtrip\n");

    mnc2_device_t dev = mnc2_open(0);
    if (dev == NULL) {
        printf("FAIL: mnc2_open failed\n");
        return 1;
    }

    /* この example は emu:lib / device 専用 (build.ninja で test-emu 不在)。
       put+get 間で memory 状態の保持が必要、emu:process 非対応。 */

    void* sendbuf = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    void* recvbuf = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    if (sendbuf == NULL || recvbuf == NULL) {
        printf("FAIL: alloc\n");
        mnc2_close(dev);
        return 1;
    }
    memset(recvbuf, 0, ELEM_BYTES);

    double* dp = (double*)sendbuf;
    for (int i = 0; i < ELEM_COUNT; i++)
        dp[i] = (double)(i + 1) * 1.5;

    int rc = mnc2_send(dev, sendbuf, 0, ELEM_BYTES, SEND_TAG);
    if (rc != 0) {
        printf("FAIL: send returned %d\n", rc);
        goto fail;
    }

    mnc2_kernel_t k = mnc2_load_kernel(dev, "_build/put_grf.idma.dat");
    if (k == NULL) {
        printf("FAIL: load put kernel\n");
        goto fail;
    }
    rc = mnc2_exec_kernel(k);
    mnc2_free_kernel(k);
    if (rc != 0) {
        printf("FAIL: exec put returned %d\n", rc);
        goto fail;
    }

    k = mnc2_load_kernel(dev, "_build/get_grf.idma.dat");
    if (k == NULL) {
        printf("FAIL: load get kernel\n");
        goto fail;
    }
    rc = mnc2_exec_kernel(k);
    mnc2_free_kernel(k);
    if (rc != 0) {
        printf("FAIL: exec get returned %d\n", rc);
        goto fail;
    }

    rc = mnc2_recv(dev, recvbuf, OFFSET_OUT, ELEM_BYTES, RECV_TAG);
    if (rc != 0) {
        printf("FAIL: recv returned %d\n", rc);
        goto fail;
    }

    double* rp = (double*)recvbuf;
    printf("  send[0..3]: %.1f %.1f %.1f %.1f\n", dp[0], dp[1], dp[2], dp[3]);
    printf("  recv[0..3]: %.1f %.1f %.1f %.1f\n", rp[0], rp[1], rp[2], rp[3]);

    int errors = 0;
    for (int i = 0; i < ELEM_COUNT; i++) {
        if (fabs(rp[i] - dp[i]) > 1e-10) {
            if (errors < 5)
                printf("  MISMATCH [%d]: got=%g expected=%g\n", i, rp[i], dp[i]);
            errors++;
        }
    }

    if (errors > 0) {
        printf("FAIL: %d/%d mismatches\n", errors, ELEM_COUNT);
        goto fail;
    }

    printf("PASS (%d elements verified)\n", ELEM_COUNT);
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
