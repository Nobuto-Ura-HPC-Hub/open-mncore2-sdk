/* kick_idma2.c — gpfn3_kick_inst_dma2 単体実行ツール
 *
 * 事前に descriptor memory がセットアップされている前提で、
 * 指定した dm_index / dm_count で IDMA2 kick だけ行う。
 *
 * IDMA 完了待ちは PIO 0x018 (IDMA_STAT) で 2 段構え (Phase 1 開始確認 +
 * Phase 2 完了待ち)。
 *
 * Usage:
 *   kick-idma2 DM_INDEX DM_COUNT [--no-wait]
 *
 *   DM_INDEX:  descriptor memory の開始 index
 *   DM_COUNT:  descriptor entry 数
 *   --no-wait: IDMA 完了待ちをスキップ (fire-and-forget)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <gpfn3.h>

/* IDMA 完了待ち: n_dma + inst_in_ibuf (bits[20:0]) */
static int idma_wait(gpfn3_device_id_t dev, int max_cnt)
{
    /* Phase 1: kick 反映を確認 */
    for (int i = 0; i < 100; i++) {
        uint64_t stat;
        if (gpfn3_read_pio(dev, 0x018, &stat) != GPFN3_SUCCESS) return -1;
        if ((stat & 0x1fffffULL) != 0) break;
    }
    /* Phase 2: 完了 */
    for (int i = 0; i < max_cnt; i++) {
        uint64_t stat;
        if (gpfn3_read_pio(dev, 0x018, &stat) != GPFN3_SUCCESS) return -1;
        if ((stat & 0x1fffffULL) == 0) return 0;
    }
    fprintf(stderr, "FAIL: idma_wait timeout\n");
    return -1;
}

int main(int argc, char **argv)
{
    int no_wait = 0;
    int argp = 1;
    const char *dm_index_s = NULL;
    const char *dm_count_s = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--no-wait") == 0) {
            no_wait = 1;
        } else if (dm_index_s == NULL) {
            dm_index_s = argv[i];
        } else if (dm_count_s == NULL) {
            dm_count_s = argv[i];
        } else {
            fprintf(stderr, "FAIL: unexpected argument '%s'\n", argv[i]);
            return 1;
        }
    }
    (void)argp;

    if (dm_index_s == NULL || dm_count_s == NULL) {
        fprintf(stderr,
            "Usage: kick-idma2 DM_INDEX DM_COUNT [--no-wait]\n"
            "  DM_INDEX:  descriptor memory の開始 index\n"
            "  DM_COUNT:  descriptor entry 数\n"
            "  --no-wait: IDMA 完了待ちをスキップ\n");
        return 1;
    }

    size_t dm_index = (size_t)strtoul(dm_index_s, NULL, 0);
    size_t dm_count = (size_t)strtoul(dm_count_s, NULL, 0);

    gpfn3_device_id_t dev = gpfn3_get_device_id(0);
    if (dev == GPFN3_INVALID_DEVICE_ID) {
        fprintf(stderr, "FAIL: gpfn3_get_device_id\n");
        return 1;
    }

    gpfn3_error_t err = gpfn3_kick_inst_dma2(dev, dm_index, dm_count);
    if (err != GPFN3_SUCCESS) {
        fprintf(stderr, "FAIL: gpfn3_kick_inst_dma2 rc=%d\n", (int)err);
        gpfn3_close_device(dev);
        return 1;
    }

    if (!no_wait) {
        if (idma_wait(dev, 5000) < 0) {
            gpfn3_close_device(dev);
            return 1;
        }
    }

    fprintf(stderr, "OK: kick_inst_dma2 dm_index=%zu dm_count=%zu%s\n",
            dm_index, dm_count, no_wait ? " (no-wait)" : "");

    gpfn3_close_device(dev);
    return 0;
}
