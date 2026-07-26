/* test_nbody3d.c -- 3D 重力多体（f64）。size 3 / v4f64 / broadcast size 3 と時間積分を検証する。
 *
 * semi-implicit Euler（Euler-Cromer）で粒子を T ステップ進める。各ステップは 2 フェーズ:
 *   力フェーズ  : 各 PE が担当粒子の加速度 a_i = sum_k m_k (r_k-r_i)/|r|^3 を _acc に積む
 *                 （host が粒子 k を 1 個ずつ broadcast して力カーネルを N 回起動）
 *   積分フェーズ: v <- v + dt*a、x <- x + dt*v（更新後 v で位置更新）。積分カーネルを 1 回起動
 *
 * **状態 3 つ（pos / vel / acc）を PDM 常駐で持ち回る。** カーネルをまたいで PE の状態は保てないので、
 * 位置・速度・加速度を PDM に置き、力カーネルは acc を in-place 積算、積分カーネルは pos/vel を
 * in-place 更新する（17-nbody-2d / 14-odd-even-sort-full の常駐パターン）。
 *
 * **更新後の位置を次ステップの broadcast 元へ書き戻す（伝播）。** 積分後に host が pos を recv し、
 * 次ステップの力フェーズで各粒子 k の位置として broadcast する。これで全 PE が最新の位置を見る。
 *
 * **積分カーネルは triggerless（起動トリガの送信が不要）。** 入力はすべて PDM 常駐で、host から
 * 新しく送るデータが無い。積分カーネルの .param に send_wait_tag を付けないと先頭の起動ゲート wait が
 * 出ず、mnc2_exec_kernel 単独で走る（ISS-213。10-odd-even-sort の reduce カーネルと同型）。dt は init で
 * 1 回だけ送って常駐させ、毎ステップ常駐値として読む。順序は力フェーズの recv acc（force 完了保証）で担保。
 *
 * **PDM 上の並びは「成分が外側、PE が内側」である。**
 *   pos / vel / acc: [成分 0 を 4096 PE 分][成分 1][成分 2]
 * l1bmd が addr_b + cycle*64 でアクセスするため HW で決まっており選べない。並べるのはホストの責務。
 *
 * **N と STEPS は環境変数で受ける（default N=128 / STEPS=2）。**
 *   ninja test-emu-lib     -> N=128 / STEPS=2（CI。伝播を必ず踏む。各ステップ golden 照合）
 *   ninja test-emu-full    -> N=4096 の full（手動、重い）
 *   ninja test-device      -> 実機 N=128        ninja test-device-full -> 実機 N=4096
 * 4096 PE のうち PE 0..N-1 が実粒子、残りは質量 0 で無害化。N=128 でも subpeid 0..3 の全 4 種を踏み、
 * 全 PE を golden と照合するので「PE の一部だけ壊れる不具合」を検出できる。
 *
 * **検証は転送成功だけでなく計算結果。** host が同じ semi-implicit Euler を O(N^2) で 1 ステップずつ
 * 回し、各ステップ後の pos / vel をデバイスと相対誤差で照合する。エネルギー E も print する（診断。
 * semi-implicit Euler は概ね symplectic で E は有界振動、secular drift が無いはず）。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "mnc2.h"

#define N_PE       4096
#define ELEM_BYTES (N_PE * sizeof(double))
#define VEC3_BYTES (3 * N_PE * sizeof(double))   /* double3 は 3 成分ぶん */
#define BC_LW      64
#define BC_BYTES   (BC_LW * sizeof(double))
#define EPS2       0.0625      /* 2^-4 softening^2 */
#define DT         0.015625    /* 2^-6 タイムステップ（2 冪。golden と bit 一致） */

#define OFF_POS   (0ULL * 8)       /* [x 4096][y 4096][z 4096] */
#define OFF_VEL   (16384ULL * 8)   /* [vx 4096][vy 4096][vz 4096] */
#define OFF_ACC   (32768ULL * 8)   /* [ax 4096][ay 4096][az 4096] */
#define OFF_BPOS  (49152ULL * 8)   /* 粒子 k の位置 (x, y, z)。先頭 3 u64 */
#define OFF_BMASS (49216ULL * 8)   /* 粒子 k の質量。先頭 1 u64 */
#define OFF_DT    (49280ULL * 8)   /* dt。先頭 1 u64 */
#define SEND_TAG  0x10
#define RECV_ACC  0x1e
#define RECV_POS  0x1e
#define RECV_VEL  0x1d

