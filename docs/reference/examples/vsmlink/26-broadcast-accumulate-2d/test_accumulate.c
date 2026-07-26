/* test_accumulate.c — 2D 質量重み付き総和 (ホストループ + LM 累積, emu:lib)
 *
 * 星の配列を 1 つ持ち、そこから distribute 用の並びと broadcast 用の並びと golden を
 * 導出する。各 PE が自分の星を持ち、先頭 NJ 個の星を他粒子として broadcast で順に配って
 * 総和を LM に累積する。
 *   ax[i] = sum_j m[j]*(x[i]-x[j])
 *   ay[i] = sum_j m[j]*(y[i]-y[j])
 * init 1 回 (自粒子 distribute + ax,ay=0) + accumulate NJ 回 + collect 1 回。
 * accumulator は exec 間で LM に保持される。
 *
 * rsqrt を使わないので装置側は加算と乗算だけになり、ホストが同じ順序で計算すれば
 * ビット一致で再現できる。したがって照合は許容誤差ゼロで行う。
 *
 * 自己相互作用 (j == i) は除外しない。x[i]-x[j] が 0 になり寄与が厳密にゼロなので、
 * golden 側も同じ式で自然に一致する。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "mnc2.h"

#define ELEM_COUNT  4096
#define ELEM_BYTES  (ELEM_COUNT * sizeof(double))
#define BCAST_COUNT 64
#define BCAST_BYTES (BCAST_COUNT * sizeof(double))
/* 他粒子として回す個数。全対全なら ELEM_COUNT だが、send と exec を 4096 組実行すると
   エミュレータでのテスト時間が現実的でないため打ち切っている。値そのものに構造的な
   意味は無い。2D の次元数や broadcast の size とは無関係である */
#define NJ 7
/* distribute (位置) は size 2 の 1 回。PDM 上は [x 4096][y 4096] の連続した並び */
#define OFF_XI   0
#define OFF_YI   (4096ULL * 8)
#define OFF_PJ   (12288ULL * 8)
#define OFF_MJ   (12352ULL * 8)
/* collect は size 2 の 1 回。ax が先頭 4096 個、ay が続く 4096 個で連続して並ぶ */
#define OFF_AX   (131072ULL * 8)
#define DMAID_TRIGGER 0x10
#define TAG_AX 0x1e

struct star { double x, y, m; };

