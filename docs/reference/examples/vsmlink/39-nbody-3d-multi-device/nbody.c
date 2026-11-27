/* nbody.c — 3D N 体シミュレーション (MN-Core2 を複数枚使う実機向けアプリ)

 * examples/28-nbody-3d の host を土台に、次の 2 点を変えたもの。
 *
 *   1. 他粒子を、各デバイスの DRAM から @broadcast で受ける (accumulate の slot が :place :group-dram)。
 *      ホストは DRAM へ直接送れないので、PDM0 へ送り、stage kernel が全グループの DRAM へ置く
 *   2. 粒子を、デバイス 1 枚あたり B 個 (<= 4096) に分けて、複数のデバイスに割り当てる。
 *      各タイムステップで、ホストが全デバイスの結果を集めて全粒子の並びにし、全デバイスへ送り直す
 *
 * 1 枚あたり B = N / (デバイス数)。デバイス d は粒子 d*B 〜 (d+1)*B-1 を担当する (1 PE が 1 粒子)。
 * デバイス同士は直接つながらない。デバイスの間のデータの移動は、ホストだけが行う。
 *
 * 使い方:
 *   nbody [<ic.bin>] [--devs=<n>,<n>] [--every=<n>] [--verify=<n>] [--monitor=<n>] [--vel] [--show-flags]
 *
 *   --devs=<n>,<n> 使うデバイスの番号 (mnc2_open に渡す)。既定は build.ninja の -DDEVS_DEFAULT
 *   --every=<n>    n タイムステップごとに位置を記録する (既定 TIMESTEP_EVERY)。
 *                  1 コマ = 粒子数 x 3 成分 x 8 byte (8192 粒子で 196 KB)
 *   --verify=<n>   先頭 n タイムステップだけ host 倍精度と位置を照合する (既定 0)。
 *                  重力多体系は軌道が指数的に発散するので、長く回すと device と host は必ず離れる。
 *                  これは実装の誤りではなく系の性質なので、先頭の数タイムステップに限って使う
 *   --monitor=<n>  n タイムステップごとにエネルギー・運動量・重心・最大半径を
 *                  run.json に記録する (既定 TIMESTEP_EVERY、0 で無効)。長く回したときの
 *                  妥当性の根拠はこちらになる (leapfrog はシンプレクティック積分)
 *   --out=<dir>    結果を出すディレクトリ (既定は build.ninja の -DRESULT_DIR)
 *   --vel          速度のスナップショット (vel_<NNNN>.bin) も出す。デバイスの分け方を変えた
 *                  2 つの実行を、バイト単位で比べるときに使う (compare_runs.sh)
 *
 * 入力形式: struct 形バイナリ (1 粒子 8 double: x,y,z,m,vx,vy,vz,pad)。
 *           gen_ic.py と gen_ic_plummer.py の出力をそのまま使える。
 *
 * N / newton / NTIMESTEP / kernel のパスは build.ninja から -D で渡る。
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
/* build.ninja が生成する。中身は nbody.c の sha256 先頭 16 桁。実機で走らせている
   バイナリがどの版か、出力そのものから分かるようにする。 */
#include "build_id.h"

#ifndef UNROLL
#define UNROLL 4096
#endif
#ifndef NEWTON
#define NEWTON 3
#endif
#ifndef NTIMESTEP
#define NTIMESTEP 3
#endif
#define M UNROLL
#define MAX_DEV 4
#ifndef DEVS_DEFAULT
#define DEVS_DEFAULT "0"
#endif

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
/* 全粒子の粒子ごと並び [x,y,z,m] x N (4N u64) を置く PDM0 の番地。N=8192 では 32768 u64 になり、
   examples/28-nbody-3d と 38-nbody-4096-dram の 20480 番地からでは 40960 番地の速度の置き場と重なるので、262144 番地に移した
   (gen_stage.py の PJ_PDM と一致させる)。 */
#define OFF_PJ   (262144ULL * 8)
#define OFF_VEL  (40960ULL * 8)
#define OFF_POS  (131072ULL * 8)
#define OFF_VOUT (143360ULL * 8)
#define DMAID_TRIGGER 0x10
#define TAG_POS 0x1e
#define TAG_VEL 0x1f
#define EPS2 0.01
#define DT   0.015625
#define ETOL 0.01
/* half-mass radius (系の本体の広がり) が初期の何倍まで許すか。**最大半径ではない。**
   緩和で数個の粒子が脱出するのは正常なので、最大半径で判定すると正しい計算を FAIL に
   してしまう (実測: 1000 タイムステップで最大半径 29 倍、エネルギーは保存)。

   emu:lib で N=4096 は回せないので、現象そのものは実機でしか確かめられない。同じ判定は、
   他粒子を PDM から読む同じ構成の host で、実機の N=4096・1000 タイムステップで PASS している。 */
#define BOUND_FACTOR 5.0
#if NEWTON >= 3
#define TOL 1e-6
#else
#define TOL 0.2
#endif
#define ABSFLOOR 0.02

#define TIMESTEP_EVERY 1                     /* 位置スナップショットの間隔 (タイムステップ) */
#define SNAP_PER_FILE 128                    /* 1 ファイルにまとめるスナップショット数 */
#ifndef IC_DEFAULT
#define IC_DEFAULT "_build/ic_n4096.bin"
#endif
#ifndef RESULT_DIR
#define RESULT_DIR "_result"
#endif
/* 結果ディレクトリ。既定は RESULT_DIR で、--out=<dir> で実行時に変えられる (分け方の比較で 2 つの実行を別の場所に出すため)。 */
static const char *g_result_dir = RESULT_DIR;
#ifndef KERNEL_PREFIX
#define KERNEL_PREFIX "_build/"
#endif
/* accumulate は CHUNK 粒子ずつの分割カーネル。ACC_FMT は分割ファイル名の printf 書式
   (%d に分割番号 g)。全 N を 1 カーネルに unroll すると device の連続 DMA 確保に失敗して
   load できないため (examples/28 の実測: N=128 は成功、N=1024 以上は失敗)。 */
/* PDM0 の粒子ごと並びを全グループの DRAM へ置く kernel (gen_stage.py が生成) */
#ifndef STAGE_PATH
#define STAGE_PATH "_build/stage_n4096.idma.dat"
#endif
#ifndef ACC_FMT
#define ACC_FMT "_build/accumulate_n4096_g%d.idma.dat"
#endif
#ifndef CHUNK
#define CHUNK 128
#endif
#define NCHUNK ((M + CHUNK - 1) / CHUNK)

struct star { double x, y, z, m, vx, vy, vz, pad; };

/* スナップショット書き出し。ファイル名は固定 (pos_0000.bin, pos_0001.bin, ...)。
   pod からの回収でワイルドカードが使えないため、日時を名前に含めない。 */
