/* ex_debug_read_write.c — debug_write + カーネル実行 + debug_read の統合テスト
 *
 * 元ネタ: vsm-linker/examples/11-odd-even-sort/step1_even_msl.vsm
 * d set / d get をすべて C API (mnc2_debug_write / mnc2_debug_read) に置換。
 *
 * フロー:
 *   1. debug_write で各 PE の LM0[0] に初期値を書き込む
 *   2. even_msl カーネルを実行 (msl で左隣から値を取得)
 *   3. debug_read で各 PE の LM0[4] を読み出し、期待値と比較
 *
 * MAB0 (PE0-PE3): 入力 10, 20, 30, 40
 *   msl (PE[i] ← PE[i-1], MAB 内循環):
 *   期待 PE0=40(wrap), PE1=10, PE2=20, PE3=30
 *
 * MAB1 (PE4-PE7): 入力 50, 60, 70, 80
 *   期待 PE4=80(wrap), PE5=50, PE6=60, PE7=70
 *
 * 使い方:
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "mnc2.h"

/* PE ごとの入力値と期待値 */
struct pe_case {
    int mab;
    int pe;
    uint64_t input;
    uint64_t expected;
};

static const struct pe_case cases[] = {
    /* MAB0 */
    { 0, 0, 10, 40 },  /* PE0 ← PE3 (wrap) */
    { 0, 1, 20, 10 },  /* PE1 ← PE0 */
    { 0, 2, 30, 20 },  /* PE2 ← PE1 */
    { 0, 3, 40, 30 },  /* PE3 ← PE2 */
    /* MAB1 */
    { 1, 0, 50, 80 },  /* PE4 ← PE7 (wrap) */
    { 1, 1, 60, 50 },  /* PE5 ← PE4 */
    { 1, 2, 70, 60 },  /* PE6 ← PE5 */
    { 1, 3, 80, 70 },  /* PE7 ← PE6 */
};
#define NUM_CASES (sizeof(cases) / sizeof(cases[0]))

int main(void)
{
    printf("=== 47-debug-read-write: msl stencil test ===\n\n");

    mnc2_device_t dev = mnc2_open(0);
    if (dev == NULL) {
        printf("FAIL: mnc2_open failed\n");
        return 1;
    }
    printf("backend: %s\n\n", mnc2_get_backend_name(dev));

    /* 1. debug_write で各 PE の LM0[0] に初期値を書き込む */
    printf("[step1] debug_write: LM0[0] に初期値を設定\n");
    for (int i = 0; i < (int)NUM_CASES; i++) {
        mnc2_loc_t loc = { 0, 0, 0, cases[i].mab, cases[i].pe };
        int rc = mnc2_debug_write(dev, MNC2_MEM_LM0, &loc, 0, 1, &cases[i].input);
        if (rc != MNC2_SUCCESS) {
            printf("  FAIL: debug_write MAB%d PE%d returned %d\n",
                    cases[i].mab, cases[i].pe, rc);
            mnc2_close(dev);
            return 1;
        }
        printf("  MAB%d PE%d: %" PRIu64 "\n", cases[i].mab, cases[i].pe, cases[i].input);
    }
    printf("\n");

    /* 2. even_msl カーネルを実行 */
    printf("[step2] exec_kernel: even_msl (msl stencil)\n");
    mnc2_kernel_t kernel = mnc2_load_kernel(dev, "_build/even_msl.idma.dat");
    if (kernel == NULL) {
        printf("  FAIL: load_kernel(even_msl.idma.dat)\n");
        mnc2_close(dev);
        return 1;
    }
    int rc = mnc2_exec_kernel(kernel);
    mnc2_free_kernel(kernel);
    if (rc != MNC2_SUCCESS) {
        printf("  FAIL: exec_kernel returned %d\n", rc);
        mnc2_close(dev);
        return 1;
    }
    printf("  OK\n\n");

    /* 3. debug_read で各 PE の LM0[4] を読み出し、期待値と比較
       $lm8 → u32 addr 8 → u64 addr 4 */
    printf("[step3] debug_read: LM0[4] を読み出して検証\n");
    int errors = 0;
    for (int i = 0; i < (int)NUM_CASES; i++) {
        mnc2_loc_t loc = { 0, 0, 0, cases[i].mab, cases[i].pe };
        uint64_t got = 0;
        rc = mnc2_debug_read(dev, MNC2_MEM_LM0, &loc, 4, 1, &got);
        if (rc != MNC2_SUCCESS) {
            printf("  FAIL: debug_read MAB%d PE%d returned %d\n",
                    cases[i].mab, cases[i].pe, rc);
            errors++;
            continue;
        }

        const char* status = (got == cases[i].expected) ? "OK" : "MISMATCH";
        printf("  MAB%d PE%d: got=%" PRIu64 " expected=%" PRIu64 " [%s]\n",
               cases[i].mab, cases[i].pe, got, cases[i].expected, status);
        if (got != cases[i].expected)
            errors++;
    }

    printf("\n");
    mnc2_close(dev);

    if (errors == 0) {
        printf("ALL PASS\n");
        return 0;
    } else {
        printf("FAILED (%d errors)\n", errors);
        return 1;
    }
}
