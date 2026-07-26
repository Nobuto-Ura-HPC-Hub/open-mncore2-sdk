/* ex_t8_right_neighbor.c — T8: Right Neighbor (msl + l1bmd-1 + maskr) テスト
 *
 * msl + l1bmd-1 + maskr の組み合わせ。
 * subpe 3:   l1bmd-1 = MAB[N+1].PE[3]、L1B 内 wrap (16 MAB/L1B)
 * subpe 0:   expected = input[i+3]   (msl wrap)
 * subpe 1,2: expected = input[i-1]   (msl)
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "mnc2.h"

#define ELEM_COUNT  4096
#define ELEM_BYTES  (ELEM_COUNT * sizeof(uint64_t))
#define OFFSET_OUT  (131072ULL * 8)
#define SEND_TAG  0x10
#define RECV_TAG     0x1e

int main(void)
{
    printf("[test] T8: Right Neighbor (msl + l1bmd-1 + maskr)\n");

    mnc2_device_t dev = mnc2_open(0);
    if (dev == NULL) {
        printf("FAIL: mnc2_open failed\n");
        return 1;
    }

    void* sbuf = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    void* rbuf = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    if (sbuf == NULL || rbuf == NULL) {
        printf("FAIL: alloc\n");
        mnc2_close(dev);
        return 1;
    }

    uint64_t* input  = (uint64_t*)sbuf;
    uint64_t* output = (uint64_t*)rbuf;
    for (int i = 0; i < ELEM_COUNT; i++)
        input[i] = (uint64_t)(i + 1);
    memset(output, 0, ELEM_BYTES);

    int rc = mnc2_send(dev, sbuf, 0, ELEM_BYTES, SEND_TAG);
    if (rc != 0) {
        printf("FAIL: send returned %d\n", rc);
        goto fail;
    }

    mnc2_kernel_t k = mnc2_load_kernel(dev, "_build/t8_right_neighbor.idma.dat");
    if (k == NULL) {
        printf("FAIL: load_kernel\n");
        goto fail;
    }
    rc = mnc2_exec_kernel(k);
    mnc2_free_kernel(k);
    if (rc != 0) {
        printf("FAIL: exec_kernel returned %d\n", rc);
        goto fail;
    }

    rc = mnc2_recv(dev, rbuf, OFFSET_OUT, ELEM_BYTES, RECV_TAG);
    if (rc != 0) {
        printf("FAIL: recv returned %d\n", rc);
        goto fail;
    }

    printf("  output[0..7]:");
    for (int i = 0; i < 8; i++)
        printf(" %" PRIu64, output[i]);
    printf("\n");

    int fail_count = 0;
    for (int i = 0; i < ELEM_COUNT; i++) {
        int subpe = i % 4;
        uint64_t expected;
        if (subpe == 3) {
            /* l1bmd-1: MAB[N+1].PE[3] with L1B-local wrap (16 MABs per L1B) */
            int l1b_base = (i / 64) * 64;
            int mab_in_l1b = (i / 4) % 16;
            int next_mab = (mab_in_l1b + 1) % 16;
            expected = input[l1b_base + next_mab * 4 + 3];
        } else if (subpe == 0) {
            /* msl wrap: PE[0] ← PE[3] of same MAB */
            expected = input[i + 3];
        } else {
            /* msl: PE[i] ← PE[i-1] */
            expected = input[i - 1];
        }
        if (output[i] != expected) {
            if (fail_count < 10)
                printf("  MISMATCH [%d] subpe=%d: got %" PRIu64
                        " expected %" PRIu64 "\n",
                        i, subpe, output[i], expected);
            fail_count++;
        }
    }

    if (fail_count > 0) {
        printf("FAIL: %d mismatches\n", fail_count);
        goto fail;
    }

    printf("PASS\n");
    mnc2_free_host_buffer(dev, sbuf, ELEM_BYTES);
    mnc2_free_host_buffer(dev, rbuf, ELEM_BYTES);
    mnc2_close(dev);
    return 0;

fail:
    mnc2_free_host_buffer(dev, sbuf, ELEM_BYTES);
    mnc2_free_host_buffer(dev, rbuf, ELEM_BYTES);
    mnc2_close(dev);
    return 1;
}
