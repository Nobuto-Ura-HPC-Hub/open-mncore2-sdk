/* ex_dm_pdm.c — PDM 上の並びが添字の交換でどう変わるか
 *
 * これは行列の転置ではない。行列演算の機能ではなく、起きるのは 2 次元配列の
 * 添字の交換だけである。命令そのものの説明は 68-dmwrite-dmread を参照。
 *
 * PDM に置いたデータを 4096 PE へ 4 語ずつ配り、MAB 内で PE 番号と語番号を
 * 交換して、PDM へ戻す。host から見た並びの変化を確認する。
 *
 * カーネルを 2 本走らせる。並びの変化が命令によるものか、配って集める経路に
 * よるものかを切り分けるため。
 *
 *   段階 1  dm_pdm_nop.idma.dat  配る -> 集める             出力 == 入力 のはず
 *   段階 2  dm_pdm.idma.dat      配る -> 交換 -> 集める     添字が交換されるはず
 *
 * 段階 1 が恒等写像なら、段階 2 で観測される変化はすべて命令に由来する。
 *
 * PDM の並び。p を通し PE 番号 (0..4095)、c を語番号 (0..3) として
 *
 *   入力  PDM[4096 * c + p]
 *   出力  PDM[16384 + 4096 * c + p]
 *
 * 交換は MAB 内で閉じるので、s = p の下位 2 ビット (MAB 内の PE 番号)、
 * b = p - s (MAB の先頭) と置くと、段階 2 の移動は
 *
 *   入力 PDM[4096 * s + b + c]  ->  出力 PDM[16384 + 4096 * c + b + s]
 *
 * s と c が入れ替わる。PDM のアドレスをビットで書くと bit13-12 (4096 の位) と
 * bit1-0 (下位 2 ビット) の交換になる。
 *
 * 値は入力の PDM 添字がそのまま読める形 (PDM[i] = i + 1) にしてある。出力に
 * 現れた値から、それが入力のどこにあったかが分かる。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "mnc2.h"

#define N_PE       4096                       /* 全 PE */
#define N_WORD     4                          /* PE あたりの語数 */
#define ELEM_COUNT (N_PE * N_WORD)            /* 16384 */
#define ELEM_BYTES (ELEM_COUNT * sizeof(uint64_t))
#define OFFSET_IN  0
#define OFFSET_OUT ((uint64_t)ELEM_COUNT * 8) /* PDM word 16384 */
#define SEND_TAG   0x10
#define RECV_TAG   0x36

/* 入力の PDM 添字 -> 置く値 */
static uint64_t value_at(int index)
{
    return (uint64_t)index + 1;
}

/* 段階 2 で 出力 PDM[N_PE * c + p] に来るはずの値。
   交換は MAB 内で閉じるので、s = p & 3 が MAB 内の PE 番号、b = p & ~3 が MAB の先頭。
   出力の (PE p, 語 c) には、入力の (PE b + c, 語 s) が来る。 */
static uint64_t expected_swapped(int c, int p)
{
    int s = p & 3;
    int b = p & ~3;
    return value_at(N_PE * s + b + c);
}

/* デバイスを開いて 1 本のカーネルを走らせ、出力を out に受ける。
   2 本のカーネルが同じ wait タグを使うので、毎回開き直して独立させる。 */
static int run_kernel(const char* kernel_path, const uint64_t* in, uint64_t* out,
                      const char** backend_name)
{
    mnc2_device_t dev = mnc2_open(0);
    if (dev == NULL) {
        printf("FAIL: mnc2_open\n");
        return 1;
    }
    if (backend_name)
        *backend_name = mnc2_get_backend_name(dev);

    int rc = 1;
    void* sendbuf = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    void* recvbuf = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    if (sendbuf == NULL || recvbuf == NULL) {
        printf("FAIL: alloc\n");
        goto done;
    }
    memcpy(sendbuf, in, ELEM_BYTES);
    memset(recvbuf, 0, ELEM_BYTES);

    rc = mnc2_send(dev, sendbuf, OFFSET_IN, ELEM_BYTES, SEND_TAG);
    if (rc != MNC2_SUCCESS) {
        printf("FAIL: send rc=%d\n", rc);
        goto done;
    }

    mnc2_kernel_t kernel = mnc2_load_kernel(dev, kernel_path);
    if (kernel == NULL) {
        printf("FAIL: load_kernel(%s)\n", kernel_path);
        rc = 1;
        goto done;
    }
    rc = mnc2_exec_kernel(kernel);
    mnc2_free_kernel(kernel);
    if (rc != MNC2_SUCCESS) {
        printf("FAIL: exec_kernel rc=%d\n", rc);
        goto done;
    }

    rc = mnc2_recv(dev, recvbuf, OFFSET_OUT, ELEM_BYTES, RECV_TAG);
    if (rc != MNC2_SUCCESS) {
        printf("FAIL: recv rc=%d\n", rc);
        goto done;
    }
    memcpy(out, recvbuf, ELEM_BYTES);
    rc = 0;

done:
    if (sendbuf) mnc2_free_host_buffer(dev, sendbuf, ELEM_BYTES);
    if (recvbuf) mnc2_free_host_buffer(dev, recvbuf, ELEM_BYTES);
    mnc2_close(dev);
    return rc;
}

