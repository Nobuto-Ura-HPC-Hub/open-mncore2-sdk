/* test_nbody3d.c — 段階6: 3D N 体 leapfrog + 入出力ファイル (unroll, emu:lib)
 *
 * 段階5b の leapfrog に、入力の mmap 読み込みと _result/ へのスナップショット出力を足す。
 * 入力: struct 形バイナリ (1 粒子 8 double: x,y,z,m,vx,vy,vz,pad)。argv[1] か既定
 *       _build/ic.bin を mmap して読む。正規化済み (mkplummer 等) の Plummer 球も同形式。
 * 出力: _result/<tag>_<ts>_<NNNN>.bin (tag=pos/vel、ts=開始時刻、NNNN=ファイル連番) と
 *       run のマニフェスト JSON。物理検証 (値照合 + エネルギー保存) は 5b と同じ。
 *
 * N / newton / NSTEP は build.ninja から -DUNROLL / -DNEWTON / -DNSTEP で渡る。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include "mnc2.h"

#ifndef UNROLL
#define UNROLL 8
#endif
#ifndef NEWTON
#define NEWTON 0
#endif
#ifndef NSTEP
#define NSTEP 3
#endif
#define M UNROLL

#define PE_COUNT    4096
#define ELEM_BYTES  (PE_COUNT * sizeof(double))
#define DIST_COUNT  (4 * PE_COUNT)
#define DIST_BYTES  (DIST_COUNT * sizeof(double))
#define VEL_COUNT   (3 * PE_COUNT)
#define VEL_BYTES   (VEL_COUNT * sizeof(double))
#define BCAST_COUNT 64
#define BCAST_BYTES (BCAST_COUNT * sizeof(double))
#define PJ_COUNT    (((4 * M + BCAST_COUNT - 1) / BCAST_COUNT) * BCAST_COUNT)
#define PJ_BYTES    (PJ_COUNT * sizeof(double))

#define OFF_DIST 0
#define OFF_CONST (16384ULL * 8)
#define OFF_PJ   (20480ULL * 8)
#define OFF_VEL  (40960ULL * 8)
#define OFF_POS  (131072ULL * 8)
#define OFF_VOUT (143360ULL * 8)
#define DMAID_TRIGGER 0x10
#define TAG_POS 0x1e
#define TAG_VEL 0x1f
#define EPS2 0.01
#define DT   0.015625
#define ETOL 0.01
#define BOUND_FACTOR 5.0                     /* 系の最大半径が初期の何倍まで許すか (束縛の確認) */
#if NEWTON >= 3
#define TOL 1e-6
#else
#define TOL 0.2
#endif
#define ABSFLOOR 0.02

#define POS_EVERY 1                          /* 位置スナップショットの間隔 (ステップ) */
#define VEL_EVERY 1                          /* 速度スナップショットの間隔 */
#define SNAP_PER_FILE 128                    /* 1 ファイルにまとめるスナップショット数 */
#ifndef IC_DEFAULT
#define IC_DEFAULT "_build/ic.bin"
#endif
#ifndef RESULT_DIR
#define RESULT_DIR "_result"
#endif
/* accumulate は CHUNK 粒子ずつの分割カーネル。ACC_FMT は分割ファイル名の printf 書式
   (%d に分割番号 g)。全 N を 1 カーネルに unroll すると device の連続 DMA 確保に失敗して
   load できないため。NCHUNK = 分割数。 */
#ifndef ACC_FMT
#define ACC_FMT "_build/accumulate_n32_g%d.idma.dat"
#endif
#ifndef CHUNK
#define CHUNK 128
#endif
#define NCHUNK ((M + CHUNK - 1) / CHUNK)

struct star { double x, y, z, m, vx, vy, vz, pad; };

