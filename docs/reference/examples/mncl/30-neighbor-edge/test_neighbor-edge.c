/* test_neighbor-edge.c -- 30-neighbor-edge: 隣の PE の値を受け取る関数の、端の扱い
 *
 * a[i] = i + 1（i は PE の番号。4096 個）を配り、次の 4 つを計算して、端の PE を含めた 4096 個をすべて比べる。
 *
 *   clamp[i]    = L + a[i] + R。L、R は隣の値。端の PE（i = 0 の L、i = 4095 の R）は自分の値 a[i]
 *   periodic[i] = L + a[i] + R。端の PE は反対の端の値（i = 0 の L は a[4095]、i = 4095 の R は a[0]）
 *   fixed[i]    = L + a[i] + R。端の PE は 0（固定の境界値）
 *   up[j]       = 32 x 128 の格子（j = y * 32 + x）の上隣 a[j - 32]。y = 0 の行は y = 127 の行の値（周期境界）
 *
 * PDM の配置（neighbor-edge.param）:
 *   a: PDM[0..4095]（slot 8）、clamp / periodic / fixed / up: PDM[4096 * k ..]（slot 16 / 24 / 32 / 40）、
 *   boundary_flags: PDM[20480..]（load_neighbor 系が使う。ホストが exec の前に送る）
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mnc2.h"

#define N           4096
#define NX          32
#define BUF_BYTES   (N * sizeof(double))
#define BF_BYTES    (N * sizeof(uint64_t))
#define OFFSET_A    (0ULL * 8)
#define OFFSET_OUT(k) ((4096ULL * (k)) * 8)    /* k = 1〜4 */
#define OFFSET_BF   (20480ULL * 8)
#define SEND_WAIT_TAG 0x10
#define RECV_TAG(k) (0x1f - (k))               /* k = 1〜4 → 0x1e〜0x1b */
#define BF_DATA_PATH "_build/collected_flags.bin"

static const char *NAMES[] = { "", "clamp", "periodic", "fixed", "up" };

static double a_of(int i) { return (double)(i + 1); }

static double expected(int k, int i)
{
    switch (k) {
    case 1: return (i > 0 ? a_of(i - 1) : a_of(i)) + a_of(i) + (i < N - 1 ? a_of(i + 1) : a_of(i));
    case 2: return a_of((i + N - 1) % N) + a_of(i) + a_of((i + 1) % N);
    case 3: return (i > 0 ? a_of(i - 1) : 0.0) + a_of(i) + (i < N - 1 ? a_of(i + 1) : 0.0);
    default: return a_of((i + N - NX) % N);
    }
}

int main(void)
{
    printf("=== 30-neighbor-edge: clamp / periodic / fixed boundary (4096 PE, double) ===\n\n");

    mnc2_device_t dev = mnc2_open(0);
    if (!dev) { fprintf(stderr, "FAIL: mnc2_open returned NULL\n"); return 1; }
    printf("30-neighbor-edge backend: %s\n", mnc2_get_backend_name(dev));

    int rc = 0;
    void *sendbuf = mnc2_alloc_host_buffer(dev, BUF_BYTES);
    void *recvbuf = mnc2_alloc_host_buffer(dev, BUF_BYTES);
    void *bfbuf   = mnc2_alloc_host_buffer(dev, BF_BYTES);
    if (!sendbuf || !recvbuf || !bfbuf) { fprintf(stderr, "FAIL: mnc2_alloc_host_buffer\n"); rc = 1; goto cleanup; }

    /* boundary flags（タグ 0。入力より先に送る） */
    {
        FILE *fp = fopen(BF_DATA_PATH, "rb");
        if (!fp) { fprintf(stderr, "FAIL: cannot open %s\n", BF_DATA_PATH); rc = 1; goto cleanup; }
        size_t nr = fread(bfbuf, 1, BF_BYTES, fp);
        fclose(fp);
        if (nr != BF_BYTES) { fprintf(stderr, "FAIL: %s: read %zu bytes\n", BF_DATA_PATH, nr); rc = 1; goto cleanup; }
    }
    if (mnc2_send(dev, bfbuf, OFFSET_BF, BF_BYTES, 0) != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: mnc2_send(boundary_flags)\n"); rc = 1; goto cleanup;
    }

    double *in = (double *)sendbuf;
    for (int i = 0; i < N; i++) in[i] = a_of(i);
    if (mnc2_send(dev, sendbuf, OFFSET_A, BUF_BYTES, SEND_WAIT_TAG) != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: mnc2_send(a)\n"); rc = 1; goto cleanup;
    }

    mnc2_kernel_t kernel = mnc2_load_kernel(dev, "_build/neighbor-edge.idma.dat");
    if (!kernel) { fprintf(stderr, "FAIL: mnc2_load_kernel\n"); rc = 1; goto cleanup; }
    int krc = mnc2_exec_kernel(kernel);
    mnc2_free_kernel(kernel);
    if (krc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: mnc2_exec_kernel returned %d\n", krc); rc = 1; goto cleanup; }

    int fail = 0;
    for (int k = 1; k <= 4; k++) {
        memset(recvbuf, 0, BUF_BYTES);
        if (mnc2_recv(dev, recvbuf, OFFSET_OUT(k), BUF_BYTES, RECV_TAG(k)) != MNC2_SUCCESS) {
            fprintf(stderr, "FAIL: mnc2_recv(%s)\n", NAMES[k]); rc = 1; goto cleanup;
        }
        const double *rp = (const double *)recvbuf;
        int failk = 0;
        for (int i = 0; i < N; i++) {
            if (rp[i] != expected(k, i)) {
                if (failk < 5)
                    fprintf(stderr, "  MISMATCH %s[%d]: got=%.1f expected=%.1f\n", NAMES[k], i, rp[i], expected(k, i));
                failk++;
            }
        }
        printf("  %-8s: %d/%d match（端の PE: [0] = %.1f、[%d] = %.1f）\n", NAMES[k], N - failk, N, rp[0], N - 1, rp[N - 1]);
        fail += failk;
    }

    if (fail > 0) {
        fprintf(stderr, "FAIL: %d mismatches\n", fail); rc = 1;
    } else {
        printf("\nPASS 4 x %d/%d\n", N, N);
    }

cleanup:
    if (sendbuf) mnc2_free_host_buffer(dev, sendbuf, BUF_BYTES);
    if (recvbuf) mnc2_free_host_buffer(dev, recvbuf, BUF_BYTES);
    if (bfbuf)   mnc2_free_host_buffer(dev, bfbuf, BF_BYTES);
    mnc2_close(dev);
    return rc ? 1 : 0;
}
