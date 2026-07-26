/* gen_bf_golden.c — @boundary_flags 検証用 golden バイナリ生成
 *
 * Usage: ./gen_bf_golden ok   <output_file> [--edge-left] [--edge-right]
 *        ./gen_bf_golden zero <output_file>
 *
 * 出力: 4096 要素 × 8 バイト (uint64_t) のバイナリ
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define ELEM_COUNT 4096

/* 10-bit layout (cross_group 廃止):
 * bit 9: data_edge_left    bit 8: data_edge_right
 * bit 7: cross_chip_left   bit 6: cross_chip_right
 * bit 5: cross_L2B_left    bit 4: cross_L2B_right  (旧 cross_group を含む)
 * bit 3: cross_L1B_left    bit 2: cross_L1B_right
 * bit 1: cross_MAB_left    bit 0: cross_MAB_right
 */
static uint64_t expected_flags(int pe_id, int edge_left, int edge_right)
{
    int subpeid = pe_id & 0x3;
    int mabid   = (pe_id >> 2) & 0xF;
    int l1bid   = (pe_id >> 6) & 0x7;
    int l2bid   = (pe_id >> 9) & 0x7;

    /* 排他的フラグ (cross_group は cross_L2B に統合) */
    if (subpeid == 3) {
        if (mabid == 15) {
            if (l1bid == 7) {
                if (l2bid == 7) return (1 << (edge_right ? 8 : 6));  /* data_edge_right or cross_chip_right */
                return (1 << 4);  /* cross_L2B_right (旧 cross_group_right を含む) */
            }
            return (1 << 2);
        }
        return (1 << 0);
    }
    if (subpeid == 0) {
        if (mabid == 0) {
            if (l1bid == 0) {
                if (l2bid == 0) return (1 << (edge_left ? 9 : 7));  /* data_edge_left or cross_chip_left */
                return (1 << 5);  /* cross_L2B_left (旧 cross_group_left を含む) */
            }
            return (1 << 3);
        }
        return (1 << 1);
    }
    return 0;
}

int main(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s <ok|zero> <output_file> [--edge-left] [--edge-right]\n", argv[0]);
        return 1;
    }

    int ok_mode;
    if (strcmp(argv[1], "ok") == 0)
        ok_mode = 1;
    else if (strcmp(argv[1], "zero") == 0)
        ok_mode = 0;
    else {
        fprintf(stderr, "error: mode must be 'ok' or 'zero'\n");
        return 1;
    }

    int edge_left = 0, edge_right = 0;
    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--edge-left") == 0) edge_left = 1;
        if (strcmp(argv[i], "--edge-right") == 0) edge_right = 1;
    }

    uint64_t buf[ELEM_COUNT];
    if (ok_mode) {
        for (int i = 0; i < ELEM_COUNT; i++)
            buf[i] = expected_flags(i, edge_left, edge_right);
    } else {
        memset(buf, 0, sizeof(buf));
    }

    FILE *fp = fopen(argv[2], "wb");
    if (!fp) {
        perror(argv[2]);
        return 1;
    }
    if (fwrite(buf, sizeof(uint64_t), ELEM_COUNT, fp) != ELEM_COUNT) {
        fprintf(stderr, "error: write failed\n");
        fclose(fp);
        return 1;
    }
    fclose(fp);
    return 0;
}
