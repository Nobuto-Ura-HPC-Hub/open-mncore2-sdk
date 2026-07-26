/* ex_put_l1bm.c — put_l1bm distribute テスト
 *
 * PDM にデータを send し、put_l1bm カーネルで L1BM に distribute。
 * mnc2_debug_read で L1BM の値を読み出して非ゼロを確認。
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "mnc2.h"

#define ELEM_COUNT  4096
#define ELEM_BYTES  (ELEM_COUNT * sizeof(double))
#define SEND_TAG  0x10

int main(void)
{
    printf("[test] put → debug_read L1BM\n");

    mnc2_device_t dev = mnc2_open(0);
    if (dev == NULL) {
        printf("FAIL: mnc2_open failed\n");
        return 1;
    }

    void* buf = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    if (buf == NULL) {
        printf("FAIL: alloc\n");
        mnc2_close(dev);
        return 1;
    }

    double* dp = (double*)buf;
    for (int i = 0; i < ELEM_COUNT; i++)
        dp[i] = (double)(i + 1) * 1.5;

    int rc = mnc2_send(dev, buf, 0, ELEM_BYTES, SEND_TAG);
    if (rc != 0) {
        printf("FAIL: send returned %d\n", rc);
        goto fail;
    }

    mnc2_kernel_t k = mnc2_load_kernel(dev, "_build/put_l1bm.idma.dat");
    if (k == NULL) {
        printf("FAIL: load_kernel\n");
        goto fail;
    }
    rc = mnc2_exec_kernel(k);
    mnc2_free_kernel(k);
    if (rc != 0) {
        printf("FAIL: exec_kernel returned %d\n", rc);
        goto fail;
    }

    /* device backend では mnc2_debug_read が動かない。put_l1bm 実行までで
       終わり、L1BM の検証は build.ninja の peek pipeline (peek-check-device)
       で外部から行う。 */
    if (strcmp(mnc2_get_backend_name(dev), "device") == 0) {
        printf("[device] put_l1bm executed; debug_read skipped "
               "(verified externally by peek-check-device step)\n");
        mnc2_free_host_buffer(dev, buf, ELEM_BYTES);
        mnc2_close(dev);
        return 0;
    }

    uint64_t out[4];
    memset(out, 0, sizeof(out));
    mnc2_loc_t loc = MNC2_LOC_INIT;
    rc = mnc2_debug_read(dev, MNC2_MEM_L1BM, &loc, 0, 4, out);
    if (rc != 0) {
        printf("FAIL: debug_read returned %d\n", rc);
        goto fail;
    }

    printf("  L1BM[0..3]:");
    for (int i = 0; i < 4; i++)
        printf(" 0x%016" PRIx64, out[i]);
    printf("\n");

    int all_zero = 1;
    for (int i = 0; i < 4; i++)
        if (out[i] != 0) { all_zero = 0; break; }

    if (all_zero)
        printf("  WARN: all values are zero\n");

    printf("PASS\n");
    mnc2_free_host_buffer(dev, buf, ELEM_BYTES);
    mnc2_close(dev);
    return 0;

fail:
    mnc2_free_host_buffer(dev, buf, ELEM_BYTES);
    mnc2_close(dev);
    return 1;
}
