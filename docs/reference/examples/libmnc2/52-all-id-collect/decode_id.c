/* decode_id.c — all_id_collect.bin からフィールドを抽出
 *
 * Usage: decode_id [--peid|--subpeid|--mabid|--l1bid|--l2bid|--flat] [--swap] [--text] < input.bin
 *
 * ビットレイアウト (u64):
 *   bits [17:12]: $peid
 *   bits [11:9] : $l2bid
 *   bits [8:6]  : $l1bid
 *   bits [5:2]  : $mabid
 *   bits [1:0]  : $subpeid
 *   bits [11:0] : flat ID (subpeid + mabid*4 + l1bid*64 + l2bid*512)
 *
 * デフォルト: 各フィールドを 8bit バイナリで stdout に出力
 * --text: テキスト (10進数 + 改行) で出力
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

enum field { F_PEID, F_SUBPEID, F_MABID, F_L1BID, F_L2BID, F_FLAT };

static uint8_t extract(uint64_t v, enum field f)
{
    switch (f) {
    case F_PEID:    return (v >> 12) & 0x3F;
    case F_SUBPEID: return v & 0x3;
    case F_MABID:   return (v >> 2) & 0xF;
    case F_L1BID:   return (v >> 6) & 0x7;
    case F_L2BID:   return (v >> 9) & 0x7;
    case F_FLAT:    return v & 0xFF; /* 下位 8bit のみ。12bit 全部は uint16_t が必要 */
    }
    return 0;
}

static uint16_t extract16(uint64_t v, enum field f)
{
    if (f == F_FLAT) return v & 0xFFF;
    return extract(v, f);
}

int main(int argc, char **argv)
{
    enum field field = F_PEID;
    int text = 0;
    int swap = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--peid") == 0)    field = F_PEID;
        else if (strcmp(argv[i], "--subpeid") == 0) field = F_SUBPEID;
        else if (strcmp(argv[i], "--mabid") == 0)   field = F_MABID;
        else if (strcmp(argv[i], "--l1bid") == 0)    field = F_L1BID;
        else if (strcmp(argv[i], "--l2bid") == 0)    field = F_L2BID;
        else if (strcmp(argv[i], "--flat") == 0)     field = F_FLAT;
        else if (strcmp(argv[i], "--text") == 0)     text = 1;
        else if (strcmp(argv[i], "--swap") == 0)     swap = 1;
        else {
            printf("Usage: decode_id [--peid|--subpeid|--mabid|--l1bid|--l2bid|--flat] [--swap] [--text] < input.bin\n");
            return 1;
        }
    }

    uint64_t v;
    while (fread(&v, sizeof(v), 1, stdin) == 1) {
        if (swap) v = __builtin_bswap64(v);
        if (text) {
            printf("%u\n", extract16(v, field));
        } else {
            uint8_t b = extract(v, field);
            fwrite(&b, 1, 1, stdout);
        }
    }

    return 0;
}
