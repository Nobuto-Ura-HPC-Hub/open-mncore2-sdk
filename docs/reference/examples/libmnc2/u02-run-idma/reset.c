/* reset.c — gpfn3 device software reset ツール
 *
 * モード:
 *   (引数なし)   gpfn3_reset_device() を 1 発呼ぶ完全リセット。ただしチップが命令を
 *                実行できない状態や DMA キューに中途半端な設定が残った状態では、
 *                ドライバ内部の命令カウンタ待ち(上限なし)で戻ってこないことがある。
 *   --pio-reset  SPI-A リセットレジスタ(PIO 0x050)を直接書くだけの軽量リセット。
 *                命令カウンタを待たないので必ず戻る。DMA キューの掃除が目的で、
 *                固まった通常リセットを Ctrl-C で kill した後の復旧に使える。
 *   -h/--help    使い方
 *   --explain    なぜ通常リセットが固まりうるか / --pio-reset が何をするかの詳しい説明
 *
 * run_idma.c の reset block と同じく、リセット前後に 1 ms の usleep を入れて
 * apb/pio write との衝突を避ける。
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <gpfn3.h>

/* SPI-A リセットレジスタ (PIO 0x050)。gpfn3_reset_device が最初に書くのと同じビットで、
 * DMAQ_CLEAR が DMA キューを掃除する。PIO 書き込みだけなので上限のない待ちを含まない。 */
#define SPIA_RESET             0x050u
#define SPIA_RESET_SPIA_MODULE (1ULL << 63)
#define SPIA_RESET_DMAQ_CLEAR  (1ULL << 62)
#define SPIA_RESET_FRB_RESET   (1ULL << 61)

static void print_help(const char* argv0)
{
    printf("Usage: %s [OPTION]\n", argv0);
    printf("gpfn3 device software reset.\n\n");
    printf("Options:\n");
    printf("  (none)        full reset via gpfn3_reset_device().\n");
    printf("                May hang if the chip cannot execute instructions or the\n");
    printf("                DMA queue is left in a partial state (see --explain).\n");
    printf("  --pio-reset   lightweight SPI-A reset (writes PIO 0x050 directly).\n");
    printf("                Clears the DMA queue and always returns (no unbounded wait).\n");
    printf("                Use this to recover after killing a hung full reset.\n");
    printf("  --explain     print why the full reset can hang and what --pio-reset does.\n");
    printf("  -h, --help    show this help.\n");
    printf("\nRun '%s --explain' for the detailed background.\n", argv0);
}

static void print_explain(void)
{
    printf(
"Why the default reset can hang, and what --pio-reset does\n"
"========================================================\n"
"\n"
"The default reset calls gpfn3_reset_device(). After resetting, that routine\n"
"kicks a short instruction sequence and polls the PMC instruction counter until\n"
"it advances by a fixed amount. That poll loop has no upper bound: it assumes\n"
"the chip can execute those instructions.\n"
"\n"
"This is exactly the assumption that breaks in the situations you call reset for:\n"
"\n"
"  (a) The chip is wedged and cannot execute instructions. The counter never\n"
"      advances, so the loop spins forever.\n"
"  (b) The DMA queue has a partial/leftover setup. The counter can be pushed\n"
"      past the awaited value, and because the loop waits for an exact match it\n"
"      never sees it. The chip is alive but the reset still never returns.\n"
"\n"
"A recovery API that depends on the very thing it is trying to recover (the chip\n"
"being able to run instructions) is the core problem. Ctrl-C does not help: the\n"
"busy loop lives inside the driver library and cannot be interrupted to return,\n"
"so Ctrl-C just kills the whole process. Running the default reset again hits the\n"
"same code and hangs again.\n"
"\n"
"--pio-reset takes a different path. It writes the SPI-A reset register (PIO\n"
"0x050) directly: SPIA_MODULE | DMAQ_CLEAR | FRB_RESET, then two dummy writes.\n"
"This is the same SPI-A reset the full path does first, but it does NOT run the\n"
"instruction-kick + counter poll afterwards. PIO writes contain no unbounded\n"
"wait, so --pio-reset always returns.\n"
"\n"
"What it fixes and what it cannot:\n"
"  - case (b), a dirty DMA queue on a live chip: DMAQ_CLEAR flushes the queue,\n"
"    so --pio-reset recovers it. This is the common case.\n"
"  - case (a), a truly wedged chip: SPI-A reset is more forceful than the full\n"
"    path's later steps, but reviving a dead chip is not guaranteed. There is no\n"
"    software path that reliably recovers it.\n"
"\n"
"If --pio-reset does not bring the device back, the remaining options are outside\n"
"this software stack: a PCIe function-level reset\n"
"(/sys/bus/pci/devices/<BDF>/reset), a PCI remove + rescan, or reloading the\n"
"kernel module. These depend on FLR support and on your environment's\n"
"permissions.\n");
}

static int do_pio_reset(gpfn3_device_id_t dev)
{
    const uint64_t bits = SPIA_RESET_SPIA_MODULE | SPIA_RESET_DMAQ_CLEAR | SPIA_RESET_FRB_RESET;
    gpfn3_error_t err;

    usleep(1000);

    err = gpfn3_write_pio(dev, SPIA_RESET, bits);
    if (err != GPFN3_SUCCESS) {
        fprintf(stderr, "FAIL: gpfn3_write_pio(SPIA_RESET) rc=%d\n", (int)err);
        return 1;
    }
    /* dummy write x2 (書き込み順序の保証。完了を待つループではない) */
    err = gpfn3_write_pio(dev, SPIA_RESET, 0);
    if (err != GPFN3_SUCCESS) {
        fprintf(stderr, "FAIL: gpfn3_write_pio(SPIA_RESET dummy 1) rc=%d\n", (int)err);
        return 1;
    }
    err = gpfn3_write_pio(dev, SPIA_RESET, 0);
    if (err != GPFN3_SUCCESS) {
        fprintf(stderr, "FAIL: gpfn3_write_pio(SPIA_RESET dummy 2) rc=%d\n", (int)err);
        return 1;
    }

    usleep(1000);

    printf("SPI-A reset (DMA queue clear) OK\n");
    return 0;
}

static int do_full_reset(gpfn3_device_id_t dev)
{
    usleep(1000);
    gpfn3_error_t err = gpfn3_reset_device(dev);
    usleep(1000);

    if (err != GPFN3_SUCCESS) {
        fprintf(stderr, "FAIL: gpfn3_reset_device rc=%d\n", (int)err);
        return 1;
    }
    printf("device reset OK\n");
    return 0;
}

int main(int argc, char** argv)
{
    int pio_reset = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_help(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--explain") == 0) {
            print_explain();
            return 0;
        } else if (strcmp(argv[i], "--pio-reset") == 0) {
            pio_reset = 1;
        } else {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            print_help(argv[0]);
            return 2;
        }
    }

    gpfn3_device_id_t dev = gpfn3_get_device_id(0);
    if (dev == GPFN3_INVALID_DEVICE_ID) {
        fprintf(stderr, "FAIL: gpfn3_get_device_id\n");
        return 1;
    }

    int rc = pio_reset ? do_pio_reset(dev) : do_full_reset(dev);

    gpfn3_close_device(dev);
    return rc;
}
