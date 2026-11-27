/* test_bitonic-sort.c -- バイトニックソート（4096 個、double）の E2E test (emu:lib)
 *
 * 入力を kernel で昇順に並べ、結果が、入力を昇順に並べたもの（qsort）とビット列まで一致することを確かめる。
 * 入力は、有限で 0 でない f64（正と負の両方）。kernel の比較は 0 を同一視し、NaN の扱いが特別なので、0 と NaN は入れない。
 *
 * PDM layout (bitonic-sort.param):
 *   ids (@identify): PDM[0..4095]      -- ホストが [0..4095] を送る
 *   a  (slot 8):     PDM[4096..8191]
 *   out (slot 16):   PDM[8192..12287]
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
#define OFFSET_OUT  (8192ULL * 8)
#define TAG_IDS     0x10
#define TAG_A       0x11
#define RECV_TAG    0x1e

/* [-2^32, 2^32) の、有限で 0 でない疑似乱数（f64 で誤差なく表せる） */
static double src_value(int i)
{
    uint64_t x = (uint64_t)(i + 1) * 6364136223846793005ULL + 1442695040888963407ULL;
    x ^= x >> 29;
    x *= 0xBF58476D1CE4E5B9ULL;
    double v = (double)((int64_t)(x >> 11) - (1LL << 52)) / 1048576.0;
    return v == 0.0 ? 0.5 : v;
}

static int cmp_f64(const void *x, const void *y)
{
    double a = *(const double *)x, b = *(const double *)y;
    return (a > b) - (a < b);
}

int main(void)
{
    printf("=== 24-bitonic-sort: バイトニックソート（4096 個、double）===\n\n");

    mnc2_device_t dev = mnc2_open(0);
    if (dev == NULL) { fprintf(stderr, "FAIL: mnc2_open returned NULL\n"); return 1; }
    printf("24-bitonic-sort backend: %s\n", mnc2_get_backend_name(dev));

    int rc = 0;
    void *idbuf = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    void *sendbuf = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    void *recvbuf = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    double input[ELEM_COUNT], sorted[ELEM_COUNT];
    if (!idbuf || !sendbuf || !recvbuf) { fprintf(stderr, "FAIL: alloc\n"); rc = 1; goto cleanup; }

    uint64_t *ids = (uint64_t *)idbuf, *send_u64 = (uint64_t *)sendbuf;
    int negatives = 0;
    for (int i = 0; i < ELEM_COUNT; i++) {
        ids[i] = (uint64_t)i;
        input[i] = src_value(i);
        if (input[i] < 0) negatives++;
        memcpy(&send_u64[i], &input[i], sizeof(double));
    }
    memcpy(sorted, input, sizeof sorted);
    qsort(sorted, ELEM_COUNT, sizeof sorted[0], cmp_f64);

    if (mnc2_send(dev, idbuf, OFFSET_IDS, ELEM_BYTES, TAG_IDS) != MNC2_SUCCESS ||
        mnc2_send(dev, sendbuf, OFFSET_A, ELEM_BYTES, TAG_A) != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: send\n"); rc = 1; goto cleanup;
    }

    mnc2_kernel_t kernel = mnc2_load_kernel(dev, "_build/bitonic-sort.idma.dat");
    if (!kernel) { fprintf(stderr, "FAIL: load_kernel\n"); rc = 1; goto cleanup; }
    int krc = mnc2_exec_kernel(kernel);
    mnc2_free_kernel(kernel);
    if (krc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: exec\n"); rc = 1; goto cleanup; }

    memset(recvbuf, 0, ELEM_BYTES);
    if (mnc2_recv(dev, recvbuf, OFFSET_OUT, ELEM_BYTES, RECV_TAG) != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: recv\n"); rc = 1; goto cleanup;
    }

    uint64_t *rp = (uint64_t *)recvbuf;
    int errors = 0;
    for (int i = 0; i < ELEM_COUNT; i++) {
        double got; memcpy(&got, &rp[i], sizeof(got));
        if (memcmp(&got, &sorted[i], sizeof(double)) != 0) {
            if (errors < 5)
                fprintf(stderr, "  MISMATCH out[%d]: got=%.17g expected=%.17g\n", i, got, sorted[i]);
            errors++;
        }
    }
    if (errors > 0) { fprintf(stderr, "FAIL: %d/%d mismatches\n", errors, ELEM_COUNT); rc = 1; }
    else            { printf("PASS %d/%d（全体が昇順。入力の負の値 %d 個）\n", ELEM_COUNT, ELEM_COUNT, negatives); rc = 0; }

cleanup:
    if (idbuf)   mnc2_free_host_buffer(dev, idbuf, ELEM_BYTES);
    if (sendbuf) mnc2_free_host_buffer(dev, sendbuf, ELEM_BYTES);
    if (recvbuf) mnc2_free_host_buffer(dev, recvbuf, ELEM_BYTES);
    mnc2_close(dev);
    return (rc != 0) ? 1 : 0;
}
