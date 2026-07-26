/* test_T4.c — T4: mask safety E2E test (debug_read version)
 *
 * if(flag1) {
 *     r16=1
 *     if(flag2) { r16=5; @stencil; r18=1 } else { r16=9 }
 * } else {
 *     r16=2
 *     if(flag2) { r16=6 } else { r16=10 }
 * }
 *
 * Test runs 4 combinations of (flag1, flag2).
 *   flag1=T, flag2=T → r16=5,  r18=1
 *   flag1=T, flag2=F → r16=9,  r18=0
 *   flag1=F, flag2=T → r16=6,  r18=0
 *   flag1=F, flag2=F → r16=10, r18=0
 *
 * Verification: mnc2_debug_read で GRF0 から r16/r18 を直接読み出す。
 * @collect 不要のため wd タグ衝突なし。r16 と r18 の両方を検証可能。
 *
 * 注意: debug_read 未対応バックエンド (emu:lib 等) では SKIP になる。
 *       実機または debug_read 対応バックエンドで ALL PASS を確認すること。
 *
 * PDM layout (T4.param):
 *   slot 8:  input  [0..4095]       — stencil source
 *   slot 24: flag1  [4096..8191]    — 0=true / nonzero=false
 *   slot 32: flag2  [8192..12287]   — 0=true / nonzero=false
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include "mnc2.h"

#define ELEM_COUNT  4096
#define ELEM_BYTES  (ELEM_COUNT * sizeof(uint64_t))
#define BF_BYTES    (ELEM_COUNT * sizeof(uint64_t))
#define OFFSET_IN   0                           /* slot 8:  addr 0 */
#define OFFSET_F1   (4096ULL * 8)               /* slot 24: addr 4096 */
#define OFFSET_F2   (8192ULL * 8)               /* slot 32: addr 8192 */
#define OFFSET_BF   (12288ULL * 8)              /* bf1:     addr 12288 */
#define DMAID_TRIGGER 0x10

/* boundary flags は 17-boundary-collect で生成した collected_flags.bin を使用 */
#define BF_DATA_PATH "_build/57b42dea.bin"

/* GRF0 layout: addr N = lr(N*2)/lr(N*2+1) pair
 *   r16/r17 → GRF0 addr 8
 *   r18/r19 → GRF0 addr 9
 */
#define GRF0_ADDR_R16  8
#define GRF0_ADDR_R18  9

/* imm i"N" $r24 (even register) puts N in HIGH 32 bits → actual value = N<<32 */
#define V(n) ((uint64_t)(n) << 32)

/* MN-Core 2 topology */
#define N_PE_PER_MAB  4
#define N_MAB_PER_L1B 16
#define N_L1B_PER_SEC 8
#define N_SEC_PER_L2B 2
#define PE_PER_L1B    (N_PE_PER_MAB * N_MAB_PER_L1B)
#define PE_PER_SEC    (PE_PER_L1B * N_L1B_PER_SEC)

static mnc2_loc_t pe_to_loc(int pe_idx)
{
    int section = pe_idx / PE_PER_SEC;
    int within  = pe_idx % PE_PER_SEC;
    mnc2_loc_t loc;
    loc.chip = section / N_SEC_PER_L2B;
    loc.l2b  = section % N_SEC_PER_L2B;
    loc.l1b  = within / PE_PER_L1B;
    loc.mab  = (within % PE_PER_L1B) / N_PE_PER_MAB;
    loc.pe   = within % N_PE_PER_MAB;
    return loc;
}

