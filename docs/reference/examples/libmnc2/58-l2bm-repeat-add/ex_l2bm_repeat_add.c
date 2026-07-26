/* ex_l2bm_repeat_add.c — 58: 56/57 と同じ bijection テストを L2BM 常駐版で
 *
 * データフロー (DRAM もグループ間 PDM も使わない、L2BM が中継兼ストレージ):
 *   1. host → PDM@0 (send): u64 配列 [i << 16 for i in 0..4095]
 *   2. Kernel A (distribute): mvd PDM-L2BM 1 発で PDM@0[0..4095] を
 *      8 L2BMs に 16 LW round-robin で分散 (各 L2BM 512 LW)
 *   3. Kernel B (add1): L2BM → L1BM → LM → PE +1 (ladd) → LM → L1BM → L2BM
 *      外部 hop なし、L2BM 常駐のままで N 回繰り返し
 *   4. Kernel C (collect):
 *      a) PE compute (flat PE ID = peid|(l1bid<<6)|(l2bid<<9) を $r0 に merge)
 *      b) LM → L1BM → L2BM
 *      c) mvd L2BM-PDM 1 発で 8 L2BMs → PDM@0 結合 (= distribute の逆変換)
 *   5. host ← PDM@0 (recv): u64 配列 4096 個
 *
 * 期待ビットレイアウト (recv 値):
 *   bits [15: 0] = N      (Kernel B の繰り返し加算)
 *   bits [27:16] = i      (送信インデックス 0..4095 の一意)
 *   bits [43:32] = peid   (処理 PE の flat ID 0..4095 の一意)
 *
 * emu:process は state 保持不可なので SKIP。emu:lib / device 対応。
 *
 * Usage:
 *
 * Options:
 *   N  Kernel B (+1) の繰り返し回数 (default 1, N < 65536)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "mnc2.h"

#define BLOCK_ELEMS 4096
#define BLOCK_BYTES (BLOCK_ELEMS * sizeof(uint64_t))
#define OFFSET_OUT  0
#define SEND_TAG    2
#define RECV_TAG    6
#define OUTPUT_FILE "_build/l2bm_repeat_add.bin"

#define N_MASK    ((uint64_t)0xFFFF)
#define I_MASK    ((uint64_t)0xFFF << 16)
#define PEID_MASK ((uint64_t)0xFFF << 32)
#define ALL_MASK  (N_MASK | I_MASK | PEID_MASK)

int main(int argc, char** argv)
{
    int N = 1;
    for (int ai = 1; ai < argc; ai++) {
        if (argv[ai][0] != '-') {
            N = atoi(argv[ai]);
            if (N <= 0 || N >= 65536) {
                printf(
                    "FAIL: N must be 1..65535 (got %s, bits[15:0] にのらない)\n",
                    argv[ai]);
                return 1;
            }
        } else {
            printf("FAIL: unknown option '%s'\n", argv[ai]);
            return 1;
        }
    }

    printf("[test] L2BM repeat-add bijection (N=%d): %d elements, "
           "send i<<16, expect (N, i, peid) bijection\n",
           N, BLOCK_ELEMS);

    mnc2_device_t dev = mnc2_open(0);
    if (dev == NULL) {
        printf("FAIL: mnc2_open failed\n");
        return 1;
    }

    /* emu:process は state 保持不可なので SKIP */
    const char* backend = mnc2_get_backend_name(dev);
    if (strcmp(backend, "emu:process") == 0) {
        printf("SKIP: emu:process does not preserve L2BM state "
                "across mnc2_exec_kernel calls (kernel B needs state persistence)\n");
        mnc2_close(dev);
        return 0;
    }

    void* sbuf = mnc2_alloc_host_buffer(dev, BLOCK_BYTES);
    void* rbuf = mnc2_alloc_host_buffer(dev, BLOCK_BYTES);
    if (sbuf == NULL || rbuf == NULL) {
        printf("FAIL: alloc\n");
        goto fail;
    }

    /* 初期値: sbuf[i] = (uint64_t)i << 16 */
    {
        uint64_t* dp = (uint64_t*)sbuf;
        for (unsigned int i = 0; i < BLOCK_ELEMS; i++)
            dp[i] = (uint64_t)i << 16;
    }
    int rc = mnc2_send(dev, sbuf, 0, BLOCK_BYTES, SEND_TAG);
    if (rc) { printf("FAIL: send rc=%d\n", rc); goto fail; }

    /* Kernel A: PDM → 8 L2BM 分配 */
    mnc2_kernel_t kA = mnc2_load_kernel(dev, "_build/distribute.idma.dat");
    if (kA == NULL) { printf("FAIL: load distribute\n"); goto fail; }
    rc = mnc2_exec_kernel(kA);
    mnc2_free_kernel(kA);
    if (rc) { printf("FAIL: exec distribute rc=%d\n", rc); goto fail; }

    /* Kernel B: L2BM round trip + +1, N 回繰り返し */
    mnc2_kernel_t kB = mnc2_load_kernel(dev, "_build/add1.idma.dat");
    if (kB == NULL) { printf("FAIL: load add1\n"); goto fail; }
    for (int i = 0; i < N; i++) {
        rc = mnc2_exec_kernel(kB);
        if (rc) {
            printf("FAIL: exec add1[%d/%d] rc=%d\n", i + 1, N, rc);
            mnc2_free_kernel(kB);
            goto fail;
        }
    }
    mnc2_free_kernel(kB);

    /* Kernel C: PE 状態 ($lr0) を信頼して ID merge → L2BM → mvd L2BM-PDM */
    mnc2_kernel_t kC = mnc2_load_kernel(dev, "_build/collect.idma.dat");
    if (kC == NULL) { printf("FAIL: load collect\n"); goto fail; }
    rc = mnc2_exec_kernel(kC);
    mnc2_free_kernel(kC);
    if (rc) { printf("FAIL: exec collect rc=%d\n", rc); goto fail; }

    /* recv (PDM@0 offset 0、4096 u64 全部) */
    memset(rbuf, 0, BLOCK_BYTES);
    rc = mnc2_recv(dev, rbuf, OFFSET_OUT, BLOCK_BYTES, RECV_TAG);
    if (rc) { printf("FAIL: recv rc=%d\n", rc); goto fail; }

    uint64_t* rb = (uint64_t*)rbuf;
    printf("  recv[0..3] hex: %016" PRIx64 " %016" PRIx64 " %016" PRIx64 " %016" PRIx64 "\n",
           rb[0], rb[1], rb[2], rb[3]);

    /* バイナリを先に保存 (FAIL でも残す) */
    FILE* fp = fopen(OUTPUT_FILE, "wb");
    if (fp == NULL) {
        printf("FAIL: fopen(%s)\n", OUTPUT_FILE);
        goto fail;
    }
    size_t written = fwrite(rbuf, 1, BLOCK_BYTES, fp);
    fclose(fp);
    if (written != BLOCK_BYTES) {
        printf("FAIL: fwrite wrote %zu / %zu\n", written, (size_t)BLOCK_BYTES);
        goto fail;
    }
    printf("  saved %zu bytes to %s\n", (size_t)BLOCK_BYTES, OUTPUT_FILE);

    /* 検証: bijection チェック */
    int n_mismatch = 0;
    int i_bad = 0;
    int peid_bad = 0;
    int other_bits = 0;
    int dup_i = 0;
    int dup_peid = 0;
    static char seen_i[BLOCK_ELEMS];
    static char seen_peid[BLOCK_ELEMS];
    memset(seen_i, 0, sizeof(seen_i));
    memset(seen_peid, 0, sizeof(seen_peid));

    for (int k = 0; k < BLOCK_ELEMS; k++) {
        uint64_t v = rb[k];
        uint64_t n_field    = v & N_MASK;
        uint64_t i_field    = (v >> 16) & 0xFFF;
        uint64_t peid_field = (v >> 32) & 0xFFF;
        uint64_t other      = v & ~ALL_MASK;

        if (other != 0) {
            if (other_bits < 5)
                printf("  OTHER_BITS [%d]: v=0x%016" PRIx64 " other=0x%016" PRIx64 "\n",
                        k, v, other);
            other_bits++;
        }
        if (n_field != (uint64_t)N) {
            if (n_mismatch < 5)
                printf("  N_MISMATCH [%d]: v=0x%016" PRIx64 " got_n=%" PRIu64 " exp_n=%d\n",
                        k, v, n_field, N);
            n_mismatch++;
            continue;
        }
        if (i_field >= BLOCK_ELEMS) { i_bad++; continue; }
        if (peid_field >= BLOCK_ELEMS) { peid_bad++; continue; }
        if (seen_i[i_field]++)       dup_i++;
        if (seen_peid[peid_field]++) dup_peid++;
    }

    int errors = other_bits + n_mismatch + i_bad + peid_bad + dup_i + dup_peid;

    int missing_i = 0, missing_peid = 0;
    for (int k = 0; k < BLOCK_ELEMS; k++) {
        if (!seen_i[k])    missing_i++;
        if (!seen_peid[k]) missing_peid++;
    }
    errors += missing_i + missing_peid;

    if (errors) {
        printf(
            "FAIL: other_bits=%d n_mismatch=%d i_bad=%d peid_bad=%d "
            "dup_i=%d dup_peid=%d missing_i=%d missing_peid=%d\n",
            other_bits, n_mismatch, i_bad, peid_bad,
            dup_i, dup_peid, missing_i, missing_peid);
        goto fail;
    }

    printf("PASS: bijection holds (N=%d, %d (i, peid) pairs unique over 0..%d)\n",
           N, BLOCK_ELEMS, BLOCK_ELEMS - 1);
    mnc2_free_host_buffer(dev, sbuf, BLOCK_BYTES);
    mnc2_free_host_buffer(dev, rbuf, BLOCK_BYTES);
    mnc2_close(dev);
    return 0;

fail:
    if (sbuf) mnc2_free_host_buffer(dev, sbuf, BLOCK_BYTES);
    if (rbuf) mnc2_free_host_buffer(dev, rbuf, BLOCK_BYTES);
    mnc2_close(dev);
    return 1;
}
