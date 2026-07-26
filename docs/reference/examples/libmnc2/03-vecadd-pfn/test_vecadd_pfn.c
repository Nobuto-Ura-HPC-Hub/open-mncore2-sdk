/* test_vecadd_pfn.c — vecadd DMA パターンテスト (PFN 規模版, emu:process)
 *
 * PFN 提供 vecadd.vsm (64 要素) を使った DMA パターンテスト。
 * カーネルは PDM offset 0 の 64 個の uint64_t を 2 倍にして書き戻す
 * (同一データを M, N レジスタにロードし ladd = 整数加算)。
 *
 * 正式規模 (4096 PE) のテストは test_vecadd.c を参照。
 *
 * DMA タグは vecadd.vsm の定義に合わせる:
 *   send_wait_tag=1  (カーネル内 wait i01)
 *   recv_wait_tag=2     (カーネル内 mvp/i02)
 *
 * 環境変数:
 *   MNC2_EMU_CONFIG — config.json のパス
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "mnc2.h"

#define ELEM_COUNT  64                              /* vecadd.vsm: n64 */
#define ELEM_BYTES  (ELEM_COUNT * sizeof(uint64_t)) /* 512 バイト */

/* vecadd.vsm の DMA タグ */
#define SEND_TAG  1   /* カーネル内 wait i01 */
#define RECV_TAG     2   /* カーネル内 mvp/i02 */

int main(void)
{
    /* 1. デバイスオープン */
    mnc2_device_t dev = mnc2_open(0);
    if (dev == NULL) {
        printf("FAIL: mnc2_open returned NULL\n");
        printf("  MNC2_EMU_CONFIG=%s\n",
                getenv("MNC2_EMU_CONFIG") ? getenv("MNC2_EMU_CONFIG") : "(unset)");
        return 1;
    }

    printf("=== vecadd DMA パターンテスト PFN 規模版 [%s] ===\n\n",
           mnc2_get_backend_name(dev));

    printf("  elements: %d uint64_t (%zu bytes)\n", ELEM_COUNT, ELEM_BYTES);
    printf("  send_tag: %d (vecadd.vsm wait i01)\n", SEND_TAG);
    printf("  recv_tag:    %d (vecadd.vsm mvp/i02)\n", RECV_TAG);
    printf("\n");

    /* 2. DMA バッファ確保 */
    void* sendbuf = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    void* recvbuf = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    if (sendbuf == NULL || recvbuf == NULL) {
        printf("FAIL: mnc2_alloc_host_buffer\n");
        mnc2_close(dev);
        return 1;
    }

    /* 3. vecadd カーネル読み込み */
    mnc2_kernel_t kernel = mnc2_load_kernel(dev, "_build/vecadd.idma.dat");
    if (kernel == NULL) {
        printf("FAIL: mnc2_load_kernel(vecadd.asm)\n");
        mnc2_free_host_buffer(dev, sendbuf, ELEM_BYTES);
        mnc2_free_host_buffer(dev, recvbuf, ELEM_BYTES);
        mnc2_close(dev);
        return 1;
    }

    /* 元データ: 整数 1, 2, ..., 64 */
    uint64_t orig[ELEM_COUNT];
    for (int i = 0; i < ELEM_COUNT; i++)
        orig[i] = (uint64_t)(i + 1);

    int rc;

    /* 4. 配列を送信: Host → PDM offset 0 */
    printf("[send] a → PDM offset 0 (send_wait_tag=%d)\n", SEND_TAG);
    memcpy(sendbuf, orig, ELEM_BYTES);
    rc = mnc2_send(dev, sendbuf, 0, ELEM_BYTES, SEND_TAG);
    if (rc != MNC2_SUCCESS) {
        printf("  FAIL: mnc2_send returned %d\n", rc);
        goto cleanup;
    }

    /* 5. カーネル実行 (a[i] → 2*a[i], 整数加算) */
    printf("[exec] vecadd kernel\n");
    rc = mnc2_exec_kernel(kernel);
    if (rc != MNC2_SUCCESS) {
        printf("  FAIL: mnc2_exec_kernel returned %d\n", rc);
        goto cleanup;
    }

    /* 6. 結果を受信: PDM offset 0 → Host */
    printf("[recv] result ← PDM offset 0 (recv_wait_tag=%d)\n", RECV_TAG);
    memset(recvbuf, 0, ELEM_BYTES);
    rc = mnc2_recv(dev, recvbuf, 0, ELEM_BYTES, RECV_TAG);
    if (rc != MNC2_SUCCESS) {
        printf("  FAIL: mnc2_recv returned %d\n", rc);
        goto cleanup;
    }

    /* 7. 検証: result[i] == 2 * orig[i] */
    {
        uint64_t* rp = (uint64_t*)recvbuf;
        int errors = 0;

        for (int i = 0; i < ELEM_COUNT; i++) {
            uint64_t expected = orig[i] * 2;
            if (rp[i] != expected) {
                if (errors < 3)
                    printf("  MISMATCH [%d]: got=%lu expected=%lu\n",
                            i, (unsigned long)rp[i], (unsigned long)expected);
                errors++;
            }
        }

        if (errors > 0)
            printf("  result: %d mismatches\n", errors);
        else
            printf("  result: OK (all values doubled)\n");

        printf("\n");
        if (errors == 0)
            printf("ALL PASS\n");
        else
            printf("FAILED: %d errors\n", errors);

        rc = (errors > 0) ? 1 : 0;
    }

cleanup:
    mnc2_free_kernel(kernel);
    mnc2_free_host_buffer(dev, sendbuf, ELEM_BYTES);
    mnc2_free_host_buffer(dev, recvbuf, ELEM_BYTES);
    mnc2_close(dev);
    return rc;
}
