/* test_stencil1d_debug.c — Stencil 1D 段階観察チュートリアル
 *
 * 1D ステンシル offset=-1（左隣取得）のデータ移動を段階的に観察する。
 * 各階層（PE / MAB / L1B / L2B）の境界付近の PE を
 * debug_read で読み出し、データの動きを可視化する。
 *
 * 各ステージの結果は異なる GRF レジスタに格納される:
 *   GRF0[0]: lr0  = 入力データ
 *   GRF0[1]: lr2  = msl（PE 間左シフト）
 *   GRF0[2]: lr4  = msr（PE 間右シフト）
 *   GRF0[3]: lr6  = cross MAB（l1bmd+1 + msr*3）
 *   GRF0[4]: lr8  = cross L1B（l2bm + l2bmb + l1bmd+1 + msr*3）
 *   GRF0[5]: lr10 = cross L2B（mvp + l2bmb + l1bmd+1 + msr*3）
 *
 * VSM のコメントにトポロジと命令の詳細あり。
 * ../05-stencil1d/README.md に袖交換の全体像あり。
 *
 * 実行方法: MNC2_BACKEND=emu:lib ./test_stencil1d_debug
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "mnc2.h"

#define ELEM_COUNT  4096
#define ELEM_BYTES  (ELEM_COUNT * sizeof(uint64_t))
#define DMAID_SEND  0x10

/* GRF0 register indices (addr for debug_read) */
#define N_STAGES    6

/* MN-Core 2 topology */
#define N_PE   4
#define N_MAB  16
#define N_L1B  8
#define N_SEC  8
#define PE_PER_MAB  4
#define PE_PER_L1B  64
#define PE_PER_SEC  512

/* Probe locations: MABs at interesting boundaries */
typedef struct {
    int mab_base;       /* first PE index of the MAB */
    const char* label;
} probe_t;

static probe_t probes[] = {
    {   0, "MAB 0  L1B 0  Sec 0"},   /* PE boundary */
    {   4, "MAB 1  L1B 0  Sec 0"},   /* MAB boundary (PE 3->4) */
    {  60, "MAB15  L1B 0  Sec 0"},   /* L1B boundary (PE 63->...) */
    {  64, "MAB 0  L1B 1  Sec 0"},   /* L1B boundary (...->PE 64) */
    { 508, "MAB15  L1B 7  Sec 0"},   /* L2B boundary (PE 511->...) */
    { 512, "MAB 0  L1B 0  Sec 1"},   /* L2B boundary (...->PE 512) */
};
#define N_PROBES 6

static mnc2_loc_t pe_to_loc(int pe_idx)
{
    int section = pe_idx / PE_PER_SEC;
    int within  = pe_idx % PE_PER_SEC;
    mnc2_loc_t loc;
    loc.chip = section / 2;
    loc.l2b  = section % 2;
    loc.l1b  = within / PE_PER_L1B;
    loc.mab  = (within % PE_PER_L1B) / PE_PER_MAB;
    loc.pe   = within % PE_PER_MAB;
    return loc;
}

/* --- 各ステージの期待値計算 --- */

static uint64_t expected_msl(int pe_idx)
{
    /* msl: PE j <- PE (j-1)%4 */
    int mab_base = (pe_idx / PE_PER_MAB) * PE_PER_MAB;
    int j = pe_idx % PE_PER_MAB;
    int src = mab_base + (j + PE_PER_MAB - 1) % PE_PER_MAB;
    return (uint64_t)(src + 1);
}

static uint64_t expected_msr(int pe_idx)
{
    /* msr: PE j <- PE (j+1)%4 */
    int mab_base = (pe_idx / PE_PER_MAB) * PE_PER_MAB;
    int j = pe_idx % PE_PER_MAB;
    int src = mab_base + (j + 1) % PE_PER_MAB;
    return (uint64_t)(src + 1);
}

static uint64_t expected_mab_xing(int pe_idx)
{
    /* l1bmd+1: MAB M reads MAB (M-1)%16, then msr*3: PE j <- PE (j+3)%4 */
    int l1b_base = (pe_idx / PE_PER_L1B) * PE_PER_L1B;
    int mab = (pe_idx % PE_PER_L1B) / PE_PER_MAB;
    int j   = pe_idx % PE_PER_MAB;
    int src_mab = (mab + N_MAB - 1) % N_MAB;
    int src_pe  = (j + 3) % PE_PER_MAB;
    return (uint64_t)(l1b_base + src_mab * PE_PER_MAB + src_pe + 1);
}

