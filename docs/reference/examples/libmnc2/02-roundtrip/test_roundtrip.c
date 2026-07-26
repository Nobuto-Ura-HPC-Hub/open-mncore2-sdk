/* test_roundtrip.c — PDM -> PE(LM) -> PDM roundtrip 検証
 *
 * roundtrip カーネルを使い、送信データと受信データが一致することを確認する。
 * カーネルは計算なし (distribute -> LM -> collect)。
 *
 * PDM レイアウト (roundtrip.param):
 *   入力: PDM word 0..4095       — byte offset 0
 *   出力: PDM word 131072..135167 — byte offset 1048576
 *
 * DMA タグ:
 *   send_wait_tag=0x10  (カーネル内 wait i10)
 *   recv_wait_tag=0x1e  (roundtrip.vsm: collect → PDM の mvp done flag)
 *
 * 環境変数:
 *   MNC2_EMU_CONFIG — config.json のパス (config_pdm0_roundtrip.json を使用)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mnc2.h"

#define ELEM_COUNT  4096
#define ELEM_BYTES  (ELEM_COUNT * sizeof(double))
#define OFFSET_IN   0                       /* PDM word 0      → byte 0       */
#define OFFSET_OUT  (131072ULL * 8)         /* PDM word 131072 → byte 1048576 */
#define SEND_TAG  0x10                    /* カーネル内 wait i10 のトリガー */
#define RECV_TAG     0x1e   /* roundtrip.vsm: collect → PDM の mvp done flag */

int main(void)
{
    printf("=== roundtrip: PDM -> PE(LM) -> PDM [%d doubles] ===\n\n",
           ELEM_COUNT);

    /* 1. デバイスオープン */
    mnc2_device_t dev = mnc2_open(0);
    if (dev == NULL) {
        printf("FAIL: mnc2_open returned NULL\n");
        printf("  MNC2_EMU_CONFIG=%s\n",
                getenv("MNC2_EMU_CONFIG") ? getenv("MNC2_EMU_CONFIG") : "(unset)");
        return 1;
    }

    printf("  backend: %s\n", mnc2_get_backend_name(dev));
    printf("  offset_in: %d, offset_out: %llu\n",
           OFFSET_IN, (unsigned long long)OFFSET_OUT);
    printf("  send_tag: 0x%02x, recv_tag: 0x%02x\n\n", SEND_TAG, RECV_TAG);

    /* 2. バッファ確保 */
    void *sendbuf = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    void *recvbuf = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    if (sendbuf == NULL || recvbuf == NULL) {
        printf("FAIL: mnc2_alloc_host_buffer\n");
        mnc2_close(dev);
        return 1;
    }
    memset(recvbuf, 0, ELEM_BYTES);

    /* 3. テストデータ準備 */
    double *dp = (double *)sendbuf;
    for (int i = 0; i < ELEM_COUNT; i++)
        dp[i] = (double)(i + 1) * 1.5;

    printf("  send[0..3]: %.1f %.1f %.1f %.1f\n", dp[0], dp[1], dp[2], dp[3]);

    /* 4. Host → PDM */
    int rc = mnc2_send(dev, sendbuf, OFFSET_IN, ELEM_BYTES, SEND_TAG);
    if (rc != MNC2_SUCCESS) {
        printf("FAIL: mnc2_send returned %d\n", rc);
        goto cleanup;
    }

    /* 5. カーネル実行 (roundtrip: distribute → LM → collect) */
    mnc2_kernel_t kernel = mnc2_load_kernel(dev, "_build/roundtrip.idma.dat");
    if (kernel == NULL) {
        printf("FAIL: mnc2_load_kernel\n");
        rc = 1;
        goto cleanup;
    }

    rc = mnc2_exec_kernel(kernel);
    mnc2_free_kernel(kernel);
    if (rc != MNC2_SUCCESS) {
        printf("FAIL: mnc2_exec_kernel returned %d\n", rc);
        goto cleanup;
    }

    /* 6. PDM → Host */
    rc = mnc2_recv(dev, recvbuf, OFFSET_OUT, ELEM_BYTES, RECV_TAG);
    if (rc != MNC2_SUCCESS) {
        printf("FAIL: mnc2_recv returned %d\n", rc);
        goto cleanup;
    }

    /* 7. 検証: 入力 == 出力 */
    double *rp = (double *)recvbuf;
    printf("  recv[0..3]: %.1f %.1f %.1f %.1f\n", rp[0], rp[1], rp[2], rp[3]);

    int errors = 0;
    for (int i = 0; i < ELEM_COUNT; i++) {
        double expected = (double)(i + 1) * 1.5;
        if (rp[i] != expected) {
            if (errors < 5)
                printf("  MISMATCH [%d]: got=%g expected=%g\n",
                        i, rp[i], expected);
            errors++;
        }
    }

    if (errors > 0) {
        printf("FAIL: %d mismatches\n", errors);
        rc = 1;
    } else {
        printf("\nALL PASS\n");
        rc = 0;
    }

cleanup:
    mnc2_free_host_buffer(dev, sendbuf, ELEM_BYTES);
    mnc2_free_host_buffer(dev, recvbuf, ELEM_BYTES);
    mnc2_close(dev);
    return (rc != 0) ? 1 : 0;
}
