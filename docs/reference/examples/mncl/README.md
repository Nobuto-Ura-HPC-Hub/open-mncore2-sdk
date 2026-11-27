# MNCL Examples

MNCL コンパイラの使用例。OpenCL C (.cl) から _vsm を生成し、E2E テストまで実行する。

## 構成

| # | 内容 | 計算 | host C |
|---|------|------|--------|
| 01-vecadd | c[i] = a[i] + b[i] | 1D, 4096 PE | 4096 要素 |
| 02-vecadd-2d | 同上（01 と同じ kernel と build） | 1D, 4096 PE | 4096 要素 |
| 03-vecadd-3d | 同上（01 と同じ kernel と build） | 1D, 4096 PE | 4096 要素 |
| 04-vecadd-3d-batch | 同上 | 3D, N ラウンド | 4096*N 要素 (N=2, argv 指定可) |
| 05-stencil1d | c[i] = left + self + right | 1D, 4096 PE (boundary_flags) | 4096 要素 |
| 06-vecadd-f32 | c[i] = a[i] + b[i] (f32) | 1D, 4096 PE | 4096 要素 |
| 07-reduce-add | sum 縮約 (reduce_add) | 1D, 4096 PE → 4 partial | u64 x 4096 入力、 u64 x 4 出力 |
| 08-reduce-max | max 縮約 (reduce_max) | 1D, 4096 PE → 4 partial | u64 x 4096 入力、 u64 x 4 出力 |
| 09-abs | c[i] = \|a[i]\| (if/else) | 1D, 4096 PE | 4096 要素 |
| 10-odd-even-sort | 奇偶転置ソート 1 turn = 偶数 + 奇数フェーズ (in-place) | 1D, 4096 PE (boundary_flags) | 4096 要素、 swap 回数を reduce_add で集計、 3 テスト (desc / sorted / dup) |
| 11-vecdiv-with-newton-raphson | c[i] = a[i] / b[i] (Newton-Raphson) | 1D, 4096 PE | 4096 要素 |
| 12-vecadd-i64 | c[i] = a[i] + b[i] (i64 整数。`long`) | 1D, 4096 PE | 4096 要素 |
| 13-odd-even-sort-gid | 10 の get_global_id 版。左メンバ判定を host flag でなく PE が `((id&1)==0)` で自前計算 | 1D, 4096 PE (boundary_flags) | 4096 要素、 3 テスト (desc / sorted / dup)。例 10 と同一結果 |
| 14-odd-even-sort-full | 13 の full 版。host が even/odd を収束まで回して完全ソート (get_global_id + inline reduce_add) | 1D, 4096 PE (boundary_flags) | 4096 要素、 収束後 qsort と全一致 + 昇順確認 |
| 15-broadcast | broadcast の最小デモ。host のある1つの値vを全4096 PEに配り、out[id] = v + id | 1D, 4096 PE | 4096 要素 |
| 16-broadcast-f64 | out[i] = v + w + x[i]。1カーネルに broadcast を2本置く確認（f64） | 1D, 4096 PE | 4096 要素 |
| 17-nbody-2d | 2D 重力多体（f64）。各 PE が1粒子、position/forceを double2 でベクタ計算 | 1D, 4096 PE | 4096 要素 |
| 18-broadcast-vec | broadcast の size N 確認。1回の broadcast で double3 を配る | 1D, 4096 PE | 4096 要素 |
| 19-nbody-3d | 3D 重力多体（f64）。position/velocity を double3 で保持し、semi-implicit Euler で時間積分 | 1D, 4096 PE | 4096 要素 |
| 20-persistent | `persistent` の変数を 2 つの kernel で共有する。`init` で書き、`accum` を 3 回実行して累計を取る | 1D, 4096 PE, kernel 2 つ | 4096 要素 x 4 回 |
| 21-nop | デバッグ用の待ち `nop(n)`。01-vecadd の加算の前に `nop(8)` を置く。結果は 01 と同じ | 1D, 4096 PE | 4096 要素 |
| 22-hypercube-neighbor | 超立方体の隣の PE の値を受け取る `get_hypercube_neighbor`。c[j] = a[j] + 10*a[j^1] + 100*a[j^1024]（deep 0 と 10） | 1D, 4096 PE | 4096 要素 |
| 23-emit-from-debug | デバッグ用の `emit_from`。broadcast の値を 100 回足し、HW PE ID 817 と 529 の PE の値を、足すたびに書き出す | 1D, 4096 PE | 4096 要素 |
| 24-bitonic-sort | バイトニックソート（4096 個、全 78 ステップ。`get_hypercube_neighbor`、`#pragma unroll`） | 1D, 4096 PE | 4096 要素 |
| 25-bitonic-sort-8192 | バイトニックソート（8192 個、全 91 ステップ。各 PE が 2 つ持つ。`distribute2` / `collect2`） | 1D, 4096 PE | 8192 要素 |
| 26-fft-4096 | FFT（4096 点、複素 f64、周波数間引き型。`get_hypercube_neighbor`、複素数の乗算に `mad`（積和）） | 1D, 4096 PE | 4096 点（ノルム誤差で判定） |
| 27-fft-4096-sincos | 26 と同じ FFT。w の表をホストではなく、kernel の中の `sin` と `cos` で作る（`sin` / `cos` は f64 のスカラ） | 1D, 4096 PE | 4096 点（ノルム誤差で判定） |
| 28-persistent-array | `persistent` の `double` の配列（8 要素）を 2 つの kernel で共有する。`init` で書き、`step` を 3 回実行して各要素を 2 倍し、合計を取る | 1D, 4096 PE, kernel 2 つ | 4096 要素 x 8 個 x 4 回 |
| 29-float2-float8 | f32 を u64 に 2 つずつ詰める `float2`（u64 1 つ）と `float8`（u64 4 つ。1 命令で 8 要素）。要素の入れ替えとスカラ倍、`float8` の積と差 | 1D, 4096 PE | 4096 要素 x 2 + 4096 要素 x 8 |
| 30-neighbor-edge | 隣の値の端の扱い。3 点の和を clamp（既定）、周期境界（`load_neighbor_periodic`）、固定の境界値（`load_neighbor_boundary`）で計算し、32 x 128 の格子の上隣を `get_eoshift_periodic` で受け取る | 1D, 4096 PE (boundary_flags) | 4096 要素 x 4（端の PE を含めて照合） |
| 31-heat-diffusion-2d | 2 次元の熱拡散（ヤコビ法、4 回の更新）。32 x 128 の格子の上下左右を `get_eoshift_boundary` で受け取り、格子の外側を温度 1.0 に固定する。外周の判定は要らない | 2D (32x128), 4096 PE | 4096 要素（外周を含めてビット単位で照合） |

