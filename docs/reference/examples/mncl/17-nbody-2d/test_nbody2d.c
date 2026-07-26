/* test_nbody2d.c -- 【Step 5】位置と力を double2 にまとめ、ベクタのまま計算する
 *
 * fx[i] = sum_{k} m_k*(x_k-x_i)/r^3, fy[i] = sum_{k} m_k*(y_k-y_i)/r^3   (r^2 = dx^2+dy^2+eps^2)
 * on N particles (PE 0..N-1)。
 *
 * PE 状態はカーネルをまたいで保持できないので、力積算値 f を PDM に常駐させ、各カーネルが
 * distribute で読んで加算し collect で書き戻す（10-odd-even-sort の PDM 常駐パターン）。host は
 * 粒子 k を broadcast してカーネルを起動し、毎回 f を drain する。ループ後に f を引き上げ、
 * host の O(N^2) 直接計算 (golden) と相対誤差で照合する。
 *
 * **Step 4 との違い: x/y と fx/fy を double2 にまとめ、ベクタのまま計算する。**
 * distribute が 4 本から 2 本、collect が 2 本から 1 本に減る。スカラ × ベクタ（splat）も使う。
 *
 * **PDM 上の並びは「成分が外側、PE が内側」である。**
 *
 *   pos: [x を 4096 PE 分][y を 4096 PE 分]
 *   f:   [fx を 4096 PE 分][fy を 4096 PE 分]
 *
 * OpenCL の double2 配列（PE ごとに x,y が隣接）とは違う。l1bmd が addr_b + cycle * 64 で
 * アクセスするため HW で決まっており選べない。**この並びを作るのがホストの責務である。**
 *
 * PDM layout (nbody2d.param):
 *   slot 8  (pos):   word 0      distribute size 2（初回のみ送信、PDM 常駐）
 *   slot 16 (bpos):  word 8192   broadcast size 2（毎回 先頭 2 u64 を粒子 k の位置で埋める）
 *   slot 24 (bmass): word 8320   broadcast size 1（粒子 k の質量）
 *   slot 32 (f):     word 12288  distribute size 2 + collect size 2 の in-place 積算
 *
 * テストは N を小さく (64) して emu:lib で現実的な時間にする（1 ターン = N カーネル起動）。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "mnc2.h"

#define N_PE       4096
#define N          64                    /* 実粒子数（PE 0..N-1）。残り PE は質量 0 で無害化 */
#define ELEM_BYTES (N_PE * sizeof(double))
#define VEC2_BYTES (2 * N_PE * sizeof(double))   /* double2 は 2 成分ぶん */
#define BC_LW      64
#define BC_BYTES   (BC_LW * sizeof(double))
#define EPS2       0.0625

#define OFF_POS   (0ULL * 8)      /* [x 4096][y 4096] */
#define OFF_BPOS  (8192ULL * 8)   /* 粒子 k の位置 (x, y)。先頭 2 u64 に置く */
#define OFF_BMASS (8320ULL * 8)   /* 粒子 k の質量。先頭 1 u64 に置く */
#define OFF_F     (12288ULL * 8)  /* [fx 4096][fy 4096] */
#define SEND_TAG  0x10
#define RECV_F    0x1e

