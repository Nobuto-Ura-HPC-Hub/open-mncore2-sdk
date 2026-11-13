# 30-soa2aos: collect の SoA 出力を device 上で AoS に並べ替える

`@collect` の出力は成分ごと (SoA) `[x×4096][y×4096][z×4096][m×4096]` に固定される。一方
`@broadcast size 4` は 1 命令 (v で 4 サイクル) で 1 粒子の struct を配れて効率が良いが、源が
**AoS (1 粒子の x,y,z,m が連続 4 u64)** でないと読めない。この並びの差を、**device 上で**埋める。

このカーネルは SoA を AoS の `struct {x,y,z,m}[]` に並べ替える。ホストは出力をそのまま struct
配列として読める。並びは `share/vsmlink/directives-spec.md` の「@distribute_aos2soa / @collect_soa2aos」。

## 仕組み

```asm
@distribute _a from_param 8 size 4 $lm0    # SoA を読み、各 PE に struct {x,y,z,m}
dmwrite $lm0v $lx0                          # MAB 内 4x4 転置 (書き込み。src に v が必須)
dmread  $lx0 $lm8v                          # 転置読み出し (dst にも v が必須)
@collect _b to_param 16 size 4 $lm8         # AoS で PDM へ
```

- `distribute size 4` が SoA から各 PE に 1 粒子の struct を載せる。
- `dmwrite`/`dmread` が MAB 内の 4 PE x 4 アドレスを転置する (dev manual 3.6.10.1 / 3.6.11.1)。
  **要点は src/dst 両方に `v`** (サイクルインクリメント。無いと全サイクル同番地に上書きで壊れる。
  dev manual 3.6.1.6)。
- `collect size 4` で書き戻すと、成分ごとではなく **粒子ごと (AoS)** で PDM に並ぶ。

## なぜ位置の入れ替えが SoA ↔ AoS になるのか

**SoA と AoS は、同じ 2 次元の表を、どちらの向きに 1 列へ潰すかの違いである。**

粒子 p を PE p が持ち、その PE の 4 語が x, y, z, w だとする。

**`dmwrite` / `dmread` を通す前**（各 PE が 1 粒子を丸ごと持つ）

| | 語 0 | 語 1 | 語 2 | 語 3 |
|---|---|---|---|---|
| **PE0** | x0 | y0 | z0 | w0 |
| **PE1** | x1 | y1 | z1 | w1 |
| **PE2** | x2 | y2 | z2 | w2 |
| **PE3** | x3 | y3 | z3 | w3 |

**通したあと**（各 PE が 1 成分だけを 4 粒子ぶん持つ）

| | 語 0 | 語 1 | 語 2 | 語 3 |
|---|---|---|---|---|
| **PE0** | x0 | x1 | x2 | x3 |
| **PE1** | y0 | y1 | y2 | y3 |
| **PE2** | z0 | z1 | z2 | z3 |
| **PE3** | w0 | w1 | w2 | w3 |

### 入れ替わったのは「粒子番号」と「成分の添字」

| | PE 番号が表すもの | 語の位置が表すもの |
|---|---|---|
| 前 | **粒子番号** | 成分（x,y,z,w）|
| 後 | **成分** | 粒子番号 |

**同じ 2 つの番号のうち、どちらを PE 番号に割り当てるかが交換されている。**
値そのものは加えも掛けもされず、**位置だけが移る**。添字を交換する操作なので「転置」と
呼ばれる（dev manual 3.6.10.1 / 3.6.11.1）。

### 命令だけでは完結しない

**命令が届くのは 4 PE x 4 語の範囲だけ**である。表で言えば 4 行 4 列の正方形にあたる。

粒子が 4096 個なら表は 4096 行 4 列で、正方形ではない。**命令は各所の 4x4 を入れ替える
だけで、表全体の読み替えにはならない。** 残りは `@distribute` と `@collect` の経路が担う。

添字で書くと分かりやすい。要素の添字は 14 bit（4096 粒子 x 4 成分 = 16384 要素）である。

