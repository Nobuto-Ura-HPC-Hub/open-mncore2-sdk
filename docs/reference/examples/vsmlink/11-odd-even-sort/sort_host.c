/* sort_host.c — Odd-Even Transposition Sort E2E 検証 (emu:lib)
 *
 * Usage: ./sort_host <in-data.bin>
 *
 * in-data.bin: 4096 int64 (little-endian, 32768 bytes)
 *
 * sort_even と sort_odd を交互に起動し、swap_flags がすべて 0 になったら終了。
 * 結果は昇順になっているか検証する。
 *
 * PDM レイアウト (sort.param):
 *   slot  8 [addr     0]: data A  → byte offset 0
 *   slot 16 [addr  4096]: data B  → byte offset 32768
 *   slot 24 [addr  8192]: swap_flags → byte offset 65536
 *   @identify 0 [addr 12288]: PE IDs → byte offset 98304
 *   @boundary_flags bf1 [addr 16384]: boundary flags → byte offset 131072
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include "mnc2.h"

#define ELEM_COUNT    4096
#define ELEM_BYTES    ((size_t)ELEM_COUNT * sizeof(int64_t))

#define OFFSET_A      (0ULL * 8)      /* slot 8:  addr 0     (4096 words) */
#define OFFSET_B      (4096ULL * 8)   /* slot 16: addr 4096  (4096 words) */
#define OFFSET_SWAP   (8192ULL * 8)   /* slot 24: addr 8192  (4096 words) */
#define OFFSET_IDS    (12288ULL * 8)  /* @identify 0: addr 12288          */
#define OFFSET_BF     (16384ULL * 8)  /* @boundary_flags bf1: addr 16384  */
#define BF_DATA_PATH  "_build/57b42dea.bin"

#define DMAID_TRIGGER 0x10   /* ABI の wait i10 と対応 */
#define DMAID_INIT    0x00   /* IDs など wait が不要な転送 */
#define WD_RECV       0x1e  /* .param recv_wait_tag と対応 */

