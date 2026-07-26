/* test_l2b_crossing.c — L2B boundary crossing observation test (1D)
 *
 * cross L2B: L2BM 間に直接転送命令がないため PDM を経由する。
 * 経路: PE → L1BM → L2BM → PDM → 隣接L2BのL2BM → L1BM → PE
 *       (l1bmd → l2bm@N → mvp → mvp → l2bmb@0 → l1bmd+1 + msr×3)
 *
 * Input:  PE[i] = i+1 (uint64_t, 4096 PE)
 * Output: l2bmb@0 → l1bmd+1 + msr×3 の結果を collect して recv
 *
 * L2B セクション配置:
 *   Section 0: Group 0 L2B 0 (PE 0-511)
 *   Section 1: Group 0 L2B 1 (PE 512-1023)
 *   ...
 *   Section 7: Group 3 L2B 1 (PE 3584-4095)
 *
 * 期待結果:
 *   Section N (1-7) の L1B 0:
 *     l2bmb で source L1B 7 データを受信 → l1bmd+1 + msr×3
 *     MAB m PE j = input[(N-1)*512 + 448 + ((m+15)%16)*4 + (j+3)%4]
 *   Section N L1B 0 MAB 0 PE 0 = 前セクション最後の PE の値
 *
 * 検証:
 *   1. L2B boundary: Section 1-7 の L1B 0 (7 * 64 = 448 PE) — CRITICAL
 *   2. Section 0 internal: L1B 1-7 (l2bmb なし、通常 l1bmd+1 + msr×3)
 *
 * Usage: MNC2_BACKEND=emu:lib ./test_l2b_crossing
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "mnc2.h"

#define ELEM_COUNT  4096
#define ELEM_BYTES  (ELEM_COUNT * sizeof(uint64_t))
#define OFFSET_OUT  (131072ULL * 8)
#define DMAID_SEND  0x10
#define WD_RECV     0x1e

/* MN-Core 2 topology */
#define N_PE_PER_MAB    4
#define N_MAB_PER_L1B   16
#define N_PE_PER_L1B    (N_PE_PER_MAB * N_MAB_PER_L1B)  /* 64 */
#define N_L1B_PER_L2B   8
#define N_PE_PER_L2B    (N_PE_PER_L1B * N_L1B_PER_L2B)  /* 512 */
#define N_L2B_SECTIONS  8   /* 4 groups × 2 L2B */