```
SoA の並び:  上位 2 bit = 成分、  下位 12 bit = 粒子番号
AoS の並び:  上位 12 bit = 粒子番号、下位 2 bit = 成分
```

つまり **SoA → AoS の変換そのものは、14 bit の添字を 2 bit 左に回すこと**である
（全 16384 要素で確認した）。

**命令が動かせるのは、そのうち成分の 2 bit と粒子番号の下 2 bit だけ**で、残りの 10 bit を
どう動かすかは配り方が決める。

**だから同じ命令を使っても、配り方が違えば PDM 上の並びは変わる。** これが「命令の効果」と
「経路の効果」を分けて書く理由である（並びの式は `share/vsmlink/directives-spec.md` の「@distribute_aos2soa / @collect_soa2aos」）。

なお、このカーネルの出力は **AoS になっているが粒子の並びが入れ替わっている**（下の
「レイアウト変換のたびに PDM がどう並ぶか」を参照）。**理想的な SoA → AoS そのものでは
ない。** 理想の変換は上の 2 bit 回転で、位数は 7 になる。実際に観測される順列の位数 6 とは
別のものである。

**並びの式と注意は `share/vsmlink/directives-spec.md` の「@distribute_aos2soa / @collect_soa2aos」にまとめてある。**

### レイアウト変換のたびに PDM がどう並ぶか

入力を `x[p]=1000+p, y[p]=2000+p, z[p]=3000+p, m[p]=4000+p` として実測した結果である。

**1 回通したあと。** 位置 k を struct 4 個として読むと、そこには元の要素 `perm_src(k)` が入る。

| 位置 k | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 1024 | 1025 | 2048 | 3072 | 4095 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 元の要素 | 0 | 4 | 8 | 12 | 16 | 20 | 24 | 28 | 1 | 5 | 2 | 3 | 4095 |

```
perm_src(k) = 4 * (k % 1024) + k / 1024
```

**全 4096 位置でこの式と一致する**ことを確認してある。

**2 回通したあと。** 元に戻る。**バイト単位で一致する**ことを確認してある。

```bash
./_build/soa2aos --gen > soa.bin
./_build/soa2aos --pipe < soa.bin  > a1.bin
./_build/soa2aos --pipe < a1.bin   > a2.bin
cmp soa.bin a2.bin        # 一致する
```

よって同じツールで **SoA -> AoS も AoS -> SoA** もできる（1 回で変換、2 回で元）。

### N < 4096 のときの読み方

この example は 4096 粒子で動かす。粒子数が 4096 より少ないデータをこの変換に通すと、使っている粒子は
先頭に集まらず、**位置 0、1024、2048、3072 から始まる 4 本の区間に分かれる**（上の `perm_src` のとおり、
位置 k < 1024 には粒子 4k が来る）。読む側は 4 区間を別々に読む必要がある。実例は
読む側が slot を 4 つ使い、4 区間をそれぞれ読む形になる。

この変換を directive にした `@distribute_aos2soa` / `@collect_soa2aos` の対
（`share/vsmlink/directives-spec.md`、`examples/34-soa2aos-roundtrip`）は、配る側にも転置を入れることで
PDM の上では常に番号の順に置くので、この分かれ方は起きない。この example の手で書いた kernel は、
転置そのものの参照として残す。

### 先頭だけを見て判断しないこと

**`k < 1024` では `perm_src(k)` は単に `4k` になる。** 位置 1 に要素 4 が来るので、先頭だけを
見ても並べ替えがかかっていることは分かる。ただし、先頭 1024 個だけでは単純な 4 刻みに見え、
式の `k / 1024` の項は表れない。上の表に `1024` 以降を入れてあるのはそのためである。

**実際に取り違えた例がある。** N 体のアプリを作るとき、先頭数個だけを
見て順列の性質を誤認し、値が合わない原因を長く取り違えた。

### 2 回で戻るのは、この構成に限る

このカーネルは **PDM から `@distribute` で読み直して**レイアウト変換し、`@collect` で PDM へ書く。
だから 2 回適用すると変換が 2 回積み重なり、打ち消し合って元に戻る。