/* ソート完了判定: swap_flags がすべて 0 か */
static int all_zero(const int64_t *flags, int n)
{
    for (int i = 0; i < n; i++)
        if (flags[i] != 0) return 0;
    return 1;
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <in-data.bin>\n", argv[0]);
        return 1;
    }

    printf("=== odd-even-sort: 4096 PE Odd-Even Transposition Sort ===\n");
    printf("  in: %s\n\n", argv[1]);

    mnc2_device_t dev = mnc2_open(0);
    if (!dev) { fprintf(stderr, "FAIL: mnc2_open\n"); return 1; }

    void *ids_buf   = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    void *data_buf  = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    void *swap_buf  = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    void *out_buf   = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    if (!ids_buf || !data_buf || !swap_buf || !out_buf) {
        fprintf(stderr, "FAIL: alloc\n"); mnc2_close(dev); return 1;
    }

    int rc = MNC2_SUCCESS;

    /* --- PE ID を PDM @identify スロットに送信 (一度だけ) --- */
    int64_t *ids = (int64_t *)ids_buf;
    for (int i = 0; i < ELEM_COUNT; i++) ids[i] = (int64_t)i;
    /* IDs は DMAID_INIT=0x00 で送る。カーネルは wait i10 (0x10) を使うため
     * 0x10 で IDs と data を両方送ると double-assert エラーになる */
    printf("[init] send PE IDs to @identify slot (offset=%llu, dmaid=0x00)\n",
           (unsigned long long)OFFSET_IDS);
    rc = mnc2_send(dev, ids_buf, OFFSET_IDS, ELEM_BYTES, DMAID_INIT);
    if (rc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: send IDs\n"); goto cleanup; }

    /* --- boundary flags を PDM に送信 --- */
    {
        FILE *bfp = fopen(BF_DATA_PATH, "rb");
        if (!bfp) { fprintf(stderr, "FAIL: open %s\n", BF_DATA_PATH); goto cleanup; }
        size_t nr = fread(data_buf, 1, ELEM_BYTES, bfp);
        fclose(bfp);
        if (nr != ELEM_BYTES) {
            fprintf(stderr, "FAIL: read boundary flags (%zu bytes)\n", nr);
            goto cleanup;
        }
        printf("[init] send boundary flags (offset=%llu, dmaid=0x00)\n",
               (unsigned long long)OFFSET_BF);
        rc = mnc2_send(dev, data_buf, OFFSET_BF, ELEM_BYTES, DMAID_INIT);
        if (rc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: send BF\n"); goto cleanup; }
    }

    /* --- 入力データ読み込み --- */
    {
        FILE *dfp = fopen(argv[1], "rb");
        if (!dfp) { fprintf(stderr, "FAIL: open %s\n", argv[1]); goto cleanup; }
        size_t nr = fread(data_buf, 1, ELEM_BYTES, dfp);
        fclose(dfp);
        if (nr != ELEM_BYTES) {
            fprintf(stderr, "FAIL: read input (%zu bytes, expected %zu)\n", nr, ELEM_BYTES);
            goto cleanup;
        }
    }
    printf("[init] send input to slot A (offset=%llu)\n",
           (unsigned long long)OFFSET_A);
    rc = mnc2_send(dev, data_buf, OFFSET_A, ELEM_BYTES, DMAID_TRIGGER);
    if (rc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: send data\n"); goto cleanup; }

    /* --- カーネルロード --- */
    mnc2_kernel_t k_even = mnc2_load_kernel(dev, "_build/sort_even.idma.dat");
    mnc2_kernel_t *k_odd  = mnc2_load_kernel(dev, "_build/sort_odd.idma.dat");
    if (!k_even || !k_odd) {
        fprintf(stderr, "FAIL: load kernel\n"); rc = 1; goto cleanup_kernels;
    }

    /* --- ソートループ ---
     * result_in_b: 最後に書き込まれたスロット
     *   sort_even: slot A → slot B  (result_in_b = 1)
     *   sort_odd:  slot B → slot A  (result_in_b = 0)
     */
    int result_in_b = 0;
    int pass = 0;
    const int MAX_PASS = ELEM_COUNT; /* worst case: N passes */

    while (pass < MAX_PASS) {
        /* --- sort_even: slot A → slot B --- */
        /* 初回は上の mnc2_send で slot A を DMAID_TRIGGER 済み。
         * 2 回目以降: sort_odd が slot A に書いた結果を recv → send で再トリガー */
        if (pass > 0) {
            rc = mnc2_recv(dev, data_buf, OFFSET_A, ELEM_BYTES, WD_RECV);
            if (rc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: recv A for re-trigger\n"); goto cleanup_kernels; }
            rc = mnc2_send(dev, data_buf, OFFSET_A, ELEM_BYTES, DMAID_TRIGGER);
            if (rc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: re-send A\n"); goto cleanup_kernels; }
        }
        rc = mnc2_exec_kernel(k_even);
        if (rc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: exec sort_even\n"); goto cleanup_kernels; }
        result_in_b = 1;
        pass++;

        memset(swap_buf, 0, ELEM_BYTES);
        rc = mnc2_recv(dev, swap_buf, OFFSET_SWAP, ELEM_BYTES, 0x1f); /* swap: 2nd @collect */
        if (rc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: recv swap_flags\n"); goto cleanup_kernels; }

        if (all_zero((int64_t *)swap_buf, ELEM_COUNT)) {
            printf("[sort] done after even pass %d (result in slot B)\n", pass);
            break;
        }

        /* --- sort_odd: slot B → slot A --- */
        /* sort_even が slot B に書いた結果を recv → send で wait i10 をトリガー */
        rc = mnc2_recv(dev, data_buf, OFFSET_B, ELEM_BYTES, WD_RECV);
        if (rc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: recv B for trigger\n"); goto cleanup_kernels; }
        rc = mnc2_send(dev, data_buf, OFFSET_B, ELEM_BYTES, DMAID_TRIGGER);
        if (rc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: send B\n"); goto cleanup_kernels; }
        rc = mnc2_exec_kernel(k_odd);
        if (rc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: exec sort_odd\n"); goto cleanup_kernels; }
        result_in_b = 0;
        pass++;

        memset(swap_buf, 0, ELEM_BYTES);
        rc = mnc2_recv(dev, swap_buf, OFFSET_SWAP, ELEM_BYTES, 0x1f); /* swap: 2nd @collect */
        if (rc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: recv swap_flags\n"); goto cleanup_kernels; }

        if (all_zero((int64_t *)swap_buf, ELEM_COUNT)) {
            printf("[sort] done after odd pass %d (result in slot A)\n", pass);
            break;
        }
    }

    if (pass >= MAX_PASS)
        printf("[sort] WARNING: reached MAX_PASS=%d without convergence\n", MAX_PASS);

    /* --- 結果を collect --- */
    uint64_t result_offset = result_in_b ? OFFSET_B : OFFSET_A;
    printf("[sort] collecting result from slot %s (offset=%llu)\n",
           result_in_b ? "B" : "A", (unsigned long long)result_offset);
    memset(out_buf, 0, ELEM_BYTES);
    rc = mnc2_recv(dev, out_buf, result_offset, ELEM_BYTES, WD_RECV);
    if (rc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: recv result\n"); goto cleanup_kernels; }

    /* --- 検証: 昇順チェック --- */
    {
        int64_t *out = (int64_t *)out_buf;
        int errors = 0;
        for (int i = 1; i < ELEM_COUNT; i++) {
            if (out[i] < out[i-1]) {
                if (errors < 10)
                    fprintf(stderr, "  NOT SORTED: [%d]=%" PRId64 " > [%d]=%" PRId64 "\n",
                            i-1, out[i-1], i, out[i]);
                errors++;
            }
        }
        if (errors) {
            fprintf(stderr, "FAIL: %d ordering violations\n", errors);
            rc = 1;
        } else {
            printf("PASS: %d elements sorted correctly in %d passes\n", ELEM_COUNT, pass);
            rc = 0;
        }
    }

cleanup_kernels:
    if (k_even) mnc2_free_kernel(k_even);
    if (k_odd)  mnc2_free_kernel(k_odd);
cleanup:
    mnc2_free_host_buffer(dev, ids_buf,  ELEM_BYTES);
    mnc2_free_host_buffer(dev, data_buf, ELEM_BYTES);
    mnc2_free_host_buffer(dev, swap_buf, ELEM_BYTES);
    mnc2_free_host_buffer(dev, out_buf,  ELEM_BYTES);
    mnc2_close(dev);
    return (rc != MNC2_SUCCESS) ? 1 : rc;
}
