/* test_odd-even-sort-gid.c -- 奇偶転置ソート 1 turn (even + odd) + swap 回数集計 E2E
 *                             (get_global_id 版)
 *
 * 10-odd-even-sort の host driver とほぼ同一。 違いは「左メンバ判定用の flag 配列を host が
 * 送る」代わりに「@identify 用の id 配列 [0..4095] を送る」点だけ。 kernel 側で
 * get_global_id が PE 番号を得て is_left = ((id & 1) == 偶奇) を自前計算する。
 *
 * @identify は @distribute と同じで PDM から id を配る（23-identify サンプル参照）。 host が
 * IDs[0..4095] を identify の addr(4096 = 例 10 の flag スロット再利用) へ送る必要がある。
 * id 配列は even/odd で同じ内容（PE 番号）なので、 例 10 の flag と同じ送信タイミングに乗せる:
 *   even: id を tag 0 で置いてから data_a を tag #x10 で送る（data が trigger）
 *   odd : id を tag #x10 で送る（id が trigger。 data_b は device に残っている）
 *
 * これ以外（data の ping-pong、 reduce、 boundary_flags、 3 パターン照合）は例 10 と同一。
 *
 *   test1 desc    降順 [4096, 4095, ..., 1]                     両フェーズとも全ペアが swap する
 *   test2 sorted  昇順 [10001, 10002, ..., 14096]               swap が 1 度も起きない (収束状態)
 *   test3 dup     [22048, 22048, ..., 20001, 20001]             偶数ペアが等値、 奇数ペアは非等値
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "mnc2.h"

#define ELEM_COUNT       4096
#define ELEM_BYTES       (ELEM_COUNT * sizeof(double))
#define IDS_BYTES        (ELEM_COUNT * sizeof(int64_t))   /* @identify 用 id 配列: 1 LW per PE */
#define BF_BYTES         (ELEM_COUNT * sizeof(uint64_t))   /* boundary flags: 1 LW per PE */
#define BF_DATA_PATH     "_build/collected_flags.bin"
#define COUNT_OUT_COUNT  4
#define COUNT_OUT_BYTES  (COUNT_OUT_COUNT * sizeof(double))

/* PDM 配置 (byte offset。 .param は 8 byte = 1 LW 単位なので LW 値 * 8) */
#define OFFSET_DATA_A    (0ULL)              /* data_a  (even の入力 / odd の出力) */
#define OFFSET_DATA_B    (24576ULL * 8)      /* data_b  (even の出力 / odd の入力) */
#define OFFSET_IDS       (4096ULL * 8)       /* @identify 0 の id 配列 [0..4095]（例 10 の flag スロット再利用） */
#define OFFSET_BF        (8192ULL * 8)
#define OFFSET_SWAP      (32768ULL * 8)      /* swap_flags (even/odd が collect、 reduce が読む) */
#define OFFSET_COUNT     (131072ULL * 8)

#define SEND_WAIT_TAG    0x10
#define RECV_TAG_DATA    0x1e
#define RECV_TAG_SWAP    0x1c
#define RECV_TAG_COUNT   0x1d

#define PHASE_EVEN 0
#define PHASE_ODD  1

#define NUM_TESTS  3

/* 入力パターンを orig_data に書く。 未知のパターンなら -1。
 * 3 パターンの値域は重ならない (前のテストの残留を data 比較で必ず捕まえるため) */
static int fill_input(double *a, int n, const char *pattern)
{
    if (!strcmp(pattern, "desc")) {
        for (int i = 0; i < n; i++) {
            a[i] = (double)(n - i);                      /* [4096 .. 1] */
        }
    } else if (!strcmp(pattern, "sorted")) {
        for (int i = 0; i < n; i++) {
            a[i] = (double)(10001 + i);                  /* [10001 .. 14096] */
        }
    } else if (!strcmp(pattern, "dup")) {
        for (int i = 0; i < n; i++) {
            a[i] = (double)(20001 + n / 2 - 1 - i / 2);  /* [22048, 22048, ..., 20001, 20001] */
        }
    } else {
        return -1;
    }
    return 0;
}

/* 参照実装: 1 フェーズ適用し、 swap した pair 数を返す。
 * phase=0 はペア (0,1),(2,3),...  phase=1 はペア (1,2),(3,4),... (両端不変) */