/* component-outer で host 配列（x,y,z 別）を PDM staging バッファに詰める */
static void pack_vec3(double *dst, const double *cx, const double *cy, const double *cz)
{
    for (int i = 0; i < N_PE; i++) dst[i]           = cx[i];
    for (int i = 0; i < N_PE; i++) dst[N_PE + i]     = cy[i];
    for (int i = 0; i < N_PE; i++) dst[2*N_PE + i]   = cz[i];
}
/* component-outer の PDM staging バッファを host 配列（x,y,z 別）に展開する */
static void unpack_vec3(const double *src, double *cx, double *cy, double *cz)
{
    for (int i = 0; i < N_PE; i++) cx[i] = src[i];
    for (int i = 0; i < N_PE; i++) cy[i] = src[N_PE + i];
    for (int i = 0; i < N_PE; i++) cz[i] = src[2*N_PE + i];
}

/* 全エネルギー E = 運動 + ポテンシャル（softening 込み、G=1）。N 粒子ぶん */
static double total_energy(int N, const double *x, const double *y, const double *z,
                           const double *vx, const double *vy, const double *vz, const double *m)
{
    double ke = 0.0, pe = 0.0;
    for (int i = 0; i < N; i++)
        ke += 0.5 * m[i] * (vx[i]*vx[i] + vy[i]*vy[i] + vz[i]*vz[i]);
    for (int i = 0; i < N; i++)
        for (int k = i+1; k < N; k++) {
            double dx = x[k]-x[i], dy = y[k]-y[i], dz = z[k]-z[i];
            pe -= m[i]*m[k] / sqrt(dx*dx + dy*dy + dz*dz + EPS2);
        }
    return ke + pe;
}

/* golden を 1 ステップ進める（デバイスと同じ semi-implicit Euler）。位置・速度を上書き更新 */
static void golden_step(int N, double *x, double *y, double *z,
                        double *vx, double *vy, double *vz, const double *m)
{
    double *ax = (double *)malloc(N*sizeof(double));
    double *ay = (double *)malloc(N*sizeof(double));
    double *az = (double *)malloc(N*sizeof(double));
    for (int i = 0; i < N; i++) {
        double gx = 0.0, gy = 0.0, gz = 0.0;
        for (int k = 0; k < N; k++) {
            double dx = x[k]-x[i], dy = y[k]-y[i], dz = z[k]-z[i];
            double r2 = dx*dx + dy*dy + dz*dz + EPS2;
            double ir = 1.0/sqrt(r2), ir3 = ir*ir*ir;
            gx += m[k]*dx*ir3; gy += m[k]*dy*ir3; gz += m[k]*dz*ir3;
        }
        ax[i] = gx; ay[i] = gy; az[i] = gz;
    }
    for (int i = 0; i < N; i++) {            /* v <- v + dt*a（速度を先に） */
        vx[i] += DT*ax[i]; vy[i] += DT*ay[i]; vz[i] += DT*az[i];
    }
    for (int i = 0; i < N; i++) {            /* x <- x + dt*v（更新後 v で位置） */
        x[i] += DT*vx[i]; y[i] += DT*vy[i]; z[i] += DT*vz[i];
    }
    free(ax); free(ay); free(az);
}

