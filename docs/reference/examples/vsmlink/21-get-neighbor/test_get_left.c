/* test_get_left.c — @get_neighbor offset=-1 E2E 検証 (emu:lib)
 *
 * input[i] = i + 1  (1.0, 2.0, ..., 4096.0)
 * 期待: output[i] = input[i-1]  (全 4096 PE)
 *       PE 0: clamp → 自分自身 = input[0] = 1.0
 *       PE 1〜4095: input[i-1] = i
 *
 * PDM レイアウト (get_left.param):
 *   addr 0..4095:     left edge 用の余白（未使用、clamp では読まない）
 *   addr 4096..8191:  input データ（slot 8, addr 4096）
 *   addr 8192..12287: output（slot 16, addr 8192）
 *   addr 12288..16383: boundary_flags（bf1, addr 12288）
 *
 * clamp 処理:
 *   data_edge_left の PE（PE 0）は自分自身の値を返す。
 *   boundary_flags の data_edge_left ビット (0x800) で判定。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "mnc2.h"

#define ELEM_COUNT  4096
#define ELEM_BYTES  (ELEM_COUNT * sizeof(double))
#define BF_BYTES    (ELEM_COUNT * sizeof(uint64_t))
/* PDM アドレスを 4096 LW ずらして、slot の前に edge 値を配置する余白を確保 */
#define EDGE_MARGIN 4096
#define OFFSET_EDGE_L ((EDGE_MARGIN - 1ULL) * 8)     /* addr 4095: left edge */
#define OFFSET_IN   ((EDGE_MARGIN) * 8ULL)            /* slot 8:  addr 4096 */
#define OFFSET_OUT  ((EDGE_MARGIN + 4096ULL) * 8)     /* slot 16: addr 8192 */
#define OFFSET_BF   ((EDGE_MARGIN + 8192ULL) * 8)     /* bf1:     addr 12288 */
#define DMAID_TRIGGER 0x10
#define WD_RECV     0x1e  /* .param recv_wait_tag と対応 */

#define BF_DATA_PATH "_build/75c77b0f.bin"