typedef struct { const char *prefix; int n, count, fidx; FILE *fp; } snapw;
static void snap_write(snapw *w, const double *a, const double *b, const double *c) {
    if (!w->fp || w->count >= SNAP_PER_FILE) {
        if (w->fp) fclose(w->fp);
        char path[256];
        snprintf(path, sizeof(path), "%s/%s_%04d.bin", g_result_dir, w->prefix, w->fidx++);
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
/* 系の重心からの最大半径。符号ミスで斥力になると多数タイムステップで発散する */
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
static int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}
/* 重心から数えて質量の半分を含む半径 (half-mass radius)。系の本体がどれだけ広がったかを表す。
 *
 * **最大半径では判定できない。** 重力多体系は緩和の過程で必ず一部の粒子が脱出速度を得る
 * (中心の近接散乱でエネルギーを受け取るため)。実測では 1000 タイムステップで最大半径が
 * 29 倍になったが、エネルギーは 2.65e-3 で保存しており計算は正しかった。最大半径は 1 粒子
 * 飛べば跳ね上がるので、系が壊れたかどうかの指標にならない。
 *
 * half-mass radius なら数個の脱出粒子に影響されず、系の本体の膨張だけを見られる。
 * N 体計算で標準的に使われる指標である。O(N log N)、記録時のみなので安い。 */
static double half_mass_radius(int n, const double *x, const double *y, const double *z, const double *m) {
    double mt = 0, cx = 0, cy = 0, cz = 0;
    for (int i = 0; i < n; i++) { mt += m[i]; cx += m[i] * x[i]; cy += m[i] * y[i]; cz += m[i] * z[i]; }
    cx /= mt; cy /= mt; cz /= mt;
    double *r = malloc((size_t)n * sizeof(double));
    if (!r) return -1.0;
    for (int i = 0; i < n; i++) {
        double dx = x[i] - cx, dy = y[i] - cy, dz = z[i] - cz;
        r[i] = sqrt(dx * dx + dy * dy + dz * dz);
    }
    qsort(r, n, sizeof(double), cmp_double);
    /* 等質量を前提に、内側から n/2 個目の半径を返す (この app は m[i] = 1/N で一様) */
    double rh = r[n / 2];
    free(r);
    return rh;
}
/* 全運動量と重心。どちらも保存量なので、ずれれば実装か device の異常を示す。O(N) で安い。 */
/* 全運動量と重心。どちらも保存量なので、ずれれば実装か device の異常を示す。O(N) で安い。 */
static void momentum_and_com(int n, const double *x, const double *y, const double *z, const double *m,
                             const double *vx, const double *vy, const double *vz,
                             double *p, double *com) {
    double mt = 0;
    p[0] = p[1] = p[2] = com[0] = com[1] = com[2] = 0;
    for (int i = 0; i < n; i++) {
        mt += m[i];
        p[0] += m[i] * vx[i]; p[1] += m[i] * vy[i]; p[2] += m[i] * vz[i];
        com[0] += m[i] * x[i]; com[1] += m[i] * y[i]; com[2] += m[i] * z[i];
    }
    com[0] /= mt; com[1] /= mt; com[2] /= mt;
}

/* ---- 性能計測の準備 --------------------------------------------------------
 *
 * 状態のビットを立てている箇所は、そのまま計測点になる。**いまは何も実行しない。**
 *
 * 本体の挙動を変えないことを優先し、NBODY_TIMING を定義しない限り
 * **プリプロセッサの段階で消える**形にしてある。空の inline 関数では足りない。
 * このビルドは -O を指定していないので、空でも呼び出しがそのまま残るためである。
 *
 * 計測を入れるときは checkpoint_now() の中身を書く。**呼び出し側は変えなくてよい。** */
enum cp_id {
    CP_OPEN, CP_ALLOC, CP_INPUT, CP_LOAD, CP_SEND_INIT, CP_EXEC_INIT, CP_ACCEL0,
    CP_KICK1, CP_DRIFT, CP_COLLECT_POS, CP_IDLE_TRANSPOSE, CP_ACCUM, CP_KICK2,
    CP_RECV_POS, CP_COLLECT_VEL, CP_RECV_VEL, CP_MONITOR, CP_LOOP, CP_JUDGE,
    CP_COUNT
};

#ifdef NBODY_TIMING
static inline void checkpoint_now(enum cp_id id) {
    /* ここに時刻の取得を書く。いまは空。 */
    (void)id;
}
#define CP(id) checkpoint_now(id)
#else
#define CP(id) ((void)0)
#endif

/* ---- 実行状態の記録 --------------------------------------------------------
 *
 * 失敗をその場で出力せず、**到達した段階と失敗の種別をビットで積む**。処理の最後に
 * 期待した状態と突き合わせ、違っていたときだけ詳細を出す。期待どおりなら何も出さない。
 *
 * その場で出力する方式だと、出力のタイミングが device の状態を乱す可能性があり、
 * また失敗した瞬間の queue の状態が残らない。ビットで積んでおけば、最後にまとめて
 * 同じ書式で出せる。
 *
 * OK ビットは「そこまで到達した」、NG ビットは「そこで失敗した」を表す。
 * 正常終了は OK_ALL がすべて立ち、NG が 1 つも立っていない状態である。 */
#define OK_OPEN      (1u <<  0)   /* device を開いた */
#define OK_ALLOC     (1u <<  1)   /* host バッファを確保した */
#define OK_INPUT     (1u <<  2)   /* 初期条件を読んだ */
#define OK_LOAD      (1u <<  3)   /* kernel を load した */
#define OK_SEND_INIT (1u <<  4)   /* 初期データを送った */
#define OK_EXEC_INIT (1u <<  5)   /* init を実行した */
#define OK_ACCEL0    (1u <<  6)   /* 初回の力の評価を終えた */
#define OK_LOOP      (1u <<  7)   /* 全タイムステップを終えた */
#define OK_JUDGE     (1u <<  8)   /* 判定まで到達した */
#define OK_ALL (OK_OPEN | OK_ALLOC | OK_INPUT | OK_LOAD | OK_SEND_INIT | \
                OK_EXEC_INIT | OK_ACCEL0 | OK_LOOP | OK_JUDGE)

#define NG_OPEN      (1u << 16)   /* device が開けない */
#define NG_ALLOC     (1u << 17)   /* バッファ確保に失敗 */
#define NG_INPUT     (1u << 18)   /* 入力ファイルが読めない */
#define NG_LOAD      (1u << 19)   /* kernel の load に失敗 */
#define NG_SEND      (1u << 20)   /* 送信に失敗 */
#define NG_EXEC      (1u << 21)   /* kernel の起動に失敗 */
#define NG_RECV      (1u << 22)   /* 受信に失敗 */
#define NG_IDLE      (1u << 23)   /* idle 待ちに失敗 */
#define NG_VALUE     (1u << 24)   /* host 倍精度と値が合わない */
#define NG_ENERGY    (1u << 25)   /* エネルギーが保たれない */
#define NG_EXPAND    (1u << 26)   /* 系の本体が膨張しすぎ */