int main(int argc, char **argv)
{
    int N = 128, STEPS = 2, silent = 0;
    const char *e;
    if ((e = getenv("NBODY_N")))     { int v = atoi(e); if (v>=1 && v<=N_PE) N = v; }
    if ((e = getenv("NBODY_STEPS"))) { int v = atoi(e); if (v>=1)            STEPS = v; }
    for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "--silent")) silent = 1;

    /* 診断出力は stderr に出す（--silent で抑制）。PASS / FAIL 判定は golden 照合で行い、
     * この出力に依存させない。--silent でも終了コードと最後の 1 行の判定は分かる。 */
    if (!silent)
        fprintf(stderr, "=== 19-nbody-3d: N=%d 粒子を semi-implicit Euler で %d ステップ (double3, 4096 PE) ===\n\n",
                N, STEPS);

    mnc2_device_t dev = mnc2_open(0);
    if (!dev) { fprintf(stderr, "FAIL: mnc2_open\n"); return 1; }
    if (!silent) fprintf(stderr, "19-nbody-3d backend: %s\n", mnc2_get_backend_name(dev));

    /* PDM staging バッファ（host 側） */
    void *posbuf   = mnc2_alloc_host_buffer(dev, VEC3_BYTES);
    void *velbuf   = mnc2_alloc_host_buffer(dev, VEC3_BYTES);
    void *zbuf     = mnc2_alloc_host_buffer(dev, VEC3_BYTES);   /* acc 0 埋め用 */
    void *bposbuf  = mnc2_alloc_host_buffer(dev, BC_BYTES);
    void *bmassbuf = mnc2_alloc_host_buffer(dev, BC_BYTES);
    void *dtbuf    = mnc2_alloc_host_buffer(dev, BC_BYTES);
    void *accdrain = mnc2_alloc_host_buffer(dev, VEC3_BYTES);   /* 力フェーズの同期用（値は捨てる） */
    void *rpos     = mnc2_alloc_host_buffer(dev, VEC3_BYTES);
    void *rvel     = mnc2_alloc_host_buffer(dev, VEC3_BYTES);
    /* host 配列（成分別）。x/y/z はデバイスの最新位置（broadcast 元）、g* は golden */
    double *x=malloc(ELEM_BYTES), *y=malloc(ELEM_BYTES), *z=malloc(ELEM_BYTES);
    double *vx=malloc(ELEM_BYTES), *vy=malloc(ELEM_BYTES), *vz=malloc(ELEM_BYTES);
    double *m=malloc(ELEM_BYTES);
    double *gx=malloc(ELEM_BYTES), *gy=malloc(ELEM_BYTES), *gz=malloc(ELEM_BYTES);
    double *gvx=malloc(ELEM_BYTES), *gvy=malloc(ELEM_BYTES), *gvz=malloc(ELEM_BYTES);
    mnc2_kernel_t k_force = NULL, k_integ = NULL;
    int rc = 0;

    if (!posbuf||!velbuf||!zbuf||!bposbuf||!bmassbuf||!dtbuf||!accdrain||!rpos||!rvel||
        !x||!y||!z||!vx||!vy||!vz||!m||!gx||!gy||!gz||!gvx||!gvy||!gvz) {
        fprintf(stderr, "FAIL: alloc\n"); rc = 1; goto cleanup;
    }

    /* 初期条件: 位置は 3 次元に広めに配置、速度 0、質量。使わない PE は質量 0 で無害化 */
    for (int i = 0; i < N_PE; i++) {
        if (i < N) {
            x[i] = (double)i * 2.0 - 128.0;
            y[i] = (double)(i % 8) * 3.0 - 12.0;
            z[i] = (double)(i % 5) * 4.0 - 8.0;
            m[i] = 1.0 + (double)(i % 4);
        } else { x[i]=y[i]=z[i]=0.0; m[i]=0.0; }
        vx[i]=vy[i]=vz[i]=0.0;
    }
    /* golden を初期状態にそろえる */
    memcpy(gx,x,ELEM_BYTES); memcpy(gy,y,ELEM_BYTES); memcpy(gz,z,ELEM_BYTES);
    memcpy(gvx,vx,ELEM_BYTES); memcpy(gvy,vy,ELEM_BYTES); memcpy(gvz,vz,ELEM_BYTES);

    /* pos / vel を PDM 常駐に置く（初回のみ、tag 0）。以降はデバイスが更新し host が recv で追う */
    pack_vec3((double *)posbuf, x, y, z);
    pack_vec3((double *)velbuf, vx, vy, vz);
    if (mnc2_send(dev, posbuf, OFF_POS, VEC3_BYTES, 0) != MNC2_SUCCESS) { fprintf(stderr,"FAIL: send pos\n"); rc=1; goto cleanup; }
    if (mnc2_send(dev, velbuf, OFF_VEL, VEC3_BYTES, 0) != MNC2_SUCCESS) { fprintf(stderr,"FAIL: send vel\n"); rc=1; goto cleanup; }

    /* dt を PDM 常駐に置く（初回のみ、tag 0）。積分カーネルは triggerless なので、dt は起動トリガでは
     * なく、毎ステップ同じ値を常駐値として読むだけ。 */
    { double *p=(double*)dtbuf; for (int i=0;i<BC_LW;i++) p[i]=DT; }
    if (mnc2_send(dev, dtbuf, OFF_DT, BC_BYTES, 0) != MNC2_SUCCESS) { fprintf(stderr,"FAIL: send dt (init)\n"); rc=1; goto cleanup; }

    k_force = mnc2_load_kernel(dev, "_build/nbody3d_force.idma.dat");
    k_integ = mnc2_load_kernel(dev, "_build/nbody3d_integ.idma.dat");
    if (!k_force || !k_integ) { fprintf(stderr, "FAIL: load_kernel\n"); rc=1; goto cleanup; }

    double e0 = total_energy(N, x,y,z, vx,vy,vz, m);
    if (!silent) fprintf(stderr, "  step 0: E=%.6e (初期)\n", e0);

    double max_rel = 0.0;
    for (int step = 0; step < STEPS; step++) {
        /* 1. acc を 0 埋め（tag 0 の非トリガ送信） */
        memset(zbuf, 0, VEC3_BYTES);
        if (mnc2_send(dev, zbuf, OFF_ACC, VEC3_BYTES, 0) != MNC2_SUCCESS) { fprintf(stderr,"FAIL: zero acc s=%d\n",step); rc=1; goto cleanup; }

        /* 2. 力フェーズ: 粒子 k を broadcast して力カーネルを N 回起動、acc に積む */
        for (int k = 0; k < N; k++) {
            { double *p=(double*)bposbuf; for (int i=0;i<BC_LW;i++) p[i]=0.0; p[0]=x[k]; p[1]=y[k]; p[2]=z[k]; }
            { double *p=(double*)bmassbuf; for (int i=0;i<BC_LW;i++) p[i]=m[k]; }
            if (mnc2_send(dev, bposbuf, OFF_BPOS, BC_BYTES, 0) != MNC2_SUCCESS)          { fprintf(stderr,"FAIL: send bpos s=%d k=%d\n",step,k); rc=1; goto cleanup; }
            if (mnc2_send(dev, bmassbuf, OFF_BMASS, BC_BYTES, SEND_TAG) != MNC2_SUCCESS) { fprintf(stderr,"FAIL: send bmass s=%d k=%d\n",step,k); rc=1; goto cleanup; }
            if (mnc2_exec_kernel(k_force) != MNC2_SUCCESS)                          { fprintf(stderr,"FAIL: exec force s=%d k=%d\n",step,k); rc=1; goto cleanup; }
            if (mnc2_recv(dev, accdrain, OFF_ACC, VEC3_BYTES, RECV_ACC) != MNC2_SUCCESS) { fprintf(stderr,"FAIL: drain acc s=%d k=%d\n",step,k); rc=1; goto cleanup; }
        }

        /* 3. 積分フェーズ: triggerless。dt 送信は不要（init で常駐済み）。exec で起動し、順序は
         *    直前の力フェーズの recv acc（force 完了を保証）で担保される。 */
        if (mnc2_exec_kernel(k_integ) != MNC2_SUCCESS)                    { fprintf(stderr,"FAIL: exec integ s=%d\n",step); rc=1; goto cleanup; }
        if (mnc2_recv(dev, rpos, OFF_POS, VEC3_BYTES, RECV_POS) != MNC2_SUCCESS) { fprintf(stderr,"FAIL: recv pos s=%d\n",step); rc=1; goto cleanup; }
        if (mnc2_recv(dev, rvel, OFF_VEL, VEC3_BYTES, RECV_VEL) != MNC2_SUCCESS) { fprintf(stderr,"FAIL: recv vel s=%d\n",step); rc=1; goto cleanup; }

        /* 4. 伝播: デバイスの更新後 pos / vel を host 配列へ（次ステップの broadcast 元） */
        unpack_vec3((double *)rpos, x, y, z);
        unpack_vec3((double *)rvel, vx, vy, vz);

        /* 5. golden を 1 ステップ進めて、全 N 粒子の pos / vel を照合 */
        golden_step(N, gx,gy,gz, gvx,gvy,gvz, m);
        int errors = 0;
        for (int i = 0; i < N; i++) {
            double dpos[3] = { x[i],  y[i],  z[i]  }, gpos[3] = { gx[i],  gy[i],  gz[i]  };
            double dvel[3] = { vx[i], vy[i], vz[i] }, gvel[3] = { gvx[i], gvy[i], gvz[i] };
            for (int c = 0; c < 3; c++) {
                double dp = fabs(dpos[c]-gpos[c]), gp = fabs(gpos[c]);
                double dv = fabs(dvel[c]-gvel[c]), gv = fabs(gvel[c]);
                double relp = gp>0 ? dp/gp : 0.0, relv = gv>0 ? dv/gv : 0.0;
                if (relp>max_rel) max_rel = relp;
                if (relv>max_rel) max_rel = relv;
                if (dp > 1e-6*gp+1e-9 || dv > 1e-6*gv+1e-9) {
                    if (errors<5) fprintf(stderr,"  MISMATCH s=%d i=%d c=%d: pos got=%.9g exp=%.9g / vel got=%.9g exp=%.9g\n",
                                          step, i, c, dpos[c], gpos[c], dvel[c], gvel[c]);
                    errors++;
                }
            }
        }
        double en = total_energy(N, x,y,z, vx,vy,vz, m);
        if (!silent) fprintf(stderr, "  step %d: E=%.6e (drift=%.2e), 照合 %s\n",
                             step+1, en, fabs((en-e0)/e0), errors ? "NG" : "OK");
        if (errors) { fprintf(stderr, "  FAIL: step %d で %d 個の不一致\n", step+1, errors); rc = 1; goto verdict; }
    }