/* スナップショット書き出し (tag ごと、K 個で次ファイル) */
typedef struct { char tag[8], ts[24]; int n, count, fidx; FILE *fp; } snapw;
static void snap_write(snapw *w, const double *a, const double *b, const double *c) {
    if (!w->fp || w->count >= SNAP_PER_FILE) {
        if (w->fp) fclose(w->fp);
        char path[256];
        snprintf(path, sizeof(path), "%s/%s_%s_%04d.bin", RESULT_DIR, w->tag, w->ts, w->fidx++);
        w->fp = fopen(path, "wb");
        w->count = 0;
    }
    if (w->fp)
        for (int k = 0; k < w->n; k++) { double p[3] = { a[k], b[k], c[k] }; fwrite(p, sizeof(double), 3, w->fp); }
    w->count++;
}
static void snap_close(snapw *w) { if (w->fp) { fclose(w->fp); w->fp = NULL; } }

static void host_accel(int n, const double *x, const double *y, const double *z, const double *m,
                       double *ax, double *ay, double *az) {
    for (int i = 0; i < n; i++) {
        double sx = 0, sy = 0, sz = 0;
        for (int j = 0; j < n; j++) {
            double dx = x[j] - x[i], dy = y[j] - y[i], dz = z[j] - z[i];
            double r2 = dx * dx + dy * dy + dz * dz + EPS2, ir = 1.0 / sqrt(r2), s = m[j] * ir * ir * ir;
            sx += s * dx; sy += s * dy; sz += s * dz;
        }
        ax[i] = sx; ay[i] = sy; az[i] = sz;
    }
}
static double total_energy(int n, const double *x, const double *y, const double *z, const double *m,
                           const double *vx, const double *vy, const double *vz) {
    double ke = 0, pe = 0;
    for (int i = 0; i < n; i++) ke += 0.5 * m[i] * (vx[i] * vx[i] + vy[i] * vy[i] + vz[i] * vz[i]);
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++) {
            double dx = x[i] - x[j], dy = y[i] - y[j], dz = z[i] - z[j];
            pe -= m[i] * m[j] / sqrt(dx * dx + dy * dy + dz * dz + EPS2);
        }
    return ke + pe;
}
/* 系の重心からの最大半径 (束縛の確認用)。符号ミスで斥力になると多数ステップで発散する */
static double max_radius(int n, const double *x, const double *y, const double *z, const double *m) {
    double mt = 0, cx = 0, cy = 0, cz = 0;
    for (int i = 0; i < n; i++) { mt += m[i]; cx += m[i] * x[i]; cy += m[i] * y[i]; cz += m[i] * z[i]; }
    cx /= mt; cy /= mt; cz /= mt;
    double rm = 0;
    for (int i = 0; i < n; i++) {
        double dx = x[i] - cx, dy = y[i] - cy, dz = z[i] - cz, r = sqrt(dx * dx + dy * dy + dz * dz);
        if (r > rm) rm = r;
    }
    return rm;
}

/* IDMA queue (depth 31) を溢れさせずに 1 カーネル exec する。n_dma が 31 未満になるまで
   待ってから kick する。完全 idle 待ち (mnc2_wait_idma_idle) だと queue が毎回空になり
   depth 31 の並列性が境界で切れるので、n_dma を見て空きができたら 1 個補充する。
   分割カーネル間は LM 依存で FIFO 順に実行されるため drain は不要。 */
static int exec_q(mnc2_device_t dev, mnc2_kernel_t k) {
    mnc2_idma_stat_t st;
    while (mnc2_get_idma_stat(dev, &st) == MNC2_SUCCESS && st.n_dma >= 31) {
        /* queue 満杯。device が処理して空くまでポーリング */
    }
    return mnc2_exec_kernel(k);
}

