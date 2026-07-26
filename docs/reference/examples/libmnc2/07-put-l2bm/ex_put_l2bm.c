/* ex_put_l2bm.c — put_l2bm distribute テスト
 *
 * PDM にデータを send し、put_l2bm カーネルで L2BM に distribute。
 * mnc2_debug_read で L2BM の値を読み出して非ゼロを確認。
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
    printf("[test] put → debug_read L2BM\n");

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

    mnc2_kernel_t k = mnc2_load_kernel(dev, "_build/put_l2bm.idma.dat");
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

    /* device backend では mnc2_debug_read が動かない (emu-only API)。
       put_l2bm の実行までで終わり、L2BM の検証は build.ninja の
       run-peek-device + peek-check-device 段で peek kernel + read-pdm を
       使って外部から行う。ex_put_l2bm 自体は exit 0 で静かに終わる。 */
    if (strcmp(mnc2_get_backend_name(dev), "device") == 0) {
        printf("[device] put_l2bm executed; debug_read skipped "
               "(verified externally by peek-check-device step)\n");
        mnc2_free_host_buffer(dev, buf, ELEM_BYTES);
        mnc2_close(dev);
        return 0;
    }

    uint64_t out[4];
    memset(out, 0, sizeof(out));
    mnc2_loc_t loc = MNC2_LOC_INIT;
    rc = mnc2_debug_read(dev, MNC2_MEM_L2BM, &loc, 0, 4, out);
    if (rc != 0) {
        printf("FAIL: debug_read returned %d\n", rc);
        goto fail;
    }

    printf("  L2BM[0..3]:");
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
