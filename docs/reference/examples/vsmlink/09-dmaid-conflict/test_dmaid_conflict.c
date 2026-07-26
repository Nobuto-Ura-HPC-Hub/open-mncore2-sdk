/* test_dmaid_conflict.c — dmaid=0x23 タグ衝突の検証
 *
 * @distribute を含むカーネルでは、mvp done flag として 0x23 を内部使用する。
 * ホスト側 send で dmaid=0x23 を渡すとタグが衝突し、recv が失敗する。
 *
 * このテストは以下の 2 ケースを実行する:
 *   1. dmaid=0x10 → PASS（衝突なし）
 *   2. dmaid=0x23 → FAIL expected（mvp done flag と衝突）
 *
 * カーネル: 01-roundtrip と同一（PDM -> LM -> PDM）
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mnc2.h"

#define ELEM_COUNT  4096
#define ELEM_BYTES  (ELEM_COUNT * sizeof(double))
#define OFFSET_IN   0
#define OFFSET_OUT  (131072ULL * 8)
#define WD_RECV     0x1e  /* .param recv_wait_tag と対応 */

static int run_roundtrip(mnc2_device_t dev, int dmaid, const char *label)
{
    printf("--- %s: dmaid=0x%02x ---\n", label, dmaid);

    void *sendbuf = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    void *recvbuf = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    if (!sendbuf || !recvbuf) {
        fprintf(stderr, "  FAIL: alloc\n");
        return -1;
    }
    memset(recvbuf, 0, ELEM_BYTES);

    double *dp = (double *)sendbuf;
    for (int i = 0; i < ELEM_COUNT; i++)
        dp[i] = (double)(i + 1) * 1.5;

    int rc = mnc2_send(dev, sendbuf, OFFSET_IN, ELEM_BYTES,
                       dmaid);
    if (rc != MNC2_SUCCESS) {
        fprintf(stderr, "  send failed: %d\n", rc);
        goto done;
    }

    mnc2_kernel_t kernel = mnc2_load_kernel(dev, "_build/roundtrip.idma.dat");
    if (!kernel) { fprintf(stderr, "  FAIL: load_kernel\n"); rc = -1; goto done; }
    rc = mnc2_exec_kernel(kernel);
    mnc2_free_kernel(kernel);
    if (rc != MNC2_SUCCESS) {
        fprintf(stderr, "  exec failed: %d\n", rc);
        goto done;
    }

    rc = mnc2_recv(dev, recvbuf, OFFSET_OUT, ELEM_BYTES,
                   WD_RECV);
    if (rc != MNC2_SUCCESS) {
        fprintf(stderr, "  recv failed: %d\n", rc);
        goto done;
    }

    double *rp = (double *)recvbuf;
    int errors = 0;
    for (int i = 0; i < ELEM_COUNT; i++) {
        if (rp[i] != (double)(i + 1) * 1.5) errors++;
    }
    if (errors) {
        fprintf(stderr, "  data mismatch: %d / %d\n", errors, ELEM_COUNT);
        rc = -1;
    }

done:
    mnc2_free_host_buffer(dev, sendbuf, ELEM_BYTES);
    mnc2_free_host_buffer(dev, recvbuf, ELEM_BYTES);
    return rc;
}

int main(void)
{
    printf("=== dmaid conflict test ===\n\n");

    mnc2_device_t dev = mnc2_open(0);
    if (!dev) { fprintf(stderr, "FAIL: mnc2_open\n"); return 1; }

    /* Case 1: dmaid=0x10 — should PASS */
    int rc1 = run_roundtrip(dev, 0x10, "safe");
    int pass1 = (rc1 == MNC2_SUCCESS);
    printf("  result: %s\n\n", pass1 ? "PASS" : "FAIL");

    /* Case 2: dmaid=0x23 — should FAIL (tag conflict with mvp done flag) */
    int rc2 = run_roundtrip(dev, 0x23, "conflict");
    int fail2 = (rc2 != MNC2_SUCCESS);
    printf("  result: %s (expected FAIL)\n\n", fail2 ? "FAIL as expected" : "unexpected PASS");

    /* 残留 DMA をリセットして次のテストに影響しないようにする */
    mnc2_reset(dev);

    mnc2_close(dev);

    if (pass1 && fail2) {
        printf("PASS: dmaid=0x10 succeeded, dmaid=0x23 correctly failed\n");
        return 0;
    } else {
        fprintf(stderr, "UNEXPECTED: pass1=%d fail2=%d\n", pass1, fail2);
        return 1;
    }
}
