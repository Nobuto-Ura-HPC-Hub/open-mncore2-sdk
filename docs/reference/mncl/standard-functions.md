# 標準関数と拡張 API の対応状況（mncl-kit 0.3.5）

OpenCL C の標準関数のうち、**ここに「使える」と書いた関数だけが使える**。書いていない関数を呼ぶと、**コンパイルの誤りで止まる**
（誤った値を黙って返すことはない）。`float`（f32）の版と、ベクタ（`double2`〜`double16`、`float2`〜`float16`）の版は、
「使える」と書いたもの以外は使えない。個々の使い方は `api-reference.md`。

## 1. 標準関数（数学関数ほか）

| 関数 | `double`（スカラ） | `double2`〜`4` | `float`（スカラ） | `float` のベクタ | 備考 |
|---|---|---|---|---|---|
| `sin`、`cos` | 使える | 使えない | 使えない | 使えない | 最大相対誤差 約 2.2e-16（\|x\| < 約 1.6e6）。1 回の呼び出しで命令が約 220 行増える |
| `floor` | 使える | 使えない | 使えない | 使えない | `-∞` 側への丸め |
| `mad`（`a * b + c`） | 使える | 使えない | 使えない | 使えない | 正確に丸めた積和（`fma()`）と一致しない場合がある（`mad.md`） |
| `rsqrt` | 使える | 使える | 使えない | 使えない | `double2`、`double3`、`double4` |
| `fma` | 使えない | 使えない | 使えない | 使えない | 積和は `mad` を使う |
| `fabs`、`fmax`、`fmin` | 使えない | 使えない | 使えない | 使えない | 絶対値、最大・最小は `if` / `else` で書く（example `09-abs`） |
| `sqrt`、`copysign`、`trunc`、`ceil`、`round` | 使えない | 使えない | 使えない | 使えない | |
| `exp`、`log`、`pow`、`tan` | 使えない | 使えない | 使えない | 使えない | |
| `convert_*`（`convert_double()` など） | 使えない | 使えない | 使えない | 使えない | `(double)i` のキャストは使える（範囲の制限は `api-reference.md` の「既知の制限」） |

- 演算子は、`double` と `float` のスカラで `+`、`-`、`*`、`/`、単項の `-` が使える（`/` の精度は `api-reference.md` の「既知の制限」）。`float` の値を比較で選ぶ三項演算子と、`float` から `double` への変換（`(double)x`）も使える。`float2` と `float8` は `+`、`-`、`*`、単項の `-`、スカラとの積、要素の取り出しと組み立てが使える（`api-reference.md` の「`float2` / `float8`」）。`double2` / `double3` / `double4` は、加算・減算・乗算とスカラとの掛け算が使える（`rsqrt` は下の表。`api-reference.md` のベクタの節）
- 上の表に無い OpenCL の標準関数は、すべて使えない

## 2. MN-Core 2 の拡張 API

| API | `double` | `float` | `long` | 備考 |
|---|---|---|---|---|
| `distribute` | 使える | 使える（`float2` も） | 使える | |
| `distribute2`〜`4` | 使える | 使えない（`float8` の `distribute4` は使える。`api-reference.md` の「`float2` / `float8`」） | 使えない | |
| `collect` | 使える | 使える（`float2` も） | 使える | |
| `collect2`〜`4` | 使える | 使えない（`float8` の `collect4` は使える） | 使えない | |
| `distribute4_aos2soa`、`collect4_soa2aos` | 使える（`double4`） | 使えない | 使えない | 転置つき |
| `broadcast` | 使える | `float2` だけ使える | 使える | |
| `broadcast`（`double2`〜`double4` の配列を渡す形） | 使える（`double2`、`double3`、`double4` を返す） | 使えない | 使えない | 渡す型で配る値の数が決まる（`broadcast2` などの名前の関数は無い） |
| `load_neighbor`、`get_neighbor` | 使える | 使える（`get_neighbor` は `float2` も） | 使えない | |
| `get_eoshift` | 使える | 使える（`float2` も） | 使えない | |
| `load_neighbor_periodic`、`load_neighbor_boundary`、`get_neighbor_periodic`、`get_neighbor_boundary` | 使える | 使える（`get_neighbor_*` は `float2` も） | 使えない | 端の扱いを選ぶ版（0.3.5） |
| `get_eoshift_periodic`、`get_eoshift_boundary` | 使える | 使える（`float2` も） | 使えない | 端の扱いを選ぶ版（0.3.5） |
| `get_hypercube_neighbor` | 使える | 使える | 使えない | |
| `get_global_id` | — | — | 戻り値が `long` | PE のグローバル ID を返す |
| `emit_from`（デバッグ用） | 使える | 使えない | 使えない | |
| `nop`（デバッグ用） | — | — | — | |

## 3. 縮約（`reduce_*`）

| API | `double` | `float` | 整数 |
|---|---|---|---|
| `reduce_add`（加算） | 使える | 使えない | 使えない |
| `reduce_max`（最大値） | 使える | 使えない | 使えない |
| `reduce_min`（最小値）ほか、ビット演算や論理演算の縮約 | 使えない | 使えない | 使えない |

縮約は、全 PE の値に対する演算で、結果は PDM に書かれる（`api-reference.md` の「縮約 API」）。
