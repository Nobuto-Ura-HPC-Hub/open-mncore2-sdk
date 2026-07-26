/* test_abs.c -- fp64 abs E2E test (if/else を使う kernel)
 *
 * c[i] = |a[i]| on 4096 PEs.
 * 入力には正の値 / 負の値 / ゼロを混ぜて、 if/else 両分岐を検証する。
 *
 * PDM layout (abs.param):
 *   buf1 (a): PDM[0..4095]        -- byte offset 0
 *   buf2 (c): PDM[131072..135167] -- byte offset 1048576
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mnc2.h"

#define ELEM_COUNT  4096
#define ELEM_BYTES  (ELEM_COUNT * sizeof(double))
#define OFFSET_A    0
#define OFFSET_C    (131072ULL * 8)
#define SEND_WAIT_TAG 0x10
#define RECV_TAG      0x1e

int main(void)
{
    printf("=== abs: c[i] = |a[i]| (4096 PE, fp64, if/else) ===\n\n");

    mnc2_device_t dev = mnc2_open(0);
    if (dev == NULL) { fprintf(stderr, "FAIL: mnc2_open\n"); return 1; }
    printf("09-abs backend: %s\n", mnc2_get_backend_name(dev));

    int rc = 0;
    void *sendbuf = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    void *recvbuf = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    double *orig_a = (double *)malloc(ELEM_BYTES);
    if (!sendbuf || !recvbuf || !orig_a) {
        fprintf(stderr, "FAIL: alloc\n"); rc = 1; goto cleanup;
    }

    /* 正・負・ゼロ混在で if/else 両分岐を踏む */
    for (int i = 0; i < ELEM_COUNT; i++) {
        if (i % 3 == 0)      orig_a[i] = (double)(i + 1);     /* 正 */
        else if (i % 3 == 1) orig_a[i] = -(double)(i + 1);    /* 負 */
        else                 orig_a[i] = 0.0;                 /* ゼロ */
    }

    memcpy(sendbuf, orig_a, ELEM_BYTES);
    if (mnc2_send(dev, sendbuf, OFFSET_A, ELEM_BYTES, SEND_WAIT_TAG) != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: send a\n"); rc = 1; goto cleanup;
    }

    mnc2_kernel_t kernel = mnc2_load_kernel(dev, "_build/abs.idma.dat");
    if (!kernel) { fprintf(stderr, "FAIL: load_kernel\n"); rc = 1; goto cleanup; }
    int krc = mnc2_exec_kernel(kernel);
    mnc2_free_kernel(kernel);
    if (krc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: exec\n"); rc = 1; goto cleanup; }

    memset(recvbuf, 0, ELEM_BYTES);
    if (mnc2_recv(dev, recvbuf, OFFSET_C, ELEM_BYTES, RECV_TAG) != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: recv\n"); rc = 1; goto cleanup;
    }

    /* verify: c[i] == |a[i]| */
    double *rp = (double *)recvbuf;
    int errors = 0, pos_ok = 0, neg_ok = 0, zero_ok = 0;
    for (int i = 0; i < ELEM_COUNT; i++) {
        double expected = fabs(orig_a[i]);
        if (rp[i] != expected) {
            if (errors < 5)
                fprintf(stderr, "  MISMATCH c[%d]: got=%g expected=%g (a=%g)\n",
                        i, rp[i], expected, orig_a[i]);
            errors++;
        } else {
            if (orig_a[i] > 0)      pos_ok++;
            else if (orig_a[i] < 0) neg_ok++;
            else                    zero_ok++;
        }
    }

    if (errors > 0) {
        fprintf(stderr, "FAIL: %d/%d mismatches\n", errors, ELEM_COUNT); rc = 1;
    } else {
        printf("PASS: c[i] == |a[i]| for all %d elements (positive: %d, negative: %d, zero: %d)\n",
               ELEM_COUNT, pos_ok, neg_ok, zero_ok); rc = 0;
    }

cleanup:
    if (sendbuf) mnc2_free_host_buffer(dev, sendbuf, ELEM_BYTES);
    if (recvbuf) mnc2_free_host_buffer(dev, recvbuf, ELEM_BYTES);
    mnc2_close(dev);
    free(orig_a);
    return (rc != 0) ? 1 : 0;
}
