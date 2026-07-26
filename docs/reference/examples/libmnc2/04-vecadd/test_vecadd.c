/* test_vecadd.c — vecadd DMA パターンテスト (emu:process)
 *
 * double-vecadd カーネル (4096 PE, dvadd FP 加算) を使った DMA パターンテスト:
 *   - 2 入力配列 (a, b) を異なる PDM アドレスに send
 *   - カーネル実行 (dvadd: c = a + b)
 *   - 出力配列 c を recv して a[i] + b[i] == c[i] を検証
 *
 * 4096 doubles (32,768 バイト) × 3 配列 (a, b, c)。
 *
 * PDM レイアウト (double-vecadd.vsm から抽出):
 *   buf1 (a): PDM[0..4095]      — byte offset 0
 *   buf2 (b): PDM[4096..8191]   — byte offset 32768
 *   buf3 (c): PDM[131072..135167] — byte offset 1048576
 *
 * DMA タグ (vsm-to-config --dmaid 10 で生成):
 *   send_wait_tag=0x10  (カーネル内 wait i10)
 *   recv_wait_tag=0x1e     (カーネル出力 MVP 最終タグ)
 *
 * emu:process 注意: カーネルの c 行が tag i10 を設定するため、
 *   send 側 send_wait_tag は 0 (タグなし) にする必要がある。
 *   同じタグの二重 assertion はエミュレータがエラーにする。
 *
 * 環境変数:
 *   MNC2_EMU_CONFIG — config.json のパス (config_pdm0_vecadd.json を使用)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mnc2.h"

#define ELEM_COUNT  4096                            /* 4096 doubles */
#define ELEM_BYTES  (ELEM_COUNT * sizeof(double))   /* 32,768 バイト */
#define OFFSET_A    0                               /* buf1: PDM word 0 */
#define OFFSET_B    (4096 * 8)                      /* buf2: PDM word 4096 */
#define OFFSET_C    (131072ULL * 8)                 /* buf3: PDM word 131072 */

