/* ex_mv_11.c — MV パターン 11: DRAM→L2BM グループ内放送 (mvb2, 3.5.8.13)
 *
 * DRAM@G0 → G0 内 2 L2B スロット全体に放送 (mvb2) 後、
 * L2BM → PDM@G0 に戻して roundtrip を確認する。
 *
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "mnc2.h"

#define BLOCK_ELEMS 64
#define BLOCK_BYTES (BLOCK_ELEMS * sizeof(double))

int main(void)
{
    printf("[ex] MV 11: DRAM→L2BM グループ内放送 (mvb2, 3.5.8.13) roundtrip\n");

    mnc2_device_t dev = mnc2_open(0);
    if (!dev) { printf("FAIL: mnc2_open failed\n"); return 1; }

    void *sbuf = mnc2_alloc_host_buffer(dev, BLOCK_BYTES);
    void *rbuf = mnc2_alloc_host_buffer(dev, BLOCK_BYTES);
    if (!sbuf || !rbuf) { printf("FAIL: alloc\n"); mnc2_close(dev); return 1; }

    double *sb = (double *)sbuf;
    double *rb = (double *)rbuf;

    /* --- poison-fill: broadcast (mvb2) の wait 不足を単独実行で捕まえるガード ---
     * 本テストの前に、broadcast の転送先 L2BM を 0xDEADBEEF... で先に汚しておく。
     * broadcast が L2BM を正しく上書きすれば下の readback は入力どおりになるが、
     * broadcast の wait が不足していると readback が完了前の L2BM (= poison) を拾い、
     * rb[i]==i+1 の一致検査が FAIL する。転送先が初期値 0 だと wait 不足を見逃す
     * (単独実行で PASS してしまう) 問題への対策で、broadcast が隣接領域を壊さず
     * 転送先だけを正しく書くことをここで担保する。
     * 効くのは L2BM 状態が exec 間で持続する emu:lib / device。emu:process は kernel 間で
     * 状態を持たないため poison は不活性 (無害。本体 roundtrip の検証はそのまま有効)。 */
    uint64_t *pb = (uint64_t *)sbuf;
    for (int i = 0; i < BLOCK_ELEMS; i++)
        pb[i] = 0xDEADBEEFDEADBEEFULL;

    int rc = mnc2_send(dev, sbuf, 0, BLOCK_BYTES, 2);
    if (rc) { printf("FAIL: poison send rc=%d\n", rc); goto fail; }

    mnc2_kernel_t kp = mnc2_load_kernel(dev, "_build/poison-fill.idma.dat");
    if (!kp) { printf("FAIL: load poison-fill\n"); goto fail; }
    rc = mnc2_exec_kernel(kp);
    mnc2_free_kernel(kp);
    if (rc) { printf("FAIL: poison exec rc=%d\n", rc); goto fail; }

    /* --- ここから既存の本テスト (sbuf を実データ i+1 で上書きして送る) --- */
    for (int i = 0; i < BLOCK_ELEMS; i++)
        sb[i] = (double)(i + 1);

    rc = mnc2_send(dev, sbuf, 0, BLOCK_BYTES, 2);
    if (rc) { printf("FAIL: send rc=%d\n", rc); goto fail; }

    mnc2_kernel_t k = mnc2_load_kernel(dev, "_build/mv-p11.idma.dat");
    if (!k) { printf("FAIL: load_kernel\n"); goto fail; }
    rc = mnc2_exec_kernel(k);
    mnc2_free_kernel(k);
    if (rc) { printf("FAIL: exec_kernel rc=%d\n", rc); goto fail; }

    memset(rbuf, 0, BLOCK_BYTES);
    rc = mnc2_recv(dev, rbuf, 0, BLOCK_BYTES, 6);
    if (rc) { printf("FAIL: recv rc=%d\n", rc); goto fail; }

    printf("  recv[0..3]: %.1f %.1f %.1f %.1f\n", rb[0], rb[1], rb[2], rb[3]);

    int errors = 0;
    for (int i = 0; i < BLOCK_ELEMS; i++) {
        double exp = (double)(i + 1);
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
