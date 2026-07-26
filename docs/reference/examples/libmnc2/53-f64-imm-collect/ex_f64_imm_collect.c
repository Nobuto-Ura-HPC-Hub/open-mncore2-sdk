/* ex_f64_imm_collect.c — f64 imm 即値設定 → collect → バイナリ保存
 *
 * カーネルが imm で GRF0 に f64 1.5 (0x3FF8000000000000) を設定し、
 * collect で PDM に書き出す。
 * ホスト側で recv し、全 PE が期待値と一致することを検証する。
 *
 * Output: f64_imm_collect.bin (4096 x 8 = 32768 bytes)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "mnc2.h"

#define ELEM_COUNT  4096
#define ELEM_BYTES  (ELEM_COUNT * sizeof(uint64_t))
#define OFFSET_OUT  0
#define RECV_TAG     0x1e
#define OUTPUT_FILE "_build/f64_imm_collect.bin"

/* f64 1.5 = 0x3FF8000000000000 */
#define EXPECTED_U64 UINT64_C(0x3FF8000000000000)

int main(void)
{
    printf("[test] f64 imm (1.5) -> collect -> verify\n");

    mnc2_device_t dev = mnc2_open(0);
    if (dev == NULL) {
        printf("FAIL: mnc2_open failed\n");
        return 1;
    }

    void* recvbuf = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    if (recvbuf == NULL) {
        printf("FAIL: alloc\n");
        mnc2_close(dev);
        return 1;
    }
    memset(recvbuf, 0, ELEM_BYTES);

    mnc2_kernel_t k = mnc2_load_kernel(dev, "_build/f64_imm_collect.idma.dat");
    if (k == NULL) {
        printf("FAIL: load_kernel\n");
        goto fail;
    }
    int rc = mnc2_exec_kernel(k);
    mnc2_free_kernel(k);
    if (rc != 0) {
        printf("FAIL: exec_kernel returned %d\n", rc);
        goto fail;
    }

    rc = mnc2_recv(dev, recvbuf, OFFSET_OUT, ELEM_BYTES, RECV_TAG);
    if (rc != 0) {
        printf("FAIL: recv returned %d\n", rc);
        goto fail;
    }

    /* 先頭数要素を表示 */
    uint64_t* rp = (uint64_t*)recvbuf;
    printf("  recv[0..3]: 0x%016" PRIx64 " 0x%016" PRIx64
           " 0x%016" PRIx64 " 0x%016" PRIx64 "\n",
           rp[0], rp[1], rp[2], rp[3]);

    /* バイナリを先に保存する。検証が失敗してもデバッグ用に残す。 */
    FILE* fp = fopen(OUTPUT_FILE, "wb");
    if (fp == NULL) {
        printf("FAIL: fopen(%s)\n", OUTPUT_FILE);
        goto fail;
    }
    size_t written = fwrite(recvbuf, 1, ELEM_BYTES, fp);
    fclose(fp);
    if (written != ELEM_BYTES) {
        printf("FAIL: fwrite wrote %zu / %zu bytes\n", written, (size_t)ELEM_BYTES);
        goto fail;
    }
    printf("  saved %zu bytes to %s\n", (size_t)ELEM_BYTES, OUTPUT_FILE);

    /* 全 PE が期待値と一致することを確認 */
    int mismatches = 0;
    for (int i = 0; i < ELEM_COUNT; i++) {
        if (rp[i] != EXPECTED_U64) {
            if (mismatches < 5)
                printf("  WARN: [%d]=0x%016" PRIx64 " != expected 0x%016" PRIx64 "\n",
                        i, rp[i], (uint64_t)EXPECTED_U64);
            mismatches++;
        }
    }
    if (mismatches > 0) {
        printf("FAIL: %d/%d elements differ from expected (f64 1.5)\n",
                mismatches, ELEM_COUNT);
        goto fail;
    }

    printf("PASS: all %d PEs == 0x%016" PRIx64 " (f64 1.5)\n",
           ELEM_COUNT, (uint64_t)EXPECTED_U64);
    mnc2_free_host_buffer(dev, recvbuf, ELEM_BYTES);
    mnc2_close(dev);
    return 0;

fail:
    mnc2_free_host_buffer(dev, recvbuf, ELEM_BYTES);
    mnc2_close(dev);
    return 1;
}