int main(int argc, char **argv) {
    const char *ic_path = (argc > 1) ? argv[1] : IC_DEFAULT;
    printf("=== 3D N 体 leapfrog + file (N=%d, newton=%d, %d ステップ, 入力 %s) ===\n\n", M, NEWTON, NSTEP, ic_path);
    mnc2_device_t dev = mnc2_open(0);
    if (!dev) { fprintf(stderr, "FAIL: open\n"); return 1; }
    /* 大きい N では recv 前の IDMA idle 待ちで多数の分割を流し切る。待ち上限 (default 5000)
       が足りない場合に備えて上げる。 */
    mnc2_set_idma_complete_max(dev, 1000000);

    void *dist_b = mnc2_alloc_host_buffer(dev, DIST_BYTES), *vel_b = mnc2_alloc_host_buffer(dev, VEL_BYTES);
    void *con_b = mnc2_alloc_host_buffer(dev, BCAST_BYTES), *bc_b = mnc2_alloc_host_buffer(dev, PJ_BYTES);
    void *pos_b = mnc2_alloc_host_buffer(dev, 3 * ELEM_BYTES), *vout_b = mnc2_alloc_host_buffer(dev, 3 * ELEM_BYTES);
    double *hx = malloc(ELEM_BYTES), *hy = malloc(ELEM_BYTES), *hz = malloc(ELEM_BYTES), *hm = malloc(ELEM_BYTES);
    double *hvx = malloc(ELEM_BYTES), *hvy = malloc(ELEM_BYTES), *hvz = malloc(ELEM_BYTES);
    double *gx = malloc(ELEM_BYTES), *gy = malloc(ELEM_BYTES), *gz = malloc(ELEM_BYTES);
    double *gvx = malloc(ELEM_BYTES), *gvy = malloc(ELEM_BYTES), *gvz = malloc(ELEM_BYTES);
    double *ga = malloc(ELEM_BYTES), *gb = malloc(ELEM_BYTES), *gc = malloc(ELEM_BYTES);
    if (!dist_b || !vel_b || !con_b || !bc_b || !pos_b || !vout_b || !hx || !hy || !hz || !hm ||
        !hvx || !hvy || !hvz || !gx || !gy || !gz || !gvx || !gvy || !gvz || !ga || !gb || !gc) {
        fprintf(stderr, "FAIL: alloc\n"); mnc2_close(dev); return 1;
    }
    int rc = 0;

    /* --- 入力: struct 形バイナリを mmap して先頭 N 粒子を読む --- */
    {
        int fd = open(ic_path, O_RDONLY);
        if (fd < 0) { fprintf(stderr, "FAIL: open input %s\n", ic_path); rc = 1; goto done; }
        size_t need = (size_t)M * sizeof(struct star);
        struct star *ic = mmap(NULL, need, PROT_READ, MAP_PRIVATE, fd, 0);
        close(fd);
        if (ic == MAP_FAILED) { fprintf(stderr, "FAIL: mmap %s\n", ic_path); rc = 1; goto done; }
        for (int i = 0; i < M; i++) {
            hx[i] = ic[i].x; hy[i] = ic[i].y; hz[i] = ic[i].z; hm[i] = ic[i].m;
            hvx[i] = ic[i].vx; hvy[i] = ic[i].vy; hvz[i] = ic[i].vz;
        }
        munmap(ic, need);
        /* 系に属さない PE (N..4095) は粒子 0 で埋める (計算は無視するが NaN を避ける) */
        for (int i = M; i < PE_COUNT; i++) { hx[i] = hx[0]; hy[i] = hy[0]; hz[i] = hz[0]; hm[i] = hm[0]; hvx[i] = hvx[0]; hvy[i] = hvy[0]; hvz[i] = hvz[0]; }
    }
    for (int i = 0; i < M; i++) { gx[i] = hx[i]; gy[i] = hy[i]; gz[i] = hz[i]; gvx[i] = hvx[i]; gvy[i] = hvy[i]; gvz[i] = hvz[i]; }
    double rmax0 = max_radius(M, hx, hy, hz, hm);   /* 初期の系の最大半径 */

    /* --- 出力: _result/ とマニフェスト --- */
    char ts[24];
    { time_t t = time(NULL); struct tm *lt = localtime(&t); strftime(ts, sizeof(ts), "%y%m%d_%H%M%S", lt); }
    mkdir(RESULT_DIR, 0755);
    {
        char mpath[256]; snprintf(mpath, sizeof(mpath), "%s/run_%s.json", RESULT_DIR, ts);
        FILE *mf = fopen(mpath, "w");
        if (mf) {
            fprintf(mf, "{\n  \"N\": %d,\n  \"dt\": %.17g,\n  \"nsteps\": %d,\n  \"eps2\": %.17g,\n",
                    M, (double)DT, NSTEP, (double)EPS2);
            fprintf(mf, "  \"pos_every\": %d,\n  \"vel_every\": %d,\n  \"snapshots_per_file\": %d,\n",
                    POS_EVERY, VEL_EVERY, SNAP_PER_FILE);
            fprintf(mf, "  \"snapshot\": {\"particles\": %d, \"components\": 3, \"order\": \"x,y,z\", \"layout\": \"particle-major\", \"endian\": \"little\", \"dtype\": \"float64\"},\n", M);
            fprintf(mf, "  \"naming\": \"<tag>_%s_<NNNN>.bin\",\n  \"units\": \"N-body (G=1)\"\n}\n", ts);
            fclose(mf);
        }
    }
    snapw wp = { "pos", "", M, 0, 0, NULL }, wv = { "vel", "", M, 0, 0, NULL };
    strncpy(wp.ts, ts, sizeof(wp.ts) - 1); strncpy(wv.ts, ts, sizeof(wv.ts) - 1);

    mnc2_kernel_t k_init = mnc2_load_kernel(dev, "_build/init.idma.dat");
    mnc2_kernel_t k_kick = mnc2_load_kernel(dev, "_build/kick.idma.dat");
    mnc2_kernel_t k_drift = mnc2_load_kernel(dev, "_build/drift.idma.dat");
    mnc2_kernel_t k_cp   = mnc2_load_kernel(dev, "_build/collect_pos.idma.dat");
    mnc2_kernel_t k_cv   = mnc2_load_kernel(dev, "_build/collect_vel.idma.dat");
    /* accumulate は NCHUNK 個の分割カーネル。ACC_FMT の %d に g を入れて順にロードする。 */
    mnc2_kernel_t k_acc[NCHUNK];
    int acc_ok = 1;
    for (int g = 0; g < NCHUNK; g++) {
        char path[128];
        snprintf(path, sizeof(path), ACC_FMT, g);
        k_acc[g] = mnc2_load_kernel(dev, path);
        if (!k_acc[g]) { fprintf(stderr, "FAIL: load acc 分割 %d (%s)\n", g, path); acc_ok = 0; }
    }
    if (!k_init || !acc_ok || !k_kick || !k_drift || !k_cp || !k_cv) { fprintf(stderr, "FAIL: load\n"); rc = 1; goto done; }

    /* --- init --- */
    {
        double *c = (double *)con_b;
        for (int t = 0; t < BCAST_COUNT; t++) c[t] = 0.0;
        c[0] = EPS2; c[1] = 0.5; c[2] = 1.5; c[3] = DT;
        if (mnc2_send(dev, con_b, OFF_CONST, BCAST_BYTES, 0) != MNC2_SUCCESS) { rc = 1; goto done; }
    }
    {
        double *v = (double *)vel_b;
        for (int i = 0; i < PE_COUNT; i++) { v[i] = hvx[i]; v[PE_COUNT + i] = hvy[i]; v[2 * PE_COUNT + i] = hvz[i]; }
        if (mnc2_send(dev, vel_b, OFF_VEL, VEL_BYTES, 0) != MNC2_SUCCESS) { rc = 1; goto done; }
    }
    {
        double *d = (double *)dist_b;
        for (int i = 0; i < PE_COUNT; i++) { d[i] = hx[i]; d[PE_COUNT + i] = hy[i]; d[2 * PE_COUNT + i] = hz[i]; d[3 * PE_COUNT + i] = hm[i]; }
        if (mnc2_send(dev, dist_b, OFF_DIST, DIST_BYTES, DMAID_TRIGGER) != MNC2_SUCCESS) { rc = 1; goto done; }
    }
    if (exec_q(dev, k_init) != MNC2_SUCCESS) { fprintf(stderr, "FAIL: init\n"); rc = 1; goto done; }

    /* 初期の力 + 初期スナップショット + 初期エネルギー */
    {
        double *bp = (double *)bc_b;
        for (int t = 0; t < PJ_COUNT; t++) bp[t] = 0.0;
        for (int k = 0; k < M; k++) { bp[k * 4] = hx[k]; bp[k * 4 + 1] = hy[k]; bp[k * 4 + 2] = hz[k]; bp[k * 4 + 3] = hm[k]; }
        if (mnc2_send(dev, bc_b, OFF_PJ, PJ_BYTES, DMAID_TRIGGER) != MNC2_SUCCESS) { rc = 1; goto done; }
        for (int g = 0; g < NCHUNK; g++)
            if (exec_q(dev, k_acc[g]) != MNC2_SUCCESS) { fprintf(stderr, "FAIL: acc0 分割 %d\n", g); rc = 1; goto done; }
    }
    snap_write(&wp, hx, hy, hz);
    snap_write(&wv, hvx, hvy, hvz);
    double e0 = total_energy(M, hx, hy, hz, hm, hvx, hvy, hvz), emax = 0;

    /* --- device leapfrog --- */
    for (int step = 0; step < NSTEP; step++) {
        if (exec_q(dev, k_kick) != MNC2_SUCCESS) { rc = 1; goto done; }
        if (exec_q(dev, k_drift) != MNC2_SUCCESS) { rc = 1; goto done; }
        if (exec_q(dev, k_cp) != MNC2_SUCCESS) { rc = 1; goto done; }
        memset(pos_b, 0, 3 * ELEM_BYTES);
        /* collect_pos は 32 分割 (N=4096) の後ろに積まれるので、IDMA を流し切ってから recv する。
           流し切らないと collect の done タグが立つ前に recv の DDMA 待ちが尽きる。 */
        if (mnc2_wait_idma_idle(dev) != MNC2_SUCCESS) { fprintf(stderr, "FAIL: idma idle (pos)\n"); rc = 1; goto done; }
        if (mnc2_recv(dev, pos_b, OFF_POS, 3 * ELEM_BYTES, TAG_POS) != MNC2_SUCCESS) { rc = 1; goto done; }
        double *dp = (double *)pos_b;
        for (int k = 0; k < M; k++) { hx[k] = dp[k]; hy[k] = dp[PE_COUNT + k]; hz[k] = dp[2 * PE_COUNT + k]; }
        double *bp = (double *)bc_b;
        for (int t = 0; t < PJ_COUNT; t++) bp[t] = 0.0;
        for (int k = 0; k < M; k++) { bp[k * 4] = hx[k]; bp[k * 4 + 1] = hy[k]; bp[k * 4 + 2] = hz[k]; bp[k * 4 + 3] = hm[k]; }
        if (mnc2_send(dev, bc_b, OFF_PJ, PJ_BYTES, DMAID_TRIGGER) != MNC2_SUCCESS) { rc = 1; goto done; }
        for (int g = 0; g < NCHUNK; g++)
            if (exec_q(dev, k_acc[g]) != MNC2_SUCCESS) { rc = 1; goto done; }
        if (exec_q(dev, k_kick) != MNC2_SUCCESS) { rc = 1; goto done; }
        if (exec_q(dev, k_cv) != MNC2_SUCCESS) { rc = 1; goto done; }
        memset(vout_b, 0, 3 * ELEM_BYTES);
        if (mnc2_wait_idma_idle(dev) != MNC2_SUCCESS) { fprintf(stderr, "FAIL: idma idle (vel)\n"); rc = 1; goto done; }
        if (mnc2_recv(dev, vout_b, OFF_VOUT, 3 * ELEM_BYTES, TAG_VEL) != MNC2_SUCCESS) { rc = 1; goto done; }
        double *dv = (double *)vout_b;
        for (int k = 0; k < M; k++) { hvx[k] = dv[k]; hvy[k] = dv[PE_COUNT + k]; hvz[k] = dv[2 * PE_COUNT + k]; }
        if ((step + 1) % POS_EVERY == 0) snap_write(&wp, hx, hy, hz);
        if ((step + 1) % VEL_EVERY == 0) snap_write(&wv, hvx, hvy, hvz);
        double e = total_energy(M, hx, hy, hz, hm, hvx, hvy, hvz), rel = fabs(e - e0) / fabs(e0);
        if (rel > emax) emax = rel;
    }
    snap_close(&wp); snap_close(&wv);

    /* --- golden leapfrog --- */
    host_accel(M, gx, gy, gz, hm, ga, gb, gc);
    for (int step = 0; step < NSTEP; step++) {
        for (int i = 0; i < M; i++) { gvx[i] += ga[i] * (DT * 0.5); gvy[i] += gb[i] * (DT * 0.5); gvz[i] += gc[i] * (DT * 0.5); }
        for (int i = 0; i < M; i++) { gx[i] += gvx[i] * DT; gy[i] += gvy[i] * DT; gz[i] += gvz[i] * DT; }
        host_accel(M, gx, gy, gz, hm, ga, gb, gc);
        for (int i = 0; i < M; i++) { gvx[i] += ga[i] * (DT * 0.5); gvy[i] += gb[i] * (DT * 0.5); gvz[i] += gc[i] * (DT * 0.5); }
    }

    /* --- 照合 --- */
    {
        int errors = 0; double maxrel = 0;
        for (int i = 0; i < M; i++) {
            double val[6] = { hx[i], hy[i], hz[i], hvx[i], hvy[i], hvz[i] };
            double exp[6] = { gx[i], gy[i], gz[i], gvx[i], gvy[i], gvz[i] };
            for (int t = 0; t < 6; t++) {
                double d = fabs(val[t] - exp[t]) / (fabs(exp[t]) > ABSFLOOR ? fabs(exp[t]) : ABSFLOOR);
                if (d > maxrel) maxrel = d;
                if (d > TOL) { if (errors < 5) fprintf(stderr, "  MISMATCH[%d].%d got=%g exp=%g\n", i, t, val[t], exp[t]); errors++; }
            }
        }
        double rmax = max_radius(M, hx, hy, hz, hm);   /* 最終の系の最大半径 */
        if (errors > 0) { fprintf(stderr, "  FAIL: 値 %d 成分ずれ (max_rel=%.3e tol=%.1e)\n", errors, maxrel, TOL); rc = 1; }
        else if (emax > ETOL) { fprintf(stderr, "  FAIL: エネルギーのずれ %.3e > %.1e\n", emax, ETOL); rc = 1; }
        else if (rmax > BOUND_FACTOR * rmax0) { fprintf(stderr, "  FAIL: 系が発散 (最大半径 %.3g -> %.3g)\n", rmax0, rmax); rc = 1; }
        else {
            printf("  PASS: 値 max_rel=%.3e、エネルギーのずれ %.3e、最大半径 %.3g->%.3g、N=%d %d ステップ。%s/ に出力\n",
                   maxrel, emax, rmax0, rmax, M, NSTEP, RESULT_DIR);
            rc = 0;
        }
    }

done:
    free(hx); free(hy); free(hz); free(hm); free(hvx); free(hvy); free(hvz);
    free(gx); free(gy); free(gz); free(gvx); free(gvy); free(gvz); free(ga); free(gb); free(gc);
    if (dist_b) mnc2_free_host_buffer(dev, dist_b, DIST_BYTES);
    if (vel_b)  mnc2_free_host_buffer(dev, vel_b, VEL_BYTES);
    if (con_b)  mnc2_free_host_buffer(dev, con_b, BCAST_BYTES);
    if (bc_b)   mnc2_free_host_buffer(dev, bc_b, PJ_BYTES);
    if (pos_b)  mnc2_free_host_buffer(dev, pos_b, 3 * ELEM_BYTES);
    if (vout_b) mnc2_free_host_buffer(dev, vout_b, 3 * ELEM_BYTES);
    mnc2_close(dev);
    return rc ? 1 : 0;
}
