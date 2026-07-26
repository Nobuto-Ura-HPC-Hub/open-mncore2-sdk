# MNCL Examples

MNCL コンパイラの使用例。OpenCL C (.cl) から _vsm を生成し、E2E テストまで実行する。

## 構成

| # | 内容 | 計算 | host C |
|---|------|------|--------|
| 01-vecadd | c[i] = a[i] + b[i] | 1D, 4096 PE | 4096 要素 |
| 02-vecadd-2d | 同上 | 2D (64x64) | 4096 要素 |
| 03-vecadd-3d | 同上 | 3D (16x16x16) | 4096 要素 |
| 04-vecadd-3d-batch | 同上 | 3D, N ラウンド | 4096*N 要素 (N=2, argv 指定可) |
| 05-stencil1d | c[i] = left + self + right | 1D, 4096 PE (boundary_flags) | 4096 要素 |
| 06-vecadd-f32 | c[i] = a[i] + b[i] (f32) | 1D, 4096 PE | 4096 要素 |
| 07-reduce-add | sum 縮約 (reduce_add) | 1D, 4096 PE → 4 partial | u64 x 4096 入力、 u64 x 4 出力 |
| 08-reduce-max | max 縮約 (reduce_max) | 1D, 4096 PE → 4 partial | u64 x 4096 入力、 u64 x 4 出力 |
| 09-abs | c[i] = \|a[i]\| (if/else) | 1D, 4096 PE | 4096 要素 |
| 10-odd-even-sort | 奇偶転置ソート 1 turn = 偶数 + 奇数フェーズ (in-place) | 1D, 4096 PE (boundary_flags) | 4096 要素、 swap 回数を reduce_add で集計、 3 テスト (desc / sorted / dup) |
| 11-vecdiv-with-newton-raphson | c[i] = a[i] / b[i] (Newton-Raphson) | 1D, 4096 PE | 4096 要素 |
| 12-vecadd-i64 | c[i] = a[i] + b[i] (i64 整数。XREG の add=ladd) | 1D, 4096 PE | 4096 要素 |
| 13-odd-even-sort-gid | 10 の get_global_id 版。左メンバ判定を host flag でなく PE が `((id&1)==0)` で自前計算 | 1D, 4096 PE (boundary_flags) | 4096 要素、 3 テスト (desc / sorted / dup)。例 10 と同一結果 |
| 14-odd-even-sort-full | 13 の full 版。host が even/odd を収束まで回して完全ソート (get_global_id + inline reduce_add) | 1D, 4096 PE (boundary_flags) | 4096 要素、 収束後 qsort と全一致 + 昇順確認 |

layout :any のため MNCL の ._vsm 出力は 1D/2D/3D で同一。.stparam の pe_shape のみ異なる。
04 はホスト側でカーネルを N 回呼び出し、4096*N 要素を処理する。
06 は f32 単精度版で、 ホスト側で f32 を u64 上位 32-bit にパッキング/アンパッキングする扱い方を示す。
07 / 08 は reduce_add / reduce_max の使い方サンプル。 入力ファイル指定 or 標準入力で u64 x 4096 を取り、 `--verify` 指定時は C で再計算して PASS/FAIL を表示、 `--verify` なしで device 結果を u64 x 4 のバイナリとして標準出力に出す（`xxd -g 1` 等で表示できる形式）。

10 は奇偶転置ソートの 1 turn、 すなわち偶数フェーズ (ペア (0,1),(2,3),...) と奇数フェーズ (ペア (1,2),(3,4),...) を 1 回ずつ実行する例。 full なソート (turn を収束まで繰り返す) ではない。 kernel は 2 フェーズで同一で、 差は host が渡す flag だけ。 boundary_flags は両フェーズ共通に clamp パターンを 1 回だけ送る (奇数はペアが PE グループ境界を跨ぐので clamp が必須。 両端の PE は self 比較となり swap しない)。 data は in/out 共用のアドレスに置くのでフェーズ間で device 上に残り、 偶数の結果がそのまま奇数の入力になる。 swap 回数は reduce_add で 4 partial に集計し、 host 側の C 参照実装と照合する。 full なソートは `_mncore2-sdk-v1/share/examples/vsmlink/24-odd-even-sort-reduce` を参照。

host driver は 3 つのテストを上から順に流し、 最後にサマリを出す。 `desc` は降順で全ペアが swap する。 `sorted` は既ソートで swap が 1 度も起きない経路を通す。 `dup` は隣接ペアが等値な入力で、 等値では swap しないこと (kernel の `>` が strict であること) を検査する。 等値要素は swap しても値が変わらないので、 `dup` は data の比較では差が出ず swap 回数の照合だけが検出手段になる。 3 テストの値域は互いに重ならないので、 前のテストの data が device 上に残ったまま次のテストが PASS する事故は起きない。 期待値はハードコードせず、 すべて C 参照実装が計算する。

11 は割り算 `c[i] = a[i] / b[i]` を Newton-Raphson で実装する例。 MN-Core2 のハードには除算命令が無く、 逆数平方根の近似命令 `drsqrt` (5 bit 精度) しか無い。 そこで次の手順で除算を実現する:

1. `drsqrt(b*b)` で `1/|b|` の粗い種 (5 bit) を作り、 `b` の符号を掛け戻して `1/b` の初期近似を得る
2. Newton-Raphson 反復 `x = x * (2 - b * x)` を 5 回まわす。 1 反復ごとに有効桁が約 2 倍になり、 5 bit の種から full double (53 bit) 精度に到達する
3. 最後に `a` を掛けて `c = a * (1/b)`

emu:lib で `max_rel_err=2.220e-16` (double のマシンイプシロン級) が出る。 本来コンパイラ backend が `fdiv` を自動でこう展開すべきだが、 その自動化は今後の課題。 現状は kernel 内に NR を手書きして凌ぐ形。

12 は 10 と同じ 1 step の奇偶転置ソートに reduce と stdin/stdout binary I/F を付けた例だが、 ホスト側 loop でソートし切る部分 (loop 化) は未完で、 現状は 1 step のみ。 full なソートは 11-odd-even-sort ではなく上記 vsmlink 24 を参照。

## 使い方

```bash
# 1. SDK 環境を有効化
source <SDK_ROOT>/bin/activate

# 2. examples を作業ディレクトリにコピー
sdk-examples mncl ~/work
cd ~/work/mncl/01-vecadd

# 3. MNCL コンパイル (.cl -> ._vsm)
ninja

# 4. E2E ビルド (vsmlink + assemble3 + host C)
ninja build-e2e

# 5. テスト実行 (emu:lib)
ninja test
```

## パイプライン

```
vecadd.cl                          [ユーザ記述]
    |  ninja (default)
    v
_build/vecadd._vsm                [MNCL: clang]
    |  ninja build-e2e
    v
_build/vecadd.vsm                 [vsm-linker]
_build/vecadd.asm                  [assemble3]
_build/vecadd.idma.dat             [assemble3 --loader]
_build/test_vecadd                 [gcc + libmnc2]
    |  ninja test
    v
PASS: c[i] == a[i] + b[i]         [emu:lib]
```

## 前提

SDK に以下の kit がインストール済みであること:
- mncl-kit (clang, opencl-c.h)
- vsmlink-kit (vsmlink CLI)
- mncore2-emuenv-kit (assemble3)
- libmnc2-kit (mnc2.h, libmnc2.a)