27 は `sin` / `cos`（f64 のスカラ。最大相対誤差 約 2.2e-16、|x| < 約 1.6e6。`share/mncl/api-reference.md`）の例。26 の w の表（ホストが作る）と同じ精度のノルム誤差になる。

26 は `mad`（f64 の積和）の例。**`mad` は正確に丸めた積和（`fma`）ではない**ので、判定はビット一致ではなくノルム誤差で行う（`share/mncl/mad.md`、`26-fft-4096/README.md`）。

02 と 03 は、01 と同じ kernel、同じ build、同じホストのプログラムである（表示する名前だけが違う）。
04 はホスト側でカーネルを N 回呼び出し、4096*N 要素を処理する。
06 は f32 単精度版で、 ホスト側で f32 を u64 上位 32-bit にパッキング/アンパッキングする扱い方を示す。
07 / 08 は reduce_add / reduce_max の使い方サンプル。 入力ファイル指定 or 標準入力で u64 x 4096 を取り、 `--verify` 指定時は C で再計算して PASS/FAIL を表示、 `--verify` なしで device 結果を u64 x 4 のバイナリとして標準出力に出す（`xxd -g 1` 等で表示できる形式）。

10 は奇偶転置ソートの 1 turn、 すなわち偶数フェーズ (ペア (0,1),(2,3),...) と奇数フェーズ (ペア (1,2),(3,4),...) を 1 回ずつ実行する例。 full なソート (turn を収束まで繰り返す) ではない。 kernel は 2 フェーズで同一で、 差は host が渡す flag だけ。 `load_neighbor` の既定は、端の PE に自分の値を返す (clamp) ので、 両端の PE は self 比較となり swap しない。 boundary_flags は両フェーズ共通に 1 回だけ送る。 data は in/out 共用のアドレスに置くのでフェーズ間で device 上に残り、 偶数の結果がそのまま奇数の入力になる。 swap 回数は reduce_add で 4 partial に集計し、 host 側の C 参照実装と照合する。 収束まで回す full なソートは `14-odd-even-sort-full`（vsmlink-kit の example では `share/examples/vsmlink/24-odd-even-sort-reduce`）を参照。

host driver は 3 つのテストを上から順に流し、 最後にサマリを出す。 `desc` は降順で全ペアが swap する。 `sorted` は既ソートで swap が 1 度も起きない経路を通す。 `dup` は隣接ペアが等値な入力で、 等値では swap しないこと (kernel の `>` が strict であること) を検査する。 等値要素は swap しても値が変わらないので、 `dup` は data の比較では差が出ず swap 回数の照合だけが検出手段になる。 3 テストの値域は互いに重ならないので、 前のテストの data が device 上に残ったまま次のテストが PASS する事故は起きない。 期待値はハードコードせず、 すべて C 参照実装が計算する。

11 は割り算 `c[i] = a[i] / b[i]` を Newton-Raphson で実装する例。 MN-Core2 のハードには除算命令が無く、 逆数平方根の近似命令 `drsqrt` (5 bit 精度) しか無い。 そこで次の手順で除算を実現する:

1. `drsqrt(b*b)` で `1/|b|` の粗い種 (5 bit) を作り、 `b` の符号を掛け戻して `1/b` の初期近似を得る
2. Newton-Raphson 反復 `x = x * (2 - b * x)` を 5 回まわす。 1 反復ごとに有効桁が約 2 倍になり、 5 bit の種から full double (53 bit) 精度に到達する
3. 最後に `a` を掛けて `c = a * (1/b)`

