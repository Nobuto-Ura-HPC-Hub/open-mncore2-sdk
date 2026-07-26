/* ex_l1bmp_2u64.c — l1bmp の 1 u64 版 / 2 u64 版で L1BM の位置とレジスタの対応を実測する
 *
 * L1BM[0..7] に区別できる値を置いて 2 つの l1bmp を実行し、どの L1BM 位置が
 * どのレジスタに入ったかを出力する。
 *
 *   l1bmp $llb0 $llr0v   2 u64 版 (dev manual 3.6.8.7) -> $lr0 から $lr14
 *   l1bmp $lb0  $lr16v   1 u64 版 (dev manual 3.6.8.6) -> $lr16 から $lr22
 *
 * 回収は collect 経路 (l1bmd 結合 + l2bm + mvp) を使い、mnc2_recv で受ける。
 * 3 backend すべてで同じ検証ができる。回収するのは 2 u64 版の
 * $lr0 $lr2 $lr4 $lr6 の 4 本で、これで L1BM の位置との対応が確定する。
 *
 * emu では追加で mnc2_debug_read(GRF0) も使い、残り 8 本と、どちらの l1bmp も
 * 書かないレジスタ ($lr24 から $lr30) の見え方まで出す。device では
 * debug_read が使えないので、この追加分は出ない。
 *
 * 期待値で PASS / FAIL を決めない観察型。読んだ内容を出して OBSERVED 行で解釈を述べる。
 *
 * 値は 1 バイトを 8 回繰り返した形 (0xA3 なら 0xA3A3A3A3A3A3A3A3) にしてある。
 * バイト順が入れ替わっても同じ値に見えるので、どの L1BM 位置から来たかが読み取れる。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "mnc2.h"

#define IN_COUNT   512                        /* mvb/n512 が読む範囲 */
#define IN_BYTES   (IN_COUNT * sizeof(uint64_t))
#define N_SRC      8                          /* L1BM[0..7] に置く値の数 */
#define N_PE       4096
#define N_BLOCK    4                          /* collect した $lr0 $lr2 $lr4 $lr6 */
#define OUT_COUNT  (N_BLOCK * N_PE)
#define OUT_BYTES  (OUT_COUNT * sizeof(uint64_t))
#define N_DBG      16                         /* debug_read する u64 数 = $lr0 から $lr30 */
#define SEND_TAG   0x10
#define RECV_TAG   0x36

/* collect した 4 ブロックがどのレジスタか */
static const int block_reg[N_BLOCK] = {0, 2, 4, 6};

/* 値 -> L1BM の添字。0xAk を 8 バイト並べた形なら k を返す。それ以外は -1 */
static int src_index_of(uint64_t v)
{
    unsigned b = (unsigned)(v & 0xFF);
    for (int i = 1; i < 8; i++)
        if (((v >> (8 * i)) & 0xFF) != b) return -1;
    if ((b & 0xF0) != 0xA0) return -1;
    return (int)(b & 0x0F);
}

static void print_reg(int regno, uint64_t v)
{
    int idx = src_index_of(v);
    if (idx >= 0)
        printf("   $lr%-2d = 0x%016" PRIx64 "  <- L1BM[+%d]\n", regno, v, idx);
    else
        printf("   $lr%-2d = 0x%016" PRIx64 "  <- (どの L1BM 位置でもない)\n", regno, v);
}

