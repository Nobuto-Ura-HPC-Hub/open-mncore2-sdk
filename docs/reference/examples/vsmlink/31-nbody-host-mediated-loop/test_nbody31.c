/* test_nbody31.c — kernel 分割版の 3D N 体 leapfrog + 入出力ファイル。
 *
 * examples/28-nbody-3d の test_nbody3d.c を基礎に、accumulate を「CHUNK 粒子ずつ固定 PDM
 * 領域を読む 1 個の kernel」にし、host が「次の区間を送信 -> 同じ kernel を exec」を
 * NCHUNK 回繰り返す形に書き換えた。28 は分割ごとに別々の kernel をロードして順に exec
 * していたが (kernel 数が N に依存)、31 は kernel が 6 個で N に依存しない。
 *
 * 速度 (vx,vy,vz) は device 上で kick が更新し続けるが、host へは回収しない。したがって
 * 値照合は位置 (x,y,z) の 3 成分のみ、物理検証は束縛 (最大半径) のみ行う。エネルギー保存の
 * 検証は速度が無いため今回は行わない。
 *
 * 入力: struct 形バイナリ (1 粒子 8 double: x,y,z,m,vx,vy,vz,pad)。argv[1] か既定
 *       _build/ic.bin を mmap して読む。
 * 出力: _result/<tag>_<ts>_<NNNN>.bin (tag=pos のみ) と run のマニフェスト JSON。
 *
 * N / newton / NSTEP / CHUNK は build.ninja から -DUNROLL / -DNEWTON / -DNSTEP / -DCHUNK で渡る。
 * CHUNK は gen_nbody31.py に渡す値と一致させること (build.ninja の $chunk で共有)。
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
#ifndef CHUNK
#define CHUNK 16
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
/* accumulate は CHUNK 粒子ぶんの固定 PDM 領域を読む 1 個の kernel。CHUNK*4 u64 は
   @broadcast の PDM 区画単位 (64 u64) と一致させる (CHUNK=16 のとき ちょうど 64)。 */
#define PJ_COUNT    (CHUNK * 4)
#define PJ_BYTES    (PJ_COUNT * sizeof(double))
#define NCHUNK(n)   (((n) + CHUNK - 1) / CHUNK)

#define OFF_DIST 0
#define OFF_CONST (16384ULL * 8)
#define OFF_PJ   (20480ULL * 8)
#define OFF_VEL  (40960ULL * 8)
#define OFF_POS  (131072ULL * 8)
#define DMAID_TRIGGER 0x10
#define TAG_POS 0x1e
#define EPS2 0.01
#define DT   0.015625
#define BOUND_FACTOR 5.0                     /* 系の最大半径が初期の何倍まで許すか (束縛の確認) */
#if NEWTON >= 3
#define TOL 1e-6
#else
#define TOL 0.2
#endif
#define ABSFLOOR 0.02

#define POS_EVERY 1                          /* 位置スナップショットの間隔 (ステップ) */
#define SNAP_PER_FILE 128                    /* 1 ファイルにまとめるスナップショット数 */
#ifndef IC_DEFAULT
#define IC_DEFAULT "_build/ic.bin"
#endif
#ifndef RESULT_DIR
#define RESULT_DIR "_result"
#endif

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

/* 力の評価 1 回ぶん。reset_acc を 1 回 exec し、CHUNK 粒子ずつ NCHUNK 回「送信 -> exec」を
   繰り返す。同じ PDM 領域 (OFF_PJ) を書き換えながら回すので、送信の前に必ず前回の exec が
   読み終わるのを待つ (mnc2_wait_idma_idle)。M が CHUNK の倍数でない場合、末尾の区間は
   質量 0 の粒子で埋める (力への寄与が 0 になるので、実在しない粒子として無害)。 */
