/* bcast_soa2aos — broadcast -> 計算 -> soa2aos -> collect のループを閉じる確認
 *
 * examples/30-soa2aos はレイアウト変換単体 (PDM から読んで PDM へ書く) の確認で止まっており、その
 * 出力を @broadcast で読み直すところまで繋がっていない。ここではダブルバッファで
 * 2 タイムステップ回し、次の 2 つを確認する。
 *
 *   1. レイアウト変換が対合であること — 1 回目で順列がかかり、2 回目で元に戻る。m (PE ID) を
 *      追跡すれば並びが戻ったかどうかが分かる
 *   2. 計算が正しく行われること — ホスト側で同じ計算を倍精度で行い照合する
 *
 * 使い方: bcast_soa2aos --variant=<rot|rotx|logx>
 *   rot  … レイアウト変換なし。骨格だけの確認 (順列が発生しないので m[i] == i のまま)
 *   rotx … レイアウト変換あり。回転 (ノルム保存なので照合が安定)
 *   logx … レイアウト変換あり。ロジスティック写像 (計算部分が差し替えられることの確認)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <libgen.h>
#include "mnc2.h"

#define PE      4096
#define N       64                       /* 実際に使う粒子数 (gen_chunks_ninja.py の N) */
#define CHUNK   16                       /* 1 区画 = 64 u64 = 16 粒子 */
#define NSPLIT  ((N + CHUNK - 1) / CHUNK)
#define NSTEP   2                        /* ダブルバッファを 1 往復 */

#define SOA_COUNT (4 * PE)               /* distribute 用 (成分ごと) */
#define SOA_BYTES (SOA_COUNT * sizeof(double))
#define AOS_COUNT (4 * PE)               /* broadcast / collect 用 (粒子ごと) */
#define AOS_BYTES (AOS_COUNT * sizeof(double))

/* PDM のメモリマップ (u64 単位)。gen_step.py と README の表に一致させること。 */
#define ADDR_INIT 0
#define ADDR_BUF0 20480
#define ADDR_BUF1 40960
#define TAG_SEND  0x10
#define TAG_RECV  0x1e

static double g_x[PE], g_y[PE], g_z[PE], g_m[PE];   /* ホスト側の照合用 */

/* レイアウト変換の順列: レイアウト変換後の位置 k には、元の粒子 4*g+i が来る (k = 1024*i + g)。
   MAB 内の 4 PE x 4 アドレスを入れ替えるので、1024 MAB ぶんに広げるとこの形になる。 */
static int perm_src(int k) {
    int i = k / 1024, g = k % 1024;
    return 4 * g + i;
}