int main(void)
{
    printf("=== 69-dmwrite-dmread-pdm: PDM 上の並びが添字の交換でどう変わるか ===\n\n");

    uint64_t* in = malloc(ELEM_BYTES);
    uint64_t* out = malloc(ELEM_BYTES);
    if (in == NULL || out == NULL) {
        printf("FAIL: malloc\n");
        free(in); free(out);
        return 1;
    }
    for (int i = 0; i < ELEM_COUNT; i++)
        in[i] = value_at(i);

    /* --- 段階 1: 命令を挟まない。経路だけで並びが変わらないことを見る --- */
    printf("[段階 1] 配る -> 集める (dmwrite / dmread なし)\n");
    const char* backend = NULL;
    if (run_kernel("_build/dm_pdm_nop.idma.dat", in, out, &backend) != 0) {
        free(in); free(out);
        return 1;
    }
    printf("  backend: %s\n", backend);
    printf("  入力 PDM[0..%d]、出力 PDM[%d..%d]\n",
           ELEM_COUNT - 1, ELEM_COUNT, ELEM_COUNT * 2 - 1);

    int bad_nop = 0;
    for (int i = 0; i < ELEM_COUNT; i++) {
        if (out[i] != in[i]) {
            if (bad_nop < 5)
                printf("  MISMATCH: 出力[%d] 期待 %" PRIu64 "  実際 %" PRIu64 "\n",
                       i, in[i], out[i]);
            bad_nop++;
        }
    }
    if (bad_nop) {
        printf("FAIL: 経路だけで %d 箇所が変わった\n", bad_nop);
        free(in); free(out);
        return 1;
    }
    printf("  出力が入力と全 %d 要素で一致。配って集める経路は並びを変えない\n\n",
           ELEM_COUNT);

    /* --- 段階 2: 命令を挟む --- */
    printf("[段階 2] 配る -> 添字の交換 -> 集める\n\n");
    if (run_kernel("_build/dm_pdm.idma.dat", in, out, NULL) != 0) {
        free(in); free(out);
        return 1;
    }

    printf("  出力の先頭 4 語と、その値が入力のどこにあったか\n\n");
    printf("    出力の位置      値      入力での位置\n");
    for (int p = 0; p < 4; p++) {
        uint64_t v = out[p];
        printf("    PDM[%5d]  %8" PRIu64 "      PDM[%d]\n",
               ELEM_COUNT + p, v, (int)v - 1);
    }
    printf("\n    %d 離れた 4 つの値が連続位置に集まっている\n\n", N_PE);

    printf("  出力 PE0 の 4 語 (語 c は PDM[%d + %d * c] にある)\n\n", ELEM_COUNT, N_PE);
    printf("    語  出力の位置      値      入力での位置\n");
    for (int c = 0; c < N_WORD; c++) {
        uint64_t v = out[N_PE * c];
        printf("    %d   PDM[%5d]  %8" PRIu64 "      PDM[%d]\n",
               c, ELEM_COUNT + N_PE * c, v, (int)v - 1);
    }
    printf("\n    入力の連続 4 語が %d 刻みに散っている (上とちょうど逆)\n\n", N_PE);

    int bad_swap = 0;
    for (int c = 0; c < N_WORD; c++) {
        for (int p = 0; p < N_PE; p++) {
            uint64_t want = expected_swapped(c, p);
            uint64_t got = out[N_PE * c + p];
            if (got != want) {
                if (bad_swap < 5)
                    printf("  MISMATCH: 語%d PE%d  期待 %" PRIu64 "  実際 %" PRIu64 "\n",
                           c, p, want, got);
                bad_swap++;
            }
        }
    }

    free(in);
    free(out);

    if (bad_swap) {
        printf("FAIL: %d 箇所が期待値と違う\n", bad_swap);
        return 1;
    }

    printf("  入力 PDM[%d*s + b + c] -> 出力 PDM[%d + %d*c + b + s] が全 %d 要素で成立\n",
           N_PE, ELEM_COUNT, N_PE, ELEM_COUNT);
    printf("  (s = PE 番号の下位 2 ビット、b = MAB の先頭、c = 語番号)\n\n");
    printf("段階 1 が恒等写像だったので、この変化はすべて命令によるもの\n\n");
    printf("PASS\n");
    return 0;
}