static const struct { unsigned bit; const char *name; } OK_NAMES[] = {
    { OK_OPEN,      "device を開いた" },
    { OK_ALLOC,     "host バッファを確保した" },
    { OK_INPUT,     "初期条件を読んだ" },
    { OK_LOAD,      "kernel を load した" },
    { OK_SEND_INIT, "初期データを送った" },
    { OK_EXEC_INIT, "init を実行した" },
    { OK_ACCEL0,    "初回の力の評価を終えた" },
    { OK_LOOP,      "全タイムステップを終えた" },
    { OK_JUDGE,     "判定まで到達した" },
};
static const struct { unsigned bit; const char *name; } NG_NAMES[] = {
    { NG_OPEN,   "device が開けない" },
    { NG_ALLOC,  "バッファ確保に失敗" },
    { NG_INPUT,  "入力ファイルが読めない" },
    { NG_LOAD,   "kernel の load に失敗" },
    { NG_SEND,   "送信に失敗" },
    { NG_EXEC,   "kernel の起動に失敗" },
    { NG_RECV,   "受信に失敗" },
    { NG_IDLE,   "idle 待ちに失敗" },
    { NG_VALUE,  "host 倍精度と値が合わない" },
    { NG_ENERGY, "エネルギーが保たれない" },
    { NG_EXPAND, "系の本体が膨張しすぎ" },
};

/* 情報ビット。**立っていてもいなくてもよい**ので、判定には使わない。
   失敗したときは常に出る。成功したときは --show-flags を付けたときだけ出る。
   「待ちに入った」のような、正常でも起きうる事象を記録する。 */
#define IN_QUEUE_FULL (1u << 0)   /* exec_q がキューに空きが無く積み直した */
#define IN_OVERFLOW   (1u << 1)   /* 命令バッファの overflow を観測した */
#define IN_SNAP_SPILL (1u << 2)   /* スナップショットが 2 ファイル目に入った */
#define IN_VERIFY_RUN (1u << 3)   /* host 倍精度との照合を実行した */
#define IN_MONITOR_RUN (1u << 4)  /* 保存量を記録した */
#define IN_IDMA_WAITED (1u << 5)  /* idle 待ちに入ったとき IDMA がまだ動いていた */
#define IN_DDMA_WAITED (1u << 6)  /* idle 待ちに入ったとき DDMA がまだ動いていた */

static const struct { unsigned bit; const char *name; } IN_NAMES[] = {
    { IN_QUEUE_FULL,  "キューに空きが無く積み直した" },
    { IN_OVERFLOW,    "命令バッファの overflow を観測した" },
    { IN_SNAP_SPILL,  "スナップショットが 2 ファイル目に入った" },
    { IN_VERIFY_RUN,  "host 倍精度との照合を実行した" },
    { IN_MONITOR_RUN, "保存量を記録した" },
    { IN_IDMA_WAITED, "IDMA の idle 待ちで実際に待った" },
    { IN_DDMA_WAITED, "DDMA の idle 待ちで実際に待った" },
};

/* デバイスのハンドルと枚数。失敗がどのデバイスで起きたかを、ハンドルから逆引きして記録する。 */
static mnc2_device_t g_dev_handles[MAX_DEV];
static int g_nd = 1, g_block = M;
static int dev_index(mnc2_device_t dev) {
    if (!dev) return -1;
    for (int i = 0; i < g_nd; i++) if (g_dev_handles[i] == dev) return i;
    return -1;
}

static struct {
    unsigned flags;
    int device;               /* 失敗したデバイスの番号 (何枚目か。該当なしは -1) */
    const char *where;        /* 失敗した箇所の名前 */
    int step;                 /* そのときのタイムステップ。ループ外は -1 */
    int chunk;                /* accumulate の分割番号。該当なしは -1 */
    int api_rc;               /* API の戻り値 */
    mnc2_idma_stat_t idma;    /* 失敗を検出した「後」の IDMA の状態 */
    int idma_ok;              /* 上が取れたか */
    unsigned info;            /* 情報ビット。判定には使わない */
    long exec_count;          /* kernel を起動した回数 */
    long busy_retry;          /* MNC2_ERROR_BUSY を受けて積み直した回数 (通し) */
    int max_n_dma;            /* 観測した n_dma の最大 */
    int max_ibuf;             /* 観測した inst_in_ibuf の最大 */
    long overflow_count;      /* overflow を観測した回数 */
} rs = { 0, -1, NULL, -1, -1, 0, { 0, 0, 0 }, 0, 0, 0, 0, 0, 0, 0 };

/* IDMA の状態を見るたびに、最大値と overflow を控える。exec_q が元々 PIO read して
   いるものをそのまま使うので、余分な読み出しは増えない。 */
static void note_stat(const mnc2_idma_stat_t *st) {
    if (st->n_dma > rs.max_n_dma) rs.max_n_dma = st->n_dma;
    if (st->inst_in_ibuf > rs.max_ibuf) rs.max_ibuf = st->inst_in_ibuf;
    if (st->overflow) { rs.info |= IN_OVERFLOW; rs.overflow_count++; }
}

/* 失敗を記録する。**最初の 1 件だけ**を詳細として残す (以降は後始末の失敗が多く、
   最初の 1 件が原因に最も近い)。失敗した瞬間の IDMA の状態も控える。 */
static void note_fail(mnc2_device_t dev, unsigned bit, const char *where,
                      int step, int chunk, int api_rc) {
    int first = ((rs.flags & 0xffff0000u) == 0);
    rs.flags |= bit;
    if (!first) return;
    rs.where = where; rs.step = step; rs.chunk = chunk; rs.api_rc = api_rc; rs.device = dev_index(dev);
    if (dev) rs.idma_ok = (mnc2_get_idma_stat(dev, &rs.idma) == MNC2_SUCCESS);
}

/* IDMA キューに空きが無いと mnc2_exec_kernel は MNC2_ERROR_BUSY を返す (このとき積んで
   いない)。mnc2_wait_idma_idle で毎回掃くとキューが空になって並列性が途切れるので、
   空きができ次第に 1 個補充する。 */
static int exec_q(mnc2_device_t dev, mnc2_kernel_t k) {
    rs.exec_count++;
    int r = mnc2_exec_kernel(k);
    while (r == MNC2_ERROR_BUSY) {
        mnc2_idma_stat_t st;
        if (mnc2_get_idma_stat(dev, &st) == MNC2_SUCCESS) note_stat(&st);
        rs.info |= IN_QUEUE_FULL;
        rs.busy_retry++;
        r = mnc2_exec_kernel(k);
    }
    return r;
}

/* idle 待ちに入る前に、待つ対象が動いているかを見る。**待つこと自体は異常ではない**ので
   情報ビットに記録するだけで、判定には使わない。 */
static void note_before_idle(mnc2_device_t dev) {
    mnc2_idma_stat_t is;
    mnc2_ddma_stat_t ds;
    if (mnc2_get_idma_stat(dev, &is) == MNC2_SUCCESS) {
        note_stat(&is);
        if (is.n_dma > 0) rs.info |= IN_IDMA_WAITED;
    }
    if (mnc2_get_ddma_stat(dev, &ds) == MNC2_SUCCESS) {
        for (int i = 0; i < 2; i++)
            if (ds.host_to_pdm[i].active || ds.pdm_to_host[i].active)
                rs.info |= IN_DDMA_WAITED;
    }
}