static int run_test(mnc2_device_t dev, mnc2_kernel_t kernel,
                    void *sendbuf,
                    int flag1_true, int flag2_true,
                    uint64_t exp_r16, uint64_t exp_r18,
                    const char *label)
{
    int rc;
    uint64_t *sbuf = (uint64_t *)sendbuf;

    /* input: PE i gets value (i+1) */
    for (int i = 0; i < ELEM_COUNT; i++)
        sbuf[i] = (uint64_t)(i + 1);
    rc = mnc2_send(dev, sendbuf, OFFSET_IN, ELEM_BYTES, 0);
    if (rc != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL [%s]: send input: %d\n", label, rc);
        return 1;
    }

    /* flag1 */
    uint64_t f1_val = flag1_true ? 0ULL : 1ULL;
    for (int i = 0; i < ELEM_COUNT; i++)
        sbuf[i] = f1_val;
    rc = mnc2_send(dev, sendbuf, OFFSET_F1, ELEM_BYTES, 0);
    if (rc != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL [%s]: send flag1: %d\n", label, rc);
        return 1;
    }

    /* flag2 (with DMA trigger) */
    uint64_t f2_val = flag2_true ? 0ULL : 1ULL;
    for (int i = 0; i < ELEM_COUNT; i++)
        sbuf[i] = f2_val;
    rc = mnc2_send(dev, sendbuf, OFFSET_F2, ELEM_BYTES,
                   DMAID_TRIGGER);
    if (rc != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL [%s]: send flag2: %d\n", label, rc);
        return 1;
    }

    /* exec */
    rc = mnc2_exec_kernel(kernel);
    if (rc != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL [%s]: exec: %d\n", label, rc);
        return 1;
    }

    /* debug_read r16 and r18 from all PEs */
    int e16 = 0, e18 = 0;
    for (int i = 0; i < ELEM_COUNT; i++) {
        mnc2_loc_t loc = pe_to_loc(i);
        uint64_t val16, val18;

        rc = mnc2_debug_read(dev, MNC2_MEM_GRF0, &loc,
                             GRF0_ADDR_R16, 1, &val16);
        if (rc != MNC2_SUCCESS) {
            if (i == 0)
                fprintf(stderr, "FAIL [%s]: debug_read r16 PE0: %d\n", label, rc);
            return 1;
        }

        rc = mnc2_debug_read(dev, MNC2_MEM_GRF0, &loc,
                             GRF0_ADDR_R18, 1, &val18);
        if (rc != MNC2_SUCCESS) {
            if (i == 0)
                fprintf(stderr, "FAIL [%s]: debug_read r18 PE0: %d\n", label, rc);
            return 1;
        }

        if (val16 != exp_r16) {
            if (e16 < 5)
                fprintf(stderr, "  MISMATCH [%s] r16 PE%d: got=%" PRIu64
                        " exp=%" PRIu64 "\n", label, i, val16, exp_r16);
            e16++;
        }
        if (val18 != exp_r18) {
            if (e18 < 5)
                fprintf(stderr, "  MISMATCH [%s] r18 PE%d: got=%" PRIu64
                        " exp=%" PRIu64 "\n", label, i, val18, exp_r18);
            e18++;
        }
    }

    int errors = e16 + e18;
    if (errors > 0) {
        fprintf(stderr, "FAIL [%s]: r16=%d err, r18=%d err (/%d PEs)\n",
                label, e16, e18, ELEM_COUNT);
        return 1;
    }
    printf("PASS [%s]: all %d PEs correct (r16=%" PRIu64 ", r18=%" PRIu64 ")\n",
           label, ELEM_COUNT, exp_r16, exp_r18);
    return 0;
}

