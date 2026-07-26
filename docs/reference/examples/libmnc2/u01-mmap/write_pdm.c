/* write_pdm.c — PDM mmap 書き込み (stdin or 引数から)
 * Usage: write-pdm ADDR [DATA...]
 *   DATA: f64:1.0  f32:1.0  bf16:1.0  0xHH..  or (省略時) stdin binary
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <gpfn3.h>
#include "parse_u64.h"
#include "data_parse.h"

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
    if (argc < 2) {
        fprintf(stderr,
            "Usage: write-pdm ADDR [DATA...]\n"
            "  ADDR: PDM バイトオフセット\n"
            "  DATA: f64:1.0  f32:1.0  bf16:1.0  0xHH..  or (省略時) stdin binary\n");
        return 1;
    }
    uint64_t addr = parse_u64(argv[1]);
    int data_start = 2;

    struct buf b;
    buf_init(&b);

    if (data_start >= argc) {
        /* 引数なし: stdin からバイナリ */
        if (read_stdin_binary(&b) < 0)
            return 1;
    } else {
        /* 引数をパース (send-dma と同じ形式) */
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
        fprintf(stderr, "エラー: 書き込みデータが空\n");
        return 1;
    }

    gpfn3_device_id_t dev = open_device();
    uint64_t *pdm = gpfn3_map_pdm(dev);
    if (!pdm) {
        fprintf(stderr, "エラー: gpfn3_map_pdm failed\n");
        gpfn3_close_device(dev);
        return 1;
    }

    uint8_t *base = (uint8_t *)pdm;
    memcpy(base + addr, b.data, b.len);

    gpfn3_unmap_pdm(dev, pdm);
    gpfn3_close_device(dev);
    free(b.data);
    return 0;
}
