/* ex_reduce_pe_vec.c — PE 全段縮約 v 付き (4 レジスタ同時縮約)
 *
 * v サフィックス付きの l1bmp / l1bmrdfadd を使い、
 * 4 つのレジスタ ($lr0, $lr2, $lr4, $lr6) を同時に縮約する。
 *
 * 入力: [1.0, 2.0, 3.0, 4.0] の繰り返し
 * l1bmp v: $lr0=1.0, $lr2=2.0, $lr4=3.0, $lr6=4.0
 * l1bmrdfadd v: cycle ごとに異なるレジスタを読む
 *
 * 結果: PDM に 16 u64 (4 cycle × 4 PE position)
 *   cycle 0 ($lr0=1.0): 1024.0 × 4PE
 *   cycle 1 ($lr2=2.0): 2048.0 × 4PE
 *   cycle 2 ($lr4=3.0): 3072.0 × 4PE
 *   cycle 3 ($lr6=4.0): 4096.0 × 4PE
 *   Total: 40960.0 = 4096 PEs × (1+2+3+4)
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "mnc2.h"

#define BLOCK_ELEMS 64
#define BLOCK_BYTES (BLOCK_ELEMS * sizeof(double))

int main(void)
{
    printf("=== PE 全段縮約 v 付き (reduce-pe-vec) ===\n");

    mnc2_device_t dev = mnc2_open(0);
    if (!dev) { printf("FAIL: mnc2_open failed\n"); return 1; }

    printf("backend: %s\n", mnc2_get_backend_name(dev));

    void *sbuf = mnc2_alloc_host_buffer(dev, BLOCK_BYTES);
    void *rbuf = mnc2_alloc_host_buffer(dev, BLOCK_BYTES);
    double *sb = (double *)sbuf;
    double *rb = (double *)rbuf;

    /* 入力: [1.0, 2.0, 3.0, 4.0] の繰り返し */
    for (int i = 0; i < BLOCK_ELEMS; i++) sb[i] = (double)((i % 4) + 1);

    int rc = mnc2_send(dev, sbuf, 0, BLOCK_BYTES, 2);
    if (rc != 0) { printf("FAIL: send rc=%d\n", rc); return 1; }

    mnc2_kernel_t k = mnc2_load_kernel(dev, "_build/reduce-pe-s3v.idma.dat");
    if (!k) { printf("FAIL: load_kernel\n"); return 1; }
    rc = mnc2_exec_kernel(k);
    mnc2_free_kernel(k);
    if (rc != 0) { printf("FAIL: exec rc=%d\n", rc); return 1; }

    memset(rbuf, 0, BLOCK_BYTES);
    rc = mnc2_recv(dev, rbuf, 0, BLOCK_BYTES, 6);
    if (rc != 0) { printf("FAIL: recv rc=%d\n", rc); return 1; }

    /* 検証: 4 cycle × 4 PE position = 16 values */
    double expected[4] = { 1024.0, 2048.0, 3072.0, 4096.0 };
    int errors = 0;

    printf("PDM[0..15]:\n");
    for (int cyc = 0; cyc < 4; cyc++) {
        printf("  cycle %d ($lr%d=%.0f): ", cyc, cyc * 2, (double)(cyc + 1));
        for (int pe = 0; pe < 4; pe++) {
            int idx = cyc * 4 + pe;
            printf("%.1f ", rb[idx]);
            if (fabs(rb[idx] - expected[cyc]) > 1e-9) {
                printf("MISMATCH [%d]: got=%g exp=%g\n",
                        idx, rb[idx], expected[cyc]);
                errors++;
            }
        }
        printf("\n");
    }

    double total = 0;
    for (int i = 0; i < 16; i++) total += rb[i];
    printf("Total: %.1f (4096 PEs x 10 = 40960.0)\n", total);

    mnc2_free_host_buffer(dev, sbuf, BLOCK_BYTES);
    mnc2_free_host_buffer(dev, rbuf, BLOCK_BYTES);
    mnc2_close(dev);

    printf("%s\n", errors == 0 ? "PASS" : "FAIL");
    return errors > 0 ? 1 : 0;
}
