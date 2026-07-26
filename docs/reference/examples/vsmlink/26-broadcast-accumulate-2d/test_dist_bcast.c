/* test_dist_bcast.c — 段階1: distribute と broadcast の辻褄確認 (emu:lib)
 *
 * 各 PE に自粒子 (distribute) と他粒子 (broadcast) を配り、self + other を計算する。
 *   self[i]   = (double)i      distribute で PE i へ (PE ごとに異なる)
 *   other     = 100.0          broadcast で全 PE へ (全 PE 同値)
 *   result[i] = self[i] + other = i + 100.0
 *
 * これで distribute (PE ごとに異なる) と broadcast (全 PE 同値) が両立して
 * 正しく届くことを確認する。力計算はまだ入れない。
 *
 * PDM レイアウト (dist_bcast.param):
 *   slot 8  (self):   PDM[0..4095]         distribute
 *   slot 16 (other):  PDM[4096..4159]      broadcast (index 12..15 が MAB 内 4 PE へ)
 *   slot 24 (result): PDM[131072..135167]  collect
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mnc2.h"

#define ELEM_COUNT    4096
#define ELEM_BYTES    (ELEM_COUNT * sizeof(double))
#define OFFSET_SELF   0
#define OFFSET_OTHER  (4096ULL * 8)
#define OFFSET_RESULT (131072ULL * 8)
#define BCAST_COUNT   64
#define BCAST_BYTES   (BCAST_COUNT * sizeof(double))
#define DMAID_TRIGGER 0x10
#define WD_RECV       0x1e
#define OTHER_VAL     100.0

int main(void)
{
    printf("=== 段階1: distribute + broadcast 辻褄確認 (4096 PE) ===\n\n");

    mnc2_device_t dev = mnc2_open(0);
    if (!dev) { fprintf(stderr, "FAIL: mnc2_open\n"); return 1; }

    void *self_buf  = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    void *other_buf = mnc2_alloc_host_buffer(dev, BCAST_BYTES);
    void *recv_buf  = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    if (!self_buf || !other_buf || !recv_buf) {
        fprintf(stderr, "FAIL: alloc\n"); mnc2_close(dev); return 1;
    }

    int rc;

    /* other (broadcast): index 12..15 に OTHER_VAL、 他は 0 (全 PE 同値にする) */
    double *ob = (double *)other_buf;
    /* l1bmp: 領域を全部同値で埋める (全 64 PE に放送) */
    for (int i = 0; i < BCAST_COUNT; i++) ob[i] = OTHER_VAL;
    rc = mnc2_send(dev, other_buf, OFFSET_OTHER, BCAST_BYTES, 0);   /* 非トリガー */
    if (rc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: send other\n"); rc = 1; goto cleanup; }

    /* self (distribute): self[i] = (double)i、 トリガー (wait i10 を解除) */
    double *sb = (double *)self_buf;
    for (int i = 0; i < ELEM_COUNT; i++) sb[i] = (double)i;
    rc = mnc2_send(dev, self_buf, OFFSET_SELF, ELEM_BYTES, DMAID_TRIGGER);
    if (rc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: send self\n"); rc = 1; goto cleanup; }

    /* exec */
    mnc2_kernel_t k = mnc2_load_kernel(dev, "_build/dist_bcast.idma.dat");
    if (!k) { fprintf(stderr, "FAIL: load kernel\n"); rc = 1; goto cleanup; }
    rc = mnc2_exec_kernel(k);
    mnc2_free_kernel(k);
    if (rc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: exec\n"); rc = 1; goto cleanup; }

    /* recv result */
    memset(recv_buf, 0, ELEM_BYTES);
    rc = mnc2_recv(dev, recv_buf, OFFSET_RESULT, ELEM_BYTES, WD_RECV);
    if (rc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: recv\n"); rc = 1; goto cleanup; }

    /* 照合: result[i] == i + 100.0 */
    {
        double *rp = (double *)recv_buf;
        int errors = 0;
        for (int i = 0; i < ELEM_COUNT; i++) {
            double expected = (double)i + OTHER_VAL;
            if (rp[i] != expected) {
                if (errors < 5)
                    fprintf(stderr, "  MISMATCH result[%d]: got=%g expected=%g\n",
                            i, rp[i], expected);
                errors++;
            }
        }
        if (errors > 0) {
            fprintf(stderr, "  FAIL: %d mismatches (distribute か broadcast の辻褄ずれ)\n", errors);
            rc = 1;
        } else {
            printf("  PASS: result[i] == i + %.1f、 distribute と broadcast の辻褄 OK\n", OTHER_VAL);
            rc = 0;
        }
    }

cleanup:
    if (self_buf)  mnc2_free_host_buffer(dev, self_buf, ELEM_BYTES);
    if (other_buf) mnc2_free_host_buffer(dev, other_buf, BCAST_BYTES);
    if (recv_buf)  mnc2_free_host_buffer(dev, recv_buf, ELEM_BYTES);
    mnc2_close(dev);
    return (rc != 0) ? 1 : 0;
}
