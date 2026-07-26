/* test_odd-even-sort-full.c -- 奇偶転置ソート full 版 (host が収束まで回して完全ソート)
 *
 * 13-odd-even-sort-gid（1 turn デモ）を土台に、host が even/odd フェーズをループして配列を
 * 完全にソートする。左メンバ判定は get_global_id（kernel 側の (id & 1)）なので host flag は無い。
 * swap 有無は kernel が inline reduce_add で swap_count に出し、host が毎フェーズ受信して
 * 「even + odd の 1 turn で swap 0」= 収束 と判定して早期終了する。
 *
 * ループ骨格は SDK の vsmlink 24-odd-even-sort-reduce を参照:
 *   - データはデバイス常駐（ping-pong 2 バッファ data_a/data_b）。turn 間で再送しない。
 *   - kernel 起動トリガは id 配列を tag 0x10 で再送して出す（データを送らないため）。
 *   - swap_count 合計 0 で break。安全上限 MAX_PASS。
 *
 * 検証: 収束後の配列を host 内 qsort（昇順）と全要素比較 + 昇順であることを直接確認。
 *
 * テスト入力: 大域ソート済 [1..4096] を幅 BLOCK ごとに逆順にしたもの。各ブロック内の逆転を
 *   ソートするのに約 BLOCK フェーズかかる（= 収束が BLOCK/2 turn 程度で終わり、emu 実行時間が
 *   現実的になる）。逆順 4096 全体（最悪 2048 turn）は emu では重すぎるので避ける。
 *   host のループ自体は任意入力を収束させる（正しさは入力に依らない）。
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mnc2.h"

#define ELEM_COUNT   4096
#define ELEM_BYTES   (ELEM_COUNT * sizeof(double))
#define IDS_BYTES    (ELEM_COUNT * sizeof(int64_t))
#define BF_BYTES     (ELEM_COUNT * sizeof(uint64_t))
#define BF_DATA_PATH "_build/collected_flags.bin"
#define COUNT_OUT_COUNT 4
#define COUNT_OUT_BYTES (COUNT_OUT_COUNT * sizeof(double))

/* PDM 配置 (byte offset。 .param は 8 byte = 1 LW 単位なので LW 値 * 8) */
#define OFFSET_DATA_A (0ULL)              /* even の入力 / odd の出力 */
#define OFFSET_DATA_B (24576ULL * 8)      /* even の出力 / odd の入力 */
#define OFFSET_IDS    (4096ULL * 8)       /* @identify 0 の id 配列 [0..4095] */
#define OFFSET_BF     (8192ULL * 8)
#define OFFSET_COUNT  (131072ULL * 8)     /* inline reduce_add の 4 partial sum */

#define SEND_WAIT_TAG 0x10
#define RECV_TAG_DATA 0x1e
#define RECV_TAG_COUNT 0x1d

/* テスト入力の 1 ブロック幅。収束フェーズ数 ~= BLOCK（= 収束 turn ~= BLOCK/2）で、emu 実行時間に
 * 直結する。逆順 4096 全体（最悪 2048 turn）は emu で重すぎるので、多ターン収束を実証しつつ
 * lit で現実的な時間に収まる幅にする。BLOCK=64 で約 33 turn（emu:lib で数十秒）。 */
#define BLOCK 64
/* 収束の安全上限 turn (1 turn = even + odd)。理論最悪は N/2=2048。SDK 24 は N=4096 を上限に取る */
#define MAX_PASS 4096

/* 大域ソート済 [1..N] を幅 BLOCK ごとに逆順にした入力を作る */
static void fill_block_reversed(double *a, int n)
{
    for (int base = 0; base < n; base += BLOCK) {
        int w = (base + BLOCK <= n) ? BLOCK : (n - base);
        for (int p = 0; p < w; p++) {
            /* ブロック base..base+w-1 に昇順値 base+1..base+w を逆順で置く */
            a[base + p] = (double)(base + (w - 1 - p) + 1);
        }
    }
}

static int cmp_double(const void *x, const void *y)
{
    double a = *(const double *)x, b = *(const double *)y;
    return (a < b) ? -1 : (a > b) ? 1 : 0;
}