static int eval_force(mnc2_device_t dev, mnc2_kernel_t k_reset_acc, mnc2_kernel_t k_accumulate,
                      void *bc_b, const double *hx, const double *hy, const double *hz, const double *hm, int m) {
    if (mnc2_exec_kernel(k_reset_acc) != MNC2_SUCCESS) return 1;
    int nchunk = NCHUNK(m);
    for (int g = 0; g < nchunk; g++) {
        if (mnc2_wait_idma_idle(dev) != MNC2_SUCCESS) return 1;
        double *bp = (double *)bc_b;
        for (int j = 0; j < CHUNK; j++) {
            int idx = g * CHUNK + j;
            if (idx < m) { bp[j * 4] = hx[idx]; bp[j * 4 + 1] = hy[idx]; bp[j * 4 + 2] = hz[idx]; bp[j * 4 + 3] = hm[idx]; }
            else { bp[j * 4] = 0.0; bp[j * 4 + 1] = 0.0; bp[j * 4 + 2] = 0.0; bp[j * 4 + 3] = 0.0; }
        }
        if (mnc2_send(dev, bc_b, OFF_PJ, PJ_BYTES, DMAID_TRIGGER) != MNC2_SUCCESS) return 1;
        if (mnc2_exec_kernel(k_accumulate) != MNC2_SUCCESS) return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    const char *ic_path = (argc > 1) ? argv[1] : IC_DEFAULT;
    printf("=== 3D N 体 leapfrog + kernel 分割 (N=%d, newton=%d, CHUNK=%d, %d ステップ, 入力 %s) ===\n\n",
           M, NEWTON, CHUNK, NSTEP, ic_path);
    mnc2_device_t dev = mnc2_open(0);
    if (!dev) { fprintf(stderr, "FAIL: open\n"); return 1; }
    /* CHUNK 粒子ずつの送信のたびに idle 待ちをするため、待ち回数が 28 より大幅に多い。
       待ち上限 (default 5000) が足りない場合に備えて上げる。 */
    mnc2_set_idma_complete_max(dev, 1000000);

    void *dist_b = mnc2_alloc_host_buffer(dev, DIST_BYTES), *vel_b = mnc2_alloc_host_buffer(dev, VEL_BYTES);
    void *con_b = mnc2_alloc_host_buffer(dev, BCAST_BYTES), *bc_b = mnc2_alloc_host_buffer(dev, PJ_BYTES);
    void *pos_b = mnc2_alloc_host_buffer(dev, 3 * ELEM_BYTES);
    double *hx = malloc(ELEM_BYTES), *hy = malloc(ELEM_BYTES), *hz = malloc(ELEM_BYTES), *hm = malloc(ELEM_BYTES);
    double *hvx = malloc(ELEM_BYTES), *hvy = malloc(ELEM_BYTES), *hvz = malloc(ELEM_BYTES);
    double *gx = malloc(ELEM_BYTES), *gy = malloc(ELEM_BYTES), *gz = malloc(ELEM_BYTES);
    double *gvx = malloc(ELEM_BYTES), *gvy = malloc(ELEM_BYTES), *gvz = malloc(ELEM_BYTES);
    double *ga = malloc(ELEM_BYTES), *gb = malloc(ELEM_BYTES), *gc = malloc(ELEM_BYTES);
    if (!dist_b || !vel_b || !con_b || !bc_b || !pos_b || !hx || !hy || !hz || !hm ||
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
        /* 系に属さない PE (N..4095) は粒子 0 で埋める (distribute の対象。計算は無視するが NaN を避ける) */
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
            fprintf(mf, "  \"pos_every\": %d,\n  \"snapshots_per_file\": %d,\n", POS_EVERY, SNAP_PER_FILE);
            fprintf(mf, "  \"snapshot\": {\"particles\": %d, \"components\": 3, \"order\": \"x,y,z\", \"layout\": \"particle-major\", \"endian\": \"little\", \"dtype\": \"float64\"},\n", M);
            fprintf(mf, "  \"naming\": \"pos_%s_<NNNN>.bin\",\n  \"units\": \"N-body (G=1)\",\n", ts);
            fprintf(mf, "  \"note\": \"速度は device から回収しないため出力に含まない\"\n}\n");
            fclose(mf);
        }
    }
    snapw wp = { "pos", "", M, 0, 0, NULL };
    strncpy(wp.ts, ts, sizeof(wp.ts) - 1);

    mnc2_kernel_t k_init       = mnc2_load_kernel(dev, "_build/k/nbody31-init.idma.dat");
    mnc2_kernel_t k_reset_acc  = mnc2_load_kernel(dev, "_build/k/nbody31-reset_acc.idma.dat");
    mnc2_kernel_t k_accumulate = mnc2_load_kernel(dev, "_build/k/nbody31-accumulate.idma.dat");
    mnc2_kernel_t k_kick       = mnc2_load_kernel(dev, "_build/k/nbody31-kick.idma.dat");
    mnc2_kernel_t k_drift      = mnc2_load_kernel(dev, "_build/k/nbody31-drift.idma.dat");
    mnc2_kernel_t k_cp         = mnc2_load_kernel(dev, "_build/k/nbody31-collect_pos.idma.dat");
    if (!k_init || !k_reset_acc || !k_accumulate || !k_kick || !k_drift || !k_cp) { fprintf(stderr, "FAIL: load\n"); rc = 1; goto done; }

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
    if (mnc2_exec_kernel(k_init) != MNC2_SUCCESS) { fprintf(stderr, "FAIL: init\n"); rc = 1; goto done; }

    /* 初期の力 + 初期スナップショット */
    if (eval_force(dev, k_reset_acc, k_accumulate, bc_b, hx, hy, hz, hm, M) != 0) { fprintf(stderr, "FAIL: force0\n"); rc = 1; goto done; }
    snap_write(&wp, hx, hy, hz);

    /* --- device leapfrog --- */
    for (int step = 0; step < NSTEP; step++) {
        if (mnc2_exec_kernel(k_kick) != MNC2_SUCCESS) { rc = 1; goto done; }
        if (mnc2_exec_kernel(k_drift) != MNC2_SUCCESS) { rc = 1; goto done; }
        if (mnc2_exec_kernel(k_cp) != MNC2_SUCCESS) { rc = 1; goto done; }
        memset(pos_b, 0, 3 * ELEM_BYTES);
        if (mnc2_wait_idma_idle(dev) != MNC2_SUCCESS) { fprintf(stderr, "FAIL: idma idle (pos)\n"); rc = 1; goto done; }
        if (mnc2_recv(dev, pos_b, OFF_POS, 3 * ELEM_BYTES, TAG_POS) != MNC2_SUCCESS) { rc = 1; goto done; }
        double *dp = (double *)pos_b;
        for (int k = 0; k < M; k++) { hx[k] = dp[k]; hy[k] = dp[PE_COUNT + k]; hz[k] = dp[2 * PE_COUNT + k]; }
        if (eval_force(dev, k_reset_acc, k_accumulate, bc_b, hx, hy, hz, hm, M) != 0) { rc = 1; goto done; }
        if (mnc2_exec_kernel(k_kick) != MNC2_SUCCESS) { rc = 1; goto done; }
        if ((step + 1) % POS_EVERY == 0) snap_write(&wp, hx, hy, hz);
    }
    snap_close(&wp);

    /* --- golden leapfrog (host 倍精度、速度は内部でのみ保持) --- */
    host_accel(M, gx, gy, gz, hm, ga, gb, gc);
    for (int step = 0; step < NSTEP; step++) {
        for (int i = 0; i < M; i++) { gvx[i] += ga[i] * (DT * 0.5); gvy[i] += gb[i] * (DT * 0.5); gvz[i] += gc[i] * (DT * 0.5); }
        for (int i = 0; i < M; i++) { gx[i] += gvx[i] * DT; gy[i] += gvy[i] * DT; gz[i] += gvz[i] * DT; }
        host_accel(M, gx, gy, gz, hm, ga, gb, gc);
        for (int i = 0; i < M; i++) { gvx[i] += ga[i] * (DT * 0.5); gvy[i] += gb[i] * (DT * 0.5); gvz[i] += gc[i] * (DT * 0.5); }
    }

    /* --- 照合 (位置 3 成分のみ) --- */
    {
        int errors = 0; double maxrel = 0;
        for (int i = 0; i < M; i++) {
            double val[3] = { hx[i], hy[i], hz[i] };
            double exp[3] = { gx[i], gy[i], gz[i] };
            for (int t = 0; t < 3; t++) {
                double d = fabs(val[t] - exp[t]) / (fabs(exp[t]) > ABSFLOOR ? fabs(exp[t]) : ABSFLOOR);
                if (d > maxrel) maxrel = d;
                if (d > TOL) { if (errors < 5) fprintf(stderr, "  MISMATCH[%d].%d got=%g exp=%g\n", i, t, val[t], exp[t]); errors++; }
            }
        }
        double rmax = max_radius(M, hx, hy, hz, hm);   /* 最終の系の最大半径 */
        if (errors > 0) { fprintf(stderr, "  FAIL: 値 %d 成分ずれ (max_rel=%.3e tol=%.1e)\n", errors, maxrel, TOL); rc = 1; }
        else if (rmax > BOUND_FACTOR * rmax0) { fprintf(stderr, "  FAIL: 系が発散 (最大半径 %.3g -> %.3g)\n", rmax0, rmax); rc = 1; }
        else {
            printf("  PASS: 値 max_rel=%.3e、最大半径 %.3g->%.3g、N=%d %d ステップ。%s/ に出力\n",
                   maxrel, rmax0, rmax, M, NSTEP, RESULT_DIR);
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
    mnc2_close(dev);
    return rc ? 1 : 0;
}
