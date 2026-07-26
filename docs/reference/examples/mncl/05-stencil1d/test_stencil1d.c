/* test_stencil1d.c -- stencil1d (neighbor) E2E test (emu:lib)
 *
 * Kernel: c[i] = left + self + right
 *   where left = neighbor(a, -1), right = neighbor(a, +1)
 *
 * Boundary: :clamp — 端の PE は端の値を返す
 *   PE[0]:    left = a[0] (self)
 *   PE[4095]: right = a[4095] (self)
 *
 * Usage:
 *   ./test_stencil1d                 -- default: a[i] = i + 1
 *   ./test_stencil1d INDEX VALUE     -- override a[INDEX] = VALUE, show detail
 *
 * PDM layout (stencil1d.param):
 *   buf_a: PDM[0..4095]     -- byte offset 0      (slot 8)
 *   buf_c: PDM[4096..8191]  -- byte offset 32768   (slot 16)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "mnc2.h"

#include <stdint.h>

#define N           4096
#define ELEM_SIZE   sizeof(double)
#define BUF_BYTES   (N * ELEM_SIZE)
#define BF_BYTES    (N * sizeof(uint64_t))   /* boundary flags: 1 LW per PE */

#define OFFSET_A    (0ULL * 8)       /* slot 8:  PDM word 0    */
#define OFFSET_C    (4096ULL * 8)    /* slot 16: PDM word 4096 */
#define OFFSET_BF   (8192ULL * 8)    /* bf1:     PDM word 8192 */
#define SEND_WAIT_TAG 0x10
#define CONTEXT     3                /* display +-3 elements around index */

#define BF_DATA_PATH "_build/collected_flags.bin"

/* :clamp boundary — 端の PE は自身の値を返す */
static double get_left(const double *a, int i)  { return (i > 0)     ? a[i-1] : a[i]; }
static double get_right(const double *a, int i) { return (i < N - 1) ? a[i+1] : a[i]; }

static void show_detail(const double *input, const double *expected,
                        const double *result, int center)
{
    int lo = center - CONTEXT;
    int hi = center + CONTEXT;
    if (lo < 0)     lo = 0;
    if (hi > N - 1) hi = N - 1;

    printf("\n--- input a[%d..%d] ---\n", lo, hi);
    for (int i = lo; i <= hi; i++)
        printf("  a[%4d] = %12.1f%s\n", i, input[i], (i == center) ? "  <--" : "");

    printf("\n--- expected (left + self + right) ---\n");
    for (int i = lo; i <= hi; i++)
        printf("  e[%4d] = %12.1f  (%.1f + %.1f + %.1f)%s\n",
               i, expected[i],
               get_left(input, i), input[i], get_right(input, i),
               (i == center) ? "  <--" : "");

    printf("\n--- actual result ---\n");
    for (int i = lo; i <= hi; i++) {
        const char *tag = "";
        if (i == center) tag = "  <--";
        if (fabs(result[i] - expected[i]) > 1e-10)
            printf("  c[%4d] = %12.1f  MISMATCH (expected %.1f)%s\n",
                   i, result[i], expected[i], tag);
        else
            printf("  c[%4d] = %12.1f  OK%s\n", i, result[i], tag);
    }
    printf("\n");
}