int main(void)
{
    printf("=== 17-nbody-2d [Step 5]: N=%d 粒子の力を積算 (double2 でベクタ化, 4096 PE) ===\n\n", N);

    mnc2_device_t dev = mnc2_open(0);
    if (!dev) { fprintf(stderr, "FAIL: mnc2_open\n"); return 1; }
    printf("17-nbody-2d backend: %s\n", mnc2_get_backend_name(dev));

    void *posbuf   = mnc2_alloc_host_buffer(dev, VEC2_BYTES);
    void *zbuf     = mnc2_alloc_host_buffer(dev, VEC2_BYTES);   /* f 初期化用 0 */
    void *bposbuf  = mnc2_alloc_host_buffer(dev, BC_BYTES);     /* 粒子 k の位置 */
    void *bmassbuf = mnc2_alloc_host_buffer(dev, BC_BYTES);     /* 粒子 k の質量 */
    void *rf       = mnc2_alloc_host_buffer(dev, VEC2_BYTES);
    double *x = (double *)malloc(ELEM_BYTES);
    double *y = (double *)malloc(ELEM_BYTES);
    double *m = (double *)malloc(ELEM_BYTES);
    mnc2_kernel_t kernel = NULL;
    int rc = 0;

    if (!posbuf||!zbuf||!bposbuf||!bmassbuf||!rf||!x||!y||!m) {
        fprintf(stderr, "FAIL: alloc\n"); rc = 1; goto cleanup;
    }

    /* 粒子生成: PE 0..N-1 が実粒子、残りは質量 0（broadcast しない、検証しない）。
     * 力が大きく相殺しないよう広めに配置する */
    for (int i = 0; i < N_PE; i++) {
        if (i < N) {
            x[i] = (double)i * 2.0 - 64.0;
            y[i] = (double)(i % 8) * 3.0 - 12.0;
            m[i] = 1.0 + (double)(i % 4);
        } else {
            x[i] = 0.0; y[i] = 0.0; m[i] = 0.0;
        }
    }

    /* 初期化: f=0 を PDM に置く（send_wait_tag 無しスロット。最初の exec より前に発行）*/
    memset(zbuf, 0, VEC2_BYTES);
    if (mnc2_send(dev, zbuf, OFF_F, VEC2_BYTES, 0) != MNC2_SUCCESS) { fprintf(stderr, "FAIL: init f\n"); rc=1; goto cleanup; }

    /* pos を常駐送信（初回のみ、tag 0。以降は broadcast の #x10 トリガで読まれる）。
     * **成分が外側**なので x を先頭 4096 個、y をその後ろに置く。 */
    {
        double *p = (double *)posbuf;
        for (int i = 0; i < N_PE; i++) p[i]         = x[i];   /* 成分 0 = x */
        for (int i = 0; i < N_PE; i++) p[N_PE + i]  = y[i];   /* 成分 1 = y */
    }
    if (mnc2_send(dev, posbuf, OFF_POS, VEC2_BYTES, 0) != MNC2_SUCCESS) { fprintf(stderr, "FAIL: send pos\n"); rc=1; goto cleanup; }

    kernel = mnc2_load_kernel(dev, "_build/nbody2d.idma.dat");
    if (!kernel) { fprintf(stderr, "FAIL: load_kernel\n"); rc=1; goto cleanup; }

    /* host loop: 粒子 k を broadcast してカーネル起動、f を drain */
    for (int k = 0; k < N; k++) {
        /* 粒子 k の位置を先頭 2 u64、質量を先頭 1 u64 に置く（size N）。
         * broadcast は 1 個の値を全 PE に配るだけなので、こちらは並びの問題が無い。 */
        { double *p = (double *)bposbuf;
          for (int i=0;i<BC_LW;i++) p[i]=0.0;
          p[0]=x[k]; p[1]=y[k]; }
        { double *p = (double *)bmassbuf;
          for (int i=0;i<BC_LW;i++) p[i]=m[k]; }
        if (mnc2_send(dev, bposbuf, OFF_BPOS, BC_BYTES, 0) != MNC2_SUCCESS)          { fprintf(stderr,"FAIL: send bpos k=%d\n",k); rc=1; goto cleanup; }
        if (mnc2_send(dev, bmassbuf, OFF_BMASS, BC_BYTES, SEND_TAG) != MNC2_SUCCESS) { fprintf(stderr,"FAIL: send bmass k=%d\n",k); rc=1; goto cleanup; }
        if (mnc2_exec_kernel(kernel) != MNC2_SUCCESS)                          { fprintf(stderr,"FAIL: exec k=%d\n",k); rc=1; goto cleanup; }
        if (mnc2_recv(dev, rf, OFF_F, VEC2_BYTES, RECV_F) != MNC2_SUCCESS)      { fprintf(stderr,"FAIL: recv f k=%d\n",k); rc=1; goto cleanup; }
    }

    /* golden: O(N^2) 直接計算と相対誤差で照合（i,k とも 0..N-1。self k==i は softening で力 0）*/
    {
        double *pf = (double *)rf;
        double *pfx = pf;              /* 成分 0 = fx */
        double *pfy = pf + N_PE;       /* 成分 1 = fy */
        int errors = 0;
        double max_rel = 0.0;
        for (int i = 0; i < N; i++) {
            double gfx = 0.0, gfy = 0.0;
            for (int k = 0; k < N; k++) {
                double dx = x[k]-x[i], dy = y[k]-y[i];
                double r2 = dx*dx + dy*dy + EPS2;
                double ir = 1.0 / sqrt(r2), ir3 = ir*ir*ir;
                gfx += m[k]*dx*ir3;
                gfy += m[k]*dy*ir3;
            }
            double dfx = fabs(pfx[i]-gfx), dfy = fabs(pfy[i]-gfy);
            double tolx = 1e-6*fabs(gfx) + 1e-9, toly = 1e-6*fabs(gfy) + 1e-9;
            double relx = fabs(gfx)>0 ? dfx/fabs(gfx) : 0.0;
            double rely = fabs(gfy)>0 ? dfy/fabs(gfy) : 0.0;
            if (relx>max_rel) max_rel = relx;
            if (rely>max_rel) max_rel = rely;
            if (dfx>tolx || dfy>toly) {
                if (errors<5) fprintf(stderr,"  MISMATCH [%d]: fx got=%.9g exp=%.9g / fy got=%.9g exp=%.9g\n", i, pfx[i], gfx, pfy[i], gfy);
                errors++;
            }
        }
        printf("  max relative error = %.3e\n", max_rel);
        if (errors) { fprintf(stderr, "  FAIL: %d mismatches\n", errors); rc = 1; }
        else { printf("  PASS: fx[i], fy[i] が O(N^2) golden に一致 (N=%d 積算, double2 でベクタ化)\n", N); rc = 0; }
    }

cleanup:
    if (kernel)   mnc2_free_kernel(kernel);
    if (posbuf)   mnc2_free_host_buffer(dev, posbuf, VEC2_BYTES);
    if (zbuf)     mnc2_free_host_buffer(dev, zbuf, VEC2_BYTES);
    if (bposbuf)  mnc2_free_host_buffer(dev, bposbuf, BC_BYTES);
    if (bmassbuf) mnc2_free_host_buffer(dev, bmassbuf, BC_BYTES);
    if (rf)       mnc2_free_host_buffer(dev, rf, VEC2_BYTES);
    mnc2_close(dev);
    free(x); free(y); free(m);
    return (rc != 0) ? 1 : 0;
}
