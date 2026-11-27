# 36-bitonic-sort: バイトニックソート（4096 個、f64）

4096 個の f64 の値を、PE の番号の昇順に並べるバイトニックソートである。比較・交換の相手は、PE の番号の 1 つのビットを反転した PE
（超立方体の隣）なので、`@get_hypercube_neighbor` で受け取る。

`11-odd-even-sort` の奇偶交換ソートは、隣の PE（`@get_neighbor`）と比べ、ホストが kernel を切り替えながら繰り返す。
この example は、**1 つの kernel に全ステップを並べる**。ホストは kernel を 1 回実行するだけである。

## アルゴリズム

4096 = 2^12 個なので、段 s は 1〜12、各段のステップ j は s−1 から 0 まで、全体で 78 ステップである。

段 s、ステップ j で、PE i は次のことをする。

1. 相手 `i XOR 2^j` の PE の値を受け取る（`@get_hypercube_neighbor`、`<deep>` は j）
2. i の bit j と bit s が等しければ、自分の値と相手の値の小さい方を、そうでなければ大きい方を、新しい値にする
   （bit s が 0 の PE は昇順、1 の PE は降順に並べる。s = 12 は、全 PE の bit 12 が 0 なので、すべて昇順になる）

```asm
	@alloc hc :omr 9 :grf0 200 202 204 206 208 :n 0
	@get_hypercube_neighbor $ls16 <j> $lm0 $lr8 nbuf0 hc    ; 相手の値を $ls16 に
	@free hc
	dmin $lm0 $ls16 $lr24                                   ; 小さい方（fp64）
	dmax $lm0 $ls16 $lr32                                   ; 大きい方（fp64）
	...                                                     ; take_min の PE だけ、小さい方に差し替える
	lpassa $lr40 $lm0                                       ; 新しい値
```

「小さい方を取るか」は、`M = 2^j + 2^s` として `t = i & M` を作り、**`t == 0` または `t == M`** のときに取る。
`lxor` で、`t == 0` のときと `t == M` のときに `omr` が 1 になるようにし、最初に大きい方を置いたうえで、`maskr` で
その PE にだけ小さい方を上書きする。`$lr8` は `@identify` で配った PE の番号 i である
（PE j に値 j が入る前提で、ホストが PDM の j 番目に j を置く）。

`gen_vsm.py` が、全 78 ステップの `_vsm` を作る（手で書かない）。

## 期待値

ホスト（`sort_host.c`）が、kernel と同じ規則で、先頭の `<steps>` ステップを計算した配列を作り、kernel の結果と
**4096 個すべて**を比べる。`<steps>` が 78 のときは、あわせて、結果が入力を昇順に並べたものと一致することを確かめる。

入力は、-2^32 以上 2^32 未満の f64 の疑似乱数で、**有限で 0 でない値**（正と負の両方）である。比較は、f64 の値としての
大小である。`dmin` / `dmax` は、0 を同一視し、NaN の扱いが特別なので、0 と NaN は入れない。

## 実行

```bash
ninja                   # vsmlink のみ（全 78 ステップ）
ninja build-e2e         # + assemble3 + C ビルド
ninja test-emu-lib      # emu:lib で 78 ステップを実行し、全体が昇順になることを確かめる（5 秒ほど）
ninja test-device       # 実機で 78 ステップを実行し、全体が昇順になることを確かめる
ninja verify            # 短い版（3 ステップ）の vsmlink の出力と expected.vsm の diff
```

78 ステップには、`<deep>` が 0〜11 のすべてが含まれる。

`expected.vsm` は、全体の出力が大きくなるので、短い版（3 ステップ）のものである。

## PDM の配置（`sort.param`）

| slot | 用途 | `:addr`（u64） |
|---|---|---|
| 8 | PE の番号（`@identify`）。0〜4095 | 0 |
| 16 | 入力（`@distribute`） | 4096 |
| 24 | 出力（`@collect`、`:recv_wait_tag` #x1e） | 8192 |
| `nbuf0` | `@get_hypercube_neighbor` の一時バッファ（deep 9〜11 で使う。4160 u64） | 16384 |
