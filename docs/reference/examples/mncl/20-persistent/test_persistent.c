/* test_persistent.c -- 20-persistent のホスト側
 *
 * persistent の変数 acc を、2 つの kernel で共有する。
 *   init:  acc = seed                          (書くだけ)
 *   accum: acc = acc + in; out = acc           (前の実行が残した acc に足す)
 *
 * init を 1 回、accum を 3 回実行し、accum の out が毎回
 * 「seed + それまでの in の合計」と一致するかを確かめる。
 * kernel は 1 つずつ読み込んで実行する（persistent.cl から kernel ごとの .vsm ができる）。
 *
 * PDM の配置 (persistent.param):
 *   slot 8  seed (init の引数):  PDM[0..4095]      -- byte offset 0
 *   slot 16 in   (accum の引数): PDM[4096..8191]   -- byte offset 32768
 *   slot 24 out  (accum の引数): PDM[8192..12287]  -- byte offset 65536
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mnc2.h"

#define ELEM_COUNT    4096
#define ELEM_BYTES    (ELEM_COUNT * sizeof(double))
#define OFFSET_SEED   (0ULL)
#define OFFSET_IN     (4096ULL * 8)
#define OFFSET_OUT    (8192ULL * 8)
#define SEND_WAIT_TAG 0x10
#define RECV_TAG      0x1e
#define ROUNDS        3

int main(void)
{
    printf("=== 20-persistent: init -> accum x%d (4096 PE) ===\n\n", ROUNDS);

    mnc2_device_t dev = mnc2_open(0);
    if (dev == NULL) { fprintf(stderr, "FAIL: mnc2_open\n"); return 1; }
    printf("backend: %s\n", mnc2_get_backend_name(dev));

    int rc = 0;
    mnc2_kernel_t init_k = NULL;
    mnc2_kernel_t accum_k = NULL;
    void *sendbuf = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    void *recvbuf = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    double *expected = (double *)malloc(ELEM_BYTES);
    if (!sendbuf || !recvbuf || !expected) {
        fprintf(stderr, "FAIL: alloc\n"); rc = 1; goto cleanup;
    }

    /* --- init: acc に seed[i] = i を書く --- */
    double *sp = (double *)sendbuf;
    for (int i = 0; i < ELEM_COUNT; i++) {
        sp[i] = (double)i;
        expected[i] = sp[i];
    }
    if (mnc2_send(dev, sendbuf, OFFSET_SEED, ELEM_BYTES, SEND_WAIT_TAG) != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: send seed\n"); rc = 1; goto cleanup;
    }
    init_k = mnc2_load_kernel(dev, "_build/init.idma.dat");
    if (!init_k) { fprintf(stderr, "FAIL: load_kernel(init)\n"); rc = 1; goto cleanup; }
    if (mnc2_exec_kernel(init_k) != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: exec init\n"); rc = 1; goto cleanup;
    }
    /* init はもう使わない。mnc2_free_kernel は kernel の実行が済むのを待ってから解放する。
     * 解放しても acc の値は PE の中に残る */
    mnc2_free_kernel(init_k);
    init_k = NULL;

    /* --- accum: 3 回実行する。acc は実行の間も PE の中に残る --- */
    accum_k = mnc2_load_kernel(dev, "_build/accum.idma.dat");
    if (!accum_k) { fprintf(stderr, "FAIL: load_kernel(accum)\n"); rc = 1; goto cleanup; }

    for (int r = 0; r < ROUNDS; r++) {
        for (int i = 0; i < ELEM_COUNT; i++) {
            sp[i] = (double)i * 0.001 + (double)(r + 1);
            expected[i] += sp[i];
        }
        if (mnc2_send(dev, sendbuf, OFFSET_IN, ELEM_BYTES, SEND_WAIT_TAG) != MNC2_SUCCESS) {
            fprintf(stderr, "FAIL: send in (round %d)\n", r); rc = 1; goto cleanup;
        }
        if (mnc2_exec_kernel(accum_k) != MNC2_SUCCESS) {
            fprintf(stderr, "FAIL: exec accum (round %d)\n", r); rc = 1; goto cleanup;
        }
        memset(recvbuf, 0, ELEM_BYTES);
        if (mnc2_recv(dev, recvbuf, OFFSET_OUT, ELEM_BYTES, RECV_TAG) != MNC2_SUCCESS) {
            fprintf(stderr, "FAIL: recv out (round %d)\n", r); rc = 1; goto cleanup;
        }

        const double *rp = (const double *)recvbuf;
        int errors = 0;
        for (int i = 0; i < ELEM_COUNT; i++) {
            if (rp[i] != expected[i]) {
                if (errors < 5)
                    fprintf(stderr, "  MISMATCH round %d out[%d]: got=%g expected=%g\n",
                            r, i, rp[i], expected[i]);
                errors++;
            }
        }
        if (errors > 0) {
            fprintf(stderr, "FAIL: round %d, %d/%d mismatches\n", r, errors, ELEM_COUNT);
            rc = 1; goto cleanup;
        }
        printf("PASS: round %d out[i] == cumulative sum for all %d elements\n", r, ELEM_COUNT);
    }

cleanup:
    if (init_k) mnc2_free_kernel(init_k);
    if (accum_k) mnc2_free_kernel(accum_k);
    if (sendbuf) mnc2_free_host_buffer(dev, sendbuf, ELEM_BYTES);
    if (recvbuf) mnc2_free_host_buffer(dev, recvbuf, ELEM_BYTES);
    mnc2_close(dev);
    free(expected);
    return (rc != 0) ? 1 : 0;
}