/* double-vecadd.vsm の DMA タグ (vsm-to-config --dmaid 10) */
#define SEND_TAG  0x10  /* カーネル内 wait i10 — 最後の send のみ使用 */
#define RECV_TAG     0x1e   /* double-vecadd.vsm: collect → PDM の mvp done flag */

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

    printf("=== vecadd DMA パターンテスト [%s] ===\n\n",
           mnc2_get_backend_name(dev));

    printf("  elements: %d doubles (%zu bytes)\n", ELEM_COUNT, ELEM_BYTES);
    printf("  offset_a: %d, offset_b: %d, offset_c: %llu\n",
           OFFSET_A, (int)OFFSET_B, (unsigned long long)OFFSET_C);
    printf("  send_tag: 0x%02x (最後の send のみ)\n", SEND_TAG);
    printf("  recv_tag:    0x%02x (recv)\n", RECV_TAG);
    printf("\n");

    /* 2. DMA バッファ確保
     *    送信は a, b で別 buffer。同一 buffer を使い回すと、async kick の
     *    1 回目 send (in-flight) 中に host が memcpy で上書きしてデータ破壊。 */
    void* sendbuf_a = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    void* sendbuf_b = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    void* recvbuf   = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    if (sendbuf_a == NULL || sendbuf_b == NULL || recvbuf == NULL) {
        printf("FAIL: mnc2_alloc_host_buffer\n");
        if (sendbuf_a) mnc2_free_host_buffer(dev, sendbuf_a, ELEM_BYTES);
        if (sendbuf_b) mnc2_free_host_buffer(dev, sendbuf_b, ELEM_BYTES);
        if (recvbuf)   mnc2_free_host_buffer(dev, recvbuf,   ELEM_BYTES);
        mnc2_close(dev);
        return 1;
    }

    /* 3. カーネル読み込み (double-vecadd: dvadd FP 加算)
     *    .idma.dat は asm3_loader が c 行を分離済み (命令のみ)。
     *    emu:process では mnc2_load_kernel が .asm を自動検索する。 */
    mnc2_kernel_t kernel = mnc2_load_kernel(dev, "_build/double-vecadd.idma.dat");
    if (kernel == NULL) {
        printf("FAIL: mnc2_load_kernel(double-vecadd.asm)\n");
        mnc2_free_host_buffer(dev, sendbuf_a, ELEM_BYTES);
        mnc2_free_host_buffer(dev, sendbuf_b, ELEM_BYTES);
        mnc2_free_host_buffer(dev, recvbuf,   ELEM_BYTES);
        mnc2_close(dev);
        return 1;
    }

    /* 元データ (ヒープ確保: スタック溢れ防止) */
    double* orig_a = (double*)malloc(ELEM_BYTES);
    double* orig_b = (double*)malloc(ELEM_BYTES);
    if (orig_a == NULL || orig_b == NULL) {
        printf("FAIL: malloc for orig data\n");
        mnc2_free_kernel(kernel);
        mnc2_free_host_buffer(dev, sendbuf_a, ELEM_BYTES);
        mnc2_free_host_buffer(dev, sendbuf_b, ELEM_BYTES);
        mnc2_free_host_buffer(dev, recvbuf,   ELEM_BYTES);
        mnc2_close(dev);
        free(orig_a);
        free(orig_b);
        return 1;
    }
    for (int i = 0; i < ELEM_COUNT; i++) {
        orig_a[i] = (double)(i + 1);         /* a[i] = i+1 */
        orig_b[i] = (double)(i + 1) * 10.0;  /* b[i] = (i+1)*10 */
    }

    int rc;
    int total_errors = 0;

    /* 4. 配列 b を先に送信 (非トリガー): Host → PDM buf2 (offset 32768)
     *    send_wait_tag=0: 非トリガー DMA。HW 同期モデルでは非トリガーを先に送る。 */
    printf("[send] b → PDM offset %d (send_wait_tag=0, non-trigger)\n", (int)OFFSET_B);
    memcpy(sendbuf_b, orig_b, ELEM_BYTES);
    rc = mnc2_send(dev, sendbuf_b, OFFSET_B, ELEM_BYTES, 0);
    if (rc != MNC2_SUCCESS) {
        printf("  FAIL: mnc2_send(b) returned %d\n", rc);
        goto cleanup;
    }

    /* 5. 配列 a を送信 (トリガー): Host → PDM buf1 (offset 0)
     *    send_wait_tag=0x10: カーネルの wait i10 をトリガーする。 */
    printf("[send] a → PDM offset %d (send_wait_tag=0x%02x, trigger)\n",
           OFFSET_A, SEND_TAG);
    memcpy(sendbuf_a, orig_a, ELEM_BYTES);
    rc = mnc2_send(dev, sendbuf_a, OFFSET_A, ELEM_BYTES, SEND_TAG);
    if (rc != MNC2_SUCCESS) {
        printf("  FAIL: mnc2_send(a) returned %d\n", rc);
        goto cleanup;
    }

    /* 6. カーネル実行 (dvadd: c = a + b) */
    printf("[exec] double-vecadd kernel\n");
    rc = mnc2_exec_kernel(kernel);
    if (rc != MNC2_SUCCESS) {
        printf("  FAIL: mnc2_exec_kernel returned %d\n", rc);
        goto cleanup;
    }

    /* 7. 結果を受信: PDM buf3 (offset 1048576) → Host */
    printf("[recv] c ← PDM offset %llu (recv_wait_tag=0x%02x)\n",
           (unsigned long long)OFFSET_C, RECV_TAG);
    memset(recvbuf, 0, ELEM_BYTES);
    rc = mnc2_recv(dev, recvbuf, OFFSET_C, ELEM_BYTES, RECV_TAG);
    if (rc != MNC2_SUCCESS) {
        printf("  FAIL: mnc2_recv(c) returned %d\n", rc);
        goto cleanup;
    }

    /* 8. 検証: c[i] == a[i] + b[i] */
    {
        double* rp = (double*)recvbuf;

        for (int i = 0; i < ELEM_COUNT; i++) {
            double expected = orig_a[i] + orig_b[i];
            if (rp[i] != expected) {
                if (total_errors < 5)
                    printf("  MISMATCH c[%d]: got=%g expected=%g\n",
                            i, rp[i], expected);
                total_errors++;
            }
        }

        if (total_errors > 0)
            printf("  result: %d mismatches\n", total_errors);
        else
            printf("  result: OK (c[i] == a[i] + b[i] for all i)\n");
    }

    /* 9. 結果 */
    printf("\n");
    if (total_errors == 0)
        printf("ALL PASS\n");
    else
        printf("FAILED: %d errors\n", total_errors);

cleanup:
    mnc2_free_kernel(kernel);
    mnc2_free_host_buffer(dev, sendbuf_a, ELEM_BYTES);
    mnc2_free_host_buffer(dev, sendbuf_b, ELEM_BYTES);
    mnc2_free_host_buffer(dev, recvbuf,   ELEM_BYTES);
    mnc2_close(dev);
    free(orig_a);
    free(orig_b);
    return (rc != MNC2_SUCCESS || total_errors > 0) ? 1 : 0;
}