int main(void)
{
    printf("=== get_neighbor left: output[i] = input[i-1] (4096 PE, edge included) ===\n\n");

    mnc2_device_t dev = mnc2_open(0);
    if (!dev) {
        fprintf(stderr, "FAIL: mnc2_open\n");
        return 1;
    }

    void *sendbuf = mnc2_alloc_host_buffer(dev, ELEM_BYTES + 2 * sizeof(double));
    void *recvbuf = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    void *bfbuf   = mnc2_alloc_host_buffer(dev, BF_BYTES);
    if (!sendbuf || !recvbuf || !bfbuf) {
        fprintf(stderr, "FAIL: mnc2_alloc_host_buffer\n");
        mnc2_close(dev);
        return 1;
    }

    /* input: 1.0, 2.0, ..., 4096.0 */
    double *input = (double *)malloc(ELEM_BYTES);
    for (int i = 0; i < ELEM_COUNT; i++)
        input[i] = (double)(i + 1);

    int rc;

    /* send boundary flags (non-trigger, dmaid=0) */
    {
        FILE *fp = fopen(BF_DATA_PATH, "rb");
        if (!fp) {
            fprintf(stderr, "FAIL: cannot open %s\n"
                    "  Run: ninja -C ../17-boundary-collect build-e2e && ninja -C ../17-boundary-collect test\n",
                    BF_DATA_PATH);
            rc = 1; goto cleanup;
        }
        size_t nr = fread(bfbuf, 1, BF_BYTES, fp);
        fclose(fp);
        if (nr != BF_BYTES) {
            fprintf(stderr, "FAIL: bf read %zu/%zu\n", nr, (size_t)BF_BYTES);
            rc = 1; goto cleanup;
        }
    }
    printf("[send] boundary_flags -> PDM offset %llu (dmaid=0)\n",
           (unsigned long long)OFFSET_BF);
    rc = mnc2_send(dev, bfbuf, OFFSET_BF, BF_BYTES, 0);
    if (rc != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: mnc2_send bf: %d\n", rc);
        goto cleanup;
    }

    /* edge 値の PDM 配置
     * addr 0〜4095 に 4096 LW の edge データを書く。
     * PE 0 の get(-1) は addr 4096 - 1 = 4095 を読む。
     * input[i] = i+1 の連続として、addr 4095 に 0.0 を配置。
     * 他の addr は使われないが、4096 LW 単位で送る。 */
    {
        double *edge_buf = (double *)calloc(ELEM_COUNT, sizeof(double));
        edge_buf[ELEM_COUNT - 1] = 0.0;  /* addr 4095 = input[-1] 相当 */
        memcpy(sendbuf, edge_buf, ELEM_BYTES);
        free(edge_buf);
        printf("[send] left edge region (4096 LW) -> PDM offset 0 (dmaid=0)\n");
        rc = mnc2_send(dev, sendbuf, 0, ELEM_BYTES, 0);
        if (rc != MNC2_SUCCESS) {
            fprintf(stderr, "FAIL: mnc2_send left edge: %d\n", rc);
            goto cleanup;
        }
    }

    /* send input (trigger) */
    printf("[send] input -> PDM offset %llu (dmaid=0x%02x, trigger)\n",
           (unsigned long long)OFFSET_IN, DMAID_TRIGGER);
    memcpy(sendbuf, input, ELEM_BYTES);
    rc = mnc2_send(dev, sendbuf, OFFSET_IN, ELEM_BYTES, DMAID_TRIGGER);
    if (rc != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: mnc2_send input: %d\n", rc);
        goto cleanup;
    }

    /* exec kernel */
    printf("[exec] get_left kernel\n");
    mnc2_kernel_t kernel = mnc2_load_kernel(dev, "_build/get_left.idma.dat");
    if (!kernel) {
        fprintf(stderr, "FAIL: mnc2_load_kernel\n");
        rc = 1; goto cleanup;
    }
    rc = mnc2_exec_kernel(kernel);
    mnc2_free_kernel(kernel);
    if (rc != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: mnc2_exec_kernel: %d\n", rc);
        goto cleanup;
    }

    /* recv output */
    printf("[recv] output <- PDM offset %llu\n", (unsigned long long)OFFSET_OUT);
    memset(recvbuf, 0, ELEM_BYTES);
    rc = mnc2_recv(dev, recvbuf, OFFSET_OUT, ELEM_BYTES, WD_RECV);
    if (rc != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: mnc2_recv: %d\n", rc);
        goto cleanup;
    }

    /* verify */
    {
        double *out = (double *)recvbuf;
        int errors = 0;

        /* PE 0: clamp → 自分自身 = input[0] = 1.0 */
        {
            double expected = input[0];
            if (out[0] != expected) {
                fprintf(stderr, "  MISMATCH [0] (edge): got=%g expected=%g\n",
                        out[0], expected);
                errors++;
            }
        }

        /* PE 1〜4095: output[i] = input[i-1] = i */
        for (int i = 1; i < ELEM_COUNT; i++) {
            double expected = input[i - 1];
            if (out[i] != expected) {
                if (errors < 10)
                    fprintf(stderr, "  MISMATCH [%d]: got=%g expected=%g\n",
                            i, out[i], expected);
                errors++;
            }
        }

        if (errors > 0) {
            fprintf(stderr, "  FAIL: %d mismatches\n", errors);
            rc = 1;
        } else {
            printf("  PASS: get_neighbor left correct for all %d PEs (edge included)\n",
                   ELEM_COUNT);
            rc = 0;
        }
    }

cleanup:
    mnc2_free_host_buffer(dev, sendbuf, ELEM_BYTES + 2 * sizeof(double));
    mnc2_free_host_buffer(dev, recvbuf, ELEM_BYTES);
    mnc2_free_host_buffer(dev, bfbuf, BF_BYTES);
    mnc2_close(dev);
    free(input);
    return (rc != 0) ? 1 : 0;
}
