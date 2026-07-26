/* test_T4.c — T4: @stencil in nested if-if, mask safety E2E test
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
 *   flag1=T, flag2=T → r16=5
 *   flag1=T, flag2=F → r16=9
 *   flag1=F, flag2=T → r16=6
 *   flag1=F, flag2=F → r16=10
 *
 * Verification: @collect r16 to PDM, then mnc2_recv.
 * (mnc2_debug_read does not return GRF0 values on emu:lib)
 *
 * NOTE: Only r16 (path marker) is collected.
 * Two @collect in the same kernel causes wd tag conflict (double assertion).
 *
 * PDM layout (T4.param):
 *   slot 8:  input  [0..4095]       — stencil source
 *   slot 24: flag1  [4096..8191]    — 0=true / nonzero=false
 *   slot 32: flag2  [8192..12287]   — 0=true / nonzero=false
 *   slot 40: out_r16 [12288..16383] — collected r16
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "mnc2.h"

#define ELEM_COUNT  4096
#define ELEM_BYTES  (ELEM_COUNT * sizeof(uint64_t))
#define BF_BYTES    (ELEM_COUNT * sizeof(uint64_t))
#define OFFSET_IN   0                           /* slot 8:  addr 0 */
#define OFFSET_F1   (4096ULL * 8)               /* slot 24: addr 4096 */
#define OFFSET_F2   (8192ULL * 8)               /* slot 32: addr 8192 */
#define OFFSET_R16  (12288ULL * 8)              /* slot 40: addr 12288 */
#define OFFSET_BF   (16384ULL * 8)              /* bf1:     addr 16384 */
#define DMAID_TRIGGER 0x10
#define WD_RECV     0x1e  /* .param recv_wait_tag と対応 */

/* boundary flags は 17-boundary-collect で生成した collected_flags.bin を使用 */
#define BF_DATA_PATH "_build/57b42dea.bin"

/* imm i"N" $r24 (even register) puts N in HIGH 32 bits → actual value = N<<32 */
#define V(n) ((uint64_t)(n) << 32)

static int run_test(mnc2_device_t dev, mnc2_kernel_t kernel,
                    void *sendbuf, void *recvbuf,
                    int flag1_true, int flag2_true,
                    uint64_t exp_r16, const char *label)
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

    /* recv r16 (waits for @collect completion) */
    memset(recvbuf, 0, ELEM_BYTES);
    rc = mnc2_recv(dev, recvbuf, OFFSET_R16, ELEM_BYTES, WD_RECV);
    if (rc != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL [%s]: recv r16: %d\n", label, rc);
        return 1;
    }

    /* verify r16 */
    uint64_t *r16_buf = (uint64_t *)recvbuf;
    int errors = 0;
    for (int i = 0; i < ELEM_COUNT; i++) {
        if (r16_buf[i] != exp_r16) {
            if (errors < 5)
                fprintf(stderr, "  MISMATCH [%s] r16 PE%d: got=%llu expected=%llu\n",
                        label, i,
                        (unsigned long long)r16_buf[i],
                        (unsigned long long)exp_r16);
            errors++;
        }
    }

    if (errors > 0) {
        fprintf(stderr, "FAIL [%s]: %d/%d PEs mismatched\n",
                label, errors, ELEM_COUNT);
        return 1;
    }
    printf("PASS [%s]: all %d PEs correct (r16=%llu)\n",
           label, ELEM_COUNT, (unsigned long long)exp_r16);
    return 0;
}

int main(void)
{
    printf("=== T4: @stencil in nested if-if (mask safety) ===\n\n");

    mnc2_device_t dev = mnc2_open(0);
    if (!dev) { fprintf(stderr, "FAIL: mnc2_open\n"); return 1; }

    void *sendbuf = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    void *recvbuf = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    void *bfbuf   = mnc2_alloc_host_buffer(dev, BF_BYTES);
    if (!sendbuf || !recvbuf || !bfbuf) {
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
            mnc2_free_host_buffer(dev, recvbuf, ELEM_BYTES);
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
            mnc2_free_host_buffer(dev, recvbuf, ELEM_BYTES);
            mnc2_free_host_buffer(dev, bfbuf, BF_BYTES);
            mnc2_close(dev);
            return 1;
        }
    }
    int rc = mnc2_send(dev, bfbuf, OFFSET_BF, BF_BYTES, 0);
    if (rc != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: send boundary_flags: %d\n", rc);
        mnc2_free_host_buffer(dev, sendbuf, ELEM_BYTES);
        mnc2_free_host_buffer(dev, recvbuf, ELEM_BYTES);
        mnc2_free_host_buffer(dev, bfbuf, BF_BYTES);
        mnc2_close(dev);
        return 1;
    }

    mnc2_kernel_t kernel = mnc2_load_kernel(dev, "_build/T4.idma.dat");
    if (!kernel) {
        fprintf(stderr, "FAIL: load kernel\n");
        mnc2_free_host_buffer(dev, sendbuf, ELEM_BYTES);
        mnc2_free_host_buffer(dev, recvbuf, ELEM_BYTES);
        mnc2_free_host_buffer(dev, bfbuf, BF_BYTES);
        mnc2_close(dev);
        return 1;
    }

    int fail = 0;
    /*                                flag1 flag2  r16     label */
    fail += run_test(dev, kernel, sendbuf, recvbuf, 1, 1,  V(5), "f1=T,f2=T");
    fail += run_test(dev, kernel, sendbuf, recvbuf, 1, 0,  V(9), "f1=T,f2=F");
    fail += run_test(dev, kernel, sendbuf, recvbuf, 0, 1,  V(6), "f1=F,f2=T");
    fail += run_test(dev, kernel, sendbuf, recvbuf, 0, 0, V(10), "f1=F,f2=F");

    mnc2_free_kernel(kernel);
    mnc2_free_host_buffer(dev, sendbuf, ELEM_BYTES);
    mnc2_free_host_buffer(dev, recvbuf, ELEM_BYTES);
    mnc2_free_host_buffer(dev, bfbuf, BF_BYTES);
    mnc2_close(dev);

    printf("\n%s\n", fail == 0 ? "ALL PASS" : "SOME FAILED");
    return fail > 0 ? 1 : 0;
}
