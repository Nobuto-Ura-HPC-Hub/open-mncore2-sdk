/*
 * channel_opt.h — u01/u02 共通の --channel オプションパーサ
 *
 * DMA チャネル番号を数値 or シンボルで指定する。
 * シンボルは direction に依存せず「対象メモリ領域」で命名。
 */

#ifndef ULIB_CHANNEL_OPT_H
#define ULIB_CHANNEL_OPT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 対象メモリ領域ごとのチャネル対応 (シンボルは PDM / DRAM のみ):

   送信 (TO_DEVICE):
     PDM  / 0 : Host → PDM CH0
     1       : Host → PDM CH1 (alt)
     DRAM / 3 : Host → Device-DRAM (DDMA-2)
     (CH2 は IDMA (命令 DMA) 用、データ送信不可)

   受信 (FROM_DEVICE):
     PDM  / 0 : PDM → Host CH0
     1       : PDM → Host CH1 (alt)
     2       : Device-DRAM → Host (DDMA-2 alternative, 動作未確認)
     DRAM / 3 : Device-DRAM → Host (DDMA-2)

   シンボルは用途が明確な PDM (CH0) と DRAM (CH3) のみ提供。
   それ以外は数値 (0/1/2/3) で直接指定。
*/

/* 数値 or シンボルを 0..3 に変換。不正値は -1 を返す。 */
static inline int parse_channel(const char *s)
{
    if (s == NULL || s[0] == '\0') return -1;
    if (strcmp(s, "0") == 0) return 0;
    if (strcmp(s, "1") == 0) return 1;
    if (strcmp(s, "2") == 0) return 2;
    if (strcmp(s, "3") == 0) return 3;
    if (strcasecmp(s, "PDM")  == 0) return 0;
    if (strcasecmp(s, "DRAM") == 0) return 3;
    return -1;
}

/* Usage 文字列に差し込む用の説明文。
   send 側と recv 側で有効チャネルが異なるので別々に提供する。 */
static const char CHANNEL_HELP_SEND[] =
    "  --channel NAME  送信先チャネルを指定 (省略時 PDM)\n"
    "    PDM  / 0 : Host → PDM CH0\n"
    "    1        : Host → PDM CH1 (alt)\n"
    "    DRAM / 3 : Host → Device-DRAM (DDMA-2)\n"
    "    (CH2 は IDMA 用のため送信には使えない)\n";

static const char CHANNEL_HELP_RECV[] =
    "  --channel NAME  受信元チャネルを指定 (省略時 PDM)\n"
    "    PDM  / 0 : PDM → Host CH0\n"
    "    1        : PDM → Host CH1 (alt)\n"
    "    2        : Device-DRAM → Host (DDMA-2 alt, 動作未確認)\n"
    "    DRAM / 3 : Device-DRAM → Host (DDMA-2)\n";

/* --channel オプションを argv から抽出して除去する。
   成功時は解析した channel を *ch_out に入れて残り argv を詰める。
   エラー時は -1 を返す (stderr に出力済み)。 */
static inline int extract_channel_opt(int *argc_io, char **argv, int *ch_out)
{
    int argc = *argc_io;
    int j = 1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--channel") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "エラー: --channel に値が必要\n");
                return -1;
            }
            int ch = parse_channel(argv[i + 1]);
            if (ch < 0) {
                fprintf(stderr, "エラー: --channel 不正値 '%s'\n", argv[i + 1]);
                return -1;
            }
            *ch_out = ch;
            i++;  /* skip value */
        } else {
            argv[j++] = argv[i];
        }
    }
    *argc_io = j;
    return 0;
}

#endif /* ULIB_CHANNEL_OPT_H */