int main(void) {
    printf("=== 2D 質量重み付き総和 (4096 星, NJ=%d, ビット一致照合) ===\n\n", NJ);
    mnc2_device_t dev = mnc2_open(0);
    if (!dev) { fprintf(stderr, "FAIL: open\n"); return 1; }

    void *xi_b = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    void *yi_b = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    void *bc_b = mnc2_alloc_host_buffer(dev, BCAST_BYTES);
    void *bm_b = mnc2_alloc_host_buffer(dev, BCAST_BYTES);
    /* collect は size 2 の 1 回なので、ax と ay を連続した 1 つの領域で受ける */
    void *a_b = mnc2_alloc_host_buffer(dev, 2 * ELEM_BYTES);
    struct star *stars = (struct star *)malloc(ELEM_COUNT * sizeof(struct star));
    if (!xi_b || !yi_b || !bc_b || !bm_b || !a_b || !stars) {
        fprintf(stderr, "FAIL: alloc\n"); mnc2_close(dev); return 1;
    }
    int rc = 0;

    /* 星の配列。これが唯一のデータ源で、以下の並びはすべてここから作る */
    for (int i = 0; i < ELEM_COUNT; i++) {
        stars[i].x = 1.0 + i * 0.001;
        stars[i].y = 2.0 - i * 0.0007;
        stars[i].m = 0.5 + (i % 17) * 0.125;
    }

    mnc2_kernel_t k_init = mnc2_load_kernel(dev, "_build/init.idma.dat");
    mnc2_kernel_t k_acc  = mnc2_load_kernel(dev, "_build/accumulate.idma.dat");
    mnc2_kernel_t k_col  = mnc2_load_kernel(dev, "_build/collect.idma.dat");
    if (!k_init || !k_acc || !k_col) { fprintf(stderr, "FAIL: load\n"); rc = 1; goto done; }

    /* --- init: 星の配列から distribute 用の並びを作って送る ---
       @distribute size 2 は成分でまとめた並び [x 4096][y 4096] を要求する。
       自粒子の質量は配らない (総和が使うのは他粒子の質量だけ) */
    {
        double *px = (double *)xi_b, *py = (double *)yi_b;
        for (int i = 0; i < ELEM_COUNT; i++) {
            px[i] = stars[i].x;
            py[i] = stars[i].y;
        }
    }
    if (mnc2_send(dev, yi_b, OFF_YI, ELEM_BYTES, 0) != MNC2_SUCCESS) { rc = 1; goto done; }
    if (mnc2_send(dev, xi_b, OFF_XI, ELEM_BYTES, DMAID_TRIGGER) != MNC2_SUCCESS) { rc = 1; goto done; }
    if (mnc2_exec_kernel(k_init) != MNC2_SUCCESS) { fprintf(stderr, "FAIL: init\n"); rc = 1; goto done; }

    /* --- ループ: 先頭 NJ 個の星を他粒子として順に配る ---
       @broadcast は 1 粒子の成分が連続した並びを要求するので、distribute とは別の
       並びになる。同じ星の配列から j 番目を取り出して受け渡し領域へ置く */
    for (int j = 0; j < NJ; j++) {
        double *bp = (double *)bc_b, *bm = (double *)bm_b;
        for (int t = 0; t < BCAST_COUNT; t++) { bp[t] = 0.0; bm[t] = 0.0; }
        bp[0] = stars[j].x; bp[1] = stars[j].y;   /* 源 u64[i] が $lr16+2i に着地 */
        bm[0] = stars[j].m;
        if (mnc2_send(dev, bm_b, OFF_MJ, BCAST_BYTES, 0) != MNC2_SUCCESS) { rc = 1; goto done; }
        if (mnc2_send(dev, bc_b, OFF_PJ, BCAST_BYTES, DMAID_TRIGGER) != MNC2_SUCCESS) { rc = 1; goto done; }
        if (mnc2_exec_kernel(k_acc) != MNC2_SUCCESS) { fprintf(stderr, "FAIL: acc j=%d\n", j); rc = 1; goto done; }
    }

    /* --- collect: ax,ay を回収 --- */
    if (mnc2_exec_kernel(k_col) != MNC2_SUCCESS) { fprintf(stderr, "FAIL: collect\n"); rc = 1; goto done; }
    memset(a_b, 0, 2 * ELEM_BYTES);
    if (mnc2_recv(dev, a_b, OFF_AX, 2 * ELEM_BYTES, TAG_AX) != MNC2_SUCCESS) { rc = 1; goto done; }

    /* --- golden 照合 (ビット一致) --- */
    {
        double *dax = (double *)a_b, *day = (double *)a_b + ELEM_COUNT;
        int errors = 0;
        for (int i = 0; i < ELEM_COUNT; i++) {
            /* 装置と同じ順序で計算する。浮動小数点の加算は結合則が成り立たないので、
               順序が違うと下位ビットがずれる。積と和を別の文に分けて、FMA への
               自動融合が起きても 1 命令にまとまらないようにする */
            double ax = 0.0, ay = 0.0;
            for (int j = 0; j < NJ; j++) {
                double dx = stars[i].x - stars[j].x;
                double dy = stars[i].y - stars[j].y;
                double fx = stars[j].m * dx;
                double fy = stars[j].m * dy;
                ax = ax + fx;
                ay = ay + fy;
            }
            if (memcmp(&dax[i], &ax, sizeof(double)) != 0 ||
                memcmp(&day[i], &ay, sizeof(double)) != 0) {
                if (errors < 5) {
                    uint64_t gx, gy, rx, ry;
                    memcpy(&gx, &ax, 8);      memcpy(&rx, &dax[i], 8);
                    memcpy(&gy, &ay, 8);      memcpy(&ry, &day[i], 8);
                    fprintf(stderr, "  MISMATCH[%d]\n", i);
                    fprintf(stderr, "    ax 実測 %.17g (%016llx) 期待 %.17g (%016llx)\n",
                            dax[i], (unsigned long long)rx, ax, (unsigned long long)gx);
                    fprintf(stderr, "    ay 実測 %.17g (%016llx) 期待 %.17g (%016llx)\n",
                            day[i], (unsigned long long)ry, ay, (unsigned long long)gy);
                }
                errors++;
            }
        }
        if (errors > 0) {
            fprintf(stderr, "  FAIL: %d/%d PE でビット不一致\n", errors, ELEM_COUNT);
            rc = 1;
        } else {
            printf("  PASS: 全 %d PE でビット一致 (先頭 %d 星を他粒子として回した)\n",
                   ELEM_COUNT, NJ);
            rc = 0;
        }
    }

done:
    free(stars);
    if (xi_b) mnc2_free_host_buffer(dev, xi_b, ELEM_BYTES);
    if (yi_b) mnc2_free_host_buffer(dev, yi_b, ELEM_BYTES);
    if (bc_b) mnc2_free_host_buffer(dev, bc_b, BCAST_BYTES);
    if (bm_b) mnc2_free_host_buffer(dev, bm_b, BCAST_BYTES);
    if (a_b) mnc2_free_host_buffer(dev, a_b, 2 * ELEM_BYTES);
    mnc2_close(dev);
    return rc ? 1 : 0;
}
