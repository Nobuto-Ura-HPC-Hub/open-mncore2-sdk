/* ex_f32_imm_collect.c — f32 imm 即値設定 → collect → バイナリ保存
 *
 * VSM カーネルが $r0 = f32 1.0, $r1 = f32 -1.0 を設定し、GRF0 → PDM へ
 * collect する。ホスト側は ENDIAN_CTRL=4 (default、u64 identity) で recv
 * し、PE レジスタ u64 をそのまま観測する。
 *
 * 注意: MN-Core 2 の register 命名と host の LE byte 順でインデックスが
 * "ずれる"。
 *   - PE: $r0 = u64 の上位 32bit、$r1 = u64 の下位 32bit
 *   - host LE u64: 下位 32bit が bytes 0..3、上位 32bit が bytes 4..7
 *   - 結果として bytes 0..3 = $r1 (-1.0)、bytes 4..7 = $r0 (1.0)
 * C の `float a[2] = {1.0, -1.0}` と byte 配置が逆順になる点に注意。
 * これは MN-Core 2 の u64 原子性の性質であり、ライブラリは opinion を
 * 持たず raw u64 値をそのまま運ぶ。アプリ側で必要に応じて
 * mnc2_set_endian_ctrl(dev, 3) を呼べば f32 natural order にできる。
 *
 * Output: f32_imm_collect.bin (4096 x 8 = 32768 bytes)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "mnc2.h"

#define ELEM_COUNT  4096
#define ELEM_BYTES  (ELEM_COUNT * sizeof(uint64_t))
#define OFFSET_OUT  0
#define RECV_TAG     0x1e
#define OUTPUT_FILE "_build/f32_imm_collect.bin"

/* ENDIAN_CTRL=4 default (u64 identity) で PE reg u64 をそのまま観測する。
   上位 32bit = $r0 = 0x3F800000 (f32 1.0)、下位 32bit = $r1 = 0xBF800000 (f32 -1.0)。
   u64 値としては 0x3F800000BF800000。
   host を C の float 配列として linear に読むとバイト順で逆順 ({-1.0, 1.0}) に見える
   ので、その挙動が必要ならアプリ側で mnc2_set_endian_ctrl(dev, 3) を明示的に呼ぶ。 */
#define EXPECTED_U64 UINT64_C(0x3F800000BF800000)

int main(void)
{
    printf("[test] f32 imm (1.0, -1.0) -> collect -> verify\n");

    mnc2_device_t dev = mnc2_open(0);
    if (dev == NULL) {
        printf("FAIL: mnc2_open failed\n");
        return 1;
    }

    void* recvbuf = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    if (recvbuf == NULL) {
        printf("FAIL: alloc\n");
        mnc2_close(dev);
        return 1;
    }
    memset(recvbuf, 0, ELEM_BYTES);

    mnc2_kernel_t k = mnc2_load_kernel(dev, "_build/f32_imm_collect.idma.dat");
    if (k == NULL) {
        printf("FAIL: load_kernel\n");
        goto fail;
    }
    int rc = mnc2_exec_kernel(k);
    mnc2_free_kernel(k);
    if (rc != 0) {
        printf("FAIL: exec_kernel returned %d\n", rc);
        goto fail;
    }

    rc = mnc2_recv(dev, recvbuf, OFFSET_OUT, ELEM_BYTES, RECV_TAG);
    if (rc != 0) {
        printf("FAIL: recv returned %d\n", rc);
        goto fail;
    }

    /* 先頭数要素を表示 */
    uint64_t* rp = (uint64_t*)recvbuf;
    printf("  recv[0..3]: 0x%016" PRIx64 " 0x%016" PRIx64
           " 0x%016" PRIx64 " 0x%016" PRIx64 "\n",
           rp[0], rp[1], rp[2], rp[3]);

    /* バイナリを先に保存する。検証が失敗してもデバッグ用に残す。 */
    FILE* fp = fopen(OUTPUT_FILE, "wb");
    if (fp == NULL) {
        printf("FAIL: fopen(%s)\n", OUTPUT_FILE);
        goto fail;
    }
    size_t written = fwrite(recvbuf, 1, ELEM_BYTES, fp);
    fclose(fp);
    if (written != ELEM_BYTES) {
        printf("FAIL: fwrite wrote %zu / %zu bytes\n", written, (size_t)ELEM_BYTES);
        goto fail;
    }
    printf("  saved %zu bytes to %s\n", (size_t)ELEM_BYTES, OUTPUT_FILE);

    /* 全 PE が期待値と一致することを確認 */
    int mismatches = 0;
    for (int i = 0; i < ELEM_COUNT; i++) {
        if (rp[i] != EXPECTED_U64) {
            if (mismatches < 5)
                printf("  WARN: [%d]=0x%016" PRIx64 " != expected 0x%016" PRIx64 "\n",
                        i, rp[i], (uint64_t)EXPECTED_U64);
            mismatches++;
        }
    }
    if (mismatches > 0) {
        printf("FAIL: %d/%d elements differ from expected 0x%016" PRIx64 "\n",
                mismatches, ELEM_COUNT, (uint64_t)EXPECTED_U64);
        goto fail;
    }

    printf("PASS: all %d PEs == 0x%016" PRIx64 "\n",
           ELEM_COUNT, (uint64_t)EXPECTED_U64);
    mnc2_free_host_buffer(dev, recvbuf, ELEM_BYTES);
    mnc2_close(dev);
    return 0;

fail:
    mnc2_free_host_buffer(dev, recvbuf, ELEM_BYTES);
    mnc2_close(dev);
    return 1;
}