emu:lib で `max_rel_err=2.220e-16` (double のマシンイプシロン級) が出る。 コンパイラは、割り算 `/` を同じ形（逆数の Newton-Raphson）に自動で展開する。この example は、その展開を kernel の中に手で書いた形で、仕組みを示す。


20 は `persistent` 修飾子の最小の例。1 つの `.cl` に kernel を 2 つ書き、`persistent double acc;` を共有する。`init` が `acc` に初期値を書き、`accum` が `acc` に入力を足して結果を返す。ホストは `init` を 1 回、`accum` を 3 回実行し、毎回の結果がそれまでの累計と一致することを確かめる。kernel が複数なので、vsmlink は kernel ごとの `.vsm` を出す（下の「パイプライン」）。値が kernel の実行をまたいで残るのは emu:lib と実機で、emu:process では残らない。

21 はデバッグ用の待ち `nop(n)` の例（`21-nop/README.md`）。

22 は `get_hypercube_neighbor(v, id, deep)`（超立方体の隣の PE の値を受け取る）の例（`22-hypercube-neighbor/README.md`）。

23 は、デバッグ用の `emit_from(name, pe, p, v)` の例である。実機では PE の中の途中の値が見えないので、HW PE ID が `pe` の PE の値を、呼ぶたびに PDM の `p` の続きへ書き出す。ホストは、`collect` の完了を待って kernel の終わりを知り、その後で、書き出された列を読む。100 回呼ぶので、PE の内部の専用の領域が溢れて PDM へ書かれる経路も通る。`.param` の書き先の slot には待ちのタグを付けず、後ろにほかの slot を置かない。使い方の決まりは `share/mncl/api-reference.md` の「`emit_from`」の節

24 と 25 は、`get_hypercube_neighbor` を使ったバイトニックソートである。24 は 4096 個（各 PE が 1 つ）、25 は 8192 個（各 PE が 2 つ。a は要素 j、b は要素 j + 4096。`id + 4096` を b に渡して、要素の番号でそろえる）。どちらも `#pragma unroll` で、段 s とステップ j のループを展開して、`deep` を定数にする。詳しくは各 example の `README.md`

30 は、隣の PE の値を受け取る関数の端の扱い（clamp、周期境界、固定の境界値）の例（`30-neighbor-edge/README.md`）。

31 は、`get_eoshift_boundary` で 2 次元の格子の上下左右の値を受け取り、格子の外側の温度を固定した熱拡散の例（`31-heat-diffusion-2d/README.md`）。

## 使い方

```bash
# 1. SDK 環境を有効化
source <SDK_ROOT>/bin/activate

# 2. examples を作業ディレクトリにコピー
sdk-examples mncl ~/work
cd ~/work/mncl/01-vecadd

# 3. kernel の build（.cl -> _vsm -> .vsm -> .idma.dat。clang、vsmlink、assemble3）
ninja

# 4. ホストのプログラムも build する
ninja build-e2e

# 5. 実行して、結果を照合する
ninja test-emu-lib    # エミュレータ（emu:lib）
ninja test-device     # 実機
```

emu:lib は、エミュレータをホストのプログラムの中で動かす実行先である（libmnc2 の実行先は、実機、emu:lib、emu:process の 3 つ。`share/mnc2/usage.md`）。
`ninja test` は、この 2 つの使い方を表示するだけである。

## パイプライン

```
vecadd.cl                          [ユーザ記述]
    |  ninja (default)
    v
_build/vecadd._vsm                [MNCL: clang]
_build/vecadd.vsm                 [vsmlink。vecadd.param と]
_build/vecadd.asm                  [assemble3]
_build/vecadd.idma.dat             [assemble3 --loader]
    |  ninja build-e2e
    v
_build/test_vecadd                 [gcc + libmnc2]
    |  ninja test-emu-lib
    v
PASS: c[i] == a[i] + b[i]         [emu:lib]
```

1 つの `.cl` に kernel が複数あるとき（例: 20-persistent）は、`_vsm` は 1 つのまま、vsmlink の出力先がディレクトリになり、
kernel ごとの `.vsm` が出る。そのあとの assemble3 とホストでの読み込みは kernel ごとに行う。

```
persistent.cl                              [ユーザ記述。kernel が init と accum の 2 つ]
    |  ninja (default)
    v
_build/persistent._vsm                     [MNCL: clang。2 つの kernel を含む]
    |  ninja build-e2e
    v
_build/multikernel/persistent-init.vsm     [vsmlink。kernel ごとに 1 つ]
_build/multikernel/persistent-accum.vsm
_build/init.idma.dat, _build/accum.idma.dat [assemble3 --loader]
```

## 前提

SDK に以下の kit がインストール済みであること（mncl-kit の `requires.txt` と同じ）:
- sdk-base-kit 0.1.13 以降（`activate`、`sdk-examples`）
- libmnc2-kit 0.4.3 以降（mnc2.h, libmnc2.a）
- vsmlink-kit 0.10.3 以降（vsmlink）
- mncl-kit（clang, opencl-c.h）
