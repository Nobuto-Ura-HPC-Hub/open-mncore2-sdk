/* test_hypercube-neighbor.c -- get_hypercube_neighbor の example の E2E test (emu:lib)
 *
 *   c[j] = a[j] + 10 * a[j XOR 1] + 100 * a[j XOR 1024]     (a[j] = j + 1、4096 PE)
 *
 * PDM layout (hypercube-neighbor.param):
 *   ids (@identify): PDM[0..4095]      -- ホストが [0..4095] を送る
 *   a  (slot 8):     PDM[4096..8191]
 *   c  (slot 16):    PDM[8192..12287]
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mnc2.h"

#define ELEM_COUNT  4096
#define ELEM_BYTES  (ELEM_COUNT * sizeof(uint64_t))
#define OFFSET_IDS  0ULL
#define OFFSET_A    (4096ULL * 8)
#define OFFSET_C    (8192ULL * 8)
#define TAG_IDS     0x10
#define TAG_A       0x11
#define RECV_TAG    0x1e

int main(void)
{
    printf("=== 22-hypercube-neighbor: c[j] = a[j] + 10*a[j^1] + 100*a[j^1024] (4096 PE) ===\n\n");

    mnc2_device_t dev = mnc2_open(0);
    if (dev == NULL) { fprintf(stderr, "FAIL: mnc2_open returned NULL\n"); return 1; }
    printf("22-hypercube-neighbor backend: %s\n", mnc2_get_backend_name(dev));

    int rc = 0;
    void *idbuf = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    void *sendbuf = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    void *recvbuf = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    double orig_a[ELEM_COUNT];
    if (!idbuf || !sendbuf || !recvbuf) { fprintf(stderr, "FAIL: alloc\n"); rc = 1; goto cleanup; }

    uint64_t *ids = (uint64_t *)idbuf, *send_u64 = (uint64_t *)sendbuf;
    for (int i = 0; i < ELEM_COUNT; i++) {
        ids[i] = (uint64_t)i;
        orig_a[i] = (double)(i + 1);
        memcpy(&send_u64[i], &orig_a[i], sizeof(double));
    }

    if (mnc2_send(dev, idbuf, OFFSET_IDS, ELEM_BYTES, TAG_IDS) != MNC2_SUCCESS ||
        mnc2_send(dev, sendbuf, OFFSET_A, ELEM_BYTES, TAG_A) != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: send\n"); rc = 1; goto cleanup;
    }

    mnc2_kernel_t kernel = mnc2_load_kernel(dev, "_build/hypercube-neighbor.idma.dat");
    if (!kernel) { fprintf(stderr, "FAIL: load_kernel\n"); rc = 1; goto cleanup; }
    int krc = mnc2_exec_kernel(kernel);
    mnc2_free_kernel(kernel);
    if (krc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: exec\n"); rc = 1; goto cleanup; }

    memset(recvbuf, 0, ELEM_BYTES);
    if (mnc2_recv(dev, recvbuf, OFFSET_C, ELEM_BYTES, RECV_TAG) != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: recv\n"); rc = 1; goto cleanup;
    }

    uint64_t *rp = (uint64_t *)recvbuf;
    int errors = 0;
    for (int j = 0; j < ELEM_COUNT; j++) {
        double expected = orig_a[j] + orig_a[j ^ 1] * 10.0 + orig_a[j ^ 1024] * 100.0;
        double got; memcpy(&got, &rp[j], sizeof(got));
        if (got != expected) {
            if (errors < 5)
                fprintf(stderr, "  MISMATCH c[%d]: got=%.9g expected=%.9g\n", j, got, expected);
            errors++;
        }
    }
    if (errors > 0) { fprintf(stderr, "FAIL: %d/%d mismatches\n", errors, ELEM_COUNT); rc = 1; }
    else            { printf("PASS %d/%d\n", ELEM_COUNT, ELEM_COUNT); rc = 0; }

cleanup:
    if (idbuf)   mnc2_free_host_buffer(dev, idbuf, ELEM_BYTES);
    if (sendbuf) mnc2_free_host_buffer(dev, sendbuf, ELEM_BYTES);
    if (recvbuf) mnc2_free_host_buffer(dev, recvbuf, ELEM_BYTES);
    mnc2_close(dev);
    return (rc != 0) ? 1 : 0;
}
