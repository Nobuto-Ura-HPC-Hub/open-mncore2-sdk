/* 61-long-vsm: 長い vsm (assemble 済み idma.dat 約 40KB) を 1 回だけ実行し、
 * in (send trigger) と out (MV done_flag の recv) の PDM 待ち合わせが成立するかを見る。
 *
 * カーネルは odd-even transposition sort の偶数フェーズ (data/long.vsm)。
 * ソートの収束や結果の正しさは検証しない。長い命令列で recv が完了することだけを確認する。
 *
 * PDM レイアウト (byte 単位):
 *   offset      0 : data A
 *   offset  32768 : data B
 *   offset  65536 : swap flags
 *   offset  98304 : PE IDs (@identify 0)
 *   offset 131072 : boundary flags
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "mnc2.h"

#define ELEM_COUNT   4096
#define ELEM_BYTES   ((size_t)ELEM_COUNT * sizeof(int64_t))

#define OFFSET_A     (0ULL)
#define OFFSET_B     (4096ULL * 8)
#define OFFSET_SWAP  (8192ULL * 8)
#define OFFSET_IDS   (12288ULL * 8)
#define OFFSET_BF    (16384ULL * 8)

#define TAG_TRIGGER  0x10   /* カーネルの wait i10 (data 到着トリガー) */
#define TAG_INIT     0x00   /* wait 不要な事前転送 (IDs / boundary flags) */
#define WD_SWAP      0x1f   /* swap flags の collect */
#define WD_DATA      0x1e   /* data の collect */

#define BF_PATH      "data/bf.bin"
#define KERNEL_PATH  "_build/long.idma.dat"

int main(void)
{
    mnc2_device_t dev = mnc2_open(0);
    if (!dev) { fprintf(stderr, "FAIL: mnc2_open\n"); return 1; }

    void *ids_buf  = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    void *bf_buf   = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    void *data_buf = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    void *swap_buf = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    void *out_buf  = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    mnc2_kernel_t kernel = NULL;
    int rc = 1;

    if (!ids_buf || !bf_buf || !data_buf || !swap_buf || !out_buf) {
        fprintf(stderr, "FAIL: mnc2_alloc_host_buffer\n");
        goto fail;
    }

    /* PE IDs を @identify スロットに置く (wait 不要) */
    {
        int64_t *ids = (int64_t *)ids_buf;
        for (int i = 0; i < ELEM_COUNT; i++) ids[i] = (int64_t)i;
    }
    if (mnc2_send(dev, ids_buf, OFFSET_IDS, ELEM_BYTES, TAG_INIT) != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: send PE IDs\n"); goto fail;
    }

    /* boundary flags を置く (wait 不要) */
    {
        FILE *fp = fopen(BF_PATH, "rb");
        if (!fp) { fprintf(stderr, "FAIL: fopen %s\n", BF_PATH); goto fail; }
        size_t nr = fread(bf_buf, 1, ELEM_BYTES, fp);
        fclose(fp);
        if (nr != ELEM_BYTES) {
            fprintf(stderr, "FAIL: fread %s (%zu/%zu bytes)\n", BF_PATH, nr, ELEM_BYTES);
            goto fail;
        }
    }
    if (mnc2_send(dev, bf_buf, OFFSET_BF, ELEM_BYTES, TAG_INIT) != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: send boundary flags\n"); goto fail;
    }

    /* 入力データ (中身は不問、降順で埋める) を slot A に送りカーネルを起動 */
    {
        int64_t *data = (int64_t *)data_buf;
        for (int i = 0; i < ELEM_COUNT; i++) data[i] = (int64_t)(ELEM_COUNT - i);
    }
    if (mnc2_send(dev, data_buf, OFFSET_A, ELEM_BYTES, TAG_TRIGGER) != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: send input data\n"); goto fail;
    }

    kernel = mnc2_load_kernel(dev, KERNEL_PATH);
    if (!kernel) { fprintf(stderr, "FAIL: mnc2_load_kernel %s\n", KERNEL_PATH); goto fail; }

    if (mnc2_exec_kernel(kernel) != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: mnc2_exec_kernel\n"); goto fail;
    }

    /* out 側の MV done_flag を待つ。長い命令列でここが返ってこないのが本 example の対象症状 */
    memset(swap_buf, 0, ELEM_BYTES);
    if (mnc2_recv(dev, swap_buf, OFFSET_SWAP, ELEM_BYTES, WD_SWAP) != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: mnc2_recv swap flags (wd=0x%x)\n", WD_SWAP); goto fail;
    }

    memset(out_buf, 0, ELEM_BYTES);
    if (mnc2_recv(dev, out_buf, OFFSET_B, ELEM_BYTES, WD_DATA) != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: mnc2_recv data (wd=0x%x)\n", WD_DATA); goto fail;
    }

    printf("PASS: long-vsm executed, recv completed (%d elements)\n", ELEM_COUNT);
    rc = 0;

fail:
    if (kernel) mnc2_free_kernel(kernel);
    if (ids_buf)  mnc2_free_host_buffer(dev, ids_buf,  ELEM_BYTES);
    if (bf_buf)   mnc2_free_host_buffer(dev, bf_buf,   ELEM_BYTES);
    if (data_buf) mnc2_free_host_buffer(dev, data_buf, ELEM_BYTES);
    if (swap_buf) mnc2_free_host_buffer(dev, swap_buf, ELEM_BYTES);
    if (out_buf)  mnc2_free_host_buffer(dev, out_buf,  ELEM_BYTES);
    mnc2_close(dev);
    return rc;
}
