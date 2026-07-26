/* ex_peid_collect.c — PE ID 合成 → collect → バイナリ保存
 *
 * カーネルが $subpeid/$mabid/$l1bid/$l2bid を1つの 64bit にパックし、
 * collect で PDM に書き出す。ホスト側で recv し、バイナリファイルに保存する。
 *
 * ビットレイアウト (低位 32bit):
 *   [11:9] l2bid | [8:6] l1bid | [5:2] mabid | [1:0] subpeid
 * flat PE ID = subpeid + (mabid << 2) + (l1bid << 6) + (l2bid << 9)
 *
 * Output: peid_collect.bin (4096 x 8 = 32768 bytes)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "mnc2.h"

#define ELEM_COUNT  4096
#define ELEM_BYTES  (ELEM_COUNT * sizeof(uint64_t))
#define OFFSET_OUT  (131072ULL * 8)
#define RECV_TAG     0x1e
#define OUTPUT_FILE "peid_collect.bin"

int main(void)
{
    printf("[test] PE ID collect -> binary save\n");

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

    mnc2_kernel_t k = mnc2_load_kernel(dev, "_build/peid_collect.idma.dat");
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

    uint64_t* rp = (uint64_t*)recvbuf;

    /* 先頭 8 要素を表示 */
    printf("  recv[0..7]:");
    for (int i = 0; i < 8 && i < ELEM_COUNT; i++)
        printf(" %" PRIu64, rp[i]);
    printf("\n");

    /* 検証: 全 PE ID が 0..4095 の範囲にあり、一意であること */
    int errors = 0;
    uint8_t seen[4096];
    memset(seen, 0, sizeof(seen));

    for (int i = 0; i < ELEM_COUNT; i++) {
        uint64_t val = rp[i];
        if (val >= 4096) {
            if (errors < 5)
                printf("  RANGE ERROR [%d]: %" PRIu64 "\n", i, val);
            errors++;
            continue;
        }
        if (seen[val]) {
            if (errors < 5)
                printf("  DUPLICATE [%d]: PE ID %" PRIu64 "\n", i, val);
            errors++;
        }
        seen[val] = 1;
    }

    /* 全 ID が出現したか確認 */
    int missing = 0;
    for (int i = 0; i < 4096; i++) {
        if (!seen[i]) missing++;
    }
    if (missing > 0 && errors == 0) {
        printf("  %d PE IDs missing\n", missing);
        errors++;
    }

    if (errors > 0) {
        printf("FAIL: %d errors\n", errors);
        /* エラーがあっても先頭 16 要素をダンプ */
        printf("  dump[0..15]:");
        for (int i = 0; i < 16 && i < ELEM_COUNT; i++)
            printf(" 0x%016" PRIx64, rp[i]);
        printf("\n");
        goto fail;
    }

    /* バイナリファイルに保存 */
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

    printf("PASS: saved %zu bytes to %s (all 4096 PE IDs unique)\n",
           (size_t)ELEM_BYTES, OUTPUT_FILE);
    mnc2_free_host_buffer(dev, recvbuf, ELEM_BYTES);
    mnc2_close(dev);
    return 0;

fail:
    mnc2_free_host_buffer(dev, recvbuf, ELEM_BYTES);
    mnc2_close(dev);
    return 1;
}
