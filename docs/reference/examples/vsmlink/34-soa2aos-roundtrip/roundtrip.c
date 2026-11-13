/* roundtrip — @distribute_aos2soa / @collect_soa2aos の対を確かめるホスト。
 *
 * 引数なし:   roundtrip kernel を実行する。番号の順に並べた入力 (4096 個 x 4 値) を送り、
 *             配って何も計算せずに書き戻した出力が、全 16384 u64 で入力と一致することを見る。
 * --pe-order: pe_order kernel を実行する。配る側だけを通し、転置の無い @collect で PE の順に
 *             書いた出力から、番号 k のデータが PE 4 * (k % 1024) + k / 1024 に入っていることを見る。
 *
 * 入力の値: 番号 k のデータの 4 値は 1000 + k, 2000 + k, 3000 + k, 4000 + k (float64)。
 * 形式は float64 little-endian、4096 個 (= 16384 double = 131072 byte)。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include "mnc2.h"

#define PE    4096
#define N     (4 * PE)
#define BYTES (N * sizeof(double))
#define MAX_REPORT 20

/* 配る側を通したとき、番号 k のデータが入る PE (directives-spec.md「データと PE の対応が変わる」)。 */
static int pe_of(int k) { return 4 * (k % 1024) + k / 1024; }

int main(int argc, char **argv)
{
    int pe_order = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--pe-order")) pe_order = 1;
        else { fprintf(stderr, "使い方: %s [--pe-order]\n", argv[0]); return 2; }
    }

    mnc2_device_t dev = mnc2_open(0);
    if (!dev) { fprintf(stderr, "FAIL: mnc2_open\n"); return 1; }
    void *ib = mnc2_alloc_host_buffer(dev, BYTES), *ob = mnc2_alloc_host_buffer(dev, BYTES);
    if (!ib || !ob) { fprintf(stderr, "FAIL: alloc\n"); mnc2_close(dev); return 1; }
    int rc = 0;
    double *in = (double *)ib, *out = (double *)ob;

    /* 番号の順の並び: データ k の 4 値が in[4k .. 4k+3] */
    for (int k = 0; k < PE; k++)
        for (int c = 0; c < 4; c++) in[4 * k + c] = (double)((c + 1) * 1000 + k);

    if (mnc2_send(dev, ib, 0, BYTES, 0x10) != MNC2_SUCCESS) { fprintf(stderr, "FAIL: send\n"); rc = 1; goto done; }

    /* kernel はバイナリと同じ _build/ にある (CWD 非依存にするため argv[0] から解決) */
    char kp[512];
    { char *d = strdup(argv[0]); snprintf(kp, sizeof(kp), "%s/%s.idma.dat", dirname(d), pe_order ? "pe_order" : "roundtrip"); free(d); }
    mnc2_kernel_t k = mnc2_load_kernel(dev, kp);
    if (!k) { fprintf(stderr, "FAIL: load %s\n", kp); rc = 1; goto done; }
    if (mnc2_exec_kernel(k) != MNC2_SUCCESS) { fprintf(stderr, "FAIL: exec\n"); rc = 1; goto done; }
    mnc2_free_kernel(k);
    memset(ob, 0, BYTES);
    if (mnc2_recv(dev, ob, 131072ULL * 8, BYTES, 0x1e) != MNC2_SUCCESS) { fprintf(stderr, "FAIL: recv\n"); rc = 1; goto done; }

    int bad = 0;
    if (!pe_order) {
        /* 対で使うと元に戻る: 全 16384 u64 が入力と一致 */
        for (int i = 0; i < N; i++) {
            if (memcmp(&out[i], &in[i], sizeof(double)) == 0) continue;
            if (bad < MAX_REPORT)
                fprintf(stderr, "  mismatch: データ %d の値 %d: 期待 %.0f 実際 %.17g\n", i / 4, i % 4, in[i], out[i]);
            bad++;
        }
        if (bad == 0) printf("  PASS: roundtrip。全 %d 個 (16384 u64) が入力と一致。対で使うと元の並びに戻る\n", PE);
        else { fprintf(stderr, "  FAIL: roundtrip。不一致 %d / %d u64\n", bad, N); rc = 1; }
    } else {
        /* 成分ごとの並び: 成分 c の PE j の値は out[c * 4096 + j]。番号 k は PE pe_of(k) にある */
        for (int kk = 0; kk < PE; kk++) {
            int j = pe_of(kk);
            for (int c = 0; c < 4; c++) {
                double expect = (double)((c + 1) * 1000 + kk);
                if (out[c * PE + j] == expect) continue;
                if (bad < MAX_REPORT)
                    fprintf(stderr, "  mismatch: 番号 %d の値 %d (PE %d): 期待 %.0f 実際 %.17g\n", kk, c, j, expect, out[c * PE + j]);
                bad++;
            }
        }
        if (bad == 0) printf("  PASS: pe_order。全 %d 個で番号 k のデータが PE 4 * (k %% 1024) + k / 1024 にある\n", PE);
        else { fprintf(stderr, "  FAIL: pe_order。不一致 %d / %d u64\n", bad, N); rc = 1; }
    }

done:
    if (ib) mnc2_free_host_buffer(dev, ib, BYTES);
    if (ob) mnc2_free_host_buffer(dev, ob, BYTES);
    mnc2_close(dev);
    return rc ? 1 : 0;
}
