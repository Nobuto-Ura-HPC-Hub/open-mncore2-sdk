# 40-fft-4096: FFT（4096 点、複素 f64）

4096 点の複素 f64 の FFT（周波数間引き型、基数 2）である。`36-bitonic-sort` と同じく、相手の値を
`@get_hypercube_neighbor` で受け取る。入力は自然順、出力はビット反転順（PE p が X[bitrev(p)] を持つ）で、
期待値は Python（numpy）で、出力と同じ順に並べて作る。

点数を変えた試作は `proto-8pt/`（8 点）にある。同じ `gen_vsm.py` に点数の指数 m を渡して作る（m は 2〜12）。

## 計算

PE i が 1 点 x_i（実部と虚部）を持つ。段 j（j = 11 から 0）で、PE i は相手 i XOR 2^j の値 y を受け取る。

| i の bit j | 新しい値 |
|---|---|
| 0 | x + y |
| 1 | (y − x) · w、w = exp(−2πi · (i mod 2^j) / 2^(j+1)) |

j = 0 では w = 1 なので、乗算をしない。12 段で、`@get_hypercube_neighbor` を段あたり 2 回（実部と虚部）呼ぶ。
複素数の乗算は、`dvmulu` / `dvfmad` の対（`dvfmau` / `dvfmad` の対）による fp64 の積和で、4 回の実数の積を 8 命令で行う。

## 入力データ

入力は引数のファイルで渡す。形式は、リトルエンディアンの f64 で、複素数 N 点を (re, im) の順に並べたもの
（numpy の complex128 の `tofile` と同じ）である。

```bash
python3 gen_input.py <m> input.bin tw.bin --kind random          # [-1, 1) の疑似乱数（種は固定）
python3 gen_input.py <m> input.bin tw.bin --kind sine --k0 5      # exp(2πi · k0 · n / N)
python3 gen_input.py <m> input.bin tw.bin --kind file --file wave.txt   # 実際の波形
```

`--kind file` の `wave.txt` は、1 列なら実部、2 列なら（実部 虚部）のテキストで、先頭から N 行を使い、足りなければ 0 を補う。
拡張子が `.npy` なら numpy の配列（実数または複素数）を読む。`tw.bin` は w の表で、同時に作る。

```bash
python3 gen_golden.py <m> input.bin golden.bin                    # numpy.fft.fft。kernel の出力の順に並べる
./_build/fft_host _build/fft.idma.dat <m> input.bin tw.bin out.bin   # emu:lib（LD_LIBRARY_PATH は SDK の lib/emu-lib）
python3 check.py <m> out.bin golden.bin
```

## 許容誤差

kernel の出力と golden は、ビット一致ではなく、ノルム誤差で比べる（kernel と numpy は演算の順序が違う）。基準は、kernel を実行する前に
決めた次の式で、実測で調整しない。

```
‖X_kernel − X_golden‖2 ≤ C · log2(N) · 2^-53 · ‖X_golden‖2     C = 16
```

- 2^-53 は丸めの単位である。dev manual 4.3 により、積和の結果は出力の仮数部長への最近接偶数丸めで、`dvadd` は `vfma` の
  第 2 入力を 1 としたもの（3.6.9.1）なので、同じ丸めである。fp64 の積は、仮数部の下位 16 ビット同士の部分積を省略し、補正項 2^-74 を使うが、
  丸めの単位に比べて十分に小さい
- 基数 2 の FFT の相対ノルム誤差は O(log2(N) · u) である（Higham, Accuracy and Stability of Numerical Algorithms 第 24 章）。
  C = 16 は、その係数に golden（numpy）側の誤差を加えても収まる余裕として決めた
- 要素ごとの相対誤差は使わない。単一の正弦波では、ほとんどの周波数の値が理論上 0 になり、相対誤差が成り立たないため。
  要素ごとの比較は、‖X_golden‖2 を単位にした絶対誤差で行い、同じ上限で判定する

emu:lib での実測（参考。判定には使っていない）: 4096 点の乱数で相対誤差 5e-16（上限 2e-14）。

## PDM の配置（`gen_vsm.py` が `.param` を作る）

| slot | 用途 | `:addr`（u64） |
|---|---|---|
| 8 | PE の番号 | 0 |
| 16 | 入力（実部 4096 個、虚部 4096 個） | 4096 |
| 40 + j（j = 1..m−1） | w の表（wr 4096 個、wi 4096 個） | 12288 + (j − 1) · 8192 |
| 24 | 出力（実部 4096 個、虚部 4096 個） | 12288 + (m − 1) · 8192 |

PE のレジスタ: 実部 `$lm0`、虚部 `$lm2`、w は段 j ごとに `$lm(4j)`（wr）と `$lm(4j+2)`（wi）。

## 複素数の乗算（fp64 の積和）

上側の PE の (y − x) · w は、実数の積 4 回と加減算 2 回である。dr = Re(y − x)、di = Im(y − x) として、

```
pr = dr · wr − di · wi
pi = dr · wi + di · wr
```

fp64 の積は、2 命令の対で行う（dev manual 3.6.9.10、3.6.9.11）。`dvmulu` が PE0 と PE1 の積を作り、続く `dvfmad` が PE2 と PE3 の積を
作りながら、前の命令の結果を `$mauf`（MAU 演算結果フォワーディング、3.6.1.15）で通す。`dvfmau` と `dvfmad` の対は、積に第 3 入力を加える。

```asm
dvmulu $lr30 $lm10 $nowrite          # di · wi（PE0、PE1）
dvfmad $lr30 $lm10 $mauf $ls20       # di · wi（PE2、PE3）。$ls20 に全 PE の積が入る
dvfmau $lr28 $lm8 -$ls20 $nowrite    # dr · wr − di · wi（PE0、PE1）
dvfmad $lr28 $lm8 $mauf $lr32        # dr · wr − di · wi（PE2、PE3）。$lr32 に全 PE の結果が入る
```

分かったこと:

- 積の入力が直前の命令の結果のとき、assemble3 は、その間に `nop/2` を置くことを求める。この example では 3 か所である。最初の `dvmulu` の前
  （`di` が直前の `dvadd` の結果）と、`dvfmau` の前の 2 か所（直前の対が `$ls20` と `$ls22` に書いた積を読む）
- `imm f"X"` は fp64 の非零定数を作れない（dev manual 3.2.2）。w の値は、ホストが `cos` と `sin` で表を作り、`@distribute` で配る
- 積和の結果は、出力の仮数部長への最近接偶数丸めである（dev manual 4.3）

## `nop` について

`gen_vsm.py` の `nop` は、assemble3 のハザードチェックが指摘した箇所だけに置いた。複素数の乗算の中の 3 か所に、各 2 ステップ（`nop/2`）である。
マスクを作る部分の `nop/2` と、結果を書き戻す前の `nop` も、外すと指摘されるものだけが残っている。

## 実行

```bash
ninja              # vsmlink のみ（12 段）
ninja build-e2e    # + assemble3 + C ビルド
ninja test-emu-lib # emu:lib で 12 段を実行し、golden と比べる（3 秒ほど）
ninja test-device  # 実機で 12 段を実行し、golden と比べる
ninja verify       # 短い版（8 点）の vsmlink の出力と expected.vsm の diff
```

`expected.vsm` は、全体の出力が大きいので、8 点のものである。
