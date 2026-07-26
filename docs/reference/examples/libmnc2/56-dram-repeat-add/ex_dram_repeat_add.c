/* ex_dram_repeat_add.c — DRAM 上の状態を複数 kernel 実行で継承するテスト
 *
 * データフロー:
 *   1. host → PDM@0 (send): u64 配列 [i << 16 for i in 0..4095]
 *      → 各 u64 の bits[27:16] = 送信インデックス i
 *   2. Kernel A (distribute): PDM@0 → DRAM@0..3 (mvd, 16 LW round-robin)
 *   3. Kernel B (add1): DRAM → L2BM (並列個別) → PE +1 (ladd) → DRAM
 *      PDM 経由なし。N 回繰り返し。bits[15:0] が 0 → N になる。
 *   4. Kernel C (collect): DRAM → L2BM (並列個別) → PE (flat PE ID = 0..4095
 *      を bits[43:32] に埋め込み、ladd で合成) → L2BM → DRAM → PDM (mvd)
 *   5. host ← PDM@0 (recv): u64 配列
 *
 * 期待ビットレイアウト (recv 値):
 *   bits [15: 0] = N                       (Kernel B の繰り返し加算)
 *   bits [27:16] = i                       (送信インデックス 0..4095 の一意)
 *   bits [43:32] = flat PE ID              (処理 PE の 0..4095 の一意)
 *   それ以外のビット = 0
 *
 * bijection 検証: 4096 個の recv 値について
 *   1. bits[15:0] がすべて N
 *   2. bits[27:16] の集合 = {0..4095} (一意出現)
 *   3. bits[43:32] の集合 = {0..4095} (一意出現)
 *   4. 上記 3 領域以外のビットは全 0
 *
 * emu:process は kernel 実行毎に gpfn3_package_main を spawn するため DRAM
 * 状態が保持されず、このテストは通らない (SKIP する)。emu:lib / device のみ
 * 対応。
 *
 * Usage:
 *
 * Options:
 *   N                  Kernel B (+1) の繰り返し回数 (default 1, N < 65536)
 *   --dram-debug       Kernel A 直後に DRAM@0..3 の内容を
 *                      _build/dram{0..3}.bin に保存 (emu:lib のみ対応)。
 *                      mvd distribute は **16 u64 ラウンドロビン**:
 *                      dram0 先頭は (0<<16),(1<<16),...,(15<<16)、次は
 *                      (64<<16)..(79<<16)、末尾は (4032<<16)..(4047<<16)。
 *   --use-direct-pdm0  Collect 経路を「PE → PDM 直結」(`collect-pdm0.vsm` 1 発)
 *                      に切り替え。default は collect-dram → collect-dram-pdm0
 *                      の 2 段。`--use-direct-pdm0` 時の出力 PDM offset は
 *                      32768 byte (= BLOCK_BYTES) でシフト、入力データ
 *                      (PDM[0..32767]) を保持する。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "mnc2.h"

/* 4096 要素 = 1 u64 per PE で MN-Core 2 全 PE を活用する */
#define BLOCK_ELEMS 4096
#define BLOCK_BYTES (BLOCK_ELEMS * sizeof(uint64_t))
#define OFFSET_OUT  0
#define SEND_TAG    2
#define RECV_TAG    6
#define OUTPUT_FILE "_build/dram_repeat_add.bin"

#define N_MASK    ((uint64_t)0xFFFF)
#define I_MASK    ((uint64_t)0xFFF << 16)
#define PEID_MASK ((uint64_t)0xFFF << 32)
#define ALL_MASK  (N_MASK | I_MASK | PEID_MASK)

/* Kernel A 実行直後に、指定 DRAM group (0..3) の 1024 u64 を読み出し、
   _build/dramN.bin として保存する。emu:lib 限定のデバッグ補助。 */
static int dump_dram_group(mnc2_device_t dev, int group)
{
    uint64_t buf[1024];
    mnc2_loc_t loc = MNC2_LOC_INIT;
    loc.chip = group;

    int rc = mnc2_debug_read(dev, MNC2_MEM_DRAM, &loc, 0, 1024, buf);
    if (rc != MNC2_SUCCESS) {
        printf("FAIL: mnc2_debug_read(DRAM, chip=%d) rc=%d\n",
                group, rc);
        return rc;
    }

    char path[64];
    snprintf(path, sizeof(path), "_build/dram%d.bin", group);
    FILE* fp = fopen(path, "wb");
    if (fp == NULL) {
        printf("FAIL: fopen(%s)\n", path);
        return -1;
    }
    size_t written = fwrite(buf, 1, sizeof(buf), fp);
    fclose(fp);
    if (written != sizeof(buf)) {
        printf("FAIL: fwrite(%s) wrote %zu/%zu\n",
                path, written, sizeof(buf));
        return -1;
    }
    /* u64 として bits[27:16] の i 部分を表示 (分配パターンを目視確認)。
       16 u64 ラウンドロビンなので dram0 先頭は i=0..3、末尾は 4044..4047。 */
    printf("  dram-debug: saved %s (1024 u64): "
           "head_i=%" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64
           " ... tail_i=%" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 "\n",
           path,
           (buf[0]    >> 16) & 0xFFF,
           (buf[1]    >> 16) & 0xFFF,
           (buf[2]    >> 16) & 0xFFF,
           (buf[3]    >> 16) & 0xFFF,
           (buf[1020] >> 16) & 0xFFF,
           (buf[1021] >> 16) & 0xFFF,
           (buf[1022] >> 16) & 0xFFF,
           (buf[1023] >> 16) & 0xFFF);
    return 0;
}