static int phase_step(double *a, int n, int phase)
{
    int swapped = 0;
    for (int i = (phase == PHASE_EVEN) ? 0 : 1; i + 1 < n; i += 2) {
        if (a[i] > a[i+1]) {
            double t = a[i];
            a[i] = a[i+1];
            a[i+1] = t;
            swapped++;
        }
    }
    return swapped;
}

/* even / odd kernel を 1 回実行し、 出力 data を data_out_off から、 swap_flags を drain する。 */
static int exec_swap_phase(mnc2_device_t dev, mnc2_kernel_t kernel, const char *phase_name,
                           uint64_t data_out_off, void *recvbuf_data, void *recvbuf_swap)
{
    if (mnc2_exec_kernel(kernel) != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: exec (%s)\n", phase_name);
        return -1;
    }
    memset(recvbuf_data, 0, ELEM_BYTES);
    if (mnc2_recv(dev, recvbuf_data, data_out_off, ELEM_BYTES, RECV_TAG_DATA) != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: recv data (%s)\n", phase_name);
        return -1;
    }
    /* swap_flags を drain (done flag を落とす + reduce が読む前の collect 完了を保証)。 */
    memset(recvbuf_swap, 0, ELEM_BYTES);
    if (mnc2_recv(dev, recvbuf_swap, OFFSET_SWAP, ELEM_BYTES, RECV_TAG_SWAP) != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: recv swap_flags (%s)\n", phase_name);
        return -1;
    }
    return 0;
}

/* reduce kernel を実行し、 swap_count を受信して pair 数を *out_pairs に返す。 */
static int exec_reduce(mnc2_device_t dev, mnc2_kernel_t kernel_reduce, const char *phase_name,
                       void *recvbuf_count, int *out_pairs)
{
    if (mnc2_exec_kernel(kernel_reduce) != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: exec reduce (%s)\n", phase_name);
        return -1;
    }
    memset(recvbuf_count, 0, COUNT_OUT_BYTES);
    if (mnc2_recv(dev, recvbuf_count, OFFSET_COUNT, COUNT_OUT_BYTES, RECV_TAG_COUNT) != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: recv count (%s)\n", phase_name);
        return -1;
    }

    const double *cp = (const double *)recvbuf_count;
    double total = 0.0;
    printf("\n[%s] swap count (per PE position group):\n", phase_name);
    for (int g = 0; g < COUNT_OUT_COUNT; g++) {
        printf("  parts[%d] = %.0f\n", g, cp[g]);
        total += cp[g];
    }
    printf("  total    = %.0f\n", total);
    printf("  pairs swapped = %.0f (= total / 2)\n", total / 2.0);

    *out_pairs = (int)(total / 2.0);
    return 0;
}

/* device の data と参照配列を比較。 不一致数を返す (先頭 5 件だけ表示) */
static int compare_data(const double *got, const double *expected, const char *phase_name)
{
    int errors = 0;
    for (int i = 0; i < ELEM_COUNT; i++) {
        if (got[i] != expected[i]) {
            if (errors < 5) {
                fprintf(stderr, "  MISMATCH [%s] data[%d]: got=%g expected=%g\n",
                        phase_name, i, got[i], expected[i]);
            }
            errors++;
        }
    }
    return errors;
}

