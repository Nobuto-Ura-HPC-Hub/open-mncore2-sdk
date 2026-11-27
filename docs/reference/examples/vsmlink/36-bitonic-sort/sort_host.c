/* sort_host.c — バイトニックソート（4096 個、f64）の E2E 検証
 *
 * Usage: sort_host <kernel.idma.dat> <steps>
 *
 *   kernel が実行するのは、バイトニックソートの先頭 <steps> ステップ（全体は 78 ステップ）。
 *   ホストは、同じ規則で先頭 <steps> ステップを計算した配列を期待値とし、kernel の結果（PE の番号の順）と比べる。
 *   <steps> が 78 のときは、あわせて、結果が昇順に並び、入力を並べ替えたものであることも確かめる。
 *
 *   値は f64。有限で 0 でない値（正と負の両方）だけを使う。kernel の dmin / dmax は、0 を同一視し、NaN の扱いが特別なため、
 *   0 と NaN は入れない。比較は、f64 の値としての大小で行う。
 *
 *   段 s（1〜12）、ステップ j（s-1 から 0）で、PE i は、相手 i XOR 2^j の値と比べる。
 *   (i の bit j) == (i の bit s) なら小さい方、そうでなければ大きい方を新しい値にする。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include "mnc2.h"

#define ELEM_COUNT   4096
#define ELEM_BYTES   (ELEM_COUNT * sizeof(uint64_t))
#define TOTAL_STEPS  78
#define OFFSET_IDS   (0ULL * 8)     /* slot 8  (sort.param :addr 0) */
#define OFFSET_SRC   (4096ULL * 8)  /* slot 16 (:addr 4096) */
#define OFFSET_OUT   (8192ULL * 8)  /* slot 24 (:addr 8192) */
#define DMAID_IDS    0x10           /* .param :send_wait_tag と対応 */
#define DMAID_SRC    0x11
#define WD_RECV      0x1e           /* .param :recv_wait_tag と対応 */

/* [-2^32, 2^32) の、有限で 0 でない疑似乱数（f64 で誤差なく表せる） */
static double src_value(int i)
{
    uint64_t x = (uint64_t)(i + 1) * 6364136223846793005ULL + 1442695040888963407ULL;
    x ^= x >> 29;
    x *= 0xBF58476D1CE4E5B9ULL;
    double v = (double)((int64_t)(x >> 11) - (1LL << 52)) / 1048576.0;
    return v == 0.0 ? 0.5 : v;
}

static uint64_t bits(double d) { uint64_t u; memcpy(&u, &d, sizeof u); return u; }
static double   val(uint64_t u) { double d; memcpy(&d, &u, sizeof d); return d; }

/* 先頭 steps ステップを a に適用する */
static void bitonic_steps(double *a, int steps)
{
    double b[ELEM_COUNT];
    int t = 0;
    for (int s = 1; s <= 12; s++) {
        for (int j = s - 1; j >= 0; j--) {
            if (t++ >= steps) return;
            for (int i = 0; i < ELEM_COUNT; i++) {
                double mine = a[i], other = a[i ^ (1 << j)];
                int take_min = (((i >> j) & 1) == ((i >> s) & 1));
                b[i] = take_min ? (mine < other ? mine : other) : (mine > other ? mine : other);
            }
            memcpy(a, b, sizeof b);
        }
    }
}

static int cmp_f64(const void *x, const void *y)
{
    double a = *(const double *)x, b = *(const double *)y;
    return (a > b) - (a < b);
}

int main(int argc, char *argv[])
{
    if (argc != 3) { fprintf(stderr, "usage: %s <kernel> <steps>\n", argv[0]); return 2; }
    const char *kernel = argv[1];
    int steps = atoi(argv[2]);
    if (steps < 1 || steps > TOTAL_STEPS) { fprintf(stderr, "steps は 1〜%d\n", TOTAL_STEPS); return 2; }

    mnc2_device_t dev = mnc2_open(0);
    if (!dev) { fprintf(stderr, "FAIL: mnc2_open\n"); return 1; }

    void *ids_buf = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    void *src_buf = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    void *out_buf = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    if (!ids_buf || !src_buf || !out_buf) { fprintf(stderr, "FAIL: alloc\n"); mnc2_close(dev); return 1; }

    int rc = MNC2_SUCCESS;
    int errors = 0;

    uint64_t *ids = (uint64_t *)ids_buf, *src = (uint64_t *)src_buf, *out = (uint64_t *)out_buf;
    double input[ELEM_COUNT], expect[ELEM_COUNT];
    int negatives = 0;
    for (int j = 0; j < ELEM_COUNT; j++) {
        ids[j] = (uint64_t)j;
        input[j] = src_value(j);
        src[j] = bits(input[j]);
        expect[j] = input[j];
        if (input[j] < 0) negatives++;
    }
    bitonic_steps(expect, steps);

    rc = mnc2_send(dev, ids_buf, OFFSET_IDS, ELEM_BYTES, DMAID_IDS);
    if (rc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: send ids\n"); goto cleanup; }
    rc = mnc2_send(dev, src_buf, OFFSET_SRC, ELEM_BYTES, DMAID_SRC);
    if (rc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: send src\n"); goto cleanup; }

    mnc2_kernel_t k = mnc2_load_kernel(dev, kernel);
    if (!k) { fprintf(stderr, "FAIL: load kernel: %s\n", kernel); rc = 1; goto cleanup; }
    rc = mnc2_exec_kernel(k);
    mnc2_free_kernel(k);
    if (rc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: exec\n"); goto cleanup; }

    memset(out_buf, 0, ELEM_BYTES);
    rc = mnc2_recv(dev, out_buf, OFFSET_OUT, ELEM_BYTES, WD_RECV);
    if (rc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: recv\n"); goto cleanup; }

    for (int i = 0; i < ELEM_COUNT; i++) {
        if (out[i] != bits(expect[i])) {
            if (errors < 5)
                fprintf(stderr, "  MISMATCH PE %d: got=%.17g expect=%.17g\n", i, val(out[i]), expect[i]);
            errors++;
        }
    }

    if (steps == TOTAL_STEPS) {
        double sorted[ELEM_COUNT];
        memcpy(sorted, input, sizeof sorted);
        qsort(sorted, ELEM_COUNT, sizeof sorted[0], cmp_f64);
        int sort_errors = 0;
        for (int i = 0; i < ELEM_COUNT; i++)
            if (out[i] != bits(sorted[i])) sort_errors++;
        if (sort_errors) {
            fprintf(stderr, "  結果が、入力を昇順に並べたものと一致しない PE: %d 個\n", sort_errors);
            errors += sort_errors;
        }
    }

    if (errors) fprintf(stderr, "bitonic_sort FAIL: %d errors (steps %d)\n", errors, steps);
    else        printf("bitonic_sort PASS (steps %d%s、入力の負の値 %d 個、先頭 %.6g、末尾 %.6g)\n", steps,
                       steps == TOTAL_STEPS ? "、全体が昇順" : "、途中の結果が一致", negatives, val(out[0]), val(out[ELEM_COUNT - 1]));

cleanup:
    mnc2_free_host_buffer(dev, ids_buf, ELEM_BYTES);
    mnc2_free_host_buffer(dev, src_buf, ELEM_BYTES);
    mnc2_free_host_buffer(dev, out_buf, ELEM_BYTES);
    mnc2_reset(dev);
    mnc2_close(dev);
    return (rc != MNC2_SUCCESS || errors) ? 1 : 0;
}