/* exec_q を呼び、失敗したら記録して 0 を返す。呼び出し側は if (!EXEC(...)) goto done。 */
static int exec_step(mnc2_device_t dev, mnc2_kernel_t k, const char *where,
                     int step, int chunk) {
    int r = exec_q(dev, k);
    if (r == MNC2_SUCCESS) return 1;
    note_fail(dev, NG_EXEC, where, step, chunk, r);
    return 0;
}

/* 期待した状態で終わったかを判定する。違っていたときだけ詳細を出し、期待どおりなら
   何も出さない。device を閉じる前に呼ぶこと (IDMA の設定値を読むため)。 */
static void report_state(mnc2_device_t dev, const char *ic_path,
                         int snap_every, int monitor_every, int verify_n, int show) {
    int ok = (rs.flags == OK_ALL);
    if (ok && !show) return;

    fprintf(stderr, ok ? "\n=== 実行状態 (期待どおり) ===\n"
                       : "\n=== 実行状態が期待と違う ===\n");
    fprintf(stderr, "  到達した段階 (x = 到達):\n");
    for (size_t i = 0; i < sizeof(OK_NAMES) / sizeof(OK_NAMES[0]); i++)
        fprintf(stderr, "    [%c] %s\n",
                (rs.flags & OK_NAMES[i].bit) ? 'x' : ' ', OK_NAMES[i].name);

    if (rs.flags & 0xffff0000u) {
        fprintf(stderr, "  起きたこと:\n");
        for (size_t i = 0; i < sizeof(NG_NAMES) / sizeof(NG_NAMES[0]); i++)
            if (rs.flags & NG_NAMES[i].bit)
                fprintf(stderr, "    %s\n", NG_NAMES[i].name);
    }

    if (rs.where) {
        fprintf(stderr, "  最初の失敗: %s", rs.where);
        if (rs.device >= 0) fprintf(stderr, " (デバイス %d 枚目)", rs.device);
        if (rs.chunk >= 0) fprintf(stderr, " 分割 %d", rs.chunk);
        if (rs.step >= 0) fprintf(stderr, " (timestep %d)", rs.step);
        fprintf(stderr, "\n  API の戻り値: %d\n", rs.api_rc);
        if (rs.idma_ok)
            fprintf(stderr, "  失敗した後の IDMA:   n_dma=%d inst_in_ibuf=%d overflow=%d\n",
                    rs.idma.n_dma, rs.idma.inst_in_ibuf, rs.idma.overflow);
        else
            fprintf(stderr, "  失敗した後の IDMA:   取得できなかった\n");
    }

    /* ここから下は判定に使わない。立っていてもいなくてもよい事象と、通しの計数。 */
    fprintf(stderr, "  --- 情報 (判定には使わない) ---\n");
    for (size_t i = 0; i < sizeof(IN_NAMES) / sizeof(IN_NAMES[0]); i++)
        fprintf(stderr, "    [%c] %s\n",
                (rs.info & IN_NAMES[i].bit) ? 'x' : ' ', IN_NAMES[i].name);
    fprintf(stderr, "    kernel 起動 %ld 回 / BUSY での積み直し %ld 回"
                    " / n_dma 最大 (BUSY 時・idle 待ち前の観測) %d / inst_in_ibuf 最大 %d"
                    " / overflow 観測 %ld 回\n",
            rs.exec_count, rs.busy_retry, rs.max_n_dma, rs.max_ibuf, rs.overflow_count);

    fprintf(stderr, "  実行条件: N=%d デバイス=%d 枚 x %d 粒子 newton=%d timestep=%d chunk=%d 分割=%d\n",
            M, g_nd, g_block, NEWTON, NTIMESTEP, CHUNK, NCHUNK);
    fprintf(stderr, "            every=%d monitor=%d verify=%d\n",
            snap_every, monitor_every, verify_n);
    fprintf(stderr, "  入力: %s\n", ic_path);
    fprintf(stderr, "  build: %s\n", BUILD_ID);
    if (dev)
        fprintf(stderr, "  DMA 待ちの上限: idma_complete_max=%d idma_queue_limit=%d\n",
                mnc2_get_idma_complete_max(dev), mnc2_get_idma_queue_limit(dev));
    fprintf(stderr, "  flags=0x%08x (期待 0x%08x) info=0x%08x\n", rs.flags, OK_ALL, rs.info);
}

/* "--verify=8" のような形から値を取り出す。値の無い "--verify" は all を返す。 */
static int opt_value(const char *arg, const char *name, int all, int *out) {
    size_t nlen = strlen(name);
    if (strncmp(arg, name, nlen) != 0) return 0;
    if (arg[nlen] == '\0') { *out = all; return 1; }
    if (arg[nlen] != '=') return 0;
    *out = atoi(arg + nlen + 1);
    return 1;
}

/* デバイス 1 枚ぶんの状態。kernel は、デバイス非依存かどうかが公開文書で確かめられないので、
   デバイスごとに load する。 */
typedef struct {
    int no;                         /* mnc2_open に渡したデバイス番号 */
    mnc2_device_t dev;
    void *dist_b, *vel_b, *con_b, *bc_b, *pos_b, *vout_b;
    mnc2_kernel_t k_init, k_kick, k_drift, k_cp, k_cv, k_stage;
    mnc2_kernel_t k_acc[NCHUNK];
} dctx;

/* "0,1" のようなデバイス番号の並びを読む。 */
static int parse_devs(const char *s, int *out, int max) {
    int n = 0;
    const char *p = s;
    while (*p && n < max) {
        out[n++] = atoi(p);
        while (*p && *p != ',') p++;
        if (*p == ',') p++;
    }
    return n;
}

