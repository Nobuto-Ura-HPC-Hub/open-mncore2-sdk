/* ex_subpe.c — PDM[12..15] の 4 値を全 4096 PE の sub_pe 0/1/2/3 に配布する検証
 *
 * broadcast (mvb + l2bmb + l1bmm) で PDM を全 PE LM[0] に配る。l1bmm は 4 cycle で
 * L1BM[0..15] を読み、PE LM[0] には cycle 3 = L1BM[12..15] が届く。よって
 * PDM[12..15] = [10, 20, 30, 40] を入れると、各 MAB の sub_pe 0/1/2/3 が
 * 10/20/30/40 を受け取り、collect すると全 4096 PE LM[0] が周期 4 [10,20,30,40] に
 * なる (MAB 内 PE レベルの sub_pe 別分配の明示検証)。
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
    printf("[test] PDM[12..15]=[10,20,30,40] -> sub_pe 0/1/2/3 distribution\n");

    mnc2_device_t dev = mnc2_open(0);
    if (dev == NULL) { printf("FAIL: mnc2_open\n"); return 1; }

    void* sendbuf = mnc2_alloc_host_buffer(dev, IN_BYTES);
    void* recvbuf = mnc2_alloc_host_buffer(dev, OUT_BYTES);
    if (sendbuf == NULL || recvbuf == NULL) { printf("FAIL: alloc\n"); goto fail; }

    uint64_t* sp = (uint64_t*)sendbuf;
    memset(sendbuf, 0, IN_BYTES);
    sp[12] = 10; sp[13] = 20; sp[14] = 30; sp[15] = 40;  /* l1bmm cycle 3 が読む位置 */
    memset(recvbuf, 0, OUT_BYTES);

    int rc = mnc2_send(dev, sendbuf, 0, IN_BYTES, SEND_TAG);
    if (rc != 0) { printf("FAIL: send rc=%d\n", rc); goto fail; }

    mnc2_kernel_t k = mnc2_load_kernel(dev, "_build/subpe.idma.dat");
    if (k == NULL) { printf("FAIL: load_kernel\n"); goto fail; }
    rc = mnc2_exec_kernel(k);
    mnc2_free_kernel(k);
    if (rc != 0) { printf("FAIL: exec_kernel rc=%d\n", rc); goto fail; }

    rc = mnc2_recv(dev, recvbuf, 0, OUT_BYTES, RECV_TAG);
    if (rc != 0) { printf("FAIL: recv rc=%d\n", rc); goto fail; }

    uint64_t* rp = (uint64_t*)recvbuf;
    printf("  recv[0..7]: %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64
           " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 "\n",
           rp[0], rp[1], rp[2], rp[3], rp[4], rp[5], rp[6], rp[7]);

    /* 期待: 全 4096 PE LM[0] が周期 4 [10, 20, 30, 40] (sub_pe 0/1/2/3) */
    const uint64_t expect[4] = {10, 20, 30, 40};
    int errors = 0;
    for (int i = 0; i < OUT_COUNT; i++) {
        if (rp[i] != expect[i % 4]) {
            if (errors < 5)
                printf("  MISMATCH [%d]: got %" PRIu64 ", expected %" PRIu64 "\n",
                       i, rp[i], expect[i % 4]);
            errors++;
        }
    }
    if (errors > 0) {
        printf("FAIL: sub_pe distribution mismatch (%d errors)\n", errors);
        goto fail;
    }

    printf("PASS: 全 4096 PE が sub_pe 別に [10,20,30,40] を受信\n");
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
