/* recv_dma.c — DMA 受信 (PDM → Host) → stdout (バイナリ)
 * Usage: recv-dma ADDR LEN
 *   ADDR: PDM バイトオフセット
 *   LEN:  バイト数 (4 の倍数)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <gpfn3.h>
#include "parse_u64.h"
#include "channel_opt.h"

static gpfn3_device_id_t open_device(void)
{
    const char *env = getenv("GPFN3_DEVICE");
    int num = env ? atoi(env) : 0;
    gpfn3_device_id_t dev = gpfn3_get_device_id(num);
    if (dev == GPFN3_INVALID_DEVICE_ID) {
        fprintf(stderr, "エラー: デバイス %d を開けない\n", num);
        exit(1);
    }
    return dev;
}

int main(int argc, char **argv)
{
    int channel = 0;  /* デフォルト: PDM (CH0) */
    if (extract_channel_opt(&argc, argv, &channel) < 0) return 1;

    if (argc < 3) {
        fprintf(stderr,
            "Usage: recv-dma [--channel NAME] ADDR LEN\n"
            "%s"
            "  ADDR: 受信元メモリのバイトオフセット\n"
            "  LEN:  バイト数 (4 の倍数)\n",
            CHANNEL_HELP_RECV);
        return 1;
    }
    uint64_t pdm_offset = parse_u64(argv[1]);
    size_t len = (size_t)parse_u64(argv[2]);

    if (len == 0 || len % 4 != 0) {
        fprintf(stderr, "エラー: LEN は 4 の倍数で > 0\n");
        return 1;
    }

    gpfn3_device_id_t dev = open_device();

    void *dma_buf = gpfn3_allocate_dma_memory(dev, len);
    if (!dma_buf) {
        fprintf(stderr, "エラー: gpfn3_allocate_dma_memory(%zu) failed\n", len);
        gpfn3_close_device(dev);
        return 1;
    }
    memset(dma_buf, 0, len);

    gpfn3_error_t err = gpfn3_set_regset(dev, 1, dma_buf, pdm_offset);
    if (err != GPFN3_SUCCESS) {
        fprintf(stderr, "エラー: gpfn3_set_regset failed: %d\n", err);
        gpfn3_free_dma_memory(dev, dma_buf, len);
        gpfn3_close_device(dev);
        return 1;
    }

    struct gpfn3_data_dma_kick_t dma;
    memset(&dma, 0, sizeof(dma));
    dma.direction = GPFN3_DMA_DIRECTION_FROM_DEVICE;
    dma.regset    = 1;
    dma.channel   = (enum GPFN3_DMA_CHANNEL)channel;
    dma.wd        = 0;  /* 無条件 DMA 開始 */
    dma.size      = len / 4;

    err = gpfn3_kick_data_dma(dev, dma);
    if (err != GPFN3_SUCCESS) {
        fprintf(stderr, "エラー: gpfn3_kick_data_dma failed: %d\n", err);
        gpfn3_free_dma_memory(dev, dma_buf, len);
        gpfn3_close_device(dev);
        return 1;
    }

    /* DMA 完了待ち
       Phase 1: kick 反映 (active または n_dma > 0) を確認
       Phase 2: 完了 (全チャネル 0) を待つ
       マスク: active (bit 14) + n_dma (bits[4:0]) */
    #define DDMA_BUSY_MASK  0x401f401f401f401fULL
    for (int i = 0; i < 100; i++) {
        uint64_t stat;
        gpfn3_read_pio(dev, 0x038, &stat);
        if ((stat & DDMA_BUSY_MASK) != 0) break;
    }
    for (int i = 0; i < 1500; i++) {
        uint64_t stat;
        gpfn3_read_pio(dev, 0x038, &stat);
        if ((stat & DDMA_BUSY_MASK) == 0)
            goto dma_done;
    }
    fprintf(stderr, "エラー: ddma_wait timeout\n");
    gpfn3_free_dma_memory(dev, dma_buf, len);
    gpfn3_close_device(dev);
    return 1;

dma_done:
    ;
    ssize_t written = write(STDOUT_FILENO, dma_buf, len);
    if (written < 0 || (size_t)written != len) {
        fprintf(stderr, "エラー: write failed\n");
        gpfn3_free_dma_memory(dev, dma_buf, len);
        gpfn3_close_device(dev);
        return 1;
    }

    gpfn3_free_dma_memory(dev, dma_buf, len);
    gpfn3_close_device(dev);
    return 0;
}