int main(int argc, char **argv) {
    const char *variant = "rotx";
    int show_order = 0;
    for (int i = 1; i < argc; i++) {
        if (!strncmp(argv[i], "--variant=", 10)) variant = argv[i] + 10;
        else if (!strcmp(argv[i], "--show-order")) show_order = 1;
    }
    int transpose = (strchr(variant, 'x') != NULL);
    int logistic = (variant[0] == 'l');

    printf("=== broadcast -> 計算 -> %scollect のループ (N=%d, %d 分割, %d タイムステップ) ===\n",
           transpose ? "soa2aos -> " : "", N, NSPLIT, NSTEP);
    printf("    計算: %s / レイアウト変換: %s\n\n", logistic ? "logistic" : "rotate", transpose ? "あり" : "なし");

    mnc2_device_t dev = mnc2_open(0);
    if (!dev) { fprintf(stderr, "FAIL: open\n"); return 1; }
    mnc2_set_idma_complete_max(dev, 1000000);

    void *soa_b = mnc2_alloc_host_buffer(dev, SOA_BYTES);
    void *aos_b = mnc2_alloc_host_buffer(dev, AOS_BYTES);
    void *out_b = mnc2_alloc_host_buffer(dev, AOS_BYTES);
    if (!soa_b || !aos_b || !out_b) { fprintf(stderr, "FAIL: alloc\n"); mnc2_close(dev); return 1; }
    int rc = 0;

    /* --- 初期値 ---
       m には PE ID (0..4095) を入れる。計算では触らないので、回収した m を見れば
       並びが入れ替わったかどうかが分かる。
       x, y は単位円上に取る (x^2 + y^2 = 1)。rotate は broadcast で読んだ他粒子の
       x, y を回転の c, s に使うので、これで回転がノルムを保つ。
       logistic は z を係数 4.0 として使う。 */
    for (int p = 0; p < PE; p++) {
        double th = 2.0 * M_PI * p / PE;
        g_x[p] = logistic ? (0.1 + 0.8 * p / (double)PE) : cos(th);
        g_y[p] = logistic ? (0.2 + 0.6 * p / (double)PE) : sin(th);
        /* logistic の係数。r=4.0 だと写像の最大値がちょうど 1.0 に達し、丸め誤差で 1 を
           超えた瞬間に (1-x) が負になって発散する。r=3.9 なら最大 0.975 で 1 に届かず、
           カオス性は保たれる。rotate では使わない。 */
        g_z[p] = 3.9;
        g_m[p] = (double)p;
    }

    /* distribute 用 (成分ごと): [x x x ...][y y y ...][z ...][m ...] */
    {
        double *s = (double *)soa_b;
        for (int p = 0; p < PE; p++) {
            s[p] = g_x[p]; s[PE + p] = g_y[p]; s[2 * PE + p] = g_z[p]; s[3 * PE + p] = g_m[p];
        }
        if (mnc2_send(dev, soa_b, ADDR_INIT * 8ULL, SOA_BYTES, TAG_SEND) != MNC2_SUCCESS) {
            fprintf(stderr, "FAIL: send SoA\n"); rc = 1; goto done;
        }
    }
    /* broadcast 用 (粒子ごと): [x y z m][x y z m]...  初回だけホストが用意する。
       2 タイムステップ目以降は device 側の collect が書く。 */
    {
        double *a = (double *)aos_b;
        for (int p = 0; p < PE; p++) {
            a[4 * p] = g_x[p]; a[4 * p + 1] = g_y[p]; a[4 * p + 2] = g_z[p]; a[4 * p + 3] = g_m[p];
        }
        if (mnc2_send(dev, aos_b, ADDR_BUF0 * 8ULL, AOS_BYTES, 0) != MNC2_SUCCESS) {
            fprintf(stderr, "FAIL: send AoS\n"); rc = 1; goto done;
        }
    }
    /* 分割 kernel は送信完了を待たない (.param に send_wait_tag を付けていない)。
       2 タイムステップ目以降はホストが送らず device の @collect がこの領域を書くので、
       kernel 内で待つ形にすると 2 回目が成立しないため。1 回目ぶんはここで待つ。 */
    if (mnc2_wait_idma_idle(dev) != MNC2_SUCCESS) { fprintf(stderr, "FAIL: idma idle (init)\n"); rc = 1; goto done; }

    /* --- kernel の load --- */
    char dir[512];
    { char *d = strdup(argv[0]); snprintf(dir, sizeof(dir), "%s", dirname(d)); free(d); }
    char path[600];
    snprintf(path, sizeof(path), "%s/init.idma.dat", dir);
    mnc2_kernel_t k_init = mnc2_load_kernel(dev, path);
    if (!k_init) { fprintf(stderr, "FAIL: load %s\n", path); rc = 1; goto done; }
    /* 1 タイムステップぶんの分割 kernel を、読み書きの向き (a/b) ごとに持つ */
    mnc2_kernel_t k[2][NSPLIT];
    for (int side = 0; side < 2; side++)
        for (int g = 0; g < NSPLIT; g++) {
            snprintf(path, sizeof(path), "%s/%s_%c_%d.idma.dat", dir, variant, side ? 'b' : 'a', g);
            k[side][g] = mnc2_load_kernel(dev, path);
            if (!k[side][g]) { fprintf(stderr, "FAIL: load %s\n", path); rc = 1; goto done; }
        }

    if (mnc2_exec_kernel(k_init) != MNC2_SUCCESS) { fprintf(stderr, "FAIL: init\n"); rc = 1; goto done; }

    /* --- タイムステップ ---
       **レイアウト変換なしは 1 回しか回せない。** @collect が書くのは成分ごと (SoA) の並びで、
       @broadcast が読むのは粒子ごと (AoS) の並びなので、レイアウト変換を挟まないと 2 回目の
       broadcast が壊れた並びを読むことになる。レイアウト変換ありは 2 回まわして順列が戻ることを見る。 */
    int nstep = transpose ? NSTEP : 1;
    for (int step = 0; step < nstep; step++) {
        int side = step % 2;                          /* 0: BUF0 -> BUF1、1: BUF1 -> BUF0 */
        unsigned long long dst = side ? ADDR_BUF0 : ADDR_BUF1;
        for (int g = 0; g < NSPLIT; g++)
            if (mnc2_exec_kernel(k[side][g]) != MNC2_SUCCESS) {
                fprintf(stderr, "FAIL: exec step=%d split=%d\n", step, g); rc = 1; goto done;
            }
        memset(out_b, 0, AOS_BYTES);
        if (mnc2_wait_idma_idle(dev) != MNC2_SUCCESS) { fprintf(stderr, "FAIL: idma idle\n"); rc = 1; goto done; }
        if (mnc2_recv(dev, out_b, dst * 8ULL, AOS_BYTES, TAG_RECV) != MNC2_SUCCESS) {
            fprintf(stderr, "FAIL: recv step=%d\n", step); rc = 1; goto done;
        }

        /* ホスト側でも同じ計算を進める (全粒子を読む = 全対全)。
           **broadcast が読む順序は、そのバッファを誰が書いたかで変わる。** 1 回目は
           ホストが書いた並び (順列なし) を読むが、2 回目は 1 回目の collect が書いた
           並び (順列あり) を読む。したがって device が j 番目に読むのは、レイアウト変換を経た
           あとでは粒子 perm_src(j) である。ホストもそれに合わせる。 */
        int permuted_input = transpose && (step % 2 == 1);
        double nx[PE], ny[PE];
        for (int p = 0; p < PE; p++) { nx[p] = g_x[p]; ny[p] = g_y[p]; }
        for (int j = 0; j < N; j++) {
            int jj = permuted_input ? perm_src(j) : j;
            for (int p = 0; p < PE; p++) {
                if (logistic) {
                    nx[p] = g_z[jj] * nx[p] * (1.0 - nx[p]);
                    ny[p] = g_z[jj] * ny[p] * (1.0 - ny[p]);
                } else {
                    double c = g_x[jj], s = g_y[jj], t = nx[p];
                    nx[p] = t * c - ny[p] * s;
                    ny[p] = t * s + ny[p] * c;
                }
            }
        }
        /* device は自分の LM 上の値を更新するので、全粒子ぶん反映してから次へ */
        for (int p = 0; p < PE; p++) { g_x[p] = nx[p]; g_y[p] = ny[p]; }

        /* --- 確認 ---
           **読み方がレイアウト変換の有無で変わる。** @collect は成分ごと (SoA) に書くので、
           レイアウト変換しなければ PDM には [x x x ...][y y y ...][z ...][m ...] が出る。
           レイアウト変換を挟むと粒子ごと (AoS) の [x y z m][x y z m]... になり、これが
           @broadcast が読める形である。**この違いが soa2aos を挟む理由そのもの。** */
        double *o = (double *)out_b;
        int bad_m = 0, bad_v = 0;
        double maxrel = 0;
        for (int kk = 0; kk < PE; kk++) {
            /* レイアウト変換ありなら順列がかかる。レイアウト変換なしなら位置はそのまま */
            int src = transpose ? perm_src(kk) : kk;
            double gx, gy, gm;
            if (transpose) { gx = o[4 * kk]; gy = o[4 * kk + 1]; gm = o[4 * kk + 3]; }
            else           { gx = o[kk];     gy = o[PE + kk];    gm = o[3 * PE + kk]; }
            if (gm != (double)src) {
                if (bad_m < 3)
                    fprintf(stderr, "  m ずれ: 位置 %d に %g (期待 %d)\n", kk, gm, src);
                bad_m++;
            }
            double ex = g_x[src], ey = g_y[src];
            double dx = fabs(gx - ex) / (fabs(ex) > 1e-3 ? fabs(ex) : 1e-3);
            double dy = fabs(gy - ey) / (fabs(ey) > 1e-3 ? fabs(ey) : 1e-3);
            if (dx > maxrel) maxrel = dx;
            if (dy > maxrel) maxrel = dy;
            if (dx > 1e-9 || dy > 1e-9) bad_v++;
        }
        if (bad_m) { fprintf(stderr, "  FAIL: step %d で m の並びが %d 個ずれた\n", step, bad_m); rc = 1; goto done; }
        /* --show-order: PDM の実際の並びを出す。**先頭だけを見てはいけない。**
           順列 perm_src(k) = 4*(k%1024) + k/1024 は k < 1024 では単に 4k になるため、
           先頭 1024 個では「順列がかかった状態」と「元のまま」の区別がつかない。
           k >= 1024 を必ず含めること。 */
        if (show_order) {
            static const int ks[] = { 0, 1, 2, 3, 4, 5, 6, 7, 1024, 1025, 2048, 3072, 4095 };
            printf("  step %d の collect 出力 (位置 k にある m = 元の要素番号):\n", step);
            printf("    %8s", "k");
            for (size_t t = 0; t < sizeof(ks) / sizeof(ks[0]); t++) printf(" %6d", ks[t]);
            printf("\n    %8s", "m");
            for (size_t t = 0; t < sizeof(ks) / sizeof(ks[0]); t++)
                printf(" %6d", (int)(transpose ? o[4 * ks[t] + 3] : o[3 * PE + ks[t]]));
            printf("\n");
        }
        printf("  step %d: 並び %s / 値 max_rel=%.3e%s\n", step,
               transpose ? "順列あり (毎ステップ同じ形)" : "変化なし (期待どおり)",
               maxrel, bad_v ? " ← ずれあり" : "");
        if (bad_v) { fprintf(stderr, "  FAIL: step %d で値が %d 個ずれた\n", step, bad_v); rc = 1; goto done; }
    }

    printf("\n  PASS: %s のループが閉じた。%s\n", variant,
           transpose ? "レイアウト変換の順列は毎ステップ同じ形でかかる (累積しない)" : "順列は発生しない");

done:
    if (soa_b) mnc2_free_host_buffer(dev, soa_b, SOA_BYTES);
    if (aos_b) mnc2_free_host_buffer(dev, aos_b, AOS_BYTES);
    if (out_b) mnc2_free_host_buffer(dev, out_b, AOS_BYTES);
    mnc2_close(dev);
    return rc ? 1 : 0;
}
