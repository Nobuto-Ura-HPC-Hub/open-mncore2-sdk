/* ex_broadcast_l1bmp.c — PDM[0] を全 4096 PE に broadcast (l1bmp + v + /1000) -> collect 検証
 *
 * 動作:
 *   1. PDM[0] = broadcast する値 v を送信 (他は別値、 send_wait_tag=0x10)
 *   2. exec_kernel:
 *      a. broadcast: l1bmp $lb0 $lr0v/1000 で L1BM[0] (= PDM[0]) を全 4096 PE の GRF0[0] に
 *         (v + /1000 = cycle 0 だけ通す = 全 PE 同値。 l1bmm の sub_pe 分配・周期は起きない)
 *      b. collect:   各 PE GRF0[0] -> PDM[0..4095] (GRF0 直 collect、 lpassa 不要)
 *   3. recv で PDM[0..4095] を回収 (recv_wait_tag=0x1e)
 *   4. 全 4096 PE が v (周期なしの完全同値) を assert。
 *      60 (l1bmm、 周期 4 [12,13,14,15]) と対比して「l1bmp は周期なしの完全同値」を示す。
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
#define BVAL       ((uint64_t)0x4243444546474849ULL)   /* broadcast する値 v (非ゼロの目印) */

int main(void)
{
    printf("[test] PDM[0] -> all 4096 PE broadcast (l1bmp + v + /1000) -> collect\n");

    mnc2_device_t dev = mnc2_open(0);
    if (dev == NULL) { printf("FAIL: mnc2_open\n"); return 1; }
    int ok = 0;

    void* sendbuf = mnc2_alloc_host_buffer(dev, IN_BYTES);
    void* recvbuf = mnc2_alloc_host_buffer(dev, OUT_BYTES);
    if (sendbuf == NULL || recvbuf == NULL) { printf("FAIL: alloc\n"); goto done; }

    /* PDM[0] = v (broadcast する値)。 他は別値 (v だけが届くことを区別するため) */
    uint64_t* sp = (uint64_t*)sendbuf;
    for (int i = 0; i < IN_COUNT; i++) sp[i] = 0xFFFFFFFFFFFFFFFFULL;
    sp[0] = BVAL;
    memset(recvbuf, 0, OUT_BYTES);

    int rc = mnc2_send(dev, sendbuf, 0, IN_BYTES, SEND_TAG);
    if (rc != 0) { printf("FAIL: send rc=%d\n", rc); goto done; }

    mnc2_kernel_t k = mnc2_load_kernel(dev, "_build/broadcast_l1bmp.idma.dat");
    if (k == NULL) { printf("FAIL: load_kernel\n"); goto done; }
    rc = mnc2_exec_kernel(k);
    mnc2_free_kernel(k);
    if (rc != 0) { printf("FAIL: exec_kernel rc=%d\n", rc); goto done; }

    rc = mnc2_recv(dev, recvbuf, 0, OUT_BYTES, RECV_TAG);
    if (rc != 0) { printf("FAIL: recv rc=%d\n", rc); goto done; }

    uint64_t* rp = (uint64_t*)recvbuf;
    printf("  recv[0..7]: %" PRIx64 " %" PRIx64 " %" PRIx64 " %" PRIx64
           " %" PRIx64 " %" PRIx64 " %" PRIx64 " %" PRIx64 "\n",
           rp[0], rp[1], rp[2], rp[3], rp[4], rp[5], rp[6], rp[7]);

    /* 全 4096 PE が v (周期なしの完全同値) を assert */
    int bad = 0;
    for (int i = 0; i < OUT_COUNT; i++) {
        if (rp[i] != BVAL) {
            bad++;
            if (bad <= 3)
                printf("  MISMATCH [%d]: got %" PRIx64 " expect %" PRIx64 "\n", i, rp[i], BVAL);
        }
    }
    if (bad == 0) {
        printf("PASS: 全 4096 PE が v = %" PRIx64 " (周期なしの完全同値。 l1bmp broadcast)\n", BVAL);
        ok = 1;
    } else {
        printf("FAIL: v と違う PE が %d 件\n", bad);
    }

done:
    if (sendbuf) mnc2_free_host_buffer(dev, sendbuf, IN_BYTES);
    if (recvbuf) mnc2_free_host_buffer(dev, recvbuf, OUT_BYTES);
    mnc2_close(dev);
    return ok ? 0 : 1;
}
