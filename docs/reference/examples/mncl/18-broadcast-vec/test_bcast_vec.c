/* test_bcast_vec.c -- broadcast の size N (double3) の E2E テスト (emu:lib)
 *
 * out[i] = v.x + v.y * 2 + v.z * 4 + x[i]  on 4096 PEs。
 *   v = broadcast(_bc)   double3。1 回の broadcast で 3 個を配る
 *   x = distribute(_x)   PE ごとの値
 *
 * 目的: broadcast の size N が動くこと、および**レーンの対応が正しいこと**の確認。
 *
 * **レーンごとに別の値を置き、カーネル側で別の係数を掛けている。** 3 個とも同じ値を置くと、
 * レーンが入れ替わっていても、同じ値が 3 つ届いていても気づけない。16-broadcast-f64 は
 * バッファ全体を同じ値で埋めていたので、そこを検証できていなかった。
 *
 * 放送領域への置き方: 配りたい N 個を**先頭 N u64** に置く（源 u64[i] が cycle i として
 * 着地する。directives-spec.md の「入力規約」）。領域は 64 u64 確保する（mvb の最小転送単位）。
 *
 * PDM layout (bcast_vec.param):
 *   slot 8  (bc):  PDM word 0     -- 先頭 3 u64 = A, B, C
 *   slot 16 (x):   PDM word 4096
 *   slot 24 (out): PDM word 8192
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
#define OFFSET_X      (4096ULL * 8)             /* slot 16: PDM word 4096 */
#define OFFSET_OUT    (8192ULL * 8)             /* slot 24: PDM word 8192 */
#define SEND_WAIT_TAG 0x10
#define RECV_TAG      0x1e

/* 全 PE に配る 3 つの値。結果が f64 で正確に表せる値にして厳密比較する。
 * 3 つとも別の値にして、レーンの取り違えが結果に出るようにする。 */
#define BCAST_A  100.5
#define BCAST_B  7.25
#define BCAST_C  0.125

int main(void)
{
    printf("=== 18-broadcast-vec: out[i] = v.x + v.y*2 + v.z*4 + x[i] (broadcast size 3, 4096 PE) ===\n\n");

    mnc2_device_t dev = mnc2_open(0);
    if (dev == NULL) { fprintf(stderr, "FAIL: mnc2_open returned NULL\n"); return 1; }
    printf("18-broadcast-vec backend: %s\n", mnc2_get_backend_name(dev));

    void *bcbuf   = mnc2_alloc_host_buffer(dev, BC_BYTES);
    void *xbuf    = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    void *recvbuf = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    if (!bcbuf || !xbuf || !recvbuf) {
        fprintf(stderr, "FAIL: mnc2_alloc_host_buffer\n"); mnc2_close(dev); return 1;
    }

    double *orig_x = (double *)malloc(ELEM_BYTES);
    if (!orig_x) { fprintf(stderr, "FAIL: malloc\n"); mnc2_close(dev); return 1; }

    int rc;

    /* broadcast 入力: 先頭 3 u64 に A, B, C を置く。残りは読まれないが 0 で埋めておく。
     * 4 番目以降にゴミが残っていても結果が変わらないことも、これで確かめている。 */
    {
        double *p = (double *)bcbuf;
        for (int i = 0; i < BC_LW; i++) p[i] = 0.0;
        p[0] = BCAST_A;
        p[1] = BCAST_B;
        p[2] = BCAST_C;
    }
    rc = mnc2_send(dev, bcbuf, OFFSET_BC, BC_BYTES, 0);
    if (rc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: send bc %d\n", rc); goto cleanup; }

    /* distribute 入力 x[i]。トリガ（最後の send。カーネルはこの tag の完了で起動）*/
    for (int i = 0; i < ELEM_COUNT; i++) orig_x[i] = (double)i * 3.0 + 0.5;
    memcpy(xbuf, orig_x, ELEM_BYTES);
    rc = mnc2_send(dev, xbuf, OFFSET_X, ELEM_BYTES, SEND_WAIT_TAG);
    if (rc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: send x %d\n", rc); goto cleanup; }

    /* exec */
    mnc2_kernel_t kernel = mnc2_load_kernel(dev, "_build/bcast_vec.idma.dat");
    if (!kernel) { fprintf(stderr, "FAIL: mnc2_load_kernel\n"); rc = 1; goto cleanup; }
    rc = mnc2_exec_kernel(kernel);
    mnc2_free_kernel(kernel);
    if (rc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: mnc2_exec_kernel %d\n", rc); goto cleanup; }

    /* recv */
    memset(recvbuf, 0, ELEM_BYTES);
    rc = mnc2_recv(dev, recvbuf, OFFSET_OUT, ELEM_BYTES, RECV_TAG);
    if (rc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: mnc2_recv %d\n", rc); goto cleanup; }

    /* verify: out[i] == A + B*2 + C*4 + x[i] */
    {
        double *rp = (double *)recvbuf;
        double bc_part = BCAST_A + BCAST_B * 2.0 + BCAST_C * 4.0;
        int errors = 0;
        for (int i = 0; i < ELEM_COUNT; i++) {
            double expected = bc_part + orig_x[i];
            if (rp[i] != expected) {
                if (errors < 5)
                    fprintf(stderr, "  MISMATCH out[%d]: got=%.6f expected=%.6f (broadcast part=%.6f)\n",
                            i, rp[i], expected, bc_part);
                errors++;
            }
        }
        if (errors > 0) {
            fprintf(stderr, "  FAIL: %d mismatches\n", errors);
            fprintf(stderr, "  差が一定なら 3 レーンのどれかが届いていないか、順序が入れ替わっている。\n");
            fprintf(stderr, "  期待する broadcast 部分 = %.6f (A=%.3f B=%.3f C=%.3f)\n",
                    bc_part, BCAST_A, BCAST_B, BCAST_C);
            rc = 1;
        } else {
            printf("  PASS: out[i] == v.x + v.y*2 + v.z*4 + x[i] for all %d elements"
                   " (broadcast size 3, レーンの対応も一致)\n", ELEM_COUNT);
            rc = 0;
        }
    }

cleanup:
    mnc2_free_host_buffer(dev, bcbuf, BC_BYTES);
    mnc2_free_host_buffer(dev, xbuf, ELEM_BYTES);
    mnc2_free_host_buffer(dev, recvbuf, ELEM_BYTES);
    mnc2_close(dev);
    free(orig_x);
    return (rc != 0) ? 1 : 0;
}
