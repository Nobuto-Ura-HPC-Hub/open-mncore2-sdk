/* sort.c — 24-odd-even-sort-reduce
 *
 * Odd-Even Transposition Sort on 4096 PEs, @reduce 版。
 *
 * 11-odd-even-sort と違い、各 pass 後の swap_count を on-device で
 * @reduce :liadd により 4 つの u64 に縮約し、ホストは 4 ワード (32 byte)
 * だけ recv して合計を取る。data buffer の round-trip も無し。
 *
 * フロー:
 *   1. send input → exec init (PE ID 計算 + データ配布、データは LM0 に常駐)
 *   2. ループ:
 *        exec exec_even
 *        send boundary_flags (wait i10 トリガーのみ、データは PDM に既存)
 *        exec exec_odd
 *        exec reduce (swap_count を 4 u64 に集約)
 *        recv 4 u64 → 合計 == 0 で収束
 *   3. mnc2_debug_read で各 PE の LM0[0] を回収して結果ファイルへ
 *
 * Usage: sort <in-data.bin> <out-data.bin>
 *   in-data.bin:  4096 fp64 (入力)
 *   out-data.bin: 4096 fp64 (ソート結果)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include "mnc2.h"

#define ELEM_COUNT     4096
#define ELEM_BYTES     (ELEM_COUNT * sizeof(double))
#define MAX_PASS       ELEM_COUNT
#define DMAID_TRIGGER  0x10
#define DMAID_INIT     0x00
#define OFFSET_BF      (4096ULL * 8)
#define BF_DATA_PATH   "_build/57b42dea.bin"

/* reduce kernel が出力する swap_count: PDM[32768..] に 4 u64 (1 chip ごと) */
#define REDUCE_PDM_ADDR (32768ULL * 8)
#define REDUCE_RECV_TAG 0x06
#define REDUCE_N        4

#define N_PE_PER_MAB   4
#define N_MAB_PER_L1B  16
#define N_L1B_PER_SEC  8
#define PE_PER_L1B     (N_PE_PER_MAB * N_MAB_PER_L1B)
#define PE_PER_SEC     (PE_PER_L1B * N_L1B_PER_SEC)

static void pe_to_loc(int pe_idx, mnc2_loc_t *loc) {
    int sec = pe_idx / PE_PER_SEC;
    int w   = pe_idx % PE_PER_SEC;
    loc->chip = sec / 2;
    loc->l2b  = sec % 2;
    loc->l1b  = w / PE_PER_L1B;
    loc->mab  = (w % PE_PER_L1B) / N_PE_PER_MAB;
    loc->pe   = w % N_PE_PER_MAB;
}

static int g_view = 0;

static uint64_t recv_swap_count(mnc2_device_t dev, void *rbuf) {
    int rc = mnc2_recv(dev, rbuf, REDUCE_PDM_ADDR,
                       REDUCE_N * sizeof(uint64_t), REDUCE_RECV_TAG);
    if (rc != MNC2_SUCCESS) return UINT64_MAX;
    uint64_t *v = (uint64_t *)rbuf;
    if (g_view) {
        printf("recv_swap_count: %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 "\n",
               v[0], v[1], v[2], v[3]);
    }
    return v[0] + v[1] + v[2] + v[3];
}

static void read_data(mnc2_device_t dev, double *out) {
    for (int i = 0; i < ELEM_COUNT; i++) {
        mnc2_loc_t loc;
        pe_to_loc(i, &loc);
        uint64_t raw = 0;
        mnc2_debug_read(dev, MNC2_MEM_LM0, &loc, 0, 1, &raw);
        memcpy(&out[i], &raw, sizeof(double));
    }
}

