/* ex_dm_swap.c — dmwrite と dmread による MAB 内の添字の交換
 *
 * これは行列の転置ではない。行列演算の機能ではなく、起きるのは 2 次元配列の
 * 添字の交換だけで、値が行列を表しているかどうかとは関係がない。
 *
 * s を MAB 内の PE 番号 (0..3)、c を語番号 (0..3) として、
 *
 *   入力を A[s][c]、出力を B[s][c] と書くと   B[s][c] = A[c][s]
 *
 * 使う 2 命令 (括弧内は dev manual での名称):
 *
 *   dmwrite $lm0v $lx0   fp64 行列レジスタ書き込み       (3.6.10.1)
 *   dmread  $lx0 $lm30v  fp64 行列レジスタ転置読み出し   (3.6.11.1)
 *
 * dmread の名称に「転置」が入っているが、行列としての転置演算ではなく、書き込みと
 * 読み出しで行と列の対応が入れ替わることを指している。
 *
 * MAB0 と MAB1 の 2 つに値を置く。添字の交換が MAB 内で閉じていて MAB をまたがない
 * ことを、同時に確認するため。
 *
 * 値は MAB 番号・PE 番号・語番号が 16 進の桁でそのまま読める形にしてある。
 *
 *   MAB m の PE 番号 s の 語番号 c = 0xA<m><s><c>
 *
 * 交換されると下 2 桁だけが入れ替わって 0xA<m><c><s> になる。MAB の桁が動かない
 * ことが、MAB 内で閉じていることの確認になる。
 *
 * PDM は使わない。交換は MAB 内で完結するので、4096 PE へ配る経路も回収する経路も
 * 要らない。値の設定と読み出しは mnc2_debug_write / mnc2_debug_read で行う。
 * この 2 つは emu 専用 API なので、この example は emu:lib でのみ動く。
 *
 * PDM を経由したときに PDM 上の並びがどう変わるかは 69-dmwrite-dmread-pdm を参照。
 *
 * 行列レジスタの中身 (交換の途中経過) は host 側からは読めない。mnc2_debug_read が
 * 扱えるメモリ領域に行列レジスタが含まれないため。途中経過まで見たいときは
 * data/dm_swap_dset.vsm を gpfn3_package_main で直接実行する (README 参照)。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "mnc2.h"

#define N_MAB   2   /* MAB0 と MAB1 */
#define N_PE    4   /* MAB 内の PE 数 */
#define N_WORD  4   /* fp64 行列データは 4 行 4 列 */

/* vsm 側の入力は $lm0v = u32 アドレス 0, 2, 4, 6。
   出力は $lm30v = u32 アドレス 30, 32, 34, 36。
   mnc2_debug_write / mnc2_debug_read の addr は u64 単位なので、
   u32 アドレスの半分になる。 */
#define IN_ADDR   0
#define OUT_ADDR  15

/* MAB m の PE 番号 s の 語番号 c。交換前 */
static uint64_t value_in(int m, int s, int c)
{
    return 0xA000ULL | ((uint64_t)m << 8) | ((uint64_t)s << 4) | (uint64_t)c;
}

/* 同じ位置の交換後の期待値。下 2 桁が入れ替わる */
static uint64_t value_out(int m, int s, int c)
{
    return 0xA000ULL | ((uint64_t)m << 8) | ((uint64_t)c << 4) | (uint64_t)s;
}

static void print_table(const char* title, int mab, const uint64_t v[N_PE][N_WORD])
{
    printf("  %s (MAB%d)   語0    語1    語2    語3\n", title, mab);
    for (int s = 0; s < N_PE; s++) {
        printf("      PE%d       ", s);
        for (int c = 0; c < N_WORD; c++)
            printf("  %04" PRIx64, v[s][c]);
        printf("\n");
    }
}

int main(void)
{
    printf("=== 68-dmwrite-dmread: MAB 内で PE 番号と語番号を交換する ===\n\n");

    mnc2_device_t dev = mnc2_open(0);
    if (dev == NULL) {
        printf("FAIL: mnc2_open\n");
        return 1;
    }
    printf("backend: %s\n\n", mnc2_get_backend_name(dev));

    /* --- 値を置く --- */
    printf("[step1] debug_write: MAB0 と MAB1 の 4 PE の LM0 先頭 4 語に値を設定\n\n");
    for (int m = 0; m < N_MAB; m++) {
        uint64_t shown[N_PE][N_WORD];
        for (int s = 0; s < N_PE; s++) {
            uint64_t in[N_WORD];
            for (int c = 0; c < N_WORD; c++) {
                in[c] = value_in(m, s, c);
                shown[s][c] = in[c];
            }
            mnc2_loc_t loc = { 0, 0, 0, m, s };
            int rc = mnc2_debug_write(dev, MNC2_MEM_LM0, &loc, IN_ADDR, N_WORD, in);
            if (rc != MNC2_SUCCESS) {
                printf("FAIL: debug_write MAB%d PE%d rc=%d\n", m, s, rc);
                mnc2_close(dev);
                return 1;
            }
        }
        print_table("入力", m, shown);
        printf("\n");
    }

    /* --- 交換 --- */
    printf("[step2] exec_kernel: dmwrite $lm0v $lx0 -> dmread $lx0 $lm30v\n\n");
    mnc2_kernel_t kernel = mnc2_load_kernel(dev, "_build/dm_swap.idma.dat");
    if (kernel == NULL) {
        printf("FAIL: load_kernel\n");
        mnc2_close(dev);
        return 1;
    }
    int rc = mnc2_exec_kernel(kernel);
    mnc2_free_kernel(kernel);
    if (rc != MNC2_SUCCESS) {
        printf("FAIL: exec_kernel rc=%d\n", rc);
        mnc2_close(dev);
        return 1;
    }

    /* --- 結果を読んで期待値と比較 --- */
    printf("[step3] debug_read: 交換先の 4 語を読む\n\n");
    int mismatch = 0;
    for (int m = 0; m < N_MAB; m++) {
        uint64_t got[N_PE][N_WORD];
        for (int s = 0; s < N_PE; s++) {
            mnc2_loc_t loc = { 0, 0, 0, m, s };
            memset(got[s], 0, sizeof(got[s]));
            rc = mnc2_debug_read(dev, MNC2_MEM_LM0, &loc, OUT_ADDR, N_WORD, got[s]);
            if (rc != MNC2_SUCCESS) {
                printf("FAIL: debug_read MAB%d PE%d rc=%d\n", m, s, rc);
                mnc2_close(dev);
                return 1;
            }
        }
        print_table("出力", m, got);
        printf("\n");

        for (int s = 0; s < N_PE; s++) {
            for (int c = 0; c < N_WORD; c++) {
                uint64_t want = value_out(m, s, c);
                if (got[s][c] != want) {
                    printf("  MISMATCH: MAB%d PE%d 語%d  期待 %04" PRIx64
                           "  実際 %04" PRIx64 "\n", m, s, c, want, got[s][c]);
                    mismatch++;
                }
            }
        }
    }

    mnc2_close(dev);

    if (mismatch) {
        printf("FAIL: %d 箇所が期待値と違う\n", mismatch);
        return 1;
    }

    printf("B[s][c] = A[c][s] が %d MAB x %d PE x %d 語 で成立\n",
           N_MAB, N_PE, N_WORD);
    printf("MAB の桁 (上から 2 桁目) が動いていないので、交換は MAB 内で閉じている\n\n");
    printf("PASS\n");
    return 0;
}
