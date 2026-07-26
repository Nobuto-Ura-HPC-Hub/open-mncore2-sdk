/* test_stencil1d.c — 1D 3-point stencil E2E 検証 (emu:lib)
 *
 * カーネル: output[i] = left[i] + center[i] + right[i]
 *   left  = @stencil offset -1 (左隣 PE の値、境界 PE は 0)
 *   center = 入力値そのまま
 *   right = @stencil offset +1 (右隣 PE の値、境界 PE は 0)
 *
 * 検証: 内部 PE (1..4094) で output[i] == input[i-1] + input[i] + input[i+1]
 *        境界 PE (0, 4095) は袖交換の境界処理に依存するため別途レポート
 *
 * PDM レイアウト (stencil1d.param):
 *   input:  PDM[0..4095]       — slot 8,  byte offset 0
 *   output: PDM[4096..8191]    — slot 16, byte offset 32768
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "mnc2.h"

#define ELEM_COUNT  4096
#define ELEM_BYTES  (ELEM_COUNT * sizeof(double))
#define BF_BYTES    (ELEM_COUNT * sizeof(uint64_t))
#define OFFSET_IN   0                       /* slot 8:  PDM word 0    */
#define OFFSET_OUT  (4096ULL * 8)           /* slot 16: PDM word 4096 */
#define OFFSET_BF   (8192ULL * 8)           /* bf1:     PDM word 8192 */
#define DMAID_TRIGGER 0x10
#define WD_RECV     0x1e  /* .param recv_wait_tag と対応 */

/* boundary flags は 17-boundary-collect で生成した collected_flags.bin を使用 */
#define BF_DATA_PATH "_build/57b42dea.bin"

int main(void)
{
    printf("=== stencil1d: 3-point sum with @stencil (4096 PE) ===\n\n");

    mnc2_device_t dev = mnc2_open(0);
    if (dev == NULL) {
        fprintf(stderr, "FAIL: mnc2_open returned NULL\n");
        return 1;
    }

    void *sendbuf = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    void *recvbuf = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    void *bfbuf   = mnc2_alloc_host_buffer(dev, BF_BYTES);
    if (sendbuf == NULL || recvbuf == NULL || bfbuf == NULL) {
        fprintf(stderr, "FAIL: mnc2_alloc_host_buffer\n");
        mnc2_close(dev);
        return 1;
    }

    double *input = (double *)malloc(ELEM_BYTES);
    if (input == NULL) {
        fprintf(stderr, "FAIL: malloc\n");
        mnc2_free_host_buffer(dev, sendbuf, ELEM_BYTES);
        mnc2_free_host_buffer(dev, recvbuf, ELEM_BYTES);
        mnc2_close(dev);
        return 1;
    }
    for (int i = 0; i < ELEM_COUNT; i++)
        input[i] = (double)(i + 1);

    int rc;

    /* send boundary flags from 17-boundary-collect (non-trigger) */
    {
        FILE *fp = fopen(BF_DATA_PATH, "rb");
        if (!fp) {
            fprintf(stderr, "FAIL: cannot open %s\n"
                    "  Run: ninja -C ../17-boundary-collect build-e2e && ninja -C ../17-boundary-collect test\n",
                    BF_DATA_PATH);
            goto cleanup;
        }
        size_t nr = fread(bfbuf, 1, BF_BYTES, fp);
        fclose(fp);
        if (nr != BF_BYTES) {
            fprintf(stderr, "FAIL: %s: read %zu bytes, expected %zu\n",
                    BF_DATA_PATH, nr, (size_t)BF_BYTES);
            goto cleanup;
        }
    }
    printf("[send] boundary_flags (%s) -> PDM offset %llu (dmaid=0, non-trigger)\n",
           BF_DATA_PATH, (unsigned long long)OFFSET_BF);
    rc = mnc2_send(dev, bfbuf, OFFSET_BF, BF_BYTES, 0);
    if (rc != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: mnc2_send boundary_flags returned %d\n", rc);
        goto cleanup;
    }

    /* send input (trigger) */
    printf("[send] input -> PDM offset %d (dmaid=0x%02x, trigger)\n",
           (int)OFFSET_IN, DMAID_TRIGGER);
    memcpy(sendbuf, input, ELEM_BYTES);
    rc = mnc2_send(dev, sendbuf, OFFSET_IN, ELEM_BYTES, DMAID_TRIGGER);
    if (rc != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: mnc2_send returned %d\n", rc);
        goto cleanup;
    }

    /* exec kernel */
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

    /* recv output */
    printf("[recv] output <- PDM offset %llu\n", (unsigned long long)OFFSET_OUT);
    memset(recvbuf, 0, ELEM_BYTES);
    rc = mnc2_recv(dev, recvbuf, OFFSET_OUT, ELEM_BYTES, WD_RECV);
    if (rc != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: mnc2_recv returned %d\n", rc);
        goto cleanup;
    }

    /* verify: output[i] == input[i-1] + input[i] + input[i+1] for internal PEs */
    {
        double *rp = (double *)recvbuf;
        int errors = 0;

        /* boundary PEs: report only */
        printf("  [boundary] PE0:    got=%g\n", rp[0]);
        printf("  [boundary] PE4095: got=%g\n", rp[ELEM_COUNT - 1]);

        /* internal PEs: strict check */
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
            fprintf(stderr, "  FAIL: %d mismatches (internal PEs)\n", errors);
            rc = 1;
        } else {
            printf("  PASS: 3-point sum correct for %d internal PEs\n",
                   ELEM_COUNT - 2);
            rc = 0;
        }
    }

cleanup:
    mnc2_free_host_buffer(dev, sendbuf, ELEM_BYTES);
    mnc2_free_host_buffer(dev, recvbuf, ELEM_BYTES);
    mnc2_free_host_buffer(dev, bfbuf, BF_BYTES);
    mnc2_close(dev);
    free(input);
    return (rc != 0) ? 1 : 0;
}