verdict:
    if (!silent) fprintf(stderr, "  max relative error = %.3e\n", max_rel);
    /* 判定は golden 照合の rc で決まる。--silent でも下の 1 行だけは出す */
    if (rc == 0) printf("  PASS: pos / vel が %d ステップの O(N^2) golden に一致（N=%d, semi-implicit Euler）\n", STEPS, N);
    else         printf("  FAIL: golden と不一致（N=%d, STEPS=%d）\n", N, STEPS);

cleanup:
    if (k_force) mnc2_free_kernel(k_force);
    if (k_integ) mnc2_free_kernel(k_integ);
    if (posbuf)   mnc2_free_host_buffer(dev, posbuf, VEC3_BYTES);
    if (velbuf)   mnc2_free_host_buffer(dev, velbuf, VEC3_BYTES);
    if (zbuf)     mnc2_free_host_buffer(dev, zbuf, VEC3_BYTES);
    if (bposbuf)  mnc2_free_host_buffer(dev, bposbuf, BC_BYTES);
    if (bmassbuf) mnc2_free_host_buffer(dev, bmassbuf, BC_BYTES);
    if (dtbuf)    mnc2_free_host_buffer(dev, dtbuf, BC_BYTES);
    if (accdrain) mnc2_free_host_buffer(dev, accdrain, VEC3_BYTES);
    if (rpos)     mnc2_free_host_buffer(dev, rpos, VEC3_BYTES);
    if (rvel)     mnc2_free_host_buffer(dev, rvel, VEC3_BYTES);
    mnc2_close(dev);
    free(x);free(y);free(z);free(vx);free(vy);free(vz);free(m);
    free(gx);free(gy);free(gz);free(gvx);free(gvy);free(gvz);
    return (rc != 0) ? 1 : 0;
}