int main(int argc, char** argv)
{
    int N = 1;
    int dram_debug = 0;
    int use_direct_pdm0 = 0;
    for (int ai = 1; ai < argc; ai++) {
        if (strcmp(argv[ai], "--dram-debug") == 0) {
            dram_debug = 1;
        } else if (strcmp(argv[ai], "--use-direct-pdm0") == 0) {
            use_direct_pdm0 = 1;
        } else if (argv[ai][0] != '-') {
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

    printf("[test] DRAM repeat-add bijection (N=%d): %d elements, "
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
        printf("SKIP: emu:process does not preserve DRAM state "
                "across mnc2_exec_kernel calls (kernel B needs state persistence)\n");
        mnc2_close(dev);
        return 0;
    }

    /* --dram-debug は mnc2_debug_read に依存し、emu:lib でしか動かない */
    if (dram_debug && strcmp(backend, "emu:lib") != 0) {
        printf("FAIL: --dram-debug requires emu:lib backend "
                "(current: %s). mnc2_debug_read is not supported on %s.\n",
                backend, backend);
        mnc2_close(dev);
        return 1;
    }

    void* sbuf = mnc2_alloc_host_buffer(dev, BLOCK_BYTES);
    void* rbuf = mnc2_alloc_host_buffer(dev, BLOCK_BYTES);
    if (sbuf == NULL || rbuf == NULL) {
        printf("FAIL: alloc\n");
        goto fail;
    }

    /* 初期値: sbuf[i] = (uint64_t)i << 16
       → bits[27:16] = i (0..4095), bits[15:0] = 0, bits[63:28] = 0 */
    {
        uint64_t* dp = (uint64_t*)sbuf;
        for (unsigned int i = 0; i < BLOCK_ELEMS; i++)
            dp[i] = (uint64_t)i << 16;
    }
    int rc = mnc2_send(dev, sbuf, 0, BLOCK_BYTES, SEND_TAG);
    if (rc) { printf("FAIL: send rc=%d\n", rc); goto fail; }

    /* Kernel A: PDM → DRAM distribute */
    mnc2_kernel_t kA = mnc2_load_kernel(dev, "_build/distribute.idma.dat");
    if (kA == NULL) { printf("FAIL: load distribute\n"); goto fail; }
    rc = mnc2_exec_kernel(kA);
    mnc2_free_kernel(kA);
    if (rc) { printf("FAIL: exec distribute rc=%d\n", rc); goto fail; }

    /* --dram-debug: distribute 直後の DRAM@0..3 をダンプ (emu:lib のみ) */
    if (dram_debug) {
        for (int g = 0; g < 4; g++) {
            if (dump_dram_group(dev, g) != 0) goto fail;
        }
    }

    /* Kernel B: DRAM → PE +1 → DRAM, N 回繰り返し */
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

    /* Kernel C: collect (PE 状態 $lr0 を信頼して ID を merge → PDM へ)
       Default: collect-dram → collect-dram-pdm0 の 2 段
       --use-direct-pdm0: collect-pdm0 1 発、PDM offset を BLOCK_BYTES シフト */
    if (use_direct_pdm0) {
        mnc2_kernel_t kC = mnc2_load_kernel(dev, "_build/collect-pdm0.idma.dat");
        if (kC == NULL) {
            printf("FAIL: load collect-pdm0\n");
            goto fail;
        }
        rc = mnc2_exec_kernel(kC);
        mnc2_free_kernel(kC);
        if (rc) {
            printf("FAIL: exec collect-pdm0 rc=%d\n", rc);
            goto fail;
        }
    } else {
        mnc2_kernel_t kC1 = mnc2_load_kernel(dev, "_build/collect-dram.idma.dat");
        if (kC1 == NULL) {
            printf("FAIL: load collect-dram\n");
            goto fail;
        }
        rc = mnc2_exec_kernel(kC1);
        mnc2_free_kernel(kC1);
        if (rc) {
            printf("FAIL: exec collect-dram rc=%d\n", rc);
            goto fail;
        }

        mnc2_kernel_t kC2 = mnc2_load_kernel(dev, "_build/collect-dram-pdm0.idma.dat");
        if (kC2 == NULL) {
            printf("FAIL: load collect-dram-pdm0\n");
            goto fail;
        }
        rc = mnc2_exec_kernel(kC2);
        mnc2_free_kernel(kC2);
        if (rc) {
            printf("FAIL: exec collect-dram-pdm0 rc=%d\n", rc);
            goto fail;
        }
    }

    /* recv: --use-direct-pdm0 のとき出力 PDM offset は BLOCK_BYTES、
       そうでないとき (mvd 経由) は 0 */
    uint64_t recv_offset = use_direct_pdm0 ? (uint64_t)BLOCK_BYTES : 0;
    memset(rbuf, 0, BLOCK_BYTES);
    rc = mnc2_recv(dev, rbuf, recv_offset, BLOCK_BYTES, RECV_TAG);
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

    /* 全 index / 全 peid が 1 度ずつ出現しているか確認 */
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