int main(void)
{
    printf("=== T4: @stencil in nested if-if (mask safety, debug_read) ===\n\n");

    mnc2_device_t dev = mnc2_open(0);
    if (!dev) { fprintf(stderr, "FAIL: mnc2_open\n"); return 1; }

    void *sendbuf = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    void *bfbuf   = mnc2_alloc_host_buffer(dev, BF_BYTES);
    if (!sendbuf || !bfbuf) {
        fprintf(stderr, "FAIL: alloc\n");
        mnc2_close(dev);
        return 1;
    }

    /* boundary flags を 17-boundary-collect のデータから読み込み */
    {
        FILE *fp = fopen(BF_DATA_PATH, "rb");
        if (!fp) {
            fprintf(stderr, "FAIL: cannot open %s\n"
                    "  Run: ninja -C ../17-boundary-collect build-e2e && ninja -C ../17-boundary-collect test\n",
                    BF_DATA_PATH);
            mnc2_free_host_buffer(dev, sendbuf, ELEM_BYTES);
            mnc2_free_host_buffer(dev, bfbuf, BF_BYTES);
            mnc2_close(dev);
            return 1;
        }
        size_t nr = fread(bfbuf, 1, BF_BYTES, fp);
        fclose(fp);
        if (nr != BF_BYTES) {
            fprintf(stderr, "FAIL: %s: read %zu bytes, expected %zu\n",
                    BF_DATA_PATH, nr, (size_t)BF_BYTES);
            mnc2_free_host_buffer(dev, sendbuf, ELEM_BYTES);
            mnc2_free_host_buffer(dev, bfbuf, BF_BYTES);
            mnc2_close(dev);
            return 1;
        }
    }
    {
        int rc = mnc2_send(dev, bfbuf, OFFSET_BF, BF_BYTES, 0);
        if (rc != MNC2_SUCCESS) {
            fprintf(stderr, "FAIL: send boundary_flags: %d\n", rc);
            mnc2_free_host_buffer(dev, sendbuf, ELEM_BYTES);
            mnc2_free_host_buffer(dev, bfbuf, BF_BYTES);
            mnc2_close(dev);
            return 1;
        }
    }

    mnc2_kernel_t kernel = mnc2_load_kernel(dev, "_build/T4.idma.dat");
    if (!kernel) {
        fprintf(stderr, "FAIL: load kernel\n");
        mnc2_free_host_buffer(dev, sendbuf, ELEM_BYTES);
        mnc2_close(dev);
        return 1;
    }

    /* debug_read smoke test: exec_kernel してから read して backend 対応を確認 */
    {
        int rc = mnc2_exec_kernel(kernel);
        if (rc != MNC2_SUCCESS) {
            fprintf(stderr, "FAIL: exec_kernel (smoke): %d\n", rc);
            mnc2_free_kernel(kernel);
            mnc2_free_host_buffer(dev, sendbuf, ELEM_BYTES);
            mnc2_close(dev);
            return 1;
        }
        mnc2_loc_t loc0 = MNC2_LOC_INIT;
        uint64_t test_val;
        rc = mnc2_debug_read(dev, MNC2_MEM_GRF0, &loc0, 0, 1, &test_val);
        if (rc != MNC2_SUCCESS) {
            printf("SKIP: debug_read not supported on this backend (rc=%d)\n", rc);
            printf("  Use emu:process or real hardware.\n");
            mnc2_free_kernel(kernel);
            mnc2_free_host_buffer(dev, sendbuf, ELEM_BYTES);
            mnc2_close(dev);
            return 0;
        }
    }

    int fail = 0;
    /*                                flag1 flag2  r16    r18    label */
    fail += run_test(dev, kernel, sendbuf, 1, 1,  V(5), V(1), "f1=T,f2=T");
    fail += run_test(dev, kernel, sendbuf, 1, 0,  V(9),    0, "f1=T,f2=F");
    fail += run_test(dev, kernel, sendbuf, 0, 1,  V(6),    0, "f1=F,f2=T");
    fail += run_test(dev, kernel, sendbuf, 0, 0, V(10),    0, "f1=F,f2=F");

    mnc2_free_kernel(kernel);
    mnc2_free_host_buffer(dev, sendbuf, ELEM_BYTES);
    mnc2_free_host_buffer(dev, bfbuf, BF_BYTES);
    mnc2_close(dev);

    printf("\n%s\n", fail == 0 ? "ALL PASS" : "SOME FAILED");
    return fail > 0 ? 1 : 0;
}
