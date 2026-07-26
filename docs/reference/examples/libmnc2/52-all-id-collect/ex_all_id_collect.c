/* ex_all_id_collect.c — ID 系固定値入力オペランド合成 → collect → バイナリ保存
 *
 * ビットレイアウト (u64):
 *   bits [17:12]: $peid[5:0]
 *   bits [11:9] : $l2bid[2:0]
 *   bits [8:6]  : $l1bid[2:0]
 *   bits [5:2]  : $mabid[3:0]
 *   bits [1:0]  : $subpeid[1:0]
 *
 * 検証:
 *   1. bits[17:12] == bits[5:0] (peid と subpeid+mabid の整合)
 *   2. bits[11:0] が 0..4095 に全一意出現 (flat PE ID)
 *
 *        (emu:process / device でも動作)
 * 第 1 引数 (非オプション) を指定すると mnc2_set_endian_ctrl(dev, ENDIAN_CTRL) を呼ぶ。
 * 省略時は何もしない (mnc2_open の初期値のまま)。
 * -o PATH で出力ファイルを指定 (デフォルト: _build/all_id_collect.bin)
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
#define DEFAULT_OUTPUT_FILE "_build/all_id_collect.bin"

int main(int argc, char** argv)
{
    int endian_ctrl_override = -1;   /* -1 なら指定なし */
    const char* output_file = DEFAULT_OUTPUT_FILE;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 >= argc) {
                printf("FAIL: -o の後に出力ファイル名が必要\n");
                return 1;
            }
            output_file = argv[++i];
        } else if (endian_ctrl_override < 0) {
            endian_ctrl_override = atoi(argv[i]);
            if (endian_ctrl_override < 0 || endian_ctrl_override > 4) {
                printf(
                    "FAIL: ENDIAN_CTRL は 0..4 の範囲で指定してください (指定値: %d)\n",
                    endian_ctrl_override);
                return 1;
            }
        } else {
            printf("FAIL: 余分な引数: %s\n", argv[i]);
            return 1;
        }
    }

    printf("[test] all-ID collect -> binary save");
    if (endian_ctrl_override >= 0)
        printf(" (ENDIAN_CTRL=%d)", endian_ctrl_override);
    printf("\n");

    mnc2_device_t dev = mnc2_open(0);
    if (dev == NULL) {
        printf("FAIL: mnc2_open failed\n");
        return 1;
    }

    if (endian_ctrl_override >= 0) {
        int rc = mnc2_set_endian_ctrl(dev, endian_ctrl_override);
        if (rc != 0) {
            printf("FAIL: mnc2_set_endian_ctrl(%d) returned %d\n",
                    endian_ctrl_override, rc);
            mnc2_close(dev);
            return 1;
        }
    }

    void* recvbuf = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    if (recvbuf == NULL) {
        printf("FAIL: alloc\n");
        mnc2_close(dev);
        return 1;
    }
    memset(recvbuf, 0, ELEM_BYTES);

    mnc2_kernel_t k = mnc2_load_kernel(dev, "_build/all_id_collect.idma.dat");
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

    uint64_t* rp = (uint64_t*)recvbuf;

    /* 先頭 8 要素を表示 */
    printf("  recv[0..7]:");
    for (int i = 0; i < 8 && i < ELEM_COUNT; i++)
        printf(" 0x%016" PRIx64, rp[i]);
    printf("\n");

    /* 検証は ENDIAN_CTRL のデフォルト (またはデフォルトと同じ byte order になる
       値) でしか通らないので、-N で明示指定された場合はスキップする。
       ec 指定時は単に出力ファイルを書き出し、呼び出し側 (check-emu-ecN) が
       sha256 で正当性を判定する。 */
    if (endian_ctrl_override < 0) {
        int errors = 0;
        uint8_t seen[4096];
        memset(seen, 0, sizeof(seen));

        for (int i = 0; i < ELEM_COUNT; i++) {
            uint64_t val = rp[i];

            /* 検証 1: peid == subpeid + mabid<<2 */
            uint64_t peid_field = (val >> 12) & 0x3F;
            uint64_t flat_id    = val & 0xFFF;
            uint64_t subpeid    = val & 0x3;
            uint64_t mabid      = (val >> 2) & 0xF;
            uint64_t expected_peid = subpeid + (mabid << 2);
            if (peid_field != expected_peid) {
                if (errors < 5)
                    printf("  FAIL [%d]: peid=%" PRIu64 " != subpeid+mabid<<2=%" PRIu64 "\n",
                            i, peid_field, expected_peid);
                errors++;
            }

            /* 検証 2: flat PE ID の一意性 */
            if (flat_id >= 4096) {
                if (errors < 5)
                    printf("  RANGE [%d]: flat_id=%" PRIu64 "\n", i, flat_id);
                errors++;
                continue;
            }
            if (seen[flat_id]) {
                if (errors < 5)
                    printf("  DUPLICATE [%d]: flat_id=%" PRIu64 "\n", i, flat_id);
                errors++;
            }
            seen[flat_id] = 1;
        }

        int missing = 0;
        for (int i = 0; i < 4096; i++) {
            if (!seen[i]) missing++;
        }
        if (missing > 0 && errors == 0) {
            printf("  %d PE IDs missing\n", missing);
            errors++;
        }

        if (errors > 0) {
            printf("FAIL: %d errors\n", errors);
            goto fail;
        }
    }

    /* バイナリファイルに保存 */
    FILE* fp = fopen(output_file, "wb");
    if (fp == NULL) {
        printf("FAIL: fopen(%s)\n", output_file);
        goto fail;
    }
    size_t written = fwrite(recvbuf, 1, ELEM_BYTES, fp);
    fclose(fp);
    if (written != ELEM_BYTES) {
        printf("FAIL: fwrite wrote %zu / %zu bytes\n", written, (size_t)ELEM_BYTES);
        goto fail;
    }

    printf("PASS: saved %zu bytes to %s (all 4096 IDs unique, peid consistent)\n",
           (size_t)ELEM_BYTES, output_file);
    mnc2_free_host_buffer(dev, recvbuf, ELEM_BYTES);
    mnc2_close(dev);
    return 0;

fail:
    mnc2_free_host_buffer(dev, recvbuf, ELEM_BYTES);
    mnc2_close(dev);
    return 1;
}
