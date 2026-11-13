# vsmlink examples: .stparam チュートリアル

`.stparam` は構造パラメタ（structural parameters）を S 式で記述するファイル。
vsmlink コマンドに対してステンシルの形状・PE 配置・境界条件を伝える。

フィールド一覧・境界条件・変更履歴は [`stparam-spec.md`](../../vsmlink/stparam-spec.md) を参照。

## pe_shape

4096 PE をどういう形状で使うかの指定。積は 4096。

```lisp
(:pe_shape (4096))        ; 1D
(:pe_shape (64 64))       ; 2D
(:pe_shape (16 16 16))    ; 3D
```

独立計算（vecadd 等）では pe_shape を変えても計算結果は同じ。
アプリケーションの配列次元に合わせて設定する。

## examples

### 01-roundtrip — DMA いってかえって

最小構成。将来 E2E (host-dma 連携) の起点。

### 02-vecadd — 1D ベクトル加算

pe_shape (4096) の 1D 独立計算。golden data による検証あり。

```bash
vsmlink vecadd._vsm vecadd.param vecadd.stparam out.vsm
diff out.vsm expected.vsm   # 一致すれば OK
```

### 03-vecadd-2d — 2D ベクトル加算

同一カーネルを pe_shape (64 64) で実行。data_layout は影響しない。

### 04-vecadd-3d — 3D ベクトル加算

同一カーネルを pe_shape (16 16 16) で実行。data_layout は影響しない。

### 05-stencil1d — 1D 3 点ステンシル

全フィールド使用。boundary, variables を指定。golden data による検証あり。

### 06-jacobi2d — 2D 5 点（十字型）

パース確認用。2D のオフセット配置の例。

### 07-box2d — 2D 9 点（箱型）

パース確認用。対角を含む 8 方向オフセット。

### 08-himeno3d — 3D 7 点

パース確認用。姫野ベンチマーク相当。変数 7 つ。

### 09-dmaid-conflict — DMA タグ衝突の検証

dmaid=0x23 のタグ衝突が仕様であることの検証。

### 10-mem-default — :mem 省略（デフォルト :pdm0）

01-roundtrip と同一カーネルだが、.param で `:mem` を省略。
`:mem` なしでも `:pdm0` として動作することの検証。

### 11-odd-even-sort — Odd-Even Transposition Sort

各 PE が左右の隣接 PE とデータを比較し、奇数/偶数フェーズを交互に実行してソートする。
cross MAB・cross L1B を段階的に構築する step0〜step7 を含む。

### 12-l1b-crossing — cross L1B テスト

隣接 L1B からデータを取得するテスト。L2BM 経由の 5 段階データパスを示す。

### 13-l2b-crossing — cross L2B テスト

cross L2B のデータ取得。L2BM はセクション局所なので PDM 経由の collect/redistribute が必要。

### 14-stencil1d-debug — 1D ステンシル袖交換チュートリアル

PE シフト・MAB 越え・L1B 越え・L2B 越え・セクション越えの 5 段階を個別に観察できるデバッグ用。

### 15-mask-safety-T4 — ネスト if-then-else + @get_neighbor

maskr を使ったネスト分岐のテスト。フラグ組み合わせで異なる値を設定する「初期値 + 例外上書き」パターン。ただしこのパターンは if/then/else の中身を外に出すため、副作用で破壊を起こす問題がある。正しい if/then/else に要修正。

### 16-mask-safety-T4-debug — 15 のデバッグ版

debug_read で GRF0 を直接読む。emu:process または実機が必要（emu:lib は 0 を返す）。

### 17-boundary-collect — @boundary_flags collect

`@boundary_flags`（distribute 版）で各 PE の HW レジスタから計算した 8bit 境界フラグを
@collect で PDM に回収し、ホスト側で期待値と照合する。

### 18-bf-verify — @boundary_flags 検証（golden override → stencil）

外部 golden data で境界フラグを上書きし、stencil 結果で検証する。
golden が正しければ PASS、全ゼロなら境界 PE で結果がずれて FAIL。

### 19-collect-x3 — 3 点ステンシル（collect × 3）

output[i] = left[i] + center[i] + right[i] の 1D 3 点ステンシル。
@boundary_flags でホストから配布した境界フラグを使用。

### 20-stencil1d-distribute — 1D ステンシル（distribute 版）

19 と同等だが、ホスト事前計算の境界フラグを配布するベースライン構成。

### 21-get-neighbor — @get_neighbor テスト

全階層（MAB 内、cross MAB、cross L1B、cross L2B）での隣接値取得を検証する。

### 22-identify-reduce-add — @identify + @reduce パイプライン

@identify で PE ID [0..4095] を各 PE に配布し、@reduce :liadd で全 PE の整数加算 reduce を実行。
4 部分和（PE position 0〜3）を個別に検証する最小パイプライン。

