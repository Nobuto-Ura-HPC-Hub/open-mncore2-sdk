/* ex_mv_15.c — MV パターン 15: L2BM→DRAM グループ間結合縮約 (mvr4dfadd, 3.5.8.17)
 *
 * mvb で PDM@G0 → 全グループ L2BM に放送後、
 * mvr4dfadd で 4 グループ × L2B bank 0 を加算縮約 → DRAM@G0 に書き込む。
 * 出力は n/4 = 16 u64 のみ。期待値: 4 × 入力
 *
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "mnc2.h"

#define BLOCK_ELEMS   64
#define BLOCK_BYTES   (BLOCK_ELEMS * sizeof(double))
#define OUT_ELEMS     16   /* mvr4dfadd は n/4 u64 のみ出力 */
#define OUT_BYTES     (OUT_ELEMS * sizeof(double))

int main(void)
{
    printf("[ex] MV 15: L2BM→DRAM グループ間結合縮約 (mvr4dfadd, 3.5.8.17)\n");

    mnc2_device_t dev = mnc2_open(0);
    if (!dev) { printf("FAIL: mnc2_open failed\n"); return 1; }

    void *sbuf = mnc2_alloc_host_buffer(dev, BLOCK_BYTES);
    void *rbuf = mnc2_alloc_host_buffer(dev, BLOCK_BYTES);
    if (!sbuf || !rbuf) { printf("FAIL: alloc\n"); mnc2_close(dev); return 1; }

    double *sb = (double *)sbuf;
    double *rb = (double *)rbuf;
    for (int i = 0; i < BLOCK_ELEMS; i++)
        sb[i] = (double)(i + 1);

    int rc = mnc2_send(dev, sbuf, 0, BLOCK_BYTES, 2);
    if (rc) { printf("FAIL: send rc=%d\n", rc); goto fail; }

    mnc2_kernel_t k = mnc2_load_kernel(dev, "_build/mv-p15.idma.dat");
    if (!k) { printf("FAIL: load_kernel\n"); goto fail; }
    rc = mnc2_exec_kernel(k);
    mnc2_free_kernel(k);
    if (rc) { printf("FAIL: exec_kernel rc=%d\n", rc); goto fail; }

    memset(rbuf, 0, OUT_BYTES);
    rc = mnc2_recv(dev, rbuf, 0, OUT_BYTES, 6);
    if (rc) { printf("FAIL: recv rc=%d\n", rc); goto fail; }

    printf("  recv[0..3]: %.1f %.1f %.1f %.1f\n", rb[0], rb[1], rb[2], rb[3]);

    /* 4 グループ × L2B bank 0 の和 → 4×、出力は先頭 16 u64 のみ */
    int errors = 0;
    for (int i = 0; i < OUT_ELEMS; i++) {
        double exp = 4.0 * (double)(i + 1);
        if (fabs(rb[i] - exp) > 1e-9) {
            if (errors < 3)
                printf("  MISMATCH [%d]: got=%g exp=%g\n", i, rb[i], exp);
            errors++;
        }
    }
    if (errors) { printf("FAIL: %d mismatches\n", errors); goto fail; }

    printf("PASS\n");
    mnc2_free_host_buffer(dev, sbuf, BLOCK_BYTES);
    mnc2_free_host_buffer(dev, rbuf, BLOCK_BYTES);
    mnc2_close(dev);
    return 0;
fail:
    mnc2_free_host_buffer(dev, sbuf, BLOCK_BYTES);
    mnc2_free_host_buffer(dev, rbuf, BLOCK_BYTES);
    mnc2_close(dev);
    return 1;
}
