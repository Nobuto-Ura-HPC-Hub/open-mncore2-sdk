/* test_l1b_crossing.c — L1B boundary crossing observation test (1D)
 *
 * l1bmd → l2bm@N → l2bmb → l1bmd-1 + msr×3 で cross L1B して
 * データが正しく移動するかを検証する。
 *
 * l1bmd-1: MAB N は MAB (N-1)%16 のスロットを読む
 * 1D では PE 位置がずれるため msr×3 で PE3→PE0 に回転
 *
 * Input:  PE[i] = i+1 (uint64_t)
 * Output: l1bmd-1 + msr×3 の結果を collect して recv
 *
 * 期待結果:
 *   L1B 内 MAB 1-15: 前の MAB のデータ + msr×3 で PE 回転
 *     MAB N PE j → MAB (N-1) PE (j+3)%4 のデータ
 *   cross L1B: MAB 0 (L1B 1-7):
 *     前の L1B の MAB 15 のデータ + msr×3
 *     MAB 0 PE 0 → 前の L1B の MAB 15 PE 3 (= input[prev_l1b*64+63])
 *   L1B 0 MAB 0: 左隣の L1B がないため、自身の MAB 15 のデータ(ラップ)
 *
 * Usage: MNC2_BACKEND=emu:lib ./test_l1b_crossing
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
#define N_L1B           8

int main(void)
{
    printf("=== L1B boundary crossing test ===\n");
    printf("=== l1bmd+1 + msr x3 (1D stencil offset=-1) ===\n\n");

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

    mnc2_kernel_t* k = mnc2_load_kernel(dev, "_build/l1b_crossing.idma.dat");
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

    /* === Observation: L1B boundary MABs === */
    printf("--- L1B boundary MABs (MAB0 of each L1B) ---\n");
    for (int l1b = 0; l1b < N_L1B; l1b++) {
        int base = l1b * N_PE_PER_L1B;
        printf("  L1B[%d] MAB0 PE[0-3]: %" PRIu64 " %" PRIu64
               " %" PRIu64 " %" PRIu64,
               l1b,
               output[base], output[base+1],
               output[base+2], output[base+3]);

        if (l1b > 0) {
            /* After l1bmd-1 + msr×3:
             * MAB0 PE0 should have prev L1B's MAB15 PE3 value
             * MAB0 PE1 should have prev L1B's MAB15 PE0 value
             * MAB0 PE2 should have prev L1B's MAB15 PE1 value
             * MAB0 PE3 should have prev L1B's MAB15 PE2 value */
            int prev_mab15 = (l1b - 1) * N_PE_PER_L1B + 15 * N_PE_PER_MAB;
            printf("  (expect: %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 ")",
                   input[prev_mab15 + 3],  /* PE0 ← PE3 after msr×3 */
                   input[prev_mab15 + 0],  /* PE1 ← PE0 */
                   input[prev_mab15 + 1],  /* PE2 ← PE1 */
                   input[prev_mab15 + 2]); /* PE3 ← PE2 */
        } else {
            printf("  (L1B 0: wraps to own MAB15)");
        }
        printf("\n");
    }

    /* Internal MABs for reference */
    printf("\n--- Internal MABs (L1B 0, MAB 1-3) ---\n");
    for (int mab = 1; mab <= 3; mab++) {
        int base = mab * N_PE_PER_MAB;
        /* l1bmd-1 reads MAB (N-1), then msr×3 rotates */
        int prev = (mab - 1) * N_PE_PER_MAB;
        printf("  MAB[%d] PE[0-3]: %" PRIu64 " %" PRIu64
               " %" PRIu64 " %" PRIu64
               "  (expect: %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 ")\n",
               mab,
               output[base], output[base+1],
               output[base+2], output[base+3],
               input[prev + 3], input[prev + 0],
               input[prev + 1], input[prev + 2]);
    }

    /* === Verification === */
    /* === Verification ===
     *
     * This test applies l1bmd+1 + msr×3 to ALL PEs, but in a real stencil
     * only MAB 0 (boundary MAB) results are used via maskr.
     *
     * l2bmb overwrites the entire L1BM of L1B 1-7, so internal MABs
     * of L1B 1-7 contain data from the previous L1B (not useful).
     *
     * We verify:
     *   1. L1B boundary: MAB 0 of L1B 1-7 (the crossing result) — CRITICAL
     *   2. L1B 0 internal: MAB 0-15 of L1B 0 (no l2bmb, normal l1bmd+1)
     */
    printf("\n--- Verification ---\n");
    int errors = 0;
    int boundary_pass = 0;
    int l1b0_pass = 0;

    /* 1. L1B boundary crossing: MAB 0 of L1B 1-7 */
    printf("  [L1B boundary crossing: MAB 0 of L1B 1-7]\n");
    for (int l1b = 1; l1b < N_L1B; l1b++) {
        for (int subpe = 0; subpe < N_PE_PER_MAB; subpe++) {
            int i = l1b * N_PE_PER_L1B + subpe;
            /* l1bmd+1: MAB 0 reads MAB 15 from previous L1B */
            int src_subpe = (subpe + 3) % 4;  /* msr×3 */
            int src_pe = (l1b - 1) * N_PE_PER_L1B + 15 * N_PE_PER_MAB + src_subpe;
            uint64_t expected = input[src_pe];

            if (output[i] != expected) {
                printf("    MISMATCH PE[%d] (L1B%d MAB0 PE%d): "
                       "got=%" PRIu64 " expected=%" PRIu64 "\n",
                       i, l1b, subpe, output[i], expected);
                errors++;
            } else {
                boundary_pass++;
            }
        }
    }
    printf("    %d/%d L1B boundary PEs correct\n",
           boundary_pass, 7 * N_PE_PER_MAB);

    /* 2. L1B 0 internal: all MABs (no l2bmb, normal l1bmd+1 within L1B) */
    printf("  [L1B 0 internal: MAB 0-15]\n");
    for (int mab = 0; mab < N_MAB_PER_L1B; mab++) {
        for (int subpe = 0; subpe < N_PE_PER_MAB; subpe++) {
            int i = mab * N_PE_PER_MAB + subpe;
            /* l1bmd+1: MAB N reads MAB N-1 (wrap for MAB 0 → MAB 15) */
            int src_mab = (mab + N_MAB_PER_L1B - 1) % N_MAB_PER_L1B;
            int src_subpe = (subpe + 3) % 4;  /* msr×3 */
            int src_pe = src_mab * N_PE_PER_MAB + src_subpe;
            uint64_t expected = input[src_pe];

            if (output[i] != expected) {
                if (errors < 10)
                    printf("    MISMATCH PE[%d] (L1B0 MAB%d PE%d): "
                           "got=%" PRIu64 " expected=%" PRIu64 "\n",
                           i, mab, subpe, output[i], expected);
                errors++;
            } else {
                l1b0_pass++;
            }
        }
    }
    printf("    %d/%d L1B 0 internal PEs correct\n",
           l1b0_pass, N_PE_PER_L1B);

    printf("\n");
    if (errors == 0) {
        printf("PASS: L1B crossing verified (%d boundary + %d internal)\n",
               boundary_pass, l1b0_pass);
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
