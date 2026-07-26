/* gen_identify_golden.c — @identify / @distribute 偶奇判定テスト用 golden データ生成
 *
 * エミュレータ不要。期待値を純粋な計算で生成する。
 *
 * Usage: ./gen_identify_golden [output_dir]
 *        output_dir が省略された場合はカレントディレクトリに出力する
 *
 * 生成ファイル（ファイル名 = sha256 先頭 8 文字）:
 *
 *   ab1449ae.bin  32 KiB  test1 / test-identify / test4-run1 / test6-t4-run1
 *                         IDs [0..4095]: 奇数PE→ID, 偶数PE→0
 *                         [0, 1, 0, 3, 0, 5, ..., 0, 4095]
 *
 *   d202bcfb.bin  32 KiB  test2 / test5-run1 / test6-t5-run1
 *                         IDs [4096..8191]: 奇数ID→ID, 偶数ID→0
 *                         [0, 4097, 0, 4099, ..., 0, 8191]
 *
 *   8f8ea7e3.bin  64 KiB  test3 / test-seq
 *                         ab1449ae の内容 + d202bcfb の内容を連結
 *
 *   01becdee.bin  32 KiB  test4-run2 / test6-t4-run2
 *                         IDs [0..4095]: 偶数PE→ID, 奇数PE→0
 *                         [0, 0, 2, 0, 4, 0, ..., 4094, 0]
 *
 *   e4d40321.bin  32 KiB  test5-run2 / test6-t5-run2
 *                         IDs [4096..8191]: 偶数ID→ID, 奇数ID→0
 *                         [4096, 0, 4098, 0, ..., 8190, 0]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define ELEM_COUNT 4096
#define ELEM_BYTES (ELEM_COUNT * sizeof(uint64_t))

static int write_bin(const char *dir, const char *name,
                     const uint64_t *data, size_t count)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE *fp = fopen(path, "wb");
    if (!fp) { perror(path); return 1; }
    fwrite(data, sizeof(uint64_t), count, fp);
    fclose(fp);
    return 0;
}

int main(int argc, char *argv[])
{
    const char *dir = (argc > 1) ? argv[1] : ".";
    int rc = 0;

    uint64_t *a = malloc(ELEM_BYTES);
    uint64_t *b = malloc(ELEM_BYTES);
    uint64_t *c = malloc(ELEM_BYTES * 2);
    uint64_t *d = malloc(ELEM_BYTES);
    uint64_t *e = malloc(ELEM_BYTES);
    if (!a || !b || !c || !d || !e) {
        fprintf(stderr, "FAIL: malloc\n"); return 1;
    }

    /* ab1449ae: IDs [0..4095], 奇数→ID, 偶数→0 */
    for (int i = 0; i < ELEM_COUNT; i++)
        a[i] = (i % 2 == 1) ? (uint64_t)i : 0;
    rc |= write_bin(dir, "ab1449ae.bin", a, ELEM_COUNT);

    /* d202bcfb: IDs [4096..8191], 奇数ID→ID, 偶数ID→0
     *   PE i の ID = 4096+i。4096 は偶数なので奇数IDになるのは i が奇数のとき */
    for (int i = 0; i < ELEM_COUNT; i++)
        b[i] = (i % 2 == 1) ? (uint64_t)(4096 + i) : 0;
    rc |= write_bin(dir, "d202bcfb.bin", b, ELEM_COUNT);

    /* 8f8ea7e3: test3 = ab1449ae || d202bcfb */
    memcpy(c,              a, ELEM_BYTES);
    memcpy(c + ELEM_COUNT, b, ELEM_BYTES);
    rc |= write_bin(dir, "8f8ea7e3.bin", c, ELEM_COUNT * 2);

    /* 01becdee: IDs [0..4095], 偶数→ID, 奇数→0 (test4 run2) */
    for (int i = 0; i < ELEM_COUNT; i++)
        d[i] = (i % 2 == 0) ? (uint64_t)i : 0;
    rc |= write_bin(dir, "01becdee.bin", d, ELEM_COUNT);

    /* e4d40321: IDs [4096..8191], 偶数ID→ID, 奇数ID→0 (test5 run2) */
    for (int i = 0; i < ELEM_COUNT; i++)
        e[i] = (i % 2 == 0) ? (uint64_t)(4096 + i) : 0;
    rc |= write_bin(dir, "e4d40321.bin", e, ELEM_COUNT);

    free(a); free(b); free(c); free(d); free(e);
    return rc;
}
