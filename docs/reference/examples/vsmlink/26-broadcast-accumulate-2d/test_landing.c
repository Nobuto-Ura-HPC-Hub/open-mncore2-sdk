/* test_landing.c — 段階1: l1bmp vector dst の着地レジスタを emu:lib で実測する
 *
 * @broadcast size 4 で放送領域 (PDM 64 u64) を各 PE の $lr16 起点 vector dst へ配る。
 * 放送領域は u64[i] = 1000 + i の相異なる値で埋める。展開後に各 PE の
 * $lr16/$lr18/$lr20/$lr22 を collect で回収し、どのレジスタにどの源 index の値が
 * 着地したかを表示・判定する。
 *
 *   register が 1000+idx を読む: 源 index idx が着地
 *   register が 7777 を読む:     sentinel のまま (書かれていない)
 *
 * GRF0 は偶数アライン必須なので着地は +2 偶数刻みしかありえない。本テストは
 * size 4 が 4 レジスタすべてに書くこと、および PDM u64 とレジスタの対応を確定する。
 *
 * PDM レイアウト (landing.param):
 *   slot 8  (broadcast 入力): PDM[4096..]    64 u64
 *   slot 16 (r16 回収):        PDM[131072..]  4096 u64
 *   slot 24 (r18 回収):        PDM[135168..]  4096 u64
 *   slot 32 (r20 回収):        PDM[139264..]  4096 u64
 *   slot 40 (r22 回収):        PDM[143360..]  4096 u64
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "mnc2.h"

#define PE_COUNT      4096
#define PE_BYTES      (PE_COUNT * (uint64_t)sizeof(uint64_t))
#define BCAST_COUNT   64
#define BCAST_BYTES   (BCAST_COUNT * (uint64_t)sizeof(uint64_t))

#define OFFSET_BCAST  (4096ULL   * 8)
#define OFFSET_R16    (131072ULL * 8)
#define OFFSET_R18    (135168ULL * 8)
#define OFFSET_R20    (139264ULL * 8)
#define OFFSET_R22    (143360ULL * 8)

#define DMAID_TRIGGER 0x10
#define TAG_R16       0x1a
#define TAG_R18       0x1b
#define TAG_R20       0x1c
#define TAG_R22       0x1d

#define SENTINEL      7777ULL

static void describe(const char *name, uint64_t v)
{
    if (v == SENTINEL)
        printf("  %s = %llu  (sentinel: 書かれていない)\n",
               name, (unsigned long long)v);
    else if (v >= 1000 && v < 1000 + BCAST_COUNT)
        printf("  %s = %llu  (源 index %llu が着地)\n",
               name, (unsigned long long)v, (unsigned long long)(v - 1000));
    else
        printf("  %s = %llu  (想定外の値)\n", name, (unsigned long long)v);
}

int main(void)
{
    printf("=== 段階1: l1bmp vector dst 着地レジスタ実測 (size 4, 4096 PE) ===\n\n");

    mnc2_device_t dev = mnc2_open(0);
    if (!dev) { fprintf(stderr, "FAIL: mnc2_open\n"); return 1; }

    uint64_t *bcast = (uint64_t *)mnc2_alloc_host_buffer(dev, BCAST_BYTES);
    uint64_t *r16   = (uint64_t *)mnc2_alloc_host_buffer(dev, PE_BYTES);
    uint64_t *r18   = (uint64_t *)mnc2_alloc_host_buffer(dev, PE_BYTES);
    uint64_t *r20   = (uint64_t *)mnc2_alloc_host_buffer(dev, PE_BYTES);
    uint64_t *r22   = (uint64_t *)mnc2_alloc_host_buffer(dev, PE_BYTES);
    if (!bcast || !r16 || !r18 || !r20 || !r22) {
        fprintf(stderr, "FAIL: alloc\n"); mnc2_close(dev); return 1;
    }

    int rc = 1;

    /* 放送領域: u64[i] = 1000 + i (源 index を一意に判別できるように) */
    for (int i = 0; i < BCAST_COUNT; i++) bcast[i] = 1000ULL + i;
    if (mnc2_send(dev, bcast, OFFSET_BCAST, BCAST_BYTES, DMAID_TRIGGER) != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: send bcast\n"); goto cleanup;
    }

    mnc2_kernel_t k = mnc2_load_kernel(dev, "_build/landing.idma.dat");
    if (!k) { fprintf(stderr, "FAIL: load kernel\n"); goto cleanup; }
    rc = mnc2_exec_kernel(k);
    mnc2_free_kernel(k);
    if (rc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: exec\n"); rc = 1; goto cleanup; }

    memset(r16, 0, PE_BYTES); memset(r18, 0, PE_BYTES);
    memset(r20, 0, PE_BYTES); memset(r22, 0, PE_BYTES);
    if (mnc2_recv(dev, r16, OFFSET_R16, PE_BYTES, TAG_R16) != MNC2_SUCCESS ||
        mnc2_recv(dev, r18, OFFSET_R18, PE_BYTES, TAG_R18) != MNC2_SUCCESS ||
        mnc2_recv(dev, r20, OFFSET_R20, PE_BYTES, TAG_R20) != MNC2_SUCCESS ||
        mnc2_recv(dev, r22, OFFSET_R22, PE_BYTES, TAG_R22) != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: recv\n"); rc = 1; goto cleanup;
    }

    /* PE 0 の着地を表示 (broadcast は全 PE 同値のはず) */
    printf("PE 0 の各レジスタ着地:\n");
    describe("$lr16", r16[0]);
    describe("$lr18", r18[0]);
    describe("$lr20", r20[0]);
    describe("$lr22", r22[0]);

    /* 全 PE 同値であることの確認 (broadcast の性質) */
    {
        int uniform = 1;
        for (int i = 1; i < PE_COUNT; i++) {
            if (r16[i] != r16[0] || r18[i] != r18[0] ||
                r20[i] != r20[0] || r22[i] != r22[0]) { uniform = 0; break; }
        }
        printf("\n全 4096 PE 同値: %s\n", uniform ? "yes" : "no");
    }
    rc = 0;

cleanup:
    if (bcast) mnc2_free_host_buffer(dev, bcast, BCAST_BYTES);
    if (r16)   mnc2_free_host_buffer(dev, r16, PE_BYTES);
    if (r18)   mnc2_free_host_buffer(dev, r18, PE_BYTES);
    if (r20)   mnc2_free_host_buffer(dev, r20, PE_BYTES);
    if (r22)   mnc2_free_host_buffer(dev, r22, PE_BYTES);
    mnc2_close(dev);
    return (rc != 0) ? 1 : 0;
}