**レイアウト変換にかける値を PDM から読み直さない構成では戻らない。** LM の常駐値をそのままレイアウト変換する
場合（`examples/33-broadcast-soa2aos` のループや N 体のアプリ）、「PE p が要素 p を持つ」という
対応が毎回同じなので、**毎回同じ順列が 1 回かかるだけ**で積み重ならない。

**`dmwrite`/`dmread` そのものは同じでも、PDM に何が並ぶかは前段で決まる。**

### 粒子順は順列になる

`out[4k..4k+3]` は粒子 k の struct とは限らないが、**各 struct は壊れない**（x,y,z,m が
揃って動く）。all-to-all では全粒子を broadcast して各 PE が合算するので、**順序は問わない**。

## 使い方

```
source scripts/overlay
ninja -C examples/30-soa2aos test-emu-lib   # emu:lib で自己テスト (テストデータで自己完結)
ninja -C examples/30-soa2aos test-device    # 実機で自己テスト
```

### フィルタ (--pipe)

`--pipe` で stdin から SoA を読み、stdout に AoS を書く Unix フィルタになる。device pod (実機、
LD_LIBRARY_PATH 不要) なら:

```
cat soa.bin | ./_build/soa2aos --pipe > aos.bin
```

- テスト入力の生成: `./_build/soa2aos --gen > soa.bin`
- 往復 (2 回通すと元に戻ることの確認): `./_build/soa2aos --gen | ./_build/soa2aos --pipe | ./_build/soa2aos --pipe`
  で元の SoA に戻る。

### --use-global-id (m を on-device の global_id で上書き)

`--use-global-id` を付けると、SoA -> AoS に加えて、各 PE が自分の **global_id (flat_id 0..4095) を
on-device で計算**して m を上書きする別カーネル (`aos_id._vsm`) を使う。**id のために distribute を
使わない** (ホストが id 配列を用意しない)。出力 struct の m は「その粒子を処理した PE の flat_id」に
なる。付けなければ m は保持 (デフォルトは今までどおり)。

- `ninja -C examples/30-soa2aos test-emu-lib-id` — 自己テスト (m が 0..4095 の順列)。
- フィルタ: `cat soa.bin | ./_build/soa2aos --pipe --use-global-id > aos_id.bin`。
- flat_id = `peid + (l1bid<<6) + (l2bid<<9)` を `lpassa`/`imm`/`ilsl`/`iadd` で合成 (`24/init.vsm`
  が手本)。固定値オペランド (`$peid` 等) は ALU 第 1 入力専用なので `lpassa` 退避。
- 整数 -> double は専用命令 (itof) が無いのでマジックナンバー法: `immu f"176"` (176.0(f32)=0x43300000
  を immu の xoxo で 2^52(f64) に) + `lor` + `dvadd`。id <= 4095 は仮数部に収まり誤差なし。

## 形式

いずれも float64 little-endian、4096 粒子 (= 16384 double = 131072 byte)。

| | 並び |
|---|---|
| SoA (入力) | `[x×4096][y×4096][z×4096][m×4096]` |
| AoS (出力) | `struct { double x, y, z, m; } [4096]` (粒子順は順列) |

## ファイル構成

| ファイル | 役割 |
|---------|------|
| `aos._vsm` | distribute size4 + `dmwrite`/`dmread` 転置 + collect size4 (m 保持) |
| `aos_id._vsm` | 上に on-device flat_id 計算 + m 上書きを足した `--use-global-id` 版 |
| `aos.param` | slot 8 (SoA 入力)、slot 16 (AoS 出力) の PDM 番地と wait tag (両カーネル共用) |
| `aos.stparam` | `:pe_shape (4096)` `:pe_local (4)` |
| `soa2aos.c` | 引数なし=自己テスト (CI)、`--pipe`=フィルタ、`--gen`=テスト入力生成 |
| `build.ninja` | vsmlink + assemble3 + host ビルド + test target |