int main(int argc, char *argv[])
{
    int override_index = -1;
    double override_value = 0.0;

    if (argc == 3) {
        override_index = atoi(argv[1]);
        override_value = atof(argv[2]);
        if (override_index < 0 || override_index >= N) {
            fprintf(stderr, "ERROR: index %d out of range [0, %d)\n",
                    override_index, N);
            return 1;
        }
        printf("=== stencil1d: c[i] = left + self + right (4096 PE) ===\n");
        printf("=== override: a[%d] = %.1f ===\n\n", override_index, override_value);
    } else {
        printf("=== stencil1d: c[i] = left + self + right (4096 PE) ===\n\n");
    }

    mnc2_device_t dev = mnc2_open(0);
    if (!dev) {
        fprintf(stderr, "FAIL: mnc2_open returned NULL\n");
        return 1;
    }
    printf("05-stencil1d backend: %s\n", mnc2_get_backend_name(dev));

    void *sendbuf = mnc2_alloc_host_buffer(dev, BUF_BYTES);
    void *recvbuf = mnc2_alloc_host_buffer(dev, BUF_BYTES);
    void *bfbuf   = mnc2_alloc_host_buffer(dev, BF_BYTES);
    if (!sendbuf || !recvbuf || !bfbuf) {
        fprintf(stderr, "FAIL: mnc2_alloc_host_buffer\n");
        mnc2_close(dev);
        return 1;
    }

    /* input: a[i] = i + 1, with optional override */
    double *orig = (double *)malloc(BUF_BYTES);
    for (int i = 0; i < N; i++)
        orig[i] = (double)(i + 1);

    if (override_index >= 0) {
        printf("[override] a[%d]: %.1f -> %.1f\n\n",
               override_index, orig[override_index], override_value);
        orig[override_index] = override_value;
    }

    /* golden data */
    double *expected = (double *)malloc(BUF_BYTES);
    for (int i = 0; i < N; i++)
        expected[i] = get_left(orig, i) + orig[i] + get_right(orig, i);

    int rc;

    /* send boundary flags (non-trigger, before input) */
    {
        FILE *fp = fopen(BF_DATA_PATH, "rb");
        if (!fp) {
            fprintf(stderr, "FAIL: cannot open %s\n", BF_DATA_PATH);
            rc = 1;
            goto cleanup;
        }
        size_t nr = fread(bfbuf, 1, BF_BYTES, fp);
        fclose(fp);
        if (nr != BF_BYTES) {
            fprintf(stderr, "FAIL: %s: read %zu bytes, expected %zu\n",
                    BF_DATA_PATH, nr, (size_t)BF_BYTES);
            rc = 1;
            goto cleanup;
        }
    }
    printf("[send] boundary_flags (%s) -> PDM offset %llu (non-trigger)\n",
           BF_DATA_PATH, (unsigned long long)OFFSET_BF);
    rc = mnc2_send(dev, bfbuf, OFFSET_BF, BF_BYTES, 0);
    if (rc != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: mnc2_send(boundary_flags) returned %d\n", rc);
        goto cleanup;
    }

    /* send a (trigger) */
    printf("[send] a -> PDM offset %d (dmaid=0x%02x, trigger)\n",
           (int)OFFSET_A, SEND_WAIT_TAG);
    memcpy(sendbuf, orig, BUF_BYTES);
    rc = mnc2_send(dev, sendbuf, OFFSET_A, BUF_BYTES, SEND_WAIT_TAG);
    if (rc != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: mnc2_send(a) returned %d\n", rc);
        goto cleanup;
    }

    /* exec kernel */
    printf("[exec] stencil1d kernel\n");
    mnc2_kernel_t kernel = mnc2_load_kernel(dev, "_build/stencil1d.idma.dat");
    if (!kernel) {
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

    /* recv c */
    printf("[recv] c <- PDM offset %llu\n", (unsigned long long)OFFSET_C);
    memset(recvbuf, 0, BUF_BYTES);
    rc = mnc2_recv(dev, recvbuf, OFFSET_C, BUF_BYTES, 0x1e);
    if (rc != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: mnc2_recv(c) returned %d\n", rc);
        goto cleanup;
    }

    /* verify */
    {
        double *rp = (double *)recvbuf;
        int pass = 0, fail = 0;

        for (int i = 0; i < N; i++) {
            if (fabs(rp[i] - expected[i]) < 1e-10) {
                pass++;
            } else {
                if (fail < 10)
                    fprintf(stderr, "  MISMATCH [%d]: got=%.1f expected=%.1f"
                            " (left=%.1f self=%.1f right=%.1f)\n",
                            i, rp[i], expected[i],
                            get_left(orig, i), orig[i], get_right(orig, i));
                fail++;
            }
        }

        printf("\nRESULT: PASS %d/%d", pass, N);
        if (fail > 0) printf(", FAIL %d", fail);
        printf("\n");
        rc = (fail > 0) ? 1 : 0;

        /* show detail around override index */
        if (override_index >= 0)
            show_detail(orig, expected, rp, override_index);
    }

cleanup:
    mnc2_free_host_buffer(dev, sendbuf, BUF_BYTES);
    mnc2_free_host_buffer(dev, recvbuf, BUF_BYTES);
    mnc2_free_host_buffer(dev, bfbuf, BF_BYTES);
    mnc2_close(dev);
    free(orig);
    free(expected);
    return (rc != 0) ? 1 : 0;
}
