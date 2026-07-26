/* ex_nbody.c — nbody like (f64、 割り算/rsqrt なし)
 *
 * distribute と broadcast の対比を見せる example。
 *
 *   distribute  各 PE が自粒子 (x_j, m_j) を持つ。 PE ごとに違う値なので
 *               4096 PE x 2 値 = 8192 u64 を実際に運ぶ必要がある。
 *               l1bmd の cycle 0 に x_j、 cycle 1 に m_j を置き、 1 命令で 2 値を配る。
 *
 *   broadcast   粒子 i=0..NP-1 の (x_i, m_i) は全 PE で同じ値。 l2bmb が 1 回で運ぶ
 *               64 u64 を (x_i, m_i) の組 NP=32 個で埋めきるので、 データ移動は
 *               mvb 1 個 + l2bmb 1 個だけで済む。 詰め物は不要。
 *
 * 各 PE で F_j += (x_i - x_j) * (m_i * m_j) を i について累積
 * (f64: 減算=dvadd 負値、 乗算=dvmulu+dvfmad、 累積=dvadd)。 collect で F_j を回収し、
 * C の golden (同じ演算順で bit 一致) と全 PE 比較する。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "mnc2.h"

#define COUNT     4096              /* PE 数 = 自粒子数 (recv も 4096) */
#define NP        32                /* broadcast する粒子数 = 64 u64 / 2 */
#define MASS_OFF  4096              /* m_j distribute 領域の先頭 (PDM) */
#define BCAST_OFF 8192              /* (x_i, m_i) broadcast 領域の先頭 (PDM) */
#define IN_COUNT  (BCAST_OFF + NP * 2)
#define BYTES     (COUNT * sizeof(double))
#define IN_BYTES  (IN_COUNT * sizeof(double))
#define SEND_TAG 0x10
#define RECV_TAG 0x1e

int main(void)
{
    printf("[test] nbody like (NP=%d): distribute 2 値/PE -> broadcast %d 粒子 + 累積 -> collect -> golden\n",
           NP, NP);

    mnc2_device_t dev = mnc2_open(0);
    if (dev == NULL) { printf("FAIL: mnc2_open\n"); return 1; }
    int ok = 0;

    void* sendbuf = mnc2_alloc_host_buffer(dev, IN_BYTES);
    void* recvbuf = mnc2_alloc_host_buffer(dev, BYTES);
    if (sendbuf == NULL || recvbuf == NULL) { printf("FAIL: alloc\n"); goto done; }

    double* sp = (double*)sendbuf;
    /* distribute: 自粒子の位置 x_j = (double)j と質量 m_j = (double)(j+1)。
       それぞれ 4096 個を続けて置く (l1bmd の cycle 0 と cycle 1 に対応) */
    for (int j = 0; j < COUNT; j++) sp[j] = (double)j;
    for (int j = 0; j < COUNT; j++) sp[MASS_OFF + j] = (double)(j + 1);
    /* broadcast: 粒子 i の (x_i, m_i) を組で先頭から詰める。 64 u64 ちょうど。
       l1bmp が 4 u64 ずつ 16 回読むので、 詰め物 (同値埋め) は要らない */
    for (int i = 0; i < NP; i++) {
        sp[BCAST_OFF + 2 * i]     = (double)i;         /* x_i */
        sp[BCAST_OFF + 2 * i + 1] = (double)(i + 1);   /* m_i */
    }
    memset(recvbuf, 0, BYTES);

    int rc = mnc2_send(dev, sendbuf, 0, IN_BYTES, SEND_TAG);
    if (rc != 0) { printf("FAIL: send rc=%d\n", rc); goto done; }

    mnc2_kernel_t k = mnc2_load_kernel(dev, "_build/nbody.idma.dat");
    if (k == NULL) { printf("FAIL: load_kernel\n"); goto done; }
    rc = mnc2_exec_kernel(k);
    mnc2_free_kernel(k);
    if (rc != 0) { printf("FAIL: exec_kernel rc=%d\n", rc); goto done; }

    rc = mnc2_recv(dev, recvbuf, 0, BYTES, RECV_TAG);
    if (rc != 0) { printf("FAIL: recv rc=%d\n", rc); goto done; }

    double* rp = (double*)recvbuf;
    printf("  recv[0..7]: %g %g %g %g %g %g %g %g\n",
           rp[0], rp[1], rp[2], rp[3], rp[4], rp[5], rp[6], rp[7]);

    /* golden (f64): F_j = Σ_{i=0..NP-1} (x_i - x_j) * (m_i * m_j)。 emu と同じ演算順
       (減算、 m_i*m_j、 diff*w、 累積) なので各 2 項演算が一致し bit 一致する。
       値はすべて 2^53 未満の整数なので丸めは起きない */
    int bad = 0;
    for (int j = 0; j < COUNT; j++) {
        double mj = (double)(j + 1);
        double acc = 0.0;
        for (int i = 0; i < NP; i++) {
            double diff = (double)i - (double)j;
            double w = (double)(i + 1) * mj;   /* m_i * m_j */
            acc += diff * w;
        }
        if (rp[j] != acc) { bad++; if (bad <= 3) printf("  MISMATCH[%d]: got %g expect %g\n", j, rp[j], acc); }
    }
    if (bad == 0) {
        printf("PASS: 全 PE で F_j = Σ(x_i - x_j)*(m_i*m_j) が golden と bit 一致 (f64 質量積、 NP=%d)\n", NP);
        ok = 1;
    } else {
        printf("FAIL: F_j が golden と違う (%d 件)\n", bad);
    }

done:
    if (sendbuf) mnc2_free_host_buffer(dev, sendbuf, IN_BYTES);
    if (recvbuf) mnc2_free_host_buffer(dev, recvbuf, BYTES);
    mnc2_close(dev);
    return ok ? 0 : 1;
}
