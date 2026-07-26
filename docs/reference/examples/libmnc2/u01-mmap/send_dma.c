/* send_dma.c — DMA 送信 (Host → PDM)
 * Usage: send-dma ADDR [DATA...]
 *   DATA: f64:1.0  f32:1.0  bf16:1.0  0xHH..  or stdin binary
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <gpfn3.h>
#include "parse_u64.h"
#include "data_parse.h"
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

    if (argc < 2) {
        fprintf(stderr,
            "Usage: send-dma [--channel NAME] ADDR [DATA...]\n"
            "%s"
            "  ADDR: 送信先メモリのバイトオフセット\n"
            "  DATA: f64:1.0  f32:1.0  bf16:1.0  0xHH..  or stdin binary\n",
            CHANNEL_HELP_SEND);
        return 1;
    }
    if (channel == 2) {
        fprintf(stderr, "エラー: CH2 は IDMA 専用のため送信には使えない\n");
        return 1;
    }
    uint64_t pdm_offset = parse_u64(argv[1]);
    int data_start = 2;

    struct buf b;
    buf_init(&b);

    if (data_start >= argc) {
        /* 引数なし: stdin からバイナリ */
        if (read_stdin_binary(&b) < 0)
            return 1;
    } else {
        /* 引数をパース */
        for (int i = data_start; i < argc; i++) {
            const char *s = argv[i];
            if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
                if (parse_hex_literal(&b, s) < 0) return 1;
            } else if (strncmp(s, "f64:", 4) == 0 ||
                       strncmp(s, "f32:", 4) == 0 ||
                       strncmp(s, "bf16:", 5) == 0) {
                if (parse_typed_literal(&b, s) < 0) return 1;
            } else {
                fprintf(stderr, "エラー: 不明な引数: %s\n", s);
                return 1;
            }
        }
    }

    if (b.len == 0) {
        fprintf(stderr, "エラー: 送信データが空\n");
        return 1;
    }

    /* DMA 転送サイズは 4 バイトの倍数が必要 */
    size_t dma_size = b.len;
    if (dma_size % 4 != 0) {
        size_t padded = (dma_size + 3) & ~(size_t)3;
        while (b.len < padded)
            buf_append(&b, "\0", 1);
        dma_size = padded;
    }

    gpfn3_device_id_t dev = open_device();

    void *dma_buf = gpfn3_allocate_dma_memory(dev, dma_size);
    if (!dma_buf) {
        fprintf(stderr, "エラー: gpfn3_allocate_dma_memory(%zu) failed\n", dma_size);
        gpfn3_close_device(dev);
        return 1;
    }

    memcpy(dma_buf, b.data, b.len);

    gpfn3_error_t err = gpfn3_set_regset(dev, 0, dma_buf, pdm_offset);
    if (err != GPFN3_SUCCESS) {
        fprintf(stderr, "エラー: gpfn3_set_regset failed: %d\n", err);
        gpfn3_free_dma_memory(dev, dma_buf, dma_size);
        gpfn3_close_device(dev);
        return 1;
    }

    struct gpfn3_data_dma_kick_t dma;
    memset(&dma, 0, sizeof(dma));
    dma.direction = GPFN3_DMA_DIRECTION_TO_DEVICE;
    dma.regset    = 0;
    dma.channel   = (enum GPFN3_DMA_CHANNEL)channel;
    dma.size      = dma_size / 4;

    err = gpfn3_kick_data_dma(dev, dma);
    if (err != GPFN3_SUCCESS) {
        fprintf(stderr, "エラー: gpfn3_kick_data_dma failed: %d\n", err);
        gpfn3_free_dma_memory(dev, dma_buf, dma_size);
        gpfn3_close_device(dev);
        return 1;
    }

    /* DMA 完了待ち
       kick 後すぐ free すると DMA 読み込み中のバッファが解放される。
       Phase 1: 開始確認, Phase 2: 完了確認。 */
    #define DDMA_BUSY_MASK  0x401f401f401f401fULL
    for (int i = 0; i < 100; i++) {
        uint64_t stat;
        gpfn3_read_pio(dev, 0x038, &stat);
        if ((stat & DDMA_BUSY_MASK) != 0) break;
    }
    for (int i = 0; i < 5000; i++) {
        uint64_t stat;
        gpfn3_read_pio(dev, 0x038, &stat);
        if ((stat & DDMA_BUSY_MASK) == 0) goto dma_done;
    }
    fprintf(stderr, "エラー: ddma_wait timeout\n");
    gpfn3_free_dma_memory(dev, dma_buf, dma_size);
    gpfn3_close_device(dev);
    return 1;

dma_done:
    gpfn3_free_dma_memory(dev, dma_buf, dma_size);
    gpfn3_close_device(dev);
    free(b.data);
    return 0;
}