/* 1 フェーズ実行: id を trigger tag で再送 → exec → data_out と swap_count を drain 受信。
 * data_out はデバイス上の最新ソート状態、swap_count は合計を *out_swaps に返す。 */
static int run_phase(mnc2_device_t dev, mnc2_kernel_t kernel, const char *name,
                     void *sendbuf_ids, uint64_t data_out_off,
                     void *recvbuf_data, void *recvbuf_count, double *out_swaps)
{
    /* id を trigger tag で再送（データはデバイス常駐なので id が kernel 起動トリガ） */
    if (mnc2_send(dev, sendbuf_ids, OFFSET_IDS, IDS_BYTES, SEND_WAIT_TAG) != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: send ids (%s)\n", name); return -1;
    }
    if (mnc2_exec_kernel(kernel) != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: exec (%s)\n", name); return -1;
    }
    /* data_out を drain 受信（done flag を落とす。最新のソート状態でもある） */
    if (mnc2_recv(dev, recvbuf_data, data_out_off, ELEM_BYTES, RECV_TAG_DATA) != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: recv data (%s)\n", name); return -1;
    }
    /* swap_count(4 partial sum) を受信して合計 */
    memset(recvbuf_count, 0, COUNT_OUT_BYTES);
    if (mnc2_recv(dev, recvbuf_count, OFFSET_COUNT, COUNT_OUT_BYTES, RECV_TAG_COUNT) != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: recv count (%s)\n", name); return -1;
    }
    const double *cp = (const double *)recvbuf_count;
    double total = 0.0;
    for (int g = 0; g < COUNT_OUT_COUNT; g++) total += cp[g];
    *out_swaps = total;
    return 0;
}

