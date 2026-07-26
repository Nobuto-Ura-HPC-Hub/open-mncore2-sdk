/* test_broadcast_reduce.c — @identify + @broadcast + @reduce :liadd の E2E 検証
 *
 * Usage: test_broadcast_reduce [--view] <in-data.bin> <out-data.bin>
 *
 *   <in-data.bin>   binary 4096 × u64 (32 KB)
 *                   index 12..15 が 4 つの定数 C0/C1/C2/C3 (sub_pe_id 0..3 用)
 *                   残り index は HW 内部で読まれるが上書きされて消える (どうせ届かない)
 *
 *   <out-data.bin>  binary 4 × u64 (32 byte)
 *                   sub_pe_id 0..3 別の合計値
 *                   sum_p = sum_{k=0..1023} (4k+p + C_p) = 2095104 + 1024*(p + C_p)
 *
 *   --view          stdout に人間可読形式で表示 + 期待値検証 (PASS/FAIL)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "mnc2.h"

#define ELEM_COUNT       4096
#define ELEM_BYTES       (ELEM_COUNT * sizeof(uint64_t))

#define IDENTIFY_OFFSET  0
#define BROADCAST_OFFSET (4096ULL * 8)
#define REDUCE_OFFSET    (8192ULL * 8)
#define REDUCE_COUNT     4

#define DMAID_TRIGGER    0x10
#define REDUCE_RECV_TAG  0x06

static int g_view = 0;

int main(int argc, char *argv[])
{
    const char *in_path = NULL;
    const char *out_path = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--view") == 0) {
            g_view = 1;
        } else if (!in_path) {
            in_path = argv[i];
        } else if (!out_path) {
            out_path = argv[i];
        }
    }
    if (!in_path || !out_path) {
        fprintf(stderr, "Usage: %s [--view] <in-data.bin> <out-data.bin>\n", argv[0]);
        return 1;
    }

    /* --- 入力読み込み (4096 × u64) --- */
    FILE *fp = fopen(in_path, "rb");
    if (!fp) { perror(in_path); return 1; }
    uint64_t in_buf[ELEM_COUNT];
    if (fread(in_buf, 1, ELEM_BYTES, fp) != ELEM_BYTES) {
        fprintf(stderr, "FAIL: read %s\n", in_path);
        fclose(fp); return 1;
    }
    fclose(fp);

    /* l1bmp: broadcast は全 PE 同値。 in_buf[0] の 1 値が全 PE に届く */
    uint64_t C = in_buf[0];

    /* --- mnc2 セットアップ --- */
    mnc2_device_t dev = mnc2_open(0);
    if (!dev) { fprintf(stderr, "FAIL: mnc2_open\n"); return 1; }

    /* 1 回の DMA send で PDM[0..8191] を送るため、 ids + broadcast を連結 */
    void *send_buf = mnc2_alloc_host_buffer(dev, 2 * ELEM_BYTES);
    void *rbuf     = mnc2_alloc_host_buffer(dev, REDUCE_COUNT * sizeof(uint64_t));
    if (!send_buf || !rbuf) {
        fprintf(stderr, "FAIL: alloc\n"); mnc2_close(dev); return 1;
    }

    /* 前半 4096 u64: PE ID テーブル [0..4095] (@identify 用) */
    uint64_t *sb = (uint64_t *)send_buf;
    for (int i = 0; i < ELEM_COUNT; i++) sb[i] = (uint64_t)i;
    /* 後半 4096 u64: broadcast 入力 (@broadcast 用、 index 12..15 が 4 PE に届く) */
    memcpy(sb + ELEM_COUNT, in_buf, ELEM_BYTES);

    int rc = MNC2_SUCCESS;

    /* 1 回の send で連続領域 PDM[0..8191] を送る (DMA double-assert 回避) */
    rc = mnc2_send(dev, send_buf, IDENTIFY_OFFSET, 2 * ELEM_BYTES, DMAID_TRIGGER);
    if (rc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: send\n"); goto cleanup; }

    /* --- カーネル実行 --- */
    mnc2_kernel_t k = mnc2_load_kernel(dev, "_build/broadcast_reduce.idma.dat");
    if (!k) { fprintf(stderr, "FAIL: load kernel\n"); rc = 1; goto cleanup; }
    rc = mnc2_exec_kernel(k);
    mnc2_free_kernel(k);
    if (rc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: exec\n"); goto cleanup; }

    /* --- 結果回収 (4 × u64) --- */
    rc = mnc2_recv(dev, rbuf, REDUCE_OFFSET, REDUCE_COUNT * sizeof(uint64_t),
                   REDUCE_RECV_TAG);
    if (rc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: recv\n"); goto cleanup; }

    /* --- 出力ファイルに書き出し --- */
    FILE *ofp = fopen(out_path, "wb");
    if (!ofp) { perror(out_path); rc = 1; goto cleanup; }
    fwrite(rbuf, sizeof(uint64_t), REDUCE_COUNT, ofp);
    fclose(ofp);

    /* --- view モード: 人間可読出力 + 期待値検証 --- */
    if (g_view) {
        uint64_t *vals = (uint64_t *)rbuf;
        int errors = 0;
        for (int p = 0; p < REDUCE_COUNT; p++) {
            /* 全 PE 同値 C なので sum_p = 2095104 + 1024*p + 1024*C
               (2095104 + 1024*p = sub_pe_id p の PE ID 合計、 1024*C = C を 1024 PE 分) */
            uint64_t expected = 2095104ULL + 1024ULL * (uint64_t)p + 1024ULL * C;
            const char *mark = (vals[p] == expected) ? "OK" : "MISMATCH";
            if (vals[p] != expected) errors++;
            printf("sub_pe_id %d (C=%" PRIu64 "): sum=%" PRIu64
                   " expected=%" PRIu64 " %s\n",
                   p, C, vals[p], expected, mark);
        }
        if (errors) {
            fprintf(stderr, "test_broadcast_reduce FAIL (%d errors)\n", errors);
            rc = 1;
        } else {
            printf("test_broadcast_reduce PASS\n");
        }
    }

cleanup:
    if (send_buf) mnc2_free_host_buffer(dev, send_buf, 2 * ELEM_BYTES);
    if (rbuf)     mnc2_free_host_buffer(dev, rbuf,     REDUCE_COUNT * sizeof(uint64_t));
    mnc2_close(dev);
    return (rc == MNC2_SUCCESS) ? 0 : 1;
}
