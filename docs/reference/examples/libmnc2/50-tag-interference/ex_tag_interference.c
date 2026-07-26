/* ex_tag_interference.c — PE wait と DMA wd の干渉テスト
 *
 * device バックエンドのみ実行。エミュレータでは SKIP。
 * と agent はいっているが、りょうすさん はすべてのバックエンドで
 * うごくべき(理想)
 * なので backend による強制リターンを if 0 にしたよ
 *
 * テスト 3,5,2,4 → 1 の順に実行（テスト 1 はキューを汚すため最後）。
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include "mnc2.h"

#define ELEM_COUNT  4096
#define ELEM_BYTES  (ELEM_COUNT * sizeof(uint64_t))
#define OFFSET_OUT  0
#define RECV_TAG    0x1e

static int run_collect_and_recv(mnc2_device_t dev, const char* kernel_path,
                                int sleep_before_recv)
{
    void* recvbuf = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    if (recvbuf == NULL) return -99;
    memset(recvbuf, 0, ELEM_BYTES);

    mnc2_kernel_t k = mnc2_load_kernel(dev, kernel_path);
    if (k == NULL) {
        mnc2_free_host_buffer(dev, recvbuf, ELEM_BYTES);
        return -98;
    }

    int rc = mnc2_exec_kernel(k);
    mnc2_free_kernel(k);
    if (rc != 0) {
        mnc2_free_host_buffer(dev, recvbuf, ELEM_BYTES);
        return -97;
    }

    if (sleep_before_recv > 0)
        sleep(sleep_before_recv);

    rc = mnc2_recv(dev, recvbuf, OFFSET_OUT, ELEM_BYTES, RECV_TAG);

    if (rc == 0) {
        uint64_t* rp = (uint64_t*)recvbuf;
        uint64_t expected = 0x0000303900010932ULL;  /* imm 12345($r0=upper) / 67890($r1=lower) */
        int mismatches = 0;
        for (int i = 0; i < ELEM_COUNT; i++) {
            if (rp[i] != expected) {
                if (mismatches < 3)
                    printf("    [%d] got=0x%016" PRIx64
                            " expected=0x%016" PRIx64 "\n",
                            i, rp[i], expected);
                mismatches++;
            }
        }
        if (mismatches > 0) {
            printf("    %d/%d mismatches\n", mismatches, ELEM_COUNT);
            rc = -1;
        }
    }

    mnc2_free_host_buffer(dev, recvbuf, ELEM_BYTES);
    return rc;
}

static int run_nop_and_recv(mnc2_device_t dev)
{
    void* recvbuf = mnc2_alloc_host_buffer(dev, 8);
    if (recvbuf == NULL) return -99;

    mnc2_kernel_t k = mnc2_load_kernel(dev, "_build/nop.idma.dat");
    if (k == NULL) {
        mnc2_free_host_buffer(dev, recvbuf, 8);
        return -98;
    }

    int rc = mnc2_exec_kernel(k);
    mnc2_free_kernel(k);
    if (rc != 0) {
        mnc2_free_host_buffer(dev, recvbuf, 8);
        return -97;
    }

    rc = mnc2_recv(dev, recvbuf, 0, 8, RECV_TAG);
    mnc2_free_host_buffer(dev, recvbuf, 8);
    return rc;
}

int main(void)
{
    printf("=== tag interference test ===\n\n");

    mnc2_device_t dev = mnc2_open(0);
    if (dev == NULL) {
        printf("FAIL: mnc2_open failed\n");
        return 1;
    }

#if 0
    const char* be = mnc2_get_backend_name(dev);
    if (strcmp(be, "device") != 0) {
        printf("SKIP: device backend required (current: %s)\n", be);
        mnc2_close(dev);
        return 0;
    }
#endif

    int failed = 0;
    int rc;

    printf("[test 3] mvp/i1e (no wait) -> recv(wd=0x1e)\n");
    rc = run_collect_and_recv(dev, "_build/imm_collect_nowait.idma.dat", 0);
    if (rc == 0) printf("  PASS\n"); else { printf("  FAIL: rc=%d\n", rc); failed++; }
    mnc2_reset(dev);

    printf("[test 5] mvp/i1e (no wait) -> sleep 1 -> recv(wd=0x1e)\n");
    rc = run_collect_and_recv(dev, "_build/imm_collect_nowait.idma.dat", 1);
    if (rc == 0) printf("  PASS\n"); else { printf("  FAIL: rc=%d\n", rc); failed++; }
    mnc2_reset(dev);

    printf("[test 2] mvp/i1e + wait i1e -> recv(wd=0x1e)\n");
    rc = run_collect_and_recv(dev, "_build/imm_collect.idma.dat", 0);
    if (rc == 0) printf("  PASS\n"); else { printf("  FAIL: rc=%d\n", rc); failed++; }
    mnc2_reset(dev);

    printf("[test 4] mvp/i1e + wait i1e -> sleep 1 -> recv(wd=0x1e) [DECISIVE]\n");
    rc = run_collect_and_recv(dev, "_build/imm_collect.idma.dat", 1);
    if (rc == 0) printf("  PASS: PE wait does not interfere with DMA wd\n");
    else { printf("  FAIL: rc=%d — collect VSM の末尾 wait i1e を除去すべき\n", rc); failed++; }
    mnc2_reset(dev);

    /* test 1 は wd-wait timeout の semantic を確認する。 emu:process は
       gpfn3_package_main subprocess で kernel を 1 回実行 → PDM dump を file に
       保存 → host が dump を読む batch 型のため、 「wd が立たないので recv が
       polling 上限で timeout する」 という polling semantic を持たない。
       (= recv は dump を読むだけで成功する)。 emu:process では本 scenario を
       skip する。 emu:lib / device では polling timeout を確認できる。 */
    const char* be = mnc2_get_backend_name(dev);
    if (strcmp(be, "emu:process") == 0) {
        printf("[test 1] nop (no mvp) -> recv(wd=0x1e) -> SKIP (emu:process は wd-wait timeout の semantic なし)\n");
    } else {
        printf("[test 1] nop (no mvp) -> recv(wd=0x1e) -> expect TIMEOUT\n");
        printf("  (以下の ddma_wait timeout / recv failed は期待動作)\n");
        rc = run_nop_and_recv(dev);
        if (rc == MNC2_ERROR_TIMEOUT) printf("  PASS: wd wait works\n");
        else if (rc == 0) { printf("  FAIL: recv should timeout\n"); failed++; }
        else { printf("  FAIL: unexpected rc=%d\n", rc); failed++; }
    }

    /* 各 test 間および test 1 末尾で mnc2_reset() を呼んで前 scenario の
       queue / done_flags 残留をクリア。 emu:lib backend は gpfn3_reset_device
       を実装済 (instruction queue / DMA queue / done_flags 全 clear)。 */
    int rst_rc = mnc2_reset(dev);
    if (rst_rc != 0) printf("  WARN: mnc2_reset rc=%d\n", rst_rc);

    mnc2_close(dev);
    printf("\n");
    if (failed > 0) { printf("FAILED: %d test(s)\n", failed); return 1; }
    printf("ALL PASS\n");
    return 0;
}