int main(void)
{
    printf("=== L2B boundary crossing test ===\n");
    printf("=== PDM-mediated l2bm+mvp+l2bmb + l1bmd+1 + msr×3 ===\n\n");

    mnc2_device_t* dev = mnc2_open(0);
    if (dev == NULL) {
        fprintf(stderr, "SKIP: mnc2_open failed\n");
        return 0;
    }

    void* sbuf = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    void* rbuf = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    if (sbuf == NULL || rbuf == NULL) {
        fprintf(stderr, "FAIL: alloc\n");
        mnc2_close(dev);
        return 1;
    }

    /* Input: PE[i] = i+1 */
    uint64_t* input = (uint64_t*)sbuf;
    for (int i = 0; i < ELEM_COUNT; i++)
        input[i] = (uint64_t)(i + 1);

    int rc = mnc2_send(dev, sbuf, 0, ELEM_BYTES, DMAID_SEND);
    if (rc != 0) {
        fprintf(stderr, "FAIL: send returned %d\n", rc);
        goto fail;
    }

    mnc2_kernel_t* k = mnc2_load_kernel(dev, "_build/l2b_crossing.idma.dat");
    if (k == NULL) {
        fprintf(stderr, "FAIL: load_kernel\n");
        goto fail;
    }
    rc = mnc2_exec_kernel(k);
    mnc2_free_kernel(k);
    if (rc != 0) {
        fprintf(stderr, "FAIL: exec_kernel returned %d\n", rc);
        goto fail;
    }

    memset(rbuf, 0, ELEM_BYTES);
    rc = mnc2_recv(dev, rbuf, OFFSET_OUT, ELEM_BYTES, WD_RECV);
    if (rc != 0) {
        fprintf(stderr, "FAIL: recv returned %d\n", rc);
        goto fail;
    }

    uint64_t* output = (uint64_t*)rbuf;

    /* === Observation: L2B boundary MABs === */
    printf("--- L2B boundary: L1B 0 MAB 0 of each section ---\n");
    for (int sec = 0; sec < N_L2B_SECTIONS; sec++) {
        int base = sec * N_PE_PER_L2B;  /* L1B 0, MAB 0 */
        printf("  Section[%d] L1B0 MAB0 PE[0-3]: %" PRIu64 " %" PRIu64
               " %" PRIu64 " %" PRIu64,
               sec,
               output[base], output[base+1],
               output[base+2], output[base+3]);

        if (sec > 0) {
            /* After l2bmb + l1bmd+1 + msr×3:
             * MAB 0 PE 0 = prev section L1B 7 MAB 15 PE 3
             * MAB 0 PE 1 = prev section L1B 7 MAB 15 PE 0
             * MAB 0 PE 2 = prev section L1B 7 MAB 15 PE 1
             * MAB 0 PE 3 = prev section L1B 7 MAB 15 PE 2 */
            int prev_last = (sec - 1) * N_PE_PER_L2B
                          + (N_L1B_PER_L2B - 1) * N_PE_PER_L1B
                          + (N_MAB_PER_L1B - 1) * N_PE_PER_MAB;
            printf("  (expect: %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 ")",
                   input[prev_last + 3],   /* PE0 ← PE3 after msr×3 */
                   input[prev_last + 0],   /* PE1 ← PE0 */
                   input[prev_last + 1],   /* PE2 ← PE1 */
                   input[prev_last + 2]);  /* PE3 ← PE2 */
        } else {
            printf("  (Section 0: no left neighbor)");
        }
        printf("\n");
    }

    /* Internal MABs for reference */
    printf("\n--- Section 0 internal: L1B 1 MAB 0-3 ---\n");
    for (int mab = 0; mab <= 3; mab++) {
        int base = 1 * N_PE_PER_L1B + mab * N_PE_PER_MAB;
        /* l1bmd+1: MAB m reads MAB (m-1)%16 = MAB (m+15)%16 */
        int src_mab = (mab + N_MAB_PER_L1B - 1) % N_MAB_PER_L1B;
        int prev = 1 * N_PE_PER_L1B + src_mab * N_PE_PER_MAB;
        printf("  L1B1 MAB[%d] PE[0-3]: %" PRIu64 " %" PRIu64
               " %" PRIu64 " %" PRIu64
               "  (expect: %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 ")\n",
               mab,
               output[base], output[base+1],
               output[base+2], output[base+3],
               input[prev + 3], input[prev + 0],
               input[prev + 1], input[prev + 2]);
    }

    /* === Verification === */
    printf("\n--- Verification ---\n");
    int errors = 0;
    int boundary_pass = 0;
    int sec0_pass = 0;

    /* 1. L2B boundary crossing: L1B 0 of sections 1-7
     *    MAB m PE j = input[(prev_sec)*512 + 448 + ((m+15)%16)*4 + (j+3)%4]
     */
    printf("  [L2B boundary crossing: L1B 0 of sections 1-7]\n");
    for (int sec = 1; sec < N_L2B_SECTIONS; sec++) {
        for (int mab = 0; mab < N_MAB_PER_L1B; mab++) {
            for (int pe = 0; pe < N_PE_PER_MAB; pe++) {
                int i = sec * N_PE_PER_L2B + mab * N_PE_PER_MAB + pe;
                int src_mab = (mab + N_MAB_PER_L1B - 1) % N_MAB_PER_L1B;
                int src_pe = (pe + 3) % N_PE_PER_MAB;
                int src_i = (sec - 1) * N_PE_PER_L2B
                          + (N_L1B_PER_L2B - 1) * N_PE_PER_L1B
                          + src_mab * N_PE_PER_MAB + src_pe;
                uint64_t expected = input[src_i];

                if (output[i] != expected) {
                    if (errors < 10)
                        printf("    MISMATCH PE[%d] (Sec%d L1B0 MAB%d PE%d): "
                               "got=%" PRIu64 " expected=%" PRIu64 "\n",
                               i, sec, mab, pe, output[i], expected);
                    errors++;
                } else {
                    boundary_pass++;
                }
            }
        }
    }
    printf("    %d/%d L2B boundary PEs correct\n",
           boundary_pass, 7 * N_PE_PER_L1B);

    /* 2. Section 0 internal: L1B 1-7 (normal l1bmd+1 + msr×3, no l2bmb)
     *    MAB m PE j = input[l1b*64 + ((m+15)%16)*4 + (j+3)%4]
     */
    printf("  [Section 0 internal: L1B 1-7]\n");
    for (int l1b = 1; l1b < N_L1B_PER_L2B; l1b++) {
        for (int mab = 0; mab < N_MAB_PER_L1B; mab++) {
            for (int pe = 0; pe < N_PE_PER_MAB; pe++) {
                int i = l1b * N_PE_PER_L1B + mab * N_PE_PER_MAB + pe;
                int src_mab = (mab + N_MAB_PER_L1B - 1) % N_MAB_PER_L1B;
                int src_pe = (pe + 3) % N_PE_PER_MAB;
                int src_i = l1b * N_PE_PER_L1B
                          + src_mab * N_PE_PER_MAB + src_pe;
                uint64_t expected = input[src_i];

                if (output[i] != expected) {
                    if (errors < 20)
                        printf("    MISMATCH PE[%d] (Sec0 L1B%d MAB%d PE%d): "
                               "got=%" PRIu64 " expected=%" PRIu64 "\n",
                               i, l1b, mab, pe, output[i], expected);
                    errors++;
                } else {
                    sec0_pass++;
                }
            }
        }
    }
    printf("    %d/%d Section 0 internal PEs correct\n",
           sec0_pass, 7 * N_PE_PER_L1B);

    printf("\n");
    if (errors == 0) {
        printf("PASS: L2B crossing verified (%d boundary + %d internal)\n",
               boundary_pass, sec0_pass);
    } else {
        printf("FAIL: %d mismatches\n", errors);
    }

    mnc2_free_host_buffer(dev, sbuf, ELEM_BYTES);
    mnc2_free_host_buffer(dev, rbuf, ELEM_BYTES);
    mnc2_close(dev);
    return (errors > 0) ? 1 : 0;

fail:
    mnc2_free_host_buffer(dev, sbuf, ELEM_BYTES);
    mnc2_free_host_buffer(dev, rbuf, ELEM_BYTES);
    mnc2_close(dev);
    return 1;
}
