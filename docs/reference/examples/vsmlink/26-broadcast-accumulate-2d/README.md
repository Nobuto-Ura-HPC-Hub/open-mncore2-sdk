# 26-broadcast-accumulate-2d: distribute / broadcast / collect を使った 2D 質量重み付き総和

各 PE が自分の星を持ち (distribute)、他の星を順に broadcast で配って総和を累積する。
directive が実アプリで使えることを、N 体と同じデータの流れで確認する骨格である。

**重力ではない。** 逆二乗則の `1/r³` は落としてある。理由は次節に書く。

## rsqrt を使わない

**装置側は加算と乗算だけで組み、golden とビット一致で照合する。** これが本 example の
設計の中心である。

MN-Core 2 の倍精度は長語 64 bit、符号 1 / 指数部 11 / 仮数部 52 で、通常の範囲では
IEEE 754 に従う (dev manual の浮動小数点数フォーマットの節)。`dvadd` は `x + y`、
`dvmul` は `dvfma` の第 3 入力を 0 とした `x * y` である。したがって加算と乗算だけで
組んだ計算は、ホストが同じ順序で同じ演算をすればビット一致で再現できる。

一方 `rsqrt` は「精度は 5 ビット程度」の近似であり (dev manual の rsqrt の節)、これが
唯一のビット不一致要因になる。`rsqrt` を使うと golden 照合に許容誤差が要り、その幅が
実装の誤りも一緒に吸収してしまう。

実際、以前の実装は `rsqrt` を使っていたため許容値が 15% あり、softening eps2 を配る
`@broadcast` が丸ごと機能しなくてもテストが PASS する状態だった。検証対象の directive が
壊れても気づけない。

そこで `rsqrt` を外し、次を計算する。

```
ax_i = Σ_j  m_j * (x_i - x_j)
ay_i = Σ_j  m_j * (y_i - y_j)
```

重力ではないが、全対全総和のデータの流れは N 体と同じである。確認したい経路
(distribute で自粒子、broadcast で他粒子、exec をまたいだ累積、collect で回収) は
すべて通る。照合は許容誤差ゼロで、1 ビットでも違えば FAIL する。

`1/r³` を落としたので softening (eps2) も持たない。ゼロ除算が起きないためである。

## 星の配列からすべての並びを導出する

ホストは星の配列を 1 つだけ持ち、そこからすべてを導出する。

```c
struct star { double x, y, m; };
struct star stars[4096];
```

`@distribute size N` は成分でまとめた並びを、`@broadcast size N` は 1 粒子の成分が連続した
並びを要求する。したがって同じ配列から 2 通りの並びを作ることになる。

| 用途 | 並び | PDM 番地 |
|---|---|---|
| `@distribute size 2` (位置) | `[x を 4096 個][y を 4096 個]` | 0 |
| `@broadcast size 2` (位置) | j 番目の `[x, y]` | 12288 |
| `@broadcast size 1` (質量) | j 番目の `[m]` | 12352 |
| `@collect size 2` | `[ax を 4096 個][ay を 4096 個]` | 131072 |

golden も同じ配列から、装置と同じ演算順序で計算する。浮動小数点の加算は結合則が
成り立たないため、順序が違うと下位ビットがずれる。

## 質量の扱い

質量は位置とは別の directive で配る。位置は j ごとに変わり、質量は星ごとに決まる値で
性質が違うためである。

**自粒子の質量は配らない。** 総和 `Σ m_j * (x_i - x_j)` が使うのは他粒子の質量 `m_j`
だけで、自分の質量 `m_i` を参照する場面が無いためである。加速度が力を受ける側の質量に
依存しないのは物理としてそうなっており、逆二乗則のままでも同じである。

配ったところで結果に効かないので、その `@distribute` はテストで検証されない状態になる。
使わないものは配らない、という判断で外してある。

**broadcast の供給源に distribute の領域を使うことはできない。** `@broadcast` は
`.param` で固定された番地しか読めず j 番目を選べないので、PDM に置いた 4096 個から
j 番目を取り出すことができない。ホストが j ごとに `m[j]` を固定番地へ送る。

同じ理由で「質量は 1 回送れば済む」も実現できない。カーネルは j を知る手段がなく、
常駐させた質量配列から j 番目を選べないためである。

## 自己相互作用を除外しない

j が自分自身のとき `x_i - x_j` が 0 になり、寄与が `m_j * 0` で厳密にゼロになる。
`rsqrt` を使わないので発散もしない。golden 側も同じ式なので自然に一致する。
除外処理は書いていない。

先頭 7 星を他粒子として回すので、PE 0 から 6 では実際に j == i の回が発生する。

## 速度を持たない

N 体の中心は力の評価 (全対全の総和) であり、そこに速度は現れない。速度が要るのは
時間積分 (`v += a*dt`、`x += v*dt`) で、これは各 PE が独立に積和をするだけの部分である。
本 example は総和の評価までを対象とするので、星は速度を持たない。

物理としては不自然だが、directive の確認という目的には足りる。

## `NJ` について

他粒子として回す個数である。全対全なら 4096 だが、send と exec を 4096 組実行すると
エミュレータでのテスト時間が現実的でないため 7 で打ち切っている。lit は `-j 1` 固定なので
CI 全体に影響する。

**`NJ` の値そのものに構造的な意味は無い。** 打ち切りの数であって、2D の次元数や
broadcast の size とは無関係である。切りの良い数は構造的な数と誤解されやすいので
素数を置いてある。

## カーネル

| ファイル | 役割 |
|---------|------|
| `init._vsm` | 自粒子の位置 (x,y) を `@distribute size 2` で LM 常駐、 accumulator ax,ay を 0 |
| `accumulate._vsm` | 他粒子の位置を `@broadcast size 2`、質量を `@broadcast size 1` で受け、 総和を LM の ax,ay に累積 |
| `collect._vsm` | LM 常駐の ax,ay を `@collect size 2` で PDM へ回収 |
| `test_accumulate.c` | 星の配列を作り、 init 1 回 + accumulate NJ 回 + collect 1 回、 golden とビット一致照合 |

ホストが accumulate を NJ 回実行する間、各 PE の自粒子と accumulator は LM に保持される
(exec 間の状態保持。 前例は 24-odd-even-sort-reduce)。

fp64 命令のバンク規則 (各オペランドを別バンクに置く等) は
`docs/host-loop-accumulation.md` 第 6 章を参照。

## 記録として保持しているもの

- `dist_bcast` — distribute と broadcast の辻褄確認 (組み立て初期の段階)
- `landing._vsm` + `test_landing.c` — l1bmp の着地レジスタを emu:lib で実測
  (`ninja test-landing`)。broadcast の成分がどのレジスタに載るかの根拠

## ビルド・実行

```
source scripts/overlay
ninja -C examples/26-broadcast-accumulate-2d build-e2e      # vsmlink + assemble3 + C ビルド
ninja -C examples/26-broadcast-accumulate-2d test-emu-lib   # emu:lib で実行・照合
ninja -C examples/26-broadcast-accumulate-2d test-device    # 実機で実行
```

## 今後

- 全対全化 (他粒子を 4096 星すべてにする)
- 時間積分 (星に速度を持たせて `v += a*dt` と `x += v*dt` を入れる)
- 各 PE が他粒子の別成分を持つ配置 (msl/msr + mask) の検討
- 3D 対応
