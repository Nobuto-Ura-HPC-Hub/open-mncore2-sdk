/* repeat_idma.c — 同じ命令列を連続 kick して IDMA のあふれを観測する
 *
 * gpfn3_kick_inst_dma を繰り返し呼び、毎回 IDMA_STAT (PIO 0x018) を読んで
 * n_dma / inst_in_ibuf / overflow を 1 行ずつ出す。overflow (bit21) が
 * 立った時点で止め、何個目だったかを報告する。
 *
 * 何を知りたいか:
 *   - overflow は何個目の kick で立つか。命令列の大きさで変わるか
 *   - そのとき n_dma (キュー、5 bit) と inst_in_ibuf (命令バッファ、13 bit) は
 *     いくつか。どちらが先に飽和するか
 *   - overflow が立った後、kick はエラーを返すか、黙って受けるか
 *
 * Usage:
 *   repeat-idma PATH.idma.dat MAX_COUNT [--full-reset] [--no-reset]
 *
 * 命令列は gen_nop.py で大きさを変えて作れる (data/nop_x*.vsm)。
 *
 * 実機専用。エミュレータは IDMA のキューも命令バッファもモデル化しておらず、
 * kick した命令をその場で実行するだけなので、あふれは再現しない。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <gpfn3.h>

/* IDMA_STAT (PIO 0x018) のビット割り当て。チップ仕様書と照合して確定したもの。 */
#define IDMA_STAT       0x018u
#define IDMA_NDMA(v)    ((unsigned)((v) & 0x1fu))          /* bits[4:0]   キュー内 DMA 数 */
#define IDMA_INBUF(v)   ((unsigned)(((v) >> 8) & 0x1fffu)) /* bits[20:8]  命令バッファ内命令数 */
#define IDMA_OVF(v)     ((unsigned)(((v) >> 21) & 0x1u))   /* bit[21]     overflow */

/* busy 判定は n_dma + inst_in_ibuf (overflow は含めない)。libmnc2 の
   MNC2_IDMA_BUSY_MASK と同じ値。 */
#define IDMA_BUSY_MASK  0x1fffffULL

/* idle 待ちの上限 (PIO read の回数)。超えたら警告を出して先へ進む。
   上限のない待ちは置かない。 */
#define IDLE_WAIT_MAX   200000

/* SPI-A リセットレジスタ (PIO 0x050)。reset.c の定義と同じ。
   gpfn3_reset_device と違い PIO 書き込みだけなので必ず戻る。 */
#define SPIA_RESET             0x050u
#define SPIA_RESET_SPIA_MODULE (1ULL << 63)
#define SPIA_RESET_DMAQ_CLEAR  (1ULL << 62)
#define SPIA_RESET_FRB_RESET   (1ULL << 61)

/* SPI-A リセット。DMAQ_CLEAR が DMA キューを掃除する。
   本ツールは意図的にキューを埋めるので、gpfn3_reset_device は使わない
   (キューに残留があると上限のない命令カウンタ待ちで戻らなくなる)。 */
static int spia_reset(gpfn3_device_id_t dev)
{
    const uint64_t bits = SPIA_RESET_SPIA_MODULE
                        | SPIA_RESET_DMAQ_CLEAR
                        | SPIA_RESET_FRB_RESET;
    gpfn3_error_t err;

    usleep(1000);

    err = gpfn3_write_pio(dev, SPIA_RESET, bits);
    if (err != GPFN3_SUCCESS) {
        fprintf(stderr, "FAIL: gpfn3_write_pio(SPIA_RESET) rc=%d\n", (int)err);
        return -1;
    }
    /* dummy write x2 (書き込み順序の保証。完了を待つループではない) */
    for (int i = 0; i < 2; i++) {
        err = gpfn3_write_pio(dev, SPIA_RESET, 0);
        if (err != GPFN3_SUCCESS) {
            fprintf(stderr, "FAIL: gpfn3_write_pio(SPIA_RESET dummy %d) rc=%d\n",
                    i + 1, (int)err);
            return -1;
        }
    }

    usleep(1000);
    return 0;
}

/* .idma.dat ファイルを読んで DMA memory に置く (run_idma.c と同じ手順)。
   gpfn3_kick_inst_dma は size % 128 == 0 を assert するので事前に確認する。 */
static void* load_kernel_file(gpfn3_device_id_t dev,
                              const char* path, size_t* out_size)
{
    FILE* fp = fopen(path, "rb");
    if (fp == NULL) {
        fprintf(stderr, "FAIL: fopen(%s)\n", path);
        return NULL;
    }
    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    rewind(fp);
    if (fsize <= 0 || (fsize % 128) != 0) {
        fprintf(stderr, "FAIL: %s size %ld is not a positive multiple of 128\n",
                path, fsize);
        fclose(fp);
        return NULL;
    }
    size_t size = (size_t)fsize;

    void* dma = gpfn3_allocate_dma_memory(dev, size);
    if (dma == NULL) {
        fprintf(stderr, "FAIL: gpfn3_allocate_dma_memory(%zu)\n", size);
        fclose(fp);
        return NULL;
    }

    size_t nread = fread(dma, 1, size, fp);
    fclose(fp);
    if (nread != size) {
        fprintf(stderr, "FAIL: fread %zu / %zu\n", nread, size);
        gpfn3_free_dma_memory(dev, dma, size);
        return NULL;
    }

    *out_size = size;
    return dma;
}

