/* test_reduce.c -- reduce_max sample (4096 PE → 4 partial maxes)
 *
 * 入力: 4096 個の double (= u64 x 4096 = 32768 byte 連続バイナリ)
 *   - 第 1 引数で指定したファイル
 *   - 引数なしなら標準入力
 *
 * 動作モード:
 *   --verify あり : 入力を C で reduce_max し、 device 結果と verify。 結果を stdout に表示。 exit 0=PASS / 1=FAIL
 *   --verify なし : device 結果（4 u64 = 32 byte）を stdout にバイナリ出力。 stdout は他に何も出さない。 exit 0=OK / 1=IO エラー
 *                   `xxd -g 1` で表示できる出力形式。
 *
 * エラー / 進捗ログは stderr のみ。
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mnc2.h"

#define ELEM_COUNT     4096
#define ELEM_BYTES     (ELEM_COUNT * 8)            /* u64 単位 */
#define OUTPUT_COUNT   4                            /* 4 partial maxes */
#define OUTPUT_BYTES   (OUTPUT_COUNT * 8)
#define OFFSET_A       0
#define OFFSET_C       (131072ULL * 8)
#define SEND_WAIT_TAG  0x10
#define RECV_TAG       0x1e

static int read_input(const char *path, void *buf, size_t bytes) {
    FILE *fp = (path == NULL) ? stdin : fopen(path, "rb");
    if (fp == NULL) {
        fprintf(stderr, "ERROR: cannot open input file: %s\n", path);
        return -1;
    }
    size_t n = fread(buf, 1, bytes, fp);
    if (path != NULL) fclose(fp);
    if (n != bytes) {
        fprintf(stderr, "ERROR: short read (got %zu, expected %zu)\n", n, bytes);
        return -1;
    }
    return 0;
}

static int run_verify(const double *input, const double *parts) {
    /* C 側: 全要素 max */
    double c_total = input[0];
    for (int i = 1; i < ELEM_COUNT; i++) {
        if (input[i] > c_total) c_total = input[i];
    }

    /* device 側: 4 partial maxes の max */
    double dev_total = parts[0];
    for (int i = 1; i < OUTPUT_COUNT; i++) {
        if (parts[i] > dev_total) dev_total = parts[i];
    }

    /* C 側: group ごと（interleaved mapping: PE i → group (i % OUTPUT_COUNT)）
     * 07-reduce-add と同様、 device は PE position 別の partial を返す */
    double c_groups[OUTPUT_COUNT];
    for (int g = 0; g < OUTPUT_COUNT; g++) {
        c_groups[g] = input[g];
        for (int i = g + OUTPUT_COUNT; i < ELEM_COUNT; i += OUTPUT_COUNT) {
            if (input[i] > c_groups[g]) c_groups[g] = input[i];
        }
    }

    printf("=== reduce_max verify ===\n");
    printf("device parts (per group as returned by HW):\n");
    for (int g = 0; g < OUTPUT_COUNT; g++) {
        printf("  parts[%d] = %.15g\n", g, parts[g]);
    }
    printf("C per-group max (interleaved, i %% %d == g):\n", OUTPUT_COUNT);
    for (int g = 0; g < OUTPUT_COUNT; g++) {
        printf("  group[%d] = %.15g  (%s parts[%d])\n", g, c_groups[g],
               (c_groups[g] == parts[g]) ? "==" : "!=", g);
    }
    printf("\n");
    printf("C total      = %.15g\n", c_total);
    printf("device total = %.15g\n", dev_total);

    if (c_total != dev_total) {
        printf("\nFAIL\n");
        return 1;
    }
    printf("\nPASS\n");
    return 0;
}

int main(int argc, char **argv) {
    int verify = 0;
    const char *infile = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--verify") == 0) {
            verify = 1;
        } else if (argv[i][0] != '-' && infile == NULL) {
            infile = argv[i];
        } else {
            fprintf(stderr, "Usage: %s [--verify] [INPUT_FILE]\n", argv[0]);
            return 1;
        }
    }

    mnc2_device_t dev = mnc2_open(0);
    if (dev == NULL) {
        fprintf(stderr, "FAIL: mnc2_open\n");
        return 1;
    }

    int rc = 0;
    void *sendbuf = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    void *recvbuf = mnc2_alloc_host_buffer(dev, OUTPUT_BYTES);
    if (sendbuf == NULL || recvbuf == NULL) {
        fprintf(stderr, "FAIL: mnc2_alloc_host_buffer\n");
        rc = 1;
        goto cleanup;
    }

    if (read_input(infile, sendbuf, ELEM_BYTES) != 0) {
        rc = 1;
        goto cleanup;
    }

    int mnc_rc = mnc2_send(dev, sendbuf, OFFSET_A, ELEM_BYTES, SEND_WAIT_TAG);
    if (mnc_rc != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: mnc2_send (%d)\n", mnc_rc);
        rc = 1;
        goto cleanup;
    }

    mnc2_kernel_t kernel = mnc2_load_kernel(dev, "_build/reduce.idma.dat");
    if (kernel == NULL) {
        fprintf(stderr, "FAIL: mnc2_load_kernel\n");
        rc = 1;
        goto cleanup;
    }
    mnc_rc = mnc2_exec_kernel(kernel);
    mnc2_free_kernel(kernel);
    if (mnc_rc != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: mnc2_exec_kernel (%d)\n", mnc_rc);
        rc = 1;
        goto cleanup;
    }

    memset(recvbuf, 0, OUTPUT_BYTES);
    mnc_rc = mnc2_recv(dev, recvbuf, OFFSET_C, OUTPUT_BYTES, RECV_TAG);
    if (mnc_rc != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: mnc2_recv (%d)\n", mnc_rc);
        rc = 1;
        goto cleanup;
    }

    if (verify) {
        printf("08-reduce-max backend: %s\n", mnc2_get_backend_name(dev));
        rc = run_verify((const double *)sendbuf, (const double *)recvbuf);
    } else {
        size_t n = fwrite(recvbuf, 1, OUTPUT_BYTES, stdout);
        if (n != OUTPUT_BYTES) {
            fprintf(stderr, "FAIL: short write to stdout (%zu)\n", n);
            rc = 1;
        } else {
            rc = 0;
        }
    }

cleanup:
    if (sendbuf) mnc2_free_host_buffer(dev, sendbuf, ELEM_BYTES);
    if (recvbuf) mnc2_free_host_buffer(dev, recvbuf, OUTPUT_BYTES);
    mnc2_close(dev);
    return (rc != 0) ? 1 : 0;
}
