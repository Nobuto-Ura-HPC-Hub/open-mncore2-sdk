/* test_boundary_collect.c — 各 PE の $subpeid/$mabid/$l1bid をホスト側に回収する
 *
 * boundary-collect.idma.dat を emu:lib で実行し、
 * 3 つの出力バッファ（$subpeid, $mabid, $l1bid）を PDM0 から読み出す。
 *
 * PDM レイアウト (boundary-collect.param):
 *   slot 8:  addr 0    — $subpeid (4096 LW)
 *   slot 16: addr 4096 — $mabid   (4096 LW)
 *   slot 24: addr 8192 — $l1bid   (4096 LW)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "mnc2.h"

#define NUM_PES      4096
#define ELEM_BYTES   (NUM_PES * sizeof(uint64_t))

/* PDM アドレス (LW 単位) → バイト単位 */
#define OFFSET_SUBPEID  (0ULL * 8)
#define OFFSET_MABID    (4096ULL * 8)
#define OFFSET_L1BID    (8192ULL * 8)

/* 各 @collect の recv_wait_tag は .param で個別指定する (boundary-collect.param)。
   3 つすべて同じ tag だと device で 2 回目以降の recv が DMA timeout する。 */
#define WD_RECV_SUBPEID  0x1e
#define WD_RECV_MABID    0x1f
#define WD_RECV_L1BID    0x20

int main(void)
{
    printf("=== boundary-collect: $subpeid/$mabid/$l1bid 回収 ===\n\n");

    /* 1. デバイスオープン */
    mnc2_device_t dev = mnc2_open(0);
    if (!dev) {
        fprintf(stderr, "FAIL: mnc2_open\n");
        return 1;
    }

    /* 2. バッファ確保 */
    void *buf_subpeid = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    void *buf_mabid   = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    void *buf_l1bid   = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    if (!buf_subpeid || !buf_mabid || !buf_l1bid) {
        fprintf(stderr, "FAIL: mnc2_alloc_host_buffer\n");
        mnc2_close(dev);
        return 1;
    }
    memset(buf_subpeid, 0, ELEM_BYTES);
    memset(buf_mabid, 0, ELEM_BYTES);
    memset(buf_l1bid, 0, ELEM_BYTES);

    /* 3. カーネル実行（入力なし: distribute なし） */
    mnc2_kernel_t kernel = mnc2_load_kernel(dev, "_build/boundary-collect.idma.dat");
    if (!kernel) {
        fprintf(stderr, "FAIL: mnc2_load_kernel\n");
        goto fail;
    }

    int rc = mnc2_exec_kernel(kernel);
    mnc2_free_kernel(kernel);
    if (rc != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: mnc2_exec_kernel: %d\n", rc);
        goto fail;
    }

    /* 4. PDM → Host: 3 バッファを順に recv */
    rc = mnc2_recv(dev, buf_subpeid, OFFSET_SUBPEID, ELEM_BYTES,
                   WD_RECV_SUBPEID);
    if (rc != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: mnc2_recv subpeid: %d\n", rc);
        goto fail;
    }

    rc = mnc2_recv(dev, buf_mabid, OFFSET_MABID, ELEM_BYTES,
                   WD_RECV_MABID);
    if (rc != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: mnc2_recv mabid: %d\n", rc);
        goto fail;
    }

    rc = mnc2_recv(dev, buf_l1bid, OFFSET_L1BID, ELEM_BYTES,
                   WD_RECV_L1BID);
    if (rc != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: mnc2_recv l1bid: %d\n", rc);
        goto fail;
    }

    /* 5. 結果表示 */
    uint64_t *sp = (uint64_t *)buf_subpeid;
    uint64_t *mp = (uint64_t *)buf_mabid;
    uint64_t *lp = (uint64_t *)buf_l1bid;

    printf("  PE[0..7] subpeid: ");
    for (int i = 0; i < 8; i++) printf("%lu ", (unsigned long)sp[i]);
    printf("\n");

    printf("  PE[0..7] mabid:   ");
    for (int i = 0; i < 8; i++) printf("%lu ", (unsigned long)mp[i]);
    printf("\n");

    printf("  PE[0..7] l1bid:   ");
    for (int i = 0; i < 8; i++) printf("%lu ", (unsigned long)lp[i]);
    printf("\n\n");

    /* 6. サニティチェック: PE[0] = subpeid=0, PE[1] = subpeid=1, ... */
    int errors = 0;
    for (int i = 0; i < NUM_PES; i++) {
        int expected_subpeid = i % 4;
        if ((int)sp[i] != expected_subpeid) {
            if (errors < 5)
                fprintf(stderr, "  MISMATCH subpeid[%d]: got=%lu expected=%d\n",
                        i, (unsigned long)sp[i], expected_subpeid);
            errors++;
        }
    }

    if (errors > 0) {
        fprintf(stderr, "  subpeid: %d mismatches\n", errors);
    } else {
        printf("  subpeid: all %d elements match expected pattern\n", NUM_PES);
    }

    mnc2_free_host_buffer(dev, buf_subpeid, ELEM_BYTES);
    mnc2_free_host_buffer(dev, buf_mabid, ELEM_BYTES);
    mnc2_free_host_buffer(dev, buf_l1bid, ELEM_BYTES);
    mnc2_close(dev);
    return (errors > 0) ? 1 : 0;

fail:
    mnc2_free_host_buffer(dev, buf_subpeid, ELEM_BYTES);
    mnc2_free_host_buffer(dev, buf_mabid, ELEM_BYTES);
    mnc2_free_host_buffer(dev, buf_l1bid, ELEM_BYTES);
    mnc2_close(dev);
    return 1;
}
