/* test_broadcast.c -- broadcast の E2E テスト (emu:lib)
 *
 * out[id] = v + id on 4096 PEs。
 *   v  = broadcast(_bc)    全 PE 同値（host が broadcast 入力の index 12..15 を V で埋める）
 *   id = get_global_id(0)  PE ごとの ID [0..4095]（@identify。host が id 表を送る）
 *
 * PDM layout (broadcast.param):
 *   slot 8      (_bc,  broadcast 入力): PDM word 0     -- host が index 12..15 を V で埋める
 *   identify 0  (id 表):                PDM word 4096
 *   slot 16     (_out, collect 出力):   PDM word 8192
 *
 * broadcast の HW 特性: @broadcast は PDM の index 12..15 の 4 u64 を MAB 内 4 PE
 * (sub_pe_id 0..3) に配る。全 PE 同値にするには 12..15 を同値にする（ここでは broadcast
 * 入力バッファ全体を V で埋めるので 12..15 も V になる）。詳細は
 * _mncore2-sdk-v1/share/examples/vsmlink/25-broadcast-reduce-add/README.md
 *
 * send のトリガ順（13-odd-even-sort-gid と同じ考え方）:
 *   broadcast 入力を tag 0（非トリガ）で送り、id 表を tag 0x10（トリガ）で送る。
 *   カーネルは tag 0x10 の完了を待って起動する。id 表の DMA 完了時に broadcast 入力も
 *   発行済み・完了しているので、同一 tag を 2 度立てずに両方の到着を保証できる。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "mnc2.h"

#define ELEM_COUNT    4096
#define ELEM_BYTES    (ELEM_COUNT * sizeof(int64_t))
#define BC_LW         64                         /* mvb/n64 の最小転送単位（64 u64） */
#define BC_BYTES      (BC_LW * sizeof(int64_t))
#define OFFSET_BC     0                          /* slot 8:     PDM word 0    */
#define OFFSET_IDS    (4096ULL * 8)              /* identify 0: PDM word 4096 */
#define OFFSET_OUT    (8192ULL * 8)              /* slot 16:    PDM word 8192 */
#define SEND_WAIT_TAG 0x10
#define RECV_TAG      0x1e   /* vsmlink + asm3 が割り当てる collect の done tag */

/* 全 PE に配る値。32-bit を越える値にして i64 の両バンク(64-bit)を実際に使う */
#define BCAST_VALUE   ((int64_t)0x0000000700000005LL)

int main(void)
{
    printf("=== 15-broadcast: out[id] = v + id (v は全 PE 同値, 4096 PE) ===\n\n");

    mnc2_device_t dev = mnc2_open(0);
    if (dev == NULL) {
        fprintf(stderr, "FAIL: mnc2_open returned NULL\n");
        return 1;
    }
    printf("15-broadcast backend: %s\n", mnc2_get_backend_name(dev));

    void *bcbuf   = mnc2_alloc_host_buffer(dev, BC_BYTES);
    void *idbuf   = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    void *recvbuf = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    if (bcbuf == NULL || idbuf == NULL || recvbuf == NULL) {
        fprintf(stderr, "FAIL: mnc2_alloc_host_buffer\n");
        mnc2_close(dev);
        return 1;
    }

    int rc;

    /* broadcast 入力: バッファ全体を V で埋める（index 12..15 が全 PE に届く）。非トリガ */
    {
        int64_t *bp = (int64_t *)bcbuf;
        for (int i = 0; i < BC_LW; i++) bp[i] = BCAST_VALUE;
    }
    printf("[send] broadcast 入力 (v=0x%llx) -> PDM offset %d (dmaid=0)\n",
           (unsigned long long)BCAST_VALUE, (int)OFFSET_BC);
    rc = mnc2_send(dev, bcbuf, OFFSET_BC, BC_BYTES, 0);
    if (rc != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: mnc2_send(broadcast) returned %d\n", rc);
        goto cleanup;
    }

    /* id 表 [0..4095] を送る（トリガ）。カーネルはこの tag の完了を待って起動する */
    {
        int64_t *ip = (int64_t *)idbuf;
        for (int i = 0; i < ELEM_COUNT; i++) ip[i] = (int64_t)i;
    }
    printf("[send] id 表 [0..4095] -> PDM offset %llu (dmaid=0x%02x, trigger)\n",
           (unsigned long long)OFFSET_IDS, SEND_WAIT_TAG);
    rc = mnc2_send(dev, idbuf, OFFSET_IDS, ELEM_BYTES, SEND_WAIT_TAG);
    if (rc != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: mnc2_send(ids) returned %d\n", rc);
        goto cleanup;
    }

    /* exec kernel */
    printf("[exec] broadcast kernel\n");
    mnc2_kernel_t kernel = mnc2_load_kernel(dev, "_build/broadcast.idma.dat");
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

    /* recv out */
    printf("[recv] out <- PDM offset %llu\n", (unsigned long long)OFFSET_OUT);
    memset(recvbuf, 0, ELEM_BYTES);
    rc = mnc2_recv(dev, recvbuf, OFFSET_OUT, ELEM_BYTES, RECV_TAG);
    if (rc != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: mnc2_recv(out) returned %d\n", rc);
        goto cleanup;
    }

    /* verify: out[i] == v + i （全 PE が同じ v を受け取る） */
    {
        int64_t *rp = (int64_t *)recvbuf;
        int errors = 0;

        for (int i = 0; i < ELEM_COUNT; i++) {
            int64_t expected = BCAST_VALUE + (int64_t)i;
            if (rp[i] != expected) {
                if (errors < 5)
                    fprintf(stderr, "  MISMATCH out[%d]: got=%lld expected=%lld\n",
                            i, (long long)rp[i], (long long)expected);
                errors++;
            }
        }

        if (errors > 0) {
            fprintf(stderr, "  FAIL: %d mismatches"
                    " (不一致が i%%4 で規則的なら broadcast 入力 index 12..15 の埋め忘れを疑う)\n",
                    errors);
            rc = 1;
        } else {
            printf("  PASS: out[i] == v + i for all %d elements (v は全 PE 同値)\n", ELEM_COUNT);
            rc = 0;
        }
    }

cleanup:
    mnc2_free_host_buffer(dev, bcbuf, BC_BYTES);
    mnc2_free_host_buffer(dev, idbuf, ELEM_BYTES);
    mnc2_free_host_buffer(dev, recvbuf, ELEM_BYTES);
    mnc2_close(dev);
    return (rc != 0) ? 1 : 0;
}
