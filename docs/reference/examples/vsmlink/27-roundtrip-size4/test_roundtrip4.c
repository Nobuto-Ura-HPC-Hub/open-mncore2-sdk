/* test_roundtrip4.c — @distribute と @collect の往復検証 (成分数は引数で与える)
 *
 * 検証したいのは PDM 上の並びが両者で一致していることである。
 * 計算はしないので、入力と出力が全要素で一致すれば往復が成立している。
 *
 * PDM の並び (size 4 の場合):
 *   [成分 0 を 4096 PE 分][成分 1][成分 2][成分 3]
 *
 * 成分ごとに違う値域を使うので、並びが入れ替わると必ず不一致になる。
 * 成分 k の PE p には 1000*(k+1) + p を入れる。
 *
 * PDM レイアウト (roundtrip4.param):
 *   入力: PDM[0..16384]        — byte offset 0
 *   出力: PDM[131072..147456]  — byte offset 1048576
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mnc2.h"

#define PE_COUNT    4096
#define MAX_SIZE_N  4
#define MAX_ELEM    (PE_COUNT * MAX_SIZE_N)
#define OFFSET_IN   0                        /* PDM word 0      → byte 0       */
#define OFFSET_OUT  (131072ULL * 8)          /* PDM word 131072 → byte 1048576 */
#define DMAID_SEND  0x10                     /* カーネル内 wait i10 のトリガー */
#define WD_RECV     0x1e                     /* .param recv_wait_tag と対応     */

/* 成分 k、PE p に入れる値。成分ごとに値域を分けて並びの入れ替わりを検出する。 */
static double value_of(int k, int p)
{
    return (double)(1000 * (k + 1) + p);
}

int main(int argc, char **argv)
{
    /* argv[1]: _vsm の size と一致させる成分数、argv[2]: カーネルのパス */
    if (argc != 3) {
        fprintf(stderr, "usage: %s <size> <kernel.idma.dat>\n", argv[0]);
        return 1;
    }
    const int SIZE_N = atoi(argv[1]);
    const char *kernel_path = argv[2];
    if (SIZE_N < 1 || SIZE_N > MAX_SIZE_N) {
        fprintf(stderr, "size は 1 から %d でなければならない\n", MAX_SIZE_N);
        return 1;
    }
    const int    ELEM_COUNT = PE_COUNT * SIZE_N;
    const size_t ELEM_BYTES = (size_t)ELEM_COUNT * sizeof(double);

    printf("=== roundtrip: size %d の @distribute と @collect の往復 ===\n\n", SIZE_N);

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
    memset(recvbuf, 0, ELEM_BYTES);

    double *dp = (double *)sendbuf;
    for (int k = 0; k < SIZE_N; k++)
        for (int p = 0; p < PE_COUNT; p++)
            dp[k * PE_COUNT + p] = value_of(k, p);

    printf("  成分ごとの先頭: ");
    for (int k = 0; k < SIZE_N; k++)
        printf("%.0f ", dp[k * PE_COUNT]);
    printf("\n");

    int rc = mnc2_send(dev, sendbuf, OFFSET_IN, ELEM_BYTES, DMAID_SEND);
    if (rc != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: mnc2_send returned %d\n", rc);
        goto cleanup;
    }

    mnc2_kernel_t kernel = mnc2_load_kernel(dev, kernel_path);
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

    rc = mnc2_recv(dev, recvbuf, OFFSET_OUT, ELEM_BYTES, WD_RECV);
    if (rc != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: mnc2_recv returned %d\n", rc);
        goto cleanup;
    }

    double *rp = (double *)recvbuf;
    printf("  戻りの先頭:     ");
    for (int k = 0; k < SIZE_N; k++)
        printf("%.0f ", rp[k * PE_COUNT]);
    printf("\n");

    int errors = 0;
    for (int k = 0; k < SIZE_N; k++) {
        for (int p = 0; p < PE_COUNT; p++) {
            double expected = value_of(k, p);
            double got      = rp[k * PE_COUNT + p];
            if (got != expected) {
                if (errors < 5)
                    fprintf(stderr, "  MISMATCH 成分 %d PE %d: got=%g expected=%g\n",
                            k, p, got, expected);
                errors++;
            }
        }
    }

    if (errors > 0) {
        fprintf(stderr, "  FAIL: %d mismatches\n", errors);
        rc = 1;
    } else {
        printf("  PASS: 全 %d 要素 (成分 %d かける PE %d) が一致\n",
               ELEM_COUNT, SIZE_N, PE_COUNT);
        rc = 0;
    }

cleanup:
    mnc2_free_host_buffer(dev, sendbuf, ELEM_BYTES);
    mnc2_free_host_buffer(dev, recvbuf, ELEM_BYTES);
    mnc2_close(dev);
    return (rc != 0) ? 1 : 0;
}