int main(void)
{
    static const char *patterns[NUM_TESTS] = { "desc", "sorted", "dup" };
    int case_failed[NUM_TESTS];
    int case_even[NUM_TESTS];
    int case_odd[NUM_TESTS];

    printf("=== odd-even-sort-gid: 1 turn (even + odd) + swap count via reduce kernel ===\n");
    printf("=== get_global_id 版 (左メンバ判定を PE が自前計算、 host flag 不要) ===\n");
    printf("=== %d tests: desc / sorted / dup ===\n\n", NUM_TESTS);

    mnc2_device_t dev = mnc2_open(0);
    if (dev == NULL) { fprintf(stderr, "FAIL: mnc2_open\n"); return 1; }
    printf("13-odd-even-sort-gid backend: %s\n", mnc2_get_backend_name(dev));

    int rc = 0;
    mnc2_kernel_t kernel_even = NULL, kernel_odd = NULL, kernel_reduce = NULL;
    void *sendbuf_data  = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    void *sendbuf_ids   = mnc2_alloc_host_buffer(dev, IDS_BYTES);   /* @identify 用 id 配列 [0..4095] */
    void *bfbuf         = mnc2_alloc_host_buffer(dev, BF_BYTES);
    void *recvbuf_data  = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    void *recvbuf_swap  = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    void *recvbuf_count = mnc2_alloc_host_buffer(dev, COUNT_OUT_BYTES);
    double *orig_data = (double *)malloc(ELEM_BYTES);
    double *ref_data  = (double *)malloc(ELEM_BYTES);   /* C 参照。 フェーズごとに更新 */
    if (!sendbuf_data || !sendbuf_ids || !bfbuf
            || !recvbuf_data || !recvbuf_swap || !recvbuf_count || !orig_data || !ref_data) {
        fprintf(stderr, "FAIL: alloc\n"); rc = 1; goto cleanup;
    }

    /* @identify 用の id 配列 [0..4095]。 even/odd で共通なので 1 回だけ埋める */
    for (int i = 0; i < ELEM_COUNT; i++) {
        ((int64_t *)sendbuf_ids)[i] = (int64_t)i;
    }

    /* boundary_flags (clamp) を 1 回だけ送る。 non-trigger */
    {
        FILE *fp = fopen(BF_DATA_PATH, "rb");
        if (!fp) { fprintf(stderr, "FAIL: open %s\n", BF_DATA_PATH); rc = 1; goto cleanup; }
        size_t nr = fread(bfbuf, 1, BF_BYTES, fp);
        fclose(fp);
        if (nr != BF_BYTES) {
            fprintf(stderr, "FAIL: %s: read %zu bytes, expected %zu\n",
                    BF_DATA_PATH, nr, (size_t)BF_BYTES);
            rc = 1; goto cleanup;
        }
    }
    if (mnc2_send(dev, bfbuf, OFFSET_BF, BF_BYTES, 0) != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: send bf\n"); rc = 1; goto cleanup;
    }

    kernel_even   = mnc2_load_kernel(dev, "_build/odd-even-sort-gid-even.idma.dat");
    kernel_odd    = mnc2_load_kernel(dev, "_build/odd-even-sort-gid-odd.idma.dat");
    kernel_reduce = mnc2_load_kernel(dev, "_build/odd-even-sort-gid-reduce.idma.dat");
    if (!kernel_even || !kernel_odd || !kernel_reduce) {
        fprintf(stderr, "FAIL: load_kernel\n"); rc = 1; goto cleanup;
    }

    /* --- テストを 3 本、 上から順に流す。 kernel と boundary_flags は共通 --- */
    for (int t = 0; t < NUM_TESTS; t++) {
        const char *pattern = patterns[t];
        printf("\n======== test%d: %s ========\n", t + 1, pattern);

        if (fill_input(orig_data, ELEM_COUNT, pattern) != 0) {
            fprintf(stderr, "FAIL: unknown pattern '%s'\n", pattern);
            rc = 1; goto cleanup;
        }
        memcpy(ref_data, orig_data, ELEM_BYTES);

        /* --- 偶数フェーズ --- id は non-trigger (tag 0) で置き、 data (data_a) が trigger --- */
        if (mnc2_send(dev, sendbuf_ids, OFFSET_IDS, IDS_BYTES, 0) != MNC2_SUCCESS) {
            fprintf(stderr, "FAIL: send ids (even)\n"); rc = 1; goto cleanup;
        }
        memcpy(sendbuf_data, orig_data, ELEM_BYTES);
        if (mnc2_send(dev, sendbuf_data, OFFSET_DATA_A, ELEM_BYTES, SEND_WAIT_TAG) != MNC2_SUCCESS) {
            fprintf(stderr, "FAIL: send data\n"); rc = 1; goto cleanup;
        }

        int dev_pairs_even = 0;
        if (exec_swap_phase(dev, kernel_even, "EVEN", OFFSET_DATA_B,
                            recvbuf_data, recvbuf_swap) != 0) {
            rc = 1; goto cleanup;
        }
        if (exec_reduce(dev, kernel_reduce, "EVEN", recvbuf_count, &dev_pairs_even) != 0) {
            rc = 1; goto cleanup;
        }
        int ref_pairs_even = phase_step(ref_data, ELEM_COUNT, PHASE_EVEN);
        int errors = compare_data((const double *)recvbuf_data, ref_data, "EVEN");

        /* --- 奇数フェーズ --- data は device 上に残る (data_b)。 id を送り、 これが trigger --- */
        if (mnc2_send(dev, sendbuf_ids, OFFSET_IDS, IDS_BYTES, SEND_WAIT_TAG) != MNC2_SUCCESS) {
            fprintf(stderr, "FAIL: send ids (odd)\n"); rc = 1; goto cleanup;
        }

        int dev_pairs_odd = 0;
        if (exec_swap_phase(dev, kernel_odd, "ODD", OFFSET_DATA_A,
                            recvbuf_data, recvbuf_swap) != 0) {
            rc = 1; goto cleanup;
        }
        if (exec_reduce(dev, kernel_reduce, "ODD", recvbuf_count, &dev_pairs_odd) != 0) {
            rc = 1; goto cleanup;
        }
        int ref_pairs_odd = phase_step(ref_data, ELEM_COUNT, PHASE_ODD);
        errors += compare_data((const double *)recvbuf_data, ref_data, "ODD");

        /* --- 照合。 失敗しても残りのテストは流す --- */
        printf("\nswap pairs: even device=%d C=%d / odd device=%d C=%d\n",
               dev_pairs_even, ref_pairs_even, dev_pairs_odd, ref_pairs_odd);

        int failed = 0;
        if (dev_pairs_even != ref_pairs_even) {
            fprintf(stderr, "FAIL: swap count mismatch [EVEN] (device=%d, C=%d)\n",
                    dev_pairs_even, ref_pairs_even);
            failed = 1;
        }
        if (dev_pairs_odd != ref_pairs_odd) {
            fprintf(stderr, "FAIL: swap count mismatch [ODD] (device=%d, C=%d)\n",
                    dev_pairs_odd, ref_pairs_odd);
            failed = 1;
        }
        if (errors > 0) {
            fprintf(stderr, "FAIL: %d data mismatches\n", errors);
            failed = 1;
        }
        if (!failed) {
            printf("PASS test%d (%s): data %d/%d 一致、 swap pair %d + %d 一致\n",
                   t + 1, pattern, ELEM_COUNT, ELEM_COUNT, ref_pairs_even, ref_pairs_odd);
        }

        case_failed[t] = failed;
        case_even[t]   = ref_pairs_even;
        case_odd[t]    = ref_pairs_odd;
        if (failed) {
            rc = 1;
        }
    }

    /* --- サマリ --- */
    printf("\n======== summary ========\n");
    {
        int passed = 0;
        for (int t = 0; t < NUM_TESTS; t++) {
            printf("  test%d %-6s : %s (even %d pairs / odd %d pairs)\n",
                   t + 1, patterns[t], case_failed[t] ? "FAIL" : "PASS",
                   case_even[t], case_odd[t]);
            if (!case_failed[t]) {
                passed++;
            }
        }
        if (rc == 0) {
            printf("\nPASS: %d/%d tests\n", passed, NUM_TESTS);
        } else {
            fprintf(stderr, "\nFAIL: %d/%d tests passed\n", passed, NUM_TESTS);
        }
    }

cleanup:
    if (kernel_even)   mnc2_free_kernel(kernel_even);
    if (kernel_odd)    mnc2_free_kernel(kernel_odd);
    if (kernel_reduce) mnc2_free_kernel(kernel_reduce);
    if (sendbuf_data)  mnc2_free_host_buffer(dev, sendbuf_data, ELEM_BYTES);
    if (sendbuf_ids)   mnc2_free_host_buffer(dev, sendbuf_ids, IDS_BYTES);
    if (bfbuf)         mnc2_free_host_buffer(dev, bfbuf, BF_BYTES);
    if (recvbuf_data)  mnc2_free_host_buffer(dev, recvbuf_data, ELEM_BYTES);
    if (recvbuf_swap)  mnc2_free_host_buffer(dev, recvbuf_swap, ELEM_BYTES);
    if (recvbuf_count) mnc2_free_host_buffer(dev, recvbuf_count, COUNT_OUT_BYTES);
    mnc2_close(dev);
    free(orig_data);
    free(ref_data);
    return (rc != 0) ? 1 : 0;
}
