/* pio.c — PIO レジスタ read/write 統合コマンド
 *
 * Usage:
 *   pio ADDR         … read  (pio-read と同等)
 *   pio ADDR VALUE   … write (pio-write と同等)
 */

#include <stdio.h>
#include <stdlib.h>
#include <gpfn3.h>
#include "parse_u64.h"

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
    if (argc < 2 || argc > 3) {
        fprintf(stderr,
            "Usage: pio ADDR [VALUE]\n"
            "  ADDR:  PIO レジスタオフセット\n"
            "  VALUE: 指定時は write、省略時は read\n");
        return 1;
    }

    unsigned int addr = (unsigned int)parse_u64(argv[1]);
    gpfn3_device_id_t dev = open_device();

    if (argc == 2) {
        uint64_t value;
        gpfn3_error_t err = gpfn3_read_pio(dev, addr, &value);
        if (err != GPFN3_SUCCESS) {
            fprintf(stderr, "エラー: gpfn3_read_pio(0x%x) failed: %d\n", addr, err);
            gpfn3_close_device(dev);
            return 1;
        }
        printf("0x%016llx\n", (unsigned long long)value);
    } else {
        uint64_t value = parse_u64(argv[2]);
        gpfn3_error_t err = gpfn3_write_pio(dev, addr, value);
        if (err != GPFN3_SUCCESS) {
            fprintf(stderr, "エラー: gpfn3_write_pio(0x%x, 0x%llx) failed: %d\n",
                    addr, (unsigned long long)value, err);
            gpfn3_close_device(dev);
            return 1;
        }
    }

    gpfn3_close_device(dev);
    return 0;
}
