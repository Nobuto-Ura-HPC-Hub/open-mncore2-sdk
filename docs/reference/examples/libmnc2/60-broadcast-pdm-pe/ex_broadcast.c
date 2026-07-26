/* ex_broadcast.c — PDM → 全 4096 PE broadcast (mvb + l2bmb + l1bmm) → collect 検証
 *
 * 動作:
 *   1. PDM[0..511] = [0, 1, 2, ..., 511] を送信 (連番、 send_wait_tag=0x10)
 *   2. exec_kernel:
 *      a. broadcast: PDM[0..511] → 全 16 MAB × 4 PE LM[0]
 *         (mvb/n512 + l2bmb + l1bmm)
 *      b. collect:   各 PE LM[0] → PDM[0..4095] (pe_idx 順)
 *   3. recv で PDM[0..4095] を回収 (recv_wait_tag=0x1e)
 *   4. 出力の先頭 16 個と「unique な値の集合」 を表示。
 *      全 PE LM[0] が周期 4 でどの 4 値の繰り返しになっているかを観察し
 *      記録する (期待値の硬直 assert はせず、 観察結果を出す)。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "mnc2.h"

#define IN_COUNT   512
#define IN_BYTES   (IN_COUNT * sizeof(uint64_t))
#define OUT_COUNT  4096
#define OUT_BYTES  (OUT_COUNT * sizeof(uint64_t))
#define SEND_TAG   0x10
#define RECV_TAG   0x1e

int main(void)
{
    printf("[test] PDM -> all 4096 PE broadcast (mvb + l2bmb + l1bmm) -> collect\n");

    mnc2_device_t dev = mnc2_open(0);
    if (dev == NULL) {
        printf("FAIL: mnc2_open\n");
        return 1;
    }

    void* sendbuf = mnc2_alloc_host_buffer(dev, IN_BYTES);
    void* recvbuf = mnc2_alloc_host_buffer(dev, OUT_BYTES);
    if (sendbuf == NULL || recvbuf == NULL) {
        printf("FAIL: alloc\n");
        goto fail;
    }

    uint64_t* sp = (uint64_t*)sendbuf;
    for (int i = 0; i < IN_COUNT; i++) sp[i] = (uint64_t)i;
    memset(recvbuf, 0, OUT_BYTES);

    int rc = mnc2_send(dev, sendbuf, 0, IN_BYTES, SEND_TAG);
    if (rc != 0) { printf("FAIL: send rc=%d\n", rc); goto fail; }

    mnc2_kernel_t k = mnc2_load_kernel(dev, "_build/broadcast.idma.dat");
    if (k == NULL) { printf("FAIL: load_kernel\n"); goto fail; }
    rc = mnc2_exec_kernel(k);
    mnc2_free_kernel(k);
    if (rc != 0) { printf("FAIL: exec_kernel rc=%d\n", rc); goto fail; }

    rc = mnc2_recv(dev, recvbuf, 0, OUT_BYTES, RECV_TAG);
    if (rc != 0) { printf("FAIL: recv rc=%d\n", rc); goto fail; }

    uint64_t* rp = (uint64_t*)recvbuf;
    printf("  recv[0..15]:");
    for (int i = 0; i < 16; i++)
        printf(" %" PRIu64, rp[i]);
    printf("\n");

    /* 周期 4 の繰り返しを期待: rp[i] が rp[i%4] と一致するかを確認 */
    int period_errors = 0;
    for (int i = 4; i < OUT_COUNT; i++) {
        if (rp[i] != rp[i % 4]) {
            if (period_errors < 5)
                printf("  PERIOD MISMATCH [%d]: rp[%d]=%" PRIu64 " expected (= rp[%d])=%" PRIu64 "\n",
                       i, i, rp[i], i % 4, rp[i % 4]);
            period_errors++;
        }
    }
    if (period_errors > 0) {
        printf("OBSERVED: 全 4096 PE が周期 4 で揃っていない (差異 %d 件)\n",
               period_errors);
        goto fail;
    }

    printf("OBSERVED: 全 4096 PE LM[0] が周期 4 [%" PRIu64 ", %" PRIu64
           ", %" PRIu64 ", %" PRIu64 "] の繰り返し\n",
           rp[0], rp[1], rp[2], rp[3]);
    printf("PASS: broadcast pattern observed\n");
    mnc2_free_host_buffer(dev, sendbuf, IN_BYTES);
    mnc2_free_host_buffer(dev, recvbuf, OUT_BYTES);
    mnc2_close(dev);
    return 0;

fail:
    if (sendbuf) mnc2_free_host_buffer(dev, sendbuf, IN_BYTES);
    if (recvbuf) mnc2_free_host_buffer(dev, recvbuf, OUT_BYTES);
    mnc2_close(dev);
    return 1;
}
