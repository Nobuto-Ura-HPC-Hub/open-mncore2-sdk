/* test_bf_override.c — @boundary_flags 検証: golden data で bf 上書き → stencil
 *
 * argv[1] = カーネルファイル (.idma.dat)
 * argv[2] = golden バイナリ (4096 × uint64_t)
 * argv[3] = --expect-fail (任意: mismatches がある → PASS)
 *
 * golden を non-trigger で送り、input を trigger で送り、
 * stencil 結果を検証する。
 *
 * Usage: MNC2_BACKEND=emu:lib ./test_bf_override kernel.idma.dat golden_ok.bin
 *        MNC2_BACKEND=emu:lib ./test_bf_override kernel.idma.dat golden_ng.bin --expect-fail
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "mnc2.h"

#define ELEM_COUNT     4096
#define DOUBLE_BYTES   (ELEM_COUNT * sizeof(double))
#define UINT64_BYTES   (ELEM_COUNT * sizeof(uint64_t))
#define OFFSET_IN      0
#define OFFSET_OUT     (4096ULL * 8)
#define OFFSET_GOLDEN  (8192ULL * 8)
#define OFFSET_BF      (12288ULL * 8)   /* @boundary_flags distribute 用（ダミー） */
#define DMAID_TRIGGER  0x10
#define WD_RECV        0x1e

static int read_golden(const char *path, void *dst, size_t size)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) { perror(path); return -1; }
    size_t n = fread(dst, 1, size, fp);
    fclose(fp);
    if (n != size) {
        fprintf(stderr, "error: %s: expected %zu bytes, got %zu\n", path, size, n);
        return -1;
    }
    return 0;
}

int main(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s <kernel.idma.dat> <golden.bin> [--expect-fail]\n", argv[0]);
        return 1;
    }
    const char *kernel_file = argv[1];
    const char *golden_file = argv[2];
    int expect_fail = (argc > 3 && strcmp(argv[3], "--expect-fail") == 0);

    printf("[test] bf-override: kernel=%s golden=%s%s\n",
           kernel_file, golden_file, expect_fail ? " (expect-fail)" : "");

    mnc2_device_t dev = mnc2_open(0);
    if (!dev) {
        fprintf(stderr, "SKIP: mnc2_open failed\n");
        return 0;
    }

    void *golden_buf = mnc2_alloc_host_buffer(dev, UINT64_BYTES);
    void *input_buf  = mnc2_alloc_host_buffer(dev, DOUBLE_BYTES);
    void *recv_buf   = mnc2_alloc_host_buffer(dev, DOUBLE_BYTES);
    if (!golden_buf || !input_buf || !recv_buf) {
        fprintf(stderr, "FAIL: alloc\n");
        mnc2_close(dev);
        return 1;
    }

    /* @boundary_flags distribute 用ダミーデータ (non-trigger)
     * @boundary_flags の結果は golden で上書きされるので中身は不問 */
    void *bf_buf = mnc2_alloc_host_buffer(dev, UINT64_BYTES);
    if (!bf_buf) {
        fprintf(stderr, "FAIL: alloc bf\n");
        goto fail;
    }
    memset(bf_buf, 0, UINT64_BYTES);
    int rc = mnc2_send(dev, bf_buf, OFFSET_BF, UINT64_BYTES,
                       0);
    mnc2_free_host_buffer(dev, bf_buf, UINT64_BYTES);
    if (rc != 0) {
        fprintf(stderr, "FAIL: send bf dummy returned %d\n", rc);
        goto fail;
    }

    /* golden data をファイルから読み込み */
    if (read_golden(golden_file, golden_buf, UINT64_BYTES) != 0)
        goto fail;

    /* golden を先に送信 (non-trigger) */
    rc = mnc2_send(dev, golden_buf, OFFSET_GOLDEN, UINT64_BYTES,
                   0);
    if (rc != 0) {
        fprintf(stderr, "FAIL: send golden returned %d\n", rc);
        goto fail;
    }

    /* input data (trigger) */
    double *input = (double *)malloc(DOUBLE_BYTES);
    if (!input) {
        fprintf(stderr, "FAIL: malloc\n");
        goto fail;
    }
    for (int i = 0; i < ELEM_COUNT; i++)
        input[i] = (double)(i + 1);
    memcpy(input_buf, input, DOUBLE_BYTES);
    rc = mnc2_send(dev, input_buf, OFFSET_IN, DOUBLE_BYTES,
                   DMAID_TRIGGER);
    if (rc != 0) {
        fprintf(stderr, "FAIL: send input returned %d\n", rc);
        free(input);
        goto fail;
    }

    /* カーネル実行 */
    mnc2_kernel_t k = mnc2_load_kernel(dev, kernel_file);
    if (!k) {
        fprintf(stderr, "FAIL: load_kernel\n");
        free(input);
        goto fail;
    }
    rc = mnc2_exec_kernel(k);
    mnc2_free_kernel(k);
    if (rc != 0) {
        fprintf(stderr, "FAIL: exec_kernel returned %d\n", rc);
        free(input);
        goto fail;
    }

    /* recv */
    memset(recv_buf, 0, DOUBLE_BYTES);
    rc = mnc2_recv(dev, recv_buf, OFFSET_OUT, DOUBLE_BYTES, WD_RECV);
    if (rc != 0) {
        fprintf(stderr, "FAIL: recv returned %d\n", rc);
        free(input);
        goto fail;
    }

    /* 検証 */
    {
        double *rp = (double *)recv_buf;
        int errors = 0;

        printf("  [boundary] PE0=%g PE4095=%g (検証対象外)\n", rp[0], rp[ELEM_COUNT - 1]);

        for (int i = 1; i < ELEM_COUNT - 1; i++) {
            double expected = input[i - 1] + input[i] + input[i + 1];
            if (rp[i] != expected) {
                if (errors < 10)
                    fprintf(stderr, "  MISMATCH [%d]: got=%g exp=%g\n",
                            i, rp[i], expected);
                errors++;
            }
        }

        if (expect_fail) {
            if (errors > 0) {
                printf("  [internal] %d / %d FAIL (期待通り)\n", errors, ELEM_COUNT - 2);
                printf("PASS (expect-fail)\n");
            } else {
                fprintf(stderr, "  [internal] PASS — bad golden が効いていない\n");
                fprintf(stderr, "FAIL (expect-fail)\n");
                free(input);
                goto fail;
            }
        } else {
            if (errors > 0) {
                fprintf(stderr, "  [internal] %d / %d FAIL\n", errors, ELEM_COUNT - 2);
                printf("FAIL\n");
                free(input);
                goto fail;
            }
            printf("  [internal] %d / %d PASS\n", ELEM_COUNT - 2, ELEM_COUNT - 2);
            printf("PASS\n");
        }
    }

    free(input);
    mnc2_free_host_buffer(dev, golden_buf, UINT64_BYTES);
    mnc2_free_host_buffer(dev, input_buf, DOUBLE_BYTES);
    mnc2_free_host_buffer(dev, recv_buf, DOUBLE_BYTES);
    mnc2_close(dev);
    return 0;

fail:
    mnc2_free_host_buffer(dev, golden_buf, UINT64_BYTES);
    mnc2_free_host_buffer(dev, input_buf, DOUBLE_BYTES);
    mnc2_free_host_buffer(dev, recv_buf, DOUBLE_BYTES);
    mnc2_close(dev);
    return 1;
}
