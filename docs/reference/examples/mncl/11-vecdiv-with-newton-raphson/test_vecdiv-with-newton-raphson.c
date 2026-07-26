/* test_vecdiv-with-newton-raphson.c -- Newton-Raphson 法による vecdiv E2E
 *
 * c[i] = a[i] / b[i] を kernel 内で Newton-Raphson 5 反復で計算する。
 * 期待精度: full double (rel_err < 1e-12)。
 *
 * PDM layout (vecdiv-with-newton-raphson.param):
 *   slot 8  (a): PDM offset 0
 *   slot 16 (b): PDM offset 32768  (4096 * 8)
 *   slot 24 (c): PDM offset 1048576 (131072 * 8)
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mnc2.h"

#define ELEM_COUNT     4096
#define ELEM_BYTES     (ELEM_COUNT * sizeof(double))
#define OFFSET_A       0
#define OFFSET_B       (4096ULL * 8)
#define OFFSET_C       (131072ULL * 8)
#define SEND_WAIT_TAG  0x10
#define RECV_TAG       0x1e

int main(void)
{
    printf("=== vecdiv: c[i] = a[i] / b[i] (Newton-Raphson 5 反復、 4096 PE, fp64) ===\n\n");

    mnc2_device_t dev = mnc2_open(0);
    if (dev == NULL) { fprintf(stderr, "FAIL: mnc2_open\n"); return 1; }
    printf("11-vecdiv-with-newton-raphson backend: %s\n\n", mnc2_get_backend_name(dev));

    int rc = 0;
    void *sendbuf = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    void *recvbuf = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    double *orig_a = (double *)malloc(ELEM_BYTES);
    double *orig_b = (double *)malloc(ELEM_BYTES);
    if (!sendbuf || !recvbuf || !orig_a || !orig_b) {
        fprintf(stderr, "FAIL: alloc\n"); rc = 1; goto cleanup;
    }

    /* 偶数 PE は正の除数、 奇数 PE は負の除数 */
    for (int i = 0; i < ELEM_COUNT; i++) {
        orig_a[i] = (double)(i + 1) * 2.0;
        orig_b[i] = (i % 2 == 0) ? (double)(i + 1) : -(double)(i + 1);
    }

    memcpy(sendbuf, orig_b, ELEM_BYTES);
    if (mnc2_send(dev, sendbuf, OFFSET_B, ELEM_BYTES, 0) != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: send b\n"); rc = 1; goto cleanup;
    }
    memcpy(sendbuf, orig_a, ELEM_BYTES);
    if (mnc2_send(dev, sendbuf, OFFSET_A, ELEM_BYTES, SEND_WAIT_TAG) != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: send a\n"); rc = 1; goto cleanup;
    }

    mnc2_kernel_t kernel = mnc2_load_kernel(dev, "_build/vecdiv-with-newton-raphson.idma.dat");
    if (!kernel) { fprintf(stderr, "FAIL: load_kernel\n"); rc = 1; goto cleanup; }
    int krc = mnc2_exec_kernel(kernel);
    mnc2_free_kernel(kernel);
    if (krc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: exec\n"); rc = 1; goto cleanup; }

    memset(recvbuf, 0, ELEM_BYTES);
    if (mnc2_recv(dev, recvbuf, OFFSET_C, ELEM_BYTES, RECV_TAG) != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: recv\n"); rc = 1; goto cleanup;
    }

    /* 検証: full double 精度を期待 (rel_err < 1e-12) */
    double *rp = (double *)recvbuf;
    int errors = 0;
    double max_rel = 0, sum_rel = 0;
    for (int i = 0; i < ELEM_COUNT; i++) {
        double expected = orig_a[i] / orig_b[i];
        double rel_err = fabs(rp[i] - expected) / fabs(expected);
        if (rel_err > 1e-12) {
            if (errors < 5)
                fprintf(stderr, "  MISMATCH c[%d]: got=%.17g expected=%.17g rel_err=%.3e\n",
                        i, rp[i], expected, rel_err);
            errors++;
        }
        if (rel_err > max_rel) max_rel = rel_err;
        sum_rel += rel_err;
    }

    if (errors > 0) {
        fprintf(stderr, "FAIL: %d/%d mismatches (max_rel=%.3e avg_rel=%.3e)\n",
                errors, ELEM_COUNT, max_rel, sum_rel / ELEM_COUNT);
        rc = 1;
    } else {
        printf("PASS %d/%d\n", ELEM_COUNT, ELEM_COUNT);
        printf("  precision: max_rel_err=%.3e avg_rel_err=%.3e\n", max_rel, sum_rel / ELEM_COUNT);
        rc = 0;
    }

cleanup:
    if (sendbuf) mnc2_free_host_buffer(dev, sendbuf, ELEM_BYTES);
    if (recvbuf) mnc2_free_host_buffer(dev, recvbuf, ELEM_BYTES);
    mnc2_close(dev);
    free(orig_a); free(orig_b);
    return (rc != 0) ? 1 : 0;
}
