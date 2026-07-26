/* test_vecadd-i64.c -- i64 整数 vecadd の E2E テスト (emu:lib)
 *
 * c[i] = a[i] + b[i] on 4096 PEs (i64 整数加算 = XREG の ladd)。
 *
 * PDM layout (vecadd-i64.param、01-vecadd と同一。i64 も 8 byte):
 *   buf1 (a): PDM word 0        -- byte offset 0
 *   buf2 (b): PDM word 4096     -- byte offset 32768
 *   buf3 (c): PDM word 131072   -- byte offset 1048576
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "mnc2.h"

#define ELEM_COUNT  4096
#define ELEM_BYTES  (ELEM_COUNT * sizeof(int64_t))
#define OFFSET_A    0                       /* slot 8:  PDM word 0      */
#define OFFSET_B    (4096ULL * 8)           /* slot 16: PDM word 4096   */
#define OFFSET_C    (131072ULL * 8)         /* slot 24: PDM word 131072 */
#define SEND_WAIT_TAG 0x10
#define RECV_TAG      0x1e   /* vsmlink + asm3 が割り当てる collect の done tag */

int main(void)
{
    printf("=== vecadd-i64: c[i] = a[i] + b[i] (i64, 4096 PE) ===\n\n");

    mnc2_device_t dev = mnc2_open(0);
    if (dev == NULL) {
        fprintf(stderr, "FAIL: mnc2_open returned NULL\n");
        return 1;
    }
    printf("12-vecadd-i64 backend: %s\n", mnc2_get_backend_name(dev));

    void *sendbuf = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    void *recvbuf = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    if (sendbuf == NULL || recvbuf == NULL) {
        fprintf(stderr, "FAIL: mnc2_alloc_host_buffer\n");
        mnc2_close(dev);
        return 1;
    }

    int64_t *orig_a = (int64_t *)malloc(ELEM_BYTES);
    int64_t *orig_b = (int64_t *)malloc(ELEM_BYTES);
    if (orig_a == NULL || orig_b == NULL) {
        fprintf(stderr, "FAIL: malloc\n");
        mnc2_free_host_buffer(dev, sendbuf, ELEM_BYTES);
        mnc2_free_host_buffer(dev, recvbuf, ELEM_BYTES);
        mnc2_close(dev);
        free(orig_a); free(orig_b);
        return 1;
    }
    /* 32-bit を越える値も入れて i64 (両バンク 64-bit) を実際に使う */
    for (int i = 0; i < ELEM_COUNT; i++) {
        orig_a[i] = (int64_t)(i + 1) * 1000000007LL;
        orig_b[i] = (int64_t)(i + 1) * -3LL + 0x100000000LL;
    }

    int rc;

    /* send b (non-trigger) */
    printf("[send] b -> PDM offset %llu (dmaid=0)\n", (unsigned long long)OFFSET_B);
    memcpy(sendbuf, orig_b, ELEM_BYTES);
    rc = mnc2_send(dev, sendbuf, OFFSET_B, ELEM_BYTES, 0);
    if (rc != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: mnc2_send(b) returned %d\n", rc);
        goto cleanup;
    }

    /* send a (trigger) */
    printf("[send] a -> PDM offset %d (dmaid=0x%02x, trigger)\n",
           (int)OFFSET_A, SEND_WAIT_TAG);
    memcpy(sendbuf, orig_a, ELEM_BYTES);
    rc = mnc2_send(dev, sendbuf, OFFSET_A, ELEM_BYTES, SEND_WAIT_TAG);
    if (rc != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: mnc2_send(a) returned %d\n", rc);
        goto cleanup;
    }

    /* exec kernel */
    printf("[exec] i64 vecadd kernel\n");
    mnc2_kernel_t kernel = mnc2_load_kernel(dev, "_build/vecadd-i64.idma.dat");
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

    /* recv c */
    printf("[recv] c <- PDM offset %llu\n", (unsigned long long)OFFSET_C);
    memset(recvbuf, 0, ELEM_BYTES);
    rc = mnc2_recv(dev, recvbuf, OFFSET_C, ELEM_BYTES, RECV_TAG);
    if (rc != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: mnc2_recv(c) returned %d\n", rc);
        goto cleanup;
    }

    /* verify: c[i] == a[i] + b[i] */
    {
        int64_t *rp = (int64_t *)recvbuf;
        int errors = 0;

        for (int i = 0; i < ELEM_COUNT; i++) {
            int64_t expected = orig_a[i] + orig_b[i];
            if (rp[i] != expected) {
                if (errors < 5)
                    fprintf(stderr, "  MISMATCH c[%d]: got=%lld expected=%lld\n",
                            i, (long long)rp[i], (long long)expected);
                errors++;
            }
        }

        if (errors > 0) {
            fprintf(stderr, "  FAIL: %d mismatches\n", errors);
            rc = 1;
        } else {
            printf("  PASS: c[i] == a[i] + b[i] for all %d i64 elements\n", ELEM_COUNT);
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
