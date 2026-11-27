# 24-bitonic-sort: バイトニックソート（4096 個、double）

4096 個の double を、PE の番号の昇順に並べるバイトニックソートである。比較・交換の相手は、`get_hypercube_neighbor` で受け取る
（vsmlink の `36-bitonic-sort` に当たる MNCL の example）。

## アルゴリズム

全体は 12 段で、12 × 13 / 2 = **78 ステップ**である（段 s は 1〜12、各段のステップ j は s−1 から 0）。
PE の番号 `id` の PE は、相手 `id XOR 2^j` の値を `get_hypercube_neighbor(v, id, j)` で受け取り、
`id` の bit j と bit s が等しければ小さい方、そうでなければ大きい方を新しい値にする。

```c
double other = get_hypercube_neighbor(v, id, j);
double lo = (v < other) ? v : other;
double hi = (v < other) ? other : v;
bool take_min = (((id >> j) ^ (id >> s)) & 1) == 0;
v = take_min ? lo : hi;
```

`get_hypercube_neighbor` の `deep`（ここでは j）は定数でなければならない。`#pragma unroll` で、s と j のループを完全に展開して定数にする。
**ループの展開に頼る形なので、`#pragma unroll` を外すと、`deep` が定数にならず、コンパイルの誤りになる。**

## ホストがすること（`test_bitonic-sort.c`）

1. PE の番号 `[0..4095]` を、`.param` の `:identify` の番地（addr 0）へ送る（`get_global_id` と同じ）
2. 入力 4096 個（有限で 0 でない double。正と負の両方）を `a`（slot 8）へ送る
3. kernel を実行し、`out`（slot 16）を受け取る
4. 結果が、入力を `qsort` で昇順に並べたものと、**ビット列まで一致する**ことを確かめる

入力に 0 と NaN を入れないのは、`dmin` / `dmax` が 0 を同一視し、NaN の扱いが特別なため。

## PDM の配置（`bitonic-sort.param`）

| slot | 用途 | `:addr`（u64） |
|---|---|---|
| `:identify` | PE の番号（4096 個） | 0 |
| 8 | 入力（4096 個。`:send_wait_tag` #x11） | 4096 |
| 16 | 出力（4096 個。`:recv_wait_tag` #x1e） | 8192 |
| `:buffer nbuf0` | `get_hypercube_neighbor` の deep 9〜11 が使う作業領域（4160 u64） | 16384 |

## 実行

```bash
ninja              # .cl -> ._vsm -> .vsm
ninja build-e2e    # + assemble3 + ホストの C
ninja test-emu-lib # emu:lib で実行し、全体が昇順になることを確かめる
ninja test-device  # 実機で実行（実機で PASS を確認）
```

## 注意（同時に使う `long` の値の数）

この kernel は、同時に生きる `long`（i64）の値が多い（`id >> j` が 12 個）。i64 を置くレジスタ（XREG）が足りないと、
コンパイルの誤り（`同時に使う値が GRF0 / GRF1 / LM0 / LM1 に入りきらない（register class: XREG）`）で止まる。mncl-kit 0.3.5 の XREG は 32 本で、この example は 16 本以上が要る。
