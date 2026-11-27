/* test_fft-4096.c -- FFT（4096 点、複素 f64）の E2E test (emu:lib / 実機)
 *
 * 入力は [-1, 1) の疑似乱数の複素数 4096 点（種は固定）。kernel の出力（PE p が X[bitrev(p)] を持つ）を、
 * 定義の通りの DFT（long double）の結果と、ノルム誤差で比べる。
 *
 * 判定の基準は、kernel を実行する前に決めた次の式で、実測で調整しない（vsmlink の 40-fft-4096 と同じ式）:
 *     ||X_kernel - X_ref||_2 <= C * log2(N) * 2^-53 * ||X_ref||_2        C = 16
 * kernel の積和 mad は、正確に丸めた積和ではない（share/mncl/mad.md）ので、ビット単位の一致は求めない。
 * 要素ごとの相対誤差は使わない（ほとんどの周波数の値が小さくなり、相対誤差が成り立たない）。
 *
 * PDM layout (fft-4096.param):
 *   ids (@identify): PDM[0..4095]
 *   x   (slot 8):    PDM[4096..12287]                 -- 実部 4096 個、虚部 4096 個
 *   tw_j(slot 8+8j): PDM[12288 + (j-1)*8192 ...]       -- j = 1..11。wr 4096 個、wi 4096 個
 *   out (slot 104):  PDM[12288 + 11*8192 ...]          -- 実部 4096 個、虚部 4096 個
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mnc2.h"

#define PES         4096
#define LOG2N       12
#define VEC_BYTES   (PES * sizeof(uint64_t))
#define CPLX_BYTES  (2 * VEC_BYTES)
#define ADDR_IDS    0ULL
#define ADDR_X      4096ULL
#define ADDR_TW(j)  (12288ULL + (unsigned long long)((j) - 1) * 8192ULL)
#define ADDR_OUT    (12288ULL + 11ULL * 8192ULL)
#define TAG_IDS     0x10
#define TAG_X       0x11
#define TAG_TW(j)   (0x11 + (j))
#define RECV_TAG    0x1e
#define TOL_C       16.0

/* [-1, 1) の疑似乱数 */
static double rnd(uint64_t i, uint64_t salt)
{
    uint64_t x = (i + 1) * 6364136223846793005ULL + salt * 1442695040888963407ULL + 1442695040888963407ULL;
    x ^= x >> 29;
    x *= 0xBF58476D1CE4E5B9ULL;
    x ^= x >> 32;
    return (double)(int64_t)(x >> 11) / 4503599627370496.0 - 1.0;   /* (x >> 11) は 53 ビット。2^52 で割って 1 を引く */
}

static unsigned bitrev(unsigned p)
{
    unsigned r = 0;
    for (int b = 0; b < LOG2N; b++) r |= ((p >> b) & 1u) << (LOG2N - 1 - b);
    return r;
}

