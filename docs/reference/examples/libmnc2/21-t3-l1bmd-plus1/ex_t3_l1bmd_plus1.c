/* ex_t3_l1bmd_plus1.c — T3: L1BMD +1 方向観察テスト
 *
 * L1BMD +1 の方向を観察する。MAB[0-2] の PE[0-3] を出力して判定。
 * - output[4] == input[0] → MAB[N-1] から読み出し
 * - output[0] == input[4] → MAB[N+1] から読み出し
 * 常に PASS（観察のみ）。
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
    printf("[test] T3: L1BMD +1 direction observation\n");

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

    mnc2_kernel_t k = mnc2_load_kernel(dev, "_build/t3_l1bmd_plus1.idma.dat");
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

    /* Print MAB[0], MAB[1], MAB[2] (PE[0-3] each) */
    for (int mab = 0; mab < 3; mab++) {
        printf("  MAB[%d] PE[0-3]:", mab);
        for (int pe = 0; pe < 4; pe++)
            printf(" %" PRIu64, output[mab * 4 + pe]);
        printf("\n");
    }

    /* Direction check */
    if (output[4] == input[0]) {
        printf("  Direction: l1bmd+1 reads from MAB[N-1]\n");
    } else if (output[0] == input[4]) {
        printf("  Direction: l1bmd+1 reads from MAB[N+1]\n");
    } else {
        printf("  Direction: undetermined (output[0]=%" PRIu64
               " output[4]=%" PRIu64 ")\n", output[0], output[4]);
    }

    printf("PASS (observation only)\n");
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
