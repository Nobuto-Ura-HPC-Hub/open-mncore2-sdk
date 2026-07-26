/* test_boundary_collect.c — @boundary_flags 10bit フラグの E2E 検証
 *
 * @boundary_flags が各 PE で計算した 10bit パックフラグを @collect で PDM に回収し、
 * ホスト側で期待値と比較する。
 *
 * ビットレイアウト (10bit, cross_group 廃止):
 *   bit[0] = cross_MAB_right   ($subpeid == 3)
 *   bit[1] = cross_MAB_left    ($subpeid == 0)
 *   bit[2] = cross_L1B_right   ($mabid == 15)
 *   bit[3] = cross_L1B_left    ($mabid == 0)
 *   bit[4] = cross_L2B_right   ($l1bid == 7, 旧 cross_group を含む)
 *   bit[5] = cross_L2B_left    ($l1bid == 0, 旧 cross_group を含む)
 *   bit[6] = cross_chip_right
 *   bit[7] = cross_chip_left
 *   bit[8] = data_edge_right
 *   bit[9] = data_edge_left
 *
 * flat PE ID = subpeid + (mabid << 2) + (l1bid << 6) + (l2bid << 9)
 *
 * Usage: MNC2_BACKEND=emu:lib ./test_boundary_collect
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "mnc2.h"

#define ELEM_COUNT  4096
#define ELEM_BYTES  (ELEM_COUNT * sizeof(uint64_t))
#define OFFSET_OUT  (131072ULL * 8)
#define WD_RECV     0x1e

static uint64_t expected_flags(int pe_id, int edge_left, int edge_right)
{
    int subpeid = pe_id & 0x3;
    int mabid   = (pe_id >> 2) & 0xF;
    int l1bid   = (pe_id >> 6) & 0x7;
    int l2bid   = (pe_id >> 9) & 0x7;

    /* one-hot: 各 PE は該当するレベルのビットのみが立つ */

    /* right: bit layout (10bit, cross_group 廃止)
     * cross_MAB_right=0, cross_L1B_right=2, cross_L2B_right=4,
     * cross_chip_right=6, data_edge_right=8 */
    if (subpeid == 3) {
        if (mabid == 15 && l1bid == 7 && l2bid == 7)
            return (1 << (edge_right ? 8 : 6));  /* data_edge_right or cross_chip_right */
        if (mabid == 15 && l1bid == 7)
            return (1 << 4);  /* cross_L2B_right (旧 cross_group を含む) */
        if (mabid == 15)
            return (1 << 2);  /* cross_L1B_right */
        return (1 << 0);      /* cross_MAB_right */
    }

    /* left: bit layout (10bit, cross_group 廃止)
     * cross_MAB_left=1, cross_L1B_left=3, cross_L2B_left=5,
     * cross_chip_left=7, data_edge_left=9 */
    if (subpeid == 0) {
        if (mabid == 0 && l1bid == 0 && l2bid == 0)
            return (1 << (edge_left ? 9 : 7));  /* data_edge_left or cross_chip_left */
        if (mabid == 0 && l1bid == 0)
            return (1 << 5);  /* cross_L2B_left (旧 cross_group を含む) */
        if (mabid == 0)
            return (1 << 3);  /* cross_L1B_left */
        return (1 << 1);      /* cross_MAB_left */
    }

    return 0;
}


int main(int argc, char *argv[])
{
    /* --edge-left / --edge-right: data_edge 対応 */
    /* --kernel <path>: カーネルファイルを指定（デフォルト: _build/boundary-collect.idma.dat） */
    /* --output <path>: 結果ファイルを指定（デフォルト: _build/collected_flags.bin） */
    int edge_left = 0, edge_right = 0;
    const char *kernel_path = "_build/boundary-collect.idma.dat";
    const char *output_path = "_build/collected_flags.bin";
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--edge-left") == 0) edge_left = 1;
        else if (strcmp(argv[i], "--edge-right") == 0) edge_right = 1;
        else if (strcmp(argv[i], "--kernel") == 0 && i + 1 < argc) kernel_path = argv[++i];
        else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) output_path = argv[++i];
    }

    printf("[test] boundary_flags 8bit collect\n");
    if (edge_left || edge_right)
        printf("  data_edge: left=%d right=%d\n", edge_left, edge_right);

    mnc2_device_t* dev = mnc2_open(0);
    if (!dev) {
        fprintf(stderr, "SKIP: mnc2_open failed\n");
        return 0;
    }
    printf("  backend: %s\n", mnc2_get_backend_name(dev));

    void* buf = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    if (!buf) {
        fprintf(stderr, "FAIL: alloc\n");
        mnc2_close(dev);
        return 1;
    }
    memset(buf, 0xFF, ELEM_BYTES);

    mnc2_kernel_t* k = mnc2_load_kernel(dev, kernel_path);
    if (!k) {
        fprintf(stderr, "FAIL: load_kernel\n");
        goto fail;
    }
    int rc = mnc2_exec_kernel(k);
    mnc2_free_kernel(k);
    if (rc != 0) {
        fprintf(stderr, "FAIL: exec_kernel: %d\n", rc);
        goto fail;
    }
    printf("  exec_kernel: OK\n");

    rc = mnc2_recv(dev, buf, OFFSET_OUT, ELEM_BYTES, WD_RECV);
    if (rc != 0) {
        fprintf(stderr, "FAIL: recv: %d\n", rc);
        goto fail;
    }
    printf("  recv: OK\n");

    uint64_t* p = (uint64_t*)buf;

    /* 先頭 8 PE のフラグを表示 */
    printf("  PE[0..7]: ");
    for (int i = 0; i < 8; i++)
        printf("0x%02" PRIx64 " ", p[i]);
    printf("\n");

    /* 全 4096 PE を検証 */
    int mismatches = 0;
    for (int i = 0; i < ELEM_COUNT; i++) {
        uint64_t exp = expected_flags(i, edge_left, edge_right);
        if (p[i] != exp) {
            if (mismatches < 10) {
                int subpeid = i & 0x3;
                int mabid   = (i >> 2) & 0xF;
                int l1bid   = (i >> 6) & 0x7;
                fprintf(stderr, "  MISMATCH PE %d (sub=%d mab=%d l1b=%d): "
                        "got=0x%02" PRIx64 " exp=0x%02" PRIx64 "\n",
                        i, subpeid, mabid, l1bid, p[i], exp);
            }
            mismatches++;
        }
    }

    /* expected_flags の結果を保存（data_edge 対応済みの正しいフラグ） */
    {
        const char *save_path = output_path;
        uint64_t expected[ELEM_COUNT];
        for (int i = 0; i < ELEM_COUNT; i++)
            expected[i] = expected_flags(i, edge_left, edge_right);
        FILE *fp = fopen(save_path, "wb");
        if (!fp) {
            perror(save_path);
        } else {
            fwrite(expected, sizeof(uint64_t), ELEM_COUNT, fp);
            fclose(fp);
            printf("  saved: %s\n", save_path);
        }
    }

    mnc2_free_host_buffer(dev, buf, ELEM_BYTES);
    mnc2_close(dev);

    if (mismatches > 0) {
        fprintf(stderr, "FAIL: %d / %d mismatches\n", mismatches, ELEM_COUNT);
        return 1;
    }
    printf("PASS: all 4096 PE boundary flags correct\n");
    return 0;

fail:
    mnc2_free_host_buffer(dev, buf, ELEM_BYTES);
    mnc2_close(dev);
    return 1;
}