/* IDMA が idle になるまで待つ (上限付き)。idle になれば 0、
   上限に達したら 1、PIO エラーなら -1 を返す。 */
static int wait_idma_idle(gpfn3_device_id_t dev, uint64_t* last_stat)
{
    for (long i = 0; i < IDLE_WAIT_MAX; i++) {
        uint64_t rdt = 0;
        gpfn3_error_t err = gpfn3_read_pio(dev, IDMA_STAT, &rdt);
        if (err != GPFN3_SUCCESS) {
            fprintf(stderr, "FAIL: gpfn3_read_pio(IDMA_STAT) rc=%d\n", (int)err);
            return -1;
        }
        *last_stat = rdt;
        if ((rdt & IDMA_BUSY_MASK) == 0) {
            return 0;
        }
    }
    return 1;
}

/* 1 kick 分の観測。**ループの中では出力しない**。printf はループの中で呼ぶと
   その間に device がキューを掃いてしまい、測りたい溜まり方そのものが変わる
   (標準出力が端末なら 1 行ごとに write が走り、kick より桁違いに時間を食う)。
   ここに貯めておいて、ループを抜けてからまとめて出す。 */
struct sample {
    int      rc;   /* gpfn3_kick_inst_dma の戻り値 */
    uint64_t raw;  /* IDMA_STAT の生値。分解は出力時に行う */
};

static void usage(FILE* fp)
{
    fprintf(fp,
        "Usage: repeat-idma PATH.idma.dat MAX_COUNT [--full-reset] [--no-reset]\n"
        "\n"
        "  PATH.idma.dat   assemble3 --loader 生成の idma.dat\n"
        "                  (大きさを変えたものは gen_nop.py で作れる)\n"
        "  MAX_COUNT       kick の上限回数。overflow が立たないまま\n"
        "                  延々と回らないための安全弁\n"
        "  --full-reset    開始時に gpfn3_reset_device を使う。\n"
        "                  **キューに残留があると戻ってこないことがある**\n"
        "                  既定の SPI-A リセットは必ず戻る\n"
        "  --no-reset      開始時のリセットを省略\n"
        "\n"
        "既定では開始時と終了時に SPI-A リセット (PIO 0x%03x の DMAQ_CLEAR) を\n"
        "打つ。終了時に打つのは、overflow の状態とキューの残留を次の実行に\n"
        "持ち越さないため。\n",
        SPIA_RESET);
}