int main(int argc, char **argv) {
    const char *ic_path = IC_DEFAULT;
    int verify_n = 0;                   /* 位置照合を行う先頭タイムステップ数 */
    int monitor_every = TIMESTEP_EVERY; /* 保存量を記録する間隔 */
    int snap_every = TIMESTEP_EVERY;    /* 位置スナップショットを出す間隔 */
    int show_flags = 0;                 /* 成功しても実行状態を出す */
    int write_vel = 0;                  /* 速度のスナップショットも出す (分け方の比較に使う) */
    int devno[MAX_DEV];
    int nd = parse_devs(DEVS_DEFAULT, devno, MAX_DEV);

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--show-flags") == 0) { show_flags = 1; continue; }
        if (strcmp(argv[i], "--vel") == 0) { write_vel = 1; continue; }
        if (strncmp(argv[i], "--devs=", 7) == 0) { nd = parse_devs(argv[i] + 7, devno, MAX_DEV); continue; }
        if (strncmp(argv[i], "--out=", 6) == 0) { g_result_dir = argv[i] + 6; continue; }
        if (opt_value(argv[i], "--verify", NTIMESTEP, &verify_n)) continue;
        if (opt_value(argv[i], "--monitor", 1, &monitor_every)) continue;
        if (opt_value(argv[i], "--every", 1, &snap_every)) continue;
        if (argv[i][0] == '-') {
            fprintf(stderr, "usage: %s [<ic.bin>] [--devs=<n>,<n>] [--every=<n>] [--verify=<n>] [--monitor=<n>] [--vel] [--out=<dir>] [--show-flags]\n", argv[0]);
            return 1;
        }
        ic_path = argv[i];
    }
    if (snap_every < 1) snap_every = 1;   /* 0 以下は「毎回」として扱う */
    if (nd < 1 || M % nd != 0 || M / nd > PE_COUNT) {
        fprintf(stderr, "FAIL: N=%d をデバイス %d 枚に等分できない、または 1 枚あたりが PE 数 %d を超える\n", M, nd, PE_COUNT);
        return 1;
    }
    const int B = M / nd;               /* 1 枚あたりの粒子数 */
    g_nd = nd; g_block = B;

    printf("=== 3D N 体 leapfrog (N=%d、デバイス %d 枚 x 1 枚あたり %d 粒子、newton=%d, %d タイムステップ, 入力 %s) ===\n",
           M, nd, B, NEWTON, NTIMESTEP, ic_path);
    printf("    デバイス番号:");
    for (int d = 0; d < nd; d++) printf(" %d", devno[d]);
    printf("\n    位置の記録: %d タイムステップごと (初期を含めて %d コマ)\n",
           snap_every, 1 + NTIMESTEP / snap_every);
    printf("    照合: 先頭 %d タイムステップ / 保存量の記録: %d タイムステップごと\n\n",
           verify_n, monitor_every);
    printf("    build: %s (nbody.c の sha256 先頭 16 桁)\n", BUILD_ID);

    dctx dc[MAX_DEV];
    memset(dc, 0, sizeof dc);
    for (int d = 0; d < nd; d++) {
        dc[d].no = devno[d];
        dc[d].dev = mnc2_open(devno[d]);
        if (!dc[d].dev) {
            note_fail(NULL, NG_OPEN, "mnc2_open", -1, -1, devno[d]);
            report_state(NULL, ic_path, snap_every, monitor_every, verify_n, show_flags);
            for (int e = 0; e < d; e++) mnc2_close(dc[e].dev);
            return 1;
        }
        g_dev_handles[d] = dc[d].dev;
        /* 大きい N では recv 前の IDMA idle 待ちで多数の分割を流し切る。待ち上限 (default 5000)
           が足りないので上げる。 */
        mnc2_set_idma_complete_max(dc[d].dev, 1000000);
    }
    rs.flags |= OK_OPEN; CP(CP_OPEN);

    int rc = 0;
    FILE *mf = NULL;
    size_t mbytes = (size_t)M * sizeof(double);
    double *hx = malloc(mbytes), *hy = malloc(mbytes), *hz = malloc(mbytes), *hm = malloc(mbytes);
    double *hvx = malloc(mbytes), *hvy = malloc(mbytes), *hvz = malloc(mbytes);
    double *gx = malloc(mbytes), *gy = malloc(mbytes), *gz = malloc(mbytes);
    double *gvx = malloc(mbytes), *gvy = malloc(mbytes), *gvz = malloc(mbytes);
    double *ga = malloc(mbytes), *gb = malloc(mbytes), *gc = malloc(mbytes);
    int alloc_ok = hx && hy && hz && hm && hvx && hvy && hvz && gx && gy && gz && gvx && gvy && gvz && ga && gb && gc;
    for (int d = 0; d < nd; d++) {
        dc[d].dist_b = mnc2_alloc_host_buffer(dc[d].dev, DIST_BYTES);
        dc[d].vel_b  = mnc2_alloc_host_buffer(dc[d].dev, VEL_BYTES);
        dc[d].con_b  = mnc2_alloc_host_buffer(dc[d].dev, BCAST_BYTES);
        dc[d].bc_b   = mnc2_alloc_host_buffer(dc[d].dev, PJ_BYTES);
        dc[d].pos_b  = mnc2_alloc_host_buffer(dc[d].dev, 3 * ELEM_BYTES);
        dc[d].vout_b = mnc2_alloc_host_buffer(dc[d].dev, 3 * ELEM_BYTES);
        if (!dc[d].dist_b || !dc[d].vel_b || !dc[d].con_b || !dc[d].bc_b || !dc[d].pos_b || !dc[d].vout_b) alloc_ok = 0;
    }
    if (!alloc_ok) {
        note_fail(dc[0].dev, NG_ALLOC, "host バッファ", -1, -1, 0);
        rc = 1; goto done;
    }
    rs.flags |= OK_ALLOC; CP(CP_ALLOC);

    /* --- 入力: struct 形バイナリを mmap して先頭 N 粒子を読む --- */
    {
        int fd = open(ic_path, O_RDONLY);
        if (fd < 0) { note_fail(dc[0].dev, NG_INPUT, "open", -1, -1, 0); rc = 1; goto done; }
        size_t need = (size_t)M * sizeof(struct star);
        struct star *ic = mmap(NULL, need, PROT_READ, MAP_PRIVATE, fd, 0);
        close(fd);
        if (ic == MAP_FAILED) { note_fail(dc[0].dev, NG_INPUT, "mmap", -1, -1, 0); rc = 1; goto done; }
        for (int i = 0; i < M; i++) {
            hx[i] = ic[i].x; hy[i] = ic[i].y; hz[i] = ic[i].z; hm[i] = ic[i].m;
            hvx[i] = ic[i].vx; hvy[i] = ic[i].vy; hvz[i] = ic[i].vz;
        }
        munmap(ic, need);
        rs.flags |= OK_INPUT; CP(CP_INPUT);
    }
    for (int i = 0; i < M; i++) { gx[i] = hx[i]; gy[i] = hy[i]; gz[i] = hz[i]; gvx[i] = hvx[i]; gvy[i] = hvy[i]; gvz[i] = hvz[i]; }
    double rmax0 = max_radius(M, hx, hy, hz, hm);
    double rh0 = half_mass_radius(M, hx, hy, hz, hm);   /* 判定にはこちらを使う */
    double p0[3], com0[3];
    momentum_and_com(M, hx, hy, hz, hm, hvx, hvy, hvz, p0, com0);

    /* --- 出力: 固定名のマニフェストを開く (保存量を追記していく) --- */
    mkdir(g_result_dir, 0755);
    {
        char mpath[256]; snprintf(mpath, sizeof(mpath), "%s/run.json", g_result_dir);
        mf = fopen(mpath, "w");
        if (mf) {
            fprintf(mf, "{\n  \"build_id\": \"%s\",\n  \"N\": %d,\n  \"dt\": %.17g,\n  \"n_timestep\": %d,\n  \"eps2\": %.17g,\n",
                    BUILD_ID, M, (double)DT, NTIMESTEP, (double)EPS2);
            fprintf(mf, "  \"devices\": %d,\n  \"particles_per_device\": %d,\n  \"device_numbers\": [", nd, B);
            for (int d = 0; d < nd; d++) fprintf(mf, "%s%d", d ? ", " : "", devno[d]);
            fprintf(mf, "],\n");
            fprintf(mf, "  \"newton\": %d,\n  \"chunk\": %d,\n  \"n_kernel_split\": %d,\n",
                    NEWTON, CHUNK, NCHUNK);
            fprintf(mf, "  \"timestep_every\": %d,\n  \"snapshots_per_file\": %d,\n",
                    snap_every, SNAP_PER_FILE);
            fprintf(mf, "  \"n_snapshot\": %d,\n  \"monitor_every\": %d,\n",
                    1 + NTIMESTEP / snap_every, monitor_every);
            fprintf(mf, "  \"input\": \"%s\",\n", ic_path);
            fprintf(mf, "  \"snapshot\": {\"particles\": %d, \"components\": 3, \"order\": \"x,y,z\", \"layout\": \"particle-major\", \"endian\": \"little\", \"dtype\": \"float64\"},\n", M);
            fprintf(mf, "  \"naming\": \"pos_<NNNN>.bin (--vel を付けると vel_<NNNN>.bin も)\",\n  \"units\": \"N-body (G=1)\",\n");
            fprintf(mf, "  \"monitor\": [\n");
        }
    }
    snapw wp = { "pos", M, 0, 0, NULL }, wv = { "vel", M, 0, 0, NULL };

    /* --- kernel をデバイスごとに load する --- */
    {
        char path[256];
        for (int d = 0; d < nd; d++) {
            mnc2_device_t dev = dc[d].dev;
            snprintf(path, sizeof(path), "%sinit.idma.dat", KERNEL_PREFIX);
            dc[d].k_init = mnc2_load_kernel(dev, path);
            snprintf(path, sizeof(path), "%skick.idma.dat", KERNEL_PREFIX);
            dc[d].k_kick = mnc2_load_kernel(dev, path);
            snprintf(path, sizeof(path), "%sdrift.idma.dat", KERNEL_PREFIX);
            dc[d].k_drift = mnc2_load_kernel(dev, path);
            snprintf(path, sizeof(path), "%scollect_pos.idma.dat", KERNEL_PREFIX);
            dc[d].k_cp = mnc2_load_kernel(dev, path);
            snprintf(path, sizeof(path), "%scollect_vel.idma.dat", KERNEL_PREFIX);
            dc[d].k_cv = mnc2_load_kernel(dev, path);
            dc[d].k_stage = mnc2_load_kernel(dev, STAGE_PATH);
            int ok = dc[d].k_init && dc[d].k_kick && dc[d].k_drift && dc[d].k_cp && dc[d].k_cv && dc[d].k_stage;
            if (!dc[d].k_init)  note_fail(dev, NG_LOAD, "init", -1, -1, 0);
            if (!dc[d].k_kick)  note_fail(dev, NG_LOAD, "kick", -1, -1, 0);
            if (!dc[d].k_drift) note_fail(dev, NG_LOAD, "drift", -1, -1, 0);
            if (!dc[d].k_cp)    note_fail(dev, NG_LOAD, "collect_pos", -1, -1, 0);
            if (!dc[d].k_cv)    note_fail(dev, NG_LOAD, "collect_vel", -1, -1, 0);
            if (!dc[d].k_stage) note_fail(dev, NG_LOAD, "stage", -1, -1, 0);
            /* accumulate は NCHUNK 個の分割カーネル。ACC_FMT の %d に g を入れて順にロードする。 */
            for (int g = 0; g < NCHUNK; g++) {
                snprintf(path, sizeof(path), ACC_FMT, g);
                dc[d].k_acc[g] = mnc2_load_kernel(dev, path);
                if (!dc[d].k_acc[g]) { note_fail(dev, NG_LOAD, "accumulate", -1, g, d); ok = 0; }
            }
            if (!ok) { rc = 1; goto done; }
        }
        rs.flags |= OK_LOAD; CP(CP_LOAD);
    }

    /* --- init: デバイス d に、粒子 d*B 〜 (d+1)*B-1 を配る。B < PE 数のとき、残りの PE は
       そのデバイスの先頭の粒子で埋める (計算は無視するが NaN を避ける) --- */
    for (int d = 0; d < nd; d++) {
        mnc2_device_t dev = dc[d].dev;
        double *c = (double *)dc[d].con_b;
        for (int t = 0; t < BCAST_COUNT; t++) c[t] = 0.0;
        c[0] = EPS2; c[1] = 0.5; c[2] = 1.5; c[3] = DT;
        if (mnc2_send(dev, dc[d].con_b, OFF_CONST, BCAST_BYTES, 0) != MNC2_SUCCESS) {
            note_fail(dev, NG_SEND, "定数", -1, -1, 0); rc = 1; goto done; }
        double *v = (double *)dc[d].vel_b;
        double *dd = (double *)dc[d].dist_b;
        for (int i = 0; i < PE_COUNT; i++) {
            int gi = d * B + (i < B ? i : 0);
            v[i] = hvx[gi]; v[PE_COUNT + i] = hvy[gi]; v[2 * PE_COUNT + i] = hvz[gi];
            dd[i] = hx[gi]; dd[PE_COUNT + i] = hy[gi]; dd[2 * PE_COUNT + i] = hz[gi]; dd[3 * PE_COUNT + i] = hm[gi];
        }
        if (mnc2_send(dev, dc[d].vel_b, OFF_VEL, VEL_BYTES, 0) != MNC2_SUCCESS) {
            note_fail(dev, NG_SEND, "初期速度", -1, -1, 0); rc = 1; goto done; }
        if (mnc2_send(dev, dc[d].dist_b, OFF_DIST, DIST_BYTES, DMAID_TRIGGER) != MNC2_SUCCESS) {
            note_fail(dev, NG_SEND, "初期位置", -1, -1, 0); rc = 1; goto done; }
    }
    rs.flags |= OK_SEND_INIT; CP(CP_SEND_INIT);
    for (int d = 0; d < nd; d++)
        if (!exec_step(dc[d].dev, dc[d].k_init, "init", -1, -1)) { rc = 1; goto done; }
    rs.flags |= OK_EXEC_INIT; CP(CP_EXEC_INIT);

    /* 初期の力 + 初期スナップショット。
       全粒子の位置 [x,y,z,m] を、全デバイスの PDM0 へ送り (ホスト ↔ PDM0。DRAM へは直接送れない)、
       stage kernel が各デバイスの全グループの DRAM へ置く。accumulate は DRAM から読む。 */
    {
        for (int d = 0; d < nd; d++) {
            double *bp = (double *)dc[d].bc_b;
            for (int t = 0; t < PJ_COUNT; t++) bp[t] = 0.0;
            for (int k = 0; k < M; k++) { bp[k * 4] = hx[k]; bp[k * 4 + 1] = hy[k]; bp[k * 4 + 2] = hz[k]; bp[k * 4 + 3] = hm[k]; }
            if (mnc2_send(dc[d].dev, dc[d].bc_b, OFF_PJ, PJ_BYTES, 0) != MNC2_SUCCESS) {
                note_fail(dc[d].dev, NG_SEND, "初回の粒子ごと配列", -1, -1, 0); rc = 1; goto done; }
        }
        /* デバイス 0 に積み切ってから、デバイス 1 に積む (交互には積まない) */
        for (int d = 0; d < nd; d++) {
            if (!exec_step(dc[d].dev, dc[d].k_stage, "初回の stage", -1, -1)) { rc = 1; goto done; }
            for (int g = 0; g < NCHUNK; g++)
                if (!exec_step(dc[d].dev, dc[d].k_acc[g], "初回の accumulate", -1, g)) { rc = 1; goto done; }
        }
        rs.flags |= OK_ACCEL0; CP(CP_ACCEL0);
    }
    snap_write(&wp, hx, hy, hz);
    if (write_vel) snap_write(&wv, hvx, hvy, hvz);
    double e0 = total_energy(M, hx, hy, hz, hm, hvx, hvy, hvz), emax = 0;
    int monitor_count = 0;

    /* --- device leapfrog --- */
    for (int step = 0; step < NTIMESTEP; step++) {
        CP(CP_KICK1);
        for (int d = 0; d < nd; d++) {
            if (!exec_step(dc[d].dev, dc[d].k_kick, "kick 前半", step, -1)) { rc = 1; goto done; }
            if (!exec_step(dc[d].dev, dc[d].k_drift, "drift", step, -1)) { rc = 1; goto done; }
            if (!exec_step(dc[d].dev, dc[d].k_cp, "collect_pos", step, -1)) { rc = 1; goto done; }
        }
        /* collect_pos は分割カーネルの後ろに積まれるので、IDMA を流し切ってから recv する。
           流し切らないと collect の done タグが立つ前に recv の DDMA 待ちが尽きる。 */
        for (int d = 0; d < nd; d++) {
            mnc2_device_t dev = dc[d].dev;
            memset(dc[d].pos_b, 0, 3 * ELEM_BYTES);
            note_before_idle(dev);
            if (mnc2_wait_idma_idle(dev) != MNC2_SUCCESS) {
                note_fail(dev, NG_IDLE, "idma idle (位置回収前)", step, -1, 0); rc = 1; goto done; }
            if (mnc2_recv(dev, dc[d].pos_b, OFF_POS, 3 * ELEM_BYTES, TAG_POS) != MNC2_SUCCESS) {
                note_fail(dev, NG_RECV, "recv 位置", step, -1, 0); rc = 1; goto done; }
            /* 受信の後始末をしてから次の IDMA を起動する。片付けずに起動すると、間を空けても
               起動が通らないことがある (実機の実測で確認)。 */
            mnc2_wait_ddma_idle(dev);
            /* デバイス d の PE i の値は、粒子 d*B + i の新しい位置 */
            double *dp = (double *)dc[d].pos_b;
            for (int i = 0; i < B; i++) {
                int gi = d * B + i;
                hx[gi] = dp[i]; hy[gi] = dp[PE_COUNT + i]; hz[gi] = dp[2 * PE_COUNT + i];
            }
        }
        CP(CP_RECV_POS);
        /* 全デバイスの位置が回収できたので、粒子ごとの並びを作って、全デバイスへ送り直す。
           デバイス同士は直接つながらないので、この往復はホストだけが行う。 */
        for (int d = 0; d < nd; d++) {
            double *bp = (double *)dc[d].bc_b;
            for (int t = 0; t < PJ_COUNT; t++) bp[t] = 0.0;
            for (int k = 0; k < M; k++) { bp[k * 4] = hx[k]; bp[k * 4 + 1] = hy[k]; bp[k * 4 + 2] = hz[k]; bp[k * 4 + 3] = hm[k]; }
            if (mnc2_send(dc[d].dev, dc[d].bc_b, OFF_PJ, PJ_BYTES, 0) != MNC2_SUCCESS) {
                note_fail(dc[d].dev, NG_SEND, "粒子ごと配列", step, -1, 0); rc = 1; goto done; }
            mnc2_wait_ddma_idle(dc[d].dev);
        }
        /* デバイス 0 に積み切ってから、デバイス 1 に積む (交互には積まない) */
        for (int d = 0; d < nd; d++) {
            if (!exec_step(dc[d].dev, dc[d].k_stage, "stage", step, -1)) { rc = 1; goto done; }
            for (int g = 0; g < NCHUNK; g++)
                if (!exec_step(dc[d].dev, dc[d].k_acc[g], "accumulate", step, g)) { rc = 1; goto done; }
            CP(CP_KICK2);
            if (!exec_step(dc[d].dev, dc[d].k_kick, "kick 後半", step, -1)) { rc = 1; goto done; }
            CP(CP_COLLECT_VEL);
            if (!exec_step(dc[d].dev, dc[d].k_cv, "collect_vel", step, -1)) { rc = 1; goto done; }
        }
        for (int d = 0; d < nd; d++) {
            mnc2_device_t dev = dc[d].dev;
            memset(dc[d].vout_b, 0, 3 * ELEM_BYTES);
            note_before_idle(dev);
            if (mnc2_wait_idma_idle(dev) != MNC2_SUCCESS) {
                note_fail(dev, NG_IDLE, "idma idle (速度回収前)", step, -1, 0); rc = 1; goto done; }
            if (mnc2_recv(dev, dc[d].vout_b, OFF_VOUT, 3 * ELEM_BYTES, TAG_VEL) != MNC2_SUCCESS) {
                note_fail(dev, NG_RECV, "recv 速度", step, -1, 0); rc = 1; goto done; }
            mnc2_wait_ddma_idle(dev);
            double *dv = (double *)dc[d].vout_b;
            for (int i = 0; i < B; i++) {
                int gi = d * B + i;
                hvx[gi] = dv[i]; hvy[gi] = dv[PE_COUNT + i]; hvz[gi] = dv[2 * PE_COUNT + i];
            }
        }
        CP(CP_RECV_VEL);

        if ((step + 1) % snap_every == 0) {
            snap_write(&wp, hx, hy, hz);
            if (write_vel) snap_write(&wv, hvx, hvy, hvz);
        }

        /* 保存量の監視。エネルギーは O(N^2) だが記録時のみなので安い。 */
        if (monitor_every > 0 && (step + 1) % monitor_every == 0) {
            double e = total_energy(M, hx, hy, hz, hm, hvx, hvy, hvz), rel = fabs(e - e0) / fabs(e0);
            if (rel > emax) emax = rel;
            double p[3], com[3];
            momentum_and_com(M, hx, hy, hz, hm, hvx, hvy, hvz, p, com);
            double rmax = max_radius(M, hx, hy, hz, hm);
            double rh = half_mass_radius(M, hx, hy, hz, hm);
            if (mf) {
                fprintf(mf, "%s    {\"timestep\": %d, \"energy_rel\": %.6e, \"r_max\": %.6g, \"r_half\": %.6g,",
                        monitor_count ? ",\n" : "", step + 1, rel, rmax, rh);
                fprintf(mf, " \"dp\": [%.3e, %.3e, %.3e],", p[0] - p0[0], p[1] - p0[1], p[2] - p0[2]);
                fprintf(mf, " \"dcom\": [%.3e, %.3e, %.3e]}", com[0] - com0[0], com[1] - com0[1], com[2] - com0[2]);
            }
            monitor_count++;
            rs.info |= IN_MONITOR_RUN; CP(CP_MONITOR);
        }
    }
    rs.flags |= OK_LOOP; CP(CP_LOOP);
    if (wp.fidx > 1) rs.info |= IN_SNAP_SPILL;
    snap_close(&wp); snap_close(&wv);

    /* --- 位置照合 (先頭 verify_n タイムステップぶんだけ host 倍精度で追いかける) --- */
    double maxrel = 0;
    int errors = 0;
    if (verify_n > 0) {
        rs.info |= IN_VERIFY_RUN;
        host_accel(M, gx, gy, gz, hm, ga, gb, gc);
        for (int step = 0; step < verify_n; step++) {
            for (int i = 0; i < M; i++) { gvx[i] += ga[i] * (DT * 0.5); gvy[i] += gb[i] * (DT * 0.5); gvz[i] += gc[i] * (DT * 0.5); }
            for (int i = 0; i < M; i++) { gx[i] += gvx[i] * DT; gy[i] += gvy[i] * DT; gz[i] += gvz[i] * DT; }
            host_accel(M, gx, gy, gz, hm, ga, gb, gc);
            for (int i = 0; i < M; i++) { gvx[i] += ga[i] * (DT * 0.5); gvy[i] += gb[i] * (DT * 0.5); gvz[i] += gc[i] * (DT * 0.5); }
        }
        /* verify_n == NTIMESTEP のときだけ device の最終状態と直接比べられる。
           それ以外は途中経過を保持していないので、照合は行わず記録もしない。 */
        if (verify_n == NTIMESTEP) {
            for (int i = 0; i < M; i++) {
                double val[6] = { hx[i], hy[i], hz[i], hvx[i], hvy[i], hvz[i] };
                double exp[6] = { gx[i], gy[i], gz[i], gvx[i], gvy[i], gvz[i] };
                for (int t = 0; t < 6; t++) {
                    double d = fabs(val[t] - exp[t]) / (fabs(exp[t]) > ABSFLOOR ? fabs(exp[t]) : ABSFLOOR);
                    if (d > maxrel) maxrel = d;
                    if (d > TOL) { if (errors < 5) fprintf(stderr, "  MISMATCH[%d].%d got=%g exp=%g\n", i, t, val[t], exp[t]); errors++; }
                }
            }
        }
    }

    /* --- 判定と報告 --- */
    {
        double rmax = max_radius(M, hx, hy, hz, hm);
        double rh = half_mass_radius(M, hx, hy, hz, hm);
        if (mf) {
            fprintf(mf, "\n  ],\n");
            fprintf(mf, "  \"result\": {\"energy_rel_max\": %.6e, \"r_max_initial\": %.6g, \"r_max_final\": %.6g",
                    emax, rmax0, rmax);
            fprintf(mf, ", \"r_half_initial\": %.6g, \"r_half_final\": %.6g", rh0, rh);
            if (verify_n == NTIMESTEP) fprintf(mf, ", \"position_rel_max\": %.6e", maxrel);
            fprintf(mf, "}\n}\n");
            fclose(mf);
            mf = NULL;
        }
        rs.flags |= OK_JUDGE; CP(CP_JUDGE);
        if (errors > 0) { note_fail(dc[0].dev, NG_VALUE, "値の照合", -1, -1, errors);
            fprintf(stderr, "  FAIL: 値 %d 成分ずれ (max_rel=%.3e tol=%.1e)\n", errors, maxrel, TOL); rc = 1; }
        else if (monitor_every > 0 && emax > ETOL) { note_fail(dc[0].dev, NG_ENERGY, "エネルギー保存", -1, -1, 0);
            fprintf(stderr, "  FAIL: エネルギーのずれ %.3e > %.1e\n", emax, ETOL); rc = 1; }
        /* 判定は half-mass radius で行う。最大半径は緩和で脱出する数個の粒子で跳ね上がり、
           正しい計算を FAIL にしてしまう (関数のコメント参照)。 */
        else if (rh > BOUND_FACTOR * rh0) { note_fail(dc[0].dev, NG_EXPAND, "系の広がり", -1, -1, 0);
            fprintf(stderr, "  FAIL: 系の本体が膨張しすぎ (half-mass radius %.3g -> %.3g)\n", rh0, rh); rc = 1; }
        else {
            printf("  PASS: ");
            if (verify_n == NTIMESTEP) printf("値 max_rel=%.3e、", maxrel);
            if (monitor_every > 0) printf("エネルギーのずれ %.3e、", emax);
            printf("half-mass radius %.3g->%.3g (最大半径 %.3g->%.3g)、N=%d (デバイス %d 枚) %d タイムステップ。%s/ に出力\n",
                   rh0, rh, rmax0, rmax, M, nd, NTIMESTEP, g_result_dir);
            rc = 0;
        }
    }

