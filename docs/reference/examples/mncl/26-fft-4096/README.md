# 26-fft-4096: FFT（4096 点、複素 f64、周波数間引き型、基数 2）

vsmlink の `40-fft-4096` に当たる MNCL の example である。相手の値は `get_hypercube_neighbor` で受け取り（`24-bitonic-sort` と同じ）、
複素数の乗算に **`mad`（積和）** を使う。`mad` の説明は `share/mncl/mad.md`。

## 計算

PE の番号 i の PE が 1 点 x_i（実部 xr、虚部 xi）を持つ。段 j（j = 11 から 0）で、PE i は相手 i XOR 2^j の値 y を受け取る
（`get_hypercube_neighbor` を、実部と虚部で 1 回ずつ）。

| i の bit j | 新しい値 |
|---|---|
| 0 | x + y |
| 1 | (y − x) · w、w = exp(−2πi · (i mod 2^j) / 2^(j+1)) |

j = 0 では w = 1 なので、乗算をしない。入力は自然順、出力は**ビット反転順**（PE p が X[bitrev(p)] を持つ）である。

```c
double dr = yr - xr, di = yi - xi;
double qr = mad(dr, w.x, -(di * w.y));   // dr * wr - di * wi
double qi = mad(dr, w.y, di * w.x);      // dr * wi + di * wr
```

`get_hypercube_neighbor` の `deep`（ここでは j）は定数でなければならない。段ごとに j を定数で書くため、`STAGE` マクロを 11 回並べる
（`24-bitonic-sort` のように `#pragma unroll` で書かないのは、段ごとに配る w の表（kernel 引数）が違うため。kernel 引数は、ポインタの配列では渡せない）。

## w の表

w の表（j = 1〜11）は、ホストが `cos` と `sin` で作り、段ごとの kernel 引数（`_arg_tw1`〜`_arg_tw11`）で渡す。`distribute2` で、wr が成分 x、wi が成分 y に届く。
kernel の中で w は計算しない（f64 の `cos` と `sin` は使えない）。

## `mad` の丸めの性質

**`mad` は、正確に丸めた積和（`fma`）ではない。** MN-Core 2 の f64 の積和は、仮数部の下位 16 ビット同士の部分積を省略し、補正項 2^-74 で置き換える
（dev manual 4.3。詳細は `share/mncl/mad.md`）。そのため、結果は numpy や DFT の定義通りの計算とビット単位では一致しない。
この example の判定は、ビット一致ではなく、ノルム誤差で行う。

## 判定（許容誤差）

kernel の出力と、定義の通りの DFT（`long double`）の結果を、ノルム誤差で比べる。基準は、kernel を実行する前に決めた次の式で、実測で調整しない
（vsmlink の `40-fft-4096` と同じ式）。

```
‖X_kernel − X_ref‖2 ≤ C · log2(N) · 2^-53 · ‖X_ref‖2     C = 16
```

- 2^-53 は丸めの単位である。基数 2 の FFT の相対ノルム誤差は O(log2(N) · u) である（Higham, Accuracy and Stability of Numerical Algorithms 第 24 章）。
  C = 16 は、その係数に参照側の誤差を加えても収まる余裕として決めた
- 要素ごとの相対誤差は使わない。ほとんどの周波数の値が小さくなり、相対誤差が成り立たないため

emu:lib での実測（参考。判定には使っていない）: 4096 点の疑似乱数で、相対誤差 4.7e-16（上限 2.1e-14）。

## ホストがすること（`test_fft-4096.c`）

1. PE の番号 `[0..4095]` を、`.param` の `:identify` の番地（addr 0）へ送る
2. 入力（実部 4096 個、虚部 4096 個。[-1, 1) の疑似乱数、種は固定）を slot 8 へ送る
3. w の表 11 枚（各 wr 4096 個、wi 4096 個）を slot 16、24、…、96 へ送る
4. kernel を実行し、出力（slot 104）を受け取る
5. PE p の値を X[bitrev(p)] として、参照の DFT とノルム誤差を比べる

## PDM の配置（`fft-4096.param`）

| slot | 用途 | `:addr`（u64） |
|---|---|---|
| `:identify` | PE の番号（4096 個） | 0 |
| 8 | 入力（実部 4096 個、虚部 4096 個） | 4096 |
| 8 + 8j（j = 1〜11） | 段 j の w の表（wr 4096 個、wi 4096 個） | 12288 + (j − 1) · 8192 |
| 104 | 出力（実部 4096 個、虚部 4096 個） | 12288 + 11 · 8192 |
| `:buffer nbuf0` | `get_hypercube_neighbor` の deep 9〜11 が使う作業領域（4160 u64） | 12288 + 12 · 8192 |

## 実行

```bash
ninja              # .cl -> ._vsm -> .vsm
ninja build-e2e    # + vsmlink + assemble3 + ホストの C
ninja test-emu-lib # emu:lib で実行し、ノルム誤差を判定
ninja test-device  # 実機で実行（実機の確認は未実施）
```
