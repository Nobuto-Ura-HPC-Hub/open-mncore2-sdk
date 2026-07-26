/* set-capture.c
 * Usage: set-capture <B|C|D|E> <instruction_threshold>
 * 例: ./set-capture B 500
 *   → Capture B を命令カウンタ 500 で trigger するよう arm
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gpfn3.h>

int main(int argc, char** argv)
{
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <B|C|D|E> <threshold>\n", argv[0]);
        return 1;
    }

    enum GPFN3_CAPTURE_SEL sel;
    switch (argv[1][0]) {
    case 'B': case 'b': sel = GPFN3_CAPTURE_B; break;
    case 'C': case 'c': sel = GPFN3_CAPTURE_C; break;
    case 'D': case 'd': sel = GPFN3_CAPTURE_D; break;
    case 'E': case 'e': sel = GPFN3_CAPTURE_E; break;
    default:
        fprintf(stderr, "FAIL: unknown capture '%s' (B/C/D/E)\n", argv[1]);
        return 1;
    }

    uint64_t threshold = strtoull(argv[2], NULL, 0);

    gpfn3_device_id_t dev = gpfn3_get_device_id(0);
    if (dev == GPFN3_INVALID_DEVICE_ID) {
        fprintf(stderr, "FAIL: gpfn3_get_device_id\n");
        return 1;
    }

    gpfn3_error_t err = gpfn3_capture_set_inst(dev, sel, threshold);
    if (err != GPFN3_SUCCESS) {
        fprintf(stderr, "FAIL: gpfn3_capture_set_inst rc=%d\n", (int)err);
        gpfn3_close_device(dev);
        return 1;
    }

    printf("Capture %c armed at threshold %llu\n",
           argv[1][0], (unsigned long long)threshold);

    gpfn3_close_device(dev);
    return 0;
}