static uint64_t expected_l1b_xing(int pe_idx)
{
    int sec_base = (pe_idx / PE_PER_SEC) * PE_PER_SEC;
    int l1b = (pe_idx % PE_PER_SEC) / PE_PER_L1B;
    int mab = (pe_idx % PE_PER_L1B) / PE_PER_MAB;
    int j   = pe_idx % PE_PER_MAB;
    int src_mab = (mab + N_MAB - 1) % N_MAB;
    int src_pe  = (j + 3) % PE_PER_MAB;

    if (l1b == 0) {
        /* L1B 0: l2bmb なし -> l1bmd+1 は自身の L1BM から読む (ラップ) */
        return (uint64_t)(sec_base + src_mab * PE_PER_MAB + src_pe + 1);
    } else {
        /* L1B 1-7: l2bmb で前の L1B のデータが来る */
        int src_l1b_base = sec_base + (l1b - 1) * PE_PER_L1B;
        return (uint64_t)(src_l1b_base + src_mab * PE_PER_MAB + src_pe + 1);
    }
}

static uint64_t expected_l2b_xing(int pe_idx)
{
    int sec = pe_idx / PE_PER_SEC;
    int l1b = (pe_idx % PE_PER_SEC) / PE_PER_L1B;
    int mab = (pe_idx % PE_PER_L1B) / PE_PER_MAB;
    int j   = pe_idx % PE_PER_MAB;
    int src_mab = (mab + N_MAB - 1) % N_MAB;
    int src_pe  = (j + 3) % PE_PER_MAB;

    if (l1b == 0 && sec == 0) {
        /* Sec 0 L1B 0: 左隣なし（境界条件 = 0） */
        return 0;
    } else if (l1b == 0 && sec > 0) {
        /* L1B 0: l2bmb で前セクションの L1B 7 のデータが来る */
        int src_base = (sec - 1) * PE_PER_SEC + 7 * PE_PER_L1B;
        return (uint64_t)(src_base + src_mab * PE_PER_MAB + src_pe + 1);
    } else {
        /* L1B 1-7: cross L2Bの対象外。l1bmd+1 結果がそのまま残る */
        int sec_base = (pe_idx / PE_PER_SEC) * PE_PER_SEC;
        int l1b_base = sec_base + l1b * PE_PER_L1B;
        return (uint64_t)(l1b_base + src_mab * PE_PER_MAB + src_pe + 1);
    }
}

/* --- ステージ情報 --- */

static const char* stage_intros[] = {
    /* Stage 0 */
    "入力データを PDM から全 4096 PE に配布します。\n"
    "  経路: PDM -> (mvp) -> L2BM -> (l2bmb) -> L1BM -> (l1bmd) -> PE\n"
    "  各 PE に PE 番号 + 1 の値が入ります。",
    /* Stage 1 */
    "msl 命令で MAB 内の左隣 PE のデータを取得します。\n"
    "  PE j は PE (j-1) mod 4 のデータを受け取ります。\n"
    "  PE 1-3 は正しい左隣ですが、PE 0 は MAB 内ラップ（PE 3 のデータ）です。\n"
    "  -> MAB 0 の PE 0 に注目: 値が PE 3 のものになっています。",
    /* Stage 2 */
    "msr 命令で MAB 内の右隣 PE のデータを取得します。\n"
    "  PE j は PE (j+1) mod 4 のデータを受け取ります。\n"
    "  PE 3 は MAB 内ラップ（PE 0 のデータ）です。",
    /* Stage 3 */
    "L1BM を経由して隣接 MAB のデータを取得します。\n"
    "  経路: PE -> (l1bmd) -> L1BM -> (l1bmd+1) -> PE -> (msr*3)\n"
    "  l1bmd+1 は「1 つ前の MAB」のスロットを読み出します。\n"
    "  msr*3 は結果を PE 3 -> PE 0 に回転させます。\n"
    "  -> MAB 0 は L1B 内ラップ（MAB 15 のデータ）。L1B 0 では不正確です。",
    /* Stage 4 */
    "L2BM を経由して隣接 L1B のデータを取得します。\n"
    "  経路: PE -> L1BM -> (l2bm) -> L2BM -> (l2bmb) -> L1BM -> PE\n"
    "  l2bmb@N は L2BM スロット N-1 を L1B N に配信します。\n"
    "  -> L1B 1-7 の MAB 0 に注目: 前の L1B のデータに置き換わっています。\n"
    "  -> L1B 0 は受信しない（左隣がセクション外）ので Stage 3 のラップが残ります。",
    /* Stage 5 */
    "PDM を一時バッファとして、隣接セクションのデータを取得します。\n"
    "  経路: PE -> L1BM -> L2BM -> (mvp) -> PDM -> (mvp) -> L2BM -> (l2bmb@0) -> L1BM -> PE\n"
    "  L2BM 間は直接通信できないので、PDM 経由で受け渡します。\n"
    "  -> Section 1-7 の L1B 0 に注目: 前セクションの L1B 7 のデータが入ります。\n"
    "  -> Section 0 の L1B 0 は左隣なし（境界条件 = 0）。",
};