int main(void)
{
    printf("=== 14-odd-even-sort-full: get_global_id 版 full ソート (host ループで収束) ===\n");
    printf("=== 入力: 大域ソート済を幅 %d ごとに逆順。収束まで even/odd を回す ===\n\n", BLOCK);

    mnc2_device_t dev = mnc2_open(0);
    if (dev == NULL) { fprintf(stderr, "FAIL: mnc2_open\n"); return 1; }
    printf("14-odd-even-sort-full backend: %s\n", mnc2_get_backend_name(dev));

    int rc = 0;
    mnc2_kernel_t kernel_even = NULL, kernel_odd = NULL;
    void *sendbuf_data  = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    void *sendbuf_ids   = mnc2_alloc_host_buffer(dev, IDS_BYTES);
    void *bfbuf         = mnc2_alloc_host_buffer(dev, BF_BYTES);
    void *recvbuf_data  = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    void *recvbuf_count = mnc2_alloc_host_buffer(dev, COUNT_OUT_BYTES);
    double *orig_data = (double *)malloc(ELEM_BYTES);
    double *expected  = (double *)malloc(ELEM_BYTES);
    if (!sendbuf_data || !sendbuf_ids || !bfbuf || !recvbuf_data || !recvbuf_count
            || !orig_data || !expected) {
        fprintf(stderr, "FAIL: alloc\n"); rc = 1; goto cleanup;
    }

    /* 入力を作る。expected = qsort(入力) = 昇順の参照 */
    fill_block_reversed(orig_data, ELEM_COUNT);
    memcpy(expected, orig_data, ELEM_BYTES);
    qsort(expected, ELEM_COUNT, sizeof(double), cmp_double);

    /* @identify 用 id 配列 [0..4095] */
    for (int i = 0; i < ELEM_COUNT; i++) ((int64_t *)sendbuf_ids)[i] = (int64_t)i;

    /* boundary_flags を 1 回だけ送る (non-trigger) */
    {
        FILE *fp = fopen(BF_DATA_PATH, "rb");
        if (!fp) { fprintf(stderr, "FAIL: open %s\n", BF_DATA_PATH); rc = 1; goto cleanup; }
        size_t nr = fread(bfbuf, 1, BF_BYTES, fp);
        fclose(fp);
        if (nr != BF_BYTES) { fprintf(stderr, "FAIL: bf read\n"); rc = 1; goto cleanup; }
    }
    if (mnc2_send(dev, bfbuf, OFFSET_BF, BF_BYTES, 0) != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: send bf\n"); rc = 1; goto cleanup;
    }

    /* id 配列と初期データをデバイスに置く (non-trigger)。以降 data は再送しない (ping-pong 常駐) */
    if (mnc2_send(dev, sendbuf_ids, OFFSET_IDS, IDS_BYTES, 0) != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: send ids (init)\n"); rc = 1; goto cleanup;
    }
    memcpy(sendbuf_data, orig_data, ELEM_BYTES);
    if (mnc2_send(dev, sendbuf_data, OFFSET_DATA_A, ELEM_BYTES, 0) != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: send data (init)\n"); rc = 1; goto cleanup;
    }

    kernel_even = mnc2_load_kernel(dev, "_build/odd-even-sort-full-even.idma.dat");
    kernel_odd  = mnc2_load_kernel(dev, "_build/odd-even-sort-full-odd.idma.dat");
    if (!kernel_even || !kernel_odd) { fprintf(stderr, "FAIL: load_kernel\n"); rc = 1; goto cleanup; }

    /* --- 収束ループ --- */
    int converged = 0, passes = 0;
    for (int pass = 0; pass < MAX_PASS; pass++) {
        double even_swaps = 0.0, odd_swaps = 0.0;

        /* even: data_a を読み data_b へ。data_b を drain 受信 */
        if (run_phase(dev, kernel_even, "EVEN", sendbuf_ids, OFFSET_DATA_B,
                      recvbuf_data, recvbuf_count, &even_swaps) != 0) { rc = 1; goto cleanup; }
        /* odd: data_b を読み data_a へ。data_a を drain 受信（収束時これがソート結果） */
        if (run_phase(dev, kernel_odd, "ODD", sendbuf_ids, OFFSET_DATA_A,
                      recvbuf_data, recvbuf_count, &odd_swaps) != 0) { rc = 1; goto cleanup; }

        passes = pass + 1;
        if (pass < 3 || (pass % 16) == 0) {
            printf("  turn %4d: even swaps=%.0f, odd swaps=%.0f\n", pass, even_swaps, odd_swaps);
        }
        if (even_swaps == 0.0 && odd_swaps == 0.0) { converged = 1; break; }
    }

    printf("\n%s (turns=%d)\n", converged ? "収束した" : "MAX_PASS で打ち切り", passes);

    /* --- 検証: recvbuf_data (最後の odd 出力 = data_a) が昇順ソート済か --- */
    const double *got = (const double *)recvbuf_data;
    int errors = 0, not_ascending = 0;
    for (int i = 0; i < ELEM_COUNT; i++) {
        if (got[i] != expected[i]) {
            if (errors < 5)
                fprintf(stderr, "  MISMATCH data[%d]: got=%g expected=%g\n", i, got[i], expected[i]);
            errors++;
        }
        if (i > 0 && got[i] < got[i - 1]) not_ascending++;
    }

    if (!converged) {
        fprintf(stderr, "FAIL: %d turn で収束しなかった\n", MAX_PASS);
        rc = 1;
    } else if (errors > 0) {
        fprintf(stderr, "FAIL: qsort と %d 要素不一致\n", errors);
        rc = 1;
    } else if (not_ascending > 0) {
        fprintf(stderr, "FAIL: 昇順でない箇所が %d\n", not_ascending);
        rc = 1;
    } else {
        printf("PASS: %d 要素すべて昇順ソート済 (qsort と一致)、%d turn で収束\n", ELEM_COUNT, passes);
    }

cleanup:
    if (kernel_even)   mnc2_free_kernel(kernel_even);
    if (kernel_odd)    mnc2_free_kernel(kernel_odd);
    if (sendbuf_data)  mnc2_free_host_buffer(dev, sendbuf_data, ELEM_BYTES);
    if (sendbuf_ids)   mnc2_free_host_buffer(dev, sendbuf_ids, IDS_BYTES);
    if (bfbuf)         mnc2_free_host_buffer(dev, bfbuf, BF_BYTES);
    if (recvbuf_data)  mnc2_free_host_buffer(dev, recvbuf_data, ELEM_BYTES);
    if (recvbuf_count) mnc2_free_host_buffer(dev, recvbuf_count, COUNT_OUT_BYTES);
    mnc2_close(dev);
    free(orig_data);
    free(expected);
    return (rc != 0) ? 1 : 0;
}
