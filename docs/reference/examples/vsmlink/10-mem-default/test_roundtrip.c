/* test_roundtrip.c — PDM -> PE(LM) -> PDM roundtrip 検証
 *
 * vsmlink + assemble3 で生成したカーネルを emu:lib で実行し、
 * 送信データと受信データが一致することを確認する。
 *
 * PDM レイアウト (roundtrip.param):
 *   入力: PDM[0..3584]      — byte offset 0
 *   出力: PDM[131072..134656] — byte offset 1048576
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "mnc2.h"

#define ELEM_COUNT  4096
#define ELEM_BYTES  (ELEM_COUNT * sizeof(double))
#define OFFSET_IN   0                       /* PDM word 0    → byte 0       */
#define OFFSET_OUT  (131072ULL * 8)         /* PDM word 131072 → byte 1048576 */
#define DMAID_SEND  0x10                    /* カーネル内 wait i10 のトリガー */
#define WD_RECV     0x1e  /* .param recv_wait_tag と対応 */       /* collect 最終 wait tag        */

int main(void)
{
    printf("=== roundtrip: PDM -> PE(LM) -> PDM ===\n\n");

    /* 1. デバイスオープン (emu:lib) */
    mnc2_device_t dev = mnc2_open(0);
    if (dev == NULL) {
        fprintf(stderr, "FAIL: mnc2_open returned NULL\n");
        return 1;
    }

    /* 2. バッファ確保 */
    void *sendbuf = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    void *recvbuf = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    if (sendbuf == NULL || recvbuf == NULL) {
        fprintf(stderr, "FAIL: mnc2_alloc_host_buffer\n");
        mnc2_close(dev);
        return 1;
    }
    memset(recvbuf, 0, ELEM_BYTES);

    /* 3. テストデータ準備 */
    double *dp = (double *)sendbuf;
    for (int i = 0; i < ELEM_COUNT; i++)
        dp[i] = (double)(i + 1) * 1.5;

    printf("  send[0..3]: %.1f %.1f %.1f %.1f\n",
           dp[0], dp[1], dp[2], dp[3]);

    /* 4. Host -> PDM */
    int rc = mnc2_send(dev, sendbuf, OFFSET_IN, ELEM_BYTES,
                       DMAID_SEND);
    if (rc != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: mnc2_send returned %d\n", rc);
        goto cleanup;
    }

    /* 5. カーネル実行 (roundtrip: distribute -> LM -> collect) */
    mnc2_kernel_t kernel = mnc2_load_kernel(dev, "_build/roundtrip.idma.dat");
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

    /* 6. PDM -> Host */
    rc = mnc2_recv(dev, recvbuf, OFFSET_OUT, ELEM_BYTES,
                   WD_RECV);
    if (rc != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: mnc2_recv returned %d\n", rc);
        goto cleanup;
    }

    /* 7. 検証: 入力 == 出力 */
    double *rp = (double *)recvbuf;
    printf("  recv[0..3]: %.1f %.1f %.1f %.1f\n",
           rp[0], rp[1], rp[2], rp[3]);

    int errors = 0;
    for (int i = 0; i < ELEM_COUNT; i++) {
        double expected = (double)(i + 1) * 1.5;
        if (rp[i] != expected) {
            if (errors < 5)
                fprintf(stderr, "  MISMATCH [%d]: got=%g expected=%g\n",
                        i, rp[i], expected);
            errors++;
        }
    }

    if (errors > 0) {
        fprintf(stderr, "  FAIL: %d mismatches\n", errors);
        rc = 1;
    } else {
        printf("  PASS: all %d elements match\n", ELEM_COUNT);
        rc = 0;
    }

cleanup:
    mnc2_free_host_buffer(dev, sendbuf, ELEM_BYTES);
    mnc2_free_host_buffer(dev, recvbuf, ELEM_BYTES);
    mnc2_close(dev);
    return (rc != 0) ? 1 : 0;
}