static const char* stage_results[] = {
    /* Stage 0 */
    "全 PE にデータが配布されました。PE[i] = i+1 です。",
    /* Stage 1 */
    "PE 1-3 は正しい左隣を取得しました。PE 0 は MAB ラップ（要修正）。",
    /* Stage 2 */
    "PE 0-2 は正しい右隣を取得しました。PE 3 は MAB ラップ（要修正）。",
    /* Stage 3 */
    "cross MABました。MAB 0 PE 0 が前の MAB のデータを持っています。\n"
    "  ただし L1B 0 の MAB 0 は L1B 内ラップ -> 次の cross L1Bで修正します。",
    /* Stage 4 */
    "cross L1Bました。L1B 1-7 の MAB 0 が前の L1B のデータを持っています。\n"
    "  ただし L1B 0 はラップのまま -> 次の cross L2Bで修正します。",
    /* Stage 5 */
    "cross L2Bました。Section 1-7 の L1B 0 が前セクションのデータを持っています。\n"
    "  Section 0 L1B 0 は左隣なし（境界条件 = 0）。これが最終的な端の扱いです。",
};

static const char* stage_names[] = {
    "Distribute（PDM -> PE）",
    "msl（PE 間左シフト）",
    "msr（PE 間右シフト）",
    "cross MAB（l1bmd+1 + msr*3）",
    "cross L1B（l2bm + l2bmb + l1bmd+1 + msr*3）",
    "cross L2B（mvp + l2bmb + l1bmd+1 + msr*3）",
};

