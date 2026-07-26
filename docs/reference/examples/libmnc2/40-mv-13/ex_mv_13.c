/* ex_mv_13.c — MV パターン 13: L2BM→DRAM 個別並列 (3.5.8.12) roundtrip
 *
 * DRAM → L2BM に個別並列転送後、L2BM@G.0 → DRAM@G に逆転送 (3.5.8.12)。
 * 全 4 ブロック分のデータが各グループのデータと一致することを確認する。
 *
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "mnc2.h"

#define BLOCK_ELEMS 64
#define BLOCK_BYTES (BLOCK_ELEMS * sizeof(double))
#define BUF_BYTES   (4 * BLOCK_BYTES)

static const double BLOCK_BASE[4] = {0.0, 100.0, 200.0, 300.0};

int main(void)
{
    printf("[ex] MV 13: L2BM→DRAM 個別並列 (3.5.8.12) roundtrip\n");

    mnc2_device_t dev = mnc2_open(0);
    if (!dev) { printf("FAIL: mnc2_open failed\n"); return 1; }

    void *sbuf = mnc2_alloc_host_buffer(dev, BUF_BYTES);
    void *rbuf = mnc2_alloc_host_buffer(dev, BUF_BYTES);
    if (!sbuf || !rbuf) { printf("FAIL: alloc\n"); mnc2_close(dev); return 1; }

    double *sb = (double *)sbuf;
    double *rb = (double *)rbuf;
    for (int b = 0; b < 4; b++)
        for (int i = 0; i < BLOCK_ELEMS; i++)
            sb[b * BLOCK_ELEMS + i] = BLOCK_BASE[b] + (double)(i + 1);

    int rc = mnc2_send(dev, sbuf, 0, BUF_BYTES, 2);
    if (rc) { printf("FAIL: send rc=%d\n", rc); goto fail; }

    mnc2_kernel_t k = mnc2_load_kernel(dev, "_build/mv-p13.idma.dat");
    if (!k) { printf("FAIL: load_kernel\n"); goto fail; }
    rc = mnc2_exec_kernel(k);
    mnc2_free_kernel(k);
    if (rc) { printf("FAIL: exec_kernel rc=%d\n", rc); goto fail; }

    memset(rbuf, 0, BUF_BYTES);
    rc = mnc2_recv(dev, rbuf, 0, BUF_BYTES, 6);
    if (rc) { printf("FAIL: recv rc=%d\n", rc); goto fail; }

    printf("  recv blk0[0..3]: %.1f %.1f %.1f %.1f\n", rb[0], rb[1], rb[2], rb[3]);

    int errors = 0;
    for (int b = 0; b < 4; b++) {
        for (int i = 0; i < BLOCK_ELEMS; i++) {
            double exp = BLOCK_BASE[b] + (double)(i + 1);
            if (fabs(rb[b * BLOCK_ELEMS + i] - exp) > 1e-9) {
                if (errors < 3)
                    printf("  MISMATCH blk%d[%d]: got=%g exp=%g\n",
                            b, i, rb[b * BLOCK_ELEMS + i], exp);
                errors++;
            }
        }
    }
    if (errors) { printf("FAIL: %d mismatches\n", errors); goto fail; }

    printf("PASS\n");
    mnc2_free_host_buffer(dev, sbuf, BUF_BYTES);
    mnc2_free_host_buffer(dev, rbuf, BUF_BYTES);
    mnc2_close(dev);
    return 0;
fail:
    mnc2_free_host_buffer(dev, sbuf, BUF_BYTES);
    mnc2_free_host_buffer(dev, rbuf, BUF_BYTES);
    mnc2_close(dev);
    return 1;
}
