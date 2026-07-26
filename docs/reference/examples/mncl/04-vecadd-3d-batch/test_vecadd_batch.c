/* test_vecadd_batch.c -- double-vecadd batch E2E test (emu:lib)
 *
 * c[i] = a[i] + b[i] for 4096*N elements, processed in N rounds.
 * Each round uses the same kernel (same PDM addresses).
 * The host shifts its buffer pointer by 4096 elements per round.
 *
 * Usage: ./test_vecadd_batch [N]   (default: N=2)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mnc2.h"

#define PE_COUNT    4096
#define ROUND_BYTES (PE_COUNT * sizeof(double))
#define OFFSET_A    0                       /* slot 8:  PDM word 0      */
#define OFFSET_B    (4096ULL * 8)           /* slot 16: PDM word 4096   */
#define OFFSET_C    (131072ULL * 8)         /* slot 24: PDM word 131072 */
#define SEND_WAIT_TAG 0x10
#define RECV_TAG      0x1e   /* vsmlink + asm3 が割り当てる collect の done tag */

int main(int argc, char **argv)
{
    int nrounds = 2;
    if (argc >= 2) {
        nrounds = atoi(argv[1]);
        if (nrounds < 1) nrounds = 1;
    }
    int total = PE_COUNT * nrounds;

    printf("=== vecadd batch: c[i] = a[i] + b[i] (%d elements, %d rounds) ===\n\n",
           total, nrounds);

    mnc2_device_t dev = mnc2_open(0);
    if (dev == NULL) {
        fprintf(stderr, "FAIL: mnc2_open returned NULL\n");
        return 1;
    }
    printf("04-vecadd-3d-batch backend: %s\n", mnc2_get_backend_name(dev));

    void *sendbuf = mnc2_alloc_host_buffer(dev, ROUND_BYTES);
    void *recvbuf = mnc2_alloc_host_buffer(dev, ROUND_BYTES);
    if (sendbuf == NULL || recvbuf == NULL) {
        fprintf(stderr, "FAIL: mnc2_alloc_host_buffer\n");
        mnc2_close(dev);
        return 1;
    }

    double *orig_a = (double *)malloc(total * sizeof(double));
    double *orig_b = (double *)malloc(total * sizeof(double));
    double *result = (double *)malloc(total * sizeof(double));
    if (orig_a == NULL || orig_b == NULL || result == NULL) {
        fprintf(stderr, "FAIL: malloc\n");
        mnc2_free_host_buffer(dev, sendbuf, ROUND_BYTES);
        mnc2_free_host_buffer(dev, recvbuf, ROUND_BYTES);
        mnc2_close(dev);
        free(orig_a); free(orig_b); free(result);
        return 1;
    }
    for (int i = 0; i < total; i++) {
        orig_a[i] = (double)(i + 1);
        orig_b[i] = (double)(i + 1) * 10.0;
    }

    mnc2_kernel_t kernel = mnc2_load_kernel(dev, "_build/vecadd.idma.dat");
    if (kernel == NULL) {
        fprintf(stderr, "FAIL: mnc2_load_kernel\n");
        mnc2_free_host_buffer(dev, sendbuf, ROUND_BYTES);
        mnc2_free_host_buffer(dev, recvbuf, ROUND_BYTES);
        mnc2_close(dev);
        free(orig_a); free(orig_b); free(result);
        return 1;
    }

    int rc = 0;

    for (int r = 0; r < nrounds; r++) {
        int base = r * PE_COUNT;
        printf("[round %d/%d] elements %d..%d\n", r + 1, nrounds, base, base + PE_COUNT - 1);

        /* send b */
        memcpy(sendbuf, &orig_b[base], ROUND_BYTES);
        rc = mnc2_send(dev, sendbuf, OFFSET_B, ROUND_BYTES, 0);
        if (rc != MNC2_SUCCESS) {
            fprintf(stderr, "FAIL: mnc2_send(b) round %d returned %d\n", r, rc);
            goto cleanup;
        }

        /* send a (trigger) */
        memcpy(sendbuf, &orig_a[base], ROUND_BYTES);
        rc = mnc2_send(dev, sendbuf, OFFSET_A, ROUND_BYTES, SEND_WAIT_TAG);
        if (rc != MNC2_SUCCESS) {
            fprintf(stderr, "FAIL: mnc2_send(a) round %d returned %d\n", r, rc);
            goto cleanup;
        }

        /* exec kernel */
        rc = mnc2_exec_kernel(kernel);
        if (rc != MNC2_SUCCESS) {
            fprintf(stderr, "FAIL: mnc2_exec_kernel round %d returned %d\n", r, rc);
            goto cleanup;
        }

        /* recv c */
        memset(recvbuf, 0, ROUND_BYTES);
        rc = mnc2_recv(dev, recvbuf, OFFSET_C, ROUND_BYTES, RECV_TAG);
        if (rc != MNC2_SUCCESS) {
            fprintf(stderr, "FAIL: mnc2_recv(c) round %d returned %d\n", r, rc);
            goto cleanup;
        }

        memcpy(&result[base], recvbuf, ROUND_BYTES);
    }

    /* verify all elements */
    {
        int errors = 0;
        for (int i = 0; i < total; i++) {
            double expected = orig_a[i] + orig_b[i];
            if (result[i] != expected) {
                if (errors < 5)
                    fprintf(stderr, "  MISMATCH c[%d]: got=%g expected=%g\n",
                            i, result[i], expected);
                errors++;
            }
        }

        if (errors > 0) {
            fprintf(stderr, "  FAIL: %d/%d mismatches\n", errors, total);
            rc = 1;
        } else {
            printf("  PASS: c[i] == a[i] + b[i] for all %d elements (%d rounds)\n",
                   total, nrounds);
            rc = 0;
        }
    }

cleanup:
    mnc2_free_kernel(kernel);
    mnc2_free_host_buffer(dev, sendbuf, ROUND_BYTES);
    mnc2_free_host_buffer(dev, recvbuf, ROUND_BYTES);
    mnc2_close(dev);
    free(orig_a);
    free(orig_b);
    free(result);
    return (rc != 0) ? 1 : 0;
}
