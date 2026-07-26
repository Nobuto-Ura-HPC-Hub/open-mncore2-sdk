/* test_stencil1d_distribute.c — @boundary_flags distribute の E2E 検証
 *
 * example 05 と同じ 1D 3-point stencil だが、境界フラグをホスト側で計算し
 * PDM に send してからカーネルを実行する。
 *
 * ビットレイアウト (LOW 32bit):
 *   bit[0] = PE_right_edge   ($subpeid == 3)
 *   bit[1] = PE_left_edge    ($subpeid == 0)
 *   bit[2] = MAB_right_edge  ($mabid == 15)
 *   bit[3] = MAB_left_edge   ($mabid == 0)
 *   bit[4] = L1B_right_edge  ($l1bid == 7)
 *   bit[5] = L1B_left_edge   ($l1bid == 0)
 *   bit[6] = L2B_right_edge  (将来: 常に 0)
 *   bit[7] = L2B_left_edge   (将来: 常に 0)
 *   bit[8] = CHIP_right_edge (将来: 常に 0)
 *   bit[9] = CHIP_left_edge  (将来: 常に 0)
 *
 * flat PE ID = subpeid + (mabid << 2) + (l1bid << 6) + (l2bid << 9)
 *
 * PDM レイアウト (stencil1d.param):
 *   slot 8:  addr 0    — input  (4096 LW)
 *   slot 16: addr 4096 — output (4096 LW)
 *
 * boundary flags は @boundary_flags_compute で HW レジスタから計算（PDM 不要）
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "mnc2.h"

#define ELEM_COUNT    4096
#define ELEM_BYTES    (ELEM_COUNT * sizeof(double))

#define OFFSET_IN     0                       /* slot 8:  PDM word 0    */
#define OFFSET_OUT    (4096ULL * 8)           /* slot 16: PDM word 4096 */

#define DMAID_TRIGGER 0x10
#define WD_RECV       0x1e  /* .param recv_wait_tag と対応 */


int main(void)
{
    printf("=== stencil1d: 3-point sum with @boundary_flags distribute ===\n\n");

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

    /* 入力データ: input[i] = i + 1 */
    double *input = (double *)malloc(ELEM_BYTES);
    if (input == NULL) {
        fprintf(stderr, "FAIL: malloc\n");
        goto cleanup;
    }
    for (int i = 0; i < ELEM_COUNT; i++)
        input[i] = (double)(i + 1);

    /* boundary flags は @boundary_flags_compute で計算（PDM 不要） */

    int rc;
    /* 入力データ send (trigger) */
    printf("[send] input -> PDM offset %d (dmaid=0x%02x, trigger)\n",
           (int)OFFSET_IN, DMAID_TRIGGER);
    memcpy(sendbuf, input, ELEM_BYTES);
    rc = mnc2_send(dev, sendbuf, OFFSET_IN, ELEM_BYTES, DMAID_TRIGGER);
    if (rc != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: mnc2_send input returned %d\n", rc);
        goto cleanup;
    }

    /* カーネル実行 */
    printf("[exec] stencil1d kernel\n");
    mnc2_kernel_t kernel = mnc2_load_kernel(dev, "_build/stencil1d.idma.dat");
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

    /* 結果 recv */
    printf("[recv] output <- PDM offset %llu\n", (unsigned long long)OFFSET_OUT);
    memset(recvbuf, 0, ELEM_BYTES);
    rc = mnc2_recv(dev, recvbuf, OFFSET_OUT, ELEM_BYTES, WD_RECV);
    if (rc != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: mnc2_recv returned %d\n", rc);
        goto cleanup;
    }

    /* 検証 */
    {
        double *rp = (double *)recvbuf;
        int errors = 0;

        printf("  [boundary] PE0=%g PE4095=%g (検証対象外)\n", rp[0], rp[ELEM_COUNT - 1]);

        for (int i = 1; i < ELEM_COUNT - 1; i++) {
            double expected = input[i - 1] + input[i] + input[i + 1];
            if (rp[i] != expected) {
                if (errors < 10)
                    fprintf(stderr, "  MISMATCH [%d]: got=%g expected=%g\n",
                            i, rp[i], expected);
                errors++;
            }
        }

        if (errors > 0) {
            fprintf(stderr, "  [internal] %d / %d FAIL\n", errors, ELEM_COUNT - 2);
            rc = 1;
        } else {
            printf("  [internal] %d / %d PASS\n", ELEM_COUNT - 2, ELEM_COUNT - 2);
            rc = 0;
        }
    }

cleanup:
    mnc2_free_host_buffer(dev, sendbuf, ELEM_BYTES);
    mnc2_free_host_buffer(dev, recvbuf, ELEM_BYTES);
    mnc2_close(dev);
    free(input);
    return (rc != 0) ? 1 : 0;
}