int main(void)
{
    printf("=== Stencil 1D 段階観察チュートリアル ===\n");
    printf("\n");
    printf("1D ステンシル offset=-1（左隣取得）のデータ移動を段階的に観察します。\n");
    printf("入力: PE[i] = i+1（PE 番号 + 1 の整数値）\n");
    printf("\n");
    printf("MN-Core 2 のトポロジ:\n");
    printf("  4 PE -> 1 MAB -> 16 MAB -> 1 L1B -> 8 L1B -> 1 Section -> 8 Section -> 4096 PE\n");
    printf("  PE は同じ MAB 内の 4 PE としか直接通信できません（msl/msr）。\n");
    printf("  隣の MAB・L1B・セクションのデータは上位メモリ階層を経由します。\n");
    printf("\n");
    printf("観測点: 各階層の境界付近の MAB を debug_read で読み出します。\n");
    printf("  (VSM コメントにトポロジと命令の詳細あり。05-stencil1d/README.md も参照)\n");
    printf("\n");

    mnc2_device_t* dev = mnc2_open(0);
    if (dev == NULL) {
        fprintf(stderr, "SKIP: mnc2_open failed\n");
        return 0;
    }

    void* sbuf = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    if (sbuf == NULL) {
        fprintf(stderr, "FAIL: alloc\n");
        mnc2_close(dev);
        return 1;
    }

    uint64_t* input = (uint64_t*)sbuf;
    for (int i = 0; i < ELEM_COUNT; i++)
        input[i] = (uint64_t)(i + 1);

    printf("--- 準備: 入力データを PDM に送信します ---\n");
    int rc = mnc2_send(dev, sbuf, 0, ELEM_BYTES, DMAID_SEND);
    if (rc != 0) {
        fprintf(stderr, "FAIL: send returned %d\n", rc);
        goto fail;
    }
    printf("  送信完了: %d 要素 (PE[0]=1, PE[1]=2, ..., PE[4095]=4096)\n\n", ELEM_COUNT);

    printf("--- 準備: カーネルを実行します ---\n");
    mnc2_kernel_t* k = mnc2_load_kernel(dev, "_build/stencil1d_debug.idma.dat");
    if (k == NULL) {
        fprintf(stderr, "FAIL: load_kernel\n");
        goto fail;
    }
    rc = mnc2_exec_kernel(k);
    if (rc != 0) {
        fprintf(stderr, "FAIL: exec_kernel returned %d\n", rc);
        mnc2_free_kernel(k);
        goto fail;
    }
    printf("  カーネル実行完了。各ステージの結果が GRF レジスタに格納されています。\n");
    printf("  debug_read で各 PE のレジスタを読み出して観察します。\n\n");

    /* --- debug_read smoke test --- */
    {
        mnc2_loc_t loc0 = {0, 0, 0, 0, 0};
        uint64_t test_val;
        rc = mnc2_debug_read(dev, MNC2_MEM_GRF0, &loc0, 0, 1, &test_val);
        if (rc != 0) {
            printf("debug_read 非対応のバックエンド (rc=%d)\n", rc);
            mnc2_free_kernel(k);
            goto fail;
        }
        printf("--- debug_read 動作確認 ---\n");
        printf("  PE[0] lr0 = %" PRIu64 "（期待値: 1）... ", test_val);
        if (test_val != 1) {
            printf("不一致。GRF0 レイアウトが想定と異なります。中断します。\n");
            for (int a = 0; a < 16; a++) {
                mnc2_debug_read(dev, MNC2_MEM_GRF0, &loc0, a, 1, &test_val);
                printf("  GRF0 addr=%d: %" PRIu64 "\n", a, test_val);
            }
            mnc2_free_kernel(k);
            goto fail;
        }
        printf("OK\n\n");
    }

    /* --- 全プローブ PE を読み出す --- */
    uint64_t data[N_PROBES][PE_PER_MAB][N_STAGES];

    for (int p = 0; p < N_PROBES; p++) {
        for (int j = 0; j < PE_PER_MAB; j++) {
            int pe_idx = probes[p].mab_base + j;
            mnc2_loc_t loc = pe_to_loc(pe_idx);
            rc = mnc2_debug_read(dev, MNC2_MEM_GRF0, &loc,
                                 0, N_STAGES, data[p][j]);
            if (rc != 0) {
                fprintf(stderr, "FAIL: debug_read PE[%d] rc=%d\n", pe_idx, rc);
                mnc2_free_kernel(k);
                goto fail;
            }
        }
    }
    mnc2_free_kernel(k);

    /* --- ステージごとに表示・検証 --- */
    typedef uint64_t (*expect_fn)(int);
    expect_fn expects[] = {
        NULL,                /* Stage 0: input = pe_idx + 1 */
        expected_msl,
        expected_msr,
        expected_mab_xing,
        expected_l1b_xing,
        expected_l2b_xing,
    };

    int total_errors = 0;

    for (int s = 0; s < N_STAGES; s++) {
        printf("========================================\n");
        printf("Stage %d: %s\n", s, stage_names[s]);
        printf("========================================\n");
        printf("\n%s\n\n", stage_intros[s]);

        int stage_ok = 1;

        for (int p = 0; p < N_PROBES; p++) {
            printf("  %s:", probes[p].label);
            for (int j = 0; j < PE_PER_MAB; j++) {
                int pe_idx = probes[p].mab_base + j;
                uint64_t got = data[p][j][s];
                uint64_t exp;
                if (s == 0)
                    exp = (uint64_t)(pe_idx + 1);
                else
                    exp = expects[s](pe_idx);

                int ok = (got == exp);
                if (!ok) {
                    stage_ok = 0;
                    total_errors++;
                }

                char mark = ok ? ' ' : '!';
                printf(" %4" PRIu64 "%c", got, mark);
            }
            printf("\n");
        }

        printf("\n-> %s\n", stage_results[s]);
        if (!stage_ok)
            printf("   *** 不一致あり（! マークの値を確認してください）***\n");
        printf("\n");
    }

    /* --- まとめ --- */
    printf("========================================\n");
    printf("まとめ: offset=-1 の袖交換で使われる階層\n");
    printf("========================================\n");
    printf("\n");
    printf("  PE 1-3:   msl で同一 MAB 内の左隣を直接取得\n");
    printf("  PE 0:     cross MAB（L1BM 経由: l1bmd+1 + msr*3）\n");
    printf("  MAB 0:    cross L1B（L2BM 経由: l2bm + l2bmb）\n");
    printf("  L1B 0:    cross L2B（PDM 経由: mvp で受け渡し）\n");
    printf("  Sec 0 端: 境界条件（左隣なし = 0）\n");
    printf("\n");
    printf("  実際の @stencil ディレクティブでは、これらを maskr/ipassa でマージして\n");
    printf("  各 PE に正しい左隣の値を配置します（05-stencil1d を参照）。\n");
    printf("\n");

    if (total_errors == 0)
        printf("PASS: 全 %d 個のプローブ値が一致\n", N_PROBES * PE_PER_MAB * N_STAGES);
    else
        printf("FAIL: %d 件の不一致\n", total_errors);

    mnc2_free_host_buffer(dev, sbuf, ELEM_BYTES);
    mnc2_close(dev);
    return (total_errors > 0) ? 1 : 0;

fail:
    mnc2_free_host_buffer(dev, sbuf, ELEM_BYTES);
    mnc2_close(dev);
    return 1;
}