### 23-identify — @identify サンプル

@identify directive の最小サンプル（PE 座標配布）。

### 24-odd-even-sort-reduce — odd-even sort + reduce

odd-even sort と @reduce を組み合わせたパイプライン。

### 25-broadcast-reduce-add — @broadcast (l1bmp) + @reduce

放送命令 l1bmp による全 PE 同値放送を @broadcast で行い、@reduce :liadd で検証する。

### 26-broadcast-accumulate-2d — 2D 質量重み付き総和（distribute / broadcast / collect）

各 PE が自分の星を持ち、他の星を @broadcast で順に配って総和を LM に累積する。distribute（自粒子 size 2）と broadcast（他粒子 size 2 と size 1）と collect（結果 size 2）を実アプリで使う骨格。N 体と同じデータの流れだが、rsqrt を使わず加算と乗算だけで組み、golden とビット一致で照合する。

### 27-roundtrip-size4 — @distribute / @collect 往復（成分数可変）

`@distribute`と`@collect`の往復を、成分数（size）を引数で変えながら検証する。他の example と異なりこのディレクトリに専用README.mdは無い。詳細は`test_roundtrip4.c`のコメントを参照。

### 28-nbody-3d — 3D N 体（distribute / broadcast / collect + rsqrt 重力）

3D N 体の example。段階実装で、段階 1〜7 まで実装済み（N=4096 まで実機 PASS）。段階 1 では各 PE が自粒子 (x,y,z,m) を持ち、他粒子を @broadcast size 4 で配って softening 付き重力の加速度を collect size 3 で回収する。rsqrt をそのまま使い、ホスト倍精度と緩い許容誤差で照合する。

### 29-broadcast-collect — broadcast + collect の往復

`@broadcast`で全 PE に配った struct を各 PE で加工し、`@collect`で PDM に集めて回収する最小の往復 example。集める側のレイアウトとハードウェア経路の説明は `share/vsmlink/directives-spec.md` の「PDM 上の並び」。

### 30-soa2aos — collect の SoA 出力を device 上で AoS に並べ替える

`@collect`の出力（成分ごと SoA）を、device 上で`struct {x,y,z,m}[]`（AoS）に並べ替えるカーネル。`dmwrite`/`dmread`による MAB 内 4x4 転置を使う。並びは `share/vsmlink/directives-spec.md` の「@distribute_aos2soa / @collect_soa2aos」。

### 31-nbody-host-mediated-loop — kernel 数の N 非依存と host 不介在ループは両立しない

`@kernel_begin`/`@kernel_end` による kernel 分割を、単体テストの外で実アプリケーション規模の入力に対して（将来 MNCL が複数 kernel 入りの `_vsm` を出すことを見据えて）検証する目的で作った example。28-nbody-3d を基礎に、accumulate（全対全の力計算）を「CHUNK 粒子ずつ固定 PDM 領域を読む 1 個の kernel」にし、host が「送信 -> exec」を繰り返す形に書き換えた。手書き kernel 6 個は本来 1 個ずつ別々に vsmlink へかければ済むが、あえて 1 本の `_vsm` に合成してから分割し直している（詳細は本 example の README.md）。28 は分割ごとに別々の kernel を生成していた（kernel 数が N に依存するが host 不介在）のに対し、31 は kernel 数が N に依存しない代わりに host が毎回データを詰め替える。この trade-off が MN-Core2 の本質的な制約（`@broadcast` の PDM 番地が link 時固定、ループを持たない静的スケジュール）に由来することを、作業を進める中で発見した。速度はホストへ回収しない。

### 32-nbody-runtime-link — host が実行時に vsmlink を呼び直す 3D N 体

28・31 と同じ 3D 重力の全対全 N 体を題材に、28（ビルド時に必要な数だけ kernel を生成）・
31（1 個の kernel を使い回し host がデータを送り直す）とも異なる第三の構成として、
全粒子を 1 回だけ PDM へ送り、host が chunk ごとに `accumulate_PH` kernel の
`@broadcast` スロット番号（まだ確定していない）を埋めて `vsmlink`（+ `assemble3`）を
実行時に呼び直す構成を実装した。emu:lib での実行・値照合まで確認済み（N=32）。
詳細は本 example の README.md。

## .param 形式

`.param` の仕様は [`param-spec.md`](../../vsmlink/param-spec.md) を参照。

## API での使い方

```c
#include "vsmlink.h"

vsmlink_structure_t st = {0};
int rc = vsmlink_parse_stparam_file("kernel.stparam", &st);
if (rc != VSMLINK_SUCCESS) { /* error */ }

// st.ndim, st.pe_shape, st.pe_local 等が使える

vsmlink_free_structure(&st);
```