int main(void)
{
    printf("[test] l1bmp 1 u64 版 (3.6.8.6) / 2 u64 版 (3.6.8.7) の L1BM 位置とレジスタの対応\n");

    mnc2_device_t dev = mnc2_open(0);
    if (dev == NULL) { printf("FAIL: mnc2_open\n"); return 1; }

    void* sendbuf = mnc2_alloc_host_buffer(dev, IN_BYTES);
    void* recvbuf = mnc2_alloc_host_buffer(dev, OUT_BYTES);
    if (sendbuf == NULL || recvbuf == NULL) { printf("FAIL: alloc\n"); goto fail; }

    /* L1BM[k] = 0xAk を 8 バイト並べた値 (k = 0..7)。残りは 0 */
    uint64_t* sp = (uint64_t*)sendbuf;
    memset(sp, 0, IN_BYTES);
    for (int k = 0; k < N_SRC; k++)
        sp[k] = (uint64_t)(0xA0 + k) * 0x0101010101010101ULL;
    memset(recvbuf, 0, OUT_BYTES);

    int rc = mnc2_send(dev, sendbuf, 0, IN_BYTES, SEND_TAG);
    if (rc != 0) { printf("FAIL: send rc=%d\n", rc); goto fail; }

    mnc2_kernel_t k = mnc2_load_kernel(dev, "_build/l1bmp_map.idma.dat");
    if (k == NULL) { printf("FAIL: load_kernel\n"); goto fail; }
    rc = mnc2_exec_kernel(k);
    mnc2_free_kernel(k);
    if (rc != 0) { printf("FAIL: exec_kernel rc=%d\n", rc); goto fail; }

    rc = mnc2_recv(dev, recvbuf, 0, OUT_BYTES, RECV_TAG);
    if (rc != 0) { printf("FAIL: recv rc=%d\n", rc); goto fail; }

    uint64_t* rp = (uint64_t*)recvbuf;

    printf("\n入力: L1BM[+k] = 0x%016" PRIx64 " (k=0) から 0x%016" PRIx64 " (k=7)\n",
           sp[0], sp[N_SRC - 1]);

    printf("\n[2 u64 版] l1bmp $llb0 $llr0v  (collect で回収、全 backend 共通)\n");
    for (int b = 0; b < N_BLOCK; b++)
        print_reg(block_reg[b], rp[b * N_PE]);

    /* broadcast なので 1 ブロック内の 4096 PE はすべて同値のはず */
    int uniform = 1;
    for (int b = 0; b < N_BLOCK && uniform; b++)
        for (int i = 1; i < N_PE; i++)
            if (rp[b * N_PE + i] != rp[b * N_PE]) { uniform = 0; break; }

    /* --- 観察の解釈 (collect の結果だけで判定できる) --- */
    printf("\n");
    int lo0 = src_index_of(rp[0 * N_PE]);   /* $lr0 */
    int hi0 = src_index_of(rp[1 * N_PE]);   /* $lr2 */
    int lo1 = src_index_of(rp[2 * N_PE]);   /* $lr4 */
    int hi1 = src_index_of(rp[3 * N_PE]);   /* $lr6 */

    if (lo0 == 0 && hi0 == 4 && lo1 == 1 && hi1 == 5)
        printf("OBSERVED: 2 u64 版は 1 サイクル内で data[0] = L1BM[+c] が小さいレジスタ番号\n"
               "          ($lr(4c)) に入り、data[1] = L1BM[+c+4] が $lr(4c+2) に入る\n");
    else if (lo0 == 4 && hi0 == 0 && lo1 == 5 && hi1 == 1)
        printf("OBSERVED: 2 u64 版は 1 サイクル内で data[1] = L1BM[+c+4] が小さいレジスタ番号\n"
               "          ($lr(4c)) に入り、data[0] = L1BM[+c] が $lr(4c+2) に入る\n");
    else
        printf("OBSERVED: 2 u64 版の対応が想定のどちらとも違う (上の表を参照)\n");

    if (lo1 == lo0 + 1)
        printf("OBSERVED: サイクルが 1 進むと L1BM 側も 1 進む (cycle c が L1BM[+c])\n");
    else
        printf("OBSERVED: サイクルと L1BM 位置の進み方が想定と違う\n");

    printf("OBSERVED: %s\n", uniform ? "各ブロックの 4096 PE がすべて同値 (broadcast として整合)"
                                     : "同一ブロック内で PE によって値が違う (broadcast なのに同値でない)");

    /* --- emu 限定の追加情報: 残り 8 本と、書かれないレジスタ --- */
    if (strcmp(mnc2_get_backend_name(dev), "device") != 0) {
        mnc2_loc_t loc = MNC2_LOC_INIT;
        uint64_t r[N_DBG];
        memset(r, 0, sizeof(r));
        if (mnc2_debug_read(dev, MNC2_MEM_GRF0, &loc, 0, N_DBG, r) == 0) {
            printf("\n--- 以下は emu のみ (mnc2_debug_read で GRF0 を直接読む) ---\n");
            printf("\n[2 u64 版] 残りの 4 本\n");
            for (int i = 4; i < 8; i++) print_reg(i * 2, r[i]);
            printf("\n[1 u64 版] l1bmp $lb0 $lr16v\n");
            for (int i = 0; i < 4; i++) print_reg(16 + i * 2, r[(16 + i * 2) / 2]);
            printf("\n[どちらの l1bmp も書かないレジスタ]\n");
            for (int i = 0; i < 4; i++) print_reg(24 + i * 2, r[(24 + i * 2) / 2]);

            int one_ok = 1;
            for (int c = 0; c < 4; c++)
                if (src_index_of(r[(16 + c * 2) / 2]) != c) one_ok = 0;
            printf("\nOBSERVED: 1 u64 版は %s\n",
                   one_ok ? "cycle c が L1BM[+c] を $lr(16+2c) に入れる"
                          : "cycle c と L1BM[+c] の対応が想定と違う (上の表を参照)");
        } else {
            printf("\n(mnc2_debug_read に失敗したため emu 限定の追加情報は出せない)\n");
        }
    } else {
        printf("\n(device では mnc2_debug_read が使えないため、残り 8 本は出せない)\n");
    }

    printf("\nPASS: l1bmp の L1BM 位置とレジスタの対応を観察した\n");
    mnc2_free_host_buffer(dev, sendbuf, IN_BYTES);
    mnc2_free_host_buffer(dev, recvbuf, OUT_BYTES);
    mnc2_close(dev);
    return 0;

fail:
    if (sendbuf) mnc2_free_host_buffer(dev, sendbuf, IN_BYTES);
    if (recvbuf) mnc2_free_host_buffer(dev, recvbuf, OUT_BYTES);
    mnc2_close(dev);
    return 1;
}
