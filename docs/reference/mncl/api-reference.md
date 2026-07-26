# mncl-kit API Reference (0.3.3)

MNCL コンパイラ kit が OpenCL C カーネルから呼び出せる API の一覧。

## 概要

`mncl-kit` は OpenCL C (`.cl`) を MN-Core 2 用の `_vsm` に変換する clang を提供する。
カーネル内では通常の OpenCL C 構文に加え、以下の MNCL 拡張 API が使える。

これらは `include/mncl/opencl-c.h` で宣言されている。`-include <PREFIX>/include/mncl/opencl-c.h`
で先読みすると、ユーザコードからそのまま呼び出せる。

## データ転送 API

### `distribute` — PDM から PE 各レーンに値を配る

```c
double distribute(const __global double *p);
float  distribute(const __global float  *p);
long   distribute(const __global long   *p);
```

PDM 上の配列 `p` を全 PE に分配し、各 PE に対応する 1 要素を返す。
PE インデックスと配列インデックスの対応は `.stparam` の `pe_shape` / `layout` で決まる。

`float` 版は `u64` の上位 32-bit に格納される（下位 32-bit は未定義）。
`long` 版は 64-bit 整数をそのまま扱う。

### `collect` — PE 各レーンの値を PDM に集める

```c
void collect(__global double *p, double v);
void collect(__global float  *p, float  v);
void collect(__global long   *p, long   v);
```

各 PE の `v` を PDM 上の配列 `p` に書き戻す。`distribute` と対の操作で、
`pe_shape` / `layout` が一致している必要がある。

### `neighbor` — 隣接 PE の値を取得する

```c
double neighbor(const __global double *p, int offset);
float  neighbor(const __global float  *p, int offset);
```

`distribute(p)` が配ったのと同じ配置で、自 PE の `offset` 個ずれた位置の値を返す。
`offset` は 1D の場合は隣 PE への距離（負も可）、多次元の場合は線形化されたインデックス。

境界の扱いは vsmlink が `@boundary_flags` で生成するパターン（クランプ等）に依存する。

## 実行時 ID API

### `get_global_id` — PE のグローバル ID を返す

```c
long get_global_id(uint dim);
```

各 PE のグローバル ID（0..要素数-1）を `long`（64-bit 整数）で返す。標準 OpenCL の `get_global_id` だが、
MN-Core2 は 64-bit 整数がネイティブなので `long` を返す。host が flag を配らずに、PE 自身が役割
（例: ペアの左メンバか）を `((id & 1) == 0)` のように計算するのに使える。

**host 側の準備が要る**: vsmlink の `@identify` で PE 番号を配るため、host は ID 配列 `[0..要素数-1]` を
`.param` の `:identify` が指すアドレスへ事前送信する必要がある（送らないと全 PE が 0 になる）。
使用例は `13-odd-even-sort-gid` / `14-odd-even-sort-full`。

## 縮約 API

### `reduce_add` — 全 PE で加算縮約して PDM に書く

```c
void reduce_add(__global double *out, double v);
```

全 PE の `v` を加算し、PDM 上の `out` に書き込む。`out` は 4 要素の配列で、
PE position 別の部分和が 4 つ書かれる（PE i は group `i % 4` に属する）。
最終合計はホスト側で 4 要素を加算する。

### `reduce_max` — 全 PE で最大値縮約

```c
void reduce_max(__global double *out, double v);
```

`reduce_add` と同じ I/F で、加算の代わりに最大値を取る。

### サポート状況一覧

MNCL の縮約 builtin と、 内部で使用する vsmlink `@reduce` の operator の対応:

| MNCL builtin | 型 | vsmlink operator | 状態 |
|---|---|---|---|
| `reduce_add(double*, double)` | f64 加算 | `dfadd` | ✅ |
| `reduce_max(double*, double)` | f64 最大 | `dmax` | ✅ |
| (float 版 reduce_add) | f32 加算 | `ffadd` | 未提供 |
| (float 版 reduce_max) | f32 最大 | `fmax` | 未提供 |
| (reduce_min 系) | f64/f32 最小 | `dmin` / `fmin` | 未提供 |
| (整数 reduce 系) | i16/i32/i64 各種 | `siadd` / `iiadd` / `liadd` 等 | 未提供 |

vsmlink 自身は計 24 種の `@reduce` operator を持つが、 MNCL builtin として公開しているのは
上記の `reduce_add` / `reduce_max` のみ。 他は必要時に追加する。

## clang の呼び出し例

```bash
clang -cc1 -O1 \
    -triple=mncore2-unknown-unknown -target-cpu mncore2 \
    -cl-std=CL2.0 \
    -fno-builtin \
    -include $SDK_ROOT/include/mncl/opencl-c.h \
    -S kernel.cl -o kernel._vsm
```

詳細な使い方は `share/examples/mncl/` を参照。

## 既知の制限

- `fdiv` の精度は `drsqrt` 5 bit による近似（最大 5.6% 誤差）。高精度が必要な場合は Newton-Raphson を上位で実装する
- f64 即値で下位 32-bit が非ゼロの場合は未対応
- 制御フロー: `if/else` / `select` 対応済み。`for`/`while` のループは limited（ヘッダ展開が必要なケースあり）

## 変更履歴

- **0.3.3**:
  - `broadcast` の複数個対応: `double2` / `double3` / `double4` を 1 回で全 PE に配る
  - `distribute` / `collect` の複数個対応: `distribute2` / `distribute3` / `distribute4`、`collect2` / `collect3` / `collect4`（1 回で複数個を配る・回収する。要 vsmlink-kit 0.9.0 以降）
  - `double2` / `double4` のベクタ演算に対応: 加算・乗算・減算・`rsqrt`、およびスカラとの掛け算
  - 使用例を追加: `15-broadcast`、`16-broadcast-f64`、`17-nbody-2d`（2D 重力多体を double2 で計算）、`18-broadcast-vec`
  - `_vsm` ヘッダに生成元（clang バージョン・ビルドコミット・ビルド日）を記録
  - i64（`long`）整数の基本サポート: `distribute` / `collect` の `long` オーバーロード、加算・論理積、`== 0` / `!= 0` の等値比較
  - `get_global_id` が `long`（64-bit 整数）を返すよう変更（host が flag を配らなくても PE 自身が役割を判定できる）
  - 64-bit ポインタに対応（DataLayout を p:64:64 に）
  - 使用例を追加: `12-vecadd-i64`（i64 加算）、`13-odd-even-sort-gid`（get_global_id で左メンバ判定）、`14-odd-even-sort-full`（host ループで奇偶転置ソートを完全ソートまで）
  - f64 の条件分岐（`if`/`else`）と比較の不具合を修正（一部の比較で誤った結果を返す問題を解消）
  - f32 加算のレジスタ割り当てを改善（余分なコピーを削減）
- **0.3.2**:
  - f32 全演算（fadd/fsub/fmul/fneg/fdiv）対応
  - `distribute` / `collect` / `neighbor` の `float` オーバーロード追加
  - `reduce_add` / `reduce_max` 追加（全段縮約、PDM 出力）
  - GRF1 を MAU 演算の入力に使うレジスタ割り当てに対応（性能向上）
- **0.3.1**: `@get_neighbor` 直接出力、fix-stencil 後処理を廃止
- **0.3.0**: SDK v1（vsmlink 0.7.0）対応版