done:
    /* 期待した状態で終わったかを見る。違っていたときだけ詳細が出る。
       device を閉じる前に呼ぶこと (待ちの上限を読むため)。 */
    report_state(dc[0].dev, ic_path, snap_every, monitor_every, verify_n, show_flags);
    if (mf) fclose(mf);
    free(hx); free(hy); free(hz); free(hm); free(hvx); free(hvy); free(hvz);
    free(gx); free(gy); free(gz); free(gvx); free(gvy); free(gvz); free(ga); free(gb); free(gc);
    for (int d = 0; d < nd; d++) {
        mnc2_device_t dev = dc[d].dev;
        if (dc[d].dist_b) mnc2_free_host_buffer(dev, dc[d].dist_b, DIST_BYTES);
        if (dc[d].vel_b)  mnc2_free_host_buffer(dev, dc[d].vel_b, VEL_BYTES);
        if (dc[d].con_b)  mnc2_free_host_buffer(dev, dc[d].con_b, BCAST_BYTES);
        if (dc[d].bc_b)   mnc2_free_host_buffer(dev, dc[d].bc_b, PJ_BYTES);
        if (dc[d].pos_b)  mnc2_free_host_buffer(dev, dc[d].pos_b, 3 * ELEM_BYTES);
        if (dc[d].vout_b) mnc2_free_host_buffer(dev, dc[d].vout_b, 3 * ELEM_BYTES);
        mnc2_close(dev);
    }
    return rc ? 1 : 0;
}
