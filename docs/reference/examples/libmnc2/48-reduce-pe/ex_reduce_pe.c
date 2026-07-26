/* ex_reduce_pe.c — PE 全段縮約 (l1bmr → l2bmr → mvrdfadd)
 *
 * 全 4096 PE の $lr0 = 1.0 を階層的に縮約し、PDM に 4 u64 を書き出す。
 * 結果は PE position (0-3) 別の部分和: 各 1024.0。
 * ホスト側で 4 値を足せば全 PE の総和 4096.0 になる。
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "mnc2.h"

#define BLOCK_ELEMS 64
#define BLOCK_BYTES (BLOCK_ELEMS * sizeof(double))

int main(void)
{
    printf("=== PE 全段縮約 (reduce-pe) ===\n");

    mnc2_device_t dev = mnc2_open(0);
    if (!dev) { printf("FAIL: mnc2_open failed\n"); return 1; }

    printf("backend: %s\n", mnc2_get_backend_name(dev));

    void *sbuf = mnc2_alloc_host_buffer(dev, BLOCK_BYTES);
    void *rbuf = mnc2_alloc_host_buffer(dev, BLOCK_BYTES);
    double *sb = (double *)sbuf;
    double *rb = (double *)rbuf;

    /* 入力: 全要素 1.0 */
    for (int i = 0; i < BLOCK_ELEMS; i++) sb[i] = 1.0;

    int rc = mnc2_send(dev, sbuf, 0, BLOCK_BYTES, 2);
    if (rc != 0) { printf("FAIL: send rc=%d\n", rc); return 1; }

    mnc2_kernel_t k = mnc2_load_kernel(dev, "_build/reduce-pe-s3.idma.dat");
    if (!k) { printf("FAIL: load_kernel\n"); return 1; }
    rc = mnc2_exec_kernel(k);
    mnc2_free_kernel(k);
    if (rc != 0) { printf("FAIL: exec rc=%d\n", rc); return 1; }

    memset(rbuf, 0, BLOCK_BYTES);
    rc = mnc2_recv(dev, rbuf, 0, BLOCK_BYTES, 6);
    if (rc != 0) { printf("FAIL: recv rc=%d\n", rc); return 1; }

    /* 検証: PDM[0..3] = 1024.0 (16 MAB × 8 L1B × 8 L2B bank × 1.0) */
    printf("PDM[0..3]: %.1f %.1f %.1f %.1f\n", rb[0], rb[1], rb[2], rb[3]);

    int errors = 0;
    for (int i = 0; i < 4; i++) {
        if (fabs(rb[i] - 1024.0) > 1e-9) {
            printf("MISMATCH [%d]: got=%g exp=1024.0\n", i, rb[i]);
            errors++;
        }
    }

    double total = rb[0] + rb[1] + rb[2] + rb[3];
    printf("Total: %.1f (4096 PEs x 1.0 = 4096.0)\n", total);

    mnc2_free_host_buffer(dev, sbuf, BLOCK_BYTES);
    mnc2_free_host_buffer(dev, rbuf, BLOCK_BYTES);
    mnc2_close(dev);

    printf("%s\n", errors == 0 ? "PASS" : "FAIL");
    return errors > 0 ? 1 : 0;
}