int main(int argc, char *argv[]) {
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

    printf("=== odd-even-sort-reduce: 4096 PE, on-device convergence check ===\n");
    printf("  in:  %s\n", in_path);
    printf("  out: %s\n\n", out_path);

    /* --- input --- */
    FILE *fp = fopen(in_path, "rb");
    if (!fp) { fprintf(stderr, "FAIL: open %s\n", in_path); return 1; }

    mnc2_device_t dev = mnc2_open(0);
    if (!dev) { fprintf(stderr, "FAIL: mnc2_open\n"); fclose(fp); return 1; }

    void *sendbuf = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    void *recvbuf = mnc2_alloc_host_buffer(dev, REDUCE_N * sizeof(uint64_t));
    void *bf_buf  = mnc2_alloc_host_buffer(dev, ELEM_COUNT * sizeof(uint64_t));
    if (!sendbuf || !recvbuf || !bf_buf) {
        fprintf(stderr, "FAIL: alloc\n"); fclose(fp); mnc2_close(dev); return 1;
    }
    if (fread(sendbuf, 1, ELEM_BYTES, fp) != ELEM_BYTES) {
        fprintf(stderr, "FAIL: read %s\n", in_path); fclose(fp); goto fail;
    }
    fclose(fp);

    /* --- boundary flags 投入 (非トリガー) ---
       device の DMA は host_buffer (mnc2_alloc_host_buffer) を要求するため、
       stack array ではなく host_buffer 経由で送る。 */
    {
        FILE *bfp = fopen(BF_DATA_PATH, "rb");
        if (!bfp) { fprintf(stderr, "FAIL: open %s\n", BF_DATA_PATH); goto fail; }
        if (fread(bf_buf, sizeof(uint64_t), ELEM_COUNT, bfp) != ELEM_COUNT) {
            fprintf(stderr, "FAIL: read boundary flags\n"); fclose(bfp); goto fail;
        }
        fclose(bfp);
        int rc = mnc2_send(dev, bf_buf, OFFSET_BF, ELEM_COUNT * sizeof(uint64_t),
                           DMAID_INIT);
        if (rc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: send bf\n"); goto fail; }
    }

    /* --- カーネルロード --- */
    mnc2_kernel_t k_init   = mnc2_load_kernel(dev, "_build/init.idma.dat");
    mnc2_kernel_t k_even   = mnc2_load_kernel(dev, "_build/exec_even.idma.dat");
    mnc2_kernel_t k_odd    = mnc2_load_kernel(dev, "_build/exec_odd.idma.dat");
    mnc2_kernel_t k_reduce = mnc2_load_kernel(dev, "_build/reduce.idma.dat");
    if (!k_init || !k_even || !k_odd || !k_reduce) {
        fprintf(stderr, "FAIL: load kernels\n"); goto fail;
    }

    /* --- init: send + exec --- */
    int rc = mnc2_send(dev, sendbuf, 0, ELEM_BYTES, DMAID_TRIGGER);
    if (rc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: send input\n"); goto fail; }
    rc = mnc2_exec_kernel(k_init);
    if (rc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: exec init\n"); goto fail; }
    printf("[init] OK\n");

    /* --- ソートループ --- */
    int pass;
    for (pass = 0; pass < MAX_PASS; pass++) {
        rc = mnc2_exec_kernel(k_even);
        if (rc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: exec_even pass=%d\n", pass); goto fail; }

        /* odd: bf を 0x10 で再送（@boundary_flags の wait i10 トリガーのみ） */
        rc = mnc2_send(dev, bf_buf, OFFSET_BF, ELEM_COUNT * sizeof(uint64_t),
                       DMAID_TRIGGER);
        if (rc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: send bf trigger\n"); goto fail; }
        rc = mnc2_exec_kernel(k_odd);
        if (rc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: exec_odd pass=%d\n", pass); goto fail; }

        rc = mnc2_exec_kernel(k_reduce);
        if (rc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: exec_reduce pass=%d\n", pass); goto fail; }

        uint64_t swap_count = recv_swap_count(dev, recvbuf);
        if (swap_count == UINT64_MAX) { fprintf(stderr, "FAIL: recv reduce\n"); goto fail; }

        if (swap_count == 0) {
            printf("[sort] converged at pass %d\n", pass);
            break;
        }
    }
    if (pass >= MAX_PASS) {
        printf("[sort] WARNING: did not converge in %d passes\n", MAX_PASS);
    }

    /* --- 結果回収: debug_read で各 PE の LM0[0] を読む --- */
    double *outbuf = (double *)malloc(ELEM_BYTES);
    if (!outbuf) { fprintf(stderr, "FAIL: malloc outbuf\n"); rc = 1; goto fail; }
    read_data(dev, outbuf);

    FILE *ofp = fopen(out_path, "wb");
    if (!ofp) { fprintf(stderr, "FAIL: open %s for write\n", out_path); free(outbuf); rc = 1; goto fail; }
    fwrite(outbuf, sizeof(double), ELEM_COUNT, ofp);
    fclose(ofp);

    /* --- 検証: 昇順 --- */
    int errors = 0;
    for (int i = 1; i < ELEM_COUNT; i++) {
        if (outbuf[i] < outbuf[i-1]) {
            if (errors < 5)
                fprintf(stderr, "  NOT SORTED: [%d]=%.1f > [%d]=%.1f\n",
                        i-1, outbuf[i-1], i, outbuf[i]);
            errors++;
        }
    }
    free(outbuf);

    if (errors == 0) {
        printf("PASS: %d elements sorted in %d passes\n", ELEM_COUNT, pass);
        rc = 0;
    } else {
        printf("FAIL: %d ordering violations\n", errors);
        rc = 1;
    }

    mnc2_free_kernel(k_init);
    mnc2_free_kernel(k_even);
    mnc2_free_kernel(k_odd);
    mnc2_free_kernel(k_reduce);
    mnc2_free_host_buffer(dev, sendbuf, ELEM_BYTES);
    mnc2_free_host_buffer(dev, recvbuf, REDUCE_N * sizeof(uint64_t));
    mnc2_free_host_buffer(dev, bf_buf, ELEM_COUNT * sizeof(uint64_t));
    mnc2_close(dev);
    return rc;

fail:
    if (sendbuf) mnc2_free_host_buffer(dev, sendbuf, ELEM_BYTES);
    if (recvbuf) mnc2_free_host_buffer(dev, recvbuf, REDUCE_N * sizeof(uint64_t));
    if (bf_buf)  mnc2_free_host_buffer(dev, bf_buf,  ELEM_COUNT * sizeof(uint64_t));
    mnc2_close(dev);
    return 1;
}
