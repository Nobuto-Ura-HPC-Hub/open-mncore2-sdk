/* ex_lmcycle.c — l1bmm の 4 cycle が PE LM のどの位置に書くかの切り分け
 *
 * broadcast 後に PE LM0[0] と LM0[1] を別々に collect する。
 *   PDM[0..4095]    = 全 4096 PE の LM0[0]
 *   PDM[4096..8191] = 全 4096 PE の LM0[1]
 * l1bmm が LM0[0] を上書きするなら LM0[1] は 0、LM0[0..3] に別々に書くなら
 * LM0[1] は cycle 1 の値になる。期待値は未知なので観察結果を出す。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "mnc2.h"

#define IN_COUNT   512
#define IN_BYTES   (IN_COUNT * sizeof(uint64_t))
#define OUT_COUNT  8192            /* LM0[0] 4096 + LM0[1] 4096 */
#define OUT_BYTES  (OUT_COUNT * sizeof(uint64_t))
#define SEND_TAG   0x10
#define RECV_TAG   0x26

int main(void)
{
    printf("[test] l1bmm 4-cycle write position (collect LM0[0] and LM0[1])\n");

    mnc2_device_t dev = mnc2_open(0);
    if (dev == NULL) { printf("FAIL: mnc2_open\n"); return 1; }

    void* sendbuf = mnc2_alloc_host_buffer(dev, IN_BYTES);
    void* recvbuf = mnc2_alloc_host_buffer(dev, OUT_BYTES);
    if (sendbuf == NULL || recvbuf == NULL) { printf("FAIL: alloc\n"); goto fail; }

    uint64_t* sp = (uint64_t*)sendbuf;
    for (int i = 0; i < IN_COUNT; i++) sp[i] = (uint64_t)i;  /* 連番 */
    memset(recvbuf, 0, OUT_BYTES);

    int rc = mnc2_send(dev, sendbuf, 0, IN_BYTES, SEND_TAG);
    if (rc != 0) { printf("FAIL: send rc=%d\n", rc); goto fail; }

    mnc2_kernel_t k = mnc2_load_kernel(dev, "_build/lmcycle.idma.dat");
    if (k == NULL) { printf("FAIL: load_kernel\n"); goto fail; }
    rc = mnc2_exec_kernel(k);
    mnc2_free_kernel(k);
    if (rc != 0) { printf("FAIL: exec_kernel rc=%d\n", rc); goto fail; }

    rc = mnc2_recv(dev, recvbuf, 0, OUT_BYTES, RECV_TAG);
    if (rc != 0) { printf("FAIL: recv rc=%d\n", rc); goto fail; }

    uint64_t* rp = (uint64_t*)recvbuf;
    printf("  LM0[0] recv[0..7]: %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64
           " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 "\n",
           rp[0], rp[1], rp[2], rp[3], rp[4], rp[5], rp[6], rp[7]);
    printf("  LM0[1] recv[0..7]: %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64
           " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 "\n",
           rp[4096], rp[4097], rp[4098], rp[4099], rp[4100], rp[4101], rp[4102], rp[4103]);

    /* LM0[1] が全 0 か非 0 かを判定 (l1bmm の書き込み位置の切り分け) */
    int lm1_nonzero = 0;
    for (int i = 4096; i < 8192; i++) if (rp[i] != 0) { lm1_nonzero = 1; break; }
    if (lm1_nonzero)
        printf("OBSERVED: LM0[1] は非 0 → l1bmm は LM0[0..3] に cycle 別で書く\n");
    else
        printf("OBSERVED: LM0[1] は全 0 → l1bmm は LM0[0] のみに書く (cycle は上書き)\n");

    printf("PASS: l1bmm write-position observed\n");
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
