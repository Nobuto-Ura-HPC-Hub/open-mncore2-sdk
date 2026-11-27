/* fft_host.c — FFT（複素 f64）の emu:lib / 実機の実行
 *
 * Usage: fft_host <kernel.idma.dat> <m> <input.bin> <tw.bin> <out.bin>
 *
 *   input.bin : N = 2^m 点の複素数（re, im の順の f64 を N 組。gen_input.py の出力）
 *   tw.bin    : w の表（j = 1..m-1 の順に wr の 4096 個、wi の 4096 個。gen_input.py の出力）
 *   out.bin   : kernel の出力 N 点（PE の番号の順、re, im の順）を書く。期待値との比較は check.py が行う
 *
 *   PE の番号 4096 個のうち、N 個だけを使う。残りの PE の入力は 0。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "mnc2.h"

#define PES          4096
#define COMP_BYTES   (PES * sizeof(uint64_t))
#define ADDR_IDS     0ULL
#define ADDR_SRC     4096ULL
#define ADDR_TW0     (ADDR_SRC + 2ULL * PES)
#define DMAID_IDS    0x10
#define DMAID_SRC    0x11
#define DMAID_TW(j)  (0x11 + (j))
#define WD_RECV      0x1e

static void *read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "FAIL: open %s\n", path); return NULL; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    void *p = malloc((size_t)n);
    if (!p || fread(p, 1, (size_t)n, f) != (size_t)n) { fprintf(stderr, "FAIL: read %s\n", path); fclose(f); free(p); return NULL; }
    fclose(f);
    *size = (size_t)n;
    return p;
}

int main(int argc, char *argv[])
{
    if (argc != 6) { fprintf(stderr, "usage: %s <kernel> <m> <input.bin> <tw.bin> <out.bin>\n", argv[0]); return 2; }
    const char *kernel = argv[1];
    int m = atoi(argv[2]);
    if (m < 2 || m > 12) { fprintf(stderr, "m は 2〜12\n"); return 2; }
    int N = 1 << m;

    size_t in_size = 0, tw_size = 0;
    double *in = read_file(argv[3], &in_size);
    double *tw = read_file(argv[4], &tw_size);
    if (!in || !tw) return 1;
    if (in_size != (size_t)N * 2 * sizeof(double) || tw_size != (size_t)(m - 1) * 2 * PES * sizeof(double)) {
        fprintf(stderr, "FAIL: ファイルの大きさが m=%d と合わない（input %zu、tw %zu）\n", m, in_size, tw_size);
        return 1;
    }

    mnc2_device_t dev = mnc2_open(0);
    if (!dev) { fprintf(stderr, "FAIL: mnc2_open\n"); return 1; }

    void *ids_buf = mnc2_alloc_host_buffer(dev, COMP_BYTES);
    void *src_buf = mnc2_alloc_host_buffer(dev, 2 * COMP_BYTES);
    void *out_buf = mnc2_alloc_host_buffer(dev, 2 * COMP_BYTES);
    void *tw_buf  = mnc2_alloc_host_buffer(dev, 2 * COMP_BYTES);
    if (!ids_buf || !src_buf || !out_buf || !tw_buf) { fprintf(stderr, "FAIL: alloc\n"); mnc2_close(dev); return 1; }

    int rc = MNC2_SUCCESS;
    uint64_t *ids = ids_buf;
    double *src = src_buf, *out = out_buf;
    for (int j = 0; j < PES; j++) ids[j] = (uint64_t)j;
    memset(src_buf, 0, 2 * COMP_BYTES);
    for (int i = 0; i < N; i++) { src[i] = in[2 * i]; src[PES + i] = in[2 * i + 1]; }

    rc = mnc2_send(dev, ids_buf, ADDR_IDS * 8, COMP_BYTES, DMAID_IDS);
    if (rc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: send ids\n"); goto cleanup; }
    rc = mnc2_send(dev, src_buf, ADDR_SRC * 8, 2 * COMP_BYTES, DMAID_SRC);
    if (rc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: send src\n"); goto cleanup; }
    for (int j = 1; j < m; j++) {
        memcpy(tw_buf, tw + (size_t)(j - 1) * 2 * PES, 2 * COMP_BYTES);
        rc = mnc2_send(dev, tw_buf, (ADDR_TW0 + (uint64_t)(j - 1) * 2 * PES) * 8, 2 * COMP_BYTES, DMAID_TW(j));
        if (rc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: send tw %d\n", j); goto cleanup; }
    }

    mnc2_kernel_t k = mnc2_load_kernel(dev, kernel);
    if (!k) { fprintf(stderr, "FAIL: load kernel: %s\n", kernel); rc = 1; goto cleanup; }
    rc = mnc2_exec_kernel(k);
    mnc2_free_kernel(k);
    if (rc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: exec\n"); goto cleanup; }

    memset(out_buf, 0, 2 * COMP_BYTES);
    uint64_t addr_out = ADDR_TW0 + (uint64_t)(m - 1) * 2 * PES;
    rc = mnc2_recv(dev, out_buf, addr_out * 8, 2 * COMP_BYTES, WD_RECV);
    if (rc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: recv\n"); goto cleanup; }

    FILE *fo = fopen(argv[5], "wb");
    if (!fo) { fprintf(stderr, "FAIL: open %s\n", argv[5]); rc = 1; goto cleanup; }
    for (int i = 0; i < N; i++) {
        double c[2] = { out[i], out[PES + i] };
        fwrite(c, sizeof c, 1, fo);
    }
    fclose(fo);
    printf("fft_host: m=%d、N=%d 点の出力を %s に書いた\n", m, N, argv[5]);

cleanup:
    mnc2_free_host_buffer(dev, ids_buf, COMP_BYTES);
    mnc2_free_host_buffer(dev, src_buf, 2 * COMP_BYTES);
    mnc2_free_host_buffer(dev, out_buf, 2 * COMP_BYTES);
    mnc2_free_host_buffer(dev, tw_buf, 2 * COMP_BYTES);
    mnc2_reset(dev);
    mnc2_close(dev);
    free(in);
    free(tw);
    return rc != MNC2_SUCCESS ? 1 : 0;
}
