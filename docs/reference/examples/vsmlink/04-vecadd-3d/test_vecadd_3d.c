/* test_vecadd_3d.c — 3D vecadd E2E 検証 (emu:lib)
 *
 * c[i][j][k] = a[i][j][k] + b[i][j][k] を 16x16x16 PE で実行し、結果を検証する。
 * カーネルは 02-vecadd と同一（PE あたりの演算は同じ）。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mnc2.h"

#define NI          16
#define NJ          16
#define NK          16
#define ELEM_COUNT  (NI * NJ * NK)          /* = 4096 */
#define ELEM_BYTES  (ELEM_COUNT * sizeof(double))
#define OFFSET_A    0                       /* slot 8:  PDM word 0      */
#define OFFSET_B    (4096ULL * 8)           /* slot 16: PDM word 4096   */
#define OFFSET_C    (131072ULL * 8)         /* slot 24: PDM word 131072 */
#define DMAID_TRIGGER 0x10
#define WD_RECV     0x1e  /* .param recv_wait_tag と対応 */

int main(void)
{
    printf("=== vecadd 3D: c[i][j][k] = a[i][j][k] + b[i][j][k] (%dx%dx%d PE) ===\n\n",
           NI, NJ, NK);

    mnc2_device_t dev = mnc2_open(0);
    if (dev == NULL) {
        fprintf(stderr, "FAIL: mnc2_open returned NULL\n");
        return 1;
    }

    void *sendbuf = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    void *recvbuf = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    if (sendbuf == NULL || recvbuf == NULL) {
        fprintf(stderr, "FAIL: mnc2_alloc_host_buffer\n");
        mnc2_close(dev);
        return 1;
    }

    double *orig_a = (double *)malloc(ELEM_BYTES);
    double *orig_b = (double *)malloc(ELEM_BYTES);
    if (orig_a == NULL || orig_b == NULL) {
        fprintf(stderr, "FAIL: malloc\n");
        mnc2_free_host_buffer(dev, sendbuf, ELEM_BYTES);
        mnc2_free_host_buffer(dev, recvbuf, ELEM_BYTES);
        mnc2_close(dev);
        free(orig_a); free(orig_b);
        return 1;
    }
    for (int i = 0; i < ELEM_COUNT; i++) {
        orig_a[i] = (double)(i + 1);
        orig_b[i] = (double)(i + 1) * 10.0;
    }

    int rc;

    printf("[send] b -> PDM offset %llu (dmaid=0)\n", (unsigned long long)OFFSET_B);
    memcpy(sendbuf, orig_b, ELEM_BYTES);
    rc = mnc2_send(dev, sendbuf, OFFSET_B, ELEM_BYTES, 0);
    if (rc != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: mnc2_send(b) returned %d\n", rc);
        goto cleanup;
    }

    printf("[send] a -> PDM offset %d (dmaid=0x%02x, trigger)\n",
           (int)OFFSET_A, DMAID_TRIGGER);
    memcpy(sendbuf, orig_a, ELEM_BYTES);
    rc = mnc2_send(dev, sendbuf, OFFSET_A, ELEM_BYTES, DMAID_TRIGGER);
    if (rc != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: mnc2_send(a) returned %d\n", rc);
        goto cleanup;
    }

    printf("[exec] double-vecadd kernel (3D: %dx%dx%d)\n", NI, NJ, NK);
    mnc2_kernel_t kernel = mnc2_load_kernel(dev, "_build/vecadd.idma.dat");
    if (kernel == NULL) {
        fprintf(stderr, "FAIL: mnc2_load_kernel\n");
        rc = 1;
        goto cleanup;
    }
    rc = mnc2_exec_kernel(kernel);
    mnc2_free_kernel(kernel);
    if (rc != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: mnc2_exec_kernel returned %d\n", rc);
        goto cleanup;
    }

    printf("[recv] c <- PDM offset %llu\n", (unsigned long long)OFFSET_C);
    memset(recvbuf, 0, ELEM_BYTES);
    rc = mnc2_recv(dev, recvbuf, OFFSET_C, ELEM_BYTES, WD_RECV);
    if (rc != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: mnc2_recv(c) returned %d\n", rc);
        goto cleanup;
    }

    {
        double *rp = (double *)recvbuf;
        int errors = 0;

        for (int i = 0; i < ELEM_COUNT; i++) {
            double expected = orig_a[i] + orig_b[i];
            if (rp[i] != expected) {
                if (errors < 5)
                    fprintf(stderr, "  MISMATCH [%d]: got=%g expected=%g\n",
                            i, rp[i], expected);
                errors++;
            }
        }

        if (errors > 0) {
            fprintf(stderr, "  FAIL: %d mismatches\n", errors);
            rc = 1;
        } else {
            printf("  PASS: c[i][j][k] == a[i][j][k] + b[i][j][k] for all %d elements\n",
                   ELEM_COUNT);
            rc = 0;
        }
    }

cleanup:
    mnc2_free_host_buffer(dev, sendbuf, ELEM_BYTES);
    mnc2_free_host_buffer(dev, recvbuf, ELEM_BYTES);
    mnc2_close(dev);
    free(orig_a);
    free(orig_b);
    return (rc != 0) ? 1 : 0;
}
