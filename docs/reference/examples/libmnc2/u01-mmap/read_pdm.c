/* read_pdm.c — PDM mmap 読み出し → stdout (バイナリ)
 * Usage: read-pdm ADDR [LEN]
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
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
    if (argc < 2) {
        fprintf(stderr, "Usage: read-pdm ADDR [LEN]\n");
        return 1;
    }
    uint64_t addr = parse_u64(argv[1]);
    size_t len = (argc >= 3) ? (size_t)parse_u64(argv[2]) : 8;

    gpfn3_device_id_t dev = open_device();
    uint64_t *pdm = gpfn3_map_pdm(dev);
    if (!pdm) {
        fprintf(stderr, "エラー: gpfn3_map_pdm failed\n");
        gpfn3_close_device(dev);
        return 1;
    }

    const uint8_t *base = (const uint8_t *)pdm;
    ssize_t written = write(STDOUT_FILENO, base + addr, len);
    if (written < 0 || (size_t)written != len) {
        fprintf(stderr, "エラー: write failed\n");
        gpfn3_unmap_pdm(dev, pdm);
        gpfn3_close_device(dev);
        return 1;
    }

    gpfn3_unmap_pdm(dev, pdm);
    gpfn3_close_device(dev);
    return 0;
}
