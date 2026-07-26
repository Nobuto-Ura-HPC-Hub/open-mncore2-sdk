/* test_bcast_f64.c -- broadcast の f64 版の E2E テスト (emu:lib)
 *
 * out[i] = v + w + x[i]  on 4096 PEs。
 *   v = broadcast(_bc)   全 PE 同値（host が bc の index 12..15 を V で埋める）
 *   w = broadcast(_bc2)  全 PE 同値（同一カーネルで 2 本目の broadcast）
 *   x = distribute(_x)   PE ごとの値
 *
 * 目的: broadcast の f64 版が動くこと、および 1 カーネルに broadcast を 2 本置けることの確認
 * （nbody は 3 本使う）。
 *
 * PDM layout (bcast_f64.param):
 *   slot 8  (bc):  PDM word 0     -- index 12..15 = V
 *   slot 16 (bc2): PDM word 64    -- index 12..15 = W
 *   slot 24 (x):   PDM word 4096
 *   slot 32 (out): PDM word 8192
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "mnc2.h"

#define ELEM_COUNT    4096
#define ELEM_BYTES    (ELEM_COUNT * sizeof(double))
#define BC_LW         64                        /* mvb/n64 の最小転送単位 */
#define BC_BYTES      (BC_LW * sizeof(double))
#define OFFSET_BC     0                         /* slot 8:  PDM word 0    */
#define OFFSET_BC2    (64ULL * 8)               /* slot 16: PDM word 64   */
#define OFFSET_X      (4096ULL * 8)             /* slot 24: PDM word 4096 */
#define OFFSET_OUT    (8192ULL * 8)             /* slot 32: PDM word 8192 */
#define SEND_WAIT_TAG 0x10
#define RECV_TAG      0x1e

/* 全 PE に配る 2 つの値。V+W+x[i] が f64 で正確に表せる値にして厳密比較する */
#define BCAST_V  100.5
#define BCAST_W  7.25

int main(void)
{
    printf("=== 16-broadcast-f64: out[i] = v + w + x[i] (f64 broadcast x2, 4096 PE) ===\n\n");

    mnc2_device_t dev = mnc2_open(0);
    if (dev == NULL) { fprintf(stderr, "FAIL: mnc2_open returned NULL\n"); return 1; }
    printf("16-broadcast-f64 backend: %s\n", mnc2_get_backend_name(dev));

    void *bcbuf   = mnc2_alloc_host_buffer(dev, BC_BYTES);
    void *bc2buf  = mnc2_alloc_host_buffer(dev, BC_BYTES);
    void *xbuf    = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    void *recvbuf = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    if (!bcbuf || !bc2buf || !xbuf || !recvbuf) {
        fprintf(stderr, "FAIL: mnc2_alloc_host_buffer\n"); mnc2_close(dev); return 1;
    }

    double *orig_x = (double *)malloc(ELEM_BYTES);
    if (!orig_x) { fprintf(stderr, "FAIL: malloc\n"); mnc2_close(dev); return 1; }

    int rc;

    /* broadcast 入力: バッファ全体を V / W で埋める（index 12..15 が全 PE に届く）。非トリガ */
    { double *p = (double *)bcbuf;  for (int i = 0; i < BC_LW; i++) p[i] = BCAST_V; }
    { double *p = (double *)bc2buf; for (int i = 0; i < BC_LW; i++) p[i] = BCAST_W; }
    rc = mnc2_send(dev, bcbuf, OFFSET_BC, BC_BYTES, 0);
    if (rc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: send bc %d\n", rc); goto cleanup; }
    rc = mnc2_send(dev, bc2buf, OFFSET_BC2, BC_BYTES, 0);
    if (rc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: send bc2 %d\n", rc); goto cleanup; }

    /* distribute 入力 x[i]。トリガ（最後の send。カーネルはこの tag の完了で起動）*/
    for (int i = 0; i < ELEM_COUNT; i++) orig_x[i] = (double)i * 3.0 + 0.5;
    memcpy(xbuf, orig_x, ELEM_BYTES);
    rc = mnc2_send(dev, xbuf, OFFSET_X, ELEM_BYTES, SEND_WAIT_TAG);
    if (rc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: send x %d\n", rc); goto cleanup; }

    /* exec */
    mnc2_kernel_t kernel = mnc2_load_kernel(dev, "_build/bcast_f64.idma.dat");
    if (!kernel) { fprintf(stderr, "FAIL: mnc2_load_kernel\n"); rc = 1; goto cleanup; }
    rc = mnc2_exec_kernel(kernel);
    mnc2_free_kernel(kernel);
    if (rc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: mnc2_exec_kernel %d\n", rc); goto cleanup; }

    /* recv */
    memset(recvbuf, 0, ELEM_BYTES);
    rc = mnc2_recv(dev, recvbuf, OFFSET_OUT, ELEM_BYTES, RECV_TAG);
    if (rc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: mnc2_recv %d\n", rc); goto cleanup; }

    /* verify: out[i] == V + W + x[i] */
    {
        double *rp = (double *)recvbuf;
        int errors = 0;
        for (int i = 0; i < ELEM_COUNT; i++) {
            double expected = BCAST_V + BCAST_W + orig_x[i];
            if (rp[i] != expected) {
                if (errors < 5)
                    fprintf(stderr, "  MISMATCH out[%d]: got=%.6f expected=%.6f\n", i, rp[i], expected);
                errors++;
            }
        }
        if (errors > 0) {
            fprintf(stderr, "  FAIL: %d mismatches"
                    " (不一致が i%%4 で規則的なら broadcast 入力 index 12..15 の埋め忘れを疑う)\n", errors);
            rc = 1;
        } else {
            printf("  PASS: out[i] == V + W + x[i] for all %d elements (f64 broadcast x2)\n", ELEM_COUNT);
            rc = 0;
        }
    }

cleanup:
    mnc2_free_host_buffer(dev, bcbuf, BC_BYTES);
    mnc2_free_host_buffer(dev, bc2buf, BC_BYTES);
    mnc2_free_host_buffer(dev, xbuf, ELEM_BYTES);
    mnc2_free_host_buffer(dev, recvbuf, ELEM_BYTES);
    mnc2_close(dev);
    free(orig_x);
    return (rc != 0) ? 1 : 0;
}