int main(int argc, char** argv)
{
    const char* path = NULL;
    long max_count = -1;
    int full_reset = 0;
    int no_reset = 0;

    for (int i = 1; i < argc; i++) {
        const char* arg = argv[i];

        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            usage(stdout);
            return 0;
        }
        if (strcmp(arg, "--full-reset") == 0) {
            full_reset = 1;
            continue;
        }
        if (strcmp(arg, "--no-reset") == 0) {
            no_reset = 1;
            continue;
        }
        if (arg[0] == '-') {
            fprintf(stderr, "FAIL: unknown option '%s'\n", arg);
            usage(stderr);
            return 1;
        }

        /* positional: 最初が path、次が MAX_COUNT */
        if (path == NULL) {
            path = arg;
            continue;
        }
        if (max_count < 0) {
            char* end = NULL;
            long v = strtol(arg, &end, 0);
            if (end == arg || *end != '\0' || v < 1) {
                fprintf(stderr, "FAIL: invalid MAX_COUNT '%s'\n", arg);
                return 1;
            }
            max_count = v;
            continue;
        }
        fprintf(stderr, "FAIL: unexpected argument '%s'\n", arg);
        usage(stderr);
        return 1;
    }

    if (path == NULL || max_count < 0) {
        usage(stderr);
        return 1;
    }

    gpfn3_device_id_t dev = gpfn3_get_device_id(0);
    if (dev == GPFN3_INVALID_DEVICE_ID) {
        fprintf(stderr, "FAIL: gpfn3_get_device_id(0)\n");
        return 1;
    }

    /* 開始時のリセット。既定は SPI-A (必ず戻る)。 */
    if (!no_reset) {
        if (full_reset) {
            usleep(1000);
            gpfn3_error_t err = gpfn3_reset_device(dev);
            usleep(1000);
            if (err != GPFN3_SUCCESS) {
                fprintf(stderr, "FAIL: gpfn3_reset_device rc=%d\n", (int)err);
                gpfn3_close_device(dev);
                return 1;
            }
        } else if (spia_reset(dev) != 0) {
            gpfn3_close_device(dev);
            return 1;
        }
    }

    size_t ksize = 0;
    void* kdma = load_kernel_file(dev, path, &ksize);
    if (kdma == NULL) {
        gpfn3_close_device(dev);
        return 1;
    }

    struct sample* samples = calloc((size_t)max_count, sizeof(struct sample));
    if (samples == NULL) {
        fprintf(stderr, "FAIL: calloc(%ld samples)\n", max_count);
        gpfn3_free_dma_memory(dev, kdma, ksize);
        gpfn3_close_device(dev);
        return 1;
    }

    long ovf_idx = -1;
    long done = 0;
    int pio_failed = 0;
    int pio_fail_rc = 0;

    /* 計測ループ。**この中では出力も集計もしない。** 貯めるだけにして、
       kick と kick の間に余計な時間を挟まないようにする。 */
    for (long i = 0; i < max_count; i++) {
        gpfn3_error_t kick_rc = gpfn3_kick_inst_dma(dev, kdma, ksize);

        uint64_t rdt = 0;
        gpfn3_error_t err = gpfn3_read_pio(dev, IDMA_STAT, &rdt);
        if (err != GPFN3_SUCCESS) {
            pio_failed = 1;
            pio_fail_rc = (int)err;
            done = i;
            break;
        }

        samples[i].rc  = (int)kick_rc;
        samples[i].raw = rdt;
        done = i + 1;

        if (IDMA_OVF(rdt)) {
            ovf_idx = i;
            break;
        }
    }

    /* 後始末。まず idle を待つ (device がまだ命令列を読んでいる最中に
       バッファを解放しないため)。上限に達しても、続く SPI-A リセットの
       DMAQ_CLEAR でキューを掃除してから解放するので安全側に倒れる。 */
    uint64_t final_stat = 0;
    int idle_rc = wait_idma_idle(dev, &final_stat);
    if (idle_rc == 1) {
        fprintf(stderr,
                "WARN: IDMA idle wait hit the limit (%d reads). "
                "IDMA_STAT=0x%016llx\n",
                IDLE_WAIT_MAX, (unsigned long long)final_stat);
    }

    /* 終了時のリセット。overflow の状態とキューの残留を次の実行に持ち越さない。
       解放の前に打つのは、DMAQ_CLEAR で device が命令列を読むのを止めてから
       バッファを返すため。 */
    int reset_rc = spia_reset(dev);
    if (reset_rc != 0) {
        fprintf(stderr, "WARN: final SPI-A reset failed\n");
    }

    gpfn3_free_dma_memory(dev, kdma, ksize);

    /* ここから出力。device の後始末が済んでいるので、いくら時間を使ってもよい。
       kick_rc は「呼び出しがエラーを返さなかった」ことしか意味しない。
       要求が実際にキューに載ったかどうかは n_dma が増えたかどうかで見る。 */
    printf("# kernel: %s (%zu bytes)\n", path, ksize);
    printf("# max_count: %ld\n", max_count);
    printf("# reset: %s\n",
           no_reset ? "none" : (full_reset ? "gpfn3_reset_device" : "SPI-A (PIO 0x050)"));
    printf("# idx kick_rc n_dma inst_in_ibuf overflow\n");

    unsigned max_ndma = 0;
    unsigned max_inbuf = 0;

    for (long i = 0; i < done; i++) {
        unsigned ndma  = IDMA_NDMA(samples[i].raw);
        unsigned inbuf = IDMA_INBUF(samples[i].raw);
        unsigned ovf   = IDMA_OVF(samples[i].raw);

        printf("%ld %d %u %u %u\n", i, samples[i].rc, ndma, inbuf, ovf);

        if (ndma  > max_ndma)  max_ndma  = ndma;
        if (inbuf > max_inbuf) max_inbuf = inbuf;
    }
    free(samples);

    printf("# kicked: %ld\n", done);
    if (ovf_idx >= 0) {
        printf("# stopped: overflow at idx=%ld\n", ovf_idx);
    } else if (pio_failed) {
        printf("# stopped: PIO read error (rc=%d) at idx=%ld\n", pio_fail_rc, done);
    } else {
        printf("# stopped: reached max_count (overflow not observed)\n");
    }
    printf("# max n_dma: %u\n", max_ndma);
    printf("# max inst_in_ibuf: %u\n", max_inbuf);
    printf("# idle wait: %s (IDMA_STAT=0x%016llx)\n",
           idle_rc == 0 ? "idle" : (idle_rc == 1 ? "hit limit" : "PIO error"),
           (unsigned long long)final_stat);
    printf("# final reset: %s\n", reset_rc == 0 ? "OK" : "FAILED");

    gpfn3_close_device(dev);
    return pio_failed ? 1 : 0;
}
