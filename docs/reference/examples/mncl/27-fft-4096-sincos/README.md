# 27-fft-4096-sincos: FFT の w の表を、kernel の中の `sin` と `cos` で作る

`26-fft-4096` と同じ FFT（4096 点、複素 f64、周波数間引き型、基数 2）である。違いは w の作り方だけで、
26 はホストが `cos` と `sin` で作った表を kernel 引数で渡すが、27 は **kernel の中で `sin(x)` と `cos(x)`（f64 のスカラ）から作る**。
計算と判定は 26 の README を参照。`sin` と `cos` は `share/mncl/api-reference.md` の「`sin` / `cos` / `floor`」の節。

## w の作り方

PE の番号 id の PE が、段 j で使う w = cos(a) + i·sin(a)、a = −π · (id mod 2^j) / 2^j を、kernel の中で作る。

```c
double a = (double)(id & ((1 << j) - 1)) * (-PI / (double)(1 << j));
double wr = cos(a);
double wi = sin(a);
```

26 のホストの表（`-2π (i mod 2^j) / 2^(j+1)` を double で計算して `cos` と `sin`）と、a は同じ double になる
（π の定数との積と、2 のべきの割り算だけなので、丸めは同じ）。

## 精度

- `sin` と `cos` は、最大相対誤差が約 2.2e-16 以内（`fdiv` と同じ目標）。|x| < 約 1.6e6 の範囲で正しい。この example の a は −π から 0 の範囲
- emu:lib での実測（参考。判定には使っていない）: 4096 点の疑似乱数で、ノルム誤差 4.6e-16（26 は 4.7e-16。上限 2.1e-14）。
  w をデバイスで計算しても、ホストの表と同じ精度である

## 26 との違い

| | 26-fft-4096 | 27-fft-4096-sincos |
|---|---|---|
| w | ホストが作り、kernel 引数 11 個（`_arg_tw1`〜`_arg_tw11`）で渡す | kernel の中で `sin` と `cos` から作る |
| kernel 引数 | 13 個（入力、w の表 11 枚、出力） | 2 個（入力、出力） |
| PDM の使用量 | w の表 11 枚（各 8192 u64）を置く | 置かない |
| 命令の数 | 少ない | 段ごとに `sin` と `cos` の展開が入る（`_vsm` の行がおよそ 220 行ずつ） |

## PDM の配置（`fft-4096-sincos.param`）

| slot | 用途 | `:addr`（u64） |
|---|---|---|
| `:identify` | PE の番号（4096 個） | 0 |
| 8 | 入力（実部 4096 個、虚部 4096 個） | 4096 |
| 16 | 出力（実部 4096 個、虚部 4096 個） | 12288 |
| `:buffer nbuf0` | `get_hypercube_neighbor` の deep 9〜11 が使う作業領域（4160 u64） | 20480 |

## 実行

```bash
ninja              # .cl -> ._vsm -> .vsm
ninja build-e2e    # + vsmlink + assemble3 + ホストの C
ninja test-emu-lib # emu:lib で実行し、ノルム誤差を判定
ninja test-device  # 実機で実行（実機の確認は未実施）
```
