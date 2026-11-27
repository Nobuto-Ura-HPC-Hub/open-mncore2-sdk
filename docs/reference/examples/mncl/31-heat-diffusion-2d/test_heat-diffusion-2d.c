/* test_heat-diffusion-2d.c -- 31-heat-diffusion-2d: get_eoshift_boundary による 2 次元の熱拡散（double、32 x 128、4 回の更新）
 *
 *   u'[j] = (L + R + D + T) * 0.25    （4096 点すべて。j = y * 32 + x）
 *   L、R、D、T は左右上下の隣の値。格子の外側の隣（x = 0 の L、x = 31 の R、y = 0 の D、y = 127 の T）は、温度 B = 1.0
 *
 * 期待値は、vsmlink の directives-spec.md の @get_eoshift の定義と、端の指定（:reg <b のレジスタ>）から作る（kernel の出力を見て決めない）:
 *   PE の番号 j の PE は、dir が - なら j - 2^N、+ なら j + 2^N の PE の値を受け取る。
 *   ただし区切り（2^M 個。位置 j の区切りの番号は j / 2^M）をまたぐ受け取りはしない。その PE（端の PE）は B を受け取る。
 * 加算の順は kernel と同じ（((L + R) + D) + T）で、最後に 0.25 を掛ける。結果はビット単位で一致するはずである。
 *
 * PDM の配置（heat-diffusion-2d.param）: u: PDM[0..4095]（slot 8）、結果: PDM[4096..8191]（slot 16）、作業領域 nbuf0: PDM[8192..]
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mnc2.h"

#define NX          32
#define NY          128
#define N           (NX * NY)
#define BUF_BYTES   (N * sizeof(double))
#define STEPS       4
#define B           1.0
#define OFFSET_U    (0ULL * 8)
#define OFFSET_OUT  (4096ULL * 8)
#define SEND_WAIT_TAG 0x10
#define RECV_TAG      0x1e

/* @get_eoshift の定義（仕様）と、端の PE が B を受け取る端の指定 */
static void eoshift_boundary_ref(const double *src, int dir, int n, int m, double b, double *out)
{
    for (int j = 0; j < N; j++) {
        long partner = (long)j + (long)dir * (1L << n);
        int inside = (partner >= 0 && partner < N && (partner >> m) == ((long)j >> m));
        out[j] = inside ? src[partner] : b;
    }
}

int main(void)
{
    printf("=== 31-heat-diffusion-2d: Jacobi %d steps on %d x %d (get_eoshift_boundary, outside = %.1f, double) ===\n\n",
           STEPS, NX, NY, B);

    mnc2_device_t dev = mnc2_open(0);
    if (!dev) { fprintf(stderr, "FAIL: mnc2_open returned NULL\n"); return 1; }
    printf("31-heat-diffusion-2d backend: %s\n", mnc2_get_backend_name(dev));

    int rc = 0;
    void *sendbuf = mnc2_alloc_host_buffer(dev, BUF_BYTES);
    void *recvbuf = mnc2_alloc_host_buffer(dev, BUF_BYTES);
    if (!sendbuf || !recvbuf) { fprintf(stderr, "FAIL: mnc2_alloc_host_buffer\n"); rc = 1; goto cleanup; }

    static double u[N], next[N], l[N], r[N], d[N], t[N];
    double *in = (double *)sendbuf;
    for (int i = 0; i < N; i++) {
        in[i] = (double)((i * 37) % 256) / 16.0;   /* 2 進で正確に表せる値。隣の取り違えで値が変わる */
        u[i] = in[i];
    }

    /* 期待値（仕様の定義から） */
    for (int s = 0; s < STEPS; s++) {
        eoshift_boundary_ref(u, -1, 0, 5,  B, l);
        eoshift_boundary_ref(u,  1, 0, 5,  B, r);
        eoshift_boundary_ref(u, -1, 5, 12, B, d);
        eoshift_boundary_ref(u,  1, 5, 12, B, t);
        for (int j = 0; j < N; j++) next[j] = (l[j] + r[j] + d[j] + t[j]) * 0.25;
        memcpy(u, next, sizeof(u));
    }

    if (mnc2_send(dev, sendbuf, OFFSET_U, BUF_BYTES, SEND_WAIT_TAG) != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: mnc2_send(u)\n"); rc = 1; goto cleanup;
    }

    mnc2_kernel_t kernel = mnc2_load_kernel(dev, "_build/heat-diffusion-2d.idma.dat");
    if (!kernel) { fprintf(stderr, "FAIL: mnc2_load_kernel\n"); rc = 1; goto cleanup; }
    int krc = mnc2_exec_kernel(kernel);
    mnc2_free_kernel(kernel);
    if (krc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: mnc2_exec_kernel returned %d\n", krc); rc = 1; goto cleanup; }

    memset(recvbuf, 0, BUF_BYTES);
    if (mnc2_recv(dev, recvbuf, OFFSET_OUT, BUF_BYTES, RECV_TAG) != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: mnc2_recv\n"); rc = 1; goto cleanup;
    }

    const double *rp = (const double *)recvbuf;
    int errors = 0, edge_errors = 0;
    for (int j = 0; j < N; j++) {
        if (memcmp(&rp[j], &u[j], sizeof(double)) != 0) {
            int x = j % NX, y = j / NX;
            int edge = (x == 0 || x == NX - 1 || y == 0 || y == NY - 1);
            if (errors < 5)
                fprintf(stderr, "  MISMATCH (x=%d, y=%d)%s: got=%.17g expected=%.17g\n", x, y, edge ? " [外周]" : "", rp[j], u[j]);
            errors++;
            if (edge) edge_errors++;
        }
    }
    printf("  全 %d 点（ビット単位。外周を含む）: %d/%d match。うち外周の不一致 %d（角 (0,0) = %.17g）\n",
           N, N - errors, N, edge_errors, rp[0]);

    if (errors > 0) {
        fprintf(stderr, "FAIL: %d mismatches\n", errors); rc = 1;
    } else {
        printf("\nPASS %d/%d\n", N, N);
    }

cleanup:
    if (sendbuf) mnc2_free_host_buffer(dev, sendbuf, BUF_BYTES);
    if (recvbuf) mnc2_free_host_buffer(dev, recvbuf, BUF_BYTES);
    mnc2_close(dev);
    return rc ? 1 : 0;
}
