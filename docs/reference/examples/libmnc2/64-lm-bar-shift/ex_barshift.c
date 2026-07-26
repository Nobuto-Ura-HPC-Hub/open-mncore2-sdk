/* ex_barshift.c — LM1 の BAR (ベースアドレスレジスタ) シフトの検証
 *
 * imm で全 PE の LM1 物理 0..7 に 16..23 を置き、LM1 BAR に 4 を書く。その後
 * lpassa $ln0 (auto-stride, BAR 適用) で LM1 物理 4 を読み LM0[0] に写し、LM0 を
 * collect する。BAR=4 が効けば全 PE の値 = 20 (物理4)、効かなければ 16 (物理0)。
 * imm で全 PE 同値なので distribute の PE 対応に依存せず、collect は LM0 (BAR=0)
 * なので collect の BAR 適用も問わない。
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
    printf("[test] LM1 BAR shift: write BAR=4, read $ln0 -> should get physical 4 (=20)\n");

    mnc2_device_t dev = mnc2_open(0);
    if (dev == NULL) { printf("FAIL: mnc2_open\n"); return 1; }
    int ok = 0;

    void* sendbuf = mnc2_alloc_host_buffer(dev, IN_BYTES);
    void* recvbuf = mnc2_alloc_host_buffer(dev, OUT_BYTES);
    if (sendbuf == NULL || recvbuf == NULL) { printf("FAIL: alloc\n"); goto done; }

    memset(sendbuf, 0, IN_BYTES);   /* ダミー send (vsm の wait i10 を満たすだけ) */
    memset(recvbuf, 0, OUT_BYTES);

    int rc = mnc2_send(dev, sendbuf, 0, IN_BYTES, SEND_TAG);
    if (rc != 0) { printf("FAIL: send rc=%d\n", rc); goto done; }

    mnc2_kernel_t k = mnc2_load_kernel(dev, "_build/barshift.idma.dat");
    if (k == NULL) { printf("FAIL: load_kernel\n"); goto done; }
    rc = mnc2_exec_kernel(k);
    mnc2_free_kernel(k);
    if (rc != 0) { printf("FAIL: exec_kernel rc=%d\n", rc); goto done; }

    rc = mnc2_recv(dev, recvbuf, 0, OUT_BYTES, RECV_TAG);
    if (rc != 0) { printf("FAIL: recv rc=%d\n", rc); goto done; }

    uint64_t* rp = (uint64_t*)recvbuf;
    printf("  recv[0..7]: %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64
           " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 "\n",
           rp[0], rp[1], rp[2], rp[3], rp[4], rp[5], rp[6], rp[7]);

    /* imm i"N" は long word 表現で下位バイトに N が入る (recv は 0xNN..00NN)。
       下位バイトで物理位置の値を判定する。 BAR=4 なら 20 (物理4)、無効なら 16 (物理0)。 */
    unsigned v0 = (unsigned)(rp[0] & 0xff);
    if (v0 == 20) {
        int bad = 0;
        for (int i = 0; i < OUT_COUNT; i++) if ((rp[i] & 0xff) != 20) { bad++; }
        if (bad != 0) { printf("FAIL: 一部の PE が物理4 でない (%d 件)\n", bad); goto done; }
        printf("PASS: 全 4096 PE で LM1[0] が物理4=20 を読んだ -> BAR=4 が効く (LM BAR は SP に使える)\n");
        ok = 1;
    } else if (v0 == 16) {
        printf("FAIL: LM1[0]=物理0=16 -> BAR=4 が効いていない\n");
    } else {
        printf("FAIL: recv[0]=0x%" PRIx64 " 下位=%u (想定外: 16=BAR無効 / 20=BAR有効)\n", rp[0], v0);
    }

done:
    if (sendbuf) mnc2_free_host_buffer(dev, sendbuf, IN_BYTES);
    if (recvbuf) mnc2_free_host_buffer(dev, recvbuf, OUT_BYTES);
    mnc2_close(dev);
    return ok ? 0 : 1;
}