int main(void)
{
    printf("=== 26-fft-4096: FFT（4096 点、複素 f64）===\n\n");

    mnc2_device_t dev = mnc2_open(0);
    if (dev == NULL) { fprintf(stderr, "FAIL: mnc2_open returned NULL\n"); return 1; }
    printf("26-fft-4096 backend: %s\n", mnc2_get_backend_name(dev));

    int rc = 0;
    void *idbuf = mnc2_alloc_host_buffer(dev, VEC_BYTES);
    void *xbuf  = mnc2_alloc_host_buffer(dev, CPLX_BYTES);
    void *twbuf = mnc2_alloc_host_buffer(dev, CPLX_BYTES);
    void *obuf  = mnc2_alloc_host_buffer(dev, CPLX_BYTES);
    static double xr[PES], xi[PES];
    static long double refr[PES], refi[PES], cs[PES], sn[PES];
    if (!idbuf || !xbuf || !twbuf || !obuf) { fprintf(stderr, "FAIL: alloc\n"); rc = 1; goto cleanup; }

    uint64_t *ids = (uint64_t *)idbuf;
    double *xb = (double *)xbuf, *tw = (double *)twbuf, *ob = (double *)obuf;
    for (int i = 0; i < PES; i++) {
        ids[i] = (uint64_t)i;
        xr[i] = rnd((uint64_t)i, 1);
        xi[i] = rnd((uint64_t)i, 2);
        xb[i] = xr[i];
        xb[PES + i] = xi[i];
    }

    /* 参照: 定義の通りの DFT X[k] = sum_n x[n] exp(-2πi nk/N)（long double。N^2 = 16M 回） */
    for (int t = 0; t < PES; t++) {
        long double a = -2.0L * 3.14159265358979323846264338327950288L * (long double)t / (long double)PES;
        cs[t] = cosl(a);
        sn[t] = sinl(a);
    }
    for (int k = 0; k < PES; k++) {
        long double sr = 0.0L, si = 0.0L;
        for (int n = 0; n < PES; n++) {
            int t = (int)(((long long)n * k) % PES);
            sr += (long double)xr[n] * cs[t] - (long double)xi[n] * sn[t];
            si += (long double)xr[n] * sn[t] + (long double)xi[n] * cs[t];
        }
        refr[k] = sr;
        refi[k] = si;
    }

    if (mnc2_send(dev, idbuf, ADDR_IDS * 8, VEC_BYTES, TAG_IDS) != MNC2_SUCCESS ||
        mnc2_send(dev, xbuf, ADDR_X * 8, CPLX_BYTES, TAG_X) != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: send\n"); rc = 1; goto cleanup;
    }
    /* w の表: 段 j の PE i の w = exp(-2πi (i mod 2^j) / 2^(j+1))（double で計算する） */
    for (int j = 1; j <= 11; j++) {
        for (int i = 0; i < PES; i++) {
            double a = -2.0 * M_PI * (double)(i & ((1 << j) - 1)) / (double)(2 << j);
            tw[i] = cos(a);
            tw[PES + i] = sin(a);
        }
        if (mnc2_send(dev, twbuf, ADDR_TW(j) * 8, CPLX_BYTES, TAG_TW(j)) != MNC2_SUCCESS) {
            fprintf(stderr, "FAIL: send tw%d\n", j); rc = 1; goto cleanup;
        }
    }

    mnc2_kernel_t kernel = mnc2_load_kernel(dev, "_build/fft-4096.idma.dat");
    if (!kernel) { fprintf(stderr, "FAIL: load_kernel\n"); rc = 1; goto cleanup; }
    int krc = mnc2_exec_kernel(kernel);
    mnc2_free_kernel(kernel);
    if (krc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: exec\n"); rc = 1; goto cleanup; }

    memset(obuf, 0, CPLX_BYTES);
    if (mnc2_recv(dev, obuf, ADDR_OUT * 8, CPLX_BYTES, RECV_TAG) != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: recv\n"); rc = 1; goto cleanup;
    }

    /* PE p は X[bitrev(p)] を持つ */
    long double err2 = 0.0L, ref2 = 0.0L;
    double max_abs = 0.0;
    for (int p = 0; p < PES; p++) {
        int k = (int)bitrev((unsigned)p);
        long double er = (long double)ob[p] - refr[k];
        long double ei = (long double)ob[PES + p] - refi[k];
        err2 += er * er + ei * ei;
        ref2 += refr[k] * refr[k] + refi[k] * refi[k];
        double a = (double)sqrtl(er * er + ei * ei);
        if (a > max_abs) max_abs = a;
    }
    double rel = (double)(sqrtl(err2) / sqrtl(ref2));
    double bound = TOL_C * LOG2N * ldexp(1.0, -53);
    printf("ノルム誤差（相対）: %.3e   上限（C=%.0f）: %.3e   最大の絶対誤差: %.3e\n", rel, TOL_C, bound, max_abs);
    if (!(rel <= bound)) { fprintf(stderr, "FAIL: ノルム誤差が上限を超えた\n"); rc = 1; }
    else                 { printf("PASS（4096 点。ノルム誤差が上限の内）\n"); rc = 0; }

cleanup:
    if (idbuf) mnc2_free_host_buffer(dev, idbuf, VEC_BYTES);
    if (xbuf)  mnc2_free_host_buffer(dev, xbuf, CPLX_BYTES);
    if (twbuf) mnc2_free_host_buffer(dev, twbuf, CPLX_BYTES);
    if (obuf)  mnc2_free_host_buffer(dev, obuf, CPLX_BYTES);
    mnc2_close(dev);
    return (rc != 0) ? 1 : 0;
}
